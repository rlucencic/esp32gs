#pragma once

/*
 * The Apple IIGS (ROM 3), platform independent: the 65C816 (esp32emu's
 * core) with page tables for its memory, and the machine's I/O, memory map
 * and devices from Clemens (Samir Sinha's IIGS emulator, MIT licence, in
 * clemens/): the Mega II and FPI memory map with shadowing and the language
 * card, the VGC's modes and interrupts, ADB keyboard and mouse, the clock
 * and battery RAM, the Ensoniq DOC and the speaker, the IWM with a 3.5"
 * and a 5.25" drive, and a hard disk card in slot 7 with two units.
 *
 * Drives:
 *   GS_DRIVE_35     slot 5 drive 1: 800 KB disks (.2mg .po .woz)
 *   GS_DRIVE_525    slot 6 drive 1: 140 KB disks (.dsk .do .po .nib .woz)
 *   GS_DRIVE_HD1/2  slot 7: ProDOS block images (.po .hdv .2mg), any
 *                   size up to 32 MB, read and written in place
 */
#include <stdbool.h>
#include <stdint.h>

/* The picture: the 640 x 200 Super Hi-Res area (the Apple II modes'
 * 560 x 192 centred in it) with a border of 32 pixels left and right and
 * 8 lines above and below
 */
#define GS_SCREEN_WIDTH     704
#define GS_BORDER_X         32
#define GS_BORDER_Y         8
#define GS_SCREEN_HEIGHT    216
#define GS_AUDIO_HZ         32000
#define GS_BRAM_SIZE        256

enum { GS_DRIVE_35, GS_DRIVE_525, GS_DRIVE_HD1, GS_DRIVE_HD2, GS_DRIVES };

typedef struct {
    uint32_t ram_kb;            /* fast RAM: 1024 ... 8192 (banks $00 up) */
    const uint8_t *rom;         /* 256 KB, ROM 3 (banks $FC-$FF) */
    const uint8_t *chargen;     /* 4 KB, the text characters (the IIe's enhanced video ROM) */
    uint8_t *ram;               /* ram_kb KB */
    uint8_t *mega2;             /* 128 KB: banks $E0, $E1 */
    uint16_t *frame;            /* GS_SCREEN_WIDTH * GS_SCREEN_HEIGHT, RGB565 */
    const uint8_t *bram;        /* the battery RAM to start with (NULL: the ROM's defaults) */
    void *(*alloc)(uint32_t bytes);     /* large buffers: PSRAM */
    void (*release)(void *p);
} gs_config_t;

/* The machine's own buffers (about 2.5 MB through alloc), first thing at
 * power-on so that the big ones still fit; gs_init() does it otherwise
 */
bool gs_prepare(void *(*alloc)(uint32_t bytes), void (*release)(void *p));
bool gs_init(const gs_config_t *cfg);
void gs_reset(void);                /* power off and on */
void gs_run_frame(void);            /* 1/60 s */
uint32_t gs_screen_signature(void);
const uint16_t *gs_line(int y);

/* Keys as ADB key codes (Clemens's CLEM_ADB_KEY_*: Control $36, the arrows $3B-$3E, Reset $7F), from any task */
void gs_key(uint8_t adb_code, bool down);
void gs_mouse(int dx, int dy, bool button);
/* The pointer to picture pixel x, y (touch); desktop programs only */
void gs_mouse_position(int x, int y, bool button);
/* The ADB code for an ASCII character and whether it needs Shift; -1 if none */
int gs_ascii_key(char c, bool *shift);
void gs_type(const char *text);

bool gs_disk_insert(int drive, const char *path);
void gs_disk_eject(int drive);
const char *gs_disk_path(int drive);
bool gs_disk_write_protected(int drive);
/* Drives (bits) whose disk the computer ejected since the last call */
uint32_t gs_disk_ejected(void);
bool gs_disk_dirty(void);
bool gs_disk_flush(void);
/* A blank image: 800 KB or 140 KB (zeroed: GS/OS or ProDOS formats it), or
 * a hard disk of that many bytes
 */
bool gs_disk_create(const char *path, uint32_t bytes);

/* The battery RAM (control panel settings), to keep */
bool gs_bram_changed(void);
void gs_bram_get(uint8_t *bram);

void gs_audio_enable(bool enable);
int gs_audio_render(int16_t *dst, int frames);
int gs_audio_pending(void);

/* Free memory as GS/OS's Memory Manager counts it (FreeMem), and its
 * largest free block; false if there's no Memory Manager list (yet)
 */
bool gs_ram_free(uint32_t *free_bytes, uint32_t *largest);

typedef struct {
    uint32_t frames;
    uint32_t pc;                /* PBR:PC */
    bool fast;
    bool emulation;
    uint32_t reads[GS_DRIVES], writes[GS_DRIVES];
    bool busy[GS_DRIVES];
    int track[GS_DRIVES];
} gs_status_t;
void gs_get_status(gs_status_t *s);
/* Where a frame's time goes (thousands of cycles, the board only) */
void gs_profile(char *buf, int len);
/* Registers, the last bus callbacks and n bytes at addr (debugging) */
void gs_debug(char *buf, int len, uint32_t addr, int n);
