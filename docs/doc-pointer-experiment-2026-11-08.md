# DOC waveform-pointer arithmetic trial, 2026-11-08

Select the `select` C form for the next full-emulator A/B trial. In an
isolated P4 pointer kernel it reduces cycles by 13.28–13.30%, with smaller
compiled audio code. No production emulator source is changed yet, and no
application or S3 runtime speedup is established.

The [baseline](profile-baseline-2026-11-08.md) attributes 8.38–9.93% of core 1
to this helper in dense synthetic audio cases. This trial isolates its
address arithmetic, rather than claiming that its saving applies to the
whole oscillator function. [Measurements](measurements/doc-pointer-experiment-2026-11-08.json)
include all ABBA timings and target object sizes.

## Candidates and equivalence

The original `_clem_ensoniq_calc_waveform_ptr` masks the accumulator to
24 bits, shifts by resolution, keeps 16 bits, then shifts by `8-size` and
keeps 15 bits. Size is 0–7 and resolution 1–8. The resulting offset has
`8+size` bits; its highest source bit is `resolution+15`, at most 23.
Combining the shifts and retaining just that offset makes the preliminary
24-bit mask unnecessary, even if ignored accumulator bits are set.
The pointer-register base contributes only bits above the offset.

Three experimental forms in `tools/doc_pointer_variants.h` preserve this:

- `combined`: one accumulator shift, an arithmetic base mask and AND/OR merge.
- `select`: one accumulator shift and an XOR masked selection between base
  and offset; an arithmetic offset mask replaces the original lookup.
- `table`: combined shift and AND/OR merge, retaining an eight-entry mask table.

There is no cached derived state or register-write invalidation scheme.

## Correctness checks

`tools/doc_pointer_test.py` extracts the current production helper and mask
table into its reference translation unit; it does not maintain a handwritten
copy. The independent oracle chooses output bits individually. All four
implementations pass **6,242,880 cases**: every 16-bit source window for all
64 size/resolution combinations, all base bytes with single-set/single-clear
accumulator bits, varying ignored configuration/accumulator bits and one
million deterministic random cases. The 32 oscillator slots are exercised.

`tools/doc_audio_test.py` compiles two separate copies of the complete audio
module, prefixing its public symbols. The candidate copy replaces only the
pointer helper; the reference is current production. Each of the three
candidates passes **224 starting states and 57,344 sync/mix events** under
AddressSanitizer and UndefinedBehaviorSanitizer. Each run observes 11,866
nonzero IRQ returns and 327,629 halted-oscillator observations.

The generated cases use 1/2/3/8/16/31/32 enabled oscillators; free-run,
one-shot, sync and swap control modes; paired AM mixing; zero/nonzero sample
data; near-wrap/random accumulators; HALT/restart, size, pointer, frequency
and volume writes; and IRQ-register reads. After operations, the complete
DOC structure must match byte for byte, including registers, sound RAM,
accumulators, prior pointers, IRQ flags/stack and voice floats. Return values
and downmixed floats must match exactly. This is differential preservation
of current behavior, not an independent validation of DOC hardware accuracy.
It does not replace future full-emulator PCM/state replay.

Run from the project root:

```sh
python3 tools/doc_pointer_test.py --output /tmp/gs-pointer-kernels
python3 tools/doc_audio_test.py --sanitize --output /tmp/gs-audio-state
```

## P4 benchmark

Tab5, 360 MHz CPU / 200 MHz PSRAM, IDF 5.5.6, GCC 14.2, `-O2`, normal
jump-table options. The minimal IDF firmware has no emulation, display,
network or audio tasks. It runs on core 1 using the real
`ClemensDeviceEnsoniq` layout. The DOC is allocated in internal RAM and then
PSRAM. Four register patterns cover the baseline's small table/resolution,
mixed configurations, maximum size/resolution near wrap, and seeded random
settings. This is warm-data timing, not a cold-cache or memory-pressure test.

Each timed call evaluates all 32 pointers 512 times (16,384 evaluations),
advances the 24-bit accumulators and adds output pointers into a checksum.
The helper inlines into the loop, as in the production oscillator function.
Setup is outside timing; reference/candidate/candidate/reference use the same
starting state and must return the same checksum. All variants warm up before
measurement. Nine rounds per memory/pattern/candidate give 216 ABBA cases,
864 timed calls. Interrupts remain enabled; medians reduce their effect.

| Candidate | Median cycles per pointer, reference → candidate | Saving across patterns/memory |
| --- | --- | --- |
| combined | 30.40–30.41 → 27.36 | 10.00–10.01% |
| select | 30.40–30.41 → 26.36 | 13.28–13.30% |
| table | 30.40–30.41 → 27.36 | 10.00–10.01% |

The accumulator update, loop and checksum are included. These figures are
kernel costs, not pure helper instruction latency. Identical warm internal
and PSRAM results do not establish equal costs under full-app cache pressure.

For a minimal IDF project, compile `tools/doc_pointer_bench.c` with the four
`*.c` units emitted by `doc_pointer_test.py`, include `tools/` and
`components/apple2gs/clemens/`, require `esp_psram`, and enable P4 PSRAM at
the settings above. Match the component's `-O2 -fjump-tables
-ftree-switch-conversion` flags. It prints `BENCH BEGIN`, 216 rows and
`BENCH END`; each row is memory, pattern, candidate, round, A/B/B/A cycles
and checksum. Assertions must remain enabled.

## Compiled footprint and next step

Both target toolchains compile all variants, including complete audio-module
objects using each project's existing `compile_commands.json` flags. These
are object-symbol sizes, not a linked-image flash delta.

| Target | Reference audio text → select | Oscillator text | Write-data text | Oscillator stack |
| --- | --- | --- | --- | --- |
| P4 | 3506 → 3456 bytes | 302 → 274 | 326 → 304 | 16 → 0 bytes |
| S3 | 3138 → 3120 bytes | 277 → 269 | 276 → 266 | 32 → 32 bytes |

The `combined` form saves more object text (60 P4 / 27 S3 bytes), but is
slower than `select` in the P4 kernel. Choose `select` for the next trial;
retain the other forms as comparisons. S3 runtime might choose differently.

Next replace the production helper in an isolated candidate build, preserving
a baseline image. Compare all six native DOC/SHR fixtures' PCM, VRAM, RGB565
and state, then full-app P4 A/B profiles and timing for quiet, sparse and
dense voices plus animated SHR. Rebuild both full S3/P4 applications before
retaining the change. Include a ROM startup control and restore Tab5 from
its fresh full-flash backup. The observed helper cost suggests a modest
headroom opportunity; it does not justify a percentage application-speed
claim or an assembly implementation yet.

Tab5 is restored from the fresh pre-test 16 MB MSX full-flash backup with
hash verification. MSX Disk BASIC is running; final guest and LCD captures
match the previous restored control outside the cursor rectangle (see JSON
for exact equality). No ROMs, raw device logs or backup image are committed.
