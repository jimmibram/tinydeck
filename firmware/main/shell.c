#include "shell.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "term.h"
#include "input.h"
#include "ble_kbd.h"

#define PROMPT "> "
#define SHELL_LINE_MAX 200

static SemaphoreHandle_t s_lock;
static char s_line[SHELL_LINE_MAX];
static size_t s_len;
static bool s_editing;

/* Everything the shell prints goes to both the LCD and the USB console. */
static void out(const char *s)
{
    term_puts(s);
    fputs(s, stdout);
    fflush(stdout);
}

static void outf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void outf(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    out(buf);
}

static void redraw_prompt(void)
{
    out(PROMPT);
    if (s_len) term_write(s_line, s_len), fwrite(s_line, 1, s_len, stdout), fflush(stdout);
}

void shell_notify(const char *msg)
{
    if (!s_lock) { printf("%s\n", msg); return; }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_editing) {
        /* wipe the current line on the LCD: CR then spaces then CR */
        term_puts("\r");
        for (size_t i = 0; i < strlen(PROMPT) + s_len && i < TERM_COLS; i++) term_puts(" ");
        term_puts("\r");
        fputs("\r\033[K", stdout);
    }
    out("[kbd] ");
    out(msg);
    out("\n");
    if (s_editing) redraw_prompt();
    xSemaphoreGive(s_lock);
}

static void cmd_help(void)
{
    out("commands:\n"
        "  help          this text\n"
        "  clear         clear the screen\n"
        "  echo TEXT     print TEXT\n"
        "  kbd           keyboard status\n"
        "  kbd pair      forget bonds, pair a new keyboard\n"
        "  kbd scan      scan again now\n"
        "  uptime        seconds since boot\n"
        "  free          heap statistics\n"
        "  ver           firmware version\n"
        "  reboot        restart\n");
}

static void cmd_kbd(const char *arg)
{
    if (!arg || !*arg) {
        if (ble_kbd_connected()) outf("keyboard: %s (connected)\n", ble_kbd_device_name());
        else outf("keyboard: none. status: %s\n", ble_kbd_status());
    } else if (strcmp(arg, "pair") == 0) {
        ble_kbd_forget_and_pair();
    } else if (strcmp(arg, "scan") == 0) {
        ble_kbd_rescan();
    } else {
        out("usage: kbd [pair|scan]\n");
    }
}

static void run_line(char *line)
{
    while (*line == ' ') line++;
    if (!*line) return;
    char *arg = strchr(line, ' ');
    if (arg) { *arg++ = 0; while (*arg == ' ') arg++; }

    if (strcmp(line, "help") == 0 || strcmp(line, "?") == 0) cmd_help();
    else if (strcmp(line, "clear") == 0) { term_clear(); fputs("\033[2J\033[H", stdout); }
    else if (strcmp(line, "echo") == 0) outf("%s\n", arg ? arg : "");
    else if (strcmp(line, "kbd") == 0) cmd_kbd(arg);
    else if (strcmp(line, "uptime") == 0) outf("%lld s\n", esp_timer_get_time() / 1000000);
    else if (strcmp(line, "free") == 0) {
        outf("internal: %u free / %u total\n",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_total_size(MALLOC_CAP_INTERNAL));
        outf("psram:    %u free / %u total\n",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM), (unsigned)heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
    }
    else if (strcmp(line, "ver") == 0) {
        const esp_app_desc_t *d = esp_app_get_description();
        outf("%s %s (%s %s)\nIDF %s\n", d->project_name, d->version, d->date, d->time, d->idf_ver);
    }
    else if (strcmp(line, "reboot") == 0) { out("rebooting...\n"); vTaskDelay(pdMS_TO_TICKS(300)); esp_restart(); }
    else outf("unknown command: %s (try 'help')\n", line);
}

/* Swallow ANSI escape sequences (cursor keys etc.) until line editing supports them. */
static int s_esc_state;   /* 0 none, 1 got ESC, 2 got ESC [ */
static bool filter_escape(uint8_t c)
{
    if (s_esc_state == 0) {
        if (c == 0x1B) { s_esc_state = 1; return true; }
        return false;
    }
    if (s_esc_state == 1) {
        s_esc_state = (c == '[' || c == 'O') ? 2 : 0;
        return true;
    }
    if (c >= 0x40 && c <= 0x7E) s_esc_state = 0;   /* final byte */
    return true;
}

static void shell_task(void *arg)
{
    out("\fmini-pc  (type 'help')\n");
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_editing = true;
    redraw_prompt();
    xSemaphoreGive(s_lock);

    for (;;) {
        uint8_t c;
        if (!input_read(&c, portMAX_DELAY)) continue;
        if (filter_escape(c)) continue;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (c == '\n') {
            out("\n");
            s_editing = false;
            s_line[s_len] = 0;
            xSemaphoreGive(s_lock);
            run_line(s_line);
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_len = 0;
            s_editing = true;
            redraw_prompt();
        } else if (c == '\b' || c == 0x7F) {
            if (s_len) { s_len--; out("\b"); }
        } else if (c == 0x15) {              /* Ctrl-U: kill line */
            while (s_len) { s_len--; out("\b"); }
        } else if (c == 0x0C) {              /* Ctrl-L: clear screen */
            term_clear(); fputs("\033[2J\033[H", stdout);
            redraw_prompt();
        } else if (c == 0x03) {              /* Ctrl-C: abandon line */
            out("^C\n"); s_len = 0; redraw_prompt();
        } else if (c >= 0x20 || c >= 0x80) {
            if (s_len < SHELL_LINE_MAX - 1) {
                s_line[s_len++] = (char)c;
                char str[2] = { (char)c, 0 };
                term_write(str, 1);
                fputc(c, stdout); fflush(stdout);
            }
        }
        xSemaphoreGive(s_lock);
    }
}

void shell_start(void)
{
    s_lock = xSemaphoreCreateMutex();
    xTaskCreate(shell_task, "shell", 6 * 1024, NULL, 4, NULL);
}
