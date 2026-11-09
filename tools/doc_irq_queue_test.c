/* Exercise the DOC IRQ queue through oscillator sync and OIR reads. */
#ifdef NDEBUG
#error "IRQ queue validation requires assertions"
#endif
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "clem_device.h"
#include "clem_mmio_defs.h"
void clem_ensoniq_reset(struct ClemensDeviceEnsoniq *);
void clem_ensoniq_write_data(struct ClemensDeviceEnsoniq *,uint8_t);
uint8_t clem_ensoniq_read_data(struct ClemensDeviceEnsoniq *,uint8_t);
uint32_t clem_ensoniq_sync(struct ClemensDeviceEnsoniq *,clem_clocks_duration_t);
#ifndef DOC_IRQ_QUEUE_NO_MAIN
void clem_debug_log(int level,const char *fmt,...) {(void)level;(void)fmt;}
#endif
static struct ClemensDeviceEnsoniq doc;
static unsigned queue[32],length,events;
static void check(void){
 assert(length<=32);
 unsigned mask=0;
 for(unsigned i=0;i<length;i++){
  assert(doc.osc_stack[i]==(queue[i]|0x80));mask|=1u<<queue[i];
 }
 for(unsigned i=length;i<32;i++)assert(doc.osc_stack[i]==0);
 for(unsigned i=0;i<32;i++)assert(!!(doc.osc_flags[i]&CLEM_ENSONIQ_OSC_FLAG_OIR)==!!(mask&(1u<<i)));
 if(length){assert(!(doc.reg[0xe0]&0x80));assert(((doc.reg[0xe0]>>1)&31)==queue[0]);}
 else assert(doc.reg[0xe0]&0x80);
 events++;
}
static void emit(unsigned osc){
 doc.address=0xa0+osc;doc.is_access_ram=false;clem_ensoniq_write_data(&doc,0x0a);
 doc.ptr[osc]=0xffff;doc.acc[osc]=0;doc.cycle=osc;
 clem_ensoniq_sync(&doc,3200);
 unsigned i=0;while(i<length&&queue[i]!=osc)i++;
 if(i==length){assert(length<32);queue[length++]=osc;}
 check();
}
static void ack(void){
 doc.address=0xe0;clem_ensoniq_read_data(&doc,0);
 if(length){length--;memmove(queue,queue+1,length*sizeof(*queue));}
 check();
}
void doc_irq_queue_check(void){
 for(unsigned depth=1;depth<=32;depth++){
  memset(&doc,0,sizeof(doc));clem_ensoniq_reset(&doc);length=0;
  memset(doc.sound_ram,1,sizeof(doc.sound_ram));doc.reg[0xe1]=62;
  for(unsigned i=0;i<32;i++)doc.reg[0xa0+i]=1;
  for(unsigned i=0;i<depth;i++)emit(i);
  /* Repeated IRQs remain unique, including while the queue is full. */
  for(unsigned i=0;i<depth;i++)emit(i);
  for(unsigned round=0;round<256;round++){
   unsigned osc=queue[0];ack();emit(osc);
  }
  while(length)ack();
  ack(); /* A read after draining must not resurrect stale entries. */
 }
 printf("IRQ QUEUE PASS depths=32 checked_events=%u\n",events);
}
#ifndef DOC_IRQ_QUEUE_NO_MAIN
int main(void){doc_irq_queue_check();return 0;}
#endif
