#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * The IIGS's ROMs: built in, from the gs_roms flash partition (roms/<name>
 * in the project, written there by idf.py flash). A file of the same name
 * in the SD card's "apple2gs/roms" folder overrides one.
 */
typedef enum {
    ROM_GS3,                    /* gs_rom3.rom: ROM 3, 256 KB (341-0737 + 341-0749) */
    ROM_CHARGEN,                /* chargen.rom: the IIe enhanced video ROM, 4 KB (342-0265-A) */
    ROM_COUNT
} rom_id_t;

const char *rom_store_name(rom_id_t id);    /* "gs_rom3.rom" */
uint32_t rom_store_size(rom_id_t id);

/* The whole ROM into buf; false if there's none */
bool rom_store_load(rom_id_t id, uint8_t *buf);
/* Whether the last load came from the SD card (else the flash) */
bool rom_store_from_sd(rom_id_t id);
