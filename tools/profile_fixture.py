#!/usr/bin/env python3
"""Assemble the original IIgs DOC/SHR profiling workload (requires cc65 tools)."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--voices', type=int, choices=[0, 1, 8, 16, 32], required=True)
    parser.add_argument('--animate', action='store_true')
    parser.add_argument('--mode', type=int, choices=[320, 640], default=320)
    parser.add_argument('--ca65', default='ca65')
    parser.add_argument('--ld65', default='ld65')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    source = Path(__file__).with_suffix('.s')
    with tempfile.TemporaryDirectory(prefix='gs-fixture-') as tmp:
        tmp = Path(tmp)
        config = tmp / 'link.cfg'
        config.write_text('MEMORY { RAM: start=$2000, size=$1ff0, file=%O; }\n'
                          'SEGMENTS { CODE: load=RAM, type=ro; }\n')
        obj = tmp / 'fixture.o'
        subprocess.run([args.ca65, '--cpu', '65816', '-D', f'VOICES={args.voices}',
                        '-D', f'ANIMATE={int(args.animate)}', '-D', f'SHR640={int(args.mode==640)}',
                        str(source), '-o', str(obj)], check=True)
        subprocess.run([args.ld65, '-C', str(config), str(obj), '-o', str(args.output)], check=True)
    data = args.output.read_bytes()
    assert 0 < len(data) < 0x1ff0
    print(json.dumps({'entry': '0x022000', 'sentinel': '0x023ff0', 'bytes': len(data),
                      'voices': args.voices, 'enabled_oscillators': 32, 'animate': args.animate,
                      'mode': args.mode, 'sha256': hashlib.sha256(data).hexdigest()}))


if __name__ == '__main__':
    main()
