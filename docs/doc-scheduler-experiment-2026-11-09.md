# DOC scheduling trial and IRQ queue fix — 2026-11-09

The slot-span scheduler is worth a full-emulator A/B trial. Its isolated P4
scanline-call timings save about 13% with 32 active voices and 21–30% with
sparse voices, compared with the existing cycle-at-a-time loop. It is not
integrated into production: short calls regress and its stack frame grows.
The only embedded production change in this step fixes an IRQ queue bug
exposed by the expanded validation.

## IRQ queue correctness

When all 32 oscillators have pending interrupts, `_clem_ensoniq_next_irq`
shifts entries 1–31 down after acknowledgement, but previously left entry
31 occupied. The acknowledged oscillator can interrupt again; its flag was
cleared, while the duplicate tail still makes the queue look full. The next
append can then write `osc_stack[32]`, outside the 32-byte array. The new
scheduler boundary tests exposed this in both the committed reference and
experimental modules under UBSan. Their initial run is not counted as a
sanitizer pass.

Clear the vacated tail after shifting. Shorter queues already shift an
empty entry, so this preserves their behavior and makes a full queue release
one slot. The focused `tools/doc_irq_queue_test.c` fills every depth 1–32,
checks duplicate suppression, repeatedly acknowledges and re-enqueues an
oscillator, then drains the queue and reads once more. An independent FIFO
model checks queue bytes, pending flags and the current OIR after every event.
It fails against committed `0222626` and passes 18,000 checked events with
the fix, both native ASan/UBSan and on P4. Sanitized tests now stop at the
first sanitizer error rather than relying on the process exit after recovery.

The existing 224-state / 57,344-event native DOC mode/IRQ/mixer comparison
against `0222626` still passes. A 21,601-frame Tour replay with the production
fix exactly matches that commit's PCM, final CPU/DOC/RAM/pixels and resulting
media. Its private source image is unchanged. Normal S3 and P4 builds pass.

## Scheduler candidates

`tools/doc_schedule_test.py` snapshots the complete current audio module,
leaving its production loop untouched, and emits independently prefixed
reference/candidate modules. Both trial baselines include the IRQ tail fix:

- **Counter**: advance the unsigned cycle counter once per sync instead of
  storing it after every slot. Oscillator/IRQ helpers do not read that counter.
- **Span**: also process the oscillator portion up to the next boundary in
  one loop, then consume the two padding slots together. Oscillator visits,
  halted M0 resets, partner activation and IRQ ordering remain sequential.

Each form passes the existing 57,344-event sanitized DOC comparison, plus
`tools/doc_schedule_check.c`: 24,576 starting cases / 245,760 sync, mix and
IRQ-read calls. It covers every legal enabled count 1–32, all four modes,
halted/sparse/dense patterns, zero samples, starting/padding boundaries,
32-bit counter wrap, duration-budget wrap, partial/full rounds, quiet caching
and restart writes. ASan/UBSan is configured to halt on errors. The reference
keeps the original per-slot loop and counter stores.

Both generated modules pass the complete 21,601-frame native Tour replay
against `0222626`. `doc_software_test.py --candidate-audio <generated-module>`
links a candidate without editing production source or mounting original
media. The separate production-fix replay establishes that the IRQ fix alone
does not change this workload's outputs.

## P4 kernel measurement

Temporary minimal IDF firmware measures the complete scheduler, using nine
ABBA rounds per case, 512 sync calls per timing, internal RAM and PSRAM,
seven oscillator scenes and three clock deltas. There are 756 ABBA cases /
3,024 measured timings / 1,548,288 scheduler calls. Every four-run group
matches a hash of the entire final DOC state. The queue regression also runs
on this firmware. Production-build flags use GCC 14.2, `-O2`, P4 360 MHz.

Scanline-sized calls use 182,400 clocks, usually 57 oscillator slots. Values
below are median cycle savings; ranges cover the two memory placements.

| Scene | Counter | Span |
| --- | ---: | ---: |
| 32 enabled, one active | 8.6–8.8% | 29.5–30.5% |
| 32 enabled, eight active | 7.3–7.9% | 21.3–21.4% |
| 32 enabled, 32 active | 6.4–7.6% | 12.9–13.0% |
| Swap pairs | 5.3–7.2% | 22.5–23.9% |
| One enabled oscillator | 12.3–16.4% | 2.7–3.5% |
| Eight enabled, sync mode | 6.6–7.1% | 11.7–12.1% |

Controls matter: with a one-slot call, span is 3.8–14.9% slower in active
scenes. The already-quiet case is 4–8% slower for counter and 4–6% slower for
span in this build, despite retaining the same early exit; layout/call cost
is included in these timings. A whole-application comparison must check quiet
and sparse controls as well as dense audio.

A separate instrumented native Tour replay records 821,738 calls reaching
the active scheduler; all advance 32–63 slots. Its PCM/state/media still
match. This favors investigating span on this workload but does not substitute
for hardware application measurements or characterize other software.

| Target / form | Scheduler text | Stack frame |
| --- | ---: | ---: |
| P4 reference | 614 bytes | 48 bytes |
| P4 counter | 624 bytes | 48 bytes |
| P4 span | 650 bytes | 80 bytes |
| S3 reference | 544 bytes | 48 bytes |
| S3 counter | 538 bytes | 48 bytes |
| S3 span | 558 bytes | 64 bytes |

Next run full-app native synthetic replays and isolated P4 A/B with the span
candidate, including the real Tour music and quiet/short-call controls. Keep
the scheduler experimental until that resolves the stack and control costs.
S3 runtime and full-application speedup remain unmeasured.

[Aggregate measurements](measurements/doc-scheduler-experiment-2026-11-09.json).
Private media, audio and device logs stay outside the repositories.

The Tab5's fresh 16 MB MSX backup is restored and verified by flash digest.
After the verification restart, Disk BASIC is started; the final guest and
LCD captures exactly match the restored controls. No original SD media is
mounted or modified during the kernel trial.
