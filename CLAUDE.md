# esp32gs: Apple IIGS on the Makerfabs MaTouch ESP32-S3 7" and the M5Stack Tab5

An Apple IIGS with ROM 3: the 65C816 (esp32emu's `w65816` core) on the
machine's memory map and devices from Clemens (Samir Sinha's IIGS emulator,
MIT, in `components/apple2gs/clemens/`): the Mega II and FPI with shadowing
and the language card, the VGC (all Apple II modes and Super Hi-Res), ADB
keyboard and mouse, the clock with battery RAM, the Ensoniq DOC and the
speaker, the IWM with a 3.5" drive (slot 5) and a 5.25" drive (slot 6), and
a hard disk card in slot 7 with two units. 1 MB on the MaTouch, 1-8 MB on
the Tab5. An LVGL setup screen, a web page for disk images and live status.
ESP-IDF v5.5.4, C.

The board side (tasks, LCD, touch, SD, WiFi, BLE, setup screen, web page,
console) and the 65C816 core are the shared library in `../esp32emu`, also
used by `../esp32ste`, `../esp32mac`, `../esp32cpc`, `../esp32zx`,
`../esp32cbm`, `../esp32xl` and `../esp32msx`. See its CLAUDE.md; changes
there need all projects built, for both boards.

## Hardware

Makerfabs MaTouch ESP32-S3 Parallel TFT with Touch 7", ESP32-S3-WROOM-1-N16R8
(16 MB flash, 8 MB octal PSRAM), 1024x600 RGB565 panel, GT911 touch, microSD,
MAX98357A amplifier. Pins: `../esp32emu/components/emu/include/emu/board.h`.
The native USB port is the console, `/dev/cu.usbmodem1101` on the user's Mac.
The Tab5 is `/dev/cu.usbmodem3101` (its hardware: `../esp32emu/CLAUDE.md`).

## Build, flash, debug

```sh
bash -c '. $IDF_PATH/export.sh >/dev/null 2>&1; \
  export IDF_PYTHON_ENV_PATH=/Users/robertlucencic/.espressif/tools/python/v5.5.4/venv; \
  export PATH=$IDF_PYTHON_ENV_PATH/bin:$PATH; \
  idf.py build && idf.py -p /dev/cu.usbmodem1101 app-flash'
# the Tab5:
  idf.py -B build-tab5 -DIDF_TARGET=esp32p4 -DSDKCONFIG=$PWD/build-tab5/sdkconfig build
  idf.py -B build-tab5 -DIDF_TARGET=esp32p4 -DSDKCONFIG=$PWD/build-tab5/sdkconfig -p /dev/cu.usbmodem3101 flash
```

- `app-flash` writes only the app; `idf.py flash` also writes the ROMs in
  `roms/` (not in git) into the `gs_roms` partition (0x810000, 512 KB):
  `gs_rom3.rom` at 0 (256 KB), `chargen.rom` at 256 KB (top
  `CMakeLists.txt`, `main/rom_store.c`). The partition overlaps the other
  machines' ROM partitions: switching a board needs a full `idf.py flash`
  there (and here). NVS (WiFi, BLE pairings) is shared. Without flashed
  ROMs, `roms/` on the SD card (any case) works too.
- The MaTouch and Tab5 builds share `managed_components/`: building one after
  the other recompiles everything once (~10 minutes).
- `../esp32emu/tools/emu_console.py PORT log N | screenshot F.png | lcd F.png | send "cmd" ... | info | reset | profile ELF [N]`
  with the IDF venv's Python (`-I`). `send` doesn't read replies: start
  `log 3` in the background first to see a command's output (`g`).
- `curl http://apple2gs.local/api/status`; with both boards on, one is
  `apple2gs-2` (use the IPs). Disks: `curl -T img http://.../disks/`,
  `PUT /api/drive/<n>` (1 3.5", 2 5.25", 3 and 4 the hard disks).
- Flashing restarts the board and ends the user's session: say so first.

Console commands: the shared ones (`s` `L` `r` `F` `t` `i` `p`) and the
IIGS's (`main/main.c`): `k <hex>` tap an ADB key (24 Return, 31 space, 35
Esc), `K <hex> 0|1` (36 Control, 37 Command, 38 Shift, 3A Option, 7F Reset:
Control-Reset is `K 36 1`, `K 7F 1`, `K 7F 0`, `K 36 0`), `T <text>` type
(`\n` Return; 3 frames down, 3 up a key), `m dx dy btn` mouse (1 count =
1 Super Hi-Res pixel), `g [hex n]` registers, IRQ lines, the last 32 bus
callbacks and n bytes at an address, `R <MB>` restart with that RAM (Tab5).
`i` also prints the frame profile (see Performance) and, with the speaker
on, the sample frames it found missing since power-on (it holds the last
one: a gap in the sound). Touches: Start
`t 850 532` (MaTouch), `t 1065 637` (Tab5); the Tab5's Memory buttons
`t 351|528|706|884 269`; a touch on the picture moves the IIGS pointer there
(`t x y ms`, one per `send`; a double click needs two within ~0.3 s: write
to the port directly).

## Layout

| Path | What |
|---|---|
| `main/main.c` | The IIGS as an `emu_machine_t`: memory, ROMs, Memory choice, key map (HID -> ADB), mouse, touch, drives, battery RAM, status, console |
| `main/settings.c`, `rom_store.c` | NVS settings (`gs`: `ram_kb`, `bram`), the ROM partition |
| `components/apple2gs/gs.c` | The glue: CPU page tables from Clemens's page maps, the bus, timing, devices per line, input, sound, battery RAM, free memory |
| `components/apple2gs/video.c` | The picture: Super Hi-Res, text 40/80, lores, double lores, hires, double hires, border |
| `components/apple2gs/storage.c` | 3.5" and 5.25" drives (images <-> nibbles), the slot 7 hard disks |
| `components/apple2gs/clemens/` | Clemens's machine (MIT), with the changes listed below |
| `components/apple2gs/host/` | Host build for testing on the Mac |
| `../esp32emu/components/w65816/` | The 65C816 core |
| `roms/` | `gs_rom3.rom`, `chargen.rom` (not distributed; see its README) |
| `partitions.csv` | nvs, phy, factory 8 MB, gs_roms 512 KB (type 0x40), storage |

## Architecture

- **Clemens provides the machine, not the CPU.** `ClemensMachine` keeps the
  memory bookkeeping (a page map per bank: which bank's RAM a page reads and
  writes, I/O, card ROM, write-enable, shadowing) and `ClemensMMIO` the
  devices. Its own 65816 isn't used. `gs.c` builds the w65816's page tables
  (a read and a write pointer per 256-byte page, 2 x 64K entries, 512 KB)
  from the page maps; NULL sends an access to `bus_read`/`bus_write`: I/O
  (`clem_mmio_read/write`), card ROM, and writes to shadowed pages (the
  write and its copy in E0/E1). Clemens rebuilds its maps when a soft switch
  changes and marks the pages it touched (`clem_mmio_dirty_pages`, per bank
  group 00, 01, E0/E1); `maps_follow()` rebuilds just those entries.
- **Interrupt vectors come from the ROM** (bank $FF) whatever the language
  card shows, unless I/O+LC shadowing is off (Tech Note 68):
  `w65816_t.vector_bank`, set in `maps_follow()`.
- **Time is Clemens's clock**: a fast (2.8 MHz) cycle 1000 clocks, a Mega II
  cycle 2800. A line is 65 Mega II cycles + the stretch (`GS_LINE_CLOCKS`,
  182,400 clocks, 63.7 us); a frame 262 lines. The CPU runs a line at a time
  at the current speed, then `devices_sync()` brings the devices up to the
  line's end (cards and their DMA, VGC, IWM, SCC, sound, game port, ADB, the
  60 Hz timer, the disk speed gate) and the IRQ lines go to the CPU and into
  `mmio->irq_line`. Before every bus callback the clock is set to the
  access's cycle (`clock_now()`).
- **1 MHz on the machine's grid**: lines at multiples of `GS_LINE_CLOCKS`
  from time 0, 65 cycles, the last one stretched. At 1 MHz the CPU waits for
  the grid's next edge and keeps to it (`phi0_edge/phi0_next`); the IWM
  stretches its bit cells on the same grid (`clem_iwm_reset`). An I/O access
  at 2.8 MHz costs 2 more cycles (`MEGA2_PENALTY`), none at 1 MHz.
- **Video** (`video.c`): drawn at the end of a frame from E0/E1 and the VGC's
  mode. 704x216: the 640x200 Super Hi-Res area with a 32-pixel border left
  and right and 8 lines above and below; the Apple II modes' 560x192 in its
  middle. Per line a key: a hash of its source bytes (whole words) and of
  the mode and colours; a line whose key didn't change isn't drawn again,
  the signature moves when one was. Text: the IIe enhanced video ROM
  (`chargen.rom`: code x 8, lit where a bit is 0, bit 0 leftmost), flashing
  at ~2 Hz, MouseText with the alternate set. Presented with smooth scaling
  (`view_width` 816: the 640x200 area at 4:3).
- **Disks** (`storage.c`): the 3.5" (800 KB, 400 KB) and 5.25" (140 KB)
  images are encoded into nibbles a track at a time on insert (a 6 KB
  buffer, not the whole image) into a 1.5 MB and a 255 KB buffer. Tracks
  the computer changed (a hash per track) are decoded back into the file
  once the drive's motor has been off for a second, and on eject. `.WOZ` is
  read whole and used write-protected. The computer's own eject (GS/OS's
  Eject) reports back (`gs_disk_ejected()` -> `disks_ejected()`). The hard
  disks are Clemens's slot 7 card with two `ProdosHDD32` units: blocks read
  and written in place in the file (`.po`, `.hdv`, `.2mg` after its
  header), any size up to 65,535 blocks.
- **Input**: keys as ADB codes in Clemens's numbering (its 0x10 is T, 0x11 Y:
  its key-to-ASCII table agrees), through a multi-producer queue (BLE task,
  console, the typing on the emulation task). The mouse is relative; a touch
  uses Clemens's absolute mode (the toolbox's clamp box and cursor
  position): desktop programs only.
- **Battery RAM** (the control panel's settings) in NVS `gs`/`bram`, saved
  when the IIGS changes it.
- **Free RAM** (web page): the Memory Manager's handle records, 20 bytes each
  (block address, attributes, owner, size, previous, next), are a list in
  address order from the record `$E11600` points to; blocks owned by 0 stand
  for what isn't RAM. Free memory = the gaps between blocks (as FreeMem
  counts it), read from the memory arrays with the list checked
  (`gs_ram_free()`). The ROM starts the Memory Manager at power-on, so it
  shows outside GS/OS too.
- **Setup screen**: Memory (MaTouch: one button, 1 MB; Tab5: 1, 2, 4, 8 MB;
  more than was reserved at power-on restarts the board into the machine).

### Changes to Clemens (each marked `esp32gs:` in the source)

- `clem_debug.c` replaced (Clemens's keeps a 6.6 MB trace buffer): messages
  at WARN or above to `gs_log()`, each format at most 3 times;
  `CLEM_ASSERT` and `CLEM_DEBUG` compiled out unless `CLEM_CHECKS` (Clemens's
  release build also drops its asserts; one fires on `$C029` bits 1-4).
- Serializer code under `#ifdef CLEM_SERIALIZER`; `cross_endian.h` knows the
  ESP32; `clem_2img_generate_header`'s definition uses `uint32_t` like its
  prototype (Xtensa's `uint32_t` is `long`).
- `clem_mmio.c`: `clem_mmio_map_generation` and `clem_mmio_dirty_pages`,
  marked for the pages that move: bank 00's video pages by their banks
  before and after, shadowing by its own bits (not the ones `remap_flags`
  gains for PAGE2, 80STORE+HIRES and RAMWRT).
- `clem_adb.c`: absolute mouse tracking started with `mx0 = my` (typo).
- `clem_audio.c`: the DOC's sync skips over halted oscillators in one step
  (and does nothing until a register is written), doesn't mix silence, the
  mix's gain is worked out once (no FP divide on the S3).
- `clem_iwm.c`: with the drive off (and no SmartPort units) a step moves time
  on in one go; the reset puts the scanline grid on the machine's grid
  (Clemens started it at time 0: after a later reset the IWM stretched nearly
  every bit cell catching up). Reading with the drive on, once a bit cell's
  control (the drive's commands and queries, the head's track) changed
  nothing and the IWM's outputs stay as they were, the cells after it only
  move the bit stream: `_clem_iwm_lean_run()` does that on local copies,
  the clock in 32 bits from the call's start (insert, eject and reset start
  over with a full cell).

## Performance (measured 2026-10-18)

`i` on the console prints thousands of CPU cycles per frame by part (cpu
includes the bus callbacks and the device work they trigger, e.g. the IWM
stepping on a data register read). The MaTouch's budget is 4,000 a frame.

- MaTouch, GS/OS desktop: 100%, core 1 ~75% (cpu 2,320, video 165, sound 117,
  the rest ~400). Booting GS/OS from the slot 7 hard disk: Finder in ~30 s
  (6.0.1) or ~45 s (6.0.4's Live.Install image). From the 3.5" floppy:
  77-97% of real time while the disk turns, `_clem_iwm_step` ~31% of core 1
  (45-60% with the full bit cells: ~1,900 plus the CPU's polling). The Tour
  of the Apple IIGS loads in ~5 1/2 minutes.
- Tab5, GS/OS desktop with 8 MB: 100%, core 1 ~40%; a floppy mounted while
  running at 100%. The Tour's 4 1/2 minute load from the 3.5" drive: 100%,
  core 1 ~60%, no sample missed but ~0.02 s during the boot's silence (with
  the full bit cells: 97-98%, core 1 ~92%, 300-850 samples a second missing,
  and 0.45 s at once where it flips PAGE2: the gaps heard in its sound).
- PSRAM (MaTouch): after `reserve_memory()` 2,577 KB free, ~1.1 MB while
  running. Internal RAM is tight: 57 KB free, 38 KB at the lowest.

## Lessons (each one cost a debugging session)

- **`mmio->irq_line` is read by Clemens itself**: `$C023` reports the
  scanline and 1-second interrupts from it, not from the VGC's own line.
  Never set, the ROM's interrupt handler didn't see its scanline interrupt,
  didn't clear it, and ran again and again: GS/OS hung at the end of its
  boot on the board (the host passed that point by chance). Keep it updated
  with the CPU's IRQ (`irq_update()`).
- **Interrupt vectors from bank $FF**: read through bank 0 they came from the
  language card's RAM once GS/OS had copied code there: a jump to $000000.
- **Code size is speed on the S3**: the 65816 core with every helper forced
  inline was a 72 KB run loop, twice the instruction cache; code runs from
  PSRAM, so nearly every emulated instruction missed (~850 cycles each).
  The compiler's own inlining and `-Os`: 18 KB.
- **Page tables per changed page**: rebuilding 4 x 256 entries on every soft
  switch change took half of core 1 (the interrupt handler writes `$C068`,
  `$C035`, `$C036` on entry and exit).
- **Disk II writes need the CPU and the IWM in step**: DOS 3.3 writes a
  nibble every 32 CPU cycles. An I/O penalty at 1 MHz, a CPU that ran 65
  even cycles a line while the IWM stretched once, or a stretch out of phase
  with the IWM's (after a reset, after time at 2.8 MHz) made it write bits
  late: data fields or epilogues damaged, I/O ERROR. Reads tolerate all of
  that. The fix is the one grid above.
- **Clemens's line is 65 Mega II cycles**, not 64 + the stretch: frames were
  1.3% short of the VGC's.
- **The DOC costs a lot in silence**: 32 oscillators visited 895,000 times a
  second, 22% of a core; now skipped while all are halted.
- **Video keys**: hashing every line every frame (FNV, a multiply per byte)
  cost 200k cycles; comparing with a copy cost 440k (PSRAM reads twice);
  a word hash 165k.
- **`T`/`Y` are swapped in Clemens's ADB numbering** compared with Apple's,
  consistently with its key-to-ASCII table: follow Clemens.
- **The Disk II boot reads track 0 a while before it gives up**: "Check
  startup device!" with a 5.25" disk in the drive was the I/O penalty at
  1 MHz (above), not the image.
- **A turning floppy costs every bit cell**: the Tour reads its 3.5" disk for
  4 1/2 minutes, and Clemens's five calls per 2 us cell (control, write
  head, step, read latch, head) took 30% of the Tab5's frame: short of real
  time, the speaker ran dry (gaps in the music). A cell's control has
  nothing to do once settled; the lean cells, on local copies of the IWM's
  and the drive's state (in PSRAM, so field by field every cell cost as
  much again), and the clock in 32 bits (the S3 has no 64-bit arithmetic).
- **A PAGE2 flip rebuilt 350 pages**: Clemens adds the shadow bits to
  `remap_flags` when PAGE2 changes, so pages 04-9F of banks 00 and 01 were
  marked each flip; the Tour flips 128 times a frame for a while (0.45 s
  of sound lost at once). Mark what moves; `GS_CHECK_MAPS` checks it.

## Testing

- **Host build**: `make -C components/apple2gs/host`, then
  `build/gs_host [frames] [out.ppm]` (frames at 60 Hz; it runs ~90x real
  time). Environment: `GS_ROM`, `GS_CHARGEN` (default `roms/...`),
  `GS_RAM=<KB>`, `GS_35=`, `GS_525=`, `GS_HD1=`, `GS_HD2=` (images),
  `GS_NEWDISK=<path>:<KB>`, `GS_BRAM=<file>`, `GS_TEXT=1` (the text page at
  the end), `GS_WAV=<file>`, `GS_STATUS=1`, `GS_TRACE=<pc>:<n>`,
  `GS_RING=<n>` (+ `GS_RING_PC=<pc>`, default 000000: the last n
  instructions before pc), `GS_CLOCK=<seconds since 1904>` (a fixed clock:
  runs to compare), `GS_NO_IWM_LEAN=1` (only full IWM bit cells),
  `GS_IWM_STAT=1` (full and lean cells at the end; `status` lines show them
  too), `GS_CHECK_MAPS=1` (after each memory map change all of 00, 01, E0
  and E1 rebuilt: the pages that differed are counted at the end).
  `GS_SCRIPT=<file>`, lines `<frame> <command>`:
  `type <text>` (`\n` Return), `key <adb hex> 0|1`, `mouse dx dy btn`,
  `shot <file.ppm>`, `text`, `insert <drive 0-3> <path>`, `eject <drive>`,
  `reset`, `peek <hex>`, `dump <hex> <n>`, `flush`, `status`, `iostat` (bus
  callbacks by address, bank and page since the last one), `doc` (running
  oscillators), `nib <track> <file>` (the 5.25" drive's bits), `mm` /
  `mmdump` (the Memory Manager's handle list), `free`.
- Disk images: System 6.0.1 and 6.0.4 from
  mirrors.apple2.org.za (Apple II Documentation Project, Operating Systems,
  Apple IIGS System, Disk Images); 6.0.4's `Live.Install.po` is a 32 MB
  bootable hard disk with the whole system. DOS 3.3 (January 1983) from the
  same site's Apple DOS folder.
- Checked on the host: ROM 3 to the startup screen; Control-Reset to
  Applesoft, typing; GS/OS 6.0.1 from 3.5" (Finder in 2 minutes of emulated
  time; the Finder's writes decoded back, the written image boots again
  identically) and from the hard disk; 6.0.4's System Disk from 3.5" and
  Live.Install as a hard disk; DOS 3.3 from 5.25": CATALOG, SAVE, RUN from
  the written image, also after GS/OS ran and a reset; the power-on beep.
  The IWM's lean cells against the full ones (`GS_NO_IWM_LEAN`, `GS_CLOCK`
  fixed): the Tour's first 200 s, GS/OS 6.0.1 and 6.0.4 booting from 3.5",
  DOS 3.3 SAVE and RUN, screenshots, `GS_WAV` and images byte for byte the
  same; the page tables with `GS_CHECK_MAPS` (those, the whole Tour, 80
  columns under DOS 3.3): none missed.
- Checked on the boards: the setup screen, GS/OS 6.0.1 from 3.5" (MaTouch)
  and hard disk (both), 6.0.4 Live.Install (both), mouse (console) and touch
  (Tab5: pointer, double click opens a disk), a 3.5" disk mounted while
  running (Tab5), DOS 3.3 SAVE/RUN on the MaTouch with the image on the SD
  card updated, the web page's Free RAM row; the Tour from 3.5" on both
  (its load, the first screen), the Tab5's speaker fed without gaps
  through it (the counter in `i`).
- **Not checked by eye or ear**: sound on the speakers, a Bluetooth keyboard
  and mouse on the IIGS, touch on the MaTouch, games and other programs.

## Open items

- Floppy access on the MaTouch runs at 77-97%: the IWM still steps every bit
  cell (2 us for 3.5"), now lean. The hard disk card is the fast way.
- No Apple II mouse firmware for ProDOS 8 programs on touch (the absolute
  mode follows the toolbox's cursor only), no serial ports to the outside, no
  AppleTalk, no 2nd 3.5"/5.25" drive, no `.NIB` or nibble `.2MG`.
- The MaTouch can't hold more than 1 MB of IIGS RAM next to the floppy
  buffers and the runtime.
- Public on GitHub (`github.com/rlucencic/esp32gs`, pushed to `main`).
  Licence: MIT (`LICENSE`); Clemens is MIT too (`clemens/LICENSE.txt`).

## Working with the user

- Expects changes built, flashed and checked on both boards (MaTouch first),
  then a short report of what was verified and what wasn't.
- Keep their private data (WiFi name, IP, SD card contents) out of anything
  meant for publishing.
