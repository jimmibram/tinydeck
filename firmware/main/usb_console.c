#include "usb_console.h"
#include "input.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_log.h"

static const char *TAG = "usb_console";

static void usb_console_task(void *arg)
{
    uint8_t buf[64];
    for (;;) {
        int n = usb_serial_jtag_read_bytes(buf, sizeof(buf), portMAX_DELAY);
        if (n > 0) {
            /* Terminals send CR for Enter; the shell wants LF. */
            for (int i = 0; i < n; i++) if (buf[i] == '\r') buf[i] = '\n';
            input_push(buf, (size_t)n);
        }
    }
}

void usb_console_start(void)
{
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    cfg.rx_buffer_size = 1024;
    cfg.tx_buffer_size = 4096;
    esp_err_t err = usb_serial_jtag_driver_install(&cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "driver install failed: %s", esp_err_to_name(err));
        return;
    }
    usb_serial_jtag_vfs_use_driver();   /* stdout now goes through the buffered driver too */
    xTaskCreate(usb_console_task, "usb_console", 3072, NULL, 4, NULL);
}
