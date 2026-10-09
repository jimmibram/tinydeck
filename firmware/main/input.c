#include "input.h"
#include "freertos/queue.h"

static QueueHandle_t s_q;

void input_init(void) { s_q = xQueueCreate(256, sizeof(uint8_t)); }

void input_push(const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) xQueueSend(s_q, &data[i], pdMS_TO_TICKS(10));
}

bool input_read(uint8_t *out, TickType_t wait) { return xQueueReceive(s_q, out, wait) == pdTRUE; }
