#include "esp_log.h"
#include "nvs.h"
#include "settings.h"

#define NAMESPACE   "gs"

static const char *TAG = "settings";

settings_t settings;

void settings_load(void)
{
    settings.ram_kb = 1024;
    settings.have_bram = false;

    nvs_handle_t h;
    if (nvs_open(NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    uint16_t u16;
    if (nvs_get_u16(h, "ram_kb", &u16) == ESP_OK && u16 >= 1024 && u16 <= 8192 && u16 % 1024 == 0) {
        settings.ram_kb = u16;
    }
    size_t n = sizeof(settings.bram);
    settings.have_bram = nvs_get_blob(h, "bram", settings.bram, &n) == ESP_OK && n == sizeof(settings.bram);
    nvs_close(h);
}

void settings_save(void)
{
    nvs_handle_t h;
    if (nvs_open(NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "can't open NVS");
        return;
    }
    nvs_set_u16(h, "ram_kb", settings.ram_kb);
    nvs_commit(h);
    nvs_close(h);
}

/* The battery RAM (the control panel's settings): 256 bytes */
void settings_save_bram(void)
{
    nvs_handle_t h;
    if (nvs_open(NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "can't open NVS");
        return;
    }
    nvs_set_blob(h, "bram", settings.bram, sizeof(settings.bram));
    nvs_commit(h);
    nvs_close(h);
    settings.have_bram = true;
}
