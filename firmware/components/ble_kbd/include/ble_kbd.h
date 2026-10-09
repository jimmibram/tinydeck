#pragma once
/*
 * Bluetooth Low Energy keyboard host.
 *
 * Scans for BLE HID keyboards, connects and bonds (pairing code is shown via
 * the status callback so the user can type it on the keyboard), then
 * translates HID key reports into key_event_t (see input.h). Host-side key
 * repeat is implemented here, as HID keyboards do not repeat.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "input.h"

typedef void (*ble_kbd_key_cb_t)(const key_event_t *ev);
typedef void (*ble_kbd_status_cb_t)(const char *msg);

typedef struct {
    ble_kbd_key_cb_t on_key;          /* decoded key presses */
    ble_kbd_status_cb_t on_status;    /* human readable status / pairing messages */
} ble_kbd_config_t;

esp_err_t ble_kbd_init(const ble_kbd_config_t *cfg);

bool ble_kbd_connected(void);
const char *ble_kbd_device_name(void);     /* "" when not connected */
const char *ble_kbd_status(void);

/* Drop all bonds and look for a keyboard in pairing mode. */
esp_err_t ble_kbd_forget_and_pair(void);

/* Trigger a scan now (if not connected). */
void ble_kbd_rescan(void);
