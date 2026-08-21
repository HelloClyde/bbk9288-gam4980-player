#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gam4980_core.h"

static u8 *stream_roms[2];
static u32 stream_rom_reads;

static int stream_rom_read(
    void *context, u8 region, u32 offset, u8 *out, u32 size
)
{
    (void)context;
    if (region > GAM4980_ROM_REGION_E || !out ||
        offset > GAM4980_ROM_SIZE || size > GAM4980_ROM_SIZE - offset)
        return 0;
    memcpy(out, stream_roms[region] + offset, size);
    ++stream_rom_reads;
    return 1;
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

static int load_game(const char *path, u8 *flash)
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
    if (gam4980_load_game_header(header, (u32)size) <= 0)
        return 0;
    return flash[0x801c] == ((u32)size & 0xffu) &&
        flash[0x801d] == (((u32)size >> 8) & 0xffu) &&
        flash[0x801e] == (((u32)size >> 16) & 0xffu);
}

int main(int argc, char **argv)
{
    gam4980_buffers_t buffers;
    const u8 *frame;
    const char *frame_count_text;
    u8 *rom_8;
    u8 *rom_e;
    u32 checksum = 2166136261u;
    u32 index;
    unsigned long frame_count = 120;
    int result;

    if (argc != 3 && argc != 4) {
        fprintf(stderr, "usage: core_smoke 8.BIN E.BIN [game.gam]\n");
        return 2;
    }
    frame_count_text = getenv("GAM4980_SMOKE_FRAMES");
    if (frame_count_text && *frame_count_text) {
        char *end = 0;

        frame_count = strtoul(frame_count_text, &end, 10);
        if (!end || *end || frame_count == 0)
            return 2;
    }
    memset(&buffers, 0, sizeof(buffers));
    buffers.ram = (u8 *)malloc(GAM4980_RAM_SIZE);
    buffers.flash = (u8 *)malloc(GAM4980_FLASH_SIZE);
    rom_8 = (u8 *)malloc(GAM4980_ROM_SIZE);
    rom_e = (u8 *)malloc(GAM4980_ROM_SIZE);
    buffers.rom_8 = rom_8;
    buffers.rom_e = rom_e;
    buffers.framebuffer = 0;
    buffers.flash_size = GAM4980_FLASH_SIZE;
    if (!buffers.ram || !buffers.flash || !rom_8 || !rom_e)
        return 3;
    if (!load_file(argv[1], rom_8, GAM4980_ROM_SIZE) ||
        !load_file(argv[2], rom_e, GAM4980_ROM_SIZE))
        return 4;
    if (getenv("GAM4980_STREAM_ROM")) {
        stream_roms[GAM4980_ROM_REGION_8] = rom_8;
        stream_roms[GAM4980_ROM_REGION_E] = rom_e;
        buffers.rom_8 = 0;
        buffers.rom_e = 0;
        buffers.rom_read = stream_rom_read;
    }
    result = gam4980_init(&buffers);
    if (result <= 0) {
        fprintf(stderr, "gam4980_init failed: %d\n", result);
        return 5;
    }
    if (argc == 4) {
        unsigned long frame_number;

        if (!load_game(argv[3], buffers.flash))
            return 6;
        for (frame_number = 0; frame_number < frame_count; ++frame_number)
            gam4980_run_frame();
    } else {
        (void)gam4980_render_frame();
    }
    frame = gam4980_packed_frame();
    if (!frame)
        return 7;
    for (index = 0; index < GAM4980_LCD_PACKED_SIZE; ++index) {
        checksum ^= frame[index];
        checksum *= 16777619u;
    }
    printf(
        "core initialized: packed=%u bytes fnv1a=%08x halted=%d rom_reads=%u\n",
        (unsigned)GAM4980_LCD_PACKED_SIZE, (unsigned)checksum,
        gam4980_cpu_halted(), (unsigned)stream_rom_reads
    );
#if defined(GAM4980_ENABLE_AOT) && defined(GAM4980_AOT_DIAGNOSTICS)
    printf(
        "aot instructions=%llu\n",
        (unsigned long long)gam4980_aot_instruction_count()
    );
#endif
    gam4980_deinit();
    free(buffers.ram);
    free(buffers.flash);
    free(rom_8);
    free(rom_e);
    return 0;
}
