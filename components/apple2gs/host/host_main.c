/*
 * Host build of the IIGS core, for testing on a desktop:
 *
 *   build/gs_host [frames] [out.ppm]
 *
 * Environment:
 *   GS_ROM=<file>            ROM 3, 256 KB (default roms/gs_rom3.rom)
 *   GS_CHARGEN=<file>        the IIe enhanced video ROM, 4 KB (default roms/chargen.rom)
 *   GS_RAM=<KB>              fast RAM (default 1024)
 *   GS_35=, GS_525=          floppy images (3.5": .2mg .po .woz; 5.25": .dsk .do .po .woz)
 *   GS_HD1=, GS_HD2=         hard disk images (.po .hdv .2mg)
 *   GS_NEWDISK=<path>:<KB>   make a blank image first
 *   GS_BRAM=<file>           battery RAM: read at start, written at the end
 *   GS_SCRIPT=<file>         lines "<frame> <command> ...":
 *                              type <text> (\n is RETURN), key <adb hex> 0|1,
 *                              mouse <dx> <dy> <0|1>, shot <file.ppm>, text,
 *                              insert <drive 0-3> <path>, eject <drive>,
 *                              reset, peek <hex>, dump <hex> <n>, flush, status
 *   GS_TEXT=1                print the text screen at the end
 *   GS_WAV=<file>            write the sound
 *   GS_TRACE=<hex pc>:<n>    print n instructions from when pc (24-bit) is reached
 *   GS_STATUS=1              machine state every 60 frames
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gs_internal.h"
#include "clem_mmio_types.h"

static uint8_t *load_file(const char *path, size_t want, bool required)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        if (required) {
            fprintf(stderr, "can't open %s\n", path);
            exit(1);
        }
        return NULL;
    }
    uint8_t *buf = calloc(1, want);
    size_t n = fread(buf, 1, want, f);
    fclose(f);
    if (n != want) {
        fprintf(stderr, "%s: %zu bytes, expected %zu\n", path, n, want);
        exit(1);
    }
    return buf;
}

static void write_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "can't write %s\n", path);
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", GS_SCREEN_WIDTH, GS_SCREEN_HEIGHT);
    for (int y = 0; y < GS_SCREEN_HEIGHT; y++) {
        const uint16_t *line = gs_line(y);
        for (int x = 0; x < GS_SCREEN_WIDTH; x++) {
            uint16_t c = line[x];
            uint8_t rgb[3] = {
                (uint8_t)(((c >> 11) & 31) * 255 / 31),
                (uint8_t)(((c >> 5) & 63) * 255 / 63),
                (uint8_t)((c & 31) * 255 / 31),
            };
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
}

/* Text page 1 (40 or 80 columns), from the Mega II's memory */
static void print_text(uint32_t frame)
{
    const uint8_t *e0 = gs.mmio->e0_bank, *e1 = gs.mmio->e1_bank;
    bool col80 = gs.mmio->vgc.mode_flags & 0x4;
    printf("---- frame %u, mode %08X, c029 %02X\n", frame, gs.mmio->vgc.mode_flags, gs.mmio->new_video_c029);
    for (int row = 0; row < 24; row++) {
        uint16_t a = 0x400 + ((row & 7) << 7) + (row >> 3) * 40;
        char s[81];
        int cols = col80 ? 80 : 40;
        for (int c = 0; c < cols; c++) {
            uint8_t ch = col80 ? ((c & 1) ? e0[a + c / 2] : e1[a + c / 2]) : e0[a + c];
            ch &= 0x7F;
            if (ch < 0x20) {
                ch += 0x40;
            }
            s[c] = isprint(ch) ? ch : '.';
        }
        int n = cols;
        s[n] = 0;
        while (n > 0 && s[n - 1] == ' ') {
            s[--n] = 0;
        }
        printf("|%s\n", s);
    }
}

/* ---- Script ---- */

typedef struct {
    uint32_t frame;
    char cmd[16];
    char arg[1024];
} line_t;

static line_t *s_script;
static int s_script_n, s_script_pos;

static void load_script(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "can't open %s\n", path);
        exit(1);
    }
    char buf[1200];
    s_script = calloc(8192, sizeof(line_t));
    while (fgets(buf, sizeof(buf), f) && s_script_n < 8192) {
        buf[strcspn(buf, "\r\n")] = 0;
        line_t *l = &s_script[s_script_n];
        int used = 0;
        if (sscanf(buf, "%u %15s %n", &l->frame, l->cmd, &used) >= 2) {
            snprintf(l->arg, sizeof(l->arg), "%s", buf + used);
            s_script_n++;
        }
    }
    fclose(f);
}

static void unescape(char *s)
{
    char *o = s;
    for (; *s; s++) {
        if (s[0] == '\\' && s[1] == 'n') {
            *o++ = '\n';
            s++;
        } else {
            *o++ = *s;
        }
    }
    *o = 0;
}

static void print_status(void)
{
    gs_status_t s;
    gs_get_status(&s);
    printf("host: c036 %02X iwm async %d latch %d fast %d ", gs.mmio->speed_c036, gs.mmio->dev_iwm.async_mode, gs.mmio->dev_iwm.latch_mode, gs.mmio->dev_iwm.fast_mode);
    printf("host: frame %u pc %06X %s %s 3.5 t%d r%u w%u 5.25 t%d r%u w%u HD1 %u/%u HD2 %u/%u\n", s.frames, s.pc,
           s.fast ? "fast" : "slow", s.emulation ? "emu" : "native", s.track[0], s.reads[0], s.writes[0], s.track[1], s.reads[1],
           s.writes[1], s.reads[2], s.writes[2], s.reads[3], s.writes[3]);
}

#define RD32(a) ((uint32_t)gs_bus_read(a) | (uint32_t)gs_bus_read((a) + 1) << 8 | \
                 (uint32_t)gs_bus_read((a) + 2) << 16 | (uint32_t)gs_bus_read((a) + 3) << 24)

static void run_script(uint32_t frame)
{
    while (s_script_pos < s_script_n && s_script[s_script_pos].frame <= frame) {
        line_t *l = &s_script[s_script_pos++];
        if (!strcmp(l->cmd, "type")) {
            unescape(l->arg);
            gs_type(l->arg);
        } else if (!strcmp(l->cmd, "key")) {
            unsigned k = 0;
            int d = 0;
            sscanf(l->arg, "%x %d", &k, &d);
            gs_key(k, d);
        } else if (!strcmp(l->cmd, "mouse")) {
            int dx = 0, dy = 0, b = 0;
            sscanf(l->arg, "%d %d %d", &dx, &dy, &b);
            gs_mouse(dx, dy, b);
        } else if (!strcmp(l->cmd, "shot")) {
            write_ppm(l->arg);
        } else if (!strcmp(l->cmd, "text")) {
            print_text(frame);
        } else if (!strcmp(l->cmd, "insert")) {
            int d = 0;
            char path[400];
            if (sscanf(l->arg, "%d %399s", &d, path) == 2 && !gs_disk_insert(d, path)) {
                printf("host: can't insert %s\n", path);
            }
        } else if (!strcmp(l->cmd, "eject")) {
            gs_disk_eject(atoi(l->arg));
        } else if (!strcmp(l->cmd, "reset")) {
            gs_reset();
        } else if (!strcmp(l->cmd, "flush")) {
            gs_disk_flush();
        } else if (!strcmp(l->cmd, "iostat")) {
            /* the bus callbacks since the last iostat, most frequent first */
            extern uint32_t gs_host_io_reads[0x10000], gs_host_io_writes[0x10000], gs_host_io_banks[256];
            for (int k = 0; k < 12; k++) {
                uint32_t best = 0, bi = 0;
                bool w = false;
                for (uint32_t a = 0; a < 0x10000; a++) {
                    if (gs_host_io_reads[a] > best) { best = gs_host_io_reads[a]; bi = a; w = false; }
                    if (gs_host_io_writes[a] > best) { best = gs_host_io_writes[a]; bi = a; w = true; }
                }
                if (!best) break;
                printf("host: %s $%04X %u\n", w ? "write" : "read ", bi, best);
                if (w) gs_host_io_writes[bi] = 0; else gs_host_io_reads[bi] = 0;
            }
            for (int b = 0; b < 256; b++) {
                if (gs_host_io_banks[b] > 1000) printf("host: bank %02X %u\n", b, gs_host_io_banks[b]);
            }
            extern uint32_t gs_host_io_pages[65536];
            for (int k = 0; k < 16; k++) {
                uint32_t best = 0, bi = 0;
                for (uint32_t p = 0; p < 65536; p++) {
                    if (gs_host_io_pages[p] > best) { best = gs_host_io_pages[p]; bi = p; }
                }
                if (!best) break;
                printf("host: page %02X/%02X %u\n", bi >> 8, bi & 0xFF, best);
                gs_host_io_pages[bi] = 0;
            }
            memset(gs_host_io_pages, 0, sizeof(gs_host_io_pages));
            memset(gs_host_io_reads, 0, sizeof(gs_host_io_reads));
            memset(gs_host_io_writes, 0, sizeof(gs_host_io_writes));
            memset(gs_host_io_banks, 0, sizeof(gs_host_io_banks));
        } else if (!strcmp(l->cmd, "mm")) {
            /* the Memory Manager's handle records: 20 bytes (block address,
             * attributes, owner, size, previous, next), doubly linked */
            uint32_t banks = gs.cfg.ram_kb / 64;
            static uint32_t heads[64];
            int nheads = 0;
            for (uint32_t b = 0; b < 0x100 && nheads < 64; b++) {
                if (b >= banks && b != 0xE0 && b != 0xE1) continue;
                for (uint32_t o = 0; o < 0x10000 - 20; o += 1) {
                    uint32_t a = b << 16 | o;
                    uint32_t prev = RD32(a + 12), next = RD32(a + 16);
                    if (prev != 0 || next == 0 || (next >> 24)) continue;
                    /* a head: follow next while back links agree */
                    uint32_t cur = a, n = 0, used = 0;
                    while (n < 5000) {
                        uint32_t nx = RD32(cur + 16);
                        if (nx == 0) break;
                        if ((nx >> 24) || RD32(nx + 12) != cur) { n = 0; break; }
                        used += RD32(cur + 8);
                        cur = nx;
                        n++;
                    }
                    if (n >= 20) {
                        heads[nheads++] = a;
                        printf("host: handle chain at %06X: %u records, %u KB in blocks\n", a, n + 1, used / 1024);
                    }
                }
            }
            for (int h = 0; h < nheads; h++) {
                for (uint32_t b = 0; b < 0x100; b++) {
                    if (b >= banks && b != 0xE0 && b != 0xE1) continue;
                    for (uint32_t o = 0; o < 0x10000 - 4; o++) {
                        uint32_t a = b << 16 | o;
                        if ((RD32(a) & 0xFFFFFF) == heads[h] && gs_bus_read(a + 2) == (heads[h] >> 16))
                            printf("host:   pointer to %06X at %06X\n", heads[h], a);
                    }
                }
            }
        } else if (!strcmp(l->cmd, "free")) {
            uint32_t f, big;
            if (gs_ram_free(&f, &big)) {
                printf("host: frame %u free %u KB, largest %u KB\n", frame, f / 1024, big / 1024);
            } else {
                printf("host: frame %u no Memory Manager list\n", frame);
            }
        } else if (!strcmp(l->cmd, "mmdump")) {
            uint32_t a = RD32(0xE11600) & 0xFFFFFF;
            printf("host: E11600:");
            for (int i = 0; i < 48; i++) printf(" %02X", gs_bus_read(0xE11600 + i));
            printf("\n");
            for (int n = 0; a && n < 400; n++) {
                printf("host: rec %06X addr %08X attr %04X id %04X size %08X prev %08X next %08X\n", a, RD32(a),
                       gs_bus_read(a + 4) | gs_bus_read(a + 5) << 8, gs_bus_read(a + 6) | gs_bus_read(a + 7) << 8,
                       RD32(a + 8), RD32(a + 12), RD32(a + 16));
                a = RD32(a + 16) & 0xFFFFFF;
            }
        } else if (!strcmp(l->cmd, "nib")) {
            /* nib <track> <file>: the 5.25" drive's bits of a track */
            int t = 0;
            char path[300];
            if (sscanf(l->arg, "%d %299s", &t, path) == 2) {
                struct ClemensNibbleDisk *nib = &gs.mmio->active_drives.slot6[0].disk;
                FILE *nf = fopen(path, "wb");
                if (nf) {
                    fwrite(&nib->track_bits_count[t], 4, 1, nf);
                    fwrite(nib->bits_data + nib->track_byte_offset[t], 1, nib->track_byte_count[t], nf);
                    fclose(nf);
                }
            }
        } else if (!strcmp(l->cmd, "doc")) {
            const uint8_t *r = gs.mmio->dev_audio.doc.reg;
            printf("host: DOC enable %02X oir %02X; running:", r[0xE1], r[0xE0]);
            for (int o = 0; o < 32; o++) {
                if (!(r[0xA0 + o] & 1)) {
                    printf(" %d(ctl %02X freq %02X%02X vol %02X size %02X)", o, r[0xA0 + o], r[0x20 + o], r[o],
                           r[0x40 + o], r[0xC0 + o]);
                }
            }
            printf("\n");
        } else if (!strcmp(l->cmd, "status")) {
            print_status();
        } else if (!strcmp(l->cmd, "peek")) {
            unsigned a = strtoul(l->arg, NULL, 16);
            printf("host: frame %u $%06X = $%02X\n", frame, a, gs_bus_read(a));
        } else if (!strcmp(l->cmd, "dump")) {
            unsigned a = 0, n = 16;
            sscanf(l->arg, "%x %u", &a, &n);
            printf("host: frame %u $%06X:", frame, a);
            for (unsigned i = 0; i < n; i++) {
                printf(" %02X", gs_bus_read(a + i));
            }
            printf("\n");
        } else {
            printf("host: unknown command %s\n", l->cmd);
        }
    }
}

/* ---- Tracing ---- */

static long s_trace_pc = -1;
static int s_trace_n;
static bool s_tracing;

/* GS_RING=<n>: the last n instructions, printed when pc reaches GS_RING_PC (default 000000) */
static struct ring_entry {
    uint32_t frame, pc;
    uint16_t a, x, y, s, d;
    uint8_t dbr, p, e;
} *s_ring;
static int s_ring_n, s_ring_pos;
static uint32_t s_ring_pc;
static bool s_ring_done;

static void ring(void)
{
    w65816_t *c = &gs.cpu;
    uint32_t pc = (uint32_t)c->pbr << 16 | c->pc;
    if (s_ring_done) {
        return;
    }
    if (pc == s_ring_pc) {
        s_ring_done = true;
        printf("host: pc %06X reached at frame %u; before it:\n", pc, gs.frames);
        for (int i = 0; i < s_ring_n; i++) {
            struct ring_entry *r = &s_ring[(s_ring_pos + i) % s_ring_n];
            if (!r->frame && !r->pc) {
                continue;
            }
            char dis[48];
            w65816_disassemble(c, r->pc, dis, sizeof(dis));
            printf("%6u %06X  %-22s A=%04X X=%04X Y=%04X S=%04X D=%04X DB=%02X P=%02X%s\n", r->frame, r->pc, dis,
                   r->a, r->x, r->y, r->s, r->d, r->dbr, r->p, r->e ? " E" : "");
        }
        return;
    }
    struct ring_entry *r = &s_ring[s_ring_pos];
    s_ring_pos = (s_ring_pos + 1) % s_ring_n;
    *r = (struct ring_entry){ gs.frames, pc, c->a, c->x, c->y, c->s, c->d, c->dbr, c->p, c->e };
}

static void trace(void)
{
    w65816_t *c = &gs.cpu;
    uint32_t pc = (uint32_t)c->pbr << 16 | c->pc;
    if (s_ring) {
        ring();
    }
    if (s_trace_pc < 0) {
        return;
    }
    if (!s_tracing && pc == (uint32_t)s_trace_pc) {
        s_tracing = true;
    }
    if (s_tracing && s_trace_n > 0) {
        char dis[48];
        w65816_disassemble(c, pc, dis, sizeof(dis));
        printf("%6u %06X  %-22s A=%04X X=%04X Y=%04X S=%04X D=%04X DB=%02X P=%02X%s\n", gs.frames, pc, dis, c->a,
               c->x, c->y, c->s, c->d, c->dbr, c->p, c->e ? " E" : "");
        s_trace_n--;
    }
}

int gs_host_no_iwm_skip;

int main(int argc, char **argv)
{
    gs_host_no_iwm_skip = getenv("GS_NO_IWM_SKIP") != NULL;
    int frames = argc > 1 ? atoi(argv[1]) : 300;
    const char *out = argc > 2 ? argv[2] : NULL;

    static gs_config_t cfg;
    cfg.ram_kb = getenv("GS_RAM") ? atoi(getenv("GS_RAM")) : 1024;
    cfg.rom = load_file(getenv("GS_ROM") ? getenv("GS_ROM") : "roms/gs_rom3.rom", 256 * 1024, true);
    cfg.chargen = load_file(getenv("GS_CHARGEN") ? getenv("GS_CHARGEN") : "roms/chargen.rom", 4096, false);
    cfg.ram = calloc(1, cfg.ram_kb * 1024);
    cfg.mega2 = calloc(1, 128 * 1024);
    cfg.frame = calloc(GS_SCREEN_WIDTH * GS_SCREEN_HEIGHT, 2);
    const char *bram_path = getenv("GS_BRAM");
    if (bram_path) {
        FILE *f = fopen(bram_path, "rb");
        if (f) {
            static uint8_t bram[GS_BRAM_SIZE];
            if (fread(bram, 1, GS_BRAM_SIZE, f) == GS_BRAM_SIZE) {
                cfg.bram = bram;
            }
            fclose(f);
        }
    }

    FILE *wav = NULL;
    if (getenv("GS_WAV")) {
        wav = fopen(getenv("GS_WAV"), "wb");
        if (wav) {
            fseek(wav, 44, SEEK_SET);
            gs_audio_enable(true);
        }
    }
    if (getenv("GS_TRACE")) {
        unsigned pc = 0;
        int n = 100;
        sscanf(getenv("GS_TRACE"), "%x:%d", &pc, &n);
        s_trace_pc = pc;
        s_trace_n = n;
        gs_host_hook = trace;
    }
    if (getenv("GS_RING")) {
        s_ring_n = atoi(getenv("GS_RING"));
        s_ring_pc = getenv("GS_RING_PC") ? strtoul(getenv("GS_RING_PC"), NULL, 16) : 0;
        if (s_ring_n > 0) {
            s_ring = calloc(s_ring_n, sizeof(*s_ring));
            gs_host_hook = trace;
        }
    }
    if (getenv("GS_NEWDISK")) {
        char path[400];
        unsigned kb = 0;
        if (sscanf(getenv("GS_NEWDISK"), "%399[^:]:%u", path, &kb) != 2 || !gs_disk_create(path, kb * 1024)) {
            fprintf(stderr, "can't make %s\n", getenv("GS_NEWDISK"));
        }
    }
    if (!gs_init(&cfg)) {
        fprintf(stderr, "gs_init failed\n");
        return 1;
    }
    static const char *drive_env[GS_DRIVES] = { "GS_35", "GS_525", "GS_HD1", "GS_HD2" };
    for (int d = 0; d < GS_DRIVES; d++) {
        if (getenv(drive_env[d]) && !gs_disk_insert(d, getenv(drive_env[d]))) {
            fprintf(stderr, "can't load %s\n", getenv(drive_env[d]));
        }
    }
    if (getenv("GS_SCRIPT")) {
        load_script(getenv("GS_SCRIPT"));
    }
    uint32_t samples = 0;
    for (int f = 0; f < frames; f++) {
        run_script(f);
        gs_run_frame();
        if (wav) {
            int16_t buf[4096 * 2];
            int n;
            while ((n = gs_audio_render(buf, 4096)) > 0) {
                fwrite(buf, 4, n, wav);
                samples += n;
            }
        }
        if (getenv("GS_STATUS") && f % 60 == 0) {
            print_status();
        }
    }
    gs_disk_flush();
    if (bram_path && gs_bram_changed()) {
        uint8_t bram[GS_BRAM_SIZE];
        gs_bram_get(bram);
        FILE *f = fopen(bram_path, "wb");
        if (f) {
            fwrite(bram, 1, GS_BRAM_SIZE, f);
            fclose(f);
        }
    }
    if (out) {
        write_ppm(out);
    }
    if (getenv("GS_TEXT")) {
        print_text(frames);
    }
    if (wav) {
        uint32_t rate = GS_AUDIO_HZ, bytes = samples * 4;
        uint8_t h[45] = "RIFF....WAVEfmt \x10\0\0\0\x01\0\x02\0........\x04\0\x10\0data....";
        uint32_t riff = bytes + 36, byte_rate = rate * 4;
        memcpy(h + 4, &riff, 4);
        memcpy(h + 24, &rate, 4);
        memcpy(h + 28, &byte_rate, 4);
        memcpy(h + 40, &bytes, 4);
        fseek(wav, 0, SEEK_SET);
        fwrite(h, 1, 44, wav);
        fclose(wav);
    }
    return 0;
}
