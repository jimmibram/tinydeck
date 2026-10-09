#pragma once
/*
 * Driver for the Waveshare ESP32-S3-RLCD-4.2 reflective LCD (Sitronix ST7305,
 * 400x300, 1 bit per pixel, used in landscape orientation).
 *
 * The panel has no backlight: pixels are either "ink" (dark) or "paper"
 * (reflective/light). The driver keeps a packed framebuffer in RAM and pushes
 * the whole frame to the controller over SPI on rlcd_flush().
 */
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define RLCD_WIDTH   400
#define RLCD_HEIGHT  300

/* Board wiring */
#define RLCD_PIN_SCLK  11
#define RLCD_PIN_MOSI  12
#define RLCD_PIN_DC    5
#define RLCD_PIN_CS    40
#define RLCD_PIN_RST   41
#define RLCD_PIN_TE    6

esp_err_t rlcd_init(void);

/* Fill the framebuffer. ink=true paints the whole screen dark. */
void rlcd_clear(bool ink);

/* Set one pixel in the framebuffer (no bounds check beyond the asserts). */
void rlcd_set_pixel(int x, int y, bool ink);

/* Push the framebuffer to the panel. Blocks until the SPI transfer is done. */
esp_err_t rlcd_flush(void);

/* Read-only access to the packed framebuffer (RLCD_FB_SIZE bytes), for debugging/screenshots. */
#define RLCD_FB_SIZE (RLCD_WIDTH * RLCD_HEIGHT / 8)
const uint8_t *rlcd_framebuffer(void);

/* Put the panel to sleep / wake it (display off/on). */
esp_err_t rlcd_set_power(bool on);
