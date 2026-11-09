#!/usr/bin/env python3
"""Probe DOC helper scope/metadata layout in isolated header copies.

Requires existing P4/S3 compile_commands.json files. Production is never edited;
layout forms are code-generation probes, not runtime-validated candidates.
"""
import argparse
from pathlib import Path
import shutil,subprocess,json,shlex
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--reference-revision',default='792b627')
args=parser.parse_args()
root=Path(__file__).resolve().parents[1];r=args.output.resolve();r.mkdir(parents=True,exist_ok=False);clem=root/'components/apple2gs/clemens'
source=subprocess.check_output(['git','show',args.reference_revision+':components/apple2gs/clemens/clem_audio.c'],cwd=root,text=True)
header=subprocess.check_output(['git','show',args.reference_revision+':components/apple2gs/clemens/clem_mmio_types.h'],cwd=root,text=True)
results={}
for variant in ['reference','inline','layout','layout-inline']:
 out=r/variant;out.mkdir(exist_ok=True)
 for h in clem.glob('*.h'):shutil.copyfile(h,out/h.name)
 (out/'clem_mmio_types.h').write_text(header)
 text=source
 if 'inline' in variant and 'static uint8_t clem_ensoniq_oscillator_cycle(' not in text:text=text.replace('uint8_t clem_ensoniq_oscillator_cycle(', 'static uint8_t clem_ensoniq_oscillator_cycle(')
 if 'layout' in variant:
  h=header.replace('    uint8_t sound_ram[65536];\n','')
  begin=h.index('struct ClemensDeviceEnsoniq {');end=h.index('\n};',begin)
  h=h[:end]+'\n    uint8_t sound_ram[65536];'+h[end:]
  (out/'clem_mmio_types.h').write_text(h)
 (out/'clem_audio.c').write_text(text)
for arch,build in [('p4','build-tab5'),('s3','build')]:
 results[arch]={};item=next(c for c in json.loads((root/build/'compile_commands.json').read_text()) if c['file'].endswith('/clem_audio.c'))
 for variant in ['reference','inline','layout','layout-inline']:
  out=r/variant;obj=out/(arch+'.o');src=out/'clem_audio.c';cmd=shlex.split(item['command']);cmd[cmd.index('-o')+1]=str(obj);cmd=[str(src) if x==item['file'] else x for x in cmd];cmd+=['-I'+str(out),'-fstack-usage'];subprocess.run(cmd,cwd=item['directory'],check=True,stdout=subprocess.DEVNULL)
  tool=cmd[0].removesuffix('gcc');nm=subprocess.check_output([tool+'nm','-S','--defined-only',str(obj)],text=True);asm=subprocess.check_output([tool+'objdump','-dr',str(obj)],text=True);(out/(arch+'.asm')).write_text(asm)
  symbols={s.split()[-1]:{'bytes':int(s.split()[1],16),'kind':s.split()[2]} for s in nm.splitlines() if len(s.split())==4};stacks={s.split('\t')[0].split(':')[-1]:int(s.split('\t')[1]) for s in obj.with_suffix('.su').read_text().splitlines()}
  result={'text_bytes':sum(v['bytes'] for v in symbols.values() if v['kind'].lower()=='t'),'sync_bytes':symbols['clem_ensoniq_sync']['bytes'],'sync_stack':stacks['clem_ensoniq_sync'],'oscillator_bytes':symbols.get('clem_ensoniq_oscillator_cycle',{}).get('bytes',0),'oscillator_stack':stacks.get('clem_ensoniq_oscillator_cycle')};results[arch][variant]=result;print(arch,variant,result)
(r/'sizes.json').write_text(json.dumps(results,indent=2))
