#include "ui.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "gfx.h"
#include "rlcd.h"

static const char *TAG = "ui";

static ui_app_t s_apps[UI_MAX_APPS];
static int s_napps;
static int s_current = -1;
static volatile bool s_redraw = true;
static int64_t s_last_periodic_us;

#define PERIODIC_REDRAW_US (1000 * 1000)   /* topbar/status refresh */

void ui_init(void) { s_current = -1; s_redraw = true; }

int ui_register_app(const ui_app_t *app)
{
    if (s_napps >= UI_MAX_APPS) return -1;
    s_apps[s_napps] = *app;
    return s_napps++;
}

int ui_app_count(void) { return s_napps; }
const ui_app_t *ui_app_at(int i) { return (i >= 0 && i < s_napps) ? &s_apps[i] : NULL; }
int ui_current_app(void) { return s_current; }
void ui_request_redraw(void) { s_redraw = true; }

static const ui_app_t *active(void) { return s_current < 0 ? &ui_app_home : &s_apps[s_current]; }

void ui_open_app(int index)
{
    if (index < 0 || index >= s_napps) return;
    if (active()->on_exit) active()->on_exit();
    s_current = index;
    if (active()->on_enter) active()->on_enter();
    s_redraw = true;
}

void ui_go_home(void)
{
    if (s_current < 0) return;
    if (active()->on_exit) active()->on_exit();
    s_current = -1;
    if (ui_app_home.on_enter) ui_app_home.on_enter();
    s_redraw = true;
}

static void render(void)
{
    gfx_clear(false);
    const ui_app_t *a = active();
    if (a->on_draw) a->on_draw();
    topbar_draw();
    rlcd_flush();
}

static void ui_task(void *arg)
{
    if (ui_app_home.on_enter) ui_app_home.on_enter();
    for (;;) {
        key_event_t ev;
        if (input_read(&ev, pdMS_TO_TICKS(40))) {
            if (ev.code == KEY_ESC && s_current >= 0) {
                ui_go_home();
            } else {
                const ui_app_t *a = active();
                if (a->on_key) a->on_key(&ev);
            }
            /* drain any further queued keys before drawing */
            while (input_read(&ev, 0)) {
                if (ev.code == KEY_ESC && s_current >= 0) ui_go_home();
                else if (active()->on_key) active()->on_key(&ev);
            }
        }
        int64_t now = esp_timer_get_time();
        bool periodic = (now - s_last_periodic_us) >= PERIODIC_REDRAW_US;
        const ui_app_t *a = active();
        if (s_redraw || periodic || (a->needs_redraw && a->needs_redraw())) {
            s_redraw = false;
            s_last_periodic_us = now;
            render();
        }
    }
}

void ui_start(void)
{
    xTaskCreatePinnedToCore(ui_task, "ui", 8192, NULL, 5, NULL, 1);
    ESP_LOGI(TAG, "started with %d app(s)", s_napps);
}
