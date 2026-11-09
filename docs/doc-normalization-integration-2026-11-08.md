# DOC normalization integration, 2026-11-08

This trial applies the shared 1 KB table selected by the
[normalization experiment](doc-normalization-experiment-2026-11-08.md) to the
full emulator. `clem_ensoniq_voices` loads the normalized sample and volume
from the same 256-entry binary32 table, retaining the reference operation
order and float accumulation. Channel, mode, IRQ and timing logic are unchanged.
[Measurements](measurements/doc-normalization-integration-2026-11-08.json)
record exact replay hashes, target builds, fixed board checkpoints and fresh
reference/candidate/reference samples.

## Change and correctness

`clem_doc_normalize.h` contains generated exact hex float literals for
`i/255.0f`. The sample level is `2*table[data]-1`; volume uses `table[volume]`.
Scaling by two is exact over these byte values, so this preserves the rounded
reference normalization without either per-voice division. There is no
runtime table initialization or register-derived cache.

The same host executable harness links the committed division-based reference
(`0a7cc8c`) and current candidate separately. All six DOC/SHR replays match
byte for byte: static 0/1/8/32 active voices and animated 320-/640-mode with
32 voices. Each uses fixed RTC input, 360-frame boot, 120-frame warm-up and
600-frame capture. Compared outputs are signed-16 PCM, E1 VRAM, RGB565,
CPU registers/flags/cycles, full DOC state, 1 MB RAM and 128 KB Mega II memory.
Sentinel, guest GLU readbacks, active count, waveform, volume, mode, silence
and non-clipping checks also pass. Raw artifacts remain outside the repository.

The actual production candidate passes **524,288 exhaustive mixer cases**
against the committed reference on the native host under ASan/UBSan. These
cover every sample/volume byte pair in eight accumulation/channel/AM/control
contexts and compare all voice float bits, channel count and clipped mono
output bits. Its complete DOC module also passes **224 states / 57,344
sync/mix events**, with exact state, IRQ and float outputs, including mode
transitions, termination, register writes and HALT/restart. These preserve
current behavior; they do not independently establish DOC hardware accuracy.
The earlier isolated P4 experiment checked the same table arithmetic and
all 524,288 mixer cases directly on P4.

`tools/doc_normalize_test.py` now accepts `--production-unit` to compile the
complete current production module as the shared-table candidate, including
its real header. The reference stays pinned to the earlier division form:

```sh
python3 tools/doc_normalize_test.py --sanitize --reference-revision 0a7cc8c \
  --production-unit --output /tmp/gs-normalization-check
```

## Builds and footprint

Sequential full builds pass for reference isolated P4, candidate isolated P4,
normal candidate P4 and normal candidate S3. Both test images use the same
1 MB default and isolated test namespace/media path. Normal source defaults
are restored before normal builds. Only Tab5 is attached, so S3 runtime
performance is unmeasured.

| Target / object | Reference | Candidate |
| --- | --- | --- |
| P4 mixer text | 264 B | 270 B |
| P4 audio text symbols | 3456 B | 3462 B |
| P4 mixer frame | 64 B | 80 B |
| S3 mixer text | 216 B | 191 B |
| S3 audio text symbols | 3120 B | 3095 B |
| S3 mixer frame | 48 B | 64 B |
| Read-only normalization table | 0 B | 1024 B |

The linked P4 mixer is also 264 → 270 text bytes; the linked application
binary grows from 2,008,064 to 2,009,088 bytes after alignment. Audio text
symbol totals exclude literal pools and section padding. Both targets lose
the two normalization divide sites: P4 `fdiv.s` and S3 `__divsf3` calls.
The 16-byte mixer-frame increase is explicit; call-chain peak stack usage
is not inferred from a single function's frame.

## Hardware procedure

Tab5, P4 360 MHz / PSRAM 200 MHz, IDF 5.5.6 / GCC 14.2, ROM 3, 1 MB RAM,
speaker enabled. A fresh full-flash MSX backup precedes testing. Original
NVS bytes are restored before every round; test namespace/media path are
isolated and no user media is mounted.

Reference A and candidate B each run ROM startup, quiet static SHR, static
one-voice, static 32-voice and animated 640-mode 32-voice controls. Reference C
repeats the dense static and animated cases to bracket timing drift.

Each fixture resets and boots 360 frames, loads original code into bank 2,
then warms 120 frames. At the stopped checkpoint, the harness verifies real
guest DOC GLU readbacks and initialization sentinel, reads all 32 KB of E1
video RAM and captures registers and source pixels. Every pixel is checked
against captured VRAM/SCBs/palette, with static output also compared against
native RGB565. The 640 checker follows the current renderer's palette-quarter
order; it is a consistency check rather than an independent hardware oracle.
The debugger disconnects before live measurement.

Three serial `i` checkpoints per case delimit two approximately 15-second
profiler-free FPS windows. Three 32,000-sample core-1 captures follow; reference
A and candidate B each add three inline-aware animated captures. Late serial
`kcycles/frame` fields are truncated by the existing buffer, so this report
uses sampling for audio attribution. Sampling includes cap/idle periods and
interrupt overhead. DWARF inline ancestry identifies inlined helpers, not a
full call stack; nested percentages overlap.

## Hardware results

All ten fixture checkpoints pass DOC readback and VRAM-to-pixel checks.
Candidate and final-reference source pixels and all E1 bytes match reference A.
Initialized CPU registers match; cumulative cycles vary between live sessions
and Y is inherited from the asynchronous ROM boot (the fixture never uses it).
Native CPU snapshots match all saved fields, including Y and cycles.

| Case | Reference A FPS | Candidate B FPS | Reference C FPS | Reference core-1 idle | Candidate core-1 idle |
| --- | --- | --- | --- | --- | --- |
| rom | 59.25 / 59.17 | 59.28 / 59.21 | — | 15.1–16.4% | 15.3–16.6% |
| static-0 | 59.76 / 59.75 | 59.68 / 59.75 | — | 62.8–63.2% | 62.9–63.4% |
| static-1 | 59.70 / 59.75 | 59.75 / 59.68 | — | 54.6–55.0% | 54.6–54.9% |
| static-32 | 59.75 / 59.69 | 59.76 / 59.75 | 59.75 / 59.74 | 31.0–33.2% | 35.4–37.5% |
| animated-640 | 59.60 / 59.67 | 59.68 / 59.62 | 59.68 / 59.62 | 12.8–13.4% | 16.7–17.6% |

All 24 profiler-free timing windows have zero additional missing speaker
frames; cumulative counters also include startup, resets and debugger pauses.
All 42 captures contain the full 32,000 samples (1,344,000 total).

Voice mixing's flat sample share falls from 10.3–10.6% to 5.7–6.2% in static-32
and from 10.7–11.2% to 6.3–6.5% in animated-640. Separate inline captures
place it at 10.69–10.82% before and 6.37–6.60% after. Shares depend on frame
phase and interrupt timing; these are not per-call cycle-speed ratios.
The quiet and one-voice control ranges overlap, so no sparse-audio application
gain is isolated. Live ROM/LCD captures may show different startup-animation
phases; fixed fixture pixels are the exact comparisons used here.

**Retain the change.** Exact native float/PCM/state checks, fixed hardware
checkpoints, full target builds and reference-bracketed dense-workload headroom
support the shared table. The cost is 1024 read-only bytes, 6 P4 text bytes
and 16 bytes in the mixer's frame; S3 object text saves 25 bytes with the same
frame increase. FPS remains capped near 60. These generated workloads do not
establish a real-program throughput gain or an S3 runtime speedup.

**Next validation:** replay software that actually plays DOC audio, using
throwaway media copies, before another local arithmetic trial. Compare native
PCM/state and media writes, then P4 headroom and source pixels at controlled
checkpoints. This will exercise wider CPU/IWM/video/cache pressure than the
original generated tones. Preserve user media and restore Tab5 afterward.
S3 runtime and listening assessment remain open.

Tab5 is restored from the fresh pre-test 16 MB MSX full-flash backup with
hash verification. MSX Disk BASIC is running; final guest and LCD captures
exactly match restored controls, including cursor state. The restore removes
test NVS settings. Private raw logs, ROMs, replay snapshots and backup images
remain outside the repository.
