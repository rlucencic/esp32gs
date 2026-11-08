/* Experimental alternatives to the baseline DOC waveform-pointer helper.
 * Used only by the differential test and generated benchmark translation units.
 */
#ifndef DOC_POINTER_VARIANTS_H
#define DOC_POINTER_VARIANTS_H
#include "clem_mmio_types.h"

/* The reference keeps (8+size) bits after shifting by
 * resolution+(8-size). The highest retained source bit is resolution+15,
 * at most 23, so a separate 24-bit accumulator mask is unnecessary.
 * Upper config bits are ignored. No cached state or register-write changes.
 */
static inline uint16_t doc_pointer_combined(struct ClemensDeviceEnsoniq *doc, unsigned i)
{
    unsigned config = doc->reg[CLEM_ENSONIQ_REG_OSC_SIZE + i];
    unsigned size = (config >> 3) & 7;
    unsigned shift = (config & 7) + 9 - size;
    unsigned base_mask = 0xff00u << size & 0xffffu;
    unsigned base = (unsigned)doc->reg[CLEM_ENSONIQ_REG_OSC_PTR + i] << 8;
    return (base & base_mask) | ((doc->acc[i] >> shift) & (base_mask ^ 0xffffu));
}

static inline uint16_t doc_pointer_select(struct ClemensDeviceEnsoniq *doc, unsigned i)
{
    unsigned config = doc->reg[CLEM_ENSONIQ_REG_OSC_SIZE + i];
    unsigned size = (config >> 3) & 7;
    unsigned shift = (config & 7) + 9 - size;
    unsigned offset_mask = (0xffffu >> (8 - size));
    unsigned base = (unsigned)doc->reg[CLEM_ENSONIQ_REG_OSC_PTR + i] << 8;
    return base ^ ((base ^ (doc->acc[i] >> shift)) & offset_mask);
}

static inline uint16_t doc_pointer_table(struct ClemensDeviceEnsoniq *doc, unsigned i)
{
    static const uint16_t masks[8] = {0xff00,0xfe00,0xfc00,0xf800,0xf000,0xe000,0xc000,0x8000};
    unsigned config = doc->reg[CLEM_ENSONIQ_REG_OSC_SIZE + i];
    unsigned size = (config >> 3) & 7;
    unsigned shift = (config & 7) + 9 - size;
    unsigned base_mask = masks[size];
    unsigned base = (unsigned)doc->reg[CLEM_ENSONIQ_REG_OSC_PTR + i] << 8;
    return (base & base_mask) | ((doc->acc[i] >> shift) & (base_mask ^ 0xffffu));
}
#endif
