#!/usr/bin/env python3
"""Build complete isolated cores for a DOC layout trial and replay fixtures/media.

Copies only source. ROMs are read from the project and private media is mounted
only through disposable copies. No production source/header is modified.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import wave
from doc_layout_test import ROOT, generate


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--reference-revision', default='85d6204')
    parser.add_argument('--media', type=Path, required=True)
    parser.add_argument('--frames', type=int, default=21601)
    parser.add_argument('--ca65', default='ca65')
    parser.add_argument('--ld65', default='ld65')
    args = parser.parse_args()
    out = args.output.resolve()
    if out.is_relative_to(ROOT) or out.is_relative_to(ROOT.parent/'esp32emu'):
        parser.error('private output must be outside repositories')
    if args.frames <= 0:
        parser.error('--frames must be positive')
    out.mkdir(parents=True, exist_ok=False)
    (out/'esp32emu').symlink_to(ROOT.parent/'esp32emu', target_is_directory=True)
    generate(out/'generated', args.reference_revision)
    media = args.media.resolve()
    original = digest(media)
    cases = [('static-'+str(n), n, 0, 320) for n in [0,1,8,32]]
    cases += [('animated-'+str(n), 32, 1, n) for n in [320,640]]
    for name, voices, animated, mode in cases:
        cmd = ['python3', str(ROOT/'tools/profile_fixture.py'), '--voices', str(voices), '--mode', str(mode), '--output', str(out/(name+'.bin')), '--ca65', args.ca65, '--ld65', args.ld65]
        if animated:
            cmd.append('--animate')
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)
    results = {}
    for variant in ['reference', 'candidate']:
        project = out/variant
        core = project/'components/apple2gs'
        shutil.copytree(ROOT/'components/apple2gs', core,
                        ignore=shutil.ignore_patterns('build', '__pycache__'))
        tools = project/'tools';tools.mkdir()
        for name in ['profile_host.c','doc_state.h','doc_snapshot.h']:
            shutil.copyfile(ROOT/'tools'/name, tools/name)
        for name in ['clem_audio.c','clem_mmio_types.h']:
            shutil.copyfile(out/'generated'/variant/name, core/'clemens'/name)
        host = core/'host/host_main.c'
        text = host.read_text()
        text = '#include "../../../tools/doc_snapshot.h"\n'+text
        needle = '    }\n    gs_disk_flush();'
        assert text.count(needle) == 1
        text = text.replace(needle, '''    }
    if (getenv("GS_SNAPSHOT")) {
        const char *prefix=getenv("GS_SNAPSHOT");char path[1024];FILE *sf;
        uint64_t cpu[]={gs.cpu.a,gs.cpu.x,gs.cpu.y,gs.cpu.s,gs.cpu.d,gs.cpu.pc,gs.cpu.pbr,gs.cpu.dbr,gs.cpu.p,gs.cpu.e,gs.cpu.stopped,gs.cpu.waiting,gs.cpu.irq,gs.cpu.nmi,(uint64_t)gs.cpu.penalty,gs.cpu.vector_bank,gs.cpu.cycles};
        uint8_t doc[DOC_STATE_BYTES];doc_snapshot(doc,&gs.mmio->dev_audio.doc);
        const void *ptr[]={cpu,doc,cfg.ram,cfg.mega2,gs.mmio->e1_bank+0x2000,cfg.frame};
        size_t len[]={sizeof(cpu),sizeof(doc),cfg.ram_kb*1024,128*1024,0x8000,GS_SCREEN_WIDTH*GS_SCREEN_HEIGHT*2};
        const char *ext[]={"cpu","doc","ram","mega2","vram","rgb565"};
        for(int i=0;i<6;i++){snprintf(path,sizeof(path),"%s.%s",prefix,ext[i]);sf=fopen(path,"wb");if(!sf||fwrite(ptr[i],1,len[i],sf)!=len[i])abort();fclose(sf);}
    }
    gs_disk_flush();''')
        host.write_text(text)
        with (project/'build.log').open('w') as log:
            subprocess.run(['make','-C',str(core/'host'),'-j4','all','profile'], stdout=log, stderr=subprocess.STDOUT, check=True)
        env = {k:v for k,v in os.environ.items() if not k.startswith('GS_')}
        env.update(GS_ROM=str(ROOT/'roms/gs_rom3.rom'), GS_CHARGEN=str(ROOT/'roms/chargen.rom'), GS_RAM='1024', GS_CLOCK='3870000000')
        results[variant] = {}
        for name, voices, animated, mode in cases:
            prefix = project/name
            with prefix.with_suffix('.log').open('w') as log:
                subprocess.run([str(core/'host/build/profile_host'),str(out/(name+'.bin')),str(voices),str(animated),str(mode),str(prefix)], cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
            results[variant][name] = {ext:digest(prefix.with_suffix('.'+ext)) for ext in ['pcm','cpu','doc','ram','mega2','vram','rgb565']}
            print('REPLAY',variant,name,flush=True)
        disk = project/('disposable'+media.suffix)
        shutil.copyfile(media,disk)
        prefix = project/'tour'
        tourenv = dict(env,GS_35=str(disk),GS_WAV=str(prefix.with_suffix('.wav')),GS_SNAPSHOT=str(prefix))
        with prefix.with_suffix('.log').open('w') as log:
            subprocess.run([str(core/'host/build/gs_host'),str(args.frames)], cwd=project, env=tourenv, stdout=log, stderr=subprocess.STDOUT, check=True)
        with wave.open(str(prefix.with_suffix('.wav')),'rb') as wav:
            pcm = wav.readframes(wav.getnframes())
            assert any(pcm),'Tour must produce audio'
        outputs = {ext:digest(prefix.with_suffix('.'+ext)) for ext in ['wav','cpu','doc','ram','mega2','vram','rgb565']}
        outputs['media'] = digest(disk)
        results[variant]['tour'] = outputs
        print('REPLAY',variant,'tour',flush=True)
    assert original==digest(media),'Original media changed'
    assert results['reference']==results['candidate'],'Replay differs; inspect private output'
    results.update(equal=True,original_media_unchanged=True,frames=args.frames)
    (out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    print('PASS: six full-core fixtures and real Tour outputs match',flush=True)


if __name__ == '__main__':
    main()
