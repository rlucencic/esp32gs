/* Differential state/IRQ/mixing test for whole audio modules with one helper changed. */
#ifdef NDEBUG
#error "Audio differential tests require assertions"
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "clem_device.h"
#define DECL(P) \
void P##clem_ensoniq_reset(struct ClemensDeviceEnsoniq *); \
void P##clem_ensoniq_write_data(struct ClemensDeviceEnsoniq *,uint8_t); \
uint8_t P##clem_ensoniq_read_data(struct ClemensDeviceEnsoniq *,uint8_t); \
uint32_t P##clem_ensoniq_sync(struct ClemensDeviceEnsoniq *,clem_clocks_duration_t); \
unsigned P##clem_ensoniq_voices(struct ClemensDeviceEnsoniq *); \
void P##clem_ensoniq_mono(struct ClemensDeviceEnsoniq *,unsigned,float *,float *);
DECL(ref_) DECL(candidate_)
void clem_debug_log(int level,const char *fmt,...) {(void)level;(void)fmt;}
static struct ClemensDeviceEnsoniq a,b;
static uint32_t seed=0x937fac81;
static uint32_t rnd(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static void same(void){assert(!memcmp(&a,&b,sizeof(a)));}
static void write_reg(unsigned reg,uint8_t value){
 a.address=b.address=reg;a.is_access_ram=b.is_access_ram=false;
 ref_clem_ensoniq_write_data(&a,value);candidate_clem_ensoniq_write_data(&b,value);same();
}
int main(void){
 const unsigned counts[]={1,2,3,8,16,31,32};unsigned events=0,irq_events=0,halt_events=0;
 for(unsigned mode=0;mode<4;++mode)for(unsigned count=0;count<7;++count)for(unsigned pattern=0;pattern<8;++pattern){
  memset(&a,0,sizeof(a));memset(&b,0,sizeof(b));ref_clem_ensoniq_reset(&a);candidate_clem_ensoniq_reset(&b);same();
  for(unsigned i=0;i<65536;++i)a.sound_ram[i]=b.sound_ram[i]=(pattern&1)&&i%127==0?0:(rnd()%255)+1;
  write_reg(0xe1,2*(counts[count]-1));
  for(unsigned i=0;i<32;++i){
   write_reg(0xa0+i,1);write_reg(i,rnd());write_reg(0x20+i,pattern<4?0xff:rnd());write_reg(0x40+i,rnd());write_reg(0x80+i,rnd());write_reg(0xc0+i,rnd());
   a.acc[i]=b.acc[i]=pattern<4?0xffff00+i:rnd()&0xffffff;
   write_reg(0xa0+i,((i&1)<<4)|(mode<<1)|8);
  }
  for(unsigned n=0;n<256;++n){
   unsigned i=rnd()%counts[count];
   /* Real register writes exercise reconfiguration and HALT->run pointer refresh. */
   switch(n%16){
    case 0:write_reg(0xa0+i,1|((mode<<1)&6)|8);break;
    case 1:write_reg(0xc0+i,rnd());break;
    case 2:write_reg(0x80+i,rnd());break;
    case 3:write_reg(0xa0+i,((i&1)<<4)|(mode<<1)|8);break;
    case 4:write_reg(i,rnd());break;
    case 5:write_reg(0x20+i,rnd());break;
    case 6:write_reg(0x40+i,rnd());break;
   }
   clem_clocks_duration_t dt=(rnd()%16384)+1;
   uint32_t ar=ref_clem_ensoniq_sync(&a,dt),br=candidate_clem_ensoniq_sync(&b,dt);assert(ar==br);same();irq_events+=(ar!=0);
   unsigned av=ref_clem_ensoniq_voices(&a),bv=candidate_clem_ensoniq_voices(&b);assert(av==bv);same();
   float al,aright,bl,bright;ref_clem_ensoniq_mono(&a,av,&al,&aright);candidate_clem_ensoniq_mono(&b,bv,&bl,&bright);assert(!memcmp(&al,&bl,sizeof(float))&&!memcmp(&aright,&bright,sizeof(float)));same();
   if(n%3==0){a.address=b.address=0xe0;assert(ref_clem_ensoniq_read_data(&a,0)==candidate_clem_ensoniq_read_data(&b,0));same();}
   for(unsigned j=0;j<counts[count];++j)halt_events+=(a.reg[0xa0+j]&1)!=0;
   ++events;
  }
 }
 assert(irq_events&&halt_events);
 printf("PASS states=224 sync_mix_events=%u irq_events=%u halted_observations=%u\n",events,irq_events,halt_events);
 return 0;
}
