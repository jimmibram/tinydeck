#pragma once
/*
 * Character-cell text terminal. Accepts UTF-8 (Latin-1 range), \n, \r, \b, \t and
 * clears on form feed (\f). Rendering is done by the caller (the UI task) via
 * term_render(), which paints the whole grid at a pixel offset, optionally inverted
 * (light text on dark background).
 */
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#define TERM_MAX_COLS  50
#define TERM_MAX_ROWS  18

esp_err_t term_init(int cols, int rows);
int  term_cols(void);
int  term_rows(void);
void term_write(const char *s, size_t len);
void term_puts(const char *s);
void term_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void term_clear(void);
void term_set_cursor_visible(bool visible);

/* True once if anything changed since the last call. */
bool term_take_dirty(void);
/* Draw the grid with its top-left corner at (x0, y0). inverse=true: light glyphs on dark cells. */
void term_render(int x0, int y0, bool inverse);
