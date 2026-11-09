# DOC real-software replay — 2026-11-09

The retained waveform-pointer and normalization changes pass an exact native
replay of the Tour of the Apple IIGS, booting its 800 KB floppy and reaching
the mouse tutorial after the introductory music. This adds real software,
disk access, interrupts and changing oscillator programs to the earlier
synthetic checks. It does not establish a real-program speedup.

## Native comparison

Reference audio source: `8c6b241`, before either retained DOC optimization.
Candidate: `172bf62`. The rest of the emulator and the host driver are the
same between runners. Both use 1 MB RAM, ROM 3, a fixed clock, assertions
enabled and independent disposable copies of the same disk image.

Across 21,601 emulated frames, the complete stereo PCM stream and final CPU
registers/flags/cycles, pointer-free DOC state, configured RAM, Mega II RAM,
RGB565 frame and resulting disk bytes match exactly. Neither replay changes
its disk image. The original media is never mounted by the native harness.
At frame boundaries, 3,134 frames have at least one enabled, unhalted
oscillator with nonzero volume; the maximum is 16. The audio stream contains
11,663,730 stereo sample frames, 3,399,624 nonzero scalar samples and a peak
absolute sample value of 6,000. These counts include the complete boot, not
just the music. Frame-boundary activity is a workload check, not a complete
trace of oscillator transitions within a frame.

The reusable `tools/doc_software_test.py` builds both runners, substitutes
only historical `clem_audio.c`, mounts fresh copies, records audio and state,
and compares their hashes and activity summaries. It rejects media-insert
script commands and requires active DOC audio. Its private output directory
must be outside the project and shared-runtime repositories. For example:

```sh
python3 tools/doc_software_test.py \
  --media /private/path/tour.po \
  --reference-revision 8c6b241 --frames 21601 \
  --output /tmp/gs-doc-software-replay
```

Media, ROMs, audio, screenshots and raw device logs are not distributed.
The public measurements contain only aggregate results.

## Tab5 check and next target

An isolated-settings P4 build uses the normal media folder only to retrieve
an image and create a uniquely named throwaway copy. The original image is
not inserted. The copy boots through floppy loading, the Apple logo/music
and the opening tutorial. Three complete 32,000-sample core-1 captures during
the music (96,000 samples total) show:

| Flat function share | Range |
| --- | --- |
| `esp_cpu_wait_for_intr` | 38.2–39.3% |
| `clem_ensoniq_sync` | 8.7–9.0% |
| `clem_ensoniq_oscillator_cycle` | 2.8–4.1% |
| `clem_ensoniq_voices` | 2.6–3.2% |

These are function PC samples, not inclusive call costs. Five speaker
checkpoints from nominal seconds 240 through 325 all retain the same
cumulative missing-frame count (58,545): no new misses in that observed
window. This is not a zero-miss claim for boot; the cumulative count already
includes setup, media insertion/reset and earlier loading. This is a
candidate-only hardware check, not a matched hardware A/B timing comparison.
S3 runtime is untested. Normal and isolated P4 builds pass; embedded
production source is unchanged after the earlier integration commit.

The next candidate is the scheduling loop in `clem_ensoniq_sync`, rather
than another normalization change. Investigate batching inactive slots and
its cycle-position arithmetic while preserving slot order, halted M0
resets, partner activation, IRQ latching and counter wrap. First compare
against an independent cycle-at-a-time oracle and existing sanitized DOC
mode/state cases, then replay this real software before a matched board
trial. The profile supports investigation; no scheduler speedup is claimed.

[Aggregate measurements](measurements/doc-software-replay-2026-11-09.json).

After testing, the original SD image matches the downloaded bytes, and the
throwaway copy is ejected and removed. The fresh 16 MB MSX backup is restored
and its flash digest verified. After the verification restart, MSX Disk BASIC
is started again; guest and LCD captures exactly match the restored controls.
