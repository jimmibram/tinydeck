/* Terminal app: the shell on a light-on-dark text grid with a margin. */
#include "ui.h"
#include "gfx.h"
#include "term.h"
#include "shell.h"

#define MARGIN 8
#define TERM_X MARGIN
#define TERM_Y (UI_CONTENT_Y + MARGIN)
#define TERM_W ((GFX_W - 2 * MARGIN) / 8)                  /* 48 columns */
#define TERM_H ((UI_CONTENT_H - 2 * MARGIN) / 16)          /* 16 rows */

static void terminal_icon(int x, int y, int size)
{
    gfx_fill_round_rect(x, y, size, size, 12, true);
    gfx_text(x + (size - 32) / 2, y + (size - 32) / 2, ">_", false, 2);
}

static void terminal_enter(void)
{
    term_set_cursor_visible(true);
    shell_on_show();
}

static void terminal_key(const key_event_t *ev) { shell_handle_key(ev); }

static bool terminal_needs_redraw(void) { return term_take_dirty(); }

static void terminal_draw(void)
{
    gfx_fill_rect(0, UI_CONTENT_Y, GFX_W, UI_CONTENT_H, true);
    term_render(TERM_X, TERM_Y, true);
}

const ui_app_t ui_app_terminal = {
    .name = "Terminal",
    .draw_icon = terminal_icon,
    .on_enter = terminal_enter,
    .on_key = terminal_key,
    .on_draw = terminal_draw,
    .needs_redraw = terminal_needs_redraw,
};

int app_terminal_cols(void) { return TERM_W; }
int app_terminal_rows(void) { return TERM_H; }
