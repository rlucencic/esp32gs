/* Complete-module local oscillator-helper ABBA timings; production is unchanged. */
#ifdef NDEBUG
#error "Scheduler benchmark requires assertions"
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "clem_device.h"
#include "esp_cpu.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#define DECL(P) \
void P##clem_ensoniq_reset(struct ClemensDeviceEnsoniq *); \
uint32_t P##clem_ensoniq_sync(struct ClemensDeviceEnsoniq *,clem_clocks_duration_t);
DECL(ref_) DECL(candidate_)
void doc_irq_queue_check(void);
void clem_debug_log(int level,const char *fmt,...) {(void)level;(void)fmt;}
static void (*const reset[])(struct ClemensDeviceEnsoniq*)={ref_clem_ensoniq_reset,candidate_clem_ensoniq_reset};
static uint32_t (*const sync_doc[])(struct ClemensDeviceEnsoniq*,clem_clocks_duration_t)={ref_clem_ensoniq_sync,candidate_clem_ensoniq_sync};
static void setup(struct ClemensDeviceEnsoniq *doc,unsigned scene,unsigned variant){
 memset(doc,0,sizeof(*doc));reset[variant](doc);
 unsigned count=scene==5?1:scene==6?8:32;
 doc->reg[0xe1]=2*(count-1);doc->cycle=UINT32_MAX-511;
 for(unsigned i=0;i<65536;i++)doc->sound_ram[i]=(i%255)+1;
 unsigned active=scene==0?0:scene==1?1:scene==2?8:count;
 for(unsigned i=0;i<32;i++){
  doc->reg[i]=73+i;doc->reg[0x20+i]=19;doc->reg[0x40+i]=64;
  doc->reg[0xc0+i]=0x2d;doc->acc[i]=0xffff00+i*13;
  doc->reg[0xa0+i]=(i<active?0:1);
  if(scene==0)doc->reg[0xa0+i]|=2;
  if(scene==4)doc->reg[0xa0+i]=6|8|((i&1)?1:0);
  if(scene==6)doc->reg[0xa0+i]|=4;
 }
}
static uint32_t timed(struct ClemensDeviceEnsoniq *doc,unsigned variant,unsigned dt){
 uint32_t start=esp_cpu_get_cycle_count();
 for(unsigned n=0;n<512;n++)sync_doc[variant](doc,dt);
 return esp_cpu_get_cycle_count()-start;
}
static uint32_t hash(const void *data,size_t len){
 const uint8_t *p=data;uint32_t h=2166136261u;
 for(size_t n=0;n<len;n++)h=(h^p[n])*16777619u;
 return h;
}
static void task(void *unused){
 vTaskDelay(pdMS_TO_TICKS(3000));puts("INLINE BEGIN");doc_irq_queue_check();
 const unsigned deltas[]={3200,32000,182400};
 for(unsigned mem=0;mem<2;mem++){
  struct ClemensDeviceEnsoniq *doc=heap_caps_malloc(sizeof(*doc),(mem?MALLOC_CAP_SPIRAM:MALLOC_CAP_INTERNAL)|MALLOC_CAP_8BIT);assert(doc);
  for(unsigned scene=0;scene<7;scene++)for(unsigned dt=0;dt<3;dt++)for(unsigned candidate=1;candidate<2;candidate++){
   for(unsigned v=0;v<2;v++){setup(doc,scene,v);timed(doc,v,deltas[dt]);}
   for(unsigned round=0;round<9;round++){
    unsigned order[]={0,candidate,candidate,0};uint32_t times[4],sums[4];
    for(unsigned k=0;k<4;k++){setup(doc,scene,order[k]);times[k]=timed(doc,order[k],deltas[dt]);sums[k]=hash(doc,sizeof(*doc));}
    assert(sums[0]==sums[1]&&sums[0]==sums[2]&&sums[0]==sums[3]);
    printf("BENCH %u %u %u %u %u %lu %lu %lu %lu %lu\n",mem,scene,dt,candidate,round,(unsigned long)times[0],(unsigned long)times[1],(unsigned long)times[2],(unsigned long)times[3],(unsigned long)sums[0]);
    vTaskDelay(1);
   }
  }
  free(doc);
 }
 puts("BENCH END");vTaskDelete(NULL);
}
void app_main(void){xTaskCreatePinnedToCore(task,"schedule-bench",8192,NULL,10,NULL,1);}
