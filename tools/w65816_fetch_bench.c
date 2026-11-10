/* Appended to the retained test/speed.c assembler by the project generator. */
#ifdef NDEBUG
#error "CPU benchmark requires assertions"
#endif
#include <assert.h>
#include "esp_cpu.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "emudbg.h"
#define DECL(P) void P##_w65816_reset(w65816_t*);int P##_w65816_run(w65816_t*,int);
DECL(reference) DECL(pair) DECL(split) DECL(combined)
static void (*const resets[])(w65816_t*)={reference_w65816_reset,pair_w65816_reset,split_w65816_reset,combined_w65816_reset};
static int (*const runs[])(w65816_t*,int)={reference_w65816_run,pair_w65816_run,split_w65816_run,combined_w65816_run};
static w65816_t cpu;
static uint32_t bus_hash;
static uint8_t debug_map[8192];
void emudbg_hit(emudbg_cpu_t *unused,uint32_t pc){assert(0);}
static uint32_t hash(const void *data,size_t len) {
    const uint8_t *p=data;uint32_t h=2166136261u;
    for(size_t i=0;i<len;i++)h=(h^p[i])*16777619u;
    return h;
}
static uint8_t read_bus(void *unused,uint32_t a) {
    uint8_t value=a<BANKS*65536u?s_mem[a]:0xff;
    bus_hash=(bus_hash^a^(uint32_t)cpu.cycles^value)*16777619u;return value;
}
static void write_bus(void *unused,uint32_t a,uint8_t value) {
    bus_hash=(bus_hash^a^(uint32_t)cpu.cycles^value^0x80000000u)*16777619u;
    if(a<BANKS*65536u)s_mem[a]=value;
}
static void setup(unsigned scene,unsigned variant) {
    memset(s_mem,0,BANKS*65536u);memset(s_pages,0,65536*sizeof(*s_pages));
    for(unsigned i=0;i<BANKS*256;i++)s_pages[i]=s_mem+i*256;
    s_at=0x1000;assemble();
    if(scene==1) {s_at=0x1000;b(3,0xa9,W(0x1234));b(3,0x49,W(0x5678));b(3,0x69,W(1));b(3,0x29,W(0x7fff));b(3,0xe9,W(1));b(3,0x4c,W(0x1000));}
    if(scene==2) {s_at=0x1000;b(2,0xa9,0x34);b(2,0x49,0x78);b(2,0x69,1);b(2,0x29,0x7f);b(2,0xe9,1);b(3,0x4c,W(0x1000));}
    if(scene==3) {s_at=0x1000;b(1,0xea);b(1,0xea);b(1,0xea);b(2,0x80,0xfb);}
    unsigned pc=0x1000;
    if(scene==4) {pc=s_at=0x10fe;b(3,0xa9,W(0x1234));b(3,0x49,W(0x5678));b(3,0x4c,W(pc));}
    if(scene==5)for(unsigned i=0;i<BANKS*256;i++)s_pages[i]=NULL;
    if(scene==6)s_pages[0x40]=NULL;
    s_mem[0xfffc]=pc;s_mem[0xfffd]=pc>>8;
    memset(&cpu,0,sizeof(cpu));cpu.rd=cpu.wr=s_pages;cpu.bus=(w65816_bus_t){.read=read_bus,.write=write_bus};
    bus_hash=2166136261u;resets[variant](&cpu);cpu.s=0x1ff;
    if(scene>=1&&scene<=4){cpu.e=false;cpu.p=scene==2?W65816_M|W65816_X:0;}
    /* Normal execution has no active debugger map; scene 7 is a control. */
    cpu.dbg_map=scene==7?debug_map:NULL;
    for(unsigned i=0;i<16;i++)runs[variant](&cpu,180);
}
static uint32_t checksum(void) {
    uint64_t state[]={cpu.a,cpu.x,cpu.y,cpu.s,cpu.d,cpu.pc,cpu.pbr,cpu.dbr,cpu.p,cpu.e,cpu.stopped,cpu.waiting,cpu.irq,cpu.nmi,(uint64_t)cpu.penalty,cpu.cycles,cpu.run_base,cpu.run_budget,cpu.run_held,cpu.end_run,cpu.code_pc,bus_hash};
    assert(!cpu.stopped&&!cpu.waiting);
    return hash(state,sizeof(state))^hash(s_mem,BANKS*65536u);
}
static uint32_t timed(unsigned variant,unsigned budget) {
    uint32_t begin=esp_cpu_get_cycle_count();
    for(unsigned i=0;i<128;i++)runs[variant](&cpu,budget);
    return esp_cpu_get_cycle_count()-begin;
}
static void task(void *unused) {
    vTaskDelay(pdMS_TO_TICKS(3000));puts("FETCH BENCH BEGIN");
    const unsigned budgets[]={180,1000};
    s_pages=heap_caps_calloc(65536,sizeof(*s_pages),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);assert(s_pages);
    for(unsigned mem=0;mem<2;mem++) {
        s_mem=heap_caps_malloc(BANKS*65536u,(mem?MALLOC_CAP_SPIRAM:MALLOC_CAP_INTERNAL)|MALLOC_CAP_8BIT);assert(s_mem);
        for(unsigned scene=0;scene<8;scene++)for(unsigned budget=0;budget<2;budget++)for(unsigned candidate=1;candidate<4;candidate++) {
            for(unsigned v=0;v<4;v++){setup(scene,v);timed(v,budgets[budget]);}
            for(unsigned round=0;round<7;round++) {
                unsigned order[]={0,candidate,candidate,0};uint32_t cycles[4],sums[4];
                for(unsigned i=0;i<4;i++){setup(scene,order[i]);cycles[i]=timed(order[i],budgets[budget]);sums[i]=checksum();}
                assert(sums[0]==sums[1]&&sums[0]==sums[2]&&sums[0]==sums[3]);
                printf("BENCH %u %u %u %u %u %lu %lu %lu %lu %lu\n",mem,scene,budget,candidate,round,(unsigned long)cycles[0],(unsigned long)cycles[1],(unsigned long)cycles[2],(unsigned long)cycles[3],(unsigned long)sums[0]);
                vTaskDelay(1);
            }
        }
        free(s_mem);
    }
    free(s_pages);puts("BENCH END");vTaskDelete(NULL);
}
void app_main(void){xTaskCreatePinnedToCore(task,"fetch-bench",8192,NULL,10,NULL,1);}
