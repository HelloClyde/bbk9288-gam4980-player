#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gam4980_core.h"

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

int main(int argc, char **argv)
{
    gam4980_buffers_t buffers;
    const u8 *frame;
    u32 checksum = 2166136261u;
    u32 index;
    int result;

    if (argc != 3 && argc != 4) {
        fprintf(stderr, "usage: core_smoke 8.BIN E.BIN [game.gam]\n");
        return 2;
    }
    memset(&buffers, 0, sizeof(buffers));
    buffers.ram = (u8 *)malloc(GAM4980_RAM_SIZE);
    buffers.flash = (u8 *)malloc(GAM4980_FLASH_SIZE);
    buffers.rom_8 = (u8 *)malloc(GAM4980_ROM_SIZE);
    buffers.rom_e = (u8 *)malloc(GAM4980_ROM_SIZE);
    buffers.framebuffer = 0;
    buffers.flash_size = GAM4980_FLASH_SIZE;
    if (!buffers.ram || !buffers.flash || !buffers.rom_8 || !buffers.rom_e)
        return 3;
    if (!load_file(argv[1], buffers.rom_8, GAM4980_ROM_SIZE) ||
        !load_file(argv[2], buffers.rom_e, GAM4980_ROM_SIZE))
        return 4;
    result = gam4980_init(&buffers);
    if (result <= 0) {
        fprintf(stderr, "gam4980_init failed: %d\n", result);
        return 5;
    }
    if (argc == 4) {
        int frame_number;

        if (!load_game(argv[3]))
            return 6;
        for (frame_number = 0; frame_number < 120; ++frame_number)
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
        "core initialized: packed=%u bytes fnv1a=%08x halted=%d\n",
        (unsigned)GAM4980_LCD_PACKED_SIZE, (unsigned)checksum,
        gam4980_cpu_halted()
    );
    gam4980_deinit();
    free(buffers.ram);
    free(buffers.flash);
    free(buffers.rom_8);
    free(buffers.rom_e);
    return 0;
}
