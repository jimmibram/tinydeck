#pragma once
#include <stdint.h>

#define FONT8X16_WIDTH   8
#define FONT8X16_HEIGHT  16
#define FONT8X16_FIRST   0x20
#define FONT8X16_COUNT   (0x100 - FONT8X16_FIRST)   /* ASCII + Latin-1 */

/* One byte per glyph row, MSB is the leftmost pixel, 1 = ink. */
extern const uint8_t font8x16_glyphs[FONT8X16_COUNT][FONT8X16_HEIGHT];

static inline const uint8_t *font8x16_glyph(uint8_t latin1)
{
    if (latin1 < FONT8X16_FIRST) latin1 = '?';
    return font8x16_glyphs[latin1 - FONT8X16_FIRST];
}
