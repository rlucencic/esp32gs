#!/usr/bin/env python3
"""Replay private IIgs media against historical/current DOC audio code.

The output directory contains disposable media, audio, state and logs: keep it
outside the repository. The original media is never mounted. Native assertions
stay enabled. Only clem_audio.c changes between the two runners.
"""
import argparse
import array
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import wave

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--media", type=Path, required=True)
parser.add_argument("--drive", choices=["35", "525", "HD1", "HD2"], default="35")
parser.add_argument("--frames", type=int, default=21601)
parser.add_argument("--ram", type=int, default=1024)
parser.add_argument("--reference-revision", default="8c6b241")
parser.add_argument("--script", type=Path)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
if args.frames <= 0:
    parser.error("--frames must be positive")
if args.script:
    for line in args.script.read_text().splitlines():
        fields = line.split()
        if len(fields) > 1 and fields[1] == "insert":
            parser.error("scripts cannot insert media; the harness mounts disposable copies")
r = args.output.resolve()
if r.is_relative_to(root) or r.is_relative_to(root.parent / "esp32emu"):
    parser.error("private output must be outside the repositories")
r.mkdir(parents=True, exist_ok=False)
media = args.media.resolve()
original_hash = hashlib.sha256(media.read_bytes()).hexdigest()
with (r / "build.log").open("w") as log:
    subprocess.run(["make", "-C", "components/apple2gs/host", "-j4"],
                   cwd=root, stdout=log, stderr=subprocess.STDOUT, check=True)
s=(root/'components/apple2gs/host/host_main.c').read_text()
assert s.count('    uint32_t samples = 0;') == 1
assert s.count('        gs_run_frame();\n        if (wav)') == 1
assert s.count('    }\n    gs_disk_flush();') == 1
s=s.replace('    uint32_t samples = 0;', '    uint64_t active_frames=0, active_sum=0, max_active=0;\n    uint32_t samples = 0;')
s=s.replace('        gs_run_frame();\n        if (wav)', '''        gs_run_frame();
        unsigned active=0;
        const struct ClemensDeviceEnsoniq *d=&gs.mmio->dev_audio.doc;
        unsigned enabled=((d->reg[0xe1]>>1)&31)+1;
        for(unsigned o=0;o<enabled;o++) if(!(d->reg[0xa0+o]&1) && d->reg[0x40+o]) active++;
        active_frames+=(active>0);active_sum+=active;if(active>max_active)max_active=active;
        if (wav)''')
s=s.replace('    }\n    gs_disk_flush();', '''    }
    printf("ACTIVITY frames=%d active_frames=%llu active_sum=%llu max_active=%llu\\n",frames,(unsigned long long)active_frames,(unsigned long long)active_sum,(unsigned long long)max_active);
    if(getenv("GS_SNAPSHOT")) {
        const char *prefix=getenv("GS_SNAPSHOT");char path[1024];FILE *sf;
        uint64_t cpu[]={gs.cpu.a,gs.cpu.x,gs.cpu.y,gs.cpu.s,gs.cpu.d,gs.cpu.pc,gs.cpu.pbr,gs.cpu.dbr,gs.cpu.p,gs.cpu.e,gs.cpu.stopped,gs.cpu.waiting,gs.cpu.irq,gs.cpu.nmi,(uint64_t)gs.cpu.penalty,gs.cpu.vector_bank,gs.cpu.cycles};
        const void *ptr[]={cpu,&gs.mmio->dev_audio.doc,cfg.ram,cfg.mega2,cfg.frame};
        size_t len[]={sizeof(cpu),sizeof(gs.mmio->dev_audio.doc),cfg.ram_kb*1024,128*1024,GS_SCREEN_WIDTH*GS_SCREEN_HEIGHT*2};
        const char *ext[]={"cpu","doc","ram","mega2","rgb565"};
        for(int i=0;i<5;i++){snprintf(path,sizeof(path),"%s.%s",prefix,ext[i]);sf=fopen(path,"wb");if(!sf||fwrite(ptr[i],1,len[i],sf)!=len[i])abort();fclose(sf);}
    }
    gs_disk_flush();''')

(r / "host-replay.c").write_text(s)
flags = ["-O2", "-g", "-Icomponents/apple2gs/include", "-Icomponents/apple2gs",
         "-Icomponents/apple2gs/clemens", "-I../esp32emu/components/w65816/include",
         "-I../esp32emu/components/emudbg/include", "-I../esp32emu/components/diskspace/include"]
objects = list((root / "components/apple2gs/host/build").rglob("*.o"))
results = {}
for variant in ["reference", "candidate"]:
    obs = objects
    if variant == "reference":
        source = subprocess.check_output(["git", "show", args.reference_revision +
            ":components/apple2gs/clemens/clem_audio.c"], cwd=root)
        (r / "reference-audio.c").write_bytes(source)
        subprocess.run(["cc", *flags, "-c", str(r / "reference-audio.c"), "-o",
                        str(r / "reference-audio.o")], cwd=root, check=True)
        obs = [p for p in objects if p.name != "clem_audio.o"] + [r / "reference-audio.o"]
    runner = r / ("host-" + variant)
    subprocess.run(["cc", *flags, str(r / "host-replay.c"), *map(str, obs),
                    "-lm", "-lpthread", "-o", str(runner)], cwd=root, check=True)
    disk = r / (variant + media.suffix)
    shutil.copyfile(media, disk)
    env = {k: v for k, v in os.environ.items() if not k.startswith("GS_")}
    env.update(GS_RAM=str(args.ram), GS_CLOCK="3870000000",
               GS_WAV=str(r / (variant + ".wav")),
               GS_SNAPSHOT=str(r / (variant + "-end")))
    env["GS_" + args.drive] = str(disk)
    if args.script:
        env["GS_SCRIPT"] = str(args.script.resolve())
    with (r / (variant + ".log")).open("w") as log:
        subprocess.run([str(runner), str(args.frames)], cwd=root, env=env,
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    outputs = {"media": disk, "pcm_wav": r / (variant + ".wav")}
    outputs.update({suffix: r / (variant + "-end." + suffix)
                    for suffix in ["cpu", "doc", "ram", "mega2", "rgb565"]})
    hashes = {key: hashlib.sha256(path.read_bytes()).hexdigest()
              for key, path in outputs.items()}
    log = (r / (variant + ".log")).read_text()
    match = re.search(r"ACTIVITY frames=(\d+) active_frames=(\d+) active_sum=(\d+) max_active=(\d+)", log)
    if not match:
        raise RuntimeError("missing activity summary")
    with wave.open(str(outputs["pcm_wav"]), "rb") as wav:
        pcm = array.array("h", wav.readframes(wav.getnframes()))
    results[variant] = dict(hashes=hashes, activity=list(map(int, match.groups())),
                            nonzero_samples=sum(v != 0 for v in pcm),
                            peak=max(map(abs, pcm), default=0))
if hashlib.sha256(media.read_bytes()).hexdigest() != original_hash:
    raise RuntimeError("original media changed during replay")
if not results["candidate"]["activity"][1] or not results["candidate"]["nonzero_samples"]:
    raise RuntimeError("workload did not produce active DOC audio")
same = results["reference"] == results["candidate"]
results["equal"] = same
results["original_media_unchanged"] = True
results["media_written"] = results["candidate"]["hashes"]["media"] != original_hash
(r / "results.json").write_text(json.dumps(results, indent=2) + "\n")
if not same:
    raise RuntimeError("DOC software replay differs; inspect private output")
print("PASS: audio, CPU, DOC, RAM, pixels and disposable media match")
