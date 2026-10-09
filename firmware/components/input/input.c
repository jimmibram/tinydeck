#include "input.h"
#include "freertos/queue.h"

static QueueHandle_t s_q;

void input_init(void) { s_q = xQueueCreate(128, sizeof(key_event_t)); }

void input_push(const key_event_t *ev)
{
    if (s_q && ev && ev->code != KEY_NONE) xQueueSend(s_q, ev, pdMS_TO_TICKS(10));
}

bool input_read(key_event_t *out, TickType_t wait)
{
    return s_q && xQueueReceive(s_q, out, wait) == pdTRUE;
}

size_t utf8_encode(uint32_t cp, char out[4])
{
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) { out[0] = (char)(0xC0 | (cp >> 6)); out[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12)); out[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18)); out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}
