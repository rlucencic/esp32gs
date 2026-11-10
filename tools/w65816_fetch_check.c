/* Frozen-core differential checks: CPU, full RAM and ordered callback traces. */
#ifdef NDEBUG
#error "Fetch differential checks require assertions"
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "w65816.h"
#include "emudbg.h"
#define DECL(P) void P##_w65816_reset(w65816_t*);int P##_w65816_run(w65816_t*,int);
DECL(reference) DECL(pair) DECL(split) DECL(combined)
#define MEM 131072u
#define EVENTS 4096
struct event {uint32_t addr;uint64_t cycle;uint8_t value,write;};
struct machine {
    w65816_t cpu;
    uint8_t mem[MEM],alt[MEM];
    uint8_t *pages[65536];
    struct event trace[EVENTS];
    unsigned events,switched,action,mapmode;
    int (*run)(w65816_t*,int);
};
static struct machine a,b;
static uint8_t image[MEM],alternative[MEM];
static unsigned tests;
#if EMUDBG_ENABLED
static uint8_t debug_map[8192];
void emudbg_hit(emudbg_cpu_t *opaque,uint32_t pc) {
    struct machine *m=(void*)opaque;assert(m->events<EVENTS);
    m->trace[m->events++]=(struct event){0x2000000|pc,m->cpu.cycles,(uint8_t)m->cpu.a,0};
    m->cpu.x^=1; /* Debugger register edits must be reloaded. */
}
#endif
static uint32_t random_state=0x239146ab;
static uint32_t rnd(void){random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return random_state;}
static uint8_t *ram(struct machine *m,uint32_t addr) {
    return (m->switched&(1u<<(addr>>16))?m->alt:m->mem)+addr;
}
static void map(struct machine *m) {
    for(unsigned page=0;page<MEM/256;page++) {
        bool io=m->mapmode==3 || (m->mapmode==2 && !(page&1));
        m->pages[page]=io?NULL:ram(m,page<<8);
    }
}
static void record(struct machine *m,uint32_t addr,uint8_t v,bool write) {
    assert(m->events<EVENTS);
    m->trace[m->events++]=(struct event){addr,m->cpu.cycles,v,write};
    m->cpu.penalty+=(addr&1)+((m->action>>3)&3);
    if(m->action&1) {m->switched^=3;map(m);}
    if(m->action&2) w65816_set_irq(&m->cpu,true);
    if((m->action&4) && m->events==2)w65816_nmi(&m->cpu);
    if((m->action&32) && m->events==1)w65816_end_run(&m->cpu);
}
static uint8_t read_bus(void *ctx,uint32_t addr) {
    struct machine *m=ctx;
    uint8_t v=addr<MEM?*ram(m,addr):(addr^(addr>>8));record(m,addr,v,false);return v;
}
static void write_bus(void *ctx,uint32_t addr,uint8_t v) {
    struct machine *m=ctx;if(addr<MEM)*ram(m,addr)=v;record(m,addr,v,true);
}
static void trap(void *ctx,w65816_t *cpu,uint8_t op) {
    struct machine *m=ctx;record(m,0x1000000|op,(uint8_t)cpu->a,true);
    cpu->a^=0x1234;cpu->cycles+=3;
}
static void setup(struct machine *m,unsigned opcode,unsigned mode,unsigned pc,unsigned mapping,unsigned action) {
    memcpy(m->mem,image,MEM);memcpy(m->alt,alternative,MEM);
    m->events=m->switched=0;m->action=action;m->mapmode=mapping;
    memset(&m->cpu,0,sizeof(m->cpu));m->cpu.rd=m->cpu.wr=m->pages;
    m->cpu.bus=(w65816_bus_t){read_bus,write_bus,trap,m};map(m);
    /* All bytes used across FFFF wrap belong to the same program bank. */
    unsigned bank=(pc>>16)&1;
    uint8_t code[]={opcode,0x34,0x12,0x01};
    for(unsigned i=0;i<4;i++) {
        unsigned addr=(bank<<16)|(uint16_t)(pc+i);
        m->mem[addr]=m->alt[addr]=code[i];
    }
    if(mapping==1)m->pages[(pc>>8)]=NULL;
    m->cpu.pc=(uint16_t)pc;m->cpu.pbr=bank;
    m->cpu.p=mode==4?0x34:((mode&1?W65816_M:0)|(mode&2?W65816_X:0));m->cpu.e=mode==4;
    m->cpu.a=0x28;m->cpu.x=7;m->cpu.y=9;m->cpu.d=0x2000;m->cpu.dbr=1;
    #if EMUDBG_ENABLED
    m->cpu.dbg_map=(action&16)?debug_map:NULL;m->cpu.dbg=m;
#endif
    m->cpu.s=mode==4?0x1ff:0x2ff;m->cpu.cycles=0xfffffff0u;
}
#define CPU_FIELDS(X) X(a) X(x) X(y) X(s) X(d) X(pc) X(pbr) X(dbr) X(p) X(e) \
 X(stopped) X(waiting) X(irq) X(nmi) X(penalty) X(vector_bank) X(cycles) \
 X(run_base) X(run_budget) X(run_held) X(end_run) X(code_pc)
static void same(int ra,int rb) {
    assert(ra==rb);assert(a.events==b.events);assert(a.switched==b.switched);
#define CHECK(f) assert(a.cpu.f==b.cpu.f);
    CPU_FIELDS(CHECK)
#undef CHECK
    for(unsigned i=0;i<a.events;i++) {
        assert(a.trace[i].addr==b.trace[i].addr);assert(a.trace[i].cycle==b.trace[i].cycle);
        assert(a.trace[i].value==b.trace[i].value);assert(a.trace[i].write==b.trace[i].write);
    }
    assert(!memcmp(a.mem,b.mem,MEM));assert(!memcmp(a.alt,b.alt,MEM));++tests;
}
int main(int argc,char **argv) {
    unsigned seeds=argc>1?(unsigned)atoi(argv[1]):256;
#if EMUDBG_ENABLED
    memset(debug_map,0xff,sizeof(debug_map));
#endif
    for(unsigned i=0;i<MEM;i++){image[i]=(uint8_t)rnd();alternative[i]=image[i]^0x5a;}
    int (*const runs[])(w65816_t*,int)={pair_w65816_run,split_w65816_run,combined_w65816_run};
    const char *names[]={"pair","split","combined"};
    const unsigned positions[]={0x1000,0x10fc,0x10fd,0x10fe,0x10ff,0x1fffd,0x1fffe,0x1ffff};
    for(unsigned v=0;v<3;v++) {
        unsigned before=tests;
        for(unsigned op=0;op<256;op++)for(unsigned mode=0;mode<5;mode++)
        for(unsigned pos=0;pos<8;pos++)for(unsigned mapping=0;mapping<4;mapping++) {
            unsigned action=(op+pos+mode)%64;
            setup(&a,op,mode,positions[pos],mapping,action);setup(&b,op,mode,positions[pos],mapping,action);
            same(reference_w65816_run(&a.cpu,1),runs[v](&b.cpu,1));
        }
        unsigned boundary=tests-before;
        for(unsigned seed=0;seed<seeds;seed++) {
            unsigned op=rnd()%256,mode=rnd()%5,pc=0x1000+(rnd()%0x10000),mapping=rnd()%4,action=rnd()%64;
            setup(&a,op,mode,pc,mapping,action);setup(&b,op,mode,pc,mapping,action);
            for(unsigned slice=0;slice<32;slice++) {
                int budget=1+rnd()%400;a.events=b.events=0;
                if(slice%5==0){bool irq=rnd()&1;a.cpu.irq=b.cpu.irq=irq;}
                if(slice%7==0){a.cpu.nmi=b.cpu.nmi=true;}
                same(reference_w65816_run(&a.cpu,budget),runs[v](&b.cpu,budget));
                if(a.cpu.stopped)break;
            }
        }
        printf("FETCH CHECK %s boundary=%u total_slices=%u seeds=%u PASS\n",names[v],boundary,tests-before,seeds);fflush(stdout);
    }
    printf("FETCH CHECK ALL comparisons=%u PASS\n",tests);
}
