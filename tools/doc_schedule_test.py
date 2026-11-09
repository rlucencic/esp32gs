#!/usr/bin/env python3
"""Generate DOC scheduler experiments without changing production source."""
import argparse
from pathlib import Path
import re
import subprocess


def generate(root, out, revision, production_span=False):
    source = (subprocess.check_output(['git', 'show', revision +
        ':components/apple2gs/clemens/clem_audio.c'], cwd=root, text=True)
        if revision else (root/'components/apple2gs/clemens/clem_audio.c').read_text())
    begin = source.index('    osc_cycle = doc->cycle % osc_cnt_2;')
    end = source.index('\n    return (doc->reg[CLEM_ENSONIQ_REG_OSC_OIR]', begin)
    loop = source[begin:end]
    assert loop.count('        ++doc->cycle;') == 1
    counter = loop.replace('    while (cycles--) {',
        '    doc->cycle += cycles;\n    while (cycles--) {').replace('        ++doc->cycle;\n', '')
    body_start = loop.index('            uint8_t ctl =')
    body_end = loop.index('\n        }\n        ++doc->cycle;', body_start)
    body = '\n'.join(line[4:] for line in loop[body_start:body_end].splitlines())
    span = '''    osc_cycle = doc->cycle % osc_cnt_2;
    doc->cycle += cycles;
    while (cycles) {
        if (osc_cycle >= osc_cnt) {
            unsigned skip = osc_cnt_2 - osc_cycle;
            if (skip > cycles) skip = cycles;
            cycles -= skip;
            osc_cycle += skip;
            if (osc_cycle == osc_cnt_2) osc_cycle = 0;
            continue;
        }
        unsigned run = osc_cnt - osc_cycle;
        if (run > cycles) run = cycles;
        cycles -= run;
        unsigned end = osc_cycle + run;
        do {
BODY
            ++osc_cycle;
        } while (osc_cycle != end);
    }
'''.replace('BODY', '\n'.join('    '+line for line in body.splitlines()))
    exports = re.findall(r'^(?:void|unsigned|uint8_t|uint32_t)\s+(\w+)\s*\(', source, re.M)
    out.mkdir(parents=True, exist_ok=True)
    for name, replacement in [('reference', loop), ('counter', counter), ('span', span)]:
        module = source[:begin] + replacement + source[end:]
        if name == 'span' and production_span:
            module = (root/'components/apple2gs/clemens/clem_audio.c').read_text()
        (out / (name+'-module.c')).write_text(module)
        prefix = 'ref_' if name == 'reference' else 'candidate_'
        definitions = ''.join(f'#define {symbol} {prefix}{symbol}\n' for symbol in exports)
        (out / (name+'.c')).write_text(definitions+module)
        bench_prefix = 'ref_' if name == 'reference' else name+'_'
        bench_definitions = ''.join(f'#define {symbol} {bench_prefix}{symbol}\n' for symbol in exports)
        (out / ('bench-'+name+'.c')).write_text(bench_definitions+module)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--reference-revision', default='02d9f47',
                        help='Frozen per-slot reference (default: 02d9f47)')
    parser.add_argument('--production-span', action='store_true',
                        help='Check the actual production module as the span candidate')
    parser.add_argument('--variant', action='append', choices=['counter', 'span'])
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--cc', default='cc')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    generate(root, args.output, args.reference_revision, args.production_span)
    flags = ['-O2', '-g', '-I'+str(root/'components/apple2gs/clemens')]
    if args.sanitize:
        flags += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer']
    for variant in args.variant or ['counter', 'span']:
        for name in ['doc_audio_test', 'doc_schedule_check']:
            binary = args.output / (variant+'-'+name)
            subprocess.run([args.cc, *flags, str(root/'tools'/(name+'.c')),
                str(args.output/'reference.c'), str(args.output/(variant+'.c')),
                '-lm', '-o', str(binary)], check=True)
            print(variant, name, flush=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
