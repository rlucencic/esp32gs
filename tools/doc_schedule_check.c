/* Compare a frozen cycle-at-a-time scheduler with experimental complete modules. */
#ifdef NDEBUG
#error "Scheduler validation requires assertions"
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
unsigned P##clem_ensoniq_voices(struct ClemensDeviceEnsoniq *);
DECL(ref_) DECL(candidate_)
void clem_debug_log(int level,const char *fmt,...) {(void)level;(void)fmt;}
static struct ClemensDeviceEnsoniq a,b;
static uint32_t seed=0x81f734a5;
static uint32_t rnd(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static void same(void){assert(!memcmp(&a,&b,sizeof(a)));}
static void write_reg(unsigned reg,uint8_t value){
 a.address=b.address=reg;a.is_access_ram=b.is_access_ram=false;
 ref_clem_ensoniq_write_data(&a,value);candidate_clem_ensoniq_write_data(&b,value);same();
}
static void compare(uint32_t dt){
 assert(ref_clem_ensoniq_sync(&a,dt)==candidate_clem_ensoniq_sync(&b,dt));same();
 assert(ref_clem_ensoniq_voices(&a)==candidate_clem_ensoniq_voices(&b));same();
 a.address=b.address=0xe0;
 assert(ref_clem_ensoniq_read_data(&a,0)==candidate_clem_ensoniq_read_data(&b,0));same();
}
int main(void){
 const unsigned slots[]={0,1,2,3,31,32,33,34,35,67,127,2048};
 unsigned cases=0,calls=0;
 for(unsigned count=1;count<=32;count++)for(unsigned pattern=0;pattern<8;pattern++)
 for(unsigned start=0;start<8;start++)for(unsigned length=0;length<sizeof(slots)/sizeof(*slots);length++){
  memset(&a,0,sizeof(a));memset(&b,0,sizeof(b));ref_clem_ensoniq_reset(&a);candidate_clem_ensoniq_reset(&b);same();
  for(unsigned i=0;i<65536;i++)a.sound_ram[i]=b.sound_ram[i]=(pattern&1)&&i%97==0?0:(i%255)+1;
  write_reg(0xe1,2*(count-1)+(pattern&1));
  for(unsigned i=0;i<32;i++){
   write_reg(0xa0+i,1);write_reg(i,rnd());write_reg(0x20+i,0xff);write_reg(0x40+i,rnd());
   write_reg(0x80+i,rnd());write_reg(0xc0+i,rnd());
   a.acc[i]=b.acc[i]=0xffff00+(i*7);a.ptr[i]=b.ptr[i]=0xffff;
   unsigned ctl=((i&1)<<4)|((pattern&3)<<1)|8;
   if(pattern==4 || (pattern==5&&(i&1)) || (pattern==6&&i!=count-1))ctl|=1;
   write_reg(0xa0+i,ctl);
  }
  unsigned cycle[]={0,1,count-1,count,count+1,count+2,UINT32_MAX-1,UINT32_MAX};
  a.cycle=b.cycle=cycle[start];
  a.dt_budget=b.dt_budget=(pattern==7?UINT32_MAX-1599:((pattern*457)%3200));
  compare(slots[length]*3200+(start&1?3199:0));calls++;
  /* Subsequent calls cover padding boundaries, quiet caching and IRQ reads. */
  for(unsigned n=0;n<8;n++){compare((rnd()%96)*3200+(rnd()%3200));calls++;}
  /* A later real control write must invalidate quiet state and resume exactly. */
  write_reg(0xa0+(count-1),8);compare(35*3200);calls++;
  cases++;
 }
 printf("SCHEDULE PASS cases=%u sync_mix_irq_calls=%u\n",cases,calls);
 return 0;
}
