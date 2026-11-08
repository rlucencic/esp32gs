# IIgs DOC / SHR profiling baseline, 2026-11-08

This is a baseline of the current emulator, before another production
optimization. Original generated workloads exercise active Ensoniq DOC audio
and changing Super Hi-Res video without mounting user media. Results and
fixture hashes are in [the measurement JSON](measurements/profile-baseline-2026-11-08.json).

## Reproduce the native workload

From the project root, with the usual private ROM 3 and character ROM:

```sh
make -C components/apple2gs/host profile
python3 tools/profile_fixture.py --voices 32 --animate --mode 640 \
  --output /tmp/gs-profile.bin
components/apple2gs/host/build/profile_host /tmp/gs-profile.bin 32 1 640 /tmp/gs-profile
```

The assembler wrapper requires cc65's `ca65` and `ld65`; explicit paths can
be supplied with `--ca65` and `--ld65`. No third-party program or disk image
is included. The binary loads at `$02:2000`; the host validator does this
itself. It boots 360 frames, warms the fixture for 120, then captures 600.
Assertions must remain enabled.

The six cases are static 320-mode pictures with 0, 1, 8 or 32 active
oscillators, and animated 320-/640-mode pictures with 32 active oscillators.
Every case enables 32 oscillators (`$E1=$3E`) so the DOC scan denominator is
always 34. Active oscillators free-run, alternate output channels 0/1,
use volume 4 and frequencies `$1400 + 97*i`, and read an original 256-byte
nonzero triangle. The static guest spins in a branch; animation increments
all 32,000 visible SHR bytes in sequence. Neither represents an ordinary
application's instruction mix. Palette 0 and the scanline controls are fixed.

Each case passes twice on the native core, with exact repeat hashes for
PCM, video RAM and RGB565 output. The validator checks live oscillator
controls, wave RAM, volumes, enable count, guest GLU readbacks, mode controls,
non-clipping active PCM, silent quiet PCM, and picture-signature changes for
animation. The peak absolute signed-16 sample is 131 / 992 / 3441 for static
1 / 8 / 32 voices, and 3410 for either animated mode. These are numerical
checks; speaker sound was not assessed by ear.

An early fixture setup used `$C035=$7F`, disabling bank-0 I/O. The native
validator rejected its zero volumes. All reported runs use `$3F`, validated
before flashing. This setup attempt is excluded from results.

## Hardware procedure

Tab5: ESP32-P4 360 MHz, PSRAM 200 MHz, IDF 5.5.6 / GCC 14.2, ROM 3,
1 MB RAM, speaker enabled. The test uses an isolated settings namespace and
media folder. Normal P4 and S3 builds also pass sequentially; no S3 board
was attached, so no S3 runtime result is claimed.

The debugger resets and boots 360 frames, loads the fixture in bank 2,
sets PC to `$02:2000`, then runs another 120 frames. Debugger I/O peeks do
not access devices; the guest records real DOC GLU reads into ordinary RAM
at `$02:3F00`, with initialization sentinel `$A5` at `$02:3FF0`.
The debugger disconnects before live timing and sampling.

For each case, including a no-media ROM startup control, three serial `i`
checkpoints delimit two approximately 15-second profiler-free FPS windows.
Three 32,000-sample captures per core follow, with source and LCD captures.
Sampling runs at 20 kHz and observes the frame cap/idle time; percentages
are shares of wall time, not isolated kernel speedups. Timing windows and
sampling windows are separate. The existing serial info buffer truncates
late `kcycles/frame` fields, so this report does not derive audio timing
from the truncated text. Startup speaker-missing counts are separated from
changes during the steady timing windows.

A separate stopped-frame validation repeats each fixture, checks all DOC
readbacks and the sentinel, reads 32 KB of E1 video RAM, and compares every
source pixel against the captured VRAM, palette and SCBs. Static pictures
also compare against native RGB565 output. The 640-mode checker follows the
current renderer's palette-quarter order (8, 12, 0, 4); it is a consistency
check, not an independent verification of IIgs hardware color rules.

## Baseline results

| Workload | FPS (two windows) | Core 1 idle | DOC scan + advance + mix |
| --- | --- | --- | --- |
| rom | 59.17 / 59.29 | 15.0–15.6% | 0.2–0.3% |
| static-0 | 59.75 / 59.74 | 62.9–63.0% | 0.2–0.2% |
| static-1 | 59.75 / 59.75 | 54.2–54.7% | 8.5–8.7% |
| static-8 | 59.75 / 59.75 | 47.8–48.1% | 14.5–14.5% |
| static-32 | 59.76 / 59.75 | 27.0–28.0% | 34.5–35.5% |
| animated-320 | 59.62 / 59.63 | 7.4–8.3% | 35.5–40.4% |
| animated-640 | 59.67 / 59.62 | 7.3–7.4% | 36.6–38.1% |

All fourteen steady timing windows show zero additional missing speaker frames.
Cumulative counters include startup/reset/debugger pauses and must not be
interpreted as losses during uninterrupted operation. Core 0 remains mostly
idle (see JSON); neither the speaker consumer nor presentation is the first
target in these cases. Full-app timings are frame-capped baseline observations,
not optimization comparisons.

Six additional inline-aware captures (three each for static-32 and
animated-640) isolate waveform-pointer calculation inside oscillator
advancement. It accounts for 8.38–8.65% of core 1 in static-32 and
9.18–9.93% in animated-640, within oscillator advancement's 15.56–16.33%
and 17.85–19.25%, respectively. These are separate captures from the flat
table above. DWARF ancestry identifies inlined helpers, not a full caller
stack: nested percentages overlap and must not be added. In animated-640,
SHR expansion is only 2.34–2.39%, and line hashing 2.45–2.49%.

**Next experiment:** `_clem_ensoniq_calc_waveform_ptr` in
`clemens/clem_audio.c`. First try equivalent portable-C shifts/masks and
register loads, retaining the current function as a differential reference.
Validate all size/resolution/base-pointer combinations and accumulator
boundaries before measuring native and P4 kernel costs. Preserve the current
24-bit accumulator, base alignment, sample ordering, wrap detection and IRQ
behavior. A cached derived pointer needs correct invalidation on register
writes, and is a separate, more invasive candidate. Assembly follows only
if measured C leaves a useful opportunity.

The fixtures cover free-running tones, not one-shot/swap/sync/AM/IRQ behavior.
Before retaining a production audio change, add those differential cases and
repeat PCM/state/pixel replays, hardware A/B timing and S3/P4 builds. Real
software and S3 runtime measurements remain open. No production emulator
source changes in this baseline.

All 48 new profile captures are complete (1,536,000 samples). Private raw
logs, ROMs, full-flash backup and screenshots remain outside the repository.

Tab5 is restored from the fresh pre-test 16 MB MSX full-flash backup, with
flash hash verification. MSX Disk BASIC is running; final guest and LCD
captures exactly match the previous restored control, including cursor state.
The isolated GS settings are removed by the full-flash restore.
