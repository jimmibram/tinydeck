#include "gfx.h"
#include <math.h>
#include <string.h>
#include "rlcd.h"
#include "font8x16.h"

void gfx_clear(bool ink) { rlcd_clear(ink); }
void gfx_pixel(int x, int y, bool ink) { rlcd_set_pixel(x, y, ink); }

void gfx_hline(int x, int y, int w, bool ink) { for (int i = 0; i < w; i++) rlcd_set_pixel(x + i, y, ink); }
void gfx_vline(int x, int y, int h, bool ink) { for (int i = 0; i < h; i++) rlcd_set_pixel(x, y + i, ink); }

void gfx_fill_rect(int x, int y, int w, int h, bool ink)
{
    for (int j = 0; j < h; j++) gfx_hline(x, y + j, w, ink);
}

void gfx_rect(int x, int y, int w, int h, int t, bool ink)
{
    for (int i = 0; i < t; i++) {
        gfx_hline(x + i, y + i, w - 2 * i, ink);
        gfx_hline(x + i, y + h - 1 - i, w - 2 * i, ink);
        gfx_vline(x + i, y + i, h - 2 * i, ink);
        gfx_vline(x + w - 1 - i, y + i, h - 2 * i, ink);
    }
}

/* Horizontal inset of a rounded corner at row dy (0 = outermost row) for radius r. */
static int corner_inset(int r, int dy)
{
    if (dy >= r) return 0;
    float yy = (float)(r - dy) - 0.5f;
    float xx = sqrtf((float)r * (float)r - yy * yy);
    int inset = r - (int)(xx + 0.5f);
    return inset < 0 ? 0 : inset;
}

void gfx_fill_round_rect(int x, int y, int w, int h, int r, bool ink)
{
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    for (int j = 0; j < h; j++) {
        int dy = j < r ? j : (j >= h - r ? h - 1 - j : r);
        int in = corner_inset(r, dy);
        gfx_hline(x + in, y + j, w - 2 * in, ink);
    }
}

static void plot_dotted(int x, int y, bool ink, bool dotted)
{
    if (dotted && ((x + y) & 1)) return;
    rlcd_set_pixel(x, y, ink);
}

void gfx_round_rect(int x, int y, int w, int h, int r, int t, bool ink, bool dotted)
{
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    for (int j = 0; j < h; j++) {
        int dy = j < r ? j : (j >= h - r ? h - 1 - j : r);
        int in = corner_inset(r, dy);
        int in_prev = (j == 0 || j == h - 1) ? w / 2 : corner_inset(r, (j - 1) < r ? (j - 1) : ((j - 1) >= h - r ? h - 1 - (j - 1) : r));
        int in_next = (j == h - 1) ? w / 2 : corner_inset(r, (j + 1) < r ? (j + 1) : ((j + 1) >= h - r ? h - 1 - (j + 1) : r));
        /* Outline: pixels within t of the edge, or exposed by a neighbouring row's larger inset. */
        int edge_span = in_prev > in ? in_prev - in : 0;
        int edge_span2 = in_next > in ? in_next - in : 0;
        int span = edge_span > edge_span2 ? edge_span : edge_span2;
        if (j < t || j >= h - t) {
            for (int i = in; i < w - in; i++) plot_dotted(x + i, y + j, ink, dotted);
            continue;
        }
        for (int i = 0; i < t + span && in + i < w / 2; i++) {
            plot_dotted(x + in + i, y + j, ink, dotted);
            plot_dotted(x + w - 1 - in - i, y + j, ink, dotted);
        }
    }
}

/* Decode one UTF-8 code point; returns bytes consumed (>=1). */
static int utf8_next(const char *s, uint32_t *cp)
{
    uint8_t b = (uint8_t)s[0];
    if (b < 0x80) { *cp = b; return 1; }
    int n = (b & 0xE0) == 0xC0 ? 1 : (b & 0xF0) == 0xE0 ? 2 : (b & 0xF8) == 0xF0 ? 3 : 0;
    uint32_t c = n == 1 ? (b & 0x1F) : n == 2 ? (b & 0x0F) : (b & 0x07);
    int i = 1;
    for (; i <= n && ((uint8_t)s[i] & 0xC0) == 0x80; i++) c = (c << 6) | ((uint8_t)s[i] & 0x3F);
    *cp = (i == n + 1) ? c : '?';
    return i;
}

static void draw_glyph(int x, int y, uint8_t ch, bool ink, int scale)
{
    const uint8_t *g = font8x16_glyph(ch);
    for (int r = 0; r < FONT8X16_HEIGHT; r++) {
        uint8_t bits = g[r];
        for (int c = 0; c < FONT8X16_WIDTH; c++) {
            if (!((bits >> (7 - c)) & 1)) continue;
            if (scale == 1) rlcd_set_pixel(x + c, y + r, ink);
            else gfx_fill_rect(x + c * scale, y + r * scale, scale, scale, ink);
        }
    }
}

int gfx_text(int x, int y, const char *s, bool ink, int scale)
{
    if (scale < 1) scale = 1;
    int x0 = x;
    while (*s) {
        uint32_t cp;
        s += utf8_next(s, &cp);
        draw_glyph(x, y, cp < 0x100 ? (uint8_t)cp : '?', ink, scale);
        x += FONT8X16_WIDTH * scale;
    }
    return x - x0;
}

int gfx_text_width(const char *s, int scale)
{
    int n = 0;
    while (*s) { uint32_t cp; s += utf8_next(s, &cp); n++; }
    return n * FONT8X16_WIDTH * (scale < 1 ? 1 : scale);
}

int gfx_text_right(int right_x, int y, const char *s, bool ink, int scale)
{
    int w = gfx_text_width(s, scale);
    return gfx_text(right_x - w, y, s, ink, scale);
}

int gfx_text_center(int cx, int y, const char *s, bool ink, int scale)
{
    int w = gfx_text_width(s, scale);
    return gfx_text(cx - w / 2, y, s, ink, scale);
}

void gfx_icon(int x, int y, const char *const *rows, int nrows, bool ink)
{
    for (int r = 0; r < nrows; r++) {
        for (int c = 0; rows[r][c]; c++) {
            if (rows[r][c] == '#') rlcd_set_pixel(x + c, y + r, ink);
        }
    }
}
