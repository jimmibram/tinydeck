#pragma once
/* Small 1-bit drawing library on top of the rlcd framebuffer. ink=true draws dark pixels. */
#include <stdbool.h>
#include <stdint.h>

#define GFX_W 400
#define GFX_H 300

void gfx_clear(bool ink);
void gfx_pixel(int x, int y, bool ink);
void gfx_hline(int x, int y, int w, bool ink);
void gfx_vline(int x, int y, int h, bool ink);
void gfx_fill_rect(int x, int y, int w, int h, bool ink);
void gfx_rect(int x, int y, int w, int h, int thickness, bool ink);
/* Rounded rectangle. dotted=true draws every other outline pixel (used for empty slots). */
void gfx_round_rect(int x, int y, int w, int h, int r, int thickness, bool ink, bool dotted);
void gfx_fill_round_rect(int x, int y, int w, int h, int r, bool ink);

/* Text with the 8x16 font (UTF-8 input, Latin-1 glyphs). Transparent background. Returns width in pixels. */
int gfx_text(int x, int y, const char *utf8, bool ink, int scale);
int gfx_text_width(const char *utf8, int scale);
int gfx_text_right(int right_x, int y, const char *utf8, bool ink, int scale);   /* right-aligned */
int gfx_text_center(int cx, int y, const char *utf8, bool ink, int scale);

/* Pixel-art icon given as rows of '#' (ink) and '.' (transparent). */
void gfx_icon(int x, int y, const char *const *rows, int nrows, bool ink);
