/* Home screen: 3x2 app launcher on the left, live status on the right. */
#include "ui.h"
#include <stdio.h>
#include <string.h>
#include "gfx.h"
#include "sensors.h"
#include "ble_kbd.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"

#define GRID_COLS 3
#define GRID_ROWS 2
#define PANEL_W   240          /* left panel width */
#define CELL_W    (PANEL_W / GRID_COLS)
#define CELL_H    (UI_CONTENT_H / GRID_ROWS)
#define ICON      56
#define ICON_R    12
#define STATUS_X  (PANEL_W + 12)

static int s_sel;

static void home_enter(void) { ui_request_redraw(); }

static void home_key(const key_event_t *ev)
{
    int col = s_sel % GRID_COLS, row = s_sel / GRID_COLS;
    switch (ev->code) {
    case KEY_LEFT:  col = (col + GRID_COLS - 1) % GRID_COLS; break;
    case KEY_RIGHT: col = (col + 1) % GRID_COLS; break;
    case KEY_UP:    row = (row + GRID_ROWS - 1) % GRID_ROWS; break;
    case KEY_DOWN:  row = (row + 1) % GRID_ROWS; break;
    case KEY_ENTER:
        if (s_sel < ui_app_count()) ui_open_app(s_sel);
        return;
    case KEY_CHAR:
        if (ev->ch >= '1' && ev->ch <= '6') {
            int i = (int)(ev->ch - '1');
            if (i < ui_app_count()) ui_open_app(i);
        }
        return;
    default: return;
    }
    s_sel = row * GRID_COLS + col;
    ui_request_redraw();
}

static void draw_slot(int index)
{
    int col = index % GRID_COLS, row = index / GRID_COLS;
    int cx = col * CELL_W + CELL_W / 2;
    int top = UI_CONTENT_Y + row * CELL_H;
    int ix = cx - ICON / 2;
    int iy = top + (CELL_H - (ICON + 6 + 16)) / 2;
    const ui_app_t *app = ui_app_at(index);

    if (app) {
        if (app->draw_icon) app->draw_icon(ix, iy, ICON);
        else {
            gfx_fill_round_rect(ix, iy, ICON, ICON, ICON_R, true);
            char initial[2] = { app->name[0], 0 };
            gfx_text_center(cx, iy + (ICON - 32) / 2, initial, false, 2);
        }
        gfx_text_center(cx, iy + ICON + 6, app->name, true, 1);
    } else {
        gfx_round_rect(ix, iy, ICON, ICON, ICON_R, 1, true, true);
    }
    if (index == s_sel) {
        gfx_round_rect(ix - 5, iy - 5, ICON + 10, ICON + 10, ICON_R + 4, 2, true, false);
    }
}

static void status_line(int *y, const char *label, const char *value)
{
    gfx_text(STATUS_X, *y, label, true, 1);
    gfx_text(STATUS_X + 7 * 8, *y, value, true, 1);
    *y += 20;
}

static void draw_status(void)
{
    gfx_vline(PANEL_W + 2, UI_CONTENT_Y + 8, UI_CONTENT_H - 16, true);
    int y = UI_CONTENT_Y + 10;
    gfx_fill_round_rect(STATUS_X - 4, y - 2, 6 * 8 + 8, 20, 4, true);
    gfx_text(STATUS_X, y, "STATUS", false, 1);
    y += 30;

    sensors_t s;
    sensors_get(&s);
    char v[32];

    if (s.rtc_valid) snprintf(v, sizeof(v), "%02d:%02d:%02d", s.rtc_time.tm_hour, s.rtc_time.tm_min, s.rtc_time.tm_sec);
    else snprintf(v, sizeof(v), "--:--:--");
    status_line(&y, "Time", v);
    if (s.rtc_valid) snprintf(v, sizeof(v), "%04d-%02d-%02d", s.rtc_time.tm_year + 1900, s.rtc_time.tm_mon + 1, s.rtc_time.tm_mday);
    else snprintf(v, sizeof(v), "not set");
    status_line(&y, "Date", v);

    if (s.env_valid) snprintf(v, sizeof(v), "%.1f\xc2\xb0" "C", s.temperature_c);
    else snprintf(v, sizeof(v), "--");
    status_line(&y, "Temp", v);
    if (s.env_valid) snprintf(v, sizeof(v), "%.0f %%", s.humidity_pct);
    else snprintf(v, sizeof(v), "--");
    status_line(&y, "Humid", v);

    if (!s.battery_valid) snprintf(v, sizeof(v), "--");
    else if (s.battery_pct < 0) snprintf(v, sizeof(v), "USB %.2fV", s.battery_volts);
    else snprintf(v, sizeof(v), "%d%% %.2fV", s.battery_pct, s.battery_volts);
    status_line(&y, "Power", v);

    if (ble_kbd_connected()) snprintf(v, sizeof(v), "%.11s", ble_kbd_device_name());
    else snprintf(v, sizeof(v), "searching");
    status_line(&y, "Keyb", v);

    status_line(&y, "WiFi", "off");

    snprintf(v, sizeof(v), "%u KB", (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
    status_line(&y, "RAM", v);

    int64_t up = esp_timer_get_time() / 1000000;
    snprintf(v, sizeof(v), "%lldh %02lldm %02llds", up / 3600, (up / 60) % 60, up % 60);
    status_line(&y, "Up", v);
}

static void home_draw(void)
{
    for (int i = 0; i < GRID_COLS * GRID_ROWS; i++) draw_slot(i);
    draw_status();
}

const ui_app_t ui_app_home = {
    .name = "Home",
    .on_enter = home_enter,
    .on_key = home_key,
    .on_draw = home_draw,
};
