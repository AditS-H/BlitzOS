#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

// PS/2 controller ports
#define KB_DATA_PORT    0x60
#define KB_STATUS_PORT  0x64
#define KB_COMMAND_PORT 0x64

// Status register flags
#define KB_STATUS_OUTPUT_FULL 0x01
#define KB_STATUS_INPUT_FULL  0x02

// Scancode set 1, make codes
#define KEY_ESC       0x01
#define KEY_BACKSPACE 0x0E
#define KEY_TAB       0x0F
#define KEY_ENTER     0x1C
#define KEY_LCTRL     0x1D
#define KEY_LSHIFT    0x2A
#define KEY_RSHIFT    0x36
#define KEY_LALT      0x38
#define KEY_SPACE     0x39
#define KEY_CAPSLOCK  0x3A
#define KEY_F1        0x3B
#define KEY_NUMLOCK   0x45
#define KEY_SCROLLLOCK 0x46

// Prefix byte for extended keys (arrows, right ctrl/alt, etc.)
#define KB_EXTENDED_PREFIX 0xE0

#define KB_BUFFER_SIZE 256

// Keys with no ASCII equivalent are reported above the ASCII range so a single
// int can carry both. keyboard_getchar() filters these out; the shell uses
// keyboard_getkey() so it can bind the arrows to command history.
#define KEY_NONE        0
#define KEY_ARROW_UP    0x100
#define KEY_ARROW_DOWN  0x101
#define KEY_ARROW_LEFT  0x102
#define KEY_ARROW_RIGHT 0x103
#define KEY_HOME        0x104
#define KEY_END         0x105
#define KEY_PAGE_UP     0x106
#define KEY_PAGE_DOWN   0x107
#define KEY_DELETE      0x108
#define KEY_INSERT      0x109
#define KEY_FKEY_BASE   0x110   // F1 = KEY_FKEY_BASE + 0 ... F12 = +11

// Modifier bitmask returned by keyboard_get_modifiers()
#define KB_MOD_SHIFT 0x01
#define KB_MOD_CTRL  0x02
#define KB_MOD_ALT   0x04
#define KB_MOD_CAPS  0x08

void keyboard_init(void);
void keyboard_handler(void);      // called from IRQ1

// True if a keystroke is waiting.
int  keyboard_has_input(void);

// Blocking reads. These deschedule the calling process onto
// WAIT_CHANNEL_KEYBOARD, so a shell waiting for input costs zero CPU - the IRQ
// handler wakes it when a key actually arrives.
int  keyboard_getkey(void);       // may return a KEY_* code above 0xFF
char keyboard_getchar(void);      // ASCII only; skips special keys

// Non-blocking: returns KEY_NONE when the buffer is empty.
int  keyboard_getkey_nonblocking(void);

uint8_t keyboard_get_modifiers(void);

// Discard anything buffered.
void keyboard_flush(void);

// Push a key into the console input queue from somewhere other than the PS/2
// port, and wake anything blocked waiting for input.
//
// The serial driver uses this so the shell can be driven over COM1 - which
// means you can pipe a script into `make run-headless` and get the output
// back, instead of needing a video window and a human at the keyboard.
// Safe to call from an interrupt handler.
void keyboard_inject_key(int key);

#endif // KEYBOARD_H
