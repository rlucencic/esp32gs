#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "gs.h"

/* Machine settings kept in NVS (namespace "gs") */
typedef struct {
    uint16_t ram_kb;        /* fast RAM: 1024 ... 8192 */
    bool have_bram;         /* bram holds the control panel's settings */
    uint8_t bram[GS_BRAM_SIZE];
} settings_t;

extern settings_t settings;

void settings_load(void);
void settings_save(void);
void settings_save_bram(void);
