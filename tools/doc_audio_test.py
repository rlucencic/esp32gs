#!/usr/bin/env python3
"""Compare complete DOC state/IRQ/mixing against the current production audio module."""
import argparse
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--cc', default='cc')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    clemens = root / 'components/apple2gs/clemens'
    source = (clemens / 'clem_audio.c').read_text()
    start = source.index('static inline uint16_t _clem_ensoniq_calc_waveform_ptr(')
    end = source.index('\nuint8_t clem_ensoniq_oscillator_cycle', start)
    exports = re.findall(r'^(?:void|unsigned|uint8_t|uint32_t)\s+(\w+)\s*\(', source, re.M)
    args.output.mkdir(parents=True, exist_ok=True)
    flags = ['-O2', '-g', '-w', '-I'+str(clemens), '-I'+str(root/'tools')]
    if args.sanitize:
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    for variant in ['combined', 'select', 'table']:
        files = []
        for prefix in ['ref_', 'candidate_']:
            definitions = ''.join(f'#define {name} {prefix}{name}\n' for name in exports)
            text = source
            if prefix == 'candidate_':
                text = ('#include "doc_pointer_variants.h"\n' + source[:start] +
                        'static inline uint16_t _clem_ensoniq_calc_waveform_ptr(struct ClemensDeviceEnsoniq *doc,unsigned i) '
                        '{return doc_pointer_'+variant+'(doc,i);}\n' + source[end:])
            path = args.output / (variant + '-' + prefix + '.c')
            path.write_text(definitions + text)
            files.append(str(path))
        binary = args.output / ('doc_audio_' + variant)
        subprocess.run([args.cc, *flags, str(root/'tools/doc_audio_test.c'), *files,
                        '-lm', '-o', str(binary)], check=True)
        print(variant, flush=True)
        subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
