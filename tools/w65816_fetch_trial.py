#!/usr/bin/env python3
"""Generate isolated 65C816 fetch candidates; production source stays unchanged."""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess

ROOT=Path(__file__).resolve().parents[1]
EMU=ROOT.parent/'esp32emu'


def generate(out,revision):
    source=subprocess.check_output(['git','show',revision+':components/w65816/w65816.c'],cwd=EMU,text=True)
    start=source.index('INLINE uint8_t fetch(w65816_t *c, regs_t *r)')
    end=source.index('\n/* The 6502',start)
    old=source[start:end]
    pair='''INLINE uint16_t fetch16(w65816_t *c, regs_t *r)
{
    /* Both bytes are directly mapped: no callback or page/bank crossing. */
    if (__builtin_expect(r->ip_end - r->ip >= 2, 1)) {
        const uint8_t *ip = r->ip;
        r->ip = ip + 2;
        r->left -= 2;
        return (uint16_t)ip[0] | (uint16_t)ip[1] << 8;
    }
    uint32_t lo = fetch(c, r);
    return lo | (uint32_t)fetch(c, r) << 8;
}
'''
    split='''static uint8_t NOINLINE fetch_slow(w65816_t *c, regs_t *r)
{
    refill_t f = refill(c, r->ip, r->pbr, r->left);
    r->ip = f.ip;
    r->ip_end = f.ip_end;
    r->left = f.left;
    return f.v;
}

INLINE __attribute__((always_inline)) uint8_t fetch(w65816_t *c, regs_t *r)
{
    if (__builtin_expect(r->ip != r->ip_end, 1)) {
        r->left--;
        return *r->ip++;
    }
    return fetch_slow(c, r);
}

'''
    old16=old[old.index('INLINE uint16_t fetch16'):]
    forms={'reference':old,'pair':old[:old.index('INLINE uint16_t fetch16')]+pair,
           'split':split+old16,'combined':split+pair}
    out.mkdir(parents=True,exist_ok=False)
    for name,body in forms.items():
        module=source[:start]+body+source[end:]
        (out/(name+'-core.c')).write_text(module)
        prefix=''.join(f'#define w65816_{fn} {name}_w65816_{fn}\n' for fn in ['reset','run','step'])
        (out/(name+'.c')).write_text(prefix+module)
    (out/'source-metadata.json').write_text(json.dumps({'reference_revision':revision,'source_sha256':{name:hashlib.sha256((out/(name+'-core.c')).read_bytes()).hexdigest() for name in forms}},indent=2)+'\n')
    return forms


def codegen(out,forms):
    result={}
    for arch,build in [('p4','build-tab5'),('s3','build')]:
        item=next(c for c in json.loads((ROOT/build/'compile_commands.json').read_text()) if c['file'].endswith('/w65816.c'))
        result[arch]={}
        for name in forms:
            obj=out/(arch+'-'+name+'.o');cmd=shlex.split(item['command'])
            cmd[cmd.index('-o')+1]=str(obj);cmd=[str(out/(name+'-core.c')) if x==item['file'] else x for x in cmd];cmd+=['-fstack-usage']
            subprocess.run(cmd,cwd=item['directory'],check=True,stdout=subprocess.DEVNULL)
            tool=cmd[0].removesuffix('gcc')
            nm=subprocess.check_output([tool+'nm','-S','--defined-only',str(obj)],text=True)
            symbols={p[3]:{'bytes':int(p[1],16),'kind':p[2]} for line in nm.splitlines() if len(p:=line.split())==4}
            stacks={s.split('\t')[0].split(':')[-1]:int(s.split('\t')[1]) for s in obj.with_suffix('.su').read_text().splitlines()}
            result[arch][name]={'text_bytes':sum(v['bytes'] for v in symbols.values() if v['kind'].lower()=='t'),'run_bytes':symbols['w65816_run']['bytes'],'run_stack':stacks['w65816_run'],'fetch_bytes':symbols.get('fetch',{}).get('bytes',0),'fetch16_bytes':symbols.get('fetch16',{}).get('bytes',0),'fetch_slow_bytes':symbols.get('fetch_slow',{}).get('bytes',0),'helper_stacks':{name:stacks[name] for name in ['refill','fetch','fetch16','fetch_slow'] if name in stacks}}
            (out/(arch+'-'+name+'.asm')).write_text(subprocess.check_output([tool+'objdump','-dr',str(obj)],text=True))
            print(arch,name,result[arch][name],flush=True)
    (out/'codegen.json').write_text(json.dumps(result,indent=2)+'\n')


def check(out,sanitize,seeds):
    flags=['-Os','-g','-I'+str(EMU/'components/w65816/include'),'-I'+str(EMU/'components/emudbg/include')]
    if sanitize:flags+=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer']
    objects=[]
    for name in ['reference','pair','split','combined']:
        obj=out/(name+'-native.o')
        subprocess.run(['cc',*flags,'-c',str(out/(name+'.c')),'-o',str(obj)],check=True)
        objects.append(str(obj))
    binary=out/'fetch-check'
    subprocess.run(['cc',*flags,str(ROOT/'tools/w65816_fetch_check.c'),*objects,'-o',str(binary)],check=True)
    result=subprocess.check_output([str(binary),str(seeds)],text=True)
    print(result,end='',flush=True)
    report={'seeds_per_variant':seeds,'sanitized':sanitize,'debugger_enabled':True,'result':result,'irq':{}}
    for name in ['reference','pair','split','combined']:
        binary=out/(name+'-irq')
        subprocess.run(['cc',*flags,'-DEMUDBG_OFF',str(EMU/'components/w65816/test/irq.c'),str(out/(name+'-core.c')),'-o',str(binary)],check=True)
        result=subprocess.check_output([str(binary)],text=True)
        print(name,result.strip(),flush=True)
        (out/(name+'-irq.log')).write_text(result)
        report['irq'][name]=result.strip()
    (out/'native-results.json').write_text(json.dumps(report,indent=2)+'\n')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--reference-revision',default='dbf4811')
    p.add_argument('--codegen',action='store_true')
    p.add_argument('--check',action='store_true')
    p.add_argument('--sanitize',action='store_true')
    p.add_argument('--seeds',type=int,default=256)
    args=p.parse_args();out=args.output.resolve()
    if args.seeds<=0:p.error('--seeds must be positive')
    if out.is_relative_to(ROOT) or out.is_relative_to(EMU):p.error('isolated output must be outside repositories')
    forms=generate(out,args.reference_revision)
    if args.codegen:codegen(out,forms)
    if args.check:check(out,args.sanitize,args.seeds)


if __name__=='__main__':main()
