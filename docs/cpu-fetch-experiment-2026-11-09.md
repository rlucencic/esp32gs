# 65C816 fetch trial — 2026-11-09

Take the combined cached-word / split-byte fetch candidate to full-app testing.
P4 mixed-code kernel cycles fall 18.7–20.3%; bus-only execution costs about
1.8% more. This is a validated isolated candidate, not a retained shared CPU
change. Production runtime remains at shared revision `dbf4811`; machine
reference is `99b32f1` (the retained DOC optimizations, original DOC layout).

## Candidates and constraints

The baseline profiles put `fetch` near 13% of core 1 and `fetch16` near 3%
in the animated IIgs fixture after DOC improvements. Helpers are deliberately
compact at `-Os`; forcing the whole CPU inline previously harmed S3 instruction
cache behavior. This trial changes only two helpers, in isolated source copies:

- **pair:** `fetch16` checks whether two bytes remain in the cached code page,
  loads both as bytes, advances the pointer by two and counts two cycles;
  otherwise it uses the original two `fetch` calls.
- **split:** put the existing refill/update path in `fetch_slow` and force
  only the small `fetch` wrapper inline. Cached byte reads avoid a helper call;
  page/refill/bus accesses still use the original `refill` implementation.
- **combined:** both changes.

No unaligned word load, CPU layout/API change, page-table cache or emulated
prefetch is introduced. The word fast path applies only within a directly
mapped page. It cannot skip a bus callback or cross a page/bank boundary.
Instructions that interleave operand fetching with stack accesses retain their
original individual `fetch` calls. Table changes through callbacks still
invalidate the cached instruction page before the next fetch. Direct memory
is owned by the emulation task; the documented core API permits remapping
through callbacks/between runs, not concurrent page-table mutation.

## Correctness

`tools/w65816_fetch_trial.py --check --sanitize` compiles the frozen reference
and all candidates together with assertions, ASan/UBSan and `-Os`.
`w65816_fetch_check.c` compares exact recorded CPU fields, complete main and
alternate RAM, returned cycles and ordered bus/debugger callbacks including
address, value, read/write kind and callback cycle. The final debugger-enabled
run passes **129,748 comparisons**:

- 40,960 opcode/mode/location/mapping cases per candidate: all 256 opcodes,
  native M/X combinations and emulation mode, page endings and bank wrap;
- 256 random seeds per candidate with repeated variable-budget runs;
- mapped and callback-only memory, remaps during callbacks, wait states,
  IRQ/NMI, end-run, WDM and debugger register edits/reload;
- initial cycle count near 32-bit wrap, compared as the core's 64-bit count.

The existing IRQ/WAI/STP/MVN/penalty/WDM suite passes for the reference and
all three candidates. An earlier debugger-disabled differential run passes
131,046 comparisons. These are comparisons against the retained core and its
existing IRQ tests; the external SingleStepTests data suite is not rerun here.

`tools/w65816_fetch_replay.py` links each CPU object into the complete retained
IIgs native machine. All three candidates match the reference on six fixtures
(static 0/1/8/32 voices and animated SHR 320/640 at 32 voices), and on a
21,601-frame Tour boot/music replay. Outputs include complete PCM, final CPU
registers/flags/cycles, canonical DOC state, configured 1 MB RAM, Mega II RAM,
VRAM and RGB565; Tour also compares resulting media bytes and active DOC frame
counts. Both active DOC frames and nonzero PCM are required. Original private
media is never mounted and is hash-verified unchanged; each runner uses a copy.
No ROM or media contents are included in public measurements.

## Code size and frames

Normal target flags, GCC 14.2, CPU object text (excluding read-only tables):

| Target / form | CPU text | Run text | Run stack |
| --- | ---: | ---: | ---: |
| P4 reference | 29,238 bytes | 25,314 bytes | 128 bytes |
| P4 pair | 29,276 bytes | 25,314 bytes | 128 bytes |
| P4 split | 30,222 bytes | 25,948 bytes | 128 bytes |
| P4 combined | 30,258 bytes | 25,948 bytes | 128 bytes |
| S3 reference | 24,691 bytes | 21,421 bytes | 96 bytes |
| S3 pair | 24,736 bytes | 21,425 bytes | 96 bytes |
| S3 split | 25,593 bytes | 22,017 bytes | 96 bytes |
| S3 combined | 25,617 bytes | 22,005 bytes | 96 bytes |

The combined form grows CPU text 1,020 P4 / 926 S3 bytes. Reported run and
fetch-helper frames remain unchanged: P4 refill/fetch-or-slow/fetch16 frames
are 16/32/16 bytes; S3 48/48/32. This is static frame reporting, not a measured
whole-program stack high-water mark. Both target CPU objects compile. Normal
candidate firmware builds remain part of integration; S3 runtime is unmeasured.

## P4 complete-CPU kernel timing

The generated minimal firmware uses the retained `test/speed.c` assembler and
additional loops. All four CPU modules use the normal `-Os` and jump-table
flags, compiled debugger guard, PSRAM instruction/rodata XIP and 360 MHz CPU /
200 MHz PSRAM settings. Each module's 1 KB opcode jump table is mapped to
internal RAM, verified in the link map. Benchmark CPU object text/run sizes
exactly match the normal-flags probes above. Page-pointer tables stay in PSRAM; guest
memory is tested in internal RAM and PSRAM. Normal scenes have no active
debugger map; the debugger control uses a non-null empty map.

Each timing executes 128 complete CPU runs with budgets of 180 or 1,000 guest
cycles. Every run starts at an instruction boundary and may exceed its budget
by the final instruction, as in production. Setup, 16 warm-up calls, complete
memory and CPU/bus-trace hashes are outside the timer. Two memory placements,
eight scenes, two budgets, three candidates and seven ABBA rounds produce
**672 groups / 2,688 timings / 344,064 timed run calls**. Every group agrees
on final hashes. All timings are retained, without filtering outliers.

Median cycle savings below span both placements and budgets. Negative is slower.

| Scene | Pair | Split | Combined |
| --- | ---: | ---: | ---: |
| Mixed native/8-bit/emulation code | 7.3–8.2% | 15.8–16.9% | 18.7–20.3% |
| 16-bit immediate loop | 20.5–20.9% | 18.5–19.5% | 27.1–27.4% |
| 8-bit immediate loop | 3.6–5.1% | 18.3–19.2% | 19.8–21.9% |
| NOP / short branch control | 0.0–0.3% | 22.6–24.7% | 22.1–24.7% |
| Operand crossing code-page boundary | 12.5–13.3% | 13.2–14.3% | 17.0–17.7% |
| Mixed code through bus only | −0.9 to −0.8% | −1.5 to −1.4% | −1.9 to −1.8% |
| Mixed code with callback writes | 6.8–7.7% | 14.9–16.1% | 17.9–19.6% |
| Mixed code with debugger map | 5.0–5.1% | 11.1–11.6% | 13.4–13.9% |

The near-zero pair result in the NOP control is consistent with its limited
use of word operands. Both new branches and the slow wrapper have a small
cost when every fetch is a callback. Real IIgs software mostly executes
mapped code, but a full-app measurement must establish the net benefit.
These cycle savings are complete CPU kernel results; they are not whole-app
speedups, uncapped throughput measurements or S3 runtime results.

[Aggregate measurements](measurements/cpu-fetch-experiment-2026-11-09.json).
Private ROMs, copies, snapshots, audio, firmware backups and raw device logs
remain outside the repositories. The temporary benchmark never mounts SD media.

## Reproduction and next step

```sh
python3 tools/w65816_fetch_trial.py --output /tmp/cpu-fetch --codegen --check --sanitize
python3 tools/w65816_fetch_replay.py --generated /tmp/cpu-fetch --fixtures /tmp/gs-fixtures --media /private/tour.po --output /tmp/cpu-replay
python3 tools/w65816_fetch_bench.py --generated /tmp/cpu-fetch --sdkconfig /private/benchmark-sdkconfig --output /tmp/cpu-bench
idf.py -C /tmp/cpu-bench -DIDF_TARGET=esp32p4 build
```

Fixture inputs are the six named binaries from `profile_fixture.py` (cc65).
Codegen needs existing normal P4/S3 compile command databases. The benchmark
configuration needs P4 PSRAM/XIP, 360 MHz CPU, 200 MHz PSRAM, assertions, USB
serial console and a custom partition table. Generating the project never
flashes it; preserve/restore the board separately before hardware testing.

Next integrate only the combined form into an isolated full-app build, repeat
reference/candidate/reference measurements including quiet, dense, animated
and real Tour audio, and account for changed inline attribution. Confirm
instruction cache behavior and callback-heavy controls before retaining it.
Changing shared runtime source requires the normal project/board build matrix;
this trial changes only IIgs-side test tools and reports.

The fresh full 16 MB MSX backup is restored and independently flash-verified
with a matching digest. After verification resets the board, Disk BASIC is
started again. Final guest and LCD captures match the known controls exactly.
No benchmark firmware remains active; the original SD media is untouched.
