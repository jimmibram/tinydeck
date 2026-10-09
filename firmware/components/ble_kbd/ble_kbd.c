#include "ble_kbd.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_hidh.h"
#include "esp_hid_gap.h"

static const char *TAG = "ble_kbd";

#define SCAN_SECONDS          5
#define RETRY_DELAY_MS        1500
#define OPEN_TIMEOUT_MS       15000
#define REPEAT_DELAY_MS       400
#define REPEAT_PERIOD_MS      40
#define BLE_APPEARANCE_KEYBOARD 0x03C1

/* HID keyboard usage IDs */
#define KEY_A 0x04
#define KEY_Z 0x1D
#define KEY_1 0x1E
#define KEY_0 0x27
#define KEY_ENTER 0x28
#define KEY_ESC 0x29
#define KEY_BACKSPACE 0x2A
#define KEY_TAB 0x2B
#define KEY_SPACE 0x2C
#define KEY_CAPSLOCK 0x39
#define KEY_F1 0x3A
#define KEY_F12 0x45
#define KEY_INSERT 0x49
#define KEY_HOME 0x4A
#define KEY_PGUP 0x4B
#define KEY_DELETE 0x4C
#define KEY_END 0x4D
#define KEY_PGDN 0x4E
#define KEY_RIGHT 0x4F
#define KEY_LEFT 0x50
#define KEY_DOWN 0x51
#define KEY_UP 0x52
#define KEY_KP_SLASH 0x54
#define KEY_KP_ENTER 0x58
#define KEY_KP_1 0x59
#define KEY_KP_DOT 0x63
#define KEY_NONUS_BSLASH 0x64

#define MOD_CTRL  0x11
#define MOD_SHIFT 0x22
#define MOD_ALT   0x44
#define MOD_GUI   0x88

/* US layout, HID usage 0x1E..0x38 and 0x54..0x63 */
static const char s_num_row[2][10] = { "1234567890", "!@#$%^&*()" };
static const char s_punct[2][13] = {
    { '-', '=', '[', ']', '\\', '#', ';', '\'', '`', ',', '.', '/', 0 },   /* 0x2D..0x38 */
    { '_', '+', '{', '}', '|',  '~', ':', '"',  '~', '<', '>', '?', 0 },
};
static const char s_keypad[] = "/*-+\n1234567890.";                        /* 0x54..0x63 */

typedef struct {
    ble_kbd_config_t cfg;
    esp_hidh_dev_t *dev;
    char name[32];
    char status[80];
    volatile bool connected;
    volatile bool open_pending;
    volatile bool open_failed;
    volatile bool rescan_requested;
    volatile bool pairing_mode;     /* ignore bonded preference, pick strongest keyboard */
    bool caps_lock;
    uint8_t prev_keys[6];
    uint8_t prev_mods;
    uint8_t repeat_key;
    uint8_t repeat_mods;
    int64_t repeat_due_us;
    SemaphoreHandle_t lock;
    esp_timer_handle_t repeat_timer;
} ble_kbd_t;

static ble_kbd_t s;

static void set_status(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void set_status(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s.status, sizeof(s.status), fmt, ap);
    va_end(ap);
    ESP_LOGI(TAG, "%s", s.status);
    if (s.cfg.on_status) s.cfg.on_status(s.status);
}

static void emit(const char *bytes, size_t len)
{
    if (s.cfg.on_bytes && len) s.cfg.on_bytes((const uint8_t *)bytes, len);
}

/* Translate one HID usage + modifier byte into output bytes. */
static void key_to_bytes(uint8_t key, uint8_t mods)
{
    bool shift = (mods & MOD_SHIFT) != 0;
    bool ctrl = (mods & MOD_CTRL) != 0;
    char c = 0;

    if (key >= KEY_A && key <= KEY_Z) {
        c = (char)('a' + (key - KEY_A));
        if (ctrl) { char cc = (char)(c & 0x1F); emit(&cc, 1); return; }
        if (shift ^ s.caps_lock) c = (char)(c - 'a' + 'A');
    } else if (key >= KEY_1 && key <= KEY_0) {
        c = s_num_row[shift][key - KEY_1];
    } else if (key >= 0x2D && key <= 0x38) {
        c = s_punct[shift][key - 0x2D];
    } else if (key >= KEY_KP_SLASH && key <= KEY_KP_DOT) {
        c = s_keypad[key - KEY_KP_SLASH];
    } else {
        switch (key) {
        case KEY_ENTER:     c = '\n'; break;
        case KEY_ESC:       c = 0x1B; break;
        case KEY_BACKSPACE: c = '\b'; break;
        case KEY_TAB:       c = '\t'; break;
        case KEY_SPACE:     c = ' '; break;
        case KEY_NONUS_BSLASH: c = shift ? '|' : '\\'; break;
        case KEY_UP:     emit("\x1b[A", 3); return;
        case KEY_DOWN:   emit("\x1b[B", 3); return;
        case KEY_RIGHT:  emit("\x1b[C", 3); return;
        case KEY_LEFT:   emit("\x1b[D", 3); return;
        case KEY_HOME:   emit("\x1b[H", 3); return;
        case KEY_END:    emit("\x1b[F", 3); return;
        case KEY_DELETE: emit("\x1b[3~", 4); return;
        case KEY_INSERT: emit("\x1b[2~", 4); return;
        case KEY_PGUP:   emit("\x1b[5~", 4); return;
        case KEY_PGDN:   emit("\x1b[6~", 4); return;
        default: return;    /* F-keys, media keys, modifiers: ignored */
        }
    }
    if (c) emit(&c, 1);
}

static void repeat_timer_cb(void *arg)
{
    xSemaphoreTake(s.lock, portMAX_DELAY);
    if (s.repeat_key && esp_timer_get_time() >= s.repeat_due_us) {
        key_to_bytes(s.repeat_key, s.repeat_mods);
        s.repeat_due_us = esp_timer_get_time() + REPEAT_PERIOD_MS * 1000;
    }
    xSemaphoreGive(s.lock);
}

/* Boot-protocol style report: [modifiers][reserved][key1..key6] (report ID already stripped). */
static void handle_keyboard_report(const uint8_t *data, size_t len)
{
    if (len < 3) return;
    uint8_t mods = data[0];
    const uint8_t *keys = data + 2;
    size_t nkeys = len - 2;
    if (nkeys > 6) nkeys = 6;

    xSemaphoreTake(s.lock, portMAX_DELAY);
    bool any_new = false;
    for (size_t i = 0; i < nkeys; i++) {
        uint8_t k = keys[i];
        if (k == 0 || k == 1) continue;             /* 1 = rollover error */
        bool was_down = false;
        for (size_t j = 0; j < 6; j++) if (s.prev_keys[j] == k) { was_down = true; break; }
        if (was_down) continue;
        any_new = true;
        if (k == KEY_CAPSLOCK) { s.caps_lock = !s.caps_lock; continue; }
        key_to_bytes(k, mods);
        s.repeat_key = k;
        s.repeat_mods = mods;
        s.repeat_due_us = esp_timer_get_time() + REPEAT_DELAY_MS * 1000;
    }
    /* Stop repeating if the repeating key was released. */
    if (s.repeat_key) {
        bool still = false;
        for (size_t i = 0; i < nkeys; i++) if (keys[i] == s.repeat_key) { still = true; break; }
        if (!still) s.repeat_key = 0;
        else if (!any_new) s.repeat_mods = mods;
    }
    memset(s.prev_keys, 0, sizeof(s.prev_keys));
    memcpy(s.prev_keys, keys, nkeys);
    s.prev_mods = mods;
    xSemaphoreGive(s.lock);
}

static void hidh_callback(void *arg, esp_event_base_t base, int32_t id, void *event_data)
{
    esp_hidh_event_data_t *p = (esp_hidh_event_data_t *)event_data;
    switch ((esp_hidh_event_t)id) {
    case ESP_HIDH_OPEN_EVENT:
        s.open_pending = false;
        if (p->open.status == ESP_OK) {
            const char *n = esp_hidh_dev_name_get(p->open.dev);
            strlcpy(s.name, n && *n ? n : "keyboard", sizeof(s.name));
            s.dev = p->open.dev;
            s.connected = true;
            s.pairing_mode = false;
            set_status("connected: %s", s.name);
        } else {
            s.open_failed = true;
            set_status("connect failed (0x%x)", p->open.status);
        }
        break;
    case ESP_HIDH_INPUT_EVENT:
        if (p->input.usage == ESP_HID_USAGE_KEYBOARD || p->input.usage == ESP_HID_USAGE_GENERIC) {
            handle_keyboard_report(p->input.data, p->input.length);
        }
        break;
    case ESP_HIDH_BATTERY_EVENT:
        ESP_LOGI(TAG, "battery %d%%", p->battery.level);
        break;
    case ESP_HIDH_CLOSE_EVENT:
        s.connected = false;
        s.dev = NULL;
        xSemaphoreTake(s.lock, portMAX_DELAY);
        s.repeat_key = 0;
        memset(s.prev_keys, 0, sizeof(s.prev_keys));
        xSemaphoreGive(s.lock);
        set_status("disconnected: %s", s.name);
        s.name[0] = 0;
        esp_hidh_dev_free(p->close.dev);
        break;
    default:
        break;
    }
}

/* Pairing hooks from esp_hid_gap.c */
void esp_hid_gap_on_passkey(uint32_t passkey)
{
    set_status("type %06lu on the keyboard, then Enter", (unsigned long)passkey);
}

void esp_hid_gap_on_auth_complete(bool success, int reason)
{
    if (success) set_status("paired");
    else set_status("pairing failed (0x%x)", reason);
}

static bool is_bonded(const uint8_t *bda)
{
    int n = esp_ble_get_bond_device_num();
    if (n <= 0) return false;
    esp_ble_bond_dev_t *list = calloc((size_t)n, sizeof(*list));
    if (!list) return false;
    bool found = false;
    if (esp_ble_get_bond_device_list(&n, list) == ESP_OK) {
        for (int i = 0; i < n; i++) {
            if (memcmp(list[i].bd_addr, bda, 6) == 0) { found = true; break; }
        }
    }
    free(list);
    return found;
}

static void remove_all_bonds(void)
{
    int n = esp_ble_get_bond_device_num();
    if (n <= 0) return;
    esp_ble_bond_dev_t *list = calloc((size_t)n, sizeof(*list));
    if (!list) return;
    if (esp_ble_get_bond_device_list(&n, list) == ESP_OK) {
        for (int i = 0; i < n; i++) esp_ble_remove_bond_device(list[i].bd_addr);
    }
    free(list);
}

static bool looks_like_keyboard(const esp_hid_scan_result_t *r)
{
    return r->usage == ESP_HID_USAGE_KEYBOARD || r->ble.appearance == BLE_APPEARANCE_KEYBOARD;
}

static void kbd_task(void *arg)
{
    for (;;) {
        if (s.connected || s.open_pending) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        set_status("scanning for a BLE keyboard...");
        size_t n = 0;
        esp_hid_scan_result_t *results = NULL;
        if (esp_hid_scan(SCAN_SECONDS, &n, &results) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(RETRY_DELAY_MS));
            continue;
        }
        esp_hid_scan_result_t *best = NULL;
        bool best_bonded = false;
        for (esp_hid_scan_result_t *r = results; r; r = r->next) {
            if (r->transport != ESP_HID_TRANSPORT_BLE) continue;
            ESP_LOGI(TAG, "  %02x:%02x:%02x:%02x:%02x:%02x rssi %d usage %s appearance 0x%04x name '%s'",
                     r->bda[0], r->bda[1], r->bda[2], r->bda[3], r->bda[4], r->bda[5],
                     r->rssi, esp_hid_usage_str(r->usage), r->ble.appearance, r->name ? r->name : "");
            /* Every result is a BLE HID device. Prefer bonded, then keyboard-like, then strongest signal. */
            bool bonded = !s.pairing_mode && is_bonded(r->bda);
            bool kbd = looks_like_keyboard(r);
            int score = (bonded ? 1000 : 0) + (kbd ? 500 : 0) + r->rssi;
            int best_score = best ? (best_bonded ? 1000 : 0) + (looks_like_keyboard(best) ? 500 : 0) + best->rssi : -100000;
            if (!best || score > best_score) {
                best = r;
                best_bonded = bonded;
            }
        }
        if (best) {
            set_status("connecting to %s%s", best->name ? best->name : "keyboard", best_bonded ? " (bonded)" : "");
            s.open_pending = true;
            s.open_failed = false;
            esp_err_t err = esp_hidh_dev_open(best->bda, best->transport, best->ble.addr_type) ? ESP_OK : ESP_FAIL;
            if (err != ESP_OK) {
                s.open_pending = false;
                set_status("open failed");
            } else {
                int waited = 0;
                while (s.open_pending && waited < OPEN_TIMEOUT_MS) {
                    vTaskDelay(pdMS_TO_TICKS(100));
                    waited += 100;
                }
                if (s.open_pending) {
                    s.open_pending = false;
                    set_status("connect timed out");
                }
            }
        } else {
            set_status("no keyboard found (%u BLE devices seen)", (unsigned)n);
        }
        esp_hid_scan_results_free(results);
        if (!s.connected) {
            int waited = 0;
            while (waited < RETRY_DELAY_MS && !s.rescan_requested) {
                vTaskDelay(pdMS_TO_TICKS(100));
                waited += 100;
            }
            s.rescan_requested = false;
        }
    }
}

static esp_err_t configure_security(void)
{
    /* We can show a passkey on the display; keyboards can type one. -> Passkey Entry with MITM + bonding. */
    esp_ble_auth_req_t auth = ESP_LE_AUTH_REQ_SC_MITM_BOND;
    esp_ble_io_cap_t iocap = ESP_IO_CAP_OUT;
    uint8_t key_size = 16;
    uint8_t init_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t rsp_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t oob = ESP_BLE_OOB_DISABLE;
    ESP_RETURN_ON_ERROR(esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth, sizeof(auth)), TAG, "auth");
    ESP_RETURN_ON_ERROR(esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, sizeof(iocap)), TAG, "iocap");
    ESP_RETURN_ON_ERROR(esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(key_size)), TAG, "keysize");
    ESP_RETURN_ON_ERROR(esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key, sizeof(init_key)), TAG, "initkey");
    ESP_RETURN_ON_ERROR(esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &rsp_key, sizeof(rsp_key)), TAG, "rspkey");
    ESP_RETURN_ON_ERROR(esp_ble_gap_set_security_param(ESP_BLE_SM_OOB_SUPPORT, &oob, sizeof(oob)), TAG, "oob");
    return ESP_OK;
}

esp_err_t ble_kbd_init(const ble_kbd_config_t *cfg)
{
    if (cfg) s.cfg = *cfg;
    s.lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s.lock, ESP_ERR_NO_MEM, TAG, "lock");

    ESP_RETURN_ON_ERROR(esp_hid_gap_init(HIDH_BLE_MODE), TAG, "gap init");
    ESP_RETURN_ON_ERROR(esp_ble_gattc_register_callback(esp_hidh_gattc_event_handler), TAG, "gattc cb");
    ESP_RETURN_ON_ERROR(configure_security(), TAG, "security");

    esp_hidh_config_t hcfg = {
        .callback = hidh_callback,
        .event_stack_size = 4096,
        .callback_arg = NULL,
    };
    ESP_RETURN_ON_ERROR(esp_hidh_init(&hcfg), TAG, "hidh init");

    const esp_timer_create_args_t targs = { .callback = repeat_timer_cb, .name = "kbd_repeat" };
    ESP_RETURN_ON_ERROR(esp_timer_create(&targs, &s.repeat_timer), TAG, "timer");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(s.repeat_timer, REPEAT_PERIOD_MS * 1000), TAG, "timer start");

    const uint8_t *own = esp_bt_dev_get_address();
    if (own) ESP_LOGI(TAG, "own address %02x:%02x:%02x:%02x:%02x:%02x, %d bonded device(s)",
                      own[0], own[1], own[2], own[3], own[4], own[5], esp_ble_get_bond_device_num());

    ESP_RETURN_ON_FALSE(xTaskCreate(kbd_task, "ble_kbd", 6 * 1024, NULL, 3, NULL) == pdPASS, ESP_FAIL, TAG, "task");
    return ESP_OK;
}

bool ble_kbd_connected(void) { return s.connected; }
const char *ble_kbd_device_name(void) { return s.connected ? s.name : ""; }
const char *ble_kbd_status(void) { return s.status; }

esp_err_t ble_kbd_forget_and_pair(void)
{
    if (s.dev) esp_hidh_dev_close(s.dev);
    remove_all_bonds();
    s.pairing_mode = true;
    s.rescan_requested = true;
    set_status("bonds cleared; put the keyboard in pairing mode");
    return ESP_OK;
}

void ble_kbd_rescan(void) { s.rescan_requested = true; }
