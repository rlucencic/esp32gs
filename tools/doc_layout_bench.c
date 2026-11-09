/* DOC metadata-layout trial: complete modules, canonical-state ABBA checks. */
#ifdef NDEBUG
#error "Layout benchmark requires assertions"
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "doc_state.h"
#include "esp_cpu.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#define DECL(P) \
size_t P##size(void); \
void P##setup(void*,unsigned); \
void P##run(void*,unsigned); \
void P##snapshot(void*,uint8_t*);
DECL(ref_) DECL(candidate_)
void clem_debug_log(int level,const char *fmt,...) {(void)level;(void)fmt;}
static void (*const setup[])(void*,unsigned)={ref_setup,candidate_setup};
static void (*const run[])(void*,unsigned)={ref_run,candidate_run};
static void (*const snapshot[])(void*,uint8_t*)={ref_snapshot,candidate_snapshot};
static uint32_t timed(void *doc,unsigned variant,unsigned dt) {
    uint32_t start=esp_cpu_get_cycle_count();run[variant](doc,dt);
    return esp_cpu_get_cycle_count()-start;
}
static uint32_t hash(const uint8_t *p) {
    uint32_t h=2166136261u;for(unsigned n=0;n<DOC_STATE_BYTES;n++)h=(h^p[n])*16777619u;
    return h;
}
static void task(void *unused) {
    vTaskDelay(pdMS_TO_TICKS(3000));puts("LAYOUT BEGIN");
    assert(ref_size()==candidate_size());
    const unsigned deltas[]={3200,32000,182400};
    uint8_t *state=heap_caps_malloc(DOC_STATE_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);assert(state);
    for(unsigned mem=0;mem<2;mem++) {
        void *doc=heap_caps_malloc(ref_size(),(mem?MALLOC_CAP_SPIRAM:MALLOC_CAP_INTERNAL)|MALLOC_CAP_8BIT);assert(doc);
        for(unsigned scene=0;scene<7;scene++)for(unsigned dt=0;dt<3;dt++) {
            for(unsigned v=0;v<2;v++){setup[v](doc,scene);timed(doc,v,deltas[dt]);}
            for(unsigned round=0;round<9;round++) {
                unsigned order[]={0,1,1,0};uint32_t times[4],sums[4];
                for(unsigned k=0;k<4;k++) {
                    unsigned v=order[k];setup[v](doc,scene);times[k]=timed(doc,v,deltas[dt]);
                    snapshot[v](doc,state);sums[k]=hash(state);
                }
                assert(sums[0]==sums[1]&&sums[0]==sums[2]&&sums[0]==sums[3]);
                printf("BENCH %u %u %u 1 %u %lu %lu %lu %lu %lu\n",mem,scene,dt,round,(unsigned long)times[0],(unsigned long)times[1],(unsigned long)times[2],(unsigned long)times[3],(unsigned long)sums[0]);
                vTaskDelay(1);
            }
        }
        free(doc);
    }
    free(state);puts("BENCH END");vTaskDelete(NULL);
}
void app_main(void){xTaskCreatePinnedToCore(task,"layout-bench",8192,NULL,10,NULL,1);}
