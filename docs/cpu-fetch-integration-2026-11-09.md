# 65C816 combined fetch integration — 2026-11-09

Reference: machine `99b32f1`, shared core `dbf4811`. Candidate: the combined
cached-word / split-byte fetch form from the [kernel trial](cpu-fetch-experiment-2026-11-09.md).
The retained DOC helper and original DOC layout are identical in both images.

## Scope and correctness evidence

A temporary project-local `w65816` component overrides the shared component
for both reference and candidate builds, preserving the normal component
CMake flags, headers, debugger adapter and jump-table linker fragment. The
build checks its compile database to verify the selected source path. Only
the candidate CPU source differs between the board images. The board images were built without changing shared runtime source. The
byte-identical candidate was then applied to shared source for production
checks; temporary-component removal and the actual shared source path are
verified before the normal project builds. Temporary main/settings
edits isolate RAM/BRAM, drive settings and the SD folder; each flash restores
the fresh backup's original NVS bytes. Images start with 1 MB RAM.

The exact candidate source and retained machine match the preceding passing
native proofs, so those unchanged runs are reused rather than rerun:
129,748 fail-fast ASan/UBSan CPU/RAM/ordered callback comparisons with debugger
register edits; existing IRQ/WAI/STP/MVN/penalty/WDM suites; six complete-core
fixtures; and the 21,601-frame Tour replay. An earlier debugger-disabled
check passes 131,046 comparisons. Native replay matches PCM, all recorded CPU
fields/cycles, canonical DOC state, configured RAM, Mega II RAM, VRAM, RGB565,
resulting disposable media and active DOC frame counts. Source media is never
mounted and is verified unchanged. The external SingleStepTests dataset is
not rerun here. Candidate source hashes are verified against these artifacts.

The actual production host CPU object (the normal host `-O2` build) is also
compared against the frozen reference CPU compiled at `-Os`. All six fixtures
and the 21,601-frame Tour replay match the same complete outputs, including
nonzero audio and active DOC frames. `w65816_fetch_replay.py --production-only`
reproduces that check.

## Build and code-size checks

Isolated reference/candidate P4 images, plus normal candidate P4 and S3 images,
use the temporary component with the retained normal compiler flags. The
project-local override is removed after building. All 24 production
project/board builds pass, including GS on S3/P4 through the actual shared
CPU source. The other thirteen project working trees are unchanged. No
compiler or linker options are changed to manufacture the result.

| Linked P4 function | Reference | Candidate |
| --- | ---: | ---: |
| `w65816_run` | 20,710 bytes | 21,344 bytes |
| `fetch` | 72 bytes | absent |
| `fetch16` | 48 bytes | 136 bytes |
| `fetch_slow` | absent | 50 bytes |
| `refill` | 130 bytes | 130 bytes |
| Aligned image | 2,009,104 bytes | 2,010,128 bytes |

Normal CPU object text grows 1,020 P4 / 926 S3 bytes; run and fetch-helper
reported frames stay unchanged. See the kernel report for all object sizes
and frame values. S3 runtime remains unmeasured.

## Profiling and board method

`tools/profile_inline.py` now counts each sampled PC once if a DWARF source
frame belongs to `w65816.c`. This CPU core union includes fetch code inlined
into addressing modes or the run loop. It excludes machine callback code
outside the CPU module. A second union identifies fetch/fetch16/fetch_slow/
refill ancestry within that module. Inline ancestry describes source inlining,
not a runtime call stack. Generic function names in unrelated files cannot
inflate the CPU union. Physical and leaf attribution totals must each equal
32,000 per complete capture.

Two previous media-free captures verify that the new CPU union equals the
sum of physical CPU-object function samples, while preserving DOC attribution.
ROM and Tour also exercise CPU and machine callbacks with identical helper
names (notably `bus_read` / `bus_write`), so flat name totals cannot validate
those cases. Each new capture is independently checked against the exact
CPU object text ranges in the linked map, avoiding those name collisions.
The new metric avoids treating disappearance of the physical `fetch` symbol
as a speedup. Raw PCs and detailed attribution stay outside the repositories;
public measurements contain aggregate counts and function shares.

Reference A and candidate B run ROM, quiet/sparse/dense static SHR and animated
SHR 640, then boot fresh Tour copies. Reference C repeats the dense static and
animated fixtures. Each synthetic case has three 32,000-PC core-1 captures
and two approximately ten-second timing windows. Fixed paused checkpoints
verify guest DOC readbacks, VRAM, independent source/palette pixel decoding
and initialized CPU fields. Fixture Y is inherited from ROM and unused;
cumulative live cycles also vary. Native replay compares all recorded fields.

Tour uses nominal reset-relative checkpoints at 240/270/285/300/325 seconds
and music profiles at 270/285/300. Live phases are not locked to native
execution. The observed window starts after loading has already accumulated
any setup/reset misses; only the window's missing-speaker delta is compared.
Synthetic timing windows precede profiling. FPS is capped near 60; CPU sample
share and idle headroom do not measure uncapped emulator throughput.

Private firmware backups, ROMs, media, PCM, snapshots, raw PCs and serial /
network logs remain outside the repositories. The benchmark and full-app
trial never mount original SD media. Tour is mounted only as a verified copy
inside the isolated folder, then ejected, verified unchanged and removed.

## Completed A/B measurements

The candidate's five synthetic cases and Tour complete without added misses
in their observed timing windows. All eight available fixture checkpoints
and all five Tour guest pairs / final LCD pair match. The candidate lowers
CPU core sample share and raises idle headroom:

| Workload / metric | Reference A | Candidate B |
| --- | ---: | ---: |
| ROM CPU core | 39.27–39.83% | 36.94–37.56% |
| ROM idle | 16.1–16.4% | 20.1–20.5% |
| Quiet static CPU core | 26.59–26.89% | 21.09–21.35% |
| Quiet static idle | 63.2–63.5% | 68.9% |
| One-voice static CPU core | 26.02–26.45% | 21.51–21.82% |
| One-voice static idle | 54.4–57.2% | 59.5–62.0% |
| Dense static CPU core | 26.51–26.82% | 21.49–21.64% |
| Dense static idle | 44.9–45.2% | 50.1–50.2% |
| Animated 640 CPU core | 38.01–38.70% | 29.91–30.37% |
| Animated 640 idle | 25.4–25.9% | 33.5–33.9% |

Tour music checkpoints (270/285/300 seconds): CPU core 30.53/30.50/30.49%
versus 27.06/27.24/26.91%; idle 45.7/43.1/43.2% versus 49.0/45.9/46.6%.
The live phases limit precise per-call interpretation despite exact displayed
stages and exact native PCM replay. These are capped-FPS CPU-share/headroom
measurements, not an uncapped program-throughput gain.

The final reference C startup was interrupted when Tab5's USB device
disappeared from Linux. Its Wi-Fi status endpoint still responds, but the
reference checkpoints and full MSX flash restoration require the USB data
connection. Final drift verification and restoration remain pending until
that connection is restored. Both Tour test copies are already verified
unchanged and removed; the fresh full backup is preserved outside the repo.

[Current measurements](measurements/cpu-fetch-integration-2026-11-09.json)
record the completed A/B cases and explicitly mark the pending reference /
restoration checks.
