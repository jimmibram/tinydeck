#pragma once
/* Unified byte stream of keyboard input from every source (BLE keyboard, USB console). */
#include <stddef.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"

void input_init(void);
void input_push(const uint8_t *data, size_t len);
/* Returns true and one byte, or false on timeout. */
bool input_read(uint8_t *out, TickType_t wait);
