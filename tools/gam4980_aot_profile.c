#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gam4980_core.h"

#ifndef GAM4980_ENABLE_PROFILING
#error Build this tool with -DGAM4980_ENABLE_PROFILING
#endif

#define DEFAULT_FRAMES 3600u
#define DEFAULT_TOP 30u
#define INITIAL_BUCKETS 4096u
#define INVALID_RECORD ((size_t)-1)
#define MAX_KEY_EVENTS 64u

typedef struct block_record {
    uint64_t key;
    uint64_t entries;
    uint64_t instructions;
} block_record_t;

typedef struct profiler {
    block_record_t *records;
    size_t record_count;
    size_t record_capacity;
    size_t *buckets;
    size_t bucket_count;
    size_t current_record;
    uint64_t total_instructions;
    u32 previous_physical_pc;
    u16 previous_virtual_pc;
    u8 previous_opcode;
    int have_previous;
    int failed;
} profiler_t;

typedef struct key_event {
    unsigned long frame;
    u8 key;
} key_event_t;

static const u8 opcode_lengths[256] = {
    1,2,2,1,2,2,2,2,1,2,1,1,3,3,3,3,
    2,2,2,1,2,2,2,2,1,3,1,1,3,3,3,3,
    3,2,2,1,2,2,2,2,1,2,1,1,3,3,3,3,
    2,2,2,1,2,2,2,2,1,3,1,1,3,3,3,3,
    1,2,2,1,2,2,2,2,1,2,1,1,3,3,3,3,
    2,2,2,1,2,2,2,2,1,3,1,1,3,3,3,3,
    1,2,2,1,2,2,2,2,1,2,1,1,3,3,3,3,
    2,2,2,1,2,2,2,2,1,3,1,1,3,3,3,3,
    2,2,2,1,2,2,2,2,1,2,1,1,3,3,3,3,
    2,2,2,1,2,2,2,2,1,3,1,1,3,3,3,3,
    2,2,2,1,2,2,2,2,1,2,1,1,3,3,3,3,
    2,2,2,1,2,2,2,2,1,3,1,1,3,3,3,3,
    2,2,2,1,2,2,2,2,1,2,1,1,3,3,3,3,
    2,2,2,1,2,2,2,2,1,3,1,1,3,3,3,3,
    2,2,2,1,2,2,2,2,1,2,1,1,3,3,3,3,
    2,2,2,1,2,2,2,2,1,3,1,1,3,3,3,3,
};

static uint64_t hash_key(uint64_t key)
{
    key ^= key >> 30;
    key *= 0xbf58476d1ce4e5b9ULL;
    key ^= key >> 27;
    key *= 0x94d049bb133111ebULL;
    return key ^ (key >> 31);
}

static int profiler_rehash(profiler_t *profiler, size_t bucket_count)
{
    size_t *buckets = (size_t *)calloc(bucket_count, sizeof(*buckets));
    size_t index;

    if (!buckets)
        return 0;
    for (index = 0; index < profiler->record_count; ++index) {
        size_t bucket = (size_t)hash_key(profiler->records[index].key) &
            (bucket_count - 1u);

        while (buckets[bucket])
            bucket = (bucket + 1u) & (bucket_count - 1u);
        buckets[bucket] = index + 1u;
    }
    free(profiler->buckets);
    profiler->buckets = buckets;
    profiler->bucket_count = bucket_count;
    return 1;
}

static size_t profiler_record(profiler_t *profiler, uint64_t key)
{
    size_t bucket;

    if (!profiler->bucket_count &&
        !profiler_rehash(profiler, INITIAL_BUCKETS))
        return INVALID_RECORD;
    if ((profiler->record_count + 1u) * 10u >=
        profiler->bucket_count * 7u &&
        !profiler_rehash(profiler, profiler->bucket_count * 2u))
        return INVALID_RECORD;

    bucket = (size_t)hash_key(key) & (profiler->bucket_count - 1u);
    while (profiler->buckets[bucket]) {
        size_t index = profiler->buckets[bucket] - 1u;

        if (profiler->records[index].key == key)
            return index;
        bucket = (bucket + 1u) & (profiler->bucket_count - 1u);
    }

    if (profiler->record_count == profiler->record_capacity) {
        size_t capacity = profiler->record_capacity
            ? profiler->record_capacity * 2u : 2048u;
        block_record_t *records = (block_record_t *)realloc(
            profiler->records, capacity * sizeof(*records)
        );

        if (!records)
            return INVALID_RECORD;
        profiler->records = records;
        profiler->record_capacity = capacity;
    }
    profiler->records[profiler->record_count].key = key;
    profiler->records[profiler->record_count].entries = 0;
    profiler->records[profiler->record_count].instructions = 0;
    profiler->buckets[bucket] = profiler->record_count + 1u;
    return profiler->record_count++;
}

static int terminates_block(u8 opcode)
{
    if (opcode == 0x00u || opcode == 0x20u || opcode == 0x40u ||
        opcode == 0x4cu || opcode == 0x60u || opcode == 0x6cu ||
        opcode == 0x7cu || opcode == 0x80u)
        return 1;
    if ((opcode & 0x1fu) == 0x10u)
        return 1;
    return (opcode & 0x0fu) == 0x0fu;
}

static void profile_instruction(
    void *context, u16 virtual_pc, u32 physical_pc, u8 opcode
)
{
    profiler_t *profiler = (profiler_t *)context;
    int starts_block = !profiler->have_previous;

    if (profiler->failed)
        return;
    if (!starts_block) {
        u8 length = opcode_lengths[profiler->previous_opcode];
        u16 expected_virtual = (u16)(profiler->previous_virtual_pc + length);
        u32 expected_physical = profiler->previous_physical_pc + length;

        starts_block = terminates_block(profiler->previous_opcode) ||
            virtual_pc != expected_virtual || physical_pc != expected_physical;
    }
    if (starts_block) {
        uint64_t key = ((uint64_t)physical_pc << 16) | virtual_pc;

        profiler->current_record = profiler_record(profiler, key);
        if (profiler->current_record == INVALID_RECORD) {
            profiler->failed = 1;
            return;
        }
        ++profiler->records[profiler->current_record].entries;
    }
    ++profiler->records[profiler->current_record].instructions;
    ++profiler->total_instructions;
    profiler->previous_virtual_pc = virtual_pc;
    profiler->previous_physical_pc = physical_pc;
    profiler->previous_opcode = opcode;
    profiler->have_previous = 1;
}

static int load_file(const char *path, u8 *data, u32 size)
{
    FILE *file = fopen(path, "rb");
    int ok;

    if (!file)
        return 0;
    ok = fread(data, 1, size, file) == size && fgetc(file) == EOF;
    fclose(file);
    return ok;
}

static int load_game(const char *path)
{
    u8 header[GAM4980_GAME_HEADER_SIZE];
    FILE *file = fopen(path, "rb");
    long size;

    if (!file)
        return 0;
    if (fseek(file, 0, SEEK_END) != 0 ||
        (size = ftell(file)) < (long)GAM4980_GAME_HEADER_SIZE ||
        size > (long)GAM4980_GAME_MAX_SIZE ||
        fseek(file, 0, SEEK_SET) != 0 ||
        fread(header, 1, sizeof(header), file) != sizeof(header) ||
        fseek(file, 0, SEEK_SET) != 0 ||
        fread(gam4980_game_storage(), 1, (size_t)size, file) != (size_t)size) {
        fclose(file);
        return 0;
    }
    fclose(file);
    return gam4980_load_game_header(header, (u32)size) > 0;
}

static int ascii_equal(const char *left, const char *right)
{
    while (*left && *right) {
        char a = *left++;
        char b = *right++;

        if (a >= 'A' && a <= 'Z')
            a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z')
            b = (char)(b - 'A' + 'a');
        if (a != b)
            return 0;
    }
    return *left == *right;
}

static int parse_key(const char *name, u8 *key)
{
    static const struct {
        const char *name;
        u8 key;
    } keys[] = {
        {"enter", GAM4980_KEY_ENTER}, {"exit", GAM4980_KEY_EXIT},
        {"esc", GAM4980_KEY_EXIT}, {"up", GAM4980_KEY_UP},
        {"down", GAM4980_KEY_DOWN}, {"left", GAM4980_KEY_LEFT},
        {"right", GAM4980_KEY_RIGHT}, {"space", GAM4980_KEY_SPACE},
        {"menu", GAM4980_KEY_MENU},
    };
    size_t index;

    for (index = 0; index < sizeof(keys) / sizeof(keys[0]); ++index) {
        if (ascii_equal(name, keys[index].name)) {
            *key = keys[index].key;
            return 1;
        }
    }
    if (name[0] && !name[1] && name[0] >= '0' && name[0] <= '9') {
        *key = name[0] == '0'
            ? GAM4980_KEY_0 : (u8)(GAM4980_KEY_1 + name[0] - '1');
        return 1;
    }
    return 0;
}

static int parse_unsigned(const char *text, unsigned long *value)
{
    char *end = 0;

    *value = strtoul(text, &end, 10);
    return text[0] && end && !*end;
}

static int parse_key_event(const char *text, key_event_t *event)
{
    const char *colon = strchr(text, ':');
    char frame_text[32];
    size_t length;

    if (!colon || colon == text)
        return 0;
    length = (size_t)(colon - text);
    if (length >= sizeof(frame_text))
        return 0;
    memcpy(frame_text, text, length);
    frame_text[length] = 0;
    return parse_unsigned(frame_text, &event->frame) &&
        parse_key(colon + 1, &event->key);
}

static int compare_records(const void *left, const void *right)
{
    const block_record_t *a = (const block_record_t *)left;
    const block_record_t *b = (const block_record_t *)right;

    if (a->instructions < b->instructions)
        return 1;
    if (a->instructions > b->instructions)
        return -1;
    return a->key < b->key ? -1 : a->key != b->key;
}

static const char *physical_region(u32 physical_pc)
{
    if (physical_pc < 0x8000u)
        return "RAM";
    if (physical_pc >= 0x200000u && physical_pc < 0x400000u)
        return "FLASH";
    if (physical_pc >= 0x800000u && physical_pc < 0xa00000u)
        return "ROM8";
    if (physical_pc >= 0xe00000u && physical_pc < 0x1000000u)
        return "ROME";
    return "UNMAPPED";
}

static int write_report(
    profiler_t *profiler, const char *path, unsigned long top
)
{
    static const unsigned thresholds[] = {50u, 80u, 90u, 95u, 99u};
    FILE *file;
    uint64_t cumulative = 0;
    size_t threshold_index = 0;
    size_t index;

    qsort(
        profiler->records, profiler->record_count,
        sizeof(*profiler->records), compare_records
    );
    file = fopen(path, "w");
    if (!file)
        return 0;
    fprintf(
        file,
        "rank,region,physical_pc,virtual_pc,entries,instructions,"
        "average_instructions,coverage_percent,cumulative_percent\n"
    );
    for (index = 0; index < profiler->record_count; ++index) {
        const block_record_t *record = &profiler->records[index];
        u16 virtual_pc = (u16)record->key;
        u32 physical_pc = (u32)(record->key >> 16);
        double coverage = profiler->total_instructions
            ? 100.0 * (double)record->instructions /
                (double)profiler->total_instructions : 0.0;
        double average = record->entries
            ? (double)record->instructions / (double)record->entries : 0.0;

        cumulative += record->instructions;
        fprintf(
            file, "%zu,%s,0x%06x,0x%04x,%llu,%llu"
            ",%.3f,%.6f,%.6f\n",
            index + 1u, physical_region(physical_pc), physical_pc, virtual_pc,
            (unsigned long long)record->entries,
            (unsigned long long)record->instructions, average, coverage,
            profiler->total_instructions
                ? 100.0 * (double)cumulative /
                    (double)profiler->total_instructions : 0.0
        );
    }
    fclose(file);

    cumulative = 0;
    printf(
        "profiled instructions: %llu\ndistinct physical blocks: %zu\n",
        (unsigned long long)profiler->total_instructions,
        profiler->record_count
    );
    for (index = 0; index < profiler->record_count; ++index) {
        cumulative += profiler->records[index].instructions;
        while (threshold_index < sizeof(thresholds) / sizeof(thresholds[0]) &&
            cumulative * 100u >=
                profiler->total_instructions * thresholds[threshold_index]) {
            printf(
                "%u%% instruction coverage: %zu blocks\n",
                thresholds[threshold_index], index + 1u
            );
            ++threshold_index;
        }
        if (index < (size_t)top) {
            u16 virtual_pc = (u16)profiler->records[index].key;
            u32 physical_pc = (u32)(profiler->records[index].key >> 16);

            printf(
                "#%zu %-5s phys=%06x virt=%04x entries=%llu"
                " instructions=%llu\n",
                index + 1u, physical_region(physical_pc), physical_pc,
                virtual_pc,
                (unsigned long long)profiler->records[index].entries,
                (unsigned long long)profiler->records[index].instructions
            );
        }
    }
    printf("CSV report: %s\n", path);
    return 1;
}

static int write_frame(const char *path)
{
    const u8 *frame;
    FILE *file;

    (void)gam4980_render_frame();
    frame = gam4980_packed_frame();
    if (!frame)
        return 0;
    file = fopen(path, "wb");
    if (!file)
        return 0;
    fprintf(file, "P4\n%d %d\n", GAM4980_LCD_STRIDE, GAM4980_LCD_HEIGHT);
    if (fwrite(frame, 1, GAM4980_LCD_PACKED_SIZE, file) !=
        GAM4980_LCD_PACKED_SIZE) {
        fclose(file);
        return 0;
    }
    return fclose(file) == 0;
}

static void usage(const char *program)
{
    fprintf(
        stderr,
        "usage: %s [--frames N] [--start-frame N] [--top N] "
        "[--output FILE] [--frame-output FILE] "
        "[--key FRAME:KEY]... 8.BIN E.BIN game.gam\n",
        program
    );
}

int main(int argc, char **argv)
{
    gam4980_buffers_t buffers;
    profiler_t profiler;
    key_event_t key_events[MAX_KEY_EVENTS];
    size_t key_event_count = 0;
    unsigned long frames = DEFAULT_FRAMES;
    unsigned long start_frame = 0;
    unsigned long top = DEFAULT_TOP;
    const char *output = "aot-profile.csv";
    const char *frame_output = 0;
    const char *paths[3];
    int path_count = 0;
    int index;
    int result = 1;

    memset(&profiler, 0, sizeof(profiler));
    memset(&buffers, 0, sizeof(buffers));
    for (index = 1; index < argc; ++index) {
        const char *argument = argv[index];

        if (strcmp(argument, "--frames") == 0 && index + 1 < argc) {
            if (!parse_unsigned(argv[++index], &frames) || !frames) {
                usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argument, "--start-frame") == 0 && index + 1 < argc) {
            if (!parse_unsigned(argv[++index], &start_frame)) {
                usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argument, "--top") == 0 && index + 1 < argc) {
            if (!parse_unsigned(argv[++index], &top)) {
                usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argument, "--output") == 0 && index + 1 < argc) {
            output = argv[++index];
        } else if (strcmp(argument, "--frame-output") == 0 &&
            index + 1 < argc) {
            frame_output = argv[++index];
        } else if (strcmp(argument, "--key") == 0 && index + 1 < argc) {
            if (key_event_count == MAX_KEY_EVENTS ||
                !parse_key_event(argv[++index], &key_events[key_event_count])) {
                usage(argv[0]);
                return 2;
            }
            ++key_event_count;
        } else if (argument[0] == '-' || path_count == 3) {
            usage(argv[0]);
            return 2;
        } else {
            paths[path_count++] = argument;
        }
    }
    if (path_count != 3 || start_frame >= frames) {
        usage(argv[0]);
        return 2;
    }

    buffers.ram = (u8 *)malloc(GAM4980_RAM_SIZE);
    buffers.flash = (u8 *)malloc(GAM4980_FLASH_SIZE);
    buffers.rom_8 = (u8 *)malloc(GAM4980_ROM_SIZE);
    buffers.rom_e = (u8 *)malloc(GAM4980_ROM_SIZE);
    buffers.flash_size = GAM4980_FLASH_SIZE;
    if (!buffers.ram || !buffers.flash || !buffers.rom_8 || !buffers.rom_e) {
        fprintf(stderr, "out of memory\n");
        goto cleanup;
    }
    if (!load_file(paths[0], buffers.rom_8, GAM4980_ROM_SIZE) ||
        !load_file(paths[1], buffers.rom_e, GAM4980_ROM_SIZE)) {
        fprintf(stderr, "could not load exact 2 MiB ROM files\n");
        goto cleanup;
    }
    if (gam4980_init(&buffers) <= 0) {
        fprintf(stderr, "could not initialize the core\n");
        goto cleanup;
    }
    if (!load_game(paths[2])) {
        fprintf(stderr, "could not load the game\n");
        goto cleanup_core;
    }
    for (index = 0; (unsigned long)index < frames; ++index) {
        size_t event;

        if ((unsigned long)index == start_frame)
            gam4980_set_instruction_profile(profile_instruction, &profiler);
        for (event = 0; event < key_event_count; ++event) {
            if (key_events[event].frame == (unsigned long)index)
                gam4980_key_down(key_events[event].key);
        }
        gam4980_step_frame();
        if (gam4980_shutdown_requested())
            break;
    }
    gam4980_set_instruction_profile(0, 0);
    if (profiler.failed) {
        fprintf(stderr, "profiling table allocation failed\n");
        goto cleanup_core;
    }
    if (!profiler.total_instructions || !write_report(&profiler, output, top)) {
        fprintf(stderr, "could not write profiling report\n");
        goto cleanup_core;
    }
    if (frame_output && !write_frame(frame_output)) {
        fprintf(stderr, "could not write final PBM frame\n");
        goto cleanup_core;
    }
#if defined(GAM4980_ENABLE_AOT) && defined(GAM4980_AOT_DIAGNOSTICS)
    printf(
        "AOT instructions: %llu\n",
        (unsigned long long)gam4980_aot_instruction_count()
    );
    {
        u32 block_id;

        for (block_id = 0; block_id < gam4980_aot_block_count(); ++block_id) {
            u64 hits = gam4980_aot_block_hit_count(block_id);

            if (hits) {
                printf(
                    "AOT block %u: hits=%llu bank2=%03x varies=%d\n",
                    (unsigned)block_id, (unsigned long long)hits,
                    (unsigned)gam4980_aot_block_bank2(block_id),
                    gam4980_aot_block_bank2_varies(block_id)
                );
            }
        }
    }
#endif
#ifdef GAM4980_STATE_DIAGNOSTICS
    printf(
        "state hash: %016llx\n",
        (unsigned long long)gam4980_state_hash()
    );
#endif
    result = 0;

cleanup_core:
    gam4980_deinit();
cleanup:
    free(profiler.buckets);
    free(profiler.records);
    free(buffers.ram);
    free(buffers.flash);
    free(buffers.rom_8);
    free(buffers.rom_e);
    return result;
}
