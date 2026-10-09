#pragma once
/* Unified keyboard events from every source (BLE keyboard, USB console). */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"

typedef enum {
    KEY_NONE = 0,
    KEY_CHAR,          /* printable: ch holds the Unicode code point */
    KEY_ENTER, KEY_ESC, KEY_BACKSPACE, KEY_TAB,
    KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
    KEY_HOME, KEY_END, KEY_DELETE, KEY_INSERT, KEY_PGUP, KEY_PGDN,
    KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
} key_code_t;

#define KMOD_CTRL  0x01
#define KMOD_SHIFT 0x02
#define KMOD_ALT   0x04
#define KMOD_GUI   0x08

typedef struct {
    uint8_t code;      /* key_code_t */
    uint8_t mods;      /* KMOD_* */
    uint32_t ch;       /* code point for KEY_CHAR, else 0 */
} key_event_t;

void input_init(void);
void input_push(const key_event_t *ev);
bool input_read(key_event_t *out, TickType_t wait);

/* Helpers */
size_t utf8_encode(uint32_t cp, char out[4]);
static inline key_event_t key_char(uint32_t cp, uint8_t mods) { key_event_t e = { KEY_CHAR, mods, cp }; return e; }
static inline key_event_t key_code(key_code_t code, uint8_t mods) { key_event_t e = { (uint8_t)code, mods, 0 }; return e; }
