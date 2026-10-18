/*
 * The Apple IIGS: the 65C816 on Clemens's memory map and devices.
 *
 * Clemens keeps, for every bank, a map of what each 256-byte page is (RAM
 * of which bank, I/O, card ROM, writable or not, shadowed to the Mega II's
 * E0/E1 or not), and rebuilds the maps of banks 00, 01, E0, E1 whenever a
 * soft switch changes them. From those maps this file builds the CPU's own
 * page tables (a pointer per page for reading and for writing), again after
 * every change: ordinary memory is then read and written directly, and
 * only I/O, card ROM and shadowed writes come back here, to Clemens's
 * clem_mmio_read/write (or the write and its shadow copy).
 *
 * Time is Clemens's clock (1 fast cycle = 1000 clocks, a Mega II cycle
 * 2800): set from the CPU's cycle count before every access that reaches
 * the devices. A frame is 262 lines of 64 Mega II cycles plus the stretch;
 * the CPU runs a line at a time at the speed the speed register and the
 * disk drives allow, then the devices catch up (VGC, IWM, serial, sound,
 * game port, ADB, timers, the slot 7 card and its DMA) and their IRQ lines
 * go to the CPU. An access to the Mega II (I/O) costs about two more fast
 * cycles; E0/E1 memory itself is read at full speed here.
 */
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "gs_internal.h"
#include "clem_mmio.h"
#include "clem_mmio_defs.h"
#include "clem_device.h"
#include "clem_drive.h"
#include "clem_vgc.h"
#include "clem_scc.h"
#include "clem_cycle.h"
#include "clem_debug.h"
#include "emulator_mmio.h"

#define MEGA2_PENALTY       2       /* fast cycles an I/O access costs more (at 2.8 MHz only) */
#define INPUT_QUEUE         64
#define AUDIO_RING          8192    /* stereo frames, a power of two */
#define MIX_FRAMES          (GS_AUDIO_HZ / 4)

gs_t gs;

/* Where a frame's time goes (cycle counter, the board only) */
#ifdef ESP_PLATFORM
#include "esp_cpu.h"
#define PROF_NOW()      esp_cpu_get_cycle_count()
#else
#define PROF_NOW()      0u
#endif
enum { P_CPU, P_CARDS, P_VGC, P_IWM, P_SCC, P_SOUND, P_ADB, P_VIDEO, P_AUDIO, P_OTHER, P_COUNT };
static uint32_t s_prof[P_COUNT], s_prof_sum[P_COUNT], s_prof_avg[P_COUNT];
static uint32_t s_bus_calls, s_bus_sum, s_bus_avg;     /* bus callbacks */
static uint64_t s_cycles_frame0;
static uint32_t s_cycles_avg;                           /* 65816 cycles a frame */
#define PROF(i, stmt)   do { uint32_t t0_ = PROF_NOW(); stmt; s_prof[i] += PROF_NOW() - t0_; } while (0)
#ifndef ESP_PLATFORM
void (*gs_host_hook)(void);
#endif

static uint8_t **s_rd, **s_wr;      /* the CPU's page tables */
static uint8_t *s_empty;            /* 64 KB of nothing */
static uint8_t *s_sink;             /* writes to ROM */
static uint8_t *s_slot_rom;         /* the cards' $C800 areas */
static uint32_t s_generation;       /* Clemens's page maps as last built */
static clem_clocks_time_t s_run_clocks; /* the CPU's run began then ... */
static uint64_t s_run_cycles;           /* ... at this cycle */
static clem_clocks_duration_t s_step;   /* clocks a cycle in this run */
static uint32_t s_irq_cards;
static uint64_t s_last_io_cycle;        /* the language card's double reads */
static bool s_reset_held;

/* Input events from any task to the emulation's: a bounded queue for
 * several producers (each slot's sequence number says whose turn it is)
 */
static struct {
    struct {
        struct ClemensInputEvent ev;
        uint32_t seq;
    } slot[INPUT_QUEUE];
    uint32_t head, tail;
} s_input;

static struct {
    char text[1024];
    int pos, len, phase, key;
    bool shift;
} s_type;

static struct {
    bool enabled;
    float *mix;
    int16_t *ring;                  /* AUDIO_RING stereo frames (PSRAM: internal RAM is scarce) */
    volatile uint32_t head, tail;
} s_audio;

void *gs_alloc(uint32_t bytes)
{
    return gs.cfg.alloc ? gs.cfg.alloc(bytes) : malloc(bytes);
}

void gs_release(void *p)
{
    if (gs.cfg.release) {
        gs.cfg.release(p);
    } else {
        free(p);
    }
}

/* ---- The page tables ---- */

static inline uint8_t *bank_mem(uint8_t bank)
{
    ClemensMachine *clem = gs.clem;
    if (bank == 0xE0 || bank == 0xE1) {
        return clem->mem.mega2_bank_map[bank & 1];
    }
    return clem->mem.fpi_bank_map[bank];
}

static void build_page(int b, int p)
{
    struct ClemensMemoryPageMap *map = gs.clem->mem.bank_page_map[b];
    const struct ClemensMemoryPageInfo *pg = &map->pages[p];
    uint32_t i = (uint32_t)b << 8 | p;
    if (pg->flags & CLEM_MEM_IO_MEMORY_MASK) {
        s_rd[i] = s_wr[i] = NULL;
        return;
    }
    uint8_t br = (pg->flags & CLEM_MEM_PAGE_DIRECT_FLAG)    ? b
                 : (pg->flags & CLEM_MEM_PAGE_MAINAUX_FLAG) ? (b & 0xFE) | (pg->bank_read & 1)
                                                            : pg->bank_read;
    s_rd[i] = bank_mem(br) + ((uint32_t)pg->read << 8);
    uint8_t bw = (pg->flags & CLEM_MEM_PAGE_DIRECT_FLAG)    ? b
                 : (pg->flags & CLEM_MEM_PAGE_MAINAUX_FLAG) ? (b & 0xFE) | (pg->bank_write & 1)
                                                            : pg->bank_write;
    const struct ClemensMemoryPageMap *wmap = gs.clem->mem.bank_page_map[bw];
    const struct ClemensMemoryPageInfo *wp = &wmap->pages[p];
    if (!(wp->flags & CLEM_MEM_PAGE_WRITEOK_FLAG)) {
        s_wr[i] = s_sink;
    } else if (wmap->shadow_map && wmap->shadow_map->pages[wp->write]) {
        s_wr[i] = NULL;                         /* and to E0/E1: through here */
    } else {
        s_wr[i] = bank_mem(bw) + ((uint32_t)wp->write << 8);
    }
}

static void build_bank(int b)
{
    for (int p = 0; p < 256; p++) {
        build_page(b, p);
    }
}

/* After Clemens changed its page maps: the pages it marked, in banks 00,
 * 01, E0, E1 (the others never move). Interrupt vectors come from the ROM
 * whatever the language card shows, unless I/O and language card
 * shadowing is off (Tech Note 68)
 */
static inline void maps_follow(void)
{
    if (s_generation == clem_mmio_map_generation) {
        return;
    }
    s_generation = clem_mmio_map_generation;
    static const uint8_t banks[3][2] = { { 0x00, 0x00 }, { 0x01, 0x01 }, { 0xE0, 0xE1 } };
    for (int g = 0; g < 3; g++) {
        for (int w = 0; w < 8; w++) {
            uint32_t bits = clem_mmio_dirty_pages[g][w];
            clem_mmio_dirty_pages[g][w] = 0;
            while (bits) {
                int p = w * 32 + __builtin_ctz(bits);
                bits &= bits - 1;
                build_page(banks[g][0], p);
                if (banks[g][1] != banks[g][0]) {
                    build_page(banks[g][1], p);
                }
            }
        }
    }
    gs.cpu.vector_bank = gs.clem->mem.mmio_niolc(&gs.clem->mem) ? 0x00 : 0xFF;
}

/* ---- The bus ---- */

/* At 1 MHz the CPU keeps the Mega II's pace, on one grid for the whole
 * machine: a line every GS_LINE_CLOCKS from time 0, 65 cycles of which the
 * last is longer by the stretch. The IWM stretches its bit cells at the same
 * moment (clem_iwm.c keeps its scanlines on this grid): Disk II writes time
 * their nibbles by counting CPU cycles, and a CPU out of step with the
 * drive's clock wrote bits late.
 */
static uint64_t s_run_j;                /* a 1 MHz run started at grid cycle j */

static inline clem_clocks_time_t phi0_edge(uint64_t j)
{
    return (j / 65) * GS_LINE_CLOCKS + (j % 65) * CLEM_CLOCKS_PHI0_CYCLE;
}

/* The first cycle edge at or after t */
static inline uint64_t phi0_next(clem_clocks_time_t t)
{
    uint64_t k = t / GS_LINE_CLOCKS;
    uint32_t i = (uint32_t)((t - k * GS_LINE_CLOCKS + CLEM_CLOCKS_PHI0_CYCLE - 1) / CLEM_CLOCKS_PHI0_CYCLE);
    if (i > 64) {
        k++;
        i = 0;
    }
    return k * 65 + i;
}

static inline clem_clocks_time_t clocks_after(uint64_t n)
{
    if (s_step == CLEM_CLOCKS_PHI0_CYCLE) {
        return phi0_edge(s_run_j + n);
    }
    return s_run_clocks + n * s_step;
}

static inline void clock_now(void)
{
    gs.clem->tspec.clocks_spent = clocks_after(gs.cpu.cycles - s_run_cycles);
}

/* The devices' IRQ lines to the CPU, and into Clemens's own summary: the
 * interrupt handler reads that ($C023 shows the scanline and 1-second
 * interrupts from it; with it never set, the handler didn't see its
 * scanline interrupt to clear it and ran again and again)
 */
static void irq_update(void)
{
    ClemensMMIO *m = gs.mmio;
    m->irq_line = m->dev_adb.irq_line | m->dev_timer.irq_line | m->dev_audio.irq_line | m->vgc.irq_line |
                  m->dev_scc.irq_line | s_irq_cards;
    gs.cpu.irq = m->irq_line != 0;
}

/* After an I/O access: the page maps, the speed, the IRQ line */
static inline void after_io(void)
{
    maps_follow();
    clem_iwm_speed_disk_gate(gs.mmio, &gs.clem->tspec);     /* a drive's motor: 1 MHz at once */
    if (gs.clem->tspec.clocks_step != s_step) {
        w65816_end_run(&gs.cpu);
    }
    irq_update();
}

static uint8_t mem_read(uint32_t addr, uint8_t flags)
{
    ClemensMachine *clem = gs.clem;
    uint8_t bank = addr >> 16;
    const struct ClemensMemoryPageInfo *pg = &clem->mem.bank_page_map[bank]->pages[(addr >> 8) & 0xFF];
    bool mega2 = false;
    if (pg->flags & CLEM_MEM_PAGE_IOADDR_FLAG) {
        uint16_t offset = (uint16_t)(pg->read << 8) | (addr & 0xFF);
        if (offset >= 0xC071 && offset < 0xC080) {
            return clem->mem.fpi_bank_map[0xFF][offset];
        }
        return clem->mem.mmio_read(&clem->mem, &clem->tspec, offset, flags, &mega2);
    }
    if (pg->flags & CLEM_MEM_PAGE_CARDMEM_FLAG) {
        return clem->mem.mmio_read(&clem->mem, &clem->tspec, (uint16_t)(pg->read << 8) | (addr & 0xFF),
                                   flags | CLEM_OP_IO_CARD, &mega2);
    }
    const uint8_t *p = s_rd[(addr >> 8) & 0xFFFF];
    return p ? p[addr & 0xFF] : 0xFF;
}

static void mem_write(uint32_t addr, uint8_t v, uint8_t flags)
{
    ClemensMachine *clem = gs.clem;
    uint8_t bank = addr >> 16;
    struct ClemensMemoryPageMap *map = clem->mem.bank_page_map[bank];
    const struct ClemensMemoryPageInfo *pg = &map->pages[(addr >> 8) & 0xFF];
    bool mega2 = false;
    if (pg->flags & CLEM_MEM_IO_MEMORY_MASK) {
        uint16_t offset = (uint16_t)(pg->write << 8) | (addr & 0xFF);
        if (pg->flags & CLEM_MEM_PAGE_IOADDR_FLAG) {
            if (pg->flags & CLEM_MEM_PAGE_WRITEOK_FLAG) {
                clem->mem.mmio_write(&clem->mem, &clem->tspec, v, offset, flags, &mega2);
            }
        } else {
            clem->mem.mmio_write(&clem->mem, &clem->tspec, v, offset, flags | CLEM_OP_IO_CARD, &mega2);
        }
        return;
    }
    /* Memory, as clem_write() has it: the bank's page, and its shadow in E0/E1 */
    uint8_t bw = (pg->flags & CLEM_MEM_PAGE_DIRECT_FLAG)    ? bank
                 : (pg->flags & CLEM_MEM_PAGE_MAINAUX_FLAG) ? (bank & 0xFE) | (pg->bank_write & 1)
                                                            : pg->bank_write;
    const struct ClemensMemoryPageMap *wmap = clem->mem.bank_page_map[bw];
    const struct ClemensMemoryPageInfo *wp = &wmap->pages[(addr >> 8) & 0xFF];
    if (!(wp->flags & CLEM_MEM_PAGE_WRITEOK_FLAG)) {
        return;
    }
    uint16_t offset = (uint16_t)(wp->write << 8) | (addr & 0xFF);
    bank_mem(bw)[offset] = v;
    if (wmap->shadow_map && wmap->shadow_map->pages[wp->write]) {
        bank_mem(0xE0 | (bw & 1))[offset] = v;
    }
}

/* The soft switches that want two accesses in a row ($C08x) see the last
 * I/O address only if it was a moment ago (the CPU's other accesses don't
 * come here)
 */
static inline void io_begin(void)
{
    clock_now();
    if (gs.cpu.cycles - s_last_io_cycle > 12) {
        gs.mmio->last_data_address = 0xFFFFFFFF;
    }
}

static inline void io_end(uint32_t addr)
{
    gs.mmio->last_data_address = addr;
    s_last_io_cycle = gs.cpu.cycles;
    if (s_step == gs.clem->tspec.clocks_step_fast) {
        gs.cpu.penalty += MEGA2_PENALTY;    /* at 1 MHz the CPU keeps the Mega II's pace anyway */
    }
    after_io();
}

#ifndef ESP_PLATFORM
uint32_t gs_host_io_reads[0x10000], gs_host_io_writes[0x10000];  /* by address & 0xFFFF; bank in high bits ignored */
uint32_t gs_host_io_banks[256];
uint32_t gs_host_io_pages[65536];   /* by bank:page */
#endif

/* The last bus callbacks, for gs_debug() */
static uint32_t s_recent[32];
static uint32_t s_recent_n;

static uint8_t GS_HOT bus_read(void *ctx, uint32_t addr)
{
    s_bus_calls++;
    s_recent[s_recent_n++ & 31] = addr;
#ifndef ESP_PLATFORM
    gs_host_io_reads[addr & 0xFFFF]++;
    gs_host_io_banks[addr >> 16]++;
    gs_host_io_pages[(addr >> 8) & 0xFFFF]++;
#endif
    io_begin();
    uint8_t v = mem_read(addr, 0);
    io_end(addr);
    if ((addr & 0xFFFF) == 0xC0EC) {
        storage_data_read(v);               /* the IWM's data register: count the nibbles */
    }
    return v;
}

static void GS_HOT bus_write(void *ctx, uint32_t addr, uint8_t v)
{
    s_bus_calls++;
    s_recent[s_recent_n++ & 31] = addr | 0x80000000u;
#ifndef ESP_PLATFORM
    gs_host_io_writes[addr & 0xFFFF]++;
    gs_host_io_banks[addr >> 16]++;
    gs_host_io_pages[(addr >> 8) & 0xFFFF]++;
#endif
    const struct ClemensMemoryPageInfo *pg = &gs.clem->mem.bank_page_map[addr >> 16]->pages[(addr >> 8) & 0xFF];
    if (!(pg->flags & CLEM_MEM_IO_MEMORY_MASK)) {
        mem_write(addr, v, 0);                  /* shadowed memory */
        return;
    }
    io_begin();
    mem_write(addr, v, 0);
    io_end(addr);
}

uint8_t gs_bus_read(uint32_t addr)
{
    const uint8_t *p = s_rd[(addr >> 8) & 0xFFFF];
    return p ? p[addr & 0xFF] : mem_read(addr, 0);
}

void gs_bus_write(uint32_t addr, uint8_t v)
{
    uint8_t *p = s_wr[(addr >> 8) & 0xFFFF];
    if (p) {
        p[addr & 0xFF] = v;
    } else {
        mem_write(addr, v, 0);
    }
}

static const w65816_bus_t s_bus = { .read = bus_read, .write = bus_write };

/* ---- Devices ---- */

/* Everything that runs on Clemens's clock, caught up to now (once a line) */
static void devices_sync(void)
{
    ClemensMMIO *mmio = gs.mmio;
    ClemensMachine *clem = gs.clem;
    struct ClemensClock clock = { clem->tspec.clocks_spent, CLEM_CLOCKS_PHI0_CYCLE };
    struct ClemensDeviceMega2Memory m2 = { mmio->e0_bank, mmio->e1_bank };
    uint32_t delta = (uint32_t)(clem->tspec.clocks_spent / CLEM_CLOCKS_PHI0_CYCLE - mmio->mega2_cycles);
    mmio->mega2_cycles += delta;
    mmio->timer_60hz_us += delta;

    uint32_t card_irqs = 0;
    uint32_t t0 = PROF_NOW();
    for (int i = 0; i < CLEM_CARD_SLOT_COUNT; i++) {
        ClemensCard *card = mmio->card_slot[i];
        if (!card) {
            continue;
        }
        uint32_t r = card->io_sync(&clock, card->context);
        if (r & CLEM_CARD_IRQ) {
            card_irqs |= CLEM_IRQ_SLOT_1 << i;
        }
        if ((r & CLEM_CARD_DMA) && card->io_dma) {
            for (uint32_t c = 0; c < delta; c++) {
                uint8_t dbank, data;
                uint16_t daddr;
                uint8_t latch = card->io_dma(&dbank, &daddr, true, card->context);
                if (!latch) {
                    data = gs_bus_read((uint32_t)dbank << 16 | daddr);
                }
                latch = card->io_dma(&data, &daddr, false, card->context);
                if (latch) {
                    gs_bus_write((uint32_t)dbank << 16 | daddr, data);
                }
            }
        }
    }
    s_irq_cards = card_irqs;
    s_prof[P_CARDS] += PROF_NOW() - t0;

    PROF(P_VGC, clem_vgc_sync(&mmio->vgc, &clock, mmio->e0_bank, mmio->e1_bank));
    PROF(P_IWM, clem_iwm_glu_sync(&mmio->dev_iwm, &mmio->active_drives, &clem->tspec));
    PROF(P_SCC, clem_scc_glu_sync(&mmio->dev_scc, &clock));
    PROF(P_SOUND, clem_sound_glu_sync(&mmio->dev_audio, &clock));
    t0 = PROF_NOW();
    clem_gameport_sync(&mmio->dev_adb.gameport, &clock);
    clem_adb_glu_sync(&mmio->dev_adb, &m2, delta);
    while (mmio->timer_60hz_us >= CLEM_MEGA2_CYCLES_PER_60TH) {
        clem_timer_sync(&mmio->dev_timer, CLEM_MEGA2_CYCLES_PER_60TH);
        mmio->timer_60hz_us -= CLEM_MEGA2_CYCLES_PER_60TH;
    }
    clem_iwm_speed_disk_gate(mmio, &clem->tspec);
    maps_follow();
    irq_update();
    s_prof[P_ADB] += PROF_NOW() - t0;
}

/* ---- Input ---- */

static void input_push(enum ClemensInputType type, int16_t a, int16_t b)
{
    uint32_t pos = __atomic_load_n(&s_input.head, __ATOMIC_RELAXED);
    for (;;) {
        uint32_t seq = __atomic_load_n(&s_input.slot[pos % INPUT_QUEUE].seq, __ATOMIC_ACQUIRE);
        int32_t diff = (int32_t)(seq - pos);
        if (diff < 0) {
            return;                                 /* full: dropped */
        }
        if (diff == 0 && __atomic_compare_exchange_n(&s_input.head, &pos, pos + 1, true, __ATOMIC_RELAXED,
                                                     __ATOMIC_RELAXED)) {
            break;
        }
        if (diff > 0) {
            pos = __atomic_load_n(&s_input.head, __ATOMIC_RELAXED);
        }
    }
    struct ClemensInputEvent *e = &s_input.slot[pos % INPUT_QUEUE].ev;
    memset(e, 0, sizeof(*e));
    e->type = type;
    e->value_a = a;
    e->value_b = b;
    __atomic_store_n(&s_input.slot[pos % INPUT_QUEUE].seq, pos + 1, __ATOMIC_RELEASE);
}

void gs_key(uint8_t adb_code, bool down)
{
    input_push(down ? kClemensInputType_KeyDown : kClemensInputType_KeyUp, adb_code, 0);
}

static bool s_button_held;

void gs_mouse(int dx, int dy, bool button)
{
    while (dx || dy) {
        int sx = dx > 63 ? 63 : dx < -63 ? -63 : dx;
        int sy = dy > 63 ? 63 : dy < -63 ? -63 : dy;
        input_push(kClemensInputType_MouseMove, sx, sy);
        dx -= sx;
        dy -= sy;
    }
    if (button != s_button_held) {
        s_button_held = button;
        input_push(button ? kClemensInputType_MouseButtonDown : kClemensInputType_MouseButtonUp, 0, 0);
    }
}

static void input_apply(void)
{
    for (;;) {
        uint32_t pos = s_input.tail;
        uint32_t seq = __atomic_load_n(&s_input.slot[pos % INPUT_QUEUE].seq, __ATOMIC_ACQUIRE);
        if (seq != pos + 1) {
            return;
        }
        clemens_input(gs.mmio, &s_input.slot[pos % INPUT_QUEUE].ev);
        s_input.tail = pos + 1;
        __atomic_store_n(&s_input.slot[pos % INPUT_QUEUE].seq, pos + INPUT_QUEUE, __ATOMIC_RELEASE);
    }
}

static void input_reset(void)
{
    s_input.head = s_input.tail = 0;
    for (uint32_t i = 0; i < INPUT_QUEUE; i++) {
        s_input.slot[i].seq = i;
    }
}

/* The pointer to a place in the picture (touch): through Clemens's tracking
 * of the toolbox's cursor, in the clamp box's coordinates (640 or 320 wide)
 */
void gs_mouse_position(int x, int y, bool button)
{
    const uint8_t *e1 = gs.mmio->e1_bank;
    int x1 = e1[0x2BC] | e1[0x2BD] << 8;
    x -= GS_BORDER_X;
    y -= GS_BORDER_Y;
    if (x1 > 0 && x1 < 400) {
        x /= 2;                                     /* 320 mode */
    }
    input_push(kClemensInputType_MouseMoveAbsolute, x < 0 ? 0 : x, y < 0 ? 0 : y);
    if (button != s_button_held) {
        s_button_held = button;
        input_push(button ? kClemensInputType_MouseButtonDown : kClemensInputType_MouseButtonUp, 0, 0);
    }
}

/* The US keyboard's characters as ADB codes */
int gs_ascii_key(char c, bool *shift)
{
    static const char plain[] = "asdfhgzxcv\0bqwerty123465=97-80]ou[ip\0lj'k;\\,/nm.\0 `";
    static const char shifted[] = "ASDFHGZXCV\0BQWERTY!@#$^%+(&_*)}OU{IP\0LJ\"K:|<?NM>\0 ~";
    *shift = false;
    if (c == '\n' || c == '\r') {
        return 0x24;
    }
    if (c == '\b') {
        return 0x33;
    }
    if (c == 27) {
        return 0x35;
    }
    for (int i = 0; i < (int)sizeof(plain) - 1; i++) {
        if (plain[i] && plain[i] == c) {
            return i;
        }
        if (shifted[i] && shifted[i] == c) {
            *shift = true;
            return i;
        }
    }
    return -1;
}

void gs_type(const char *text)
{
    snprintf(s_type.text, sizeof(s_type.text), "%s", text);
    s_type.len = strlen(s_type.text);
    s_type.pos = 0;
    s_type.phase = 0;
    s_type.key = -1;
}

/* A key down for 3 frames, up for 3 */
static void type_frame(void)
{
    if (s_type.phase > 0 && --s_type.phase > 0) {
        return;
    }
    if (s_type.key >= 0) {
        gs_key(s_type.key, false);
        if (s_type.shift) {
            gs_key(0x38, false);
        }
        s_type.key = -1;
        s_type.phase = 3;
        return;
    }
    while (s_type.pos < s_type.len) {
        bool shift;
        int k = gs_ascii_key(s_type.text[s_type.pos++], &shift);
        if (k < 0) {
            continue;
        }
        if (shift) {
            gs_key(0x38, true);
        }
        gs_key(k, true);
        s_type.key = k;
        s_type.shift = shift;
        s_type.phase = 3;
        return;
    }
}

/* ---- Sound: Clemens mixes floats; the speaker's task takes 16-bit stereo ---- */

static void audio_frame(void)
{
    struct ClemensDeviceAudio *a = &gs.mmio->dev_audio;
    unsigned n = a->mix_frame_index;
    if (n > a->mix_buffer.frame_count) {
        n = a->mix_buffer.frame_count;
    }
    if (s_audio.enabled) {
        uint32_t head = s_audio.head;
        uint32_t tail = __atomic_load_n(&s_audio.tail, __ATOMIC_ACQUIRE);
        for (unsigned i = 0; i < n && head - tail < AUDIO_RING; i++) {
            const float *f = &s_audio.mix[i * 2];
            for (int c = 0; c < 2; c++) {
                float v = f[c] * 24000.0f;
                s_audio.ring[(head % AUDIO_RING) * 2 + c] = v > 32767 ? 32767 : v < -32768 ? -32768 : (int16_t)v;
            }
            head++;
        }
        __atomic_store_n(&s_audio.head, head, __ATOMIC_RELEASE);
    }
    clemens_audio_next_frame(gs.mmio, n);
}

void gs_audio_enable(bool enable)
{
    s_audio.enabled = enable;
}

int gs_audio_render(int16_t *dst, int frames)
{
    uint32_t tail = s_audio.tail;
    uint32_t head = __atomic_load_n(&s_audio.head, __ATOMIC_ACQUIRE);
    int n = 0;
    while (n < frames && tail != head) {
        dst[n * 2] = s_audio.ring[(tail % AUDIO_RING) * 2];
        dst[n * 2 + 1] = s_audio.ring[(tail % AUDIO_RING) * 2 + 1];
        tail++;
        n++;
    }
    __atomic_store_n(&s_audio.tail, tail, __ATOMIC_RELEASE);
    return n;
}

int gs_audio_pending(void)
{
    return __atomic_load_n(&s_audio.head, __ATOMIC_ACQUIRE) - s_audio.tail;
}

/* ---- The clock and battery RAM ---- */

static uint32_t seconds_since_1904(void)
{
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    /* local time as if it were UTC: the IIGS keeps local time */
    int64_t days = 0;
    for (int y = 1904; y < tm.tm_year + 1900; y++) {
        days += (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 366 : 365;
    }
    days += tm.tm_yday;
    return (uint32_t)(days * 86400 + tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec);
}

#define RTC_FLAG_BRAM_DIRTY 2       /* clem_rtc.c's */

bool gs_bram_changed(void)
{
    return (gs.mmio->dev_rtc.flags & RTC_FLAG_BRAM_DIRTY) != 0;
}

void gs_bram_get(uint8_t *bram)
{
    bool dirty;
    memcpy(bram, clemens_rtc_get_bram(gs.mmio, &dirty), GS_BRAM_SIZE);
}

/* ---- Running ---- */

static void cpu_reset(void)
{
    ClemensMachine *clem = gs.clem;
    ClemensMMIO *mmio = gs.mmio;
    clem_mmio_bind_machine(clem, mmio);
    clem_disk_reset_drives(&mmio->active_drives);
    clem_mmio_reset(mmio, &clem->tspec);
    clem_iwm_speed_disk_gate(mmio, &clem->tspec);
    struct ClemensClock clock = { clem->tspec.clocks_spent, CLEM_CLOCKS_PHI0_CYCLE };
    for (int i = 0; i < CLEM_CARD_SLOT_COUNT; i++) {
        if (mmio->card_slot[i]) {
            mmio->card_slot[i]->io_reset(&clock, mmio->card_slot[i]->context);
        }
    }
    clem_iwm_speed_disk_gate(mmio, &clem->tspec);
    mmio->state_type = kClemensMMIOStateType_Active;
    maps_follow();
    gs.cpu.vector_bank = clem->mem.mmio_niolc(&clem->mem) ? 0x00 : 0xFF;
    w65816_reset(&gs.cpu);
}

void gs_run_frame(void)
{
    ClemensMachine *clem = gs.clem;
    uint32_t frame_t0 = PROF_NOW();
    input_apply();
    for (int line = 0; line < GS_LINES; line++) {
        clem_clocks_time_t end = clem->tspec.clocks_spent + GS_LINE_CLOCKS;
        while (clem->tspec.clocks_spent < end) {
            s_step = clem->tspec.clocks_step;
            int budget;
            if (s_step == CLEM_CLOCKS_PHI0_CYCLE) {
                /* to the grid's next edge (as the CPU waits for the Mega II) */
                s_run_j = phi0_next(clem->tspec.clocks_spent);
                uint64_t last = phi0_next(end);
                if (last <= s_run_j) {
                    clem->tspec.clocks_spent = end;
                    break;
                }
                clem->tspec.clocks_spent = phi0_edge(s_run_j);
                budget = (int)(last - s_run_j);
            } else {
                budget = (int)((end - clem->tspec.clocks_spent + s_step - 1) / s_step);
            }
            s_run_clocks = clem->tspec.clocks_spent;
            s_run_cycles = gs.cpu.cycles;
#ifndef ESP_PLATFORM
            int used = 0;
            if (gs_host_hook) {     /* the host's tracing: an instruction at a time */
                gs.cpu.end_run = false;
                while (used < budget && !gs.cpu.end_run && !gs.cpu.stopped) {
                    gs_host_hook();
                    int c = w65816_step(&gs.cpu);
                    if (c <= 0) {
                        break;
                    }
                    used += c;
                }
            } else {
                used = w65816_run(&gs.cpu, budget);
            }
#else
            uint32_t t0 = PROF_NOW();
            int used = w65816_run(&gs.cpu, budget);
            s_prof[P_CPU] += PROF_NOW() - t0;
#endif
            if (used <= 0 && (gs.cpu.waiting || gs.cpu.stopped)) {
                clem->tspec.clocks_spent = end;
                break;
            }
            clem->tspec.clocks_spent = clocks_after(used);
        }
        devices_sync();
    }
    /* Ctrl-Reset (with Command: a restart, as the ROM decides) */
    bool reset = gs.mmio->dev_adb.keyb.reset_key;
    if (reset && !s_reset_held) {
        cpu_reset();
    }
    s_reset_held = reset;
    if (gs.frames % 60 == 0) {
        clem_rtc_set_clock_time(&gs.mmio->dev_rtc, seconds_since_1904());
    }
    PROF(P_AUDIO, audio_frame());
    PROF(P_VIDEO, video_frame());
    storage_frame();
    type_frame();
    gs.frames++;
    /* The profile: an average over 60 frames */
    uint32_t total = PROF_NOW() - frame_t0, known = 0;
    for (int i = 0; i < P_OTHER; i++) {
        known += s_prof[i];
    }
    s_prof[P_OTHER] = total - known;
    for (int i = 0; i < P_COUNT; i++) {
        s_prof_sum[i] += s_prof[i];
        s_prof[i] = 0;
    }
    s_bus_sum += s_bus_calls;
    s_bus_calls = 0;
    if (gs.frames % 60 == 0) {
        for (int i = 0; i < P_COUNT; i++) {
            s_prof_avg[i] = s_prof_sum[i] / 60;
            s_prof_sum[i] = 0;
        }
        s_bus_avg = s_bus_sum / 60;
        s_bus_sum = 0;
        s_cycles_avg = (uint32_t)((gs.cpu.cycles - s_cycles_frame0) / 60);
        s_cycles_frame0 = gs.cpu.cycles;
    }
}

/* The CPU, the last bus callbacks (W: a write) and bytes at an address */
void gs_debug(char *buf, int len, uint32_t addr, int n)
{
    w65816_t *c = &gs.cpu;
    int k = snprintf(buf, len, "PC %02X/%04X A %04X X %04X Y %04X S %04X D %04X DB %02X P %02X%s irq %d mmap %08lX; bus:",
                     c->pbr, c->pc, c->a, c->x, c->y, c->s, c->d, c->dbr, c->p, c->e ? " E" : "", c->irq,
                     (unsigned long)gs.mmio->mmap_register);
    ClemensMMIO *m = gs.mmio;
    k += snprintf(buf + k, len - k, " [irq adb %lX timer %lX audio %lX vgc %lX scc %lX cards %lX]",
                  (unsigned long)m->dev_adb.irq_line, (unsigned long)m->dev_timer.irq_line,
                  (unsigned long)m->dev_audio.irq_line, (unsigned long)m->vgc.irq_line,
                  (unsigned long)m->dev_scc.irq_line, (unsigned long)s_irq_cards);
    for (int i = 0; i < 32 && k < len; i++) {
        uint32_t a = s_recent[(s_recent_n + i) & 31];
        k += snprintf(buf + k, len - k, " %s%06lX", (a >> 31) ? "W" : "", (unsigned long)(a & 0xFFFFFF));
    }
    if (n > 0 && k < len) {
        k += snprintf(buf + k, len - k, "; %06lX:", (unsigned long)addr);
        for (int i = 0; i < n && k < len; i++) {
            k += snprintf(buf + k, len - k, " %02X", gs_bus_read(addr + i));
        }
    }
}

/* The profile as text: thousands of CPU cycles per frame */
void gs_profile(char *buf, int len)
{
    static const char *names[P_COUNT] = { "cpu", "cards", "vgc", "iwm", "scc", "sound", "adb", "video", "audio",
                                          "other" };
    int n = snprintf(buf, len, "[65816 %u cycles, %u bus calls] ", (unsigned)s_cycles_avg, (unsigned)s_bus_avg);
    for (int i = 0; i < P_COUNT && n < len; i++) {
        n += snprintf(buf + n, len - n, "%s %u ", names[i], (unsigned)(s_prof_avg[i] / 1000));
    }
}

/* ---- Free memory, as the Memory Manager's FreeMem counts it ----
 *
 * Its handle records (20 bytes: block address, attributes, owner, size,
 * previous and next record) form a list in address order, from the record
 * $E11600 points to; blocks owned by 0 stand for what isn't there or isn't
 * RAM (I/O, the language card, the missing banks). The free memory is the
 * gaps between the blocks. Read from the memory itself (no I/O), and only
 * believed if the list holds together: before GS/OS, or under ProDOS 8, it
 * may not exist.
 */
static bool raw_peek(uint32_t addr, uint8_t *v)
{
    uint8_t bank = addr >> 16;
    if (bank < gs.cfg.ram_kb / 64) {
        *v = gs.cfg.ram[addr];
        return true;
    }
    if (bank == 0xE0 || bank == 0xE1) {
        *v = gs.cfg.mega2[addr - 0xE00000];
        return true;
    }
    return false;
}

static bool raw_peek32(uint32_t addr, uint32_t *v)
{
    uint8_t b[4];
    for (int i = 0; i < 4; i++) {
        if (!raw_peek(addr + i, &b[i])) {
            return false;
        }
    }
    *v = b[0] | b[1] << 8 | b[2] << 16 | (uint32_t)b[3] << 24;
    return true;
}

bool gs_ram_free(uint32_t *free_bytes, uint32_t *largest)
{
    uint32_t rec, prev = 0, end = 0, sum = 0, big = 0, last_addr = 0;
    *free_bytes = *largest = 0;
    if (!gs.mmio || !raw_peek32(0xE11600, &rec) || (rec >> 16) != 0xE1) {
        return false;
    }
    for (int n = 0; rec; n++) {
        uint32_t addr, size, back, next;
        if (n > 20000 || (rec >> 24) || !raw_peek32(rec, &addr) || !raw_peek32(rec + 8, &size) ||
            !raw_peek32(rec + 12, &back) || !raw_peek32(rec + 16, &next) || back != prev || addr < last_addr) {
            return false;
        }
        if (n > 0 && addr > end) {
            sum += addr - end;
            big = addr - end > big ? addr - end : big;
        }
        end = addr + size;
        last_addr = addr;
        prev = rec;
        rec = next;
    }
    *free_bytes = sum;
    *largest = big;
    return true;
}

uint32_t gs_screen_signature(void)
{
    return gs.signature;
}

const uint16_t *gs_line(int y)
{
    return gs.cfg.frame + (y < 0 ? 0 : y >= GS_SCREEN_HEIGHT ? GS_SCREEN_HEIGHT - 1 : y) * GS_SCREEN_WIDTH;
}

void gs_get_status(gs_status_t *s)
{
    memset(s, 0, sizeof(*s));
    s->frames = gs.frames;
    s->pc = (uint32_t)gs.cpu.pbr << 16 | gs.cpu.pc;
    s->fast = gs.clem->tspec.clocks_step == gs.clem->tspec.clocks_step_fast;
    s->emulation = gs.cpu.e;
    storage_status(s);
}

static void log_to_host(int level, const char *msg);

/* The machine's own buffers, once: Clemens's state, the page tables, the
 * disk drives' nibbles (1.8 MB)
 */
bool gs_prepare(void *(*alloc)(uint32_t bytes), void (*release)(void *p))
{
    if (gs.clem) {
        return true;
    }
    gs.cfg.alloc = alloc;
    gs.cfg.release = release;
    gs.clem = gs_alloc(sizeof(ClemensMachine));
    gs.mmio = gs_alloc(sizeof(ClemensMMIO));
    s_rd = gs_alloc(65536 * sizeof(uint8_t *));
    s_wr = gs_alloc(65536 * sizeof(uint8_t *));
    s_empty = gs_alloc(CLEM_IIGS_BANK_SIZE);
    s_sink = gs_alloc(256);
    s_slot_rom = gs_alloc(2048 * CLEM_CARD_SLOT_COUNT);
    s_audio.mix = gs_alloc(MIX_FRAMES * 2 * sizeof(float));
    s_audio.ring = gs_alloc(AUDIO_RING * 2 * sizeof(int16_t));
    bool ok = gs.clem && gs.mmio && s_rd && s_wr && s_empty && s_sink && s_slot_rom && s_audio.mix && s_audio.ring &&
              storage_prepare();
    if (ok) {
        memset(gs.clem, 0, sizeof(*gs.clem));   /* disks may go in before gs_init() */
        memset(gs.mmio, 0, sizeof(*gs.mmio));
    }
    if (!ok) {
        gs_log_f("gs: not enough memory for the machine");
    }
    return ok;
}

bool gs_init(const gs_config_t *cfg)
{
    if (!cfg->rom || !cfg->ram || !cfg->mega2 || !cfg->frame || cfg->ram_kb < 256) {
        return false;
    }
    gs.cfg = *cfg;
    uint32_t ram_banks = cfg->ram_kb / 64;
    if (ram_banks > 0x80) {
        ram_banks = 0x80;
    }
    if (!gs_prepare(cfg->alloc, cfg->release)) {
        return false;
    }
    ClemensMachine *clem = gs.clem;
    ClemensMMIO *mmio = gs.mmio;
    memset(clem, 0, sizeof(*clem));
    memset(mmio, 0, sizeof(*mmio));
    memset(s_empty, 0, CLEM_IIGS_BANK_SIZE);
    memset(s_slot_rom, 0, 2048 * CLEM_CARD_SLOT_COUNT);
    clem_debug_log_fn = log_to_host;

    /* Clemens's memory: RAM from bank 00, the ROM in FC-FF, E0/E1, nothing elsewhere */
    _clem_timespec_init(&clem->tspec, CLEM_CLOCKS_PHI2_FAST_CYCLE);
    memset(cfg->ram, 0, ram_banks * CLEM_IIGS_BANK_SIZE);
    memset(cfg->mega2, 0, 2 * CLEM_IIGS_BANK_SIZE);
    for (int b = 0; b < 256; b++) {
        clem->mem.fpi_bank_map[b] = s_empty;
        clem->mem.fpi_bank_used[b] = false;
    }
    for (uint32_t b = 0; b < ram_banks; b++) {
        clem->mem.fpi_bank_map[b] = cfg->ram + b * CLEM_IIGS_BANK_SIZE;
        clem->mem.fpi_bank_used[b] = true;
    }
    for (int b = 0xFC; b < 0x100; b++) {
        clem->mem.fpi_bank_map[b] = (uint8_t *)cfg->rom + (b - 0xFC) * CLEM_IIGS_BANK_SIZE;
        clem->mem.fpi_bank_used[b] = true;
    }
    clem->mem.fpi_bank_map[CLEM_IIGS_EMPTY_RAM_BANK] = s_empty;
    clem->mem.mega2_bank_map[0] = cfg->mega2;
    clem->mem.mega2_bank_map[1] = cfg->mega2 + CLEM_IIGS_BANK_SIZE;

    clem_mmio_init(mmio, &clem->dev_debug, clem->mem.bank_page_map, s_slot_rom, ram_banks, 4,
                   clem->mem.mega2_bank_map[0], clem->mem.mega2_bank_map[1], &clem->tspec);
    clem_mmio_bind_machine(clem, mmio);

    struct ClemensAudioMixBuffer mix = {
        .data = (uint8_t *)s_audio.mix,
        .stride = 2 * sizeof(float),
        .frame_count = MIX_FRAMES,
        .frames_per_second = GS_AUDIO_HZ,
    };
    clemens_assign_audio_mix_buffer(mmio, &mix);

    storage_init();
    mmio->card_slot[6] = storage_hdd_card();

    if (cfg->bram) {
        memcpy(mmio->dev_rtc.bram, cfg->bram, GS_BRAM_SIZE);
    }
    clem_rtc_set_clock_time(&mmio->dev_rtc, seconds_since_1904());

    /* The CPU's tables: every bank once, then 00/01/E0/E1 as they change */
    memset(&gs.cpu, 0, sizeof(gs.cpu));
    gs.cpu.rd = s_rd;
    gs.cpu.wr = s_wr;
    gs.cpu.bus = s_bus;
    for (int b = 0; b < 256; b++) {
        build_bank(b);
    }
    memset(clem_mmio_dirty_pages, 0, sizeof(clem_mmio_dirty_pages));
    s_generation = clem_mmio_map_generation;

    input_reset();
    s_audio.head = s_audio.tail = 0;
    s_type.len = s_type.pos = 0;
    s_type.key = -1;
    gs.frames = 0;
    video_init();
    cpu_reset();
    return true;
}

void gs_reset(void)
{
    memset(gs.cfg.ram, 0, (gs.cfg.ram_kb / 64) * CLEM_IIGS_BANK_SIZE);
    memset(gs.cfg.mega2, 0, 2 * CLEM_IIGS_BANK_SIZE);
    storage_reset();
    cpu_reset();
}

void gs_log_f(const char *fmt, ...)
{
    char msg[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    log_to_host(CLEM_DEBUG_LOG_WARN, msg);
}

static void log_to_host(int level, const char *msg)
{
#ifdef ESP_PLATFORM
    extern void gs_log(int level, const char *msg);
    gs_log(level, msg);
#else
    fprintf(stderr, "clemens: %s\n", msg);
#endif
}
