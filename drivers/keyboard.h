#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

// Keyboard I/O ports
#define KB_DATA_PORT    0x60
#define KB_STATUS_PORT  0x64
#define KB_COMMAND_PORT 0x64

// Keyboard status flags
#define KB_STATUS_OUTPUT_FULL 0x01
#define KB_STATUS_INPUT_FULL  0x02

// Special key codes
#define KEY_ESC      0x01
#define KEY_BACKSPACE 0x0E
#define KEY_TAB      0x0F
#define KEY_ENTER    0x1C
#define KEY_LCTRL    0x1D
#define KEY_LSHIFT   0x2A
#define KEY_RSHIFT   0x36
#define KEY_LALT     0x38
#define KEY_SPACE    0x39
#define KEY_CAPSLOCK 0x3A

// Keyboard buffer size
#define KB_BUFFER_SIZE 256

// Initialize keyboard driver
void keyboard_init(void);

// Keyboard interrupt handler
void keyboard_handler(void);

// Read a character from keyboard buffer (blocking)
char keyboard_getchar(void);

// Check if keyboard buffer has data
int keyboard_has_input(void);

#endif // KEYBOARD_H
