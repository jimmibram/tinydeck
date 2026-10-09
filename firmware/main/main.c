/*
 * mini-pc firmware for the Waveshare ESP32-S3-RLCD-4.2.
 * Boot -> text terminal on the reflective LCD -> BLE keyboard host -> shell.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "rlcd.h"
#include "term.h"
#include "ble_kbd.h"
#include "input.h"
#include "shell.h"
#include "usb_console.h"

static const char *TAG = "main";

static void on_kbd_bytes(const uint8_t *data, size_t len) { input_push(data, len); }
static void on_kbd_status(const char *msg) { shell_notify(msg); }

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(rlcd_init());
    ESP_ERROR_CHECK(term_init());
    input_init();
    usb_console_start();
    shell_start();

    ble_kbd_config_t kcfg = { .on_bytes = on_kbd_bytes, .on_status = on_kbd_status };
    err = ble_kbd_init(&kcfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BLE keyboard init failed: %s", esp_err_to_name(err));
        shell_notify("Bluetooth init failed; USB console input only");
    }
    ESP_LOGI(TAG, "up");
}
