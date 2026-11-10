#!/usr/bin/env python3
"""Generate a minimal P4 fetch benchmark project; never flash automatically."""
import argparse
import json
from pathlib import Path
import shutil
from w65816_fetch_trial import ROOT,EMU
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--generated',type=Path,required=True)
p.add_argument('--sdkconfig',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();out=a.output.resolve()
if out.is_relative_to(ROOT) or out.is_relative_to(EMU):p.error('output must be outside repositories')
if not (a.generated/'native-results.json').exists():p.error('run --check successfully first')
checks=json.loads((a.generated/'native-results.json').read_text())
if len(checks.get('irq',{}))!=4:p.error('complete all IRQ checks first')
out.mkdir(parents=True,exist_ok=False);shutil.copyfile(a.sdkconfig,out/'sdkconfig')
(out/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.16)\ninclude($ENV{IDF_PATH}/tools/cmake/project.cmake)\nidf_build_set_property(MINIMAL_BUILD ON)\nproject(gsfetchbench)\n')
(out/'partitions.csv').write_text('nvs,data,nvs,0x9000,0x6000\nphy_init,data,phy,0xf000,0x1000\nfactory,app,factory,0x10000,8M\n')
includes=[EMU/'components/w65816/include',EMU/'components/emudbg/include']
for name in ['reference','pair','split','combined']:
    component='fetch_'+name;dest=out/'components'/component;dest.mkdir(parents=True)
    shutil.copyfile(a.generated/(name+'.c'),dest/'cpu.c')
    (dest/'linker.lf').write_text(f'[mapping:fetch_{name}]\narchive: lib{component}.a\nentries:\n    cpu (noflash_data)\n')
    (dest/'CMakeLists.txt').write_text('idf_component_register(SRCS cpu.c INCLUDE_DIRS '+' '.join(map(str,includes))+' LDFRAGMENTS linker.lf)\ntarget_compile_options(${COMPONENT_LIB} PRIVATE -Os -fjump-tables -ftree-switch-conversion -DCONFIG_EMUDBG=1)\n')
main=out/'main';main.mkdir()
source=(EMU/'components/w65816/test/speed.c').read_text().split('static double now(void)')[0]
(main/'main.c').write_text(source+'\n#include '+json.dumps(str(ROOT/'tools/w65816_fetch_bench.c'))+'\n')
(main/'CMakeLists.txt').write_text('idf_component_register(SRCS main.c INCLUDE_DIRS '+' '.join(map(str,includes))+' PRIV_REQUIRES esp_psram fetch_reference fetch_pair fetch_split fetch_combined)\ntarget_compile_options(${COMPONENT_LIB} PRIVATE -O2 -DCONFIG_EMUDBG=1)\n')
