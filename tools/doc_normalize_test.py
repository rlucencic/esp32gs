#!/usr/bin/env python3
"""Generate bit-exact DOC normalization candidates and validate the native mixers.

Emits complete, symbol-prefixed audio modules for native and IDF tests. Production
source (current or committed) supplies the reference and is never modified.
--production-unit compares the complete current production module to it. Tables use exact
binary32 hex literals, generated with explicit rounding at each reference step.
"""
import argparse
from pathlib import Path
import re
import struct
import subprocess


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def table(name, values):
    rows = [', '.join(value.hex()+'f' for value in values[i:i+4]) for i in range(0,256,4)]
    return 'static const float '+name+'[256] = {\n    '+',\n    '.join(rows)+'\n};\n'


def generate(root, out, reference_revision=None, production_unit=False):
    source = (subprocess.check_output(['git','show',reference_revision+
              ':components/apple2gs/clemens/clem_audio.c'],cwd=root,text=True)
              if reference_revision else (root/'components/apple2gs/clemens/clem_audio.c').read_text())
    old = ('        level = (2.0f * data / 255.0f) - 1.0f;\n'
           '        doc->voice[channel] += level * (volume / 255.0f);')
    if source.count(old)!=1:
        raise ValueError('Reference must contain division-based mixing; pass --reference-revision 4ea30c6')
    norm = table('s_doc_unit', [f32(i/255.0) for i in range(256)])
    levels = table('s_doc_level', [f32(f32((2*i)/255.0)-1.0) for i in range(256)])
    replacements = {'reference': old,
                    'unit': '        level = 2.0f * s_doc_unit[data] - 1.0f;\n'
                            '        doc->voice[channel] += level * s_doc_unit[volume];',
                    'dual': '        level = s_doc_level[data];\n'
                            '        doc->voice[channel] += level * s_doc_unit[volume];'}
    exports = re.findall(r'^(?:void|unsigned|uint8_t|uint32_t)\s+(\w+)\s*\(',source,re.M)
    out.mkdir(parents=True,exist_ok=True)
    for name,prefix in [('reference','ref_'),('unit','unit_'),('dual','dual_')]:
        definitions=''.join(f'#define {symbol} {prefix}{symbol}\n' for symbol in exports)
        arrays='' if name=='reference' else norm+(levels if name=='dual' else '')
        module=source.replace(old,replacements[name])
        if name=='unit' and production_unit:
            arrays=''
            module=(root/'components/apple2gs/clemens/clem_audio.c').read_text()
        wrapper=f'''\nuint32_t normalize_run_{name}(struct ClemensDeviceEnsoniq *doc,unsigned repeats) {{
    uint32_t sum=0;
    for(unsigned n=0;n<repeats;++n) {{
        unsigned i=n&31;
        doc->reg[0x60+i] ^= (uint8_t)n;
        unsigned channels={prefix}clem_ensoniq_voices(doc);
        uint32_t word;memcpy(&word,&doc->voice[channels?channels-1:0],4);
        sum += word;
    }}
    return sum;
}}
'''
        (out/(name+'.c')).write_text(definitions+arrays+module+wrapper)
        (out/(name+'-module.c')).write_text(arrays+module)
    return source,norm,levels,replacements


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--cc',default='cc')
    parser.add_argument('--sanitize',action='store_true')
    parser.add_argument('--reference-revision',help='Committed reference module to preserve reproducibility after integration')
    parser.add_argument('--production-unit',action='store_true',
                        help='Use the current complete production module for the shared-table candidate')
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[1]
    if args.production_unit and not args.reference_revision:
        parser.error('--production-unit requires --reference-revision for the division-based reference')
    source,norm,levels,replacements=generate(root,args.output,args.reference_revision,args.production_unit)
    flags=['-O2','-g','-w','-I'+str(root/'tools'),'-I'+str(root/'components/apple2gs/clemens')]
    if args.sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    mainfile=args.output/'main.c'
    mainfile.write_text('#include "doc_normalize_check.h"\nvoid clem_debug_log(int l,const char *f,...){(void)l;(void)f;}\nint main(void){doc_normalize_check();return 0;}\n')
    binary=args.output/'normalize_check'
    subprocess.run([args.cc,*flags,str(root/'tools/doc_normalize_check.c'),str(mainfile),
                    *[str(args.output/(name+'.c')) for name in ['reference','unit','dual']],
                    '-lm','-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
    # Reuse the complete DOC state/IRQ/mode harness with two independently linked
    # modules; its public reference and candidate prefixes remain unchanged.
    exports=re.findall(r'^(?:void|unsigned|uint8_t|uint32_t)\s+(\w+)\s*\(',source,re.M)
    for variant in ['unit','dual']:
        files=[]
        for prefix in ['ref_','candidate_']:
            definitions=''.join(f'#define {name} {prefix}{name}\n' for name in exports)
            module=source
            arrays=''
            if prefix=='candidate_':
                module=source.replace(replacements['reference'],replacements[variant])
                arrays=norm+(levels if variant=='dual' else '')
                if variant=='unit' and args.production_unit:
                    arrays=''
                    module=(root/'components/apple2gs/clemens/clem_audio.c').read_text()
            path=args.output/(variant+'-'+prefix+'.c');path.write_text(definitions+arrays+module);files.append(str(path))
        binary=args.output/('state-'+variant)
        subprocess.run([args.cc,*flags,str(root/'tools/doc_audio_test.c'),*files,'-lm','-o',str(binary)],check=True)
        print('STATE',variant,flush=True)
        subprocess.run([str(binary)],check=True)


if __name__=='__main__':
    main()
