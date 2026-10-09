#pragma once
/* Onboard sensors: SHTC3 temperature/humidity, PCF85063 real-time clock, battery voltage (ADC). */
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "esp_err.h"

#define SENSORS_I2C_SDA 13
#define SENSORS_I2C_SCL 14
#define SENSORS_BATTERY_ADC_GPIO 4

typedef struct {
    bool  env_valid;
    float temperature_c;
    float humidity_pct;
    bool  battery_valid;
    float battery_volts;
    int   battery_pct;        /* 0..100, -1 when running from USB without a battery */
    bool  rtc_valid;          /* clock has been set since last power loss */
    struct tm rtc_time;
} sensors_t;

esp_err_t sensors_init(void);        /* starts a background sampling task (every 2 s) */
void sensors_get(sensors_t *out);
esp_err_t sensors_set_rtc(const struct tm *t);
