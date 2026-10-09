#include "term.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "rlcd.h"
#include "font8x16.h"

static const char *TAG = "term";

#define CELL_W FONT8X16_WIDTH
#define CELL_H FONT8X16_HEIGHT

static uint8_t s_cells[TERM_MAX_ROWS][TERM_MAX_COLS];
static int s_cols = TERM_MAX_COLS, s_rows = TERM_MAX_ROWS;
static bool s_dirty;
static int s_cx, s_cy;
static bool s_cursor_visible = true;
static SemaphoreHandle_t s_lock;
static uint32_t s_utf8_cp;
static int s_utf8_left;

static void scroll_up(void)
{
    memmove(s_cells[0], s_cells[1], sizeof(s_cells[0]) * (TERM_MAX_ROWS - 1));
    memset(s_cells[s_rows - 1], ' ', TERM_MAX_COLS);
}

static void newline(void)
{
    s_cx = 0;
    if (++s_cy >= s_rows) {
        s_cy = s_rows - 1;
        scroll_up();
    }
}

static void put_latin1(uint8_t ch)
{
    s_dirty = true;
    switch (ch) {
    case '\n': newline(); return;
    case '\r': s_cx = 0; return;
    case '\f':
        memset(s_cells, ' ', sizeof(s_cells));
        s_cx = s_cy = 0;
        return;
    case '\b':
    case 0x7F:
        if (s_cx > 0) s_cx--;
        else if (s_cy > 0) { s_cy--; s_cx = s_cols - 1; }
        s_cells[s_cy][s_cx] = ' ';
        return;
    case '\t': {
        int n = 4 - (s_cx & 3);
        while (n--) put_latin1(' ');
        return;
    }
    default:
        if (ch < 0x20) return;
    }
    if (s_cx >= s_cols) newline();
    s_cells[s_cy][s_cx] = ch;
    s_cx++;                                   /* may park at s_cols: deferred wrap */
}

static void put_byte(uint8_t b)
{
    if (s_utf8_left) {
        if ((b & 0xC0) == 0x80) {
            s_utf8_cp = (s_utf8_cp << 6) | (b & 0x3F);
            if (--s_utf8_left == 0) put_latin1(s_utf8_cp < 0x100 ? (uint8_t)s_utf8_cp : '?');
            return;
        }
        s_utf8_left = 0;
        put_latin1('?');
    }
    if (b < 0x80)                { put_latin1(b); }
    else if ((b & 0xE0) == 0xC0) { s_utf8_cp = b & 0x1F; s_utf8_left = 1; }
    else if ((b & 0xF0) == 0xE0) { s_utf8_cp = b & 0x0F; s_utf8_left = 2; }
    else if ((b & 0xF8) == 0xF0) { s_utf8_cp = b & 0x07; s_utf8_left = 3; }
    else                         { put_latin1('?'); }
}

static void draw_cell(int x0, int y0, uint8_t ch, bool light_on_dark)
{
    const uint8_t *g = font8x16_glyph(ch);
    for (int r = 0; r < CELL_H; r++) {
        uint8_t bits = g[r];
        for (int c = 0; c < CELL_W; c++) {
            bool glyph = (bits >> (7 - c)) & 1;
            rlcd_set_pixel(x0 + c, y0 + r, glyph != light_on_dark);   /* ink where glyph differs from background */
        }
    }
}

void term_render(int x0, int y0, bool inverse)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int ccol = s_cx >= s_cols ? s_cols - 1 : s_cx;
    for (int row = 0; row < s_rows; row++) {
        for (int col = 0; col < s_cols; col++) {
            bool cursor = s_cursor_visible && row == s_cy && col == ccol;
            draw_cell(x0 + col * CELL_W, y0 + row * CELL_H, s_cells[row][col], inverse != cursor);
        }
    }
    xSemaphoreGive(s_lock);
}

bool term_take_dirty(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool d = s_dirty;
    s_dirty = false;
    xSemaphoreGive(s_lock);
    return d;
}

esp_err_t term_init(int cols, int rows)
{
    if (cols < 1 || cols > TERM_MAX_COLS || rows < 1 || rows > TERM_MAX_ROWS) return ESP_ERR_INVALID_ARG;
    s_cols = cols;
    s_rows = rows;
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;
    memset(s_cells, ' ', sizeof(s_cells));
    s_dirty = true;
    ESP_LOGI(TAG, "%dx%d cells", s_cols, s_rows);
    return ESP_OK;
}

int term_cols(void) { return s_cols; }
int term_rows(void) { return s_rows; }

void term_write(const char *s, size_t len)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (size_t i = 0; i < len; i++) put_byte((uint8_t)s[i]);
    xSemaphoreGive(s_lock);
}

void term_puts(const char *s) { term_write(s, strlen(s)); }

void term_printf(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > (int)sizeof(buf) - 1) n = sizeof(buf) - 1;
    term_write(buf, (size_t)n);
}

void term_clear(void) { term_write("\f", 1); }

void term_set_cursor_visible(bool visible)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_cursor_visible = visible;
    s_dirty = true;
    xSemaphoreGive(s_lock);
}
