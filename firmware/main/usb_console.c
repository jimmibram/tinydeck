#include "usb_console.h"
#include "input.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_log.h"
#include "rlcd.h"

static const char *TAG = "usb_console";

/* ANSI escape parser state */
static int s_esc;             /* 0 none, 1 ESC, 2 ESC[ or ESC O */
static char s_esc_final;      /* '[' or 'O' */
static int s_esc_num;
static uint32_t s_cp;
static int s_utf8_left;

static void emit_code(key_code_t c) { key_event_t e = key_code(c, 0); input_push(&e); }

static void csi_final(char c)
{
    switch (c) {
    case 'A': emit_code(KEY_UP); break;
    case 'B': emit_code(KEY_DOWN); break;
    case 'C': emit_code(KEY_RIGHT); break;
    case 'D': emit_code(KEY_LEFT); break;
    case 'H': emit_code(KEY_HOME); break;
    case 'F': emit_code(KEY_END); break;
    case 'P': emit_code(KEY_F1); break;
    case 'Q': emit_code(KEY_F2); break;
    case 'R': emit_code(KEY_F3); break;
    case 'S': emit_code(KEY_F4); break;
    case '~':
        switch (s_esc_num) {
        case 1: case 7: emit_code(KEY_HOME); break;
        case 2: emit_code(KEY_INSERT); break;
        case 3: emit_code(KEY_DELETE); break;
        case 4: case 8: emit_code(KEY_END); break;
        case 5: emit_code(KEY_PGUP); break;
        case 6: emit_code(KEY_PGDN); break;
        case 11: case 12: case 13: case 14: case 15: emit_code((key_code_t)(KEY_F1 + s_esc_num - 11)); break;
        case 17: case 18: case 19: case 20: case 21: emit_code((key_code_t)(KEY_F6 + s_esc_num - 17)); break;
        case 23: emit_code(KEY_F11); break;
        case 24: emit_code(KEY_F12); break;
        default: break;
        }
        break;
    default: break;
    }
}

/* Debug: dump the framebuffer as hex so tools/screenshot.py can render it on the host. */
static void dump_screenshot(void)
{
    const uint8_t *fb = rlcd_framebuffer();
    static const char hex[] = "0123456789abcdef";
    char line[129];
    printf("\nSHOT_BEGIN %d %d\n", RLCD_WIDTH, RLCD_HEIGHT);
    for (int i = 0; i < RLCD_FB_SIZE; i += 64) {
        int n = RLCD_FB_SIZE - i < 64 ? RLCD_FB_SIZE - i : 64;
        for (int j = 0; j < n; j++) { line[2 * j] = hex[fb[i + j] >> 4]; line[2 * j + 1] = hex[fb[i + j] & 15]; }
        line[2 * n] = 0;
        printf("%s\n", line);
    }
    printf("SHOT_END\n");
    fflush(stdout);
}

static void feed(uint8_t b)
{
    if (b == 0x1C) { dump_screenshot(); return; }     /* Ctrl-\ from the host */
    if (s_esc == 1) {
        if (b == '[' || b == 'O') { s_esc = 2; s_esc_final = (char)b; s_esc_num = 0; return; }
        s_esc = 0;
        emit_code(KEY_ESC);          /* ESC followed by an ordinary key */
    } else if (s_esc == 2) {
        if (b >= '0' && b <= '9') { s_esc_num = s_esc_num * 10 + (b - '0'); return; }
        if (b == ';') { return; }    /* modifier params: ignored */
        s_esc = 0;
        csi_final((char)b);
        return;
    }
    if (s_utf8_left) {
        if ((b & 0xC0) == 0x80) {
            s_cp = (s_cp << 6) | (b & 0x3F);
            if (--s_utf8_left == 0) { key_event_t e = key_char(s_cp, 0); input_push(&e); }
            return;
        }
        s_utf8_left = 0;
    }
    if (b == 0x1B) { s_esc = 1; return; }
    if (b == '\r' || b == '\n') { emit_code(KEY_ENTER); return; }
    if (b == 0x7F || b == 0x08) { emit_code(KEY_BACKSPACE); return; }
    if (b == 0x09) { emit_code(KEY_TAB); return; }
    if (b < 0x20) { key_event_t e = key_char((uint32_t)('a' + b - 1), KMOD_CTRL); input_push(&e); return; }
    if (b < 0x80) { key_event_t e = key_char(b, 0); input_push(&e); return; }
    if ((b & 0xE0) == 0xC0) { s_cp = b & 0x1F; s_utf8_left = 1; }
    else if ((b & 0xF0) == 0xE0) { s_cp = b & 0x0F; s_utf8_left = 2; }
    else if ((b & 0xF8) == 0xF0) { s_cp = b & 0x07; s_utf8_left = 3; }
}

static void usb_console_task(void *arg)
{
    uint8_t buf[64];
    for (;;) {
        int n = usb_serial_jtag_read_bytes(buf, sizeof(buf), pdMS_TO_TICKS(50));
        if (n <= 0) {
            if (s_esc == 1) { s_esc = 0; emit_code(KEY_ESC); }   /* lone Escape key */
            continue;
        }
        for (int i = 0; i < n; i++) feed(buf[i]);
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
    usb_serial_jtag_vfs_use_driver();
    xTaskCreate(usb_console_task, "usb_console", 3072, NULL, 4, NULL);
}
