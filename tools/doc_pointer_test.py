#!/usr/bin/env python3
"""Generate current-production DOC pointer wrappers and run the native oracle.

The --output directory also contains standalone run kernels for an IDF benchmark.
"""
import argparse
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cc', default='cc')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    clemens = root / 'components/apple2gs/clemens'
    source = (clemens / 'clem_audio.c').read_text()
    start = source.index('static inline uint16_t _clem_ensoniq_calc_waveform_ptr(')
    body = source.index('{', start)
    depth = 1
    end = body + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    helper = source[start:end]
    table_start = source.index('static uint16_t s_ensoniq_ptr_bits_mask[')
    table = source[table_start:source.index(';', table_start)+1]
    args.output.mkdir(parents=True, exist_ok=True)
    files = []
    for variant in ['reference', 'combined', 'select', 'table']:
        function = '_clem_ensoniq_calc_waveform_ptr' if variant == 'reference' else 'doc_pointer_' + variant
        text = '#include "doc_pointer_variants.h"\n'
        if variant == 'reference':
            text += table + '\n' + helper + '\n'
        text += f'uint16_t pointer_{variant}(struct ClemensDeviceEnsoniq *d,unsigned i) {{return {function}(d,i);}}\n'
        text += f'''uint32_t run_{variant}(struct ClemensDeviceEnsoniq *d,unsigned repeats) {{
    uint32_t sum=0;
    for(unsigned n=0;n<repeats;++n) for(unsigned i=0;i<32;++i) {{
        sum += {function}(d,i);
        d->acc[i] = (d->acc[i] + 0x1400 + i*97) & 0xffffff;
    }}
    return sum;
}}
'''
        path = args.output / (variant + '.c')
        path.write_text(text)
        files.append(str(path))
    binary = args.output / 'doc_pointer_test'
    subprocess.run([args.cc, '-O2', '-g', '-Wall', '-Wextra', '-I'+str(clemens),
                    '-I'+str(root/'tools'), str(root/'tools/doc_pointer_test.c'), *files,
                    '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
