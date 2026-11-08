/* Isolated P4 exact-bit validation and full voice-mixer ABBA benchmark. */
#ifdef NDEBUG
#error "Normalization benchmark requires assertions"
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "doc_normalize_check.h"
#include "esp_cpu.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
void clem_debug_log(int level,const char *fmt,...) {(void)level;(void)fmt;}
static uint32_t (*const run[])(struct ClemensDeviceEnsoniq*,unsigned)={normalize_run_reference,normalize_run_unit,normalize_run_dual};
static uint32_t seed;
static uint32_t rnd(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static void setup(struct ClemensDeviceEnsoniq *doc,unsigned scene)
{
    seed=0x739af195;doc->reg[0xe1]=62;
    for(unsigned i=0;i<32;++i){
        unsigned active=scene==0?0:scene==1?1:scene==2?8:32;
        unsigned ctl=((i%(scene==5?8:2))<<4)|(i<active?0:1);
        if(scene==4&&(i&1))ctl|=4;
        doc->reg[0xa0+i]=ctl;doc->reg[0x60+i]=(scene==5&&i%4==0)?0:rnd();doc->reg[0x40+i]=rnd();
    }
    memset(doc->voice,0,sizeof(doc->voice));
}
static uint32_t timed(struct ClemensDeviceEnsoniq *doc,unsigned variant,uint32_t *sum)
{
    uint32_t start=esp_cpu_get_cycle_count();*sum=run[variant](doc,512);return esp_cpu_get_cycle_count()-start;
}
static void task(void *unused)
{
    vTaskDelay(pdMS_TO_TICKS(3000));puts("NORMALIZE BEGIN");
    doc_normalize_check();
    puts("BENCH BEGIN");
    for(unsigned mem=0;mem<2;++mem){
        struct ClemensDeviceEnsoniq *doc=heap_caps_calloc(1,sizeof(*doc),(mem?MALLOC_CAP_SPIRAM:MALLOC_CAP_INTERNAL)|MALLOC_CAP_8BIT);assert(doc);
        for(unsigned scene=0;scene<6;++scene)for(unsigned candidate=1;candidate<3;++candidate){
            for(unsigned variant=0;variant<3;++variant){uint32_t sum;setup(doc,scene);timed(doc,variant,&sum);}
            for(unsigned round=0;round<9;++round){
                uint32_t cycles[4],sums[4];unsigned order[]={0,candidate,candidate,0};
                for(unsigned k=0;k<4;++k){setup(doc,scene);cycles[k]=timed(doc,order[k],&sums[k]);}
                assert(sums[0]==sums[1]&&sums[0]==sums[2]&&sums[0]==sums[3]);
                printf("BENCH %u %u %u %u %lu %lu %lu %lu %lu\n",mem,scene,candidate,round,(unsigned long)cycles[0],(unsigned long)cycles[1],(unsigned long)cycles[2],(unsigned long)cycles[3],(unsigned long)sums[0]);
                vTaskDelay(1);
            }
        }
        free(doc);
    }
    puts("BENCH END");vTaskDelete(NULL);
}
void app_main(void){xTaskCreatePinnedToCore(task,"normalize-bench",8192,NULL,10,NULL,1);}
