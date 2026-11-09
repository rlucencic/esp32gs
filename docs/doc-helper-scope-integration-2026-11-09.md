# DOC local oscillator-helper integration — 2026-11-09

Retain the local oscillator helper after full-app validation on Tab5.

The reference is `b331c40`, with the retained span scheduler, pointer and
normalization optimizations and IRQ queue fix. The candidate makes
`clem_ensoniq_oscillator_cycle` local to its sole source caller. GCC 14.2
inlines its body into the scheduler under both targets' normal flags.
There are no new allocations, cached derived registers or layout changes.

## Correctness and builds

The actual production candidate passes six exact native fixture pairs:
static 0/1/8/32 voices and animated SHR 320/640 with 32 voices. Complete PCM
streams and final CPU registers/flags/cycles, pointer-free DOC state, configured
1 MB RAM, Mega II RAM, VRAM and RGB565 match the retained source.

Fail-fast ASan/UBSan checks against `b331c40` pass 224 states / 57,344 DOC
mode/IRQ/mix events, plus 24,576 boundary starting cases / 245,760
sync/mix/IRQ-read calls. The independent IRQ FIFO test passes 18,000 events.
These cover every legal oscillator count, all modes, halted M0 resets,
partner activation, zero samples, quiet restart and counter/budget wrap.

A 21,601-frame native Tour replay also matches PCM and final CPU/DOC/RAM,
pixels and resulting media bytes. Original media is never mounted by the
native harness; each replay uses its own copy.

Full normal candidate S3/P4 builds and isolated P4 reference/candidate builds
pass. The board images share isolated RAM/BRAM and drive settings namespaces
and their own media folder. Each round restores the original NVS bytes and
starts with 1 MB RAM.

## Profiling method

Physical function shares change meaning when the oscillator body moves into
the scheduler. `tools/profile_inline.py` resolves the retained raw PC samples
using ELF physical symbols and DWARF inline ancestry. It counts the union of
scheduler/oscillator/reset/IRQ helper code once per PC. That DOC kernel metric
covers the same work across both builds. Inline ancestry is source inlining
information, rather than a runtime call stack. ROM and unresolved PCs retain
physical-symbol attribution.

The analyzer reproduces the existing DOC leaf attribution for two previous
32,000-PC captures and checks physical/leaf totals. Each new complete capture
retains its PCs and inline attribution outside the repositories. Public
measurements contain aggregate counts and function shares.

P4 reference A and candidate B run ROM, quiet/sparse/dense SHR and animated
SHR 640, followed by a fresh Tour copy. Reference C repeats the dense static
and animated fixtures. Paused checkpoints compare guest-generated DOC
readbacks, VRAM, independently decoded source pixels and initialized CPU
fields. Live fixture Y is inherited from ROM and unused; cumulative live
cycles also vary. Native replay compares every recorded CPU field.

Tour checkpoints use nominal reset-relative seconds 240/270/285/300/325,
with music profiles at 270/285/300. Their screenshots establish software
stages; voice phases are not locked to native execution. Missing-speaker
deltas describe observed timing windows, including music captures for Tour;
setup/reset/loading can already have accumulated misses. Synthetic timing
windows precede profiling. FPS is capped near 60; uncapped throughput and
S3 runtime remain unmeasured.

## Code size and stack

| Target / form | Audio object text | Scheduler text | Scheduler frame | Separate oscillator text/frame |
| --- | ---: | ---: | ---: | ---: |
| P4 reference | 3,502 bytes | 650 bytes | 80 bytes | 274 / 0 bytes |
| P4 local helper | 3,424 bytes | 846 bytes | 32 bytes | absent |
| S3 reference | 3,105 bytes | 558 bytes | 64 bytes | 269 / 32 bytes |
| S3 local helper | 3,048 bytes | 770 bytes | 48 bytes | absent |

Total audio text saves 78 P4 / 57 S3 bytes. The scheduler frame saves
48 P4 / 16 S3 bytes and the nested oscillator call disappears. Linked P4
scheduler text is 638 → 838 bytes, with the separate 274-byte body removed
(a combined 74-byte reduction after relaxation). Both isolated images remain
2,009,104 bytes at their binary alignment.

## Live music and controls

All five Tour guest checkpoints and the final LCD capture match the reference
pixel for pixel. The three nominal music checkpoints show DOC kernel shares
7.29/9.95/9.54% in reference A and 6.19/7.82/8.07% in candidate B. Idle shares
are 44.7/40.7/41.0% and 46.4/43.1/43.7%, respectively. The observed 240–325 s
window adds no missing-speaker frames. The phase limitation above prevents
interpreting these as exact per-call kernel speedups.

Quiet and sparse controls do not support a universal improvement. One active
voice has DOC kernel share 4.55–4.58% in A versus 4.64–4.75% in B; idle ranges
overlap (53.9–56.9% versus 54.3–57.3%). Quiet fixture DOC share is 0.17–0.20%
versus 0.14–0.15%, with idle 60.2–60.4% versus 60.4–63.5%. ROM idle ranges
overlap, 16.1–16.7% versus 15.1–17.3%. Each control has three captures;
timing windows remain near the frame cap and add no missing-speaker frames.

The real program and synthetic fixtures use disposable media/settings.
Original SD media is never mounted, and both Tour copies are verified
unchanged, ejected, removed and checked absent after the run.

## Dense full-app results and decision

All 42 captures contain 32,000 samples each (1,344,000 total), with 32 timing
windows. All ten fixture checkpoints pass guest readback, independently
decoded VRAM/pixels and initialized CPU checks. Static native pixels match.
Physical and leaf attribution totals each equal 32,000 in every capture.

| Workload / metric | Reference A | Candidate B | Reference C |
| --- | ---: | ---: | ---: |
| Static 32 voices: DOC kernel, core 1 | 17.77–17.88% | 12.68–13.17% | 17.66–17.95% |
| Static 32 voices: idle | 37.0–39.4% | 44.6–44.8% | 38.8–39.4% |
| Animated 640: DOC kernel, core 1 | 19.84–20.26% | 13.34–13.62% | 20.00–20.41% |
| Animated 640: idle | 18.7–19.6% | 24.9–26.1% | 18.9–19.3% |

Dense static median sampled DOC cost falls about 26%; animated falls about
32–33%, against either bracketing reference. These are sampled CPU shares,
not uncapped emulator throughput. Dense FPS remains 59.58–59.81 across all
rounds. Every observed timing window has zero added missing-speaker frames.
Both references return to similar DOC shares, supporting the candidate gain.

The local helper is retained in production: exact output/state checks pass,
dense and live-music measurements improve, code and stack shrink, and no
additional runtime state is introduced. The sparse control remains a stated
limit. Shared runtime source is unchanged; S3 runtime gains are unmeasured.

Aggregates: [measurement JSON](measurements/doc-helper-scope-integration-2026-11-09.json).
Raw PCs, private media, firmware backups and serial/network logs remain outside
the repositories. The next bounded investigation is remaining oscillator
metadata/address loads in the inlined scheduler. Any metadata layout trial
first needs canonical field-based DOC snapshots; current raw layout hashes
cannot compare reordered structures safely.

## Board restoration

The fresh full 16 MB MSX backup is restored and independently flash-verified
with a matching digest. After verification resets the board, MSX is started
again. Disk BASIC guest and LCD captures both match the known restored
screens exactly. No test firmware or temporary media remains active.
