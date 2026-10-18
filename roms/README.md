# Apple IIGS ROM images

Put the ROM images here (not part of this repository). `idf.py flash`
writes them into the `gs_roms` flash partition.

| File | What | Size | MD5 |
|---|---|---|---|
| `gs_rom3.rom` | Apple IIGS ROM 3: banks $FC-$FD (341-0737) followed by $FE-$FF (341-0749) | 256 KB | `ba89edf2729a28a17cd9e0f7a0ac9a39` |
| `chargen.rom` | Apple IIe enhanced video ROM (342-0265-A, US): the text characters | 4 KB | `9123fff3442c0e688cc6816be88dd4ab` |

`gs_rom3.rom` is the file other emulators call `APPLE2GS.ROM` or
`ROM.03` / `gs_rom_3.rom` (256 KB). From the two chip dumps:

```sh
cat "Apple IIGS ROM 3 Banks FC-FD - 341-0737.bin" \
    "Apple IIGS ROM 3 Banks FF-FE - 341-0749 (new).bin" > gs_rom3.rom
```

(The older dump `341-0748` has its two banks the other way round.)

Without flashing, the emulator also finds them in a `roms` folder on the SD
card under the same names (any case).
