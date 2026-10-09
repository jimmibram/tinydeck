/*
 * BRAMCONSOLE firmware for the Waveshare ESP32-S3-RLCD-4.2.
 * Boot -> sensors -> UI (topbar + home screen + apps) -> BLE keyboard host.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "rlcd.h"
#include "term.h"
#include "ble_kbd.h"
#include "input.h"
#include "sensors.h"
#include "ui.h"
#include "app_terminal.h"
#include "shell.h"
#include "usb_console.h"

static const char *TAG = "main";

static void on_kbd_key(const key_event_t *ev) { input_push(ev); }
static void on_kbd_status(const char *msg) { shell_notify(msg); ui_request_redraw(); }

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(rlcd_init());
    input_init();
    if (sensors_init() != ESP_OK) ESP_LOGW(TAG, "sensors unavailable");

    ESP_ERROR_CHECK(term_init(app_terminal_cols(), app_terminal_rows()));
    shell_init();
    ui_init();
    ui_register_app(&ui_app_terminal);
    ui_start();

    usb_console_start();

    ble_kbd_config_t kcfg = { .on_key = on_kbd_key, .on_status = on_kbd_status };
    err = ble_kbd_init(&kcfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BLE keyboard init failed: %s", esp_err_to_name(err));
        shell_notify("Bluetooth init failed; USB console input only");
    }
    ESP_LOGI(TAG, "up");
}
