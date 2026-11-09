# DOC span scheduler integration — 2026-11-09

Retain span: the exact native and board checks pass, and the full-app profiles
confirm lower active scheduler cost with modest CPU headroom gains. The
increased stack frame is accepted. Frame rates stay at the existing cap;
quiet/ROM controls do not establish a universal headroom gain.

This compares the per-slot scheduler in `02d9f47` with the span scheduler in
the complete emulator. Both include the retained pointer/normalization
optimizations and the IRQ queue tail fix. The candidate advances the unsigned
cycle count once, visits oscillator slots in their original order and consumes
padding slots as a span. It adds no allocation or cached register state.

## Correctness and builds

Six native fixture pairs match exactly: static 0/1/8/32 voices, animated
320-mode and animated 640-mode with 32 voices. Each checks the complete PCM
stream and final CPU registers/flags/cycles, pointer-free DOC state, configured
1 MB RAM, Mega II RAM, VRAM and RGB565 frame. Production assertions remain
enabled.

The actual production candidate also passes fail-fast ASan/UBSan checks
against `02d9f47`: the 224-state / 57,344-event DOC mode/IRQ/mix harness and
24,576 scheduler boundary cases / 245,760 sync/mix/IRQ-read calls. Every legal
oscillator count 1–32, padding and partial rounds, halted M0 resets, all modes,
partner activation, zero samples, quiet restart, counter wrap and duration
budget wrap are covered. The IRQ FIFO regression passes 18,000 checked events.

The real Tour replay boots independent disposable floppy copies and runs
21,601 frames. PCM, final CPU/DOC/RAM/pixels and resulting media bytes match
exactly against `02d9f47`; the source media is unchanged. This uses the actual
production source, rather than the earlier generated-only candidate.

Full normal S3/P4 builds and isolated P4 reference/candidate builds pass.
The isolated builds share their own RAM/BRAM and drive settings namespaces
and media folder. Every board round restores the same original NVS bytes
before boot, uses 1 MB RAM and leaves original SD media unmounted.

## Hardware method

P4 reference A and candidate B run ROM, quiet/sparse/dense SHR fixtures and
animated SHR 640, then boot a throwaway Tour copy. Reference C repeats the
dense static and animated fixtures after the candidate. Checkpoints include
guest-generated DOC register readbacks, paused VRAM and source pixels;
initialized CPU fields are compared. Y is inherited from the booted ROM and
unused by the fixture; cumulative live cycles are also excluded. Native
comparisons include every recorded CPU field.

Three complete 32,000-sample core-1 captures per measured case report flat
function shares. Synthetic timing windows precede the profiles; their missing
speaker counts describe those windows. Tour checkpoints at nominal seconds
240/270/285/300/325 surround the three music captures. Its cumulative missing
count can already include setup/reset/loading; deltas describe the observed
window, not the entire boot. Tour media copies are verified and removed after
ejection. Screenshots establish the loading/music/tutorial stages, without
claiming exact native/live frame synchronization.

FPS remains capped near 60. Idle share measures CPU headroom, not uncapped
throughput. S3 runtime is unmeasured.

## Code size and stack

| Target / form | Scheduler object text | Stack frame |
| --- | ---: | ---: |
| P4 reference | 614 bytes | 48 bytes |
| P4 span | 650 bytes | 80 bytes |
| S3 reference | 544 bytes | 48 bytes |
| S3 span | 558 bytes | 64 bytes |

P4 linked scheduler text is 602 → 638 bytes after linker relaxation. Both
isolated P4 images are 2,009,104 bytes at the binary's alignment. The stack
cost is +32 bytes P4 / +16 S3. S3 object audio text grows 14 bytes; no S3
runtime gain is inferred from the P4 measurements.

The earlier kernel trial's one-slot slowdown remains a limitation: this
full-app test uses the emulator's normal device cadence. A separate native
Tour trace found only 32–63-slot active calls; no short-call speedup is claimed.

## Real-software observations

The five reset-relative Tour source screenshots and the final LCD capture
match exactly between A and B. Their music captures show:

| Nominal time after reset | Reference idle | Span idle | Reference scheduler | Span scheduler |
| --- | ---: | ---: | ---: | ---: |
| 270 s | 43.5% | 45.4% | 7.1% | 5.1% |
| 285 s | 36.4% | 39.1% | 9.1% | 5.7% |
| 300 s | 36.8% | 41.2% | 8.8% | 5.7% |

These are three individual captures of changing music at nominal wall-clock
checkpoints, not a phase-locked throughput benchmark. All captures are kept.
The native replay supplies the exact PCM/state comparison. Both hardware
runs add no missing-speaker frames over their observed 240–325 s windows;
earlier setup/reset/loading misses are not included in that statement.

## Full-app result and decision

All ten fixed fixture checkpoints pass DOC readbacks, VRAM/source-picture
checks and initialized-register comparisons. The run collects 42 complete
core-1 captures / 1,344,000 samples and 32 timing windows. Every observed
missing-speaker delta is zero. FPS spans about 59.1–59.8 across the different
cases, with like-for-like reference/candidate windows essentially unchanged.

The table keeps all valid captures. Dense reference ranges combine A and C,
which agree on scheduler cost; other reference cases use A only.

| Case | Reference idle range | Span idle range | Reference scheduler range | Span scheduler range |
| --- | ---: | ---: | ---: | ---: |
| ROM | 16.5–17.0% | 15.5–17.1% | 0.2–0.3% | 0.2–0.3% |
| Quiet SHR | 60.7–63.3% | 63.0–63.2% | 0.2% | 0.2% |
| One active voice, 32 enabled | 55.2–55.7% | 54.1–57.2% | 5.8–6.0% | 4.1–4.3% |
| 32 active voices, static | 37.5–38.2% | 37.5–39.4% | 7.0–7.2% | 5.0–5.3% |
| 32 active voices, animated 640 | 17.3–19.0% | 19.9–20.3% | 7.5–8.0% | 5.8–5.9% |

Dense static median idle rises 37.9 → 39.4%; animated median rises
18.25 → 20.1%. Runtime variation remains visible: the low-idle candidate
static capture spends 2.0% in `esp_crosscore_int_send`. ROM median idle is
lower (16.7 → 15.5%) while its ranges overlap. The decision rests on the
consistent active scheduler reduction, exact outputs, unchanged capped
frame rates and the complete set of controls. It does not imply that every
capture or program is faster.

The next audio target to investigate is oscillator register/address loading:
`clem_ensoniq_oscillator_cycle` remains about 12.5–12.7% in the dense static
candidate and 14.2–14.3% in animated 640. Inspect metadata placement and
address generation before adding cached derived state. Any change needs
canonical state/IRQ/PCM replay and its own kernel/full-app measurements.

[Aggregate measurements](measurements/doc-span-integration-2026-11-09.json).
Private media, screenshots and device logs remain outside the repositories.

The fresh 16 MB MSX backup is restored and passes full-flash digest
verification. After that verification restart, Disk BASIC is started again;
guest and LCD captures exactly match the restored controls. Test media copies
are verified and removed, with original SD media untouched. Board logs show
no panic, assertion or watchdog failures.
