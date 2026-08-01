#include "keyboard.h"
#include "../kernel/arch/x86_64/interrupts.h"
#include "../kernel/proc/process.h"

// ---------------------------------------------------------------------------
// Scancode set 1 -> ASCII
// ---------------------------------------------------------------------------

static const char keymap_normal[128] = {
    0,   27,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t','q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,   /* 0x1D left ctrl */
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'','`',
    0,   /* 0x2A left shift */
    '\\','z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',
    0,   /* 0x36 right shift */
    '*',
    0,   /* 0x38 left alt */
    ' ',
    0,   /* 0x3A caps lock */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,   /* 0x3B-0x44 F1-F10 */
    0,   /* 0x45 num lock */
    0,   /* 0x46 scroll lock */
    '7', '8', '9', '-',             /* keypad */
    '4', '5', '6', '+',
    '1', '2', '3',
    '0', '.',
    0, 0, 0,
    0,   /* 0x57 F11 */
    0,   /* 0x58 F12 */
    /* remainder zero-filled */
};

static const char keymap_shifted[128] = {
    0,   27,  '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t','Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0,
    '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?',
    0,
    '*',
    0,
    ' ',
    0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0,
    0,
    '7', '8', '9', '-',
    '4', '5', '6', '+',
    '1', '2', '3',
    '0', '.',
    /* remainder zero-filled */
};

// ---------------------------------------------------------------------------
// Driver state
// ---------------------------------------------------------------------------

static uint8_t modifiers = 0;
static uint8_t expecting_extended = 0;

// Circular buffer of decoded keys. uint16_t because special keys (arrows, F
// keys) are reported above the ASCII range.
static volatile uint16_t kb_buffer[KB_BUFFER_SIZE];
static volatile uint16_t kb_read  = 0;
static volatile uint16_t kb_write = 0;

static void kb_buffer_push(int key)
{
    uint16_t next = (uint16_t)((kb_write + 1) % KB_BUFFER_SIZE);

    // Drop the keystroke rather than overwriting unread input.
    if (next == kb_read) {
        return;
    }

    kb_buffer[kb_write] = (uint16_t)key;
    kb_write = next;
}

// ---------------------------------------------------------------------------
// Keyboard LEDs
// ---------------------------------------------------------------------------

static void kb_wait_input_clear(void)
{
    // Bounded, so a wedged controller cannot hang the kernel.
    for (uint32_t i = 0; i < 100000; i++) {
        if (!(inb(KB_STATUS_PORT) & KB_STATUS_INPUT_FULL)) {
            return;
        }
    }
}

static void keyboard_update_leds(void)
{
    uint8_t leds = 0;
    if (modifiers & KB_MOD_CAPS) {
        leds |= 0x04;  // caps lock LED
    }

    kb_wait_input_clear();
    outb(KB_DATA_PORT, 0xED);   // "set LEDs" command
    kb_wait_input_clear();
    outb(KB_DATA_PORT, leds);
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------

void keyboard_init(void)
{
    kb_read   = 0;
    kb_write  = 0;
    modifiers = 0;
    expecting_extended = 0;

    // Drain anything the firmware left in the output buffer, otherwise the
    // first IRQ delivers a stale byte.
    while (inb(KB_STATUS_PORT) & KB_STATUS_OUTPUT_FULL) {
        (void)inb(KB_DATA_PORT);
    }

    keyboard_update_leds();
}

// ---------------------------------------------------------------------------
// Interrupt handler
// ---------------------------------------------------------------------------

// Map an extended (0xE0-prefixed) scancode to a KEY_* value.
static int decode_extended(uint8_t scancode)
{
    switch (scancode) {
    case 0x48: return KEY_ARROW_UP;
    case 0x50: return KEY_ARROW_DOWN;
    case 0x4B: return KEY_ARROW_LEFT;
    case 0x4D: return KEY_ARROW_RIGHT;
    case 0x47: return KEY_HOME;
    case 0x4F: return KEY_END;
    case 0x49: return KEY_PAGE_UP;
    case 0x51: return KEY_PAGE_DOWN;
    case 0x52: return KEY_INSERT;
    case 0x53: return KEY_DELETE;
    case 0x1C: return '\n';   // keypad enter
    case 0x35: return '/';    // keypad slash
    default:   return KEY_NONE;
    }
}

void keyboard_handler(void)
{
    uint8_t scancode = inb(KB_DATA_PORT);

    // 0xE0 introduces a two-byte sequence; remember it and take the next byte.
    if (scancode == KB_EXTENDED_PREFIX) {
        expecting_extended = 1;
        return;
    }

    int is_release = (scancode & 0x80) != 0;
    uint8_t code   = (uint8_t)(scancode & 0x7F);

    if (expecting_extended) {
        expecting_extended = 0;

        if (code == KEY_LCTRL) {          // right ctrl
            if (is_release) modifiers &= (uint8_t)~KB_MOD_CTRL;
            else            modifiers |= KB_MOD_CTRL;
            return;
        }
        if (code == KEY_LALT) {           // right alt
            if (is_release) modifiers &= (uint8_t)~KB_MOD_ALT;
            else            modifiers |= KB_MOD_ALT;
            return;
        }

        if (!is_release) {
            int key = decode_extended(code);
            if (key != KEY_NONE) {
                kb_buffer_push(key);
                process_wake_all(WAIT_CHANNEL_KEYBOARD);
            }
        }
        return;
    }

    // ---- Modifier keys ----
    switch (code) {
    case KEY_LSHIFT:
    case KEY_RSHIFT:
        if (is_release) modifiers &= (uint8_t)~KB_MOD_SHIFT;
        else            modifiers |= KB_MOD_SHIFT;
        return;

    case KEY_LCTRL:
        if (is_release) modifiers &= (uint8_t)~KB_MOD_CTRL;
        else            modifiers |= KB_MOD_CTRL;
        return;

    case KEY_LALT:
        if (is_release) modifiers &= (uint8_t)~KB_MOD_ALT;
        else            modifiers |= KB_MOD_ALT;
        return;

    case KEY_CAPSLOCK:
        // Toggle on press only, otherwise the release flips it straight back.
        if (!is_release) {
            modifiers ^= KB_MOD_CAPS;
            keyboard_update_leds();
        }
        return;

    default:
        break;
    }

    if (is_release) {
        return;  // nothing else cares about key-up
    }

    // ---- Function keys ----
    if (code >= KEY_F1 && code <= KEY_F1 + 9) {
        kb_buffer_push(KEY_FKEY_BASE + (code - KEY_F1));
        process_wake_all(WAIT_CHANNEL_KEYBOARD);
        return;
    }

    // ---- Printable characters ----
    int shifted = (modifiers & KB_MOD_SHIFT) != 0;
    char c = shifted ? keymap_shifted[code] : keymap_normal[code];

    if (c == 0) {
        return;
    }

    // Caps lock inverts the shift state for letters only, not for digits or
    // punctuation - which is why it cannot just be OR'd into `shifted`.
    if (modifiers & KB_MOD_CAPS) {
        if (c >= 'a' && c <= 'z')      c = (char)(c - 32);
        else if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
    }

    // Ctrl+letter produces the classic control character: Ctrl+C = 0x03,
    // Ctrl+L = 0x0C, and so on.
    if (modifiers & KB_MOD_CTRL) {
        if (c >= 'a' && c <= 'z')      c = (char)(c - 'a' + 1);
        else if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 1);
    }

    kb_buffer_push((unsigned char)c);

    // Wake anything blocked on keyboard input. This is what turns the shell's
    // read from a spin loop into a real blocking wait.
    process_wake_all(WAIT_CHANNEL_KEYBOARD);
}

// ---------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------

int keyboard_has_input(void)
{
    return kb_read != kb_write;
}

int keyboard_getkey_nonblocking(void)
{
    uint64_t flags = irq_save();

    if (kb_read == kb_write) {
        irq_restore(flags);
        return KEY_NONE;
    }

    int key = kb_buffer[kb_read];
    kb_read = (uint16_t)((kb_read + 1) % KB_BUFFER_SIZE);

    irq_restore(flags);
    return key;
}

int keyboard_getkey(void)
{
    for (;;) {
        int key = keyboard_getkey_nonblocking();
        if (key != KEY_NONE) {
            return key;
        }

        // Nothing buffered: give the CPU to someone else until IRQ1 fires.
        process_block(WAIT_CHANNEL_KEYBOARD);
    }
}

char keyboard_getchar(void)
{
    for (;;) {
        int key = keyboard_getkey();
        if (key > 0 && key < 0x100) {
            return (char)key;
        }
        // Arrow / function key: not representable as a char, so keep waiting.
    }
}

uint8_t keyboard_get_modifiers(void)
{
    return modifiers;
}

void keyboard_flush(void)
{
    uint64_t flags = irq_save();
    kb_read = kb_write;
    irq_restore(flags);
}

void keyboard_inject_key(int key)
{
    if (key == KEY_NONE) {
        return;
    }

    kb_buffer_push(key);
    process_wake_all(WAIT_CHANNEL_KEYBOARD);
}
