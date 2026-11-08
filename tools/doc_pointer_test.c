/* Independent bit-selection oracle; link with generated production/variant wrappers. */
#ifdef NDEBUG
#error "Pointer tests require assertions"
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "clem_mmio_types.h"
uint16_t pointer_reference(struct ClemensDeviceEnsoniq *, unsigned);
uint16_t pointer_combined(struct ClemensDeviceEnsoniq *, unsigned);
uint16_t pointer_table(struct ClemensDeviceEnsoniq *, unsigned);
uint16_t pointer_select(struct ClemensDeviceEnsoniq *, unsigned);
static struct ClemensDeviceEnsoniq doc;
static uint32_t seed = 0x739af195;
static uint32_t random32(void) {seed ^= seed << 13;seed ^= seed >> 17;seed ^= seed << 5;return seed;}
static uint16_t oracle(uint32_t acc, unsigned config, unsigned base)
{
    unsigned size = (config >> 3) & 7, resolution = (config & 7) + 1;
    unsigned out = 0;
    for (unsigned bit = 0; bit < 16; ++bit) {
        unsigned value;
        if (bit < 8 + size) value = (acc >> (resolution + 8 - size + bit)) & 1;
        else value = (base >> (bit - 8)) & 1;
        out |= value << bit;
    }
    return out;
}
static void check(unsigned i, unsigned config, unsigned base, uint32_t acc)
{
    doc.reg[CLEM_ENSONIQ_REG_OSC_SIZE+i] = config;
    doc.reg[CLEM_ENSONIQ_REG_OSC_PTR+i] = base;
    doc.acc[i] = acc;
    unsigned want = oracle(acc,config,base);
    assert(pointer_reference(&doc,i)==want);
    assert(pointer_combined(&doc,i)==want);
    assert(pointer_select(&doc,i)==want);
    assert(pointer_table(&doc,i)==want);
}
int main(void)
{
    uint64_t count = 0;
    /* Every 16-bit source window, every size/resolution, changing base and
       ignored accumulator/config bits. Each retained bit pattern is covered. */
    for (unsigned config=0;config<64;++config) {
        unsigned shift=(config&7)+9-(config>>3);
        uint32_t window_mask=0xffffu<<shift;
        for (unsigned word=0;word<65536;++word) {
            check(word&31,config|((word&3)<<6),word&255,
                  (word<<shift)|(random32()&~window_mask));
            ++count;
        }
        for (unsigned base=0;base<256;++base) {
            for (unsigned bit=0;bit<32;++bit) {
                check(bit,config,base,1u<<bit);++count;
                check(bit,config|0xc0,base,~(1u<<bit));++count;
            }
        }
    }
    for (unsigned n=0;n<1000000;++n) {unsigned c=random32(),b=random32();check(n&31,c&255,b&255,random32());++count;}
    puts("PASS reference, combined, select and table against independent bit oracle");
    printf("cases %llu\n",(unsigned long long)count);
    return 0;
}
