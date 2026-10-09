#include "term.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "rlcd.h"
#include "font8x16.h"

static const char *TAG = "term";

#define CELL_W FONT8X16_WIDTH
#define CELL_H FONT8X16_HEIGHT
#define ALL_ROWS_DIRTY ((1u << TERM_ROWS) - 1)

static uint8_t s_cells[TERM_ROWS][TERM_COLS];
static uint32_t s_dirty;
static int s_cx, s_cy;
static bool s_cursor_visible = true;
static int s_cursor_drawn_row = -1;
static SemaphoreHandle_t s_lock;
static uint32_t s_utf8_cp;
static int s_utf8_left;

static void mark_dirty(int row) { s_dirty |= 1u << row; }

static void scroll_up(void)
{
    memmove(s_cells[0], s_cells[1], sizeof(s_cells[0]) * (TERM_ROWS - 1));
    memset(s_cells[TERM_ROWS - 1], ' ', TERM_COLS);
    s_dirty = ALL_ROWS_DIRTY;
}

static void newline(void)
{
    s_cx = 0;
    if (++s_cy >= TERM_ROWS) {
        s_cy = TERM_ROWS - 1;
        scroll_up();
    }
}

static void put_latin1(uint8_t ch)
{
    switch (ch) {
    case '\n': mark_dirty(s_cy); newline(); mark_dirty(s_cy); return;
    case '\r': mark_dirty(s_cy); s_cx = 0; return;
    case '\f':
        memset(s_cells, ' ', sizeof(s_cells));
        s_cx = s_cy = 0;
        s_dirty = ALL_ROWS_DIRTY;
        return;
    case '\b':
    case 0x7F:
        if (s_cx > 0) {
            s_cx--;
        } else if (s_cy > 0) {
            s_cy--; s_cx = TERM_COLS - 1;
        }
        s_cells[s_cy][s_cx] = ' ';
        mark_dirty(s_cy);
        return;
    case '\t': {
        int n = 4 - (s_cx & 3);
        while (n--) put_latin1(' ');
        return;
    }
    case 0x07: return;                     /* bell: ignored */
    default:
        if (ch < 0x20) return;             /* other control chars: ignored */
    }
    if (s_cx >= TERM_COLS) newline();
    s_cells[s_cy][s_cx] = ch;
    mark_dirty(s_cy);
    if (++s_cx >= TERM_COLS) {             /* deferred wrap: cursor parks past the last column */
        s_cx = TERM_COLS;
    }
}

/* Minimal UTF-8 decoder: Latin-1 range is rendered, anything else becomes '?'. */
static void put_byte(uint8_t b)
{
    if (s_utf8_left) {
        if ((b & 0xC0) == 0x80) {
            s_utf8_cp = (s_utf8_cp << 6) | (b & 0x3F);
            if (--s_utf8_left == 0) put_latin1(s_utf8_cp < 0x100 ? (uint8_t)s_utf8_cp : '?');
            return;
        }
        s_utf8_left = 0;                   /* broken sequence: fall through with this byte */
        put_latin1('?');
    }
    if (b < 0x80)             { put_latin1(b); }
    else if ((b & 0xE0) == 0xC0) { s_utf8_cp = b & 0x1F; s_utf8_left = 1; }
    else if ((b & 0xF0) == 0xE0) { s_utf8_cp = b & 0x0F; s_utf8_left = 2; }
    else if ((b & 0xF8) == 0xF0) { s_utf8_cp = b & 0x07; s_utf8_left = 3; }
    else                         { put_latin1('?'); }
}

static void draw_cell(int col, int row, uint8_t ch, bool inverse)
{
    const uint8_t *g = font8x16_glyph(ch);
    int x0 = col * CELL_W, y0 = row * CELL_H;
    for (int r = 0; r < CELL_H; r++) {
        uint8_t bits = g[r];
        if (inverse) bits = (uint8_t)~bits;
        for (int c = 0; c < CELL_W; c++) {
            rlcd_set_pixel(x0 + c, y0 + r, (bits >> (7 - c)) & 1);
        }
    }
}

static void render_locked(void)
{
    if (s_cursor_drawn_row >= 0) mark_dirty(s_cursor_drawn_row);
    int ccol = s_cx >= TERM_COLS ? TERM_COLS - 1 : s_cx;
    for (int row = 0; row < TERM_ROWS; row++) {
        if (!(s_dirty & (1u << row))) continue;
        for (int col = 0; col < TERM_COLS; col++) {
            bool cur = s_cursor_visible && row == s_cy && col == ccol;
            draw_cell(col, row, s_cells[row][col], cur);
        }
    }
    s_cursor_drawn_row = s_cursor_visible ? s_cy : -1;
    s_dirty = 0;
}

static void term_task(void *arg)
{
    for (;;) {
        bool need_flush = false;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (s_dirty) {
            render_locked();
            need_flush = true;
        }
        xSemaphoreGive(s_lock);
        if (need_flush) rlcd_flush();
        vTaskDelay(pdMS_TO_TICKS(40));
    }
}

esp_err_t term_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;
    memset(s_cells, ' ', sizeof(s_cells));
    s_dirty = ALL_ROWS_DIRTY;
    if (xTaskCreatePinnedToCore(term_task, "term", 4096, NULL, 5, NULL, 1) != pdPASS) return ESP_FAIL;
    ESP_LOGI(TAG, "%dx%d cells", TERM_COLS, TERM_ROWS);
    return ESP_OK;
}

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
    mark_dirty(s_cy);
    xSemaphoreGive(s_lock);
}

void term_get_cursor(int *col, int *row)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (col) *col = s_cx;
    if (row) *row = s_cy;
    xSemaphoreGive(s_lock);
}
