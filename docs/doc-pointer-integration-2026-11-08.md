# DOC pointer arithmetic integration, 2026-11-08

This trial applies the mask-select C form selected by the
[pointer-kernel experiment](doc-pointer-experiment-2026-11-08.md) to the
production `_clem_ensoniq_calc_waveform_ptr`. The function combines the two
shifts and selects the waveform-offset bits; the old mask table is removed.
There is no derived-state cache or register-write change. The 24-bit
accumulator update, waveform sample order, wrap/mode/IRQ handling and float
mixing are unchanged by the patch.

[Measurements](measurements/doc-pointer-integration-2026-11-08.json) record
native hashes, target builds, fixed board checkpoints and fresh A/B samples.
The earlier 13.28–13.30% P4 result is for a pointer kernel with loop/update
costs included; it is not an application-speed claim.

## Exact native comparisons

Both host executables use the same extended `tools/profile_host.c`; one links
the committed production reference (`8c6b241`) and one the candidate. Each
case boots 360 frames, warms for 120 and captures 600 with fixed RTC input
and no mounted media. All six cases (static 0/1/8/32 voices and animated
320-/640-mode 32 voices) match byte for byte in:

- Signed-16 PCM, all E1 video RAM and RGB565 output.
- CPU registers/flags/cycle count and the complete pointer-free DOC structure.
- All 1 MB of configured RAM and 128 KB of Mega II memory.

The validator still checks the initialization sentinel, real guest GLU
readbacks, live active-voice count, waveform data, volume, SHR mode, silence
and non-clipping PCM. Raw replay artifacts remain outside the repository;
only hashes are published.

The actual production helper passes all 6,242,880 bit-oracle cases again.
The complete production audio module also passes 224 starting states and
57,344 sync/mix events against `8c6b241` under ASan/UBSan, including
free-run/one-shot/sync/swap controls, paired AM mixing, sample termination,
register reconfiguration, HALT/restart, wrap and IRQ acknowledgement. The
three experimental forms pass the same historical-reference checks. State,
return values and mixed floats match exactly. These preserve current DOC
behavior; they are not an independent hardware-accuracy oracle.

The replay tool now saves CPU, DOC, RAM and Mega II snapshots. The audio test
accepts `--reference-revision` and `--variant production`, so a production
change can be compared directly to a committed original. For example:

```sh
python3 tools/doc_pointer_test.py --output /tmp/gs-pointer-check
python3 tools/doc_audio_test.py --sanitize --reference-revision 8c6b241 \
  --variant production --output /tmp/gs-audio-check
```

A first candidate replay launch failed because its copied executable lacked
execute permission. The copy was corrected before any candidate run or board
flash; all six reference/candidate replays were rerun successfully. No result
from the failed launch is included.

## Builds and footprint

Sequential builds pass for isolated reference P4, isolated candidate P4,
normal candidate P4 and normal candidate S3. The isolated images use the same
1 MB RAM default and test settings/media namespace. Production defaults are
restored in the source and the normal builds. Only Tab5 is attached; S3
runtime performance remains unmeasured.

The linked P4 oscillator function falls from 302 to 274 text bytes and
write-data from 314 to 292, with the 16-byte mask table absent. The linked
application binary is 16 bytes smaller after alignment. Normal-project-flags
object analysis gives the same 50-byte P4 audio-text saving and a 16-to-0-byte
oscillator stack frame. S3 audio-module object text falls 18 bytes, with the
oscillator's 32-byte frame unchanged. Object and linked-image sizes are kept
separate; linker relaxation changes individual function sizes.

## Hardware procedure

Tab5, P4 360 MHz / PSRAM 200 MHz, IDF 5.5.6, GCC 14.2, ROM 3, speaker on.
A fresh 16 MB full-flash backup precedes testing. Reference A and candidate B
each run a ROM startup control, static 0/1/32 voices and animated 640-mode
32 voices. A final reference C repeats static-32 and animated-640 to bracket
the dense cases in time. The original NVS bytes are restored before every
round; all use an isolated test namespace and no mounted media.

For each fixture, debugger reset/360-frame boot, bank-2 load and 120-frame
warm-up reproduce the baseline procedure. While stopped, the harness checks
DOC readbacks and sentinel, captures registers, reads all 32 KB of E1 video
RAM and captures the source picture. The debugger disconnects before live
measurement. Every source pixel is checked against captured video RAM,
SCBs and palette; static pictures also compare against native output. The
640 checker follows the current renderer's quarter order, without claiming
independent IIgs color-rule validation.

Three `i` checkpoints delimit two approximately 15-second profiler-free FPS
windows per case. Three fresh 32,000-sample core-1 captures follow. Reference
A and candidate B each have three additional inline-aware animated-640
captures. The serial info buffer truncates late timing fields, so audio
cost is derived from sampling, not the truncated `kcycles/frame` text.
Sampling observes the frame cap and idle periods; symbol percentages are
wall-time shares, not per-call cycle ratios. DWARF inline ancestry is not a
full caller stack, and nested percentages overlap.

## Hardware results

All ten fixed fixture checkpoints match source pixels to captured VRAM.
Candidate and final-reference checkpoints match reference A video RAM and
pixels exactly. Initialized CPU registers match; cumulative cycle counts
span different live sessions. The fixture never reads or writes Y, so its
inherited ROM value is not treated as an invariant (reference C changes Y
while running the original firmware). Native replays match every saved
CPU field, including Y and cumulative cycles.

The ROM source captures match exactly. Later live LCD captures contain
different positions of the ROM startup animation; they are not simultaneous
fixed-frame comparisons. No LCD animation difference is used as a failure
or as performance evidence.

| Case | Reference A FPS | Candidate B FPS | Reference C FPS | Reference idle range | Candidate idle range |
| --- | --- | --- | --- | --- | --- |
| rom | 59.30 / 59.22 | 59.28 / 59.22 | — | 15.5–16.1% | 15.6–16.2% |
| static-0 | 59.78 / 59.69 | 59.76 / 59.69 | — | 60.2–63.0% | 62.8–63.4% |
| static-1 | 59.76 / 59.68 | 59.76 / 59.69 | — | 54.0–54.6% | 54.9–55.0% |
| static-32 | 59.69 / 59.75 | 59.74 / 59.74 | 59.75 / 59.76 | 27.0–27.4% | 31.4–33.4% |
| animated-640 | 59.63 / 59.59 | 59.55 / 59.69 | 59.61 / 59.66 | 7.3–7.8% | 12.8–13.8% |

All 24 timing windows have zero additional missing speaker frames.
Cumulative counts include startup, resets and debugger pauses. All 42 new
captures are complete (1,344,000 samples).

Dense static oscillator sample share falls from 15.8–16.4% across the two
reference rounds to 12.4–12.5% with the candidate. Animated oscillator ranges
are 16.1–19.3% reference and 14.5–14.6% candidate. The reference attribution
varies more than its idle share; do not turn these into precise per-call
speed ratios. Separate inline captures attribute 8.59–9.47% to pointer
calculation before and 7.27–7.39% after, within oscillator advancement.

**Retain the change:** exact native/state checks, fixed hardware checkpoints,
smaller code/stack and reference-bracketed dense-workload headroom support
the portable C form. FPS remains capped near 60; these synthetic results do
not establish a real-program or S3 runtime speedup. Quiet controls overlap;
the small sparse-audio difference does not isolate a separate application gain.

**Next target:** `clem_ensoniq_voices`, at 10.74–10.88% of core 1 in the new
animated candidate inline samples. Target disassembly shows two `fdiv.s`
instructions per non-skipped P4 voice and two S3 `__divsf3` call sites. Test
a shared 256-entry float normalization table (1 KB) against the exact
current operation order, including AM, zero samples, volume, float
accumulation and clipping. Preserve float/PCM bits, measure the table/cache
tradeoff on P4 and build S3 before considering production or assembly.
Real-software audio and S3 runtime coverage remain open.

Tab5 is restored from the fresh pre-test 16 MB MSX full-flash backup with
hash verification. MSX Disk BASIC is running; final guest and LCD captures
exactly match the prior restored controls, including cursor state. Test
settings are removed by the full-flash restore. Raw logs, ROMs, replay
snapshots and backup images remain outside the repository.
