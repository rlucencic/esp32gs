#!/usr/bin/env python3
"""Attribute sampled PCs using physical symbols and DWARF inline ancestry.

Ancestry describes inlined functions, not the runtime call stack. DOC kernel
counts are a union, so a PC is counted once when oscillator code is inlined
inside the scheduler. ROM/unknown PCs fall back to physical symbols.
"""
import argparse
import bisect
from collections import Counter
import json
from pathlib import Path
import re
import subprocess

CPU_FETCH = {'fetch', 'fetch16', 'fetch_slow', 'refill'}

DOC = {'clem_ensoniq_sync', 'clem_ensoniq_oscillator_cycle',
       '_clem_ensoniq_reset_osc', '_clem_ensoniq_set_irq'}


def analyze(elf, pcs, nm, rom=None):
    symbols = []
    for path in [elf, rom]:
        if path is None:
            continue
        for line in subprocess.check_output([nm, '-S', '--defined-only', str(path)], text=True).splitlines():
            parts = line.split()
            if len(parts) == 4 and parts[2].lower() in 'tw':
                symbols.append((int(parts[0], 16), int(parts[1], 16), parts[3]))
    symbols.sort()
    starts = [s[0] for s in symbols]
    counts = Counter(pcs)
    lines = subprocess.check_output([str(nm)[:-2]+'addr2line', '-e', str(elf), '-a', '-f', '-i',
        *[hex(pc) for pc in counts]], text=True).splitlines()
    groups = {}
    for line in lines:
        if re.fullmatch(r'0x[0-9a-fA-F]+', line):
            current = int(line, 16)
            groups[current] = []
        else:
            groups[current].append(line)
    physical, leaf, ancestry = Counter(), Counter(), Counter()
    kernel = oscillator = cpu_core = cpu_fetch = 0
    for pc, count in counts.items():
        index = bisect.bisect_right(starts, pc)-1
        name = symbols[index][2] if index >= 0 and pc < symbols[index][0]+max(symbols[index][1], 1) else '?'
        frames = groups[pc][::2]
        known = set(frames)-{'??'}
        known.add(name)
        physical[name] += count
        leaf[frames[0] if frames and frames[0] != '??' else name] += count
        for frame in known:
            ancestry[frame] += count
        kernel += count if known & DOC else 0
        oscillator += count if 'clem_ensoniq_oscillator_cycle' in known else 0
        # File identity avoids counting unrelated generic helper names.
        core = any(Path(location.split(':',1)[0]).name == 'w65816.c'
                   for location in groups[pc][1::2])
        cpu_core += count if core else 0
        cpu_fetch += count if core and known & CPU_FETCH else 0
    return {'samples': len(pcs), 'physical': dict(physical.most_common()),
            'leaf': dict(leaf.most_common()), 'inline_ancestry': dict(ancestry.most_common()),
            'doc_kernel_union': kernel, 'oscillator_ancestry': oscillator,
            'cpu_core_union': cpu_core, 'cpu_fetch_union': cpu_fetch}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', type=Path, required=True)
    parser.add_argument('--pcs', type=Path, required=True)
    parser.add_argument('--nm', type=Path, required=True)
    parser.add_argument('--rom', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    pcs = json.loads(args.pcs.read_text())
    if not pcs:
        parser.error('PC sample list is empty')
    result = analyze(args.elf, pcs, args.nm, args.rom)
    args.output.write_text(json.dumps(result, indent=2)+'\n')


if __name__ == '__main__':
    main()
