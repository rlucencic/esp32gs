/* Original fixture validation against the unmodified host core.
 * Link with components/apple2gs/host/build objects (see the baseline report).
 * Usage: profile_host fixture.bin voices animated mode output-prefix
 * Run from the project root, with its private ROMs in roms/.
 */
#ifdef NDEBUG
#error "Fixture validation requires assertions enabled"
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gs_internal.h"
#include "clem_mmio_defs.h"
#include "doc_snapshot.h"
int gs_host_no_iwm_skip, gs_host_no_iwm_lean;
unsigned long gs_host_iwm_cells[2];
static uint8_t *load(const char *path, size_t wanted) {
    FILE *f=fopen(path,"rb");assert(f);uint8_t *p=calloc(1,wanted);assert(p);
    assert(fread(p,1,wanted,f)==wanted);assert(fgetc(f)==EOF);fclose(f);return p;
}
static void save(const char *prefix,const char *suffix,const void *p,size_t n) {
    char path[1024];assert(snprintf(path,sizeof(path),"%s%s",prefix,suffix)<sizeof(path));
    FILE *f=fopen(path,"wb");assert(f);assert(fwrite(p,1,n,f)==n);fclose(f);
}
static void drain(void) {int16_t samples[4096*2];while(gs_audio_render(samples,4096)>0){} }
int main(int argc,char **argv) {
    assert(argc==6);int voices=atoi(argv[2]),animate=atoi(argv[3]),mode=atoi(argv[4]);
    assert(voices>=0&&voices<=32);assert(mode==320||mode==640);
    gs_config_t cfg={0};cfg.ram_kb=1024;
    cfg.rom=load("roms/gs_rom3.rom",262144);cfg.chargen=load("roms/chargen.rom",4096);
    cfg.ram=calloc(1,1024*1024);cfg.mega2=calloc(1,128*1024);cfg.frame=calloc(GS_SCREEN_WIDTH*GS_SCREEN_HEIGHT,2);
    assert(cfg.ram&&cfg.mega2&&cfg.frame);gs_host_clock=3870000000u;assert(gs_init(&cfg));gs_audio_enable(true);
    for(int n=0;n<360;n++){gs_run_frame();drain();}
    FILE *f=fopen(argv[1],"rb");assert(f);int b;unsigned size=0;
    while((b=fgetc(f))!=EOF){assert(size<0x1ff0);gs_bus_write(0x022000+size++,b);}fclose(f);
    gs.cpu.pbr=2;gs.cpu.pc=0x2000;gs.cpu.p|=4;gs.cpu.waiting=false;gs.cpu.stopped=false;
    for(int n=0;n<120;n++){gs_run_frame();drain();}
    assert(gs_bus_read(0x023ff0)==0xa5);
    const struct ClemensDeviceEnsoniq *doc=&gs.mmio->dev_audio.doc;
    assert(doc->reg[0xe1]==0x3e);
    for(int i=0;i<32;i++){
        assert(((doc->reg[0xa0+i]&1)==0)==(i<voices));
        if(doc->reg[0x40+i]!=4||doc->reg[0x80+i]!=0||doc->reg[0xc0+i]!=0)fprintf(stderr,"bad osc %d volume %u ptr %u size %u pc %02x:%04x\n",i,doc->reg[0x40+i],doc->reg[0x80+i],doc->reg[0xc0+i],gs.cpu.pbr,gs.cpu.pc);
        assert(doc->reg[0x40+i]==4&&doc->reg[0x80+i]==0&&doc->reg[0xc0+i]==0);
    }
    assert(gs.mmio->new_video_c029&0x80);
    for(int i=0;i<32;i++){assert(gs_bus_read(0x023f00+i)==(i<voices?(i&1)*16:1));assert(gs_bus_read(0x023f40+i)==4);}
    assert(gs_bus_read(0x023f20)==0x3e);
    for(int i=0;i<16;i++)assert(gs_bus_read(0x023f80+i)==1+i*2);
    for(int i=0;i<256;i++)assert(doc->sound_ram[i]==(i<128?1+2*i:255-2*(i-128)));
    for(int i=0;i<200;i++)assert(gs.mmio->e1_bank[0x9d00+i]==(mode==640?0x80:0));
    unsigned signature=gs_screen_signature();uint64_t nonzero=0,total=0;int peak=0;
    char path[1024];snprintf(path,sizeof(path),"%s.pcm",argv[5]);f=fopen(path,"wb");assert(f);
    for(int n=0;n<600;n++){
        gs_run_frame();int16_t pcm[4096*2];int count;
        while((count=gs_audio_render(pcm,4096))>0){
            assert(fwrite(pcm,4,count,f)==(size_t)count);total+=(uint64_t)count*2;
            for(int i=0;i<count*2;i++){int v=pcm[i];nonzero+=(v!=0);int a=v<0?-v:v;if(a>peak)peak=a;}
        }
    }
    fclose(f);
    for(int i=0;i<32;i++)assert(((doc->reg[0xa0+i]&1)==0)==(i<voices));
    assert(total>0);assert(voices?(nonzero>0&&peak<24000):(nonzero==0));
    assert((gs_screen_signature()!=signature)==(animate!=0));
    /* Pointer-free state for exact reference/candidate replay comparisons. */
    uint64_t state[] = {gs.cpu.a,gs.cpu.x,gs.cpu.y,gs.cpu.s,gs.cpu.d,gs.cpu.pc,
        gs.cpu.pbr,gs.cpu.dbr,gs.cpu.p,gs.cpu.e,gs.cpu.stopped,gs.cpu.waiting,
        gs.cpu.irq,gs.cpu.nmi,(uint64_t)gs.cpu.penalty,gs.cpu.vector_bank,gs.cpu.cycles};
    save(argv[5],".cpu",state,sizeof(state));
    uint8_t doc_bytes[DOC_STATE_BYTES];doc_snapshot(doc_bytes,doc);
    save(argv[5],".doc",doc_bytes,sizeof(doc_bytes));
    save(argv[5],".ram",cfg.ram,1024*1024);
    save(argv[5],".mega2",cfg.mega2,128*1024);
    save(argv[5],".vram",gs.mmio->e1_bank+0x2000,0x8000);
    save(argv[5],".rgb565",cfg.frame,GS_SCREEN_WIDTH*GS_SCREEN_HEIGHT*2);
    printf("FIXTURE PASS voices=%d animate=%d mode=%d bytes=%u frames=600 samples=%llu nonzero=%llu peak=%d signature=%u\n",
           voices,animate,mode,size,(unsigned long long)total,(unsigned long long)nonzero,peak,gs_screen_signature());
    return 0;
}
