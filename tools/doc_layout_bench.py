#!/usr/bin/env python3
"""Generate an isolated IDF DOC-layout benchmark from validated native modules.

Pass a benchmark sdkconfig with P4 PSRAM/XIP and USB serial console enabled.
This generates files only; flashing requires a separate explicit command.
"""
import argparse
from pathlib import Path
import re
import shutil
from doc_layout_test import ROOT

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--generated',type=Path,required=True)
parser.add_argument('--sdkconfig',type=Path,required=True)
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args()
b=args.output.resolve();generated=args.generated.resolve()
if b.is_relative_to(ROOT):parser.error('benchmark output must be outside repository')
if not (generated/'results.json').exists():parser.error('run doc_layout_test.py successfully first')
b.mkdir(parents=True,exist_ok=False);(b/'main').mkdir()
shutil.copyfile(args.sdkconfig,b/'sdkconfig')
(b/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.16)\ninclude($ENV{IDF_PATH}/tools/cmake/project.cmake)\nidf_build_set_property(MINIMAL_BUILD ON)\nproject(gsdoclayoutbench)\n')
(b/'partitions.csv').write_text('nvs,data,nvs,0x9000,0x6000\nphy_init,data,phy,0xf000,0x1000\nfactory,app,factory,0x10000,8M\n')
sources=[ROOT/'tools/doc_layout_bench.c']
for variant,prefix in [('reference','ref_'),('candidate','candidate_')]:
 out=generated/variant;source=(out/'clem_audio.c').read_text();exports=re.findall(r'^(?:void|unsigned|uint8_t|uint32_t)\s+(\w+)\s*\(',source,re.M)
 definitions=''.join(f'#define {name} {prefix}{name}\n' for name in exports)
 bridge=definitions+source+'\n#include "doc_snapshot.h"\n'
 hooks='''
size_t PREFIXsize(void){return sizeof(struct ClemensDeviceEnsoniq);}
void PREFIXsetup(void *p,unsigned scene) {
 struct ClemensDeviceEnsoniq *doc=p;
 memset(doc,0,sizeof(*doc));PREFIXclem_ensoniq_reset(doc);
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
static uint32_t (*volatile kernel_sync)(struct ClemensDeviceEnsoniq*,clem_clocks_duration_t)=PREFIXclem_ensoniq_sync;
void PREFIXrun(void *doc,unsigned dt){for(unsigned n=0;n<512;n++)kernel_sync(doc,dt);}
void PREFIXsnapshot(void *doc,uint8_t *out){doc_snapshot(out,doc);}
'''
 bridge+=hooks.replace('PREFIX',prefix);(out/'bench-module.c').write_text(bridge);sources.append(out/'bench-module.c')
(b/'main/CMakeLists.txt').write_text('idf_component_register(SRCS '+' '.join(map(str,sources))+' INCLUDE_DIRS '+str(ROOT/'tools')+' PRIV_REQUIRES esp_psram)\ntarget_compile_options(${COMPONENT_LIB} PRIVATE -O2 -fjump-tables -ftree-switch-conversion)\n')
