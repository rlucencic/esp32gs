/*
 * The picture, drawn at the end of each frame from the Mega II's memory
 * (E0 main, E1 aux) and the VGC's mode as Clemens keeps it.
 *
 *   Super Hi-Res  E1 $2000-$9CFF, 200 lines of 160 bytes; per line its
 *                 control byte ($9D00: 640 mode, fill mode, palette) and
 *                 one of 16 palettes ($9E00, 16 x 16 colours, 0RGB)
 *   Apple II      560 x 192 in the middle of the 640 x 200 area: text 40
 *                 and 80 columns (the IIe's enhanced characters, flashing
 *                 at about 2 Hz, the alternate set with MouseText), lores,
 *                 double lores, hires (colours from neighbouring bits, or
 *                 monochrome), double hires; mixed mode, page 2
 *
 * Each picture line has a key (a hash of everything it shows); a line
 * whose key didn't change isn't drawn again, and the screen signature
 * moves only when one was.
 */
#include <string.h>
#include "gs_internal.h"
#include "clem_mmio_defs.h"

#define BORDER_X    GS_BORDER_X
#define BORDER_Y    GS_BORDER_Y
#define A2_X        (BORDER_X + 40)     /* the 560 x 192 area */
#define A2_Y        (BORDER_Y + 4)

/* The IIGS's 16 colours (4 bits each of R, G, B) */
static const uint8_t gs_rgb[16][3] = {
    { 0x0, 0x0, 0x0 }, { 0xD, 0x0, 0x3 }, { 0x0, 0x0, 0x9 }, { 0xD, 0x2, 0xD },
    { 0x0, 0x7, 0x2 }, { 0x5, 0x5, 0x5 }, { 0x2, 0x2, 0xF }, { 0x6, 0xA, 0xF },
    { 0x8, 0x5, 0x0 }, { 0xF, 0x6, 0x0 }, { 0xA, 0xA, 0xA }, { 0xF, 0x9, 0x8 },
    { 0x1, 0xD, 0x0 }, { 0xF, 0xF, 0x0 }, { 0x4, 0xF, 0x9 }, { 0xF, 0xF, 0xF },
};

static uint16_t s_color[16];
static const uint8_t s_none[1];
static uint32_t s_key[GS_SCREEN_HEIGHT];
static int s_flash_frames;
static bool s_flash;

static inline uint16_t rgb444(unsigned r, unsigned g, unsigned b)
{
    r = r * 17;
    g = g * 17;
    b = b * 17;
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

void video_init(void)
{
    for (int i = 0; i < 16; i++) {
        s_color[i] = rgb444(gs_rgb[i][0], gs_rgb[i][1], gs_rgb[i][2]);
    }
    memset(s_key, 0xFF, sizeof(s_key));
}

static inline uint32_t fnv32(uint32_t h, uint32_t v)
{
    for (int i = 0; i < 4; i++, v >>= 8) {
        h = (h ^ (v & 0xFF)) * 16777619u;
    }
    return h;
}

/* Each picture line's key: a hash of its sources (whole words: their
 * offsets and lengths are multiples of 4) and of its mode, colours, ...;
 * a line is drawn again only when its key changed. (A copy to compare
 * with cost twice the PSRAM reads.)
 */
static inline uint32_t hash_words(uint32_t h, const uint8_t *p, int n)
{
    const uint32_t *w = (const uint32_t *)p;
    for (int i = 0; i < n / 4; i++) {
        h = (h ^ w[i]) * 0x9E3779B1u;
        h ^= h >> 15;
    }
    return h;
}

static bool line_same(int y, uint32_t key, const uint8_t *a, int na, const uint8_t *b, int nb, const uint8_t *c,
                      int nc)
{
    uint32_t h = hash_words(hash_words(hash_words(key, a, na), b, nb), c, nc);
    if (h == s_key[y]) {
        return true;
    }
    s_key[y] = h;
    return false;
}

static inline void fill(uint16_t *d, int n, uint16_t c)
{
    for (int i = 0; i < n; i++) {
        d[i] = c;
    }
}

/* ---- Super Hi-Res ---- */

static void shr_line(uint16_t *d, const uint8_t *e1, int line)
{
    uint8_t scb = e1[0x9D00 + line];
    const uint8_t *pal = e1 + 0x9E00 + (scb & 15) * 32;
    uint16_t c[16];
    for (int i = 0; i < 16; i++) {
        c[i] = rgb444(pal[i * 2 + 1] & 15, pal[i * 2] >> 4, pal[i * 2] & 15);
    }
    const uint8_t *px = e1 + 0x2000 + line * 160;
    if (scb & 0x80) {                       /* 640: 4 pixels a byte, a palette quarter each */
        for (int i = 0; i < 160; i++) {
            uint8_t b = px[i];
            d[0] = c[8 + (b >> 6)];
            d[1] = c[12 + ((b >> 4) & 3)];
            d[2] = c[(b >> 2) & 3];
            d[3] = c[4 + (b & 3)];
            d += 4;
        }
        return;
    }
    bool fillmode = scb & 0x20;
    uint16_t last = c[0];
    for (int i = 0; i < 160; i++) {
        uint8_t b = px[i];
        for (int k = 0; k < 2; k++, b <<= 4) {
            unsigned v = (b >> 4) & 15;
            uint16_t col = (v == 0 && fillmode) ? last : c[v];
            last = col;
            d[0] = d[1] = col;
            d += 2;
        }
    }
}

/* ---- Apple II modes ---- */

static inline uint16_t text_row_addr(int row)
{
    return ((row & 7) << 7) + (row >> 3) * 40;
}

static inline uint16_t hires_row_addr(int y)
{
    return ((y & 7) << 10) + (((y >> 3) & 7) << 7) + (y >> 6) * 40;
}

/* A character's dots for a row: 7 bits, bit 0 leftmost, set = lit */
static inline uint8_t glyph(uint8_t code, int row, bool alt)
{
    const uint8_t *rom = gs.cfg.chargen;
    if (!rom) {
        return 0;
    }
    if (!alt && code >= 0x40 && code < 0x80) {
        code = s_flash ? code & 0x3F : (code & 0x3F) | 0x80;    /* flashing */
    }
    return ~rom[code * 8 + row] & 0x7F;
}

static void text_row(uint16_t *d, const uint8_t *e0, const uint8_t *e1, int y, bool col80, uint16_t base,
                     bool alt, uint16_t fg, uint16_t bg)
{
    int row = y >> 3, r = y & 7;
    uint16_t a = base + text_row_addr(row);
    if (col80) {
        for (int c = 0; c < 80; c++) {
            uint8_t ch = (c & 1) ? e0[a + (c >> 1)] : e1[a + (c >> 1)];
            uint8_t bits = glyph(ch, r, alt);
            for (int k = 0; k < 7; k++) {
                *d++ = (bits >> k) & 1 ? fg : bg;
            }
        }
        return;
    }
    for (int c = 0; c < 40; c++) {
        uint8_t bits = glyph(e0[a + c], r, alt);
        for (int k = 0; k < 7; k++) {
            d[0] = d[1] = (bits >> k) & 1 ? fg : bg;
            d += 2;
        }
    }
}

static void lores_row(uint16_t *d, const uint8_t *e0, const uint8_t *e1, int y, bool dbl, uint16_t base)
{
    uint16_t a = base + text_row_addr(y >> 3);
    int shift = (y & 4) ? 4 : 0;
    if (dbl) {
        for (int c = 0; c < 80; c++) {
            uint8_t v = ((c & 1) ? e0[a + (c >> 1)] : e1[a + (c >> 1)]) >> shift & 15;
            if (!(c & 1)) {
                v = ((v << 1) | (v >> 3)) & 15;     /* aux colours are rotated */
            }
            fill(d, 7, s_color[v]);
            d += 7;
        }
        return;
    }
    for (int c = 0; c < 40; c++) {
        fill(d, 14, s_color[(e0[a + c] >> shift) & 15]);
        d += 14;
    }
}

static void hires_row(uint16_t *d, const uint8_t *e0, int y, uint16_t base, bool mono)
{
    const uint8_t *p = e0 + base + hires_row_addr(y);
    uint8_t dots[282];
    uint8_t pal[282];
    dots[0] = dots[281] = 0;
    for (int c = 0; c < 40; c++) {
        for (int k = 0; k < 7; k++) {
            dots[1 + c * 7 + k] = (p[c] >> k) & 1;
            pal[1 + c * 7 + k] = p[c] >> 7;
        }
    }
    uint16_t white = s_color[15], black = s_color[0];
    for (int x = 0; x < 280; x++) {
        uint16_t col;
        if (!dots[x + 1]) {
            col = black;
        } else if (mono || dots[x] || dots[x + 2]) {
            col = white;
        } else if (pal[x + 1]) {
            col = (x & 1) ? s_color[9] : s_color[6];        /* orange / blue */
        } else {
            col = (x & 1) ? s_color[12] : s_color[3];       /* green / purple */
        }
        d[0] = d[1] = col;
        d += 2;
    }
}

static void dhires_row(uint16_t *d, const uint8_t *e0, const uint8_t *e1, int y, uint16_t base, bool mono)
{
    uint16_t a = base + hires_row_addr(y);
    uint8_t dots[560];
    int n = 0;
    for (int c = 0; c < 40; c++) {
        for (int k = 0; k < 7; k++) {
            dots[n++] = (e1[a + c] >> k) & 1;
        }
        for (int k = 0; k < 7; k++) {
            dots[n++] = (e0[a + c] >> k) & 1;
        }
    }
    if (mono) {
        for (int x = 0; x < 560; x++) {
            d[x] = dots[x] ? s_color[15] : s_color[0];
        }
        return;
    }
    for (int x = 0; x < 560; x += 4) {
        unsigned v = dots[x] | dots[x + 1] << 1 | dots[x + 2] << 2 | dots[x + 3] << 3;
        fill(d + x, 4, s_color[v]);
    }
}

/* ---- The frame ---- */

void video_frame(void)
{
    ClemensMMIO *mmio = gs.mmio;
    const uint8_t *e0 = mmio->e0_bank, *e1 = mmio->e1_bank;
    unsigned mode = mmio->vgc.mode_flags;
    uint32_t mmap = mmio->mmap_register;
    bool shr = (mmio->new_video_c029 & CLEM_MMIO_NEWVIDEO_SUPERHIRES_ENABLE) != 0;
    uint16_t border = s_color[mmio->dev_rtc.ctl_c034 & 15];
    bool page2 = (mmap & CLEM_MEM_IO_MMAP_TXTPAGE2) && !(mmap & CLEM_MEM_IO_MMAP_80COLSTORE);
    bool graphics = mode & CLEM_VGC_GRAPHICS_MODE;
    bool mixed = mode & CLEM_VGC_MIXED_TEXT;
    bool col80 = mode & CLEM_VGC_80COLUMN_TEXT;
    bool dbl = col80 && (mode & CLEM_VGC_DISABLE_AN3);
    bool hires = mode & CLEM_VGC_HIRES;
    bool alt = mode & CLEM_VGC_ALTCHARSET;
    bool mono = (mode & CLEM_VGC_MONOCHROME) || (mmio->new_video_c029 & 0x20);
    uint16_t fg = s_color[mmio->vgc.text_fg_color & 15], bg = s_color[mmio->vgc.text_bg_color & 15];

    if (++s_flash_frames >= 16) {
        s_flash_frames = 0;
        s_flash = !s_flash;
    }
    uint32_t mode_key = fnv32(2166136261u, mode ^ (shr << 31) ^ (page2 << 30) ^ (mono << 29));
    mode_key = fnv32(mode_key, (mmio->dev_rtc.ctl_c034 & 15) | (mmio->vgc.text_fg_color & 15) << 4 |
                                   (mmio->vgc.text_bg_color & 15) << 8);
    bool drawn = false;

    for (int y = 0; y < GS_SCREEN_HEIGHT; y++) {
        uint16_t *d = gs.cfg.frame + y * GS_SCREEN_WIDTH;
        int sl = y - BORDER_Y, al = y - A2_Y;
        bool in_shr = shr && sl >= 0 && sl < 200;
        bool in_a2 = !shr && al >= 0 && al < 192;
        bool text_line = in_a2 && (!graphics || (mixed && al >= 160));
        uint16_t text_base = page2 ? 0x800 : 0x400, hires_base = page2 ? 0x4000 : 0x2000;
        bool same;
        if (in_shr) {
            uint8_t scb = e1[0x9D00 + sl];
            same = line_same(y, fnv32(mode_key, scb), e1 + 0x2000 + sl * 160, 160, e1 + 0x9E00 + (scb & 15) * 32,
                             32, s_none, 0);
        } else if (in_a2) {
            uint32_t key = fnv32(mode_key, (al & 7) | text_line << 3 | (text_line && !alt && s_flash) << 4);
            if (text_line || !hires) {
                uint16_t a = text_base + text_row_addr(al >> 3);
                same = line_same(y, key, e0 + a, 40, e1 + a, col80 ? 40 : 0, s_none, 0);
            } else {
                uint16_t a = hires_base + hires_row_addr(al);
                same = line_same(y, key, e0 + a, 40, e1 + a, dbl ? 40 : 0, s_none, 0);
            }
        } else {
            same = line_same(y, mode_key, s_none, 0, s_none, 0, s_none, 0);
        }
        if (same) {
            continue;
        }
        drawn = true;
        if (in_shr) {
            fill(d, BORDER_X, border);
            shr_line(d + BORDER_X, e1, sl);
            fill(d + BORDER_X + 640, GS_SCREEN_WIDTH - BORDER_X - 640, border);
            continue;
        }
        if (!in_a2) {
            fill(d, GS_SCREEN_WIDTH, border);
            continue;
        }
        fill(d, A2_X, border);
        fill(d + A2_X + 560, GS_SCREEN_WIDTH - A2_X - 560, border);
        uint16_t *a = d + A2_X;
        if (text_line) {
            text_row(a, e0, e1, al, col80, text_base, alt, fg, bg);
        } else if (!hires) {
            lores_row(a, e0, e1, al, dbl, text_base);
        } else if (dbl) {
            dhires_row(a, e0, e1, al, hires_base, mono);
        } else {
            hires_row(a, e0, al, hires_base, mono);
        }
    }
    if (drawn) {
        gs.signature++;
    }
}
