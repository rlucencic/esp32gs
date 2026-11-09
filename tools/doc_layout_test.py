#!/usr/bin/env python3
"""Compare isolated DOC metadata-first layout against a retained revision.

Production headers/source are never edited. Modules use their own header copies;
bridges import/export every field to a common test state. Existing mode/boundary
checks retain assertions, and compare padding-free canonical bytes after calls.
"""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
FIELDS = {
    'dt_budget': ('clem_clocks_duration_t', None), 'cycle': ('unsigned', None),
    'voice': ('float', '16'), 'sound_ram': ('uint8_t', '65536'),
    'reg': ('uint8_t', '256'), 'acc': ('unsigned', '32'), 'ptr': ('uint16_t', '32'),
    'osc_flags': ('uint8_t', '32'), 'osc_stack': ('uint8_t', '32'),
    'address': ('unsigned', None), 'data_reg': ('uint8_t', None),
    'addr_auto_inc': ('bool', None), 'is_access_ram': ('bool', None),
    'is_busy': ('bool', None)}


def generate(out, revision):
    clem = ROOT/'components/apple2gs/clemens'
    audio = subprocess.check_output(['git', 'show', revision+':components/apple2gs/clemens/clem_audio.c'], cwd=ROOT, text=True)
    header = subprocess.check_output(['git', 'show', revision+':components/apple2gs/clemens/clem_mmio_types.h'], cwd=ROOT, text=True)
    body = header.split('struct ClemensDeviceEnsoniq {', 1)[1].split('\n};', 1)[0]
    declarations = re.sub(r'/\*.*?\*/|//[^\n]*', '', body, flags=re.S)
    fields = {}
    for declaration in declarations.split(';'):
        if not declaration.strip():
            continue
        match = re.fullmatch(r'\s*(\w+)\s+(\w+)(?:\[(\d+)\])?\s*', declaration)
        if not match:
            raise ValueError('Unrecognized DOC field; update the canonical schema')
        kind, name, count = match.groups()
        fields[name] = (kind, count)
    if fields != FIELDS:
        raise ValueError('DOC fields/types/counts changed; update the canonical schema')
    assert header.count('    uint8_t sound_ram[65536];\n') == 1
    moved = header.replace('    uint8_t sound_ram[65536];\n', '')
    end = moved.index('\n};', moved.index('struct ClemensDeviceEnsoniq {'))
    moved = moved[:end]+'\n    /* Test-only metadata-first layout. */\n    uint8_t sound_ram[65536];'+moved[end:]
    exports = re.findall(r'^(?:void|unsigned|uint8_t|uint32_t)\s+(\w+)\s*\(', audio, re.M)
    for variant, prefix in [('reference', 'ref_'), ('candidate', 'candidate_')]:
        dest = out/variant
        dest.mkdir(parents=True)
        for h in clem.glob('*.h'):
            shutil.copyfile(h, dest/h.name)
        (dest/'clem_mmio_types.h').write_text(header if variant == 'reference' else moved)
        definitions = ''.join(f'#define {name} {prefix}module_{name}\n' for name in exports)
        bridge = definitions+audio+'\n#include "doc_state.h"\n'
        bridge += '''
static struct ClemensDeviceEnsoniq local;
static void import_state(const struct DocState *src) { DOC_STATE_COPY(&local,src); }
static void export_state(struct DocState *dst) {
    memset(dst,0,sizeof(*dst)); DOC_STATE_COPY(dst,&local);
}
'''
        signatures = [
            ('void', 'reset', '', ''),
            ('void', 'write_data', ', uint8_t v', ',v'),
            ('uint8_t', 'read_data', ', uint8_t v', ',v'),
            ('uint32_t', 'sync', ', uint32_t v', ',v'),
            ('unsigned', 'voices', '', ''),
            ('void', 'mono', ', unsigned n, float *l, float *r', ',n,l,r')]
        for result, name, parameters, call in signatures:
            bridge += f'\n#undef clem_ensoniq_{name}\n'
            value = '' if result == 'void' else result+' value='
            ret = '' if result == 'void' else 'return value;'
            bridge += f'{result} {prefix}clem_ensoniq_{name}(struct DocState *state{parameters}) {{ import_state(state); {value}{prefix}module_clem_ensoniq_{name}(&local{call}); export_state(state); {ret} }}\n'
        # Prefix module symbols independently in each TU, preserving static caches.
        (dest/'bridge.c').write_text(bridge)
        (dest/'clem_audio.c').write_text(audio)
    for name in ['doc_audio_test', 'doc_schedule_check']:
        text = (ROOT/'tools'/(name+'.c')).read_text()
        text = text.replace('#include "clem_device.h"', '#include "doc_state.h"\ntypedef uint32_t clem_clocks_duration_t;')
        text = text.replace('struct ClemensDeviceEnsoniq', 'struct DocState')
        text = text.replace('assert(!memcmp(&a,&b,sizeof(a)));', '''uint8_t x[DOC_STATE_BYTES],y[DOC_STATE_BYTES];
 doc_state_pack(x,&a);doc_state_pack(y,&b);assert(!memcmp(x,y,sizeof(x)));''')
        (out/(name+'.c')).write_text(text)
    return {'reference_revision': revision, 'fields': sorted(fields), 'canonical_bytes': 66128}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--reference-revision', default='85d6204')
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    out = args.output.resolve()
    if out.is_relative_to(ROOT):
        parser.error('isolated output must be outside the repository')
    out.mkdir(parents=True, exist_ok=False)
    results = generate(out, args.reference_revision)
    flags = ['-O2', '-g', '-I'+str(ROOT/'tools')]
    if args.sanitize:
        flags += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer']
    for name in ['reference', 'candidate']:
        binary = out/(name+'-snapshot-check')
        subprocess.run(['cc', *flags, '-I'+str(out/name), str(ROOT/'tools/doc_state_test.c'), '-o', str(binary)], check=True)
        results[name+'_snapshot_check'] = subprocess.check_output([str(binary)], text=True).strip()
        print(results[name+'_snapshot_check'], flush=True)
    obs = []
    for name in ['reference', 'candidate']:
        obj = out/(name+'.o')
        subprocess.run(['cc', *flags, '-c', str(out/name/'bridge.c'), '-o', str(obj)], check=True)
        obs.append(str(obj))
    for name in ['doc_audio_test', 'doc_schedule_check']:
        binary = out/name
        subprocess.run(['cc', *flags, str(out/(name+'.c')), *obs, '-lm', '-o', str(binary)], check=True)
        result = subprocess.check_output([str(binary)], text=True)
        print(result, end='', flush=True)
        results[name] = result.strip()
    (out/'results.json').write_text(json.dumps(results, indent=2)+'\n')


if __name__ == '__main__':
    main()
