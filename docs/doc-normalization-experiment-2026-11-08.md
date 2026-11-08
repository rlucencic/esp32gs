# DOC voice normalization trial, 2026-11-08

Select the 1 KB shared normalization table for a full-emulator A/B trial.
Both tested forms preserve exact host and P4 mixer float bits. In the warm
P4 mixer kernel, the shared table saves about 43% with 32 active voices,
27% with eight, and 31% with AM. The 2 KB form is slightly faster in active
cases. Production source remains unchanged in this experiment; application
performance and S3 runtime gains are unmeasured.

The [pointer integration](doc-pointer-integration-2026-11-08.md) places
`clem_ensoniq_voices` at 10.74–10.88% of core 1 in its animated synthetic
workload. P4 target disassembly has two float divisions per non-skipped
voice; S3 has two `__divsf3` call sites. This experiment replaces those
normalizations with constant lookups, preserving accumulation order.
[Measurements](measurements/doc-normalization-experiment-2026-11-08.json)
include all ABBA timings, replay hashes and target object sizes.

## Forms and float behavior

The reference (`4ea30c6`) computes:

```c
level = (2.0f * data / 255.0f) - 1.0f;
doc->voice[channel] += level * (volume / 255.0f);
```

`unit` has one 256-float table `i / 255.0f`, shared by data and volume:

```c
level = 2.0f * s_doc_unit[data] - 1.0f;
doc->voice[channel] += level * s_doc_unit[volume];
```

`dual` adds a second 256-float table holding the reference's rounded
sample level, so that both operands come directly from tables. The tables
occupy 1,024 and 2,048 read-only bytes. There is no runtime table construction
or derived-state invalidation.

Scaling the normalized byte by two is exact in binary32 over this range;
there is no underflow or overflow. The reference division with numerator
`2*data` therefore has the same rounded value as twice the table's rounded
`data/255`. The following subtraction and final accumulation preserve the
reference's operands and ordering. Table generation explicitly rounds each
reference step to binary32 and emits exact C hex float literals. The tests
check actual compiled output, including the target compiler's FP contraction.

## Checks

`tools/doc_normalize_test.py` reads the production audio module, replaces
only the two normalization statements, and emits complete separately prefixed
reference/unit/dual modules. `--reference-revision` can freeze the committed
source. No handwritten replacement of mode/IRQ/channel logic is used.

Both forms pass **524,288 exhaustive mixer cases each** on the native host
under ASan/UBSan and again on P4 before benchmarking. Every data/volume byte
pair runs through eight contexts: one voice; dense shared channels; varying
sample/volume sequences; paired AM; halted/zero-sample voices; all 16 channel
indices handled by the current mixer; opposing samples with cancellation;
and mixed control modes. All 16 voice float bit patterns, channel count and
clipped mono outputs must match exactly. Test output confirms all cases.

The complete DOC state/IRQ/mode harness also passes **224 states and 57,344
sync/mix events per form** on the native host under ASan/UBSan, with 11,866
nonzero IRQ observations and 327,629 halted observations each. This retains
free-run/one-shot/sync/swap behavior, AM, sample termination, register writes,
HALT/restart and interrupt acknowledgement. It preserves existing behavior;
it is not an independent DOC hardware-accuracy oracle.

Six full native emulator replays per form compare against the committed
reference: 0/1/8/32 active voices with static SHR and 32 voices with animated
320-/640-mode SHR. The same fixed-RTC 360-frame boot, 120-frame warm-up and
600-frame capture are used. All PCM, E1 VRAM, RGB565, CPU fields/cycles, full
DOC state, 1 MB RAM and 128 KB Mega II memory snapshots match byte for byte.
Only hashes are published. P4 validation in this trial covers mixer float
bits; full-emulator P4 A/B, speaker and picture controls are the next step.

Reproduce native validation from the project root:

```sh
python3 tools/doc_normalize_test.py --sanitize --reference-revision 4ea30c6 \
  --output /tmp/gs-normalization
```

The generated `*-module.c` files retain production public names for host
linking/target object analysis. `reference.c`, `unit.c` and `dual.c` prefix
public symbols for simultaneous differential tests and the IDF benchmark.

## P4 kernel timing

Tab5, P4 360 MHz / PSRAM 200 MHz, IDF 5.5.6, GCC 14.2, `-O2` with normal
jump-table options. The isolated firmware runs the exact-bit check and
benchmark on core 1, without emulation/display/network/audio tasks. Both
candidates and reference use the actual DOC struct layout and full production
mixing function. DOC allocation is tested in internal RAM and PSRAM.

Each timed call invokes the mixer 512 times; one sample byte changes on each
iteration, and output bits feed a checksum. All variants start from identical
registers/voices and must produce equal checksums. Setup is outside timing.
Nine A/B/B/A rounds per memory/scene/candidate give **216 ABBA cases / 864
timed calls**. Interrupts remain enabled; results use medians of each round's
A and B averages. Data and tables are warm. Table/code cache pressure in the
full emulator may change the benefit.

| Mixer scene | Shared 1 KB saving | Dual 2 KB saving |
| --- | --- | --- |
| All halted | 7.43–7.45% | 0.68–0.69% |
| One active / 32 enabled | 6.79% | 7.95–7.96% |
| Eight active / 32 enabled | 27.31% | 29.32–29.38% |
| 32 active, two channels | 43.18% | 46.14–46.15% |
| 32 with paired AM | 30.64–30.66% | 33.98–33.99% |
| 32, eight channels / zero samples | 38.26–38.27% | 40.21–40.22% |

The halted scene is a mixer control. The emulator already bypasses mixing
when the DOC is silent, so it does not imply a quiet-application gain.
The loop, sample-byte update, function calls and checksum are included in
these kernel costs. They are not pure division-latency measurements.

Build `tools/doc_normalize_bench.c`, `tools/doc_normalize_check.c` and the
three prefixed generated modules in a minimal P4 IDF project. Include
`tools/` and `components/apple2gs/clemens`, require `esp_psram`, and use
`-O2 -fjump-tables -ftree-switch-conversion`. Enable 360 MHz / 200 MHz PSRAM.
The console prints the exact-bit pass before `BENCH BEGIN`, then 216 rows
and `BENCH END`. Row columns are memory, scene, candidate, round, A/B/B/A
cycles and checksum. Keep assertions enabled.

## Footprint and decision

Full audio modules compile with normal project flags for P4 and S3. These
are object-symbol sizes, not a linked full-application flash delta. Audio
text totals sum function symbols; literal pools and section padding are excluded.

| Target / form | Mixer text | Audio text total | Read-only tables | Mixer stack |
| --- | --- | --- | --- | --- |
| P4 reference | 264 B | 3456 B | 0 B | 64 B |
| P4 shared | 270 B | 3462 B | 1024 B | 80 B |
| P4 dual | 252 B | 3444 B | 2048 B | 80 B |
| S3 reference | 216 B | 3120 B | 0 B | 48 B |
| S3 shared | 191 B | 3095 B | 1024 B | 64 B |
| S3 dual | 170 B | 3074 B | 2048 B | 64 B |

Both forms remove the two P4 `fdiv.s` instructions and the two S3 software
FP-division call sites. S3 runtime is unmeasured. Both increase the mixer
frame by 16 bytes. The shared table is the first integration candidate:
it keeps half the table footprint and most of the measured active benefit.
The dual table remains an alternative if full-app evidence favors it.

Next build reference/shared-table full P4 images and normal S3/P4 applications,
repeat exact native/state checks, then reference/candidate/reference timing
and source-pixel controls for sparse/dense/animated workloads. Verify the
stack/rodata tradeoff in the linked application and restore the fresh MSX
backup. Retain only after those checks; no assembly is justified by this
experiment yet. Broader software audio and S3 runtime remain open.

Tab5 is restored from the fresh pre-test 16 MB MSX full-flash backup with
hash verification. MSX Disk BASIC is running; final guest and LCD captures
exactly match the restored controls, including cursor state. Raw logs,
ROMs, replay snapshots and backup images remain outside the repository.
