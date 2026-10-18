/*
 * Disks: the 3.5" drive (slot 5), the 5.25" drive (slot 6) and the hard
 * disk card in slot 7 with two units.
 *
 * Floppies live as nibbles (what the IWM reads and writes bit by bit) in a
 * buffer per drive: the image file is encoded into it a track at a time on
 * insert, and tracks the computer changed are decoded back into the file
 * once the drive's motor has been off for a second (and on eject). Each
 * track's hash, taken after encoding or decoding, tells which ones changed.
 * .WOZ images are nibbles already: read whole, used write-protected.
 *
 * Hard disks are ProDOS block images, read and written in place, 512 bytes
 * a block (.2MG: after its header).
 */
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include "gs_internal.h"
#include "clem_mmio_types.h"
#include "clem_disk.h"
#include "clem_drive.h"
#include "clem_woz.h"
#include "emulator_mmio.h"
#include "hddcard.h"
#include "prodos_hdd32.h"

#define PATH_MAX_LEN    256
#define FLUSH_FRAMES    60          /* motor off this long: write back */
#define BUSY_FRAMES     10
#define TRACK_BYTES     (12 * 512)

typedef struct {
    char path[PATH_MAX_LEN];
    bool loaded, wp, woz;
    unsigned format;                /* CLEM_DISK_FORMAT_DOS or _PRODOS */
    uint32_t offset;                /* the data's start in the file */
    uint32_t bytes;                 /* 140 KB, 800 KB or 1600 KB */
    uint8_t *bits;
    uint32_t bits_size;
    uint32_t hash[CLEM_DISK_LIMIT_QTR_TRACKS];
    int idle_frames;
    bool ejecting;
    uint32_t reads, writes;
    uint32_t sectors;               /* read by the computer (storage_data_read()) */
    int busy;
    enum ClemensDriveType type;
} floppy_t;

typedef struct {
    char path[PATH_MAX_LEN];
    FILE *f;
    bool wp;
    uint32_t offset;
    uint32_t blocks;
    uint32_t reads, writes;
    int busy;
    bool dirty;
    ClemensProdosHDD32 hdd;
} hd_t;

static floppy_t s_fd[2];            /* GS_DRIVE_35, GS_DRIVE_525 */
static volatile uint32_t s_ejected; /* drives the computer ejected itself */
static hd_t s_hd[2];
static ClemensCard s_card;
static uint8_t *s_track;            /* one track's sectors (12 x 512 bytes) */

static inline uint32_t fnv(const uint8_t *p, uint32_t n)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < n; i++) {
        h = (h ^ p[i]) * 16777619u;
    }
    return h;
}

static bool has_ext(const char *path, const char *ext)
{
    size_t n = strlen(path), e = strlen(ext);
    return n > e && strcasecmp(path + n - e, ext) == 0;
}

static long file_size(FILE *f)
{
    if (fseek(f, 0, SEEK_END) != 0) {
        return -1;
    }
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    return n;
}

/* A 2IMG header: the data's offset and size, the format, locked or not */
static bool parse_2mg(FILE *f, uint32_t *offset, uint32_t *bytes, unsigned *format, bool *wp)
{
    uint8_t h[64];
    if (fread(h, 1, 64, f) != 64 || memcmp(h, "2IMG", 4) != 0) {
        return false;
    }
    uint32_t fmt = h[12] | h[13] << 8 | h[14] << 16 | (uint32_t)h[15] << 24;
    uint32_t flags = h[16] | h[17] << 8 | h[18] << 16 | (uint32_t)h[19] << 24;
    uint32_t blocks = h[20] | h[21] << 8 | h[22] << 16 | (uint32_t)h[23] << 24;
    *offset = h[24] | h[25] << 8 | h[26] << 16 | (uint32_t)h[27] << 24;
    *bytes = h[28] | h[29] << 8 | h[30] << 16 | (uint32_t)h[31] << 24;
    if (fmt > 1) {
        return false;                               /* nibbles: not supported */
    }
    if (*bytes == 0 && fmt == 1) {
        *bytes = blocks * 512;
    }
    *format = fmt == 0 ? CLEM_DISK_FORMAT_DOS : CLEM_DISK_FORMAT_PRODOS;
    if (flags & 0x80000000u) {
        *wp = true;
    }
    return *offset >= 64;
}

/* ---- Floppies ---- */

static struct ClemensDrive *drive_of(floppy_t *fd)
{
    return clemens_drive_get(gs.mmio, fd->type);
}

/* The tracks of a 3.5" disk: 80 per side in 5 speed regions (12 ... 8 sectors) */
typedef void (*track_fn)(floppy_t *fd, struct ClemensNibbleDisk *nib, unsigned nib_track, unsigned qtr,
                         unsigned region, unsigned first_sector, unsigned sectors, unsigned byte_offset,
                         unsigned byte_count, void *arg);

static void tracks_35(floppy_t *fd, struct ClemensNibbleDisk *nib, track_fn fn, void *arg)
{
    unsigned per = nib->is_double_sided ? 1 : 2;
    unsigned region = 0, sector = 0, offset = 0;
    for (unsigned qtr = 0; qtr < CLEM_DISK_LIMIT_QTR_TRACKS; qtr += per) {
        unsigned n = g_clem_max_sectors_per_region_35[region];
        unsigned bytes = CLEM_DISK_35_CALC_BYTES_FROM_SECTORS(n);
        unsigned t = qtr / per;
        if (t >= nib->track_count) {
            break;
        }
        fn(fd, nib, t, qtr, region, sector, n, offset, bytes, arg);
        sector += n;
        offset += bytes;
        if (qtr + per >= g_clem_track_start_per_region_35[region + 1]) {
            region++;
        }
    }
}

static void encode_35(floppy_t *fd, struct ClemensNibbleDisk *nib, unsigned t, unsigned qtr, unsigned region,
                      unsigned first, unsigned n, unsigned offset, unsigned bytes, void *arg)
{
    FILE *f = arg;
    memset(s_track, 0, TRACK_BYTES);
    fseek(f, fd->offset + first * 512, SEEK_SET);
    fread(s_track, 512, n, f);
    struct ClemensNibEncoder enc;
    _ClemensPhysicalSectorMap map = get_physical_to_logical_sector_map(CLEM_DISK_TYPE_3_5, fd->format);
    uint8_t sector_format = (nib->is_double_sided ? 0x20 : 0x00) | 0x2;
    if (!clem_nib_begin_track_encoder(&enc, nib, t, offset, bytes)) {
        return;
    }
    clem_disk_nib_encode_track_35(&enc, qtr / 2, qtr % 2, sector_format, 0, n, map[region], s_track);
    clem_nib_end_track_encoder(&enc, nib, t);
    nib->meta_track_map[qtr] = t;
    if (!nib->is_double_sided) {
        nib->meta_track_map[qtr + 1] = 0xFF;
    }
    fd->hash[t] = fnv(nib->bits_data + nib->track_byte_offset[t], nib->track_byte_count[t]);
}

static void decode_35(floppy_t *fd, struct ClemensNibbleDisk *nib, unsigned t, unsigned qtr, unsigned region,
                      unsigned first, unsigned n, unsigned offset, unsigned bytes, void *arg)
{
    FILE *f = arg;
    uint32_t h = fnv(nib->bits_data + nib->track_byte_offset[t], nib->track_byte_count[t]);
    if (h == fd->hash[t]) {
        return;
    }
    _ClemensPhysicalSectorMap map = get_physical_to_logical_sector_map(CLEM_DISK_TYPE_3_5, fd->format);
    if (clem_disk_nib_decode_nibblized_track_35(nib, map[region], t, 0, s_track, s_track + n * 512) == n * 512) {
        fseek(f, fd->offset + first * 512, SEEK_SET);
        fwrite(s_track, 512, n, f);
        fd->hash[t] = h;
        fd->writes++;
    } else {
        gs_log_f("disk: track %u of %s didn't decode", t, fd->path);
    }
}

static bool encode_525(floppy_t *fd, struct ClemensNibbleDisk *nib, FILE *f)
{
    _ClemensPhysicalSectorMap map = get_physical_to_logical_sector_map(CLEM_DISK_TYPE_5_25, fd->format);
    for (unsigned t = 0; t < CLEM_DISK_LIMIT_525_DISK_TRACKS; t++) {
        struct ClemensNibEncoder enc;
        fseek(f, fd->offset + t * 4096, SEEK_SET);
        memset(s_track, 0, 4096);
        fread(s_track, 256, 16, f);
        if (!clem_nib_begin_track_encoder(&enc, nib, t, t * CLEM_DISK_525_BYTES_PER_TRACK,
                                          CLEM_DISK_525_BYTES_PER_TRACK)) {
            return false;
        }
        clem_disk_nib_encode_track_525(&enc, CLEM_DISK_FORMAT_DOS_VOLUME_DEFAULT, t, 0, 16, map[0], s_track);
        clem_nib_end_track_encoder(&enc, nib, t);
        if (t != 0) {
            nib->meta_track_map[t * 4 - 1] = t;
        }
        nib->meta_track_map[t * 4] = t;
        nib->meta_track_map[t * 4 + 1] = t;
        fd->hash[t] = fnv(nib->bits_data + nib->track_byte_offset[t], nib->track_byte_count[t]);
    }
    return true;
}

static void decode_525(floppy_t *fd, struct ClemensNibbleDisk *nib, FILE *f)
{
    _ClemensPhysicalSectorMap map = get_physical_to_logical_sector_map(CLEM_DISK_TYPE_5_25, fd->format);
    for (unsigned t = 0; t < nib->track_count; t++) {
        uint32_t h = fnv(nib->bits_data + nib->track_byte_offset[t], nib->track_byte_count[t]);
        if (h == fd->hash[t]) {
            continue;
        }
        if (clem_disk_nib_decode_nibblized_track_525(nib, map[0], t, 0, s_track, s_track + 4096) == 4096) {
            fseek(f, fd->offset + t * 4096, SEEK_SET);
            fwrite(s_track, 256, 16, f);
            fd->hash[t] = h;
            fd->writes++;
        } else {
            gs_log_f("disk: track %u of %s didn't decode", t, fd->path);
        }
    }
}

static bool floppy_flush(floppy_t *fd)
{
    struct ClemensDrive *drive = drive_of(fd);
    if (!fd->loaded || fd->woz || fd->wp || !drive->disk.is_dirty) {
        return true;
    }
    FILE *f = fopen(fd->path, "r+b");
    if (!f) {
        gs_log_f("disk: can't write %s", fd->path);
        return false;
    }
    if (drive->disk.disk_type == CLEM_DISK_TYPE_3_5) {
        tracks_35(fd, &drive->disk, decode_35, f);
    } else {
        decode_525(fd, &drive->disk, f);
    }
    fflush(f);
    fsync(fileno(f));
    fclose(f);
    drive->disk.is_dirty = false;
    return true;
}

static bool load_woz(floppy_t *fd, struct ClemensNibbleDisk *nib, FILE *f, unsigned disk_type)
{
    long n = file_size(f);
    if (n <= 0) {
        return false;
    }
    uint8_t *buf = gs_alloc(n);
    if (!buf) {
        gs_log_f("disk: no memory for %s (%ld bytes)", fd->path, n);
        return false;
    }
    bool ok = fread(buf, 1, n, f) == (size_t)n;
    struct ClemensWOZDisk woz;
    memset(&woz, 0, sizeof(woz));
    woz.nib = nib;
    int errc = 0;
    if (ok) {
        clem_nib_reset_tracks(nib, 0, fd->bits, fd->bits + fd->bits_size);
        ok = clem_woz_unserialize(&woz, buf, n, CLEM_WOZ_SUPPORTED_VERSION, &errc) != NULL && errc == 0 &&
             nib->disk_type == disk_type;
    }
    gs_release(buf);
    fd->woz = true;
    fd->wp = true;
    return ok;
}

static bool floppy_insert(floppy_t *fd, const char *path)
{
    struct ClemensDrive *drive = drive_of(fd);
    unsigned disk_type = fd->type == kClemensDrive_3_5_D1 ? CLEM_DISK_TYPE_3_5 : CLEM_DISK_TYPE_5_25;
    FILE *f = fopen(path, "r+b");
    bool wp = false;
    if (!f) {
        f = fopen(path, "rb");
        wp = true;
    }
    if (!f) {
        return false;
    }
    strlcpy(fd->path, path, sizeof(fd->path));
    fd->wp = wp;
    fd->woz = false;
    fd->offset = 0;
    fd->format = CLEM_DISK_FORMAT_PRODOS;
    long size = file_size(f);

    struct ClemensNibbleDisk *nib = &drive->disk;
    memset(nib, 0, sizeof(*nib));
    nib->disk_type = disk_type;
    nib->bits_data = fd->bits;
    nib->bits_data_end = fd->bits + fd->bits_size;
    memset(fd->bits, 0, fd->bits_size);
    bool ok = false;

    if (has_ext(path, ".woz")) {
        ok = load_woz(fd, nib, f, disk_type);
    } else {
        uint32_t bytes = size;
        if (has_ext(path, ".2mg")) {
            ok = parse_2mg(f, &fd->offset, &bytes, &fd->format, &fd->wp);
        } else {
            ok = true;
            if (disk_type == CLEM_DISK_TYPE_5_25 && !has_ext(path, ".po")) {
                fd->format = CLEM_DISK_FORMAT_DOS;     /* .dsk, .do: DOS order */
            }
        }
        fd->bytes = bytes;
        if (ok && disk_type == CLEM_DISK_TYPE_3_5) {
            ok = bytes == CLEM_DISK_35_PRODOS_BLOCK_COUNT * 512 || bytes == CLEM_DISK_35_DOUBLE_PRODOS_BLOCK_COUNT * 512;
            if (ok) {
                bool dbl = bytes == CLEM_DISK_35_DOUBLE_PRODOS_BLOCK_COUNT * 512;
                clem_nib_reset_tracks(nib, dbl ? 160 : 80, fd->bits, fd->bits + fd->bits_size);
                nib->is_double_sided = dbl;
                nib->bit_timing_ns = CLEM_DISK_3_5_BIT_TIMING_NS;
                tracks_35(fd, nib, encode_35, f);
            }
        } else if (ok) {
            ok = bytes == CLEM_DISK_525_PRODOS_BLOCK_COUNT * 512;
            if (ok) {
                clem_nib_reset_tracks(nib, CLEM_DISK_LIMIT_525_DISK_TRACKS, fd->bits, fd->bits + fd->bits_size);
                nib->bit_timing_ns = CLEM_DISK_5_25_BIT_TIMING_NS;
#ifndef ESP_PLATFORM
                if (getenv("GS_CLEM525")) {     /* host experiment: Clemens's own whole-disk encoder */
                    static uint8_t img[143360];
                    fseek(f, fd->offset, SEEK_SET);
                    fread(img, 1, sizeof(img), f);
                    ok = clem_disk_nib_encode_525(nib, fd->format, CLEM_DISK_FORMAT_DOS_VOLUME_DEFAULT, img, img + sizeof(img));
                } else
#endif
                ok = encode_525(fd, nib, f);
            }
        }
    }
    fclose(f);
    if (!ok) {
        memset(nib, 0, sizeof(*nib));
        nib->disk_type = disk_type;
        fd->path[0] = 0;
        return false;
    }
    nib->is_write_protected = fd->wp;
    clemens_insert_disk(gs.mmio, fd->type);
    fd->loaded = true;
    fd->idle_frames = 0;
    fd->ejecting = false;
    return true;
}

static void floppy_eject(floppy_t *fd)
{
    if (!fd->loaded) {
        return;
    }
    floppy_flush(fd);
    clemens_eject_disk(gs.mmio, fd->type);
    fd->loaded = false;
    fd->path[0] = 0;
}

/* Once a frame: write back after the motor stopped; the computer's own eject */
static void floppy_frame(floppy_t *fd)
{
    struct ClemensDrive *drive = drive_of(fd);
    if (!fd->loaded) {
        return;
    }
    if (drive->is_spindle_on) {
        fd->idle_frames = 0;
        fd->busy = BUSY_FRAMES;
    } else {
        if (++fd->idle_frames == FLUSH_FRAMES) {
            floppy_flush(fd);
        }
        if (fd->busy > 0) {
            fd->busy--;
        }
    }
    bool ejecting = clemens_eject_disk_in_progress(gs.mmio, fd->type) == CLEM_EJECT_DISK_STATUS_IN_PROGRESS;
    if (fd->ejecting && !ejecting && (drive->status_mask_35 & CLEM_IWM_DISK35_STATUS_EJECTED)) {
        floppy_eject(fd);
        __atomic_or_fetch(&s_ejected, 1u << (fd - s_fd), __ATOMIC_RELAXED);
    }
    fd->ejecting = ejecting;
}

/* ---- Hard disks ---- */

static uint8_t hd_read(void *ctx, unsigned drive_index, unsigned block, uint8_t *buf)
{
    hd_t *hd = ctx;
    (void)drive_index;
    if (!hd->f || block >= hd->blocks) {
        return CLEM_SMARTPORT_STATUS_CODE_INVALID_BLOCK;
    }
    if (fseek(hd->f, hd->offset + (long)block * 512, SEEK_SET) != 0 || fread(buf, 512, 1, hd->f) != 1) {
        return CLEM_SMARTPORT_STATUS_CODE_IO_ERR;
    }
    hd->reads++;
    hd->busy = BUSY_FRAMES;
    return CLEM_SMARTPORT_STATUS_CODE_OK;
}

static uint8_t hd_write(void *ctx, unsigned drive_index, unsigned block, const uint8_t *buf)
{
    hd_t *hd = ctx;
    (void)drive_index;
    if (!hd->f || block >= hd->blocks) {
        return CLEM_SMARTPORT_STATUS_CODE_INVALID_BLOCK;
    }
    if (hd->wp) {
        return 0x2B /* write-protected */;
    }
    if (fseek(hd->f, hd->offset + (long)block * 512, SEEK_SET) != 0 || fwrite(buf, 512, 1, hd->f) != 1) {
        return CLEM_SMARTPORT_STATUS_CODE_IO_ERR;
    }
    hd->writes++;
    hd->busy = BUSY_FRAMES;
    hd->dirty = true;
    return CLEM_SMARTPORT_STATUS_CODE_OK;
}

static bool hd_flush(hd_t *hd)
{
    if (hd->f && hd->dirty) {
        fflush(hd->f);
        fsync(fileno(hd->f));
        hd->dirty = false;
    }
    return true;
}

static bool hd_insert(int unit, const char *path)
{
    hd_t *hd = &s_hd[unit];
    FILE *f = fopen(path, "r+b");
    bool wp = false;
    if (!f) {
        f = fopen(path, "rb");
        wp = true;
    }
    if (!f) {
        return false;
    }
    long size = file_size(f);
    uint32_t offset = 0, bytes = size;
    unsigned format;
    if (has_ext(path, ".2mg") && !parse_2mg(f, &offset, &bytes, &format, &wp)) {
        fclose(f);
        return false;
    }
    if (bytes < 512 || bytes / 512 > 65535) {
        fclose(f);
        return false;
    }
    strlcpy(hd->path, path, sizeof(hd->path));
    hd->f = f;
    hd->wp = wp;
    hd->offset = offset;
    hd->blocks = bytes / 512;
    hd->dirty = false;
    memset(&hd->hdd, 0, sizeof(hd->hdd));
    hd->hdd.user_context = hd;
    hd->hdd.drive_index = unit;
    hd->hdd.block_limit = hd->blocks;
    hd->hdd.read_block = hd_read;
    hd->hdd.write_block = hd_write;
    clem_card_hdd_mount(&s_card, &hd->hdd, unit + 1);
    clem_card_hdd_lock(&s_card, wp, unit + 1);
    return true;
}

static void hd_eject(int unit)
{
    hd_t *hd = &s_hd[unit];
    if (!hd->f) {
        return;
    }
    hd_flush(hd);
    clem_card_hdd_unmount(&s_card, unit + 1);
    fclose(hd->f);
    hd->f = NULL;
    hd->path[0] = 0;
}

/* Blocks read from a floppy: the IWM works on bits, so watch the whole
 * nibbles the computer reads (a byte with its top bit set after one
 * without) for data field prologues, D5 AA AD: a sector read (512 bytes on
 * a 3.5" disk, 256 on a 5.25")
 */
void storage_data_read(uint8_t v)
{
    static bool was_valid;
    static uint32_t last;
    bool valid = v & 0x80;
    if (valid && !was_valid) {
        last = (last << 8 | v) & 0xFFFFFF;
        if (last == 0xD5AAAD) {
            floppy_t *fd = &s_fd[(gs.mmio->dev_iwm.io_flags & CLEM_IWM_FLAG_DRIVE_35) ? 0 : 1];
            if (fd->loaded) {
                fd->sectors++;
            }
        }
    }
    was_valid = valid;
}

/* ---- The machine's side ---- */

bool storage_prepare(void)
{
    s_track = gs_alloc(TRACK_BYTES);
    s_fd[0].type = kClemensDrive_3_5_D1;
    s_fd[0].bits_size = clem_disk_calculate_nib_storage_size(CLEM_DISK_TYPE_3_5);
    s_fd[0].bits = gs_alloc(s_fd[0].bits_size);
    s_fd[1].type = kClemensDrive_5_25_D1;
    s_fd[1].bits_size = clem_disk_calculate_nib_storage_size(CLEM_DISK_TYPE_5_25);
    s_fd[1].bits = gs_alloc(s_fd[1].bits_size);
    clem_card_hdd_initialize(&s_card);
    return s_track && s_fd[0].bits && s_fd[1].bits && s_card.context;
}

void storage_init(void)
{
    /* Clemens's state was cleared: the disks go back in */
    for (int i = 0; i < 2; i++) {
        floppy_t *fd = &s_fd[i];
        clemens_assign_disk_buffer(gs.mmio, fd->type, fd->bits, fd->bits + fd->bits_size);
        if (fd->loaded) {
            char path[PATH_MAX_LEN];
            strlcpy(path, fd->path, sizeof(path));
            fd->loaded = false;
            floppy_insert(fd, path);
        }
        if (s_hd[i].f) {
            clem_card_hdd_mount(&s_card, &s_hd[i].hdd, i + 1);
            clem_card_hdd_lock(&s_card, s_hd[i].wp, i + 1);
        }
    }
}

void storage_reset(void)
{
    for (int i = 0; i < 2; i++) {
        floppy_flush(&s_fd[i]);
        hd_flush(&s_hd[i]);
    }
}

ClemensCard *storage_hdd_card(void)
{
    return &s_card;
}

void storage_frame(void)
{
    for (int i = 0; i < 2; i++) {
        floppy_frame(&s_fd[i]);
        if (s_hd[i].busy > 0) {
            s_hd[i].busy--;
        }
    }
}

void storage_status(gs_status_t *s)
{
    for (int i = 0; i < 2; i++) {
        floppy_t *fd = &s_fd[i];
        struct ClemensDrive *drive = drive_of(fd);
        s->busy[i] = fd->loaded && fd->busy > 0;
        s->track[i] = fd->loaded ? drive->qtr_track_index / (i == 0 ? 2 : 4) : -1;
        s->reads[i] = i == 0 ? fd->sectors : fd->sectors / 2;     /* 512-byte blocks */
        s->writes[i] = fd->writes;
        s->busy[GS_DRIVE_HD1 + i] = s_hd[i].busy > 0;
        s->track[GS_DRIVE_HD1 + i] = -1;
        s->reads[GS_DRIVE_HD1 + i] = s_hd[i].reads;
        s->writes[GS_DRIVE_HD1 + i] = s_hd[i].writes;
    }
}

bool gs_disk_insert(int drive, const char *path)
{
    gs_disk_eject(drive);
    if (drive == GS_DRIVE_35 || drive == GS_DRIVE_525) {
        return floppy_insert(&s_fd[drive], path);
    }
    if (drive == GS_DRIVE_HD1 || drive == GS_DRIVE_HD2) {
        return hd_insert(drive - GS_DRIVE_HD1, path);
    }
    return false;
}

void gs_disk_eject(int drive)
{
    if (drive == GS_DRIVE_35 || drive == GS_DRIVE_525) {
        floppy_eject(&s_fd[drive]);
    } else if (drive == GS_DRIVE_HD1 || drive == GS_DRIVE_HD2) {
        hd_eject(drive - GS_DRIVE_HD1);
    }
}

const char *gs_disk_path(int drive)
{
    if (drive == GS_DRIVE_35 || drive == GS_DRIVE_525) {
        return s_fd[drive].loaded ? s_fd[drive].path : NULL;
    }
    if (drive == GS_DRIVE_HD1 || drive == GS_DRIVE_HD2) {
        return s_hd[drive - GS_DRIVE_HD1].f ? s_hd[drive - GS_DRIVE_HD1].path : NULL;
    }
    return NULL;
}

bool gs_disk_write_protected(int drive)
{
    if (drive == GS_DRIVE_35 || drive == GS_DRIVE_525) {
        return s_fd[drive].wp;
    }
    if (drive == GS_DRIVE_HD1 || drive == GS_DRIVE_HD2) {
        return s_hd[drive - GS_DRIVE_HD1].wp;
    }
    return false;
}

uint32_t gs_disk_ejected(void)
{
    return __atomic_exchange_n(&s_ejected, 0, __ATOMIC_RELAXED);
}

bool gs_disk_dirty(void)
{
    for (int i = 0; i < 2; i++) {
        if (s_fd[i].loaded && !s_fd[i].wp && drive_of(&s_fd[i])->disk.is_dirty) {
            return true;
        }
        if (s_hd[i].dirty) {
            return true;
        }
    }
    return false;
}

bool gs_disk_flush(void)
{
    bool ok = true;
    for (int i = 0; i < 2; i++) {
        ok &= floppy_flush(&s_fd[i]);
        ok &= hd_flush(&s_hd[i]);
    }
    return ok;
}

/* A blank disk: zeroes (GS/OS and ProDOS's Initialize format it) */
bool gs_disk_create(const char *path, uint32_t bytes)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        return false;
    }
    bool ok = true;
    if (bytes > 0) {
        ok = fseek(f, bytes - 1, SEEK_SET) == 0 && fputc(0, f) != EOF;
    }
    ok &= fclose(f) == 0;
    return ok;
}
