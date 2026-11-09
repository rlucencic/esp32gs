# DOC oscillator helper scope trial — 2026-11-09

Select local oscillator-helper scope for a full-app trial. GCC 14.2 inlines
the helper after it is declared `static`, shrinking code and stack frames.
The complete P4 DOC kernel saves about 37–38% with 32 active voices at
scanline deltas. Production source remains at `792b627`; this result does
not yet establish a whole-emulator gain.

## Candidates and correctness

The reference exposes `clem_ensoniq_oscillator_cycle` as a global function.
Its only source caller is the scheduler in the same file; it has no public
header declaration. The candidate changes that definition to `static`.
There is no forced-inline attribute, new allocation, cache or derived state.
Both tested targets eliminate its separate function body under their normal
compiler flags. `tools/doc_inline_test.py` generates complete independently
prefixed modules from frozen reference `792b627`, without editing production.

The candidate passes:

- Fail-fast native ASan/UBSan: 224 states / 57,344 DOC sync/mix/IRQ events,
  plus 24,576 boundary starting cases / 245,760 sync/mix/IRQ-read calls.
- The independent IRQ FIFO regression: 18,000 native sanitized events.
- Six complete native fixture pairs: static 0/1/8/32 voices and animated
  SHR 320/640 with 32 voices. PCM, CPU registers/flags/cycles, full DOC state,
  RAM, Mega II, VRAM and RGB565 match exactly.
- A 21,601-frame native Tour replay: complete PCM and final CPU/DOC/RAM/pixels
  and resulting media bytes match; its original private image is unchanged.

## Target code generation

`tools/doc_load_codegen.py` copies headers into isolated output directories
and compares four source/header forms, using existing S3/P4 compilation
commands. Production files are untouched. The reference audio source and
DOC type definition are read from `792b627`.

| Target / form | Audio text | Scheduler text | Scheduler stack |
| --- | ---: | ---: | ---: |
| P4 reference | 3,502 bytes | 650 bytes | 80 bytes |
| P4 local helper | 3,424 bytes | 846 bytes | 32 bytes |
| P4 metadata first | 3,250 bytes | 572 bytes | 48 bytes |
| P4 metadata first + local helper | 3,186 bytes | 744 bytes | 16 bytes |
| S3 reference | 3,105 bytes | 558 bytes | 64 bytes |
| S3 local helper | 3,048 bytes | 770 bytes | 48 bytes |
| S3 metadata first | 2,895 bytes | 546 bytes | 64 bytes |
| S3 metadata first + local helper | 2,858 bytes | 744 bytes | 64 bytes |

The local-helper form removes the separate oscillator body (274 P4 / 269 S3
bytes) and moves its work into the scheduler. Total audio text saves 78 P4 /
57 S3 bytes; the scheduler frame saves 48 P4 / 16 S3 bytes. Future full-app
profiles must account for the inlined body when comparing function shares.
Inline-aware attribution or inclusive DOC costs avoid confusing a larger
scheduler symbol share with a runtime regression.

The metadata forms move the 64 KB sound RAM array after register/accumulator
metadata, reducing large-offset address generation. They have code-generation
evidence only: they were not used for the native replays or P4 timings.
Relocation changes the DOC layout and raw state dumps; it needs canonical
field-based snapshots and separate state/IRQ/audio/media validation before
consideration. No layout or ABI change is applied here.

## P4 kernel timing

The temporary minimal firmware measures the complete scheduler/oscillator
path with 512 sync calls per timing, nine ABBA rounds per combination, two
memory placements, seven scenes and three clock deltas. There are 378 ABBA
cases / 1,512 measured timings / 774,144 scheduler calls. Each group compares
a hash of the entire final DOC state; all agree. Firmware also runs the
retained reference IRQ queue regression. Both modules use `-O2` and the
normal P4 optimization flags, at 360 MHz.

Median cycle savings below cover internal RAM and PSRAM. A scanline delta
is 182,400 clocks (usually 57 oscillator slots).

| Scene | One-slot delta | Scanline delta |
| --- | ---: | ---: |
| Quiet halted M0 | 18.6–21.3% | 18.6–21.3% |
| 32 enabled, one active | 5.8% | 7.3–7.5% |
| 32 enabled, eight active | 9.2–9.3% | 24.1–25.4% |
| 32 enabled, 32 active | 17.8–19.2% | 37.5–37.7% |
| Swap pairs | 12.2–13.2% | 27.3–28.2% |
| One enabled oscillator | 11.6–12.8% | 29.4–29.7% |
| Eight enabled, sync mode | 16.6–17.2% | 36.0–37.4% |

All tested median kernel controls improve. These are complete DOC sync
kernel timings, with mixing checked separately in the correctness tests.
There is no whole-app or S3 runtime measurement for this candidate yet.
The test firmware builds and both targets' audio objects compile; normal
candidate firmware builds remain part of the next integration step.

Next run full-app native comparisons and isolated P4 A/B with the local-helper
candidate, including quiet, sparse, dense, animated and real Tour audio.
Use inline-aware profiling and include references before/after. Defer metadata
relocation until that simpler candidate's integration is resolved.

[Aggregate measurements](measurements/doc-helper-scope-experiment-2026-11-09.json).
Private ROMs, media, audio and raw device logs stay outside the repositories.

The fresh 16 MB MSX backup is restored and digest-verified. After verification
restarts the board, Disk BASIC is started again; guest and LCD captures exactly
match restored controls. Original SD media is never mounted during this kernel
trial. Native tests use independent disposable copies of the private image.
