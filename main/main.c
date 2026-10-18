/*
 * Apple IIGS emulator on the Makerfabs MaTouch ESP32-S3 7" or the M5Stack
 * Tab5.
 *
 * The board side (tasks, LCD, touch, WiFi, Bluetooth, setup screen, web
 * page, console) is the shared runtime in ../esp32emu; this file plugs the
 * IIGS into it: memory, ROMs, settings, the battery RAM, keyboard and mouse,
 * the drives (3.5", 5.25", two hard disks), status.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "emu/emu.h"
#if CONFIG_EMU_SPEAKER
#include "emu/speaker.h"
#endif
#include "gs.h"
#include "rom_store.h"
#include "settings.h"

static const char *TAG = "main";

/* The Tab5 has room for up to 8 MB; the MaTouch for the IIGS's own 1 MB */
#if CONFIG_IDF_TARGET_ESP32P4
#define RAM_CHOICE          1
#else
#define RAM_CHOICE          0
#endif

/* PSRAM the rest needs at most: the LCD's framebuffer, WiFi, BLE, LVGL
 * while the setup screen is open
 */
#define PSRAM_FOR_THE_REST  (2u * 1024 * 1024)
/* What the IIGS needs besides its RAM: ROM, Mega II, picture, Clemens's
 * state, the CPU's page tables, the drives' nibbles (gs_prepare())
 */
#define PSRAM_FOR_THE_GS    (3200u * 1024)

static gs_config_t s_cfg;
static uint8_t *s_rom, *s_chargen;
static bool s_started;
static uint32_t s_ram_kb;           /* reserved at power-on */

static void *psram_alloc(uint32_t bytes)
{
    return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
}

void gs_log(int level, const char *msg)
{
    ESP_LOGW("clemens", "%s", msg);
}

/* ---- Memory ----
 *
 * At power-on, before the screen, WiFi and Bluetooth fragment PSRAM: the
 * RAM (as much of the chosen size as fits), the ROMs, the Mega II's 128 KB,
 * the picture, then the machine's own buffers.
 */
static void reserve_memory(void)
{
    settings_load();
    uint32_t kb = RAM_CHOICE ? settings.ram_kb : 1024;
    size_t free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    while (kb > 1024 && (size_t)kb * 1024 + PSRAM_FOR_THE_GS + PSRAM_FOR_THE_REST > free) {
        kb /= 2;
    }
    s_cfg.ram = heap_caps_malloc(kb * 1024, MALLOC_CAP_SPIRAM);
    s_ram_kb = s_cfg.ram ? kb : 0;
    s_rom = psram_alloc(rom_store_size(ROM_GS3));
    s_chargen = psram_alloc(rom_store_size(ROM_CHARGEN));
    s_cfg.mega2 = psram_alloc(128 * 1024);
    s_cfg.frame = heap_caps_calloc(GS_SCREEN_WIDTH * GS_SCREEN_HEIGHT, 2, MALLOC_CAP_SPIRAM);
    s_cfg.alloc = psram_alloc;
    s_cfg.release = heap_caps_free;
    bool ok = gs_prepare(psram_alloc, heap_caps_free);
    ESP_LOGI(TAG, "%u KB of RAM%s; PSRAM free %u KB, largest block %u KB", (unsigned)s_ram_kb,
             ok ? "" : ", the machine's buffers didn't fit",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) / 1024));
}

static void storage(bool sd_card)
{
    emu_set_drive_info(sd_card ? "Disk images and hard disks on the SD card" : "No SD card: no disks");
}

/* ---- Starting ---- */

static bool start(void)
{
    if (!s_cfg.ram || !s_rom || !s_chargen || !s_cfg.mega2 || !s_cfg.frame) {
        emu_set_message("Not enough memory: see the log.");
        return false;
    }
    if (RAM_CHOICE && settings.ram_kb > s_ram_kb) {
        /* More than fitted at power-on: reserve it first thing after a restart */
        emu_reboot_into_machine();
        return false;
    }
    if (!rom_store_load(ROM_GS3, s_rom) || !rom_store_load(ROM_CHARGEN, s_chargen)) {
        emu_set_message("No ROMs: put gs_rom3.rom and chargen.rom into roms/ and flash with idf.py flash, "
                        "or copy them into the \"roms\" folder on the SD card.");
        return false;
    }
    s_cfg.ram_kb = RAM_CHOICE ? settings.ram_kb : 1024;
    s_cfg.rom = s_rom;
    s_cfg.chargen = s_chargen;
    s_cfg.bram = settings.have_bram ? settings.bram : NULL;
    if (!gs_init(&s_cfg)) {
        emu_set_message("The IIGS could not be set up: see the log.");
        return false;
    }
    s_started = true;
    ESP_LOGI(TAG, "Apple IIGS ROM 3, %u KB", (unsigned)s_cfg.ram_kb);
    return true;
}

static void run_frame(void)
{
    gs_run_frame();
    uint32_t ejected = gs_disk_ejected();
    for (int d = 0; ejected; d++, ejected >>= 1) {
        if (ejected & 1) {
            disks_ejected(d);
        }
    }
}

static void screen_mode(emu_screen_t *mode)
{
    mode->width = GS_SCREEN_WIDTH;
    mode->height = GS_SCREEN_HEIGHT;
    mode->hz = 60;
}

static void render_line(int y, uint16_t *dst)
{
    memcpy(dst, gs_line(y), GS_SCREEN_WIDTH * sizeof(uint16_t));
}

/* ---- Keyboard, mouse, touch ----
 *
 * By position, as Apple's ADB keyboard (US): Alt is Option, the Windows or
 * Command key Command (the open apple), F12 or Pause the Reset key
 * (Control-F12: Control-Reset; Control-Command-Esc: the control panel).
 */
#define K(code)     ((code) | 0x80)
static const uint8_t usage_to_adb[0x68] = {
    [0x04] = K(0x00), [0x05] = K(0x0B), [0x06] = K(0x08), [0x07] = K(0x02),     /* a b c d */
    [0x08] = K(0x0E), [0x09] = K(0x03), [0x0A] = K(0x05), [0x0B] = K(0x04),     /* e f g h */
    [0x0C] = K(0x22), [0x0D] = K(0x26), [0x0E] = K(0x28), [0x0F] = K(0x25),     /* i j k l */
    [0x10] = K(0x2E), [0x11] = K(0x2D), [0x12] = K(0x1F), [0x13] = K(0x23),     /* m n o p */
    [0x14] = K(0x0C), [0x15] = K(0x0F), [0x16] = K(0x01), [0x17] = K(0x10),     /* q r s t */
    [0x18] = K(0x20), [0x19] = K(0x09), [0x1A] = K(0x0D), [0x1B] = K(0x07),     /* u v w x */
    [0x1C] = K(0x11), [0x1D] = K(0x06),                                   /* y z */
    [0x1E] = K(0x12), [0x1F] = K(0x13), [0x20] = K(0x14), [0x21] = K(0x15),     /* 1 2 3 4 */
    [0x22] = K(0x17), [0x23] = K(0x16), [0x24] = K(0x1A), [0x25] = K(0x1C),     /* 5 6 7 8 */
    [0x26] = K(0x19), [0x27] = K(0x1D),                                   /* 9 0 */
    [0x28] = K(0x24),      /* Return */
    [0x29] = K(0x35),      /* Esc */
    [0x2A] = K(0x33),      /* Backspace -> Delete */
    [0x2B] = K(0x30),      /* Tab */
    [0x2C] = K(0x31),      /* Space */
    [0x2D] = K(0x1B),      /* - */
    [0x2E] = K(0x18),      /* = */
    [0x2F] = K(0x21),      /* [ */
    [0x30] = K(0x1E),      /* ] */
    [0x31] = K(0x2A),      /* \ */
    [0x32] = K(0x2A),      /* non-US # */
    [0x33] = K(0x29),      /* ; */
    [0x34] = K(0x27),      /* ' */
    [0x35] = K(0x32),      /* ` */
    [0x36] = K(0x2B),      /* , */
    [0x37] = K(0x2F),      /* . */
    [0x38] = K(0x2C),      /* / */
    [0x3A] = K(0x7A), [0x3B] = K(0x78), [0x3C] = K(0x63), [0x3D] = K(0x76),     /* F1-F4 */
    [0x3E] = K(0x60), [0x3F] = K(0x61), [0x40] = K(0x62), [0x41] = K(0x64),     /* F5-F8 */
    [0x42] = K(0x65), [0x43] = K(0x6D), [0x44] = K(0x67),                    /* F9-F11 */
    [0x45] = K(0x7F),      /* F12 -> Reset */
    [0x48] = K(0x7F),      /* Pause -> Reset */
    [0x49] = K(0x72),      /* Insert -> Help */
    [0x4A] = K(0x73),      /* Home */
    [0x4B] = K(0x74),      /* Page Up */
    [0x4C] = K(0x75),      /* Delete -> keypad's delete */
    [0x4D] = K(0x77),      /* End */
    [0x4E] = K(0x79),      /* Page Down */
    [0x4F] = K(0x3C),      /* Right */
    [0x50] = K(0x3B),      /* Left */
    [0x51] = K(0x3D),      /* Down */
    [0x52] = K(0x3E),      /* Up */
    [0x53] = K(0x47),      /* Num Lock -> Clear */
    [0x54] = K(0x4B),      /* keypad / */
    [0x55] = K(0x43),      /* keypad * */
    [0x56] = K(0x4E),      /* keypad - */
    [0x57] = K(0x45),      /* keypad + */
    [0x58] = K(0x4C),      /* keypad Enter */
    [0x59] = K(0x53), [0x5A] = K(0x54), [0x5B] = K(0x55),                    /* keypad 1 2 3 */
    [0x5C] = K(0x56), [0x5D] = K(0x57), [0x5E] = K(0x58),                    /* keypad 4 5 6 */
    [0x5F] = K(0x59), [0x60] = K(0x5B), [0x61] = K(0x5C),                    /* keypad 7 8 9 */
    [0x62] = K(0x52),      /* keypad 0 */
    [0x63] = K(0x41),      /* keypad . */
    [0x64] = K(0x0A),      /* non-US \ (ISO key left of Z) */
    [0x67] = K(0x51),      /* keypad = */
};

/* Modifier usages $E0-$E7: LCtrl LShift LAlt LGUI RCtrl RShift RAlt RGUI ->
 * Control, Shift, Option, Command (one key each here)
 */
static const uint8_t modifier_to_adb[8] = { 0x36, 0x38, 0x3A, 0x37, 0x36, 0x38, 0x3A, 0x37 };

#define ADB_CAPS_LOCK   0x39

static uint8_t s_modifiers;
static bool s_caps;

static void hid_key(uint8_t usage, bool pressed)
{
    if (usage >= 0xE0 && usage <= 0xE7) {
        int bit = usage - 0xE0;
        uint8_t code = modifier_to_adb[bit];
        uint8_t mask = 0;
        for (int i = 0; i < 8; i++) {
            mask |= modifier_to_adb[i] == code ? 1u << i : 0;
        }
        bool was = (s_modifiers & mask) != 0;
        s_modifiers = pressed ? s_modifiers | (1u << bit) : s_modifiers & ~(1u << bit);
        bool now = (s_modifiers & mask) != 0;
        if (was != now) {
            gs_key(code, now);
        }
        return;
    }
    if (usage == 0x39) {
        /* Caps Lock: a key that stays down on Apple's keyboards */
        if (pressed) {
            s_caps = !s_caps;
            gs_key(ADB_CAPS_LOCK, s_caps);
        }
        return;
    }
    if (usage < sizeof(usage_to_adb) && usage_to_adb[usage]) {
        gs_key(usage_to_adb[usage] & 0x7F, pressed);
    }
}

static void hid_mouse(int dx, int dy, uint8_t buttons)
{
    gs_mouse(dx, dy, buttons != 0);
}

/* A finger on the picture is the pointer (desktop programs): down there clicks */
static void touch(int x, int y, bool pressed)
{
    gs_mouse_position(x, y, pressed);
}

/* ---- Setup screen ---- */

/* Memory: up to 8 MB on the Tab5; the MaTouch's row has its one size (only
 * the IIGS's own 1 MB fits next to the rest, see reserve_memory())
 */
#if RAM_CHOICE
static const char *const ram_names[] = { "1 MB", "2 MB", "4 MB", "8 MB", "" };
static const uint16_t ram_sizes[] = { 1024, 2048, 4096, 8192 };
#else
static const char *const ram_names[] = { "1 MB", "" };
static const uint16_t ram_sizes[] = { 1024 };
#endif
#define RAM_SIZES   (sizeof(ram_sizes) / sizeof(ram_sizes[0]))

static int ram_get(void)
{
    for (unsigned i = 0; i < RAM_SIZES; i++) {
        if (ram_sizes[i] == settings.ram_kb) {
            return i;
        }
    }
    return 0;
}

static void ram_set(int i)
{
    if (i >= 0 && (unsigned)i < RAM_SIZES && ram_sizes[i] != settings.ram_kb) {
        settings.ram_kb = ram_sizes[i];
        settings_save();
    }
}

static const emu_choice_t choices[] = {
    { "Memory", ram_names, ram_get, ram_set, false },
};
#define CHOICE_COUNT    1

/* ---- Drives: 3.5", 5.25", two hard disks ---- */

enum { DRIVE_35, DRIVE_525, DRIVE_HD1, DRIVE_HD2, DRIVES };

#define DISK_800K       819200
#define DISK_140K       143360
#define HD_32M          (32u * 1024 * 1024 - 512)   /* ProDOS's largest: 65535 blocks */
#define HD_16M          (16u * 1024 * 1024)

static const char *const extensions[] = { ".po", ".2mg", ".hdv", ".dsk", ".do", ".woz", NULL };
static const char *const drive_names[] = { "3.5\"", "5.25\"", "Hard disk 1", "Hard disk 2" };
static const disks_size_t sizes[] = {
    { DISK_800K, "800 KB 3.5\" disk" },
    { DISK_140K, "140 KB 5.25\" disk" },
    { HD_32M, "32 MB hard disk" },
    { HD_16M, "16 MB hard disk" },
};

static bool has_ext(const char *name, const char *ext)
{
    size_t n = strlen(name), e = strlen(ext);
    return n > e && strcasecmp(name + n - e, ext) == 0;
}

typedef enum { KIND_NONE, KIND_35, KIND_525, KIND_BLOCKS } kind_t;

/* What an image is: a .WOZ says so itself, the others go by size (a 3.5"
 * or 5.25" floppy's, else a hard disk's blocks)
 */
static kind_t kind_of(const char *name)
{
    char path[160];
    snprintf(path, sizeof(path), "%s/%s", disks_folder(), name);
    if (has_ext(name, ".woz")) {
        FILE *f = fopen(path, "rb");
        uint8_t h[22] = { 0 };
        size_t n = f ? fread(h, 1, sizeof(h), f) : 0;
        if (f) {
            fclose(f);
        }
        if (n < sizeof(h) || memcmp(h, "WOZ", 3) != 0) {
            return KIND_NONE;
        }
        return h[21] == 2 ? KIND_35 : h[21] == 1 ? KIND_525 : KIND_NONE;
    }
    struct stat st;
    if (stat(path, &st) != 0) {
        return KIND_NONE;
    }
    uint32_t bytes = st.st_size - (has_ext(name, ".2mg") ? 64 : 0);
    if (bytes == DISK_800K || bytes == 2 * DISK_800K) {
        return KIND_35;
    }
    if (bytes == DISK_140K) {
        return KIND_525;
    }
    return KIND_BLOCKS;
}

static bool accepts(int drive, const char *name)
{
    kind_t k = kind_of(name);
    switch (drive) {
    case DRIVE_35: return k == KIND_35;
    case DRIVE_525: return k == KIND_525;
    default: return !has_ext(name, ".woz") && !has_ext(name, ".dsk") && !has_ext(name, ".do") && k != KIND_NONE;
    }
}

static uint32_t default_size(int drive)
{
    switch (drive) {
    case DRIVE_525: return DISK_140K;
    case DRIVE_HD1: case DRIVE_HD2: return HD_32M;
    default: return DISK_800K;
    }
}

static const char *extension_for(uint32_t bytes)
{
    return bytes > 2 * DISK_800K ? ".hdv" : ".po";
}

static bool create(const char *path, uint32_t bytes)
{
    return gs_disk_create(path, bytes);
}

/* The hard disks are part of the machine: it starts again */
static void restart_if_started(void)
{
    if (s_started) {
        gs_reset();
    }
}

static bool insert(int drive, const char *path)
{
    if (!gs_disk_insert(drive, path)) {
        return false;
    }
    if (drive >= DRIVE_HD1) {
        restart_if_started();
    }
    return true;
}

static void eject(int drive)
{
    gs_disk_eject(drive);
    if (drive >= DRIVE_HD1) {
        restart_if_started();
    }
}

static const char *path(int drive)
{
    return gs_disk_path(drive);
}

static bool write_protected(int drive)
{
    return gs_disk_write_protected(drive);
}

static const disks_config_t disks = {
    .folder = "apple2gs",
    .extensions = extensions,
    .description = "Images (.PO .2MG .HDV .DSK .WOZ)",
    .title = "Drives",
    .button = "Disks",
    .nvs_namespace = "gsdisks",
    .drives = DRIVES,
    .drive_names = drive_names,
    .new_prefix = "BLANK",
    .new_extension = ".po",
    .sizes = sizes,
    .size_count = sizeof(sizes) / sizeof(sizes[0]),
    .max_upload = 64ull * 1024 * 1024,
    .insert = insert,
    .eject = eject,
    .path = path,
    .write_protected = write_protected,
    .create = create,
    .accepts = accepts,
    .default_size = default_size,
    .extension_for = extension_for,
};

static void flush(void *arg)
{
    gs_disk_flush();
}

static void every_second(void)
{
    static int count;
    static uint32_t writes;
    gs_status_t s;
    gs_get_status(&s);
    /* Hard disks written to two seconds ago and not since: onto the card
     * (floppies write themselves back once their motor stops)
     */
    uint32_t w = s.writes[GS_DRIVE_HD1] + s.writes[GS_DRIVE_HD2];
    if (w != writes) {
        writes = w;
        count = 2;
    } else if (count > 0 && --count == 0) {
        emu_paused(flush, NULL);
    }
    /* The control panel's settings */
    if (s_started && gs_bram_changed()) {
        uint8_t bram[GS_BRAM_SIZE];
        gs_bram_get(bram);
        if (!settings.have_bram || memcmp(bram, settings.bram, sizeof(bram)) != 0) {
            memcpy(settings.bram, bram, sizeof(bram));
            settings_save_bram();
            ESP_LOGI(TAG, "battery RAM saved");
        }
    }
}

/* ---- Status ---- */

static void status(emu_json_t *j, int section)
{
    char text[160];
    gs_status_t s;
    gs_get_status(&s);
    if (section == EMU_STATUS_ROWS) {
        snprintf(text, sizeof(text), "Apple IIGS, ROM 3, %u MB", (unsigned)(s_cfg.ram_kb / 1024));
        emu_json_row(j, "System", text);
        emu_json_row(j, "CPU", s.fast ? "2.8 MHz (fast)" : "1 MHz (slow)");
        /* Under GS/OS: its Memory Manager's free memory, of the RAM and the
         * Mega II's 128 KB
         */
        uint32_t ram_free, largest, total = (s_cfg.ram_kb + 128) * 1024;
        if (s_started && gs_ram_free(&ram_free, &largest)) {
            snprintf(text, sizeof(text), "%lu KB of %lu KB <span class=\"dim\">(largest block %lu KB)</span>",
                     (unsigned long)(ram_free / 1024), (unsigned long)(total / 1024),
                     (unsigned long)(largest / 1024));
            emu_json_row_bar(j, "Free RAM", text, 100 - (int)((uint64_t)ram_free * 100 / total));
        }
        return;
    }
    for (int d = 0; d < DRIVES; d++) {
        const char *name = disks_name(d);
        const char *detail = NULL;
        if (d < DRIVE_HD1 && name && s.track[d] >= 0) {
            snprintf(text, sizeof(text), "track %d", s.track[d]);
            detail = text;
        }
        emu_json_drive(j, drive_names[d], name, write_protected(d), s.busy[d], s.reads[d], s.writes[d], detail);
    }
}

/* ---- Console ---- */

static void console_info(char *buf, size_t len)
{
    gs_status_t s;
    gs_get_status(&s);
    int n = snprintf(buf, len, "frames %lu PC $%06lX %s %s; kcycles/frame: ", (unsigned long)s.frames,
                     (unsigned long)s.pc, s.fast ? "fast" : "slow", s.emulation ? "emulation" : "native");
#if CONFIG_EMU_SPEAKER
    if (n > 0 && (size_t)n < len) {
        n += snprintf(buf + n, len - n, "speaker missed %lu frames; ", (unsigned long)speaker_missing_frames());
    }
#endif
    if (n > 0 && (size_t)n < len) {
        gs_profile(buf + n, len - n);
    }
}

/*   k <hex>            tap an ADB key ($24 Return, $31 space, $35 Esc)
 *   K <hex> <0|1>      ADB key down (1) or up (0); $36 Control, $37 Command,
 *                      $38 Shift, $3A Option, $7F Reset
 *   T <text>           type text (\n is Return)
 *   m <dx> <dy> <btn>  mouse
 *   g [<hex addr> <n>] registers, the last bus accesses, n bytes at addr
 *   R <MB>             restart with that much RAM (Tab5)
 */
static bool console_command(const char *line)
{
    switch (line[0]) {
    case 'k': {
        unsigned key = strtoul(line + 1, NULL, 16);
        gs_key(key, true);
        vTaskDelay(pdMS_TO_TICKS(100));
        gs_key(key, false);
        return true;
    }
    case 'K': {
        unsigned key = 0;
        int down = 0;
        if (sscanf(line + 1, "%x %d", &key, &down) == 2) {
            gs_key(key, down);
        }
        return true;
    }
    case 'T': {
        char text[256];
        const char *s = line + 1;
        while (*s == ' ') {
            s++;
        }
        size_t n = 0;
        for (; *s && n < sizeof(text) - 1; s++) {
            if (s[0] == '\\' && s[1] == 'n') {
                text[n++] = '\n';
                s++;
            } else {
                text[n++] = *s;
            }
        }
        text[n] = 0;
        gs_type(text);
        return true;
    }
    case 'g': {
        unsigned addr = 0;
        int n = 0;
        sscanf(line + 1, "%x %d", &addr, &n);
        char *buf = malloc(1024);
        if (buf) {
            gs_debug(buf, 1024, addr, n > 64 ? 64 : n);
            printf("%s\n", buf);
            free(buf);
        }
        return true;
    }
    case 'm': {
        int dx = 0, dy = 0, b = 0;
        sscanf(line + 1, "%d %d %d", &dx, &dy, &b);
        gs_mouse(dx, dy, b != 0);
        return true;
    }
#if RAM_CHOICE
    case 'R': {
        int mb = atoi(line + 1);
        if (mb == 1 || mb == 2 || mb == 4 || mb == 8) {
            settings.ram_kb = mb * 1024;
            settings_save();
            emu_restart();
        }
        return true;
    }
#endif
    default:
        return false;
    }
}

/* ---- The machine ---- */

static const emu_machine_t machine = {
    .name = "Apple IIGS",
    .short_name = "IIGS",
    .view_width = 816,          /* 704 x 216: the 640 x 200 area at 4:3 (1 Super Hi-Res pixel 1.16 wide) */
    .scale = EMU_SCALE_SMOOTH,
    .reserve_memory = reserve_memory,
    .storage = storage,
    .start = start,
    .reset = gs_reset,
    .run_frame = run_frame,
    .screen_mode = screen_mode,
    .screen_signature = gs_screen_signature,
    .render_line = render_line,
    .audio_hz = GS_AUDIO_HZ,
    .audio_enable = gs_audio_enable,
    .audio_render = gs_audio_render,
    .audio_pending = gs_audio_pending,
    .hid_key = hid_key,
    .hid_mouse = hid_mouse,
    .touch = touch,
    .choices = choices,
    .choice_count = CHOICE_COUNT,
    .disks = &disks,
    .status = status,
    .console_info = console_info,
    .console_command = console_command,
    .every_second = every_second,
};

void app_main(void)
{
    emu_run(&machine);
}
