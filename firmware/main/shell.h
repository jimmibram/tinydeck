#pragma once
/* Line-editing shell that reads from input and writes to the LCD terminal and the USB console. */
void shell_start(void);
/* Print an asynchronous message without destroying the line being edited. */
void shell_notify(const char *msg);
