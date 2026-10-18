idf_component_register(SRCS "main.c" "settings.c" "rom_store.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES emu apple2gs esp_partition nvs_flash esp_psram vfs)
