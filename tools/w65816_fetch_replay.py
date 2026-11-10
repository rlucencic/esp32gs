#!/usr/bin/env python3
"""Replay full IIgs fixtures and private software with isolated CPU modules.

The original media is never mounted. CPU objects alone change between runners;
all use the retained machine core. Output, audio and logs stay outside repos.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import wave
from w65816_fetch_trial import ROOT,EMU


def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--generated',type=Path,required=True)
    p.add_argument('--fixtures',type=Path,required=True,help='Six outputs from profile_fixture.py')
    p.add_argument('--media',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--frames',type=int,default=21601)
    p.add_argument('--production-only',action='store_true',help='Compare actual host CPU object against frozen reference')
    args=p.parse_args();out=args.output.resolve();generated=args.generated.resolve()
    if out.is_relative_to(ROOT) or out.is_relative_to(EMU):p.error('private output must be outside repositories')
    if args.frames<=0:p.error('--frames must be positive')
    out.mkdir(parents=True,exist_ok=False);media=args.media.resolve();original=digest(media)
    with (out/'build.log').open('w') as log:
        subprocess.run(['make','-C','components/apple2gs/host','-j4','all','profile'],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    objects=[str(x) for x in (ROOT/'components/apple2gs/host/build').rglob('*.o') if x.name!='w65816.o']
    source='#include "doc_snapshot.h"\n'+(ROOT/'components/apple2gs/host/host_main.c').read_text()
    needle='    }\n    gs_disk_flush();';assert source.count(needle)==1
    assert source.count('    uint32_t samples = 0;')==1
    source=source.replace('    uint32_t samples = 0;','    unsigned doc_active_frames=0;\n    uint32_t samples = 0;')
    source=source.replace('        gs_run_frame();\n        if (wav)', '''        gs_run_frame();
        const struct ClemensDeviceEnsoniq *d=&gs.mmio->dev_audio.doc;
        for(unsigned n=0;n<((d->reg[0xe1]>>1)&31)+1;n++)
            if(!(d->reg[0xa0+n]&1)&&d->reg[0x40+n]){doc_active_frames++;break;}
        if (wav)''')
    source=source.replace(needle,'''    }
    printf("DOC_ACTIVE_FRAMES %u\\n",doc_active_frames);
    if(getenv("GS_SNAPSHOT")) {
        char path[1024];FILE *f;const char *prefix=getenv("GS_SNAPSHOT");
        uint64_t cpu[]={gs.cpu.a,gs.cpu.x,gs.cpu.y,gs.cpu.s,gs.cpu.d,gs.cpu.pc,gs.cpu.pbr,gs.cpu.dbr,gs.cpu.p,gs.cpu.e,gs.cpu.stopped,gs.cpu.waiting,gs.cpu.irq,gs.cpu.nmi,(uint64_t)gs.cpu.penalty,gs.cpu.vector_bank,gs.cpu.cycles};
        uint8_t doc[DOC_STATE_BYTES];doc_snapshot(doc,&gs.mmio->dev_audio.doc);
        const void *data[]={cpu,doc,cfg.ram,cfg.mega2,gs.mmio->e1_bank+0x2000,cfg.frame};
        size_t size[]={sizeof(cpu),sizeof(doc),cfg.ram_kb*1024,128*1024,0x8000,GS_SCREEN_WIDTH*GS_SCREEN_HEIGHT*2};
        const char *ext[]={"cpu","doc","ram","mega2","vram","rgb565"};
        for(unsigned i=0;i<6;i++){snprintf(path,sizeof(path),"%s.%s",prefix,ext[i]);f=fopen(path,"wb");if(!f||fwrite(data[i],1,size[i],f)!=size[i])abort();fclose(f);}
    }
    gs_disk_flush();''')
    (out/'tour.c').write_text(source)
    flags=['-O2','-g','-I'+str(ROOT/'tools'),'-Icomponents/apple2gs/include','-Icomponents/apple2gs','-Icomponents/apple2gs/clemens','-I../esp32emu/components/w65816/include','-I../esp32emu/components/emudbg/include','-I../esp32emu/components/diskspace/include']
    results={};cases=[('static-'+str(n),n,0,320) for n in [0,1,8,32]]+[(f'animated-{n}',32,1,n) for n in [320,640]]
    variants=['reference','production'] if args.production_only else ['reference','pair','split','combined']
    for variant in variants:
        dest=out/variant;dest.mkdir();obj=dest/'cpu.o'
        if variant=='production':
            shutil.copyfile(ROOT/'components/apple2gs/host/build/w65816.o',obj)
        else:
            subprocess.run(['cc',*flags,'-Os','-c',str(generated/(variant+'-core.c')),'-o',str(obj)],cwd=ROOT,check=True)
        for name,main in [('profile',ROOT/'tools/profile_host.c'),('tour',out/'tour.c')]:
            subprocess.run(['cc',*flags,str(main),*objects,str(obj),'-lm','-lpthread','-o',str(dest/name)],cwd=ROOT,check=True)
        results[variant]={}
        env={k:v for k,v in os.environ.items() if not k.startswith('GS_')};env.update(GS_RAM='1024',GS_CLOCK='3870000000')
        for name,voices,animated,mode in cases:
            prefix=dest/name
            with prefix.with_suffix('.log').open('w') as log:
                subprocess.run([str(dest/'profile'),str(args.fixtures.resolve()/(name+'.bin')),str(voices),str(animated),str(mode),str(prefix)],cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
            results[variant][name]={ext:digest(prefix.with_suffix('.'+ext)) for ext in ['pcm','cpu','doc','ram','mega2','vram','rgb565']}
        disk=dest/('disposable'+media.suffix);shutil.copyfile(media,disk);prefix=dest/'tour-state'
        env.update(GS_35=str(disk),GS_WAV=str(prefix.with_suffix('.wav')),GS_SNAPSHOT=str(prefix))
        with (dest/'tour.log').open('w') as log:
            subprocess.run([str(dest/'tour'),str(args.frames)],cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
        active=next(int(x.split()[1]) for x in (dest/'tour.log').read_text().splitlines() if x.startswith('DOC_ACTIVE_FRAMES '));assert active>0
        with wave.open(str(prefix.with_suffix('.wav')),'rb') as wav:assert any(wav.readframes(wav.getnframes()))
        values={ext:digest(prefix.with_suffix('.'+ext)) for ext in ['wav','cpu','doc','ram','mega2','vram','rgb565']};values.update(media=digest(disk),active_frames=active)
        results[variant]['tour']=values
        assert results[variant]==results['reference'],variant
        print('CPU REPLAY',variant,'six fixtures and Tour PASS',flush=True)
    assert digest(media)==original
    (out/'results.json').write_text(json.dumps(dict(frames=args.frames,original_media_unchanged=True,equal=True,production_only=args.production_only,variants=results),indent=2)+'\n')


if __name__=='__main__':main()
