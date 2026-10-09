#include "ui.h"
#include <stdio.h>
#include "gfx.h"
#include "sensors.h"
#include "ble_kbd.h"

/* 13x10 WiFi symbol */
static const char *const ICON_WIFI[] = {
    "....#####....",
    "..##.....##..",
    ".#.........#.",
    "#...#####...#",
    "...#.....#...",
    "..#.......#..",
    ".....###.....",
    "....#...#....",
    "......#......",
    "......#......",
};
static const int ICON_WIFI_H = 10;

static void draw_battery(int x, int y, int pct, bool usb)
{
    /* body 22x10, nub 2x4, drawn in light on the dark bar */
    gfx_rect(x, y, 22, 10, 1, false);
    gfx_fill_rect(x + 22, y + 3, 2, 4, false);
    if (usb) {
        gfx_text(x + 3, y - 3, "~", false, 1);           /* lightning-ish mark: USB powered */
        return;
    }
    int w = pct * 18 / 100;
    if (w > 0) gfx_fill_rect(x + 2, y + 2, w, 6, false);
}

void topbar_draw(void)
{
    gfx_fill_rect(0, 0, GFX_W, UI_TOPBAR_H, true);
    gfx_text(8, 4, "BRAMCONSOLE", false, 1);

    sensors_t s;
    sensors_get(&s);

    int x = GFX_W - 8;
    /* battery at the far right */
    bool usb = !s.battery_valid || s.battery_pct < 0;
    x -= 24;
    draw_battery(x, 7, usb ? 0 : s.battery_pct, usb);
    char buf[16];
    if (usb) snprintf(buf, sizeof(buf), "USB");
    else snprintf(buf, sizeof(buf), "%d%%", s.battery_pct);
    x -= 6 + gfx_text_width(buf, 1);
    gfx_text(x, 4, buf, false, 1);

    /* wifi left of battery: no network stack yet, show symbol struck through */
    x -= 14 + 13;
    gfx_icon(x, 7, ICON_WIFI, ICON_WIFI_H, false);
    for (int i = 0; i < 12; i++) gfx_pixel(x + i, 7 + 10 - i, false);   /* slash = off */

    /* keyboard indicator */
    if (ble_kbd_connected()) {
        x -= 14 + gfx_text_width("KB", 1);
        gfx_text(x, 4, "KB", false, 1);
    }
}
