#include "shell.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_app_desc.h"
#include "term.h"
#include "ble_kbd.h"
#include "sensors.h"
#include "ui.h"

#define PROMPT "> "
#define SHELL_LINE_MAX 200

static SemaphoreHandle_t s_lock;
static char s_line[SHELL_LINE_MAX];
static size_t s_len;
static bool s_editing;
static bool s_shown;

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
    if (s_len) { term_write(s_line, s_len); fwrite(s_line, 1, s_len, stdout); fflush(stdout); }
}

void shell_notify(const char *msg)
{
    if (!s_lock) { printf("%s\n", msg); return; }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_editing) {
        term_puts("\r");
        for (size_t i = 0; i < strlen(PROMPT) + s_len && i < (size_t)term_cols(); i++) term_puts(" ");
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
        "  help           this text\n"
        "  clear          clear the screen\n"
        "  echo TEXT      print TEXT\n"
        "  kbd [pair|scan] keyboard status / pairing\n"
        "  status         sensors and power\n"
        "  date [YYYY-MM-DD HH:MM:SS]  show/set clock\n"
        "  uptime, free, ver, reboot\n"
        "  home / Esc     back to the start page\n");
}

static void cmd_kbd(const char *arg)
{
    if (!arg || !*arg) {
        if (ble_kbd_connected()) outf("keyboard: %s (connected)\n", ble_kbd_device_name());
        else outf("keyboard: none. status: %s\n", ble_kbd_status());
    } else if (strcmp(arg, "pair") == 0) ble_kbd_forget_and_pair();
    else if (strcmp(arg, "scan") == 0) ble_kbd_rescan();
    else out("usage: kbd [pair|scan]\n");
}

static void cmd_status(void)
{
    sensors_t s;
    sensors_get(&s);
    if (s.env_valid) outf("temperature %.1f C, humidity %.0f %%\n", s.temperature_c, s.humidity_pct);
    else out("temperature/humidity: no reading\n");
    if (s.battery_valid) {
        if (s.battery_pct < 0) outf("power: USB (%.2f V on battery pin)\n", s.battery_volts);
        else outf("battery: %d%% (%.2f V)\n", s.battery_pct, s.battery_volts);
    }
}

static void cmd_date(const char *arg)
{
    sensors_t s;
    if (arg && *arg) {
        struct tm t = {0};
        if (sscanf(arg, "%d-%d-%d %d:%d:%d", &t.tm_year, &t.tm_mon, &t.tm_mday, &t.tm_hour, &t.tm_min, &t.tm_sec) < 5) {
            out("usage: date YYYY-MM-DD HH:MM[:SS]\n");
            return;
        }
        t.tm_year -= 1900;
        t.tm_mon -= 1;
        time_t tt = mktime(&t);
        struct tm *n = localtime(&tt);
        if (n) t.tm_wday = n->tm_wday;
        if (sensors_set_rtc(&t) != ESP_OK) { out("failed to set clock\n"); return; }
    }
    sensors_get(&s);
    if (s.rtc_valid) outf("%04d-%02d-%02d %02d:%02d:%02d\n", s.rtc_time.tm_year + 1900, s.rtc_time.tm_mon + 1,
                          s.rtc_time.tm_mday, s.rtc_time.tm_hour, s.rtc_time.tm_min, s.rtc_time.tm_sec);
    else out("clock not set (date YYYY-MM-DD HH:MM:SS)\n");
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
    else if (strcmp(line, "status") == 0) cmd_status();
    else if (strcmp(line, "date") == 0) cmd_date(arg);
    else if (strcmp(line, "home") == 0) ui_go_home();
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

void shell_on_show(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (!s_shown) {
        s_shown = true;
        out("BRAMCONSOLE terminal  (help, Esc = home)\n");
        s_editing = true;
        redraw_prompt();
    }
    xSemaphoreGive(s_lock);
}

void shell_handle_key(const key_event_t *ev)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    switch (ev->code) {
    case KEY_ENTER:
        out("\n");
        s_editing = false;
        s_line[s_len] = 0;
        xSemaphoreGive(s_lock);
        run_line(s_line);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_len = 0;
        s_editing = true;
        redraw_prompt();
        break;
    case KEY_BACKSPACE:
        if (s_len) {
            /* remove one UTF-8 code point */
            do { s_len--; } while (s_len && ((uint8_t)s_line[s_len] & 0xC0) == 0x80);
            out("\b");
        }
        break;
    case KEY_CHAR:
        if (ev->mods & KMOD_CTRL) {
            if (ev->ch == 'u') { while (s_len) { s_len--; out("\b"); } }
            else if (ev->ch == 'l') { term_clear(); fputs("\033[2J\033[H", stdout); redraw_prompt(); }
            else if (ev->ch == 'c') { out("^C\n"); s_len = 0; redraw_prompt(); }
            break;
        }
        {
            char u[4];
            size_t n = utf8_encode(ev->ch, u);
            if (s_len + n < SHELL_LINE_MAX - 1) {
                memcpy(s_line + s_len, u, n);
                s_len += n;
                term_write(u, n);
                fwrite(u, 1, n, stdout); fflush(stdout);
            }
        }
        break;
    default:
        break;   /* arrows etc: not yet */
    }
    xSemaphoreGive(s_lock);
}

void shell_init(void) { s_lock = xSemaphoreCreateMutex(); }
