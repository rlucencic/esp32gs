/* Snapshot format checks: byte order, all fields, padding and float payloads. */
#ifdef NDEBUG
#error "Snapshot checks require assertions"
#endif
#include <assert.h>
#include <stdio.h>
#include "doc_snapshot.h"
static struct ClemensDeviceEnsoniq actual;
static struct DocState zero;
static uint8_t baseline[DOC_STATE_BYTES], bytes[DOC_STATE_BYTES+16];
static unsigned checks;
static void different(void) {
    memset(bytes,0x5a,sizeof(bytes));doc_snapshot(bytes,&actual);
    assert(memcmp(bytes,baseline,DOC_STATE_BYTES));
    for(unsigned i=DOC_STATE_BYTES;i<sizeof(bytes);i++)assert(bytes[i]==0x5a);
    ++checks;
}
int main(void) {
    _Static_assert(sizeof(unsigned)==4,"Test schema expects 32-bit DOC unsigned");
    memset(&actual,0xa5,sizeof(actual));DOC_STATE_COPY(&actual,&zero);
    memset(bytes,0x5a,sizeof(bytes));doc_snapshot(bytes,&actual);
    assert(!memcmp(bytes,baseline,DOC_STATE_BYTES));
    for(unsigned i=DOC_STATE_BYTES;i<sizeof(bytes);i++)assert(bytes[i]==0x5a);
#define CHECK_SCALAR(D,S,f) actual.f=1;different();actual.f=0;
    DOC_STATE_SCALARS(CHECK_SCALAR,unused,unused)
#undef CHECK_SCALAR
#define CHECK_ARRAY(D,S,f) \
    ((uint8_t*)actual.f)[0]=1;different();((uint8_t*)actual.f)[0]=0; \
    ((uint8_t*)actual.f)[sizeof(actual.f)-1]=1;different();((uint8_t*)actual.f)[sizeof(actual.f)-1]=0;
    DOC_STATE_ARRAYS(CHECK_ARRAY,unused,unused)
#undef CHECK_ARRAY
    actual.dt_budget=0x12345678;actual.cycle=0x9abcdef0;
    actual.address=0x87654321;actual.acc[31]=0x11223344;actual.ptr[31]=0x5566;
    uint32_t bits=0x7fc12345;memcpy(&actual.voice[0],&bits,4); /* NaN payload */
    doc_snapshot(bytes,&actual);
    assert(!memcmp(bytes,"\x78\x56\x34\x12\xf0\xde\xbc\x9a\x45\x23\xc1\x7f",12));
    assert(!memcmp(bytes+65988,"\x44\x33\x22\x11",4));
    assert(!memcmp(bytes+66054,"\x66\x55",2));
    assert(!memcmp(bytes+66120,"\x21\x43\x65\x87",4));
    printf("SNAPSHOT PASS fields=14 mutation_checks=%u bytes=%u doc_size=%zu sound_ram_offset=%zu\n",checks,DOC_STATE_BYTES,sizeof(actual),(size_t)((uint8_t*)actual.sound_ram-(uint8_t*)&actual));
}
