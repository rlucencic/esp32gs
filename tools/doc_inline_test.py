#!/usr/bin/env python3
"""Check local oscillator-helper scope against the retained DOC module."""
import argparse
from pathlib import Path
import re
import subprocess


def generate(root, out, revision):
    source = subprocess.check_output(['git', 'show', revision +
        ':components/apple2gs/clemens/clem_audio.c'], cwd=root, text=True)
    needle = 'uint8_t clem_ensoniq_oscillator_cycle('
    if source.count(needle) != 1 or 'static '+needle in source:
        raise ValueError('Reference must expose the original oscillator helper')
    exports = re.findall(r'^(?:void|unsigned|uint8_t|uint32_t)\s+(\w+)\s*\(', source, re.M)
    out.mkdir(parents=True, exist_ok=True)
    for variant, prefix in [('reference', 'ref_'), ('inline', 'candidate_')]:
        module = source if variant == 'reference' else source.replace(needle, 'static '+needle)
        definitions = ''.join(f'#define {name} {prefix}{name}\n' for name in exports)
        (out/(variant+'-module.c')).write_text(module)
        (out/(variant+'.c')).write_text(definitions+module)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--reference-revision', default='792b627')
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    generate(root, args.output, args.reference_revision)
    flags = ['-O2', '-g', '-I'+str(root/'components/apple2gs/clemens')]
    if args.sanitize:
        flags += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer']
    for name in ['doc_audio_test', 'doc_schedule_check']:
        binary = args.output/name
        subprocess.run(['cc', *flags, str(root/'tools'/(name+'.c')),
            str(args.output/'reference.c'), str(args.output/'inline.c'), '-lm', '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
