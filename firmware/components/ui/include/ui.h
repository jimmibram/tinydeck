#pragma once
/*
 * Tiny app framework. A permanent topbar sits at the top; below it the current
 * app draws into the content area. The home screen (app launcher + status) is
 * shown when no app is open. One UI task owns rendering and key dispatch.
 */
#include <stdbool.h>
#include "input.h"

#define UI_TOPBAR_H     24
#define UI_CONTENT_Y    UI_TOPBAR_H
#define UI_CONTENT_H    (300 - UI_TOPBAR_H)
#define UI_MAX_APPS     6

typedef struct {
    const char *name;                       /* label under the icon */
    void (*draw_icon)(int x, int y, int size);  /* draws the app icon at x,y (size x size) */
    void (*on_enter)(void);
    void (*on_exit)(void);
    void (*on_key)(const key_event_t *ev);  /* Esc is handled by the framework (returns home) */
    void (*on_draw)(void);                  /* paint the content area */
    bool (*needs_redraw)(void);             /* polled ~25x/s; may be NULL */
} ui_app_t;

void ui_init(void);
int  ui_register_app(const ui_app_t *app);  /* returns slot index or -1 */
void ui_start(void);
void ui_request_redraw(void);
void ui_open_app(int index);
void ui_go_home(void);
int  ui_current_app(void);                  /* -1 on home */

/* Internal, used by the home screen */
int  ui_app_count(void);
const ui_app_t *ui_app_at(int index);
void topbar_draw(void);
extern const ui_app_t ui_app_home;
