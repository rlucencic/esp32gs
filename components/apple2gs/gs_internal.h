#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "gs.h"
#include "w65816.h"
#include "clem_mmio_types.h"
#include "clem_types.h"

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define GS_HOT      IRAM_ATTR
#else
#define GS_HOT
#endif

#define GS_LINES            262                 /* NTSC */
/* A scanline: 65 Mega II cycles, one of them stretched by a 7 MHz cycle
 * (63.7 us; the IWM's bit cells see the stretch too, Clemens's timing)
 */
#define GS_LINE_CLOCKS      (65 * CLEM_CLOCKS_PHI0_CYCLE + CLEM_CLOCKS_7MHZ_CYCLE)

typedef struct {
    gs_config_t cfg;
    ClemensMachine *clem;       /* Clemens's memory map bookkeeping and clock */
    ClemensMMIO *mmio;          /* its I/O and devices */
    w65816_t cpu;
    uint32_t frames;
    uint32_t signature;
} gs_t;

extern gs_t gs;

void *gs_alloc(uint32_t bytes);
void gs_release(void *p);
void gs_log_f(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* video.c */
void video_init(void);
void video_frame(void);         /* the picture, from E0/E1 and the VGC, at the end of a frame */

/* storage.c */
bool storage_prepare(void);     /* the drives' buffers, once */
void storage_init(void);        /* the drives and the slot 7 card into Clemens's state */
void storage_reset(void);
void storage_frame(void);       /* write-back and the computer's own eject */
void storage_data_read(uint8_t v); /* a read of the IWM's data register */
ClemensCard *storage_hdd_card(void);
void storage_status(gs_status_t *s);

#ifndef ESP_PLATFORM
extern void (*gs_host_hook)(void);  /* the host build: before each instruction, if set */
#endif

/* gs.c: the memory as the CPU sees it, for DMA and debugging */
uint8_t gs_bus_read(uint32_t addr);
void gs_bus_write(uint32_t addr, uint8_t v);
