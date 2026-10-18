# The Apple IIGS, platform independent (see include/gs.h): esp32emu's 65C816
# and Clemens's machine (clemens/, MIT licence)
set(clemens_srcs
    clemens/clem_2img.c clemens/clem_adb.c clemens/clem_audio.c clemens/clem_debug.c
    clemens/clem_disk.c clemens/clem_drive.c clemens/clem_drive35.c clemens/clem_iwm.c
    clemens/clem_locale.c clemens/clem_mem.c clemens/clem_mmio.c clemens/clem_rtc.c
    clemens/clem_scc.c clemens/clem_smartport.c clemens/clem_timer.c clemens/clem_vgc.c
    clemens/clem_woz.c clemens/emulator_mmio.c clemens/hddcard.c clemens/prodos_hdd32.c)

idf_component_register(SRCS "gs.c" "video.c" "storage.c" ${clemens_srcs}
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "." "clemens"
                       REQUIRES w65816)

# Runs in the emulation loop: jump tables for the switches (the IDF turns
# them off for code that may run with the flash cache disabled; the
# emulation task never does: flash writes stall it)
target_compile_options(${COMPONENT_LIB} PRIVATE -O2 -fjump-tables -ftree-switch-conversion)
# Clemens as it comes: its warnings are its own
set_source_files_properties(${clemens_srcs} PROPERTIES COMPILE_OPTIONS "-w")
