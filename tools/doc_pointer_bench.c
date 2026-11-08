/* Isolated IDF P4 benchmark; generate reference/variant units with
 * doc_pointer_test.py. See docs/doc-pointer-experiment-2026-11-08.md.
 */
#ifdef NDEBUG
#error "Benchmark checksums require assertions"
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "clem_mmio_types.h"
#include "esp_cpu.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#define DECL(N) uint32_t run_##N(struct ClemensDeviceEnsoniq*,unsigned);
DECL(reference) DECL(combined) DECL(select) DECL(table)
static uint32_t (*const run[])(struct ClemensDeviceEnsoniq*,unsigned)={run_reference,run_combined,run_select,run_table};
static uint32_t seed=0x739a951;
static uint32_t rnd(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static uint32_t timed(struct ClemensDeviceEnsoniq *d,int which,uint32_t *sum){uint32_t start=esp_cpu_get_cycle_count();*sum=run[which](d,512);return esp_cpu_get_cycle_count()-start;}
static void setup(struct ClemensDeviceEnsoniq *d,int scene){
 seed=0x739a951;
 for(unsigned i=0;i<32;++i){d->reg[0xc0+i]=scene==0?0:scene==1?i*2:scene==2?63:rnd();d->reg[0x80+i]=scene==0?0:rnd();d->acc[i]=scene==2?0xffff00+i:rnd()&0xffffff;}
}
static void task(void *unused){
 vTaskDelay(pdMS_TO_TICKS(3000));puts("BENCH BEGIN");
 for(int mem=0;mem<2;++mem){
  struct ClemensDeviceEnsoniq *d=heap_caps_calloc(1,sizeof(*d),(mem?MALLOC_CAP_SPIRAM:MALLOC_CAP_INTERNAL)|MALLOC_CAP_8BIT);assert(d);
  for(int scene=0;scene<4;++scene)for(int candidate=1;candidate<4;++candidate){
   for(int warm=0;warm<4;++warm){uint32_t sum;setup(d,scene);timed(d,warm,&sum);}
   for(int round=0;round<9;++round){uint32_t cycles[4],sums[4];int order[]={0,candidate,candidate,0};
    for(int k=0;k<4;++k){setup(d,scene);cycles[k]=timed(d,order[k],&sums[k]);}
    assert(sums[0]==sums[1]&&sums[0]==sums[2]&&sums[0]==sums[3]);
    printf("BENCH %d %d %d %d %lu %lu %lu %lu %lu\n",mem,scene,candidate,round,(unsigned long)cycles[0],(unsigned long)cycles[1],(unsigned long)cycles[2],(unsigned long)cycles[3],(unsigned long)sums[0]);vTaskDelay(1);
   }
  }free(d);
 }puts("BENCH END");vTaskDelete(NULL);
}
void app_main(void){xTaskCreatePinnedToCore(task,"bench",8192,NULL,10,NULL,1);}
