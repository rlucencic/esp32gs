/* Exhaustive sample/volume pairs through the actual mixing functions. */
#ifdef NDEBUG
#error "Normalization tests require assertions"
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "doc_normalize_check.h"
_Static_assert(sizeof(float)==4,"Checks require binary32 float storage");
static struct ClemensDeviceEnsoniq a,b,c;
static uint32_t bits(float value) {uint32_t out;memcpy(&out,&value,4);return out;}
void doc_normalize_check(void)
{
    unsigned long count=0;
    for(unsigned scene=0;scene<8;++scene)for(unsigned data=0;data<256;++data)for(unsigned volume=0;volume<256;++volume){
        unsigned n=scene==0?1:scene==6?2:32;
        a.reg[0xe1]=2*(n-1);
        for(unsigned i=0;i<32;++i){
            unsigned d=data,v=volume,ctl=(i&1)<<4;
            if(scene==2||scene==7){d=(data+i*17)&255;v=(volume+i*11)&255;}
            if(scene==3){if(i&1){ctl|=4;d=volume;}}
            if(scene==4){if(i%4==0)ctl|=1;if(i%4==1)d=0;}
            if(scene==5)ctl=(i%16)<<4;
            if(scene==6){ctl=0;if(i==1){d=255-data;v=255-volume;}}
            if(scene==7)ctl|=(i&3)*2;
            a.reg[0x40+i]=v;a.reg[0x60+i]=d;a.reg[0xa0+i]=ctl;
        }
        for(unsigned i=0;i<16;++i)a.voice[i]=(int)i*0.125f;
        /* Mixer only reads registers and writes voice output. Other DOC fields
           stay zero in these static globals; full-state checks are separate. */
        memcpy(b.reg,a.reg,256);memcpy(c.reg,a.reg,256);
        memcpy(b.voice,a.voice,sizeof(a.voice));memcpy(c.voice,a.voice,sizeof(a.voice));
        unsigned av=ref_clem_ensoniq_voices(&a),bv=unit_clem_ensoniq_voices(&b),cv=dual_clem_ensoniq_voices(&c);
        assert(av==bv&&av==cv);
        for(unsigned i=0;i<16;++i){
            if(bits(a.voice[i])!=bits(b.voice[i])||bits(a.voice[i])!=bits(c.voice[i])){
                printf("MISMATCH %u %u %u %u %08lx %08lx %08lx\n",scene,data,volume,i,(unsigned long)bits(a.voice[i]),(unsigned long)bits(b.voice[i]),(unsigned long)bits(c.voice[i]));
                assert(0);
            }
        }
        float al,ar,bl,br,cl,cr;
        ref_clem_ensoniq_mono(&a,av,&al,&ar);unit_clem_ensoniq_mono(&b,bv,&bl,&br);dual_clem_ensoniq_mono(&c,cv,&cl,&cr);
        assert(bits(al)==bits(bl)&&bits(al)==bits(cl)&&bits(ar)==bits(br)&&bits(ar)==bits(cr));
        ++count;
    }
    printf("NORMALIZE CHECK PASS %lu cases per variant\n",count);
}
