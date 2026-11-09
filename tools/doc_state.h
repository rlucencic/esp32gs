/* Test-only field-based DOC state. This is not a save-state format. */
#ifndef GS_DOC_STATE_H
#define GS_DOC_STATE_H
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

struct DocState {
    uint32_t dt_budget, cycle;
    float voice[16];
    uint8_t sound_ram[65536], reg[256];
    uint32_t acc[32];
    uint16_t ptr[32];
    uint8_t osc_flags[32], osc_stack[32];
    uint32_t address;
    uint8_t data_reg;
    bool addr_auto_inc, is_access_ram, is_busy;
};
#define DOC_STATE_SCALARS(X,D,S) \
    X(D,S,dt_budget) X(D,S,cycle) X(D,S,address) X(D,S,data_reg) \
    X(D,S,addr_auto_inc) X(D,S,is_access_ram) X(D,S,is_busy)
#define DOC_STATE_ARRAYS(X,D,S) \
    X(D,S,voice) X(D,S,sound_ram) X(D,S,reg) X(D,S,acc) X(D,S,ptr) X(D,S,osc_flags) X(D,S,osc_stack)
/* Both objects must have the named fields, irrespective of their offsets. */
#define DOC_COPY_SCALAR(D,S,field) (D)->field = (S)->field;
#define DOC_COPY_ARRAY(D,S,field) memcpy((D)->field, (S)->field, sizeof((D)->field));
#define DOC_STATE_COPY(D,S) \
    do { DOC_STATE_SCALARS(DOC_COPY_SCALAR,D,S) DOC_STATE_ARRAYS(DOC_COPY_ARRAY,D,S) } while (0)

/* Little-endian integer values, exact float bits, normalized bools, no padding. */
#define DOC_STATE_BYTES 66128u
static inline void doc_state_put(uint8_t **p, uint32_t v, unsigned n) {
    for (unsigned i=0; i<n; ++i) { *(*p)++=(uint8_t)v; v>>=8; }
}
#define DOC_STATE_PACK_FUNCTION(NAME,TYPE) \
static inline void NAME(uint8_t out[DOC_STATE_BYTES], const TYPE *d) { \
    _Static_assert(sizeof(float)==4, "DOC snapshots require 32-bit float"); \
    uint8_t *p=out; \
    doc_state_put(&p,d->dt_budget,4); doc_state_put(&p,d->cycle,4); \
    for (unsigned i=0;i<16;++i) { uint32_t bits; memcpy(&bits,&d->voice[i],4); doc_state_put(&p,bits,4); } \
    memcpy(p,d->sound_ram,65536);p+=65536; \
    memcpy(p,d->reg,256);p+=256; \
    for (unsigned i=0;i<32;++i) doc_state_put(&p,d->acc[i],4); \
    for (unsigned i=0;i<32;++i) doc_state_put(&p,d->ptr[i],2); \
    memcpy(p,d->osc_flags,32);p+=32;memcpy(p,d->osc_stack,32);p+=32; \
    doc_state_put(&p,d->address,4);*p++=d->data_reg; \
    *p++=!!d->addr_auto_inc;*p++=!!d->is_access_ram;*p++=!!d->is_busy; \
}
DOC_STATE_PACK_FUNCTION(doc_state_pack,struct DocState)
#endif
