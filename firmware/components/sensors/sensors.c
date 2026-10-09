#include "sensors.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "sensors";

#define SHTC3_ADDR    0x70
#define PCF85063_ADDR 0x51
#define SAMPLE_PERIOD_MS 2000

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_shtc3, s_rtc;
static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static sensors_t s_data;

static uint8_t bcd2bin(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }
static uint8_t bin2bcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

static esp_err_t shtc3_read(float *t, float *rh)
{
    const uint8_t wake[] = { 0x35, 0x17 };
    const uint8_t measure[] = { 0x78, 0x66 };    /* normal mode, no clock stretching, T first */
    const uint8_t sleep[] = { 0xB0, 0x98 };
    uint8_t d[6];
    ESP_RETURN_ON_ERROR(i2c_master_transmit(s_shtc3, wake, sizeof(wake), 50), TAG, "wake");
    vTaskDelay(pdMS_TO_TICKS(1));
    ESP_RETURN_ON_ERROR(i2c_master_transmit(s_shtc3, measure, sizeof(measure), 50), TAG, "measure");
    vTaskDelay(pdMS_TO_TICKS(15));
    esp_err_t err = i2c_master_receive(s_shtc3, d, sizeof(d), 50);
    i2c_master_transmit(s_shtc3, sleep, sizeof(sleep), 50);
    ESP_RETURN_ON_ERROR(err, TAG, "read");
    uint16_t traw = (uint16_t)((d[0] << 8) | d[1]);
    uint16_t hraw = (uint16_t)((d[3] << 8) | d[4]);
    *t = -45.0f + 175.0f * (float)traw / 65536.0f;
    *rh = 100.0f * (float)hraw / 65536.0f;
    return ESP_OK;
}

static esp_err_t rtc_read(struct tm *out, bool *valid)
{
    uint8_t reg = 0x04, d[7];
    ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(s_rtc, &reg, 1, d, sizeof(d), 50), TAG, "rtc read");
    *valid = !(d[0] & 0x80);                       /* OS flag set = clock integrity not guaranteed */
    memset(out, 0, sizeof(*out));
    out->tm_sec = bcd2bin(d[0] & 0x7F);
    out->tm_min = bcd2bin(d[1] & 0x7F);
    out->tm_hour = bcd2bin(d[2] & 0x3F);
    out->tm_mday = bcd2bin(d[3] & 0x3F);
    out->tm_wday = d[4] & 0x07;
    out->tm_mon = bcd2bin(d[5] & 0x1F) - 1;
    out->tm_year = bcd2bin(d[6]) + 100;            /* years since 1900, chip stores 2000-2099 */
    return ESP_OK;
}

esp_err_t sensors_set_rtc(const struct tm *t)
{
    if (!s_rtc) return ESP_ERR_INVALID_STATE;
    uint8_t d[8] = {
        0x04,
        bin2bcd((uint8_t)t->tm_sec), bin2bcd((uint8_t)t->tm_min), bin2bcd((uint8_t)t->tm_hour),
        bin2bcd((uint8_t)t->tm_mday), (uint8_t)(t->tm_wday & 7), bin2bcd((uint8_t)(t->tm_mon + 1)),
        bin2bcd((uint8_t)((t->tm_year + 1900) % 100)),
    };
    ESP_RETURN_ON_ERROR(i2c_master_transmit(s_rtc, d, sizeof(d), 50), TAG, "rtc write");
    portENTER_CRITICAL(&s_mux);
    s_data.rtc_time = *t;
    s_data.rtc_valid = true;
    portEXIT_CRITICAL(&s_mux);
    return ESP_OK;
}

static esp_err_t battery_read(float *volts)
{
    int raw = 0, mv = 0;
    ESP_RETURN_ON_ERROR(adc_oneshot_read(s_adc, ADC_CHANNEL_3, &raw), TAG, "adc");
    if (s_cali) ESP_RETURN_ON_ERROR(adc_cali_raw_to_voltage(s_cali, raw, &mv), TAG, "cali");
    else mv = raw * 3300 / 4095;
    *volts = (float)mv * 3.0f / 1000.0f;           /* board divider: 1/3 */
    return ESP_OK;
}

static void sensors_task(void *arg)
{
    for (;;) {
        sensors_t d = {0};
        float t, rh, v;
        if (shtc3_read(&t, &rh) == ESP_OK) { d.env_valid = true; d.temperature_c = t; d.humidity_pct = rh; }
        if (battery_read(&v) == ESP_OK) {
            d.battery_valid = true;
            d.battery_volts = v;
            if (v < 3.0f) d.battery_pct = -1;      /* no cell fitted: USB powered */
            else {
                int pct = (int)((v - 3.0f) / 1.12f * 100.0f + 0.5f);
                d.battery_pct = pct < 0 ? 0 : pct > 100 ? 100 : pct;
            }
        }
        bool valid = false;
        struct tm tm;
        if (rtc_read(&tm, &valid) == ESP_OK) { d.rtc_valid = valid; d.rtc_time = tm; }
        portENTER_CRITICAL(&s_mux);
        s_data = d;
        portEXIT_CRITICAL(&s_mux);
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
    }
}

esp_err_t sensors_init(void)
{
    i2c_master_bus_config_t bus = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = SENSORS_I2C_SDA,
        .scl_io_num = SENSORS_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus, &s_bus), TAG, "i2c bus");
    i2c_device_config_t dev = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = SHTC3_ADDR, .scl_speed_hz = 100000 };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_bus, &dev, &s_shtc3), TAG, "shtc3");
    dev.device_address = PCF85063_ADDR;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_bus, &dev, &s_rtc), TAG, "rtc");

    adc_oneshot_unit_init_cfg_t ucfg = { .unit_id = ADC_UNIT_1 };
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&ucfg, &s_adc), TAG, "adc unit");
    adc_oneshot_chan_cfg_t ccfg = { .bitwidth = ADC_BITWIDTH_12, .atten = ADC_ATTEN_DB_12 };
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(s_adc, ADC_CHANNEL_3, &ccfg), TAG, "adc chan");   /* GPIO4 */
    adc_cali_curve_fitting_config_t cal = { .unit_id = ADC_UNIT_1, .chan = ADC_CHANNEL_3, .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12 };
    if (adc_cali_create_scheme_curve_fitting(&cal, &s_cali) != ESP_OK) s_cali = NULL;

    ESP_RETURN_ON_FALSE(xTaskCreate(sensors_task, "sensors", 3072, NULL, 2, NULL) == pdPASS, ESP_FAIL, TAG, "task");
    return ESP_OK;
}

void sensors_get(sensors_t *out)
{
    portENTER_CRITICAL(&s_mux);
    *out = s_data;
    portEXIT_CRITICAL(&s_mux);
}
