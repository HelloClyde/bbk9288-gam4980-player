#include "Dsys.h"
#include "gam4980_core.h"

/* ROM caches are completely filled before their first read.  Keeping them
 * outside .bss avoids clearing 68 KiB before the 9288 selector appears. */
#define GAM4980_CACHE_STORAGE \
    __attribute__((aligned(4), section(".noinit")))

#define APP_TITLE "GAM4980"
#define GAM_SCREEN_WIDTH 320
#define GAM_SCREEN_HEIGHT 240
#define SCREEN_PITCH_BYTES 80
#define VIDEO_BASE 0x003c0000u
#define VIEW_X 1
#define VIEW_Y 24
#define FRAME_CLOCK_HZ 1000u
#define ESCAPE_QUIT_TICKS FRAME_CLOCK_HZ
#define FRAME_RATE_HZ 60u
#define FRAME_BASE_TICKS (FRAME_CLOCK_HZ / FRAME_RATE_HZ)
#define FRAME_REMAINDER_TICKS (FRAME_CLOCK_HZ % FRAME_RATE_HZ)
#define MAX_CATCHUP_FRAMES 3u
#define FRAME_WAKE_TIMER_ID 1
#define FRAME_WAKE_TIMER_TICKS 2
#define PATH_CAPACITY (MAX_PATH * 2)
#define FILE_IO_CHUNK 16384u
#define MAX_GAME_FILES 32
#define GAME_NAME_CAPACITY 128
#define SELECTOR_ROW_HEIGHT 18
#define SELECTOR_FIRST_ROW_Y 20
#define SELECTOR_VISIBLE_ROWS 11
#define SELECTOR_ACCEPT_DELAY_TICKS 12u

static const char k_rom_8_path[] = "a:\\gam4980\\8.BIN";
static const char k_rom_e_path[] = "a:\\gam4980\\E.BIN";
static const char k_game_root[] = "a:\\gam4980";
static const char k_game_dir[] = "a:\\gam4980\\";
static const char k_game_pattern[] = "a:\\gam4980\\*.*";
static const T_BYTE k_selector_directory[] = "A:\\gam4980\\";
static const T_BYTE k_selector_title[] = {
    0xc7, 0xeb, 0xd1, 0xa1, 0xd4, 0xf1, 0xd3, 0xce, 0xcf, 0xb7, 0
}; /* 请选择游戏 (GBK) */
static const T_BYTE k_no_games[] = {
    0xc3, 0xbb, 0xd3, 0xd0, 0xd5, 0xd2, 0xb5, 0xbd, 0x20,
    '.', 'g', 'a', 'm', 0x20, 0xce, 0xc4, 0xbc, 0xfe, 0
}; /* 没有找到 .gam 文件 (GBK) */

static T_GUI_HWND g_main_window;
static gam4980_buffers_t g_buffers;
static char g_game_path[PATH_CAPACITY];
static char g_save_path[PATH_CAPACITY];
static char g_game_names[MAX_GAME_FILES][GAME_NAME_CAPACITY]
    __attribute__((aligned(4), section(".noinit")));
static struct ffblk g_find_block
    __attribute__((aligned(4), section(".noinit")));
static int g_game_count;
static int g_selector_index;
static int g_selector_top;
static int g_selector_done;
static int g_selector_accepted;
static u32 g_selector_open_tick;
static u8 g_static_ram[GAM4980_RAM_SIZE]
    __attribute__((aligned(4), section(".noinit")));
static FS_FILE *g_rom_files[2];
static int g_close_requested;
static int g_escape_down;
static int g_game_ready;
static u32 g_escape_down_tick;
static u8 g_scaled_row[SCREEN_PITCH_BYTES] __attribute__((aligned(4)));

static int read_rom_bank(
    void *context, u8 region, u32 offset, u8 *out, u32 size
);
static void destroy_selector_window(void);
static void destroy_emulator_window(void);

void *memcpy(void *destination, const void *source, unsigned int size)
{
    u8 *out = (u8 *)destination;
    const u8 *in = (const u8 *)source;

    while (size--)
        *out++ = *in++;
    return destination;
}

void *memset(void *destination, int value, unsigned int size)
{
    u8 *out = (u8 *)destination;

    while (size--)
        *out++ = (u8)value;
    return destination;
}

static u32 tick_elapsed(u32 start, u32 end)
{
    return end - start;
}

static u32 system_clock_milliseconds(void)
{
    u8 packed_time[6] __attribute__((aligned(2)));

    if (get_tim((SYSTIME *)(void *)packed_time) < 0)
        return 0;
    return (u32)packed_time[2] |
           ((u32)packed_time[3] << 8) |
           ((u32)packed_time[4] << 16) |
           ((u32)packed_time[5] << 24);
}

static int tick_deadline_reached(u32 now, u32 deadline)
{
    return (s32)(now - deadline) >= 0;
}

static void advance_frame_deadline(u32 *deadline, u32 *phase)
{
    *deadline += FRAME_BASE_TICKS;
    *phase += FRAME_REMAINDER_TICKS;
    if (*phase >= FRAME_RATE_HZ) {
        *deadline += 1u;
        *phase -= FRAME_RATE_HZ;
    }
}

static void reset_frame_deadline(u32 now, u32 *deadline, u32 *phase)
{
    *deadline = now;
    *phase = 0;
    advance_frame_deadline(deadline, phase);
}

static void show_error(const char *text)
{
    (void)fnGUI_MessageBox(
        g_main_window ? g_main_window : HWND_DESKTOP,
        (const T_BYTE *)text, (const T_BYTE *)APP_TITLE,
        MB_OK | MB_ICONSTOP
    );
}

static void release_buffers(void)
{
    int index;

    gam4980_deinit();
    for (index = 0; index < 2; ++index) {
        if (g_rom_files[index]) {
            fs_fclose(g_rom_files[index]);
            g_rom_files[index] = 0;
        }
    }
    if (g_buffers.flash)
        free(g_buffers.flash);
    memset(&g_buffers, 0, sizeof(g_buffers));
}

static int allocate_buffers(u32 game_size)
{
    u32 flash_size = (0x15000u + game_size + 0xfffu) & ~0xfffu;

    if (flash_size > GAM4980_FLASH_SIZE)
        return 0;
    memset(&g_buffers, 0, sizeof(g_buffers));
    g_buffers.ram = g_static_ram;
    g_buffers.flash = (u8 *)malloc(flash_size);
    g_buffers.flash_size = flash_size;
    g_buffers.framebuffer = 0;
    g_buffers.rom_read = read_rom_bank;
    g_buffers.rom_context = 0;
    return g_buffers.flash != 0;
}

static int read_exact(FS_FILE *file, u8 *out, u32 size)
{
    u32 total = 0;

    while (total < size) {
        u32 remaining = size - total;
        size_t chunk = remaining > FILE_IO_CHUNK ? FILE_IO_CHUNK : remaining;
        size_t got = fs_fread(out + total, 1, chunk, file);

        if (!got)
            return 0;
        total += (u32)got;
    }
    return 1;
}

static int write_exact(FS_FILE *file, const u8 *data, u32 size)
{
    u32 total = 0;

    while (total < size) {
        u32 remaining = size - total;
        size_t chunk = remaining > FILE_IO_CHUNK ? FILE_IO_CHUNK : remaining;
        size_t wrote = fs_fwrite(data + total, 1, chunk, file);

        if (!wrote)
            return 0;
        total += (u32)wrote;
    }
    return 1;
}

static int open_rom_file(u8 region, const char *path)
{
    FS_FILE *file = fs_fopen(path, FS_O_RDONLY);
    long size;

    if (region > GAM4980_ROM_REGION_E || !file)
        return -1;
    if (fs_fseek(file, 0, SEEK_END) < 0) {
        fs_fclose(file);
        return -2;
    }
    size = fs_ftell(file);
    if (size != (long)GAM4980_ROM_SIZE) {
        fs_fclose(file);
        return -3;
    }
    if (fs_fseek(file, 0, SEEK_SET) < 0) {
        fs_fclose(file);
        return -4;
    }
    g_rom_files[region] = file;
    return 0;
}

static int read_rom_bank(
    void *context, u8 region, u32 offset, u8 *out, u32 size
)
{
    FS_FILE *file;

    (void)context;
    if (region > GAM4980_ROM_REGION_E ||
        offset > GAM4980_ROM_SIZE || size > GAM4980_ROM_SIZE - offset)
        return 0;
    file = g_rom_files[region];
    if (!file || fs_fseek(file, (long)offset, SEEK_SET) < 0)
        return 0;
    if (!read_exact(file, out, size))
        return 0;
    return 1;
}

static int verify_rom_files(void)
{
    u8 bytes[3];

    if (!read_rom_bank(
            0, GAM4980_ROM_REGION_8, 0, bytes, sizeof(bytes)
        ) || bytes[0] != 0x00 || bytes[2] != 0x0f ||
        !read_rom_bank(
            0, GAM4980_ROM_REGION_8, GAM4980_ROM_SIZE - 1u, bytes, 1
        ) || bytes[0] != 0x42)
        return 0;
    if (!read_rom_bank(
            0, GAM4980_ROM_REGION_E, 0, bytes, 2
        ) || bytes[0] != 0x86 || bytes[1] != 0xb5 ||
        !read_rom_bank(
            0, GAM4980_ROM_REGION_E, GAM4980_ROM_SIZE - 16u, bytes, 1
        ) || bytes[0] != 0x2a ||
        !read_rom_bank(
            0, GAM4980_ROM_REGION_E, GAM4980_ROM_SIZE - 1u, bytes, 1
        ) || bytes[0] != 0xff)
        return 0;
    return 1;
}

static int copy_path(char *destination, const char *source, u32 capacity)
{
    u32 index = 0;

    if (!destination || !source || !capacity)
        return 0;
    while (source[index] && index + 1u < capacity) {
        destination[index] = source[index];
        ++index;
    }
    destination[index] = 0;
    return source[index] == 0;
}

static u32 byte_length(const char *text)
{
    u32 length = 0;

    while (text && text[length])
        ++length;
    return length;
}

static char ascii_lower(char value)
{
    if (value >= 'A' && value <= 'Z')
        return (char)(value + ('a' - 'A'));
    return value;
}

static int is_gam_file_name(const char *name)
{
    u32 length = byte_length(name);

    return length > 4u && name[length - 4u] == '.' &&
        ascii_lower(name[length - 3u]) == 'g' &&
        ascii_lower(name[length - 2u]) == 'a' &&
        ascii_lower(name[length - 1u]) == 'm';
}

static const char *base_name(const char *path)
{
    const char *name = path;

    while (path && *path) {
        if (*path == '\\' || *path == '/')
            name = path + 1;
        ++path;
    }
    return name;
}

static int enumerate_games(void)
{
    int result;
    int search_open;

    g_game_count = 0;
    /* Current-firmware official browsers enumerate files with attribute 0;
     * 0x10 is used only for the separate directory pass. */
    result = fs_findfirst(k_game_pattern, 0u, &g_find_block);
    search_open = result == 0;
    while (result == 0) {
        const char *name = base_name((const char *)g_find_block.ff_name);

        if (!(g_find_block.ff_attrib & DA_DIR) && is_gam_file_name(name) &&
            g_game_count < MAX_GAME_FILES &&
            copy_path(
                g_game_names[g_game_count], name, GAME_NAME_CAPACITY
            ))
            ++g_game_count;
        result = fs_findnext(&g_find_block);
    }
    if (search_open)
        (void)fs_findclose(&g_find_block);
    return g_game_count;
}

static int copy_selected_game_path(void)
{
    u32 directory_length = byte_length(k_game_dir);
    u32 name_length;

    if (g_selector_index < 0 || g_selector_index >= g_game_count)
        return 0;
    name_length = byte_length(g_game_names[g_selector_index]);
    if (directory_length + name_length + 1u > sizeof(g_game_path))
        return 0;
    if (!copy_path(g_game_path, k_game_dir, sizeof(g_game_path)))
        return 0;
    return copy_path(
        g_game_path + directory_length,
        g_game_names[g_selector_index],
        sizeof(g_game_path) - directory_length
    );
}

static int make_save_path(const char *game_path)
{
    u32 index = 0;
    char *extension = 0;

    if (!copy_path(g_save_path, game_path, sizeof(g_save_path)))
        return 0;
    while (g_save_path[index]) {
        if (g_save_path[index] == '\\' || g_save_path[index] == '/')
            extension = 0;
        else if (g_save_path[index] == '.')
            extension = g_save_path + index;
        ++index;
    }
    if (!extension || extension + 4 != g_save_path + index)
        return 0;
    extension[1] = 's';
    extension[2] = 'a';
    extension[3] = 'v';
    return 1;
}

static void load_save(void)
{
    FS_FILE *file = fs_fopen(g_save_path, FS_O_RDONLY);

    if (!file)
        return;
    if (fs_fseek(file, 0, SEEK_END) >= 0 &&
        fs_ftell(file) == (long)GAM4980_SAVE_SIZE &&
        fs_fseek(file, 0, SEEK_SET) >= 0)
        (void)read_exact(file, gam4980_save_data(), GAM4980_SAVE_SIZE);
    fs_fclose(file);
}

static void write_save(void)
{
    FS_FILE *file;

    if (!g_save_path[0] || !gam4980_save_dirty())
        return;
    file = fs_fopen(g_save_path, FS_O_WRONLY);
    if (!file)
        return;
    if (write_exact(file, gam4980_save_data(), GAM4980_SAVE_SIZE)) {
        (void)fs_update(file);
        gam4980_save_mark_clean();
    }
    fs_fclose(file);
    (void)fs_flush_cache();
}

static void keep_selector_visible(void)
{
    if (g_selector_index < g_selector_top)
        g_selector_top = g_selector_index;
    if (g_selector_index >= g_selector_top + SELECTOR_VISIBLE_ROWS)
        g_selector_top =
            g_selector_index - SELECTOR_VISIBLE_ROWS + 1;
    if (g_selector_top < 0)
        g_selector_top = 0;
}

static void selector_move(T_GUI_HWND window, int delta)
{
    if (g_game_count <= 0)
        return;
    g_selector_index += delta;
    if (g_selector_index < 0)
        g_selector_index = 0;
    if (g_selector_index >= g_game_count)
        g_selector_index = g_game_count - 1;
    keep_selector_visible();
    (void)fnGUI_InvalidateRect(window, 0, TRUE);
}

static void selector_draw(T_GUI_HWND window)
{
    T_GUI_HDC hdc = fnGUI_BeginPaint(window);
    int row;

    (void)fnGUI_SetBkMode(hdc, BM_TRANSPARENT);
    (void)fnGUI_SetBrushColor(hdc, COLOR_LIGHTWHITE);
    fnGUI_FillBox(hdc, 0, 0, GAM_SCREEN_WIDTH, GAM_SCREEN_HEIGHT);
    (void)fnGUI_SetTextColor(hdc, COLOR_BLACK);
    (void)fnGUI_TextOut(
        hdc, 4, 2, k_selector_directory
    );

    if (g_game_count == 0) {
        (void)fnGUI_TextOut(hdc, 4, SELECTOR_FIRST_ROW_Y, k_no_games);
    } else {
        for (row = 0; row < SELECTOR_VISIBLE_ROWS; ++row) {
            int index = g_selector_top + row;
            int y = SELECTOR_FIRST_ROW_Y + row * SELECTOR_ROW_HEIGHT;

            if (index >= g_game_count)
                break;
            if (index == g_selector_index) {
                (void)fnGUI_SetBrushColor(hdc, COLOR_BLACK);
                fnGUI_FillBox(
                    hdc, 0, y, GAM_SCREEN_WIDTH, SELECTOR_ROW_HEIGHT
                );
                (void)fnGUI_SetTextColor(hdc, COLOR_LIGHTWHITE);
            } else {
                (void)fnGUI_SetTextColor(hdc, COLOR_BLACK);
            }
            (void)fnGUI_TextOut(
                hdc, 4, y + 1,
                (const T_BYTE *)g_game_names[index]
            );
        }
    }
    fnGUI_EndPaint(window, hdc);
}

static T_WORD selector_window_proc(
    T_GUI_HWND window, T_WORD message, T_GUI_WPARAM wparam, T_GUI_LPARAM lparam
)
{
    T_UHWORD scancode = LOUHWORD(wparam);

    (void)lparam;
    switch (message) {
    case MSG_KEYDOWN:
        switch (scancode) {
        case SCANCODE_CURSORUP:
        case SCANCODE_CURSORBLOCKUP:
            selector_move(window, -1);
            break;
        case SCANCODE_CURSORDOWN:
        case SCANCODE_CURSORBLOCKDOWN:
            selector_move(window, 1);
            break;
        case SCANCODE_PAGEUP:
            selector_move(window, -SELECTOR_VISIBLE_ROWS);
            break;
        case SCANCODE_PAGEDOWN:
            selector_move(window, SELECTOR_VISIBLE_ROWS);
            break;
        case SCANCODE_ENTER:
        case SCANCODE_KEYPADENTER:
            if (g_game_count > 0 && tick_elapsed(
                    g_selector_open_tick, (u32)fnGUI_GetTickCount()
                ) >= SELECTOR_ACCEPT_DELAY_TICKS) {
                g_selector_accepted = 1;
                g_selector_done = 1;
            }
            break;
        case SCANCODE_ESCAPE:
            g_selector_done = 1;
            break;
        default:
            break;
        }
        return 0;
    case MSG_ERASEBKGND:
        return 0;
    case MSG_PAINT:
        selector_draw(window);
        return 0;
    case MSG_CLOSE:
        g_selector_done = 1;
        return 0;
    default:
        return fnGUI_DefaultMainWinProc(window, message, wparam, lparam);
    }
}

static int select_game(void)
{
    T_GUI_MainWinCreate info;
    T_GUI_Msg message;

    (void)enumerate_games();
    g_selector_index = 0;
    g_selector_top = 0;
    g_selector_done = 0;
    g_selector_accepted = 0;
    memset(&info, 0, sizeof(info));
    info.dwStyle = WS_VISIBLE | WS_CAPTION;
    info.dwExStyle = WS_EX_NONE;
    info.spCaption = k_selector_title;
    info.MainWindowProc = selector_window_proc;
    info.lx = 0;
    info.ty = 0;
    info.rx = GAM_SCREEN_WIDTH;
    info.by = GAM_SCREEN_HEIGHT;
    info.iBkColor = COLOR_LIGHTWHITE;
    info.hHosting = HWND_DESKTOP;

    g_main_window = fnGUI_CreateMainWindow(&info);
    if (!g_main_window)
        return 0;
    g_selector_open_tick = (u32)fnGUI_GetTickCount();
    while (!g_selector_done && fnGUI_GetMessage(&message, g_main_window)) {
        fnGUI_TranslateMessage(&message);
        fnGUI_DispatchMessage(&message);
    }
    destroy_selector_window();
    return g_selector_accepted && copy_selected_game_path();
}

static long get_game_size(const char *path)
{
    FS_FILE *file = fs_fopen(path, FS_O_RDONLY);
    long size = -1;

    if (!file)
        return -1;
    if (fs_fseek(file, 0, SEEK_END) >= 0)
        size = fs_ftell(file);
    fs_fclose(file);
    return size;
}

static int load_game(const char *path)
{
    u8 header[GAM4980_GAME_HEADER_SIZE];
    FS_FILE *file = fs_fopen(path, FS_O_RDONLY);
    long size;

    if (!file)
        return 0;
    if (fs_fseek(file, 0, SEEK_END) < 0) {
        fs_fclose(file);
        return 0;
    }
    size = fs_ftell(file);
    if (size < (long)GAM4980_GAME_HEADER_SIZE ||
        size > (long)GAM4980_GAME_MAX_SIZE ||
        fs_fseek(file, 0, SEEK_SET) < 0 ||
        !read_exact(file, header, GAM4980_GAME_HEADER_SIZE) ||
        fs_fseek(file, 0, SEEK_SET) < 0 ||
        !read_exact(file, gam4980_game_storage(), (u32)size)) {
        fs_fclose(file);
        return 0;
    }
    fs_fclose(file);
    if (gam4980_load_game_header(header, (u32)size) <= 0 ||
        !make_save_path(path))
        return 0;
    load_save();
    gam4980_save_mark_clean();
    return 1;
}

static void clear_screen(void)
{
    volatile u32 *video = (volatile u32 *)VIDEO_BASE;
    u32 count = (GAM_SCREEN_WIDTH * GAM_SCREEN_HEIGHT * 2u) / 32u;

    while (count--)
        *video++ = 0xffffffffu;
}

static void clear_2bpp_pixel(u8 *row, int x)
{
    u32 shift = (u32)(3 - (x & 3)) * 2u;

    row[x >> 2] &= (u8)~(3u << shift);
}

static void copy_row_to_video(int y, const u8 *row)
{
    volatile u32 *destination =
        (volatile u32 *)(VIDEO_BASE + (u32)y * SCREEN_PITCH_BYTES);
    const u32 *source = (const u32 *)row;
    int index;

    for (index = 0; index < SCREEN_PITCH_BYTES / 4; ++index)
        destination[index] = source[index];
}

static void present_2x(const u8 *packed)
{
    int source_y;

    if (!packed)
        return;
    for (source_y = 0; source_y < GAM4980_LCD_HEIGHT; ++source_y) {
        const u8 *source =
            packed + source_y * GAM4980_LCD_PACKED_STRIDE;
        int source_x;

        memset(g_scaled_row, 0xff, sizeof(g_scaled_row));
        for (source_x = 0; source_x < GAM4980_LCD_WIDTH; ++source_x) {
            if (source[source_x >> 3] & (0x80u >> (source_x & 7))) {
                int destination_x = VIEW_X + source_x * 2;

                clear_2bpp_pixel(g_scaled_row, destination_x);
                clear_2bpp_pixel(g_scaled_row, destination_x + 1);
            }
        }
        copy_row_to_video(VIEW_Y + source_y * 2, g_scaled_row);
        copy_row_to_video(VIEW_Y + source_y * 2 + 1, g_scaled_row);
    }
}

static u8 map_scancode(T_UHWORD scancode)
{
    switch (scancode) {
    case SCANCODE_1: return GAM4980_KEY_1;
    case SCANCODE_2: return GAM4980_KEY_2;
    case SCANCODE_3: return GAM4980_KEY_3;
    case SCANCODE_4: return GAM4980_KEY_4;
    case SCANCODE_5: return GAM4980_KEY_5;
    case SCANCODE_6: return GAM4980_KEY_6;
    case SCANCODE_7: return GAM4980_KEY_7;
    case SCANCODE_8: return GAM4980_KEY_8;
    case SCANCODE_9: return GAM4980_KEY_9;
    case SCANCODE_0: return GAM4980_KEY_0;
    case SCANCODE_Q: return GAM4980_KEY_Q;
    case SCANCODE_W: return GAM4980_KEY_W;
    case SCANCODE_E: return GAM4980_KEY_E;
    case SCANCODE_R: return GAM4980_KEY_R;
    case SCANCODE_T: return GAM4980_KEY_T;
    case SCANCODE_Y: return GAM4980_KEY_Y;
    case SCANCODE_U: return GAM4980_KEY_U;
    case SCANCODE_I: return GAM4980_KEY_I;
    case SCANCODE_O: return GAM4980_KEY_O;
    case SCANCODE_P: return GAM4980_KEY_P;
    case SCANCODE_A: return GAM4980_KEY_A;
    case SCANCODE_S: return GAM4980_KEY_S;
    case SCANCODE_D: return GAM4980_KEY_D;
    case SCANCODE_F: return GAM4980_KEY_F;
    case SCANCODE_G: return GAM4980_KEY_G;
    case SCANCODE_H: return GAM4980_KEY_H;
    case SCANCODE_J: return GAM4980_KEY_J;
    case SCANCODE_K: return GAM4980_KEY_K;
    case SCANCODE_L: return GAM4980_KEY_L;
    case SCANCODE_Z: return GAM4980_KEY_Z;
    case SCANCODE_X: return GAM4980_KEY_X;
    case SCANCODE_C: return GAM4980_KEY_C;
    case SCANCODE_V: return GAM4980_KEY_V;
    case SCANCODE_B: return GAM4980_KEY_B;
    case SCANCODE_N: return GAM4980_KEY_N;
    case SCANCODE_M: return GAM4980_KEY_M;
    case SCANCODE_ENTER:
    case SCANCODE_KEYPADENTER: return GAM4980_KEY_ENTER;
    case SCANCODE_BACKSPACE:
    case SCANCODE_KEYPADPERIOD: return GAM4980_KEY_DELETE;
    case SCANCODE_TAB: return GAM4980_KEY_INPUT;
    case SCANCODE_SPACE: return GAM4980_KEY_SPACE;
    case SCANCODE_LEFTSHIFT:
    case SCANCODE_RIGHTSHIFT: return GAM4980_KEY_SHIFT;
    case SCANCODE_CURSORUP:
    case SCANCODE_CURSORBLOCKUP: return GAM4980_KEY_UP;
    case SCANCODE_CURSORLEFT:
    case SCANCODE_CURSORBLOCKLEFT: return GAM4980_KEY_LEFT;
    case SCANCODE_CURSORDOWN:
    case SCANCODE_CURSORBLOCKDOWN: return GAM4980_KEY_DOWN;
    case SCANCODE_CURSORRIGHT:
    case SCANCODE_CURSORBLOCKRIGHT: return GAM4980_KEY_RIGHT;
    case SCANCODE_PAGEUP: return GAM4980_KEY_PAGE_UP;
    case SCANCODE_PAGEDOWN: return GAM4980_KEY_PAGE_DOWN;
    case SCANCODE_F1: return GAM4980_KEY_SPEAK;
    case SCANCODE_F2: return GAM4980_KEY_CE;
    case SCANCODE_F3: return GAM4980_KEY_EC_SJ;
    case SCANCODE_F4: return GAM4980_KEY_EC_SW;
    case SCANCODE_F5: return GAM4980_KEY_POWER;
    case SCANCODE_F6: return GAM4980_KEY_MENU;
    case SCANCODE_F7: return GAM4980_KEY_MODIFY;
    case SCANCODE_F8: return GAM4980_KEY_SHIFT;
    case SCANCODE_F9: return GAM4980_KEY_SEARCH;
    case SCANCODE_F10: return GAM4980_KEY_DOWNLOAD;
    case SCANCODE_F11: return GAM4980_KEY_HELP;
    case SCANCODE_F12: return GAM4980_KEY_EXIT;
    default: return 0xffu;
    }
}

static void poll_game_combo_keys(void)
{
    u8 state[6];

    memset(state, 0, sizeof(state));
    /* The 9288 GUI requires this SDK call to pump the keyboard hardware. */
    (void)fnGUI_ScanKeyForGameComboKeys(state);
}

static T_WORD gam_window_proc(
    T_GUI_HWND window, T_WORD message, T_GUI_WPARAM wparam, T_GUI_LPARAM lparam
)
{
    T_UHWORD scancode = LOUHWORD(wparam);

    (void)lparam;
    switch (message) {
    case MSG_KEYDOWN:
        if (scancode == SCANCODE_ESCAPE) {
            if (!g_escape_down) {
                g_escape_down = 1;
                g_escape_down_tick = system_clock_milliseconds();
            } else if (tick_elapsed(
                           g_escape_down_tick, system_clock_milliseconds()
                       ) >= ESCAPE_QUIT_TICKS) {
                g_close_requested = 1;
            }
        } else {
            u8 key = map_scancode(scancode);

            if (key != 0xffu)
                gam4980_key_down(key);
        }
        return 0;
    case MSG_KEYUP:
        if (scancode == SCANCODE_ESCAPE && g_escape_down) {
            if (tick_elapsed(
                    g_escape_down_tick, system_clock_milliseconds()
                ) < ESCAPE_QUIT_TICKS)
                gam4980_key_down(GAM4980_KEY_EXIT);
            else {
                g_close_requested = 1;
            }
            g_escape_down = 0;
        }
        return 0;
    case MSG_ERASEBKGND:
        return 0;
    case MSG_PAINT: {
        T_WORD result =
            fnGUI_DefaultMainWinProc(window, message, wparam, lparam);

        if (g_game_ready)
            present_2x(gam4980_packed_frame());
        return result;
    }
    case MSG_CLOSE:
        g_close_requested = 1;
        return 0;
    default:
        return fnGUI_DefaultMainWinProc(window, message, wparam, lparam);
    }
}

static void init_window_info(T_GUI_pMainWinCreate info)
{
    memset(info, 0, sizeof(*info));
    info->dwStyle = WS_VISIBLE | WS_CAPTION;
    info->dwExStyle = WS_EX_NONE;
    info->spCaption = (const T_BYTE *)APP_TITLE;
    info->MainWindowProc = gam_window_proc;
    info->lx = 0;
    info->ty = 0;
    info->rx = GAM_SCREEN_WIDTH;
    info->by = GAM_SCREEN_HEIGHT;
    info->iBkColor = COLOR_LIGHTWHITE;
    info->hHosting = HWND_DESKTOP;
}

static int create_emulator_window(void)
{
    T_GUI_MainWinCreate info;
    T_GUI_Msg message;

    init_window_info(&info);
    g_main_window = fnGUI_CreateMainWindow(&info);
    if (!g_main_window)
        return 0;
    (void)fnGUI_ShowWindow(g_main_window, SW_SHOWNORMAL);
    while (fnGUI_GetMessage(&message, g_main_window)) {
        fnGUI_TranslateMessage(&message);
        fnGUI_DispatchMessage(&message);
        if (message.message == MSG_PAINT)
            break;
    }
    (void)fnGUI_SetActiveWindow(g_main_window);
    (void)fnGUI_SetFocus(g_main_window);
    return 1;
}

static void destroy_selector_window(void)
{
    T_GUI_HWND window = g_main_window;

    if (!window)
        return;
    g_main_window = 0;
    fnGUI_DestroyMainWindow(window);
    fnGUI_ThrowAwayMessages(window);
    fnGUI_MainWindowCleanup(window);
}

static void destroy_emulator_window(void)
{
    T_GUI_HWND window = g_main_window;

    if (!window)
        return;
    g_game_ready = 0;
    g_main_window = 0;
    fnGUI_DestroyMainWindow(window);
    fnGUI_PostQuitMessage(window);
    fnGUI_ThrowAwayMessages(window);
    fnGUI_MainWindowCleanup(window);
}

static int run_emulator_window(void)
{
    T_GUI_Msg message;
    u32 frame_deadline;
    u32 frame_phase;

    if (!g_main_window)
        return 0;
    g_game_ready = 1;
    clear_screen();
    g_close_requested = 0;
    g_escape_down = 0;
    (void)gam4980_render_frame();
    present_2x(gam4980_packed_frame());
    reset_frame_deadline(system_clock_milliseconds(), &frame_deadline,
                         &frame_phase);
    if (!fnGUI_SetTimer(
            g_main_window, FRAME_WAKE_TIMER_ID, FRAME_WAKE_TIMER_TICKS
        )) {
        destroy_emulator_window();
        return 0;
    }

    while (!g_close_requested && !gam4980_shutdown_requested()) {
        u32 now;
        u32 catchup_frames = 0;

        if (!fnGUI_GetMessage(&message, g_main_window)) {
            g_close_requested = 1;
            break;
        }
        fnGUI_TranslateMessage(&message);
        fnGUI_DispatchMessage(&message);
        poll_game_combo_keys();
        if (g_close_requested)
            break;

        now = system_clock_milliseconds();
        while (tick_deadline_reached(now, frame_deadline) &&
               catchup_frames < MAX_CATCHUP_FRAMES) {
            gam4980_step_frame();
            advance_frame_deadline(&frame_deadline, &frame_phase);
            ++catchup_frames;
        }
        if (catchup_frames != 0u) {
            if (gam4980_render_frame())
                present_2x(gam4980_packed_frame());
            if (gam4980_shutdown_requested())
                g_close_requested = 1;
            if (g_escape_down && tick_elapsed(
                    g_escape_down_tick, system_clock_milliseconds()
                ) >= ESCAPE_QUIT_TICKS) {
                g_close_requested = 1;
            }
        }
        if (catchup_frames == MAX_CATCHUP_FRAMES &&
            tick_deadline_reached(now, frame_deadline)) {
            reset_frame_deadline(now, &frame_deadline, &frame_phase);
        }
    }

    (void)fnGUI_KillTimer(g_main_window, FRAME_WAKE_TIMER_ID);
    destroy_emulator_window();
    return 1;
}

T_WORD App_Main(void)
{
    int core_status;
    int initialized = 0;
    int rom_status;
    long game_size;

    (void)fs_mkdir(k_game_root);
    if (!select_game())
        return 0;
    if (!create_emulator_window()) {
        show_error("Could not create the GAM4980 window.");
        return -1;
    }
    game_size = get_game_size(g_game_path);
    if (game_size < (long)GAM4980_GAME_HEADER_SIZE ||
        game_size > (long)GAM4980_GAME_MAX_SIZE) {
        show_error("The selected GAM file has an invalid size.");
        destroy_emulator_window();
        return -1;
    }
    if (!allocate_buffers((u32)game_size)) {
        show_error("Not enough memory for the selected GAM file.");
        destroy_emulator_window();
        return -1;
    }
    rom_status = open_rom_file(GAM4980_ROM_REGION_8, k_rom_8_path);
    if (rom_status == 0)
        rom_status = open_rom_file(GAM4980_ROM_REGION_E, k_rom_e_path);
    if (rom_status != 0 || !verify_rom_files()) {
        char diagnostic[] = "ROM read stage X failed.";

        diagnostic[15] = rom_status < 0 ? (char)('0' - rom_status) : '6';
        show_error(diagnostic);
        release_buffers();
        destroy_emulator_window();
        return -2;
    }
    core_status = gam4980_init(&g_buffers);
    if (core_status <= 0) {
        char diagnostic[] = "Core initialization stage X failed.";

        diagnostic[26] = (char)('0' - core_status);
        show_error(diagnostic);
        release_buffers();
        destroy_emulator_window();
        return -3;
    }
    initialized = 1;
    if (!load_game(g_game_path)) {
        show_error("The selected GAM file is invalid or unreadable.");
        release_buffers();
        destroy_emulator_window();
        return -4;
    }
    if (!run_emulator_window()) {
        show_error("Could not create the GAM4980 window.");
        release_buffers();
        return -5;
    }
    if (initialized)
        write_save();
    release_buffers();
    return 0;
}

/* The upstream core includes the instruction interpreter as one unit. */
#include "gam4980_core.c"
