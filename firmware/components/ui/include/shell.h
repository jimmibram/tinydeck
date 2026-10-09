#pragma once
/* Line-editing shell. Output goes to the LCD terminal and the USB console. */
#include "input.h"

void shell_init(void);
void shell_handle_key(const key_event_t *ev);
void shell_on_show(void);                 /* called when the terminal app becomes visible */
void shell_notify(const char *msg);       /* asynchronous message, keeps the edited line intact */
