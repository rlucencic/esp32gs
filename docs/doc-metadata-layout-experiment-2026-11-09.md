# DOC metadata layout trial — 2026-11-09

Keep the production layout from `85d6204`. Moving the 64 KB sound RAM array
after the metadata saves code but regresses the dense P4 scanline kernel by
about 3%. Short calls improve, so smaller code is insufficient evidence for
adoption. No production header or embedded runtime source changes in this trial.

## Candidate and comparison format

The isolated candidate moves only `sound_ram[65536]` to the end of
`ClemensDeviceEnsoniq`. The reference already has the local oscillator helper,
span scheduling, pointer simplification, normalization table and IRQ tail fix.
Both DOC structures remain 66,128 bytes; sound RAM moves from offset 72 to 592.
Metadata now fits shorter address offsets, while sound RAM needs a different
base. No derived caches, allocation or emulated behavior is added.

`tools/doc_state.h` and `doc_snapshot.h` describe all fourteen DOC fields using
little-endian integer values, exact 32-bit float payloads, normalized booleans
and no native padding. The 66,128-byte output is a test comparison format,
not a supported save-state ABI. Static module quiet/running caches are not
structure fields; their consequences are exercised by later calls and mixing.
The schema guard rejects changed field names, types or array counts.
`doc_state_test.c` checks every field, array endpoints, known byte order,
NaN payload preservation, buffer bounds and independence from native offsets.
It passes under ASan/UBSan for both layouts (21 mutation checks each).

The existing fixture and software replay tools now use canonical DOC snapshots.
Old raw DOC hashes must not be compared with these new-format hashes. The
retained firmware and prior optimization measurements remain unchanged.

## Correctness

`tools/doc_layout_test.py` builds each complete audio module with its own
isolated headers. Test adapters copy fields into/out of a common state and
compare canonical bytes after operations. Fail-fast ASan/UBSan checks pass:

- 224 starting states / 57,344 mode, sync, mixing and IRQ events;
- 24,576 boundary cases / 245,760 sync/mix/IRQ-read calls;
- independent FIFO-model IRQ checks: 18,000 events for each layout.

`tools/doc_layout_replay.py` rebuilds each complete native machine core against
its own headers, avoiding mixed-layout object files. Six fixtures (0/1/8/32
static voices and animated SHR 320/640 with 32 voices) match complete PCM,
CPU registers/flags/cycles, canonical DOC state, configured 1 MB RAM, 128 KB
Mega II RAM, VRAM and RGB565. A 21,601-frame Tour replay also matches PCM,
CPU/DOC/RAM/VRAM/pixels and resulting disposable media bytes. Source media is
never mounted and is hash-verified unchanged. A separate Tour run verifies
that the updated existing software replay tool still matches the retained
revision. The normal production `make profile` target builds and its dense
fixture matches the isolated retained runner in every recorded output.

## Code generation

Normal target flags and GCC 14.2, audio-module objects:

| Target / form | Audio text | Scheduler text | Scheduler frame |
| --- | ---: | ---: | ---: |
| P4 retained | 3,424 bytes | 846 bytes | 32 bytes |
| P4 metadata first | 3,186 bytes | 744 bytes | 16 bytes |
| S3 retained | 3,048 bytes | 770 bytes | 48 bytes |
| S3 metadata first | 2,858 bytes | 744 bytes | 64 bytes |

P4 saves 238 text / 16 stack bytes. S3 saves 190 text bytes but grows the
scheduler frame by 16 bytes. Both target objects compile; no full candidate
firmware integration or S3 runtime measurement is claimed. The P4 benchmark's
scheduler object sizes match the isolated normal-flags objects (846 / 744).

## P4 kernel timings

Temporary minimal firmware at 360 MHz, PSRAM/XIP enabled, `-O2` with the
normal jump-table/switch flags. Each timing executes 512 complete scheduler
calls through the same volatile function-pointer path; setup, snapshots and
hashes are outside the timed region. Two data placements, seven scenes,
three deltas and nine ABBA rounds produce 378 cases, 1,512 timings and
774,144 timed calls. All canonical final-state hashes agree within each
ABBA group. All timings are retained, without filtering outliers.

Median cycle savings below span internal RAM and PSRAM; negative means slower.
The scanline delta is 182,400 clocks, usually 57 oscillator slots.

| Scene | One-slot delta | Ten-slot delta | Scanline delta |
| --- | ---: | ---: | ---: |
| Quiet halted M0 | 7.5–9.5% | 9.3–12.9% | 7.2–9.8% |
| 32 enabled, one active | 16.0–18.4% | 10.0–10.5% | 3.1–3.3% |
| 32 enabled, eight active | 15.5–17.8% | 5.5–6.4% | 0.1–0.2% |
| 32 enabled, 32 active | 13.8–14.2% | 1.2–1.4% | −3.1 to −3.0% |
| Swap pairs | 11.9–15.0% | 5.5% | 1.2% |
| One enabled oscillator | 13.5–17.5% | 7.5–8.0% | 1.6% |
| Eight enabled, sync mode | 13.5–14.9% | 2.3% | −2.5 to −2.2% |

The preceding Tour scheduler trace showed active calls advancing 32–63 slots.
That makes scanline behavior more relevant than the strongest short-call
numbers. Defer this candidate: it provides no demonstrated dense-workload
speedup and does not justify full-app integration. No uncapped throughput,
whole-app improvement or S3 speedup is established.

[Aggregate measurements](measurements/doc-metadata-layout-experiment-2026-11-09.json).
Private ROMs, media, backups and raw serial logs stay outside the repositories.
No SD media is mounted by the temporary benchmark.

## Reproduction and next target

Run the native differential checker with assertions and sanitizers:

```sh
python3 tools/doc_layout_test.py --sanitize --output /tmp/doc-layout-check
python3 tools/doc_load_codegen.py --reference-revision 85d6204 --output /tmp/doc-layout-codegen
python3 tools/doc_layout_replay.py --media /private/tour.po --output /tmp/doc-layout-replay
```

The replay requires cc65 (`--ca65` / `--ld65` can specify paths) and private
project ROMs. To generate a minimal benchmark project, pass the successful
check directory and a P4 benchmark SDK configuration with 360 MHz CPU,
200 MHz PSRAM, PSRAM instruction/rodata XIP, assertions, USB serial console
and a custom partition table enabled:

```sh
python3 tools/doc_layout_bench.py --generated /tmp/doc-layout-check --sdkconfig /private/benchmark-sdkconfig --output /tmp/doc-layout-bench
idf.py -C /tmp/doc-layout-bench -DIDF_TARGET=esp32p4 build
```

Flashing remains a separate operation requiring preservation/restoration of
board firmware/settings. The generator does not flash. The next bounded
optimization target is the shared 65C816 fetch path: after retained DOC
improvements it accounts for about 13% of core 1 in the animated fixture,
plus about 3% for `fetch16`. Establish bus/timing/interrupt equivalence before
changing this shared code or interpreting a synthetic result as a program gain.

The fresh full 16 MB MSX backup is restored and independently flash-verified
with a matching digest. A serial-port conflict during the first verification
attempt is resolved by stopping the stale capture process and verifying
again with exclusive access. After verification restarts the board, Disk BASIC
is started again; final guest and LCD captures both match the known controls
exactly. No benchmark firmware remains active.
