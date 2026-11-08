/* Shared interfaces for generated full-audio-module variants and their checks. */
#ifndef DOC_NORMALIZE_CHECK_H
#define DOC_NORMALIZE_CHECK_H
#include "clem_mmio_types.h"
#define DECL(P) \
unsigned P##clem_ensoniq_voices(struct ClemensDeviceEnsoniq *); \
void P##clem_ensoniq_mono(struct ClemensDeviceEnsoniq *,unsigned,float *,float *);
DECL(ref_) DECL(unit_) DECL(dual_)
#undef DECL
uint32_t normalize_run_reference(struct ClemensDeviceEnsoniq *,unsigned);
uint32_t normalize_run_unit(struct ClemensDeviceEnsoniq *,unsigned);
uint32_t normalize_run_dual(struct ClemensDeviceEnsoniq *,unsigned);
void doc_normalize_check(void);
#endif
