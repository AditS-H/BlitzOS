#include "keyboard.h"
#include "../kernel/arch/x86_64/interrupts.h"

// US QWERTY keyboard layout (scancode set 1)
static const char keyboard_us[128] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, /* Ctrl */
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, /* Left shift */
    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',
    0, /* Right shift */
    '*',
    0, /* Alt */
    ' ', /* Space */
    0, /* Caps lock */
    0, /* F1 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, /* F2-F10 */
    0, /* Num lock */
    0, /* Scroll lock */
    0, /* Home */
    0, /* Up arrow */
    0, /* Page up */
    '-',
    0, /* Left arrow */
    0,
    0, /* Right arrow */
    '+',
    0, /* End */
    0, /* Down arrow */
    0, /* Page down */
    0, /* Insert */
    0, /* Delete */
    0, 0, 0,
    0, /* F11 */
    0, /* F12 */
    0, /* All other keys undefined */
};

// Shifted characters
static const char keyboard_us_shifted[128] = {
    0,  27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, /* Ctrl */
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, /* Left shift */
    '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?',
    0, /* Right shift */
    '*',
    0, /* Alt */
    ' ', /* Space */
    0, /* Caps lock */
    0, /* F1-F12 and other keys */
};

// Keyboard state
static uint8_t shift_pressed = 0;
static uint8_t ctrl_pressed = 0;
static uint8_t alt_pressed = 0;

// Circular keyboard buffer
static char kb_buffer[KB_BUFFER_SIZE];
static volatile uint16_t kb_buffer_read = 0;
static volatile uint16_t kb_buffer_write = 0;

// Add character to keyboard buffer
static void kb_buffer_add(char c) {
    uint16_t next_write = (kb_buffer_write + 1) % KB_BUFFER_SIZE;
    if (next_write != kb_buffer_read) {
        kb_buffer[kb_buffer_write] = c;
        kb_buffer_write = next_write;
    }
}

// Initialize keyboard
void keyboard_init(void) {
    // Clear buffer
    kb_buffer_read = 0;
    kb_buffer_write = 0;
    
    // Keyboard is already initialized by BIOS/GRUB
    // Just clear any pending data
    while (inb(KB_STATUS_PORT) & KB_STATUS_OUTPUT_FULL) {
        inb(KB_DATA_PORT);
    }
}

// Keyboard interrupt handler (IRQ1)
void keyboard_handler(void) {
    uint8_t scancode = inb(KB_DATA_PORT);
    
    // Check if key release (bit 7 set)
    if (scancode & 0x80) {
        // Key released
        scancode &= 0x7F;
        
        // Update modifier keys
        if (scancode == KEY_LSHIFT || scancode == KEY_RSHIFT) {
            shift_pressed = 0;
        } else if (scancode == KEY_LCTRL) {
            ctrl_pressed = 0;
        } else if (scancode == KEY_LALT) {
            alt_pressed = 0;
        }
    } else {
        // Key pressed
        
        // Update modifier keys
        if (scancode == KEY_LSHIFT || scancode == KEY_RSHIFT) {
            shift_pressed = 1;
            return;
        } else if (scancode == KEY_LCTRL) {
            ctrl_pressed = 1;
            return;
        } else if (scancode == KEY_LALT) {
            alt_pressed = 1;
            return;
        }
        
        // Convert scancode to ASCII
        char c = 0;
        if (shift_pressed) {
            c = keyboard_us_shifted[scancode];
        } else {
            c = keyboard_us[scancode];
        }
        
        // Add to buffer if valid character
        if (c != 0) {
            kb_buffer_add(c);
        }
    }
}

// Check if keyboard buffer has data
int keyboard_has_input(void) {
    return kb_buffer_read != kb_buffer_write;
}

// Read character from keyboard buffer (blocking)
char keyboard_getchar(void) {
    // Wait for input
    while (!keyboard_has_input()) {
        __asm__ volatile("hlt");
    }
    
    // Read from buffer
    char c = kb_buffer[kb_buffer_read];
    kb_buffer_read = (kb_buffer_read + 1) % KB_BUFFER_SIZE;
    return c;
}
