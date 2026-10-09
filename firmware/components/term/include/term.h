#pragma once
/*
 * Character-cell text terminal rendered onto the reflective LCD.
 * 50 columns x 18 rows with the 8x16 font. Accepts UTF-8 (Latin-1 range),
 * \n, \r, \b, \t and clears the screen on form feed (\f).
 * A background task repaints dirty rows and flushes the panel.
 */
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#define TERM_COLS  50
#define TERM_ROWS  18

esp_err_t term_init(void);
void term_write(const char *s, size_t len);
void term_puts(const char *s);
void term_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void term_clear(void);
void term_set_cursor_visible(bool visible);
void term_get_cursor(int *col, int *row);
