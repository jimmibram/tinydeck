#include "rlcd.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_heap_caps.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "rlcd";

/*
 * ST7305 memory layout for this panel in landscape:
 *  - the controller's "column" axis runs along our Y (300 px = 25 column
 *    addresses x 12 px), the "page" axis along our X (400 px = 200 pages x 2 px)
 *  - each byte packs a 2 (x) by 4 (y) block of pixels
 *  - 3 bytes per column address, 75 bytes per page, 15000 bytes per frame
 */
#define RLCD_FB_SIZE   (RLCD_WIDTH * RLCD_HEIGHT / 8)
#define RLCD_BYTES_PER_PAGE  (RLCD_HEIGHT / 4)

#define RLCD_SPI_HOST  SPI3_HOST
#define RLCD_SPI_HZ    (20 * 1000 * 1000)

static esp_lcd_panel_io_handle_t s_io;
static SemaphoreHandle_t s_flush_done;
static uint8_t *s_fb;

static bool IRAM_ATTR on_color_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *ev, void *ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_flush_done, &woken);
    return woken == pdTRUE;
}

static esp_err_t cmd(uint8_t c, const uint8_t *p, size_t n)
{
    return esp_lcd_panel_io_tx_param(s_io, c, p, n);
}
#define CMD(c, ...) do { const uint8_t _p[] = { __VA_ARGS__ }; ESP_RETURN_ON_ERROR(cmd((c), _p, sizeof(_p)), TAG, "cmd 0x%02x", (c)); } while (0)
#define CMD0(c)     ESP_RETURN_ON_ERROR(cmd((c), NULL, 0), TAG, "cmd 0x%02x", (c))

static void hw_reset(void)
{
    gpio_set_level(RLCD_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(RLCD_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(RLCD_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
}

/* Init sequence from the Waveshare reference code for this panel. */
static esp_err_t panel_init(void)
{
    hw_reset();
    CMD(0xD6, 0x17, 0x02);                          /* NVM load control */
    CMD(0xD1, 0x01);                                /* booster enable */
    CMD(0xC0, 0x11, 0x04);                          /* gate voltage */
    CMD(0xC1, 0x69, 0x69, 0x69, 0x69);              /* VSHP */
    CMD(0xC2, 0x19, 0x19, 0x19, 0x19);              /* VSLP */
    CMD(0xC4, 0x4B, 0x4B, 0x4B, 0x4B);              /* VSHN */
    CMD(0xC5, 0x19, 0x19, 0x19, 0x19);              /* VSLN */
    CMD(0xD8, 0x80, 0xE9);                          /* OSC */
    CMD(0xB2, 0x02);                                /* frame rate */
    CMD(0xB3, 0xE5, 0xF6, 0x05, 0x46, 0x77, 0x77, 0x77, 0x77, 0x76, 0x45); /* update period, HPM */
    CMD(0xB4, 0x05, 0x46, 0x77, 0x77, 0x77, 0x77, 0x76, 0x45);             /* update period, LPM */
    CMD(0x62, 0x32, 0x03, 0x1F);                    /* gate timing */
    CMD(0xB7, 0x13);                                /* source EQ */
    CMD(0xB0, 0x64);                                /* gate line setting: 400 lines */
    CMD0(0x11);                                     /* sleep out */
    vTaskDelay(pdMS_TO_TICKS(200));
    CMD(0xC9, 0x00);                                /* source voltage select */
    CMD(0x36, 0x48);                                /* memory data access control */
    CMD(0x3A, 0x11);                                /* data format: 1bpp */
    CMD(0xB9, 0x20);                                /* gamma mode: mono */
    CMD(0xB8, 0x29);                                /* panel setting */
    CMD0(0x21);                                     /* display inversion on */
    CMD(0x2A, 0x12, 0x2A);                          /* column window */
    CMD(0x2B, 0x00, 0xC7);                          /* row window */
    CMD(0x35, 0x00);                                /* tearing effect line on */
    CMD(0xD0, 0xFF);                                /* auto power down */
    CMD0(0x38);                                     /* high power mode */
    CMD0(0x29);                                     /* display on */
    return ESP_OK;
}

esp_err_t rlcd_init(void)
{
    s_flush_done = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_flush_done, ESP_ERR_NO_MEM, TAG, "sem");

    s_fb = heap_caps_malloc(RLCD_FB_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    ESP_RETURN_ON_FALSE(s_fb, ESP_ERR_NO_MEM, TAG, "framebuffer");
    memset(s_fb, 0xFF, RLCD_FB_SIZE);

    gpio_config_t rst = {
        .pin_bit_mask = 1ULL << RLCD_PIN_RST,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&rst), TAG, "rst gpio");
    gpio_set_level(RLCD_PIN_RST, 1);

    spi_bus_config_t bus = {
        .sclk_io_num = RLCD_PIN_SCLK,
        .mosi_io_num = RLCD_PIN_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = RLCD_FB_SIZE + 64,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(RLCD_SPI_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "spi bus");

    esp_lcd_panel_io_spi_config_t io = {
        .dc_gpio_num = RLCD_PIN_DC,
        .cs_gpio_num = RLCD_PIN_CS,
        .pclk_hz = RLCD_SPI_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 4,
        .on_color_trans_done = on_color_done,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)RLCD_SPI_HOST, &io, &s_io), TAG, "panel io");

    ESP_RETURN_ON_ERROR(panel_init(), TAG, "panel init");
    ESP_LOGI(TAG, "ST7305 %dx%d ready", RLCD_WIDTH, RLCD_HEIGHT);
    return rlcd_flush();
}

void rlcd_clear(bool ink)
{
    memset(s_fb, ink ? 0x00 : 0xFF, RLCD_FB_SIZE);
}

void rlcd_set_pixel(int x, int y, bool ink)
{
    if ((unsigned)x >= RLCD_WIDTH || (unsigned)y >= RLCD_HEIGHT) return;
    int iy = RLCD_HEIGHT - 1 - y;
    uint32_t idx = (uint32_t)(x >> 1) * RLCD_BYTES_PER_PAGE + (iy >> 2);
    uint8_t mask = (uint8_t)(0x80 >> (((iy & 3) << 1) | (x & 1)));
    if (ink) s_fb[idx] &= (uint8_t)~mask;   /* 0 = dark */
    else     s_fb[idx] |= mask;             /* 1 = reflective */
}

esp_err_t rlcd_flush(void)
{
    CMD(0x2A, 0x12, 0x2A);
    CMD(0x2B, 0x00, 0xC7);
    CMD0(0x2C);
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_color(s_io, -1, s_fb, RLCD_FB_SIZE), TAG, "tx color");
    if (xSemaphoreTake(s_flush_done, pdMS_TO_TICKS(500)) != pdTRUE) {
        ESP_LOGW(TAG, "flush timeout");
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t rlcd_set_power(bool on)
{
    CMD0(on ? 0x29 : 0x28);
    return ESP_OK;
}
