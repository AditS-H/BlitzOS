// vga.c - VGA text mode driver implementation

#include "vga.h"
#include "../kernel/arch/x86_64/interrupts.h"  // for outb() / inb()

static uint16_t*   vga_buffer    = (uint16_t*)VGA_MEMORY;
static uint8_t     cursor_x      = 0;
static uint8_t     cursor_y      = 0;
static vga_color_t default_color = VGA_COLOR_LIGHT_GREY;
static vga_color_t background    = VGA_COLOR_BLACK;

// Make a VGA entry (character + colour attribute byte)
static inline uint16_t vga_entry(char c, vga_color_t fg, vga_color_t bg)
{
    uint8_t color = (uint8_t)((bg << 4) | (fg & 0x0F));
    return (uint16_t)(uint8_t)c | ((uint16_t)color << 8);
}

// ---------------------------------------------------------------------------
// Hardware cursor
//
// The VGA CRT controller stores the cursor position as a 16-bit character
// offset split across two indexed registers. Without writing them the
// blinking block stays at the top-left corner no matter where text is being
// written, which makes an interactive shell feel broken.
// ---------------------------------------------------------------------------

static void vga_update_hardware_cursor(void)
{
    uint16_t position = (uint16_t)(cursor_y * VGA_WIDTH + cursor_x);

    outb(VGA_CRTC_INDEX, 0x0F);                        // cursor location low
    outb(VGA_CRTC_DATA, (uint8_t)(position & 0xFF));
    outb(VGA_CRTC_INDEX, 0x0E);                        // cursor location high
    outb(VGA_CRTC_DATA, (uint8_t)((position >> 8) & 0xFF));
}

void vga_enable_cursor(void)
{
    // Cursor start register: bits 0-4 are the top scanline, bit 5 disables.
    outb(VGA_CRTC_INDEX, 0x0A);
    outb(VGA_CRTC_DATA, (uint8_t)((inb(VGA_CRTC_DATA) & 0xC0) | 13));

    // Cursor end register: bits 0-4 are the bottom scanline.
    outb(VGA_CRTC_INDEX, 0x0B);
    outb(VGA_CRTC_DATA, (uint8_t)((inb(VGA_CRTC_DATA) & 0xE0) | 15));

    vga_update_hardware_cursor();
}

void vga_disable_cursor(void)
{
    outb(VGA_CRTC_INDEX, 0x0A);
    outb(VGA_CRTC_DATA, 0x20);  // bit 5 set = cursor off
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void vga_init(void)
{
    cursor_x      = 0;
    cursor_y      = 0;
    default_color = VGA_COLOR_LIGHT_GREY;
    background    = VGA_COLOR_BLACK;
    vga_enable_cursor();
}

void vga_clear(void)
{
    uint16_t blank = vga_entry(' ', VGA_COLOR_WHITE, background);

    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_buffer[i] = blank;
    }

    cursor_x = 0;
    cursor_y = 0;
    vga_update_hardware_cursor();
}

// ---------------------------------------------------------------------------
// Output
// ---------------------------------------------------------------------------

// Scroll the screen up one line.
static void vga_scroll(void)
{
    for (int y = 0; y < VGA_HEIGHT - 1; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            vga_buffer[y * VGA_WIDTH + x] = vga_buffer[(y + 1) * VGA_WIDTH + x];
        }
    }

    uint16_t blank = vga_entry(' ', VGA_COLOR_WHITE, background);
    for (int x = 0; x < VGA_WIDTH; x++) {
        vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = blank;
    }

    cursor_y = VGA_HEIGHT - 1;
}

// Advance to the next line, scrolling if we fall off the bottom.
static void vga_newline(void)
{
    cursor_x = 0;
    cursor_y++;
    if (cursor_y >= VGA_HEIGHT) {
        vga_scroll();
    }
}

void vga_putchar(char c, vga_color_t color)
{
    switch (c) {
    case '\n':
        vga_newline();
        vga_update_hardware_cursor();
        return;

    case '\r':
        cursor_x = 0;
        vga_update_hardware_cursor();
        return;

    case '\b':
        // Destructive backspace: step back, blank the cell, stay there.
        // The shell relies on this for line editing.
        if (cursor_x > 0) {
            cursor_x--;
        } else if (cursor_y > 0) {
            cursor_y--;
            cursor_x = VGA_WIDTH - 1;
        }
        vga_buffer[cursor_y * VGA_WIDTH + cursor_x] =
            vga_entry(' ', color, background);
        vga_update_hardware_cursor();
        return;

    case '\t':
        // Advance to the next 4-column tab stop.
        cursor_x = (uint8_t)((cursor_x + 4) & ~(4 - 1));
        if (cursor_x >= VGA_WIDTH) {
            vga_newline();
        }
        vga_update_hardware_cursor();
        return;

    default:
        break;
    }

    vga_buffer[cursor_y * VGA_WIDTH + cursor_x] = vga_entry(c, color, background);

    cursor_x++;
    if (cursor_x >= VGA_WIDTH) {
        vga_newline();
    }
    vga_update_hardware_cursor();
}

void vga_print(const char* str, vga_color_t color)
{
    if (!str) {
        return;
    }
    for (int i = 0; str[i] != '\0'; i++) {
        vga_putchar(str[i], color);
    }
}

// ---------------------------------------------------------------------------
// Cursor / colour accessors
// ---------------------------------------------------------------------------

void vga_set_cursor(uint8_t x, uint8_t y)
{
    if (x < VGA_WIDTH && y < VGA_HEIGHT) {
        cursor_x = x;
        cursor_y = y;
        vga_update_hardware_cursor();
    }
}

void vga_get_cursor(uint8_t* x, uint8_t* y)
{
    if (x) *x = cursor_x;
    if (y) *y = cursor_y;
}

void vga_set_default_color(vga_color_t color)
{
    default_color = color;
}

vga_color_t vga_get_default_color(void)
{
    return default_color;
}

void vga_set_background(vga_color_t color)
{
    background = color;
}

vga_color_t vga_get_background(void)
{
    return background;
}

void vga_recolor_screen(vga_color_t color)
{
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        char existing = (char)(vga_buffer[i] & 0xFF);
        vga_buffer[i] = vga_entry(existing, color, background);
    }
}

// ---------------------------------------------------------------------------
// Legacy number printers
//
// Kept so older call sites keep working. New code should prefer kprintf()
// with %x / %d, which handles width, padding and sign properly.
// ---------------------------------------------------------------------------

void vga_print_hex(uint64_t value)
{
    char        hex[17];
    const char* digits = "0123456789abcdef";

    for (int i = 15; i >= 0; i--) {
        hex[15 - i] = digits[(value >> (i * 4)) & 0xF];
    }
    hex[16] = '\0';

    vga_print(hex, VGA_COLOR_LIGHT_CYAN);
}

void vga_print_int(int32_t value, vga_color_t color)
{
    if (value == 0) {
        vga_putchar('0', color);
        return;
    }

    // Accumulate an unsigned magnitude so INT32_MIN does not overflow when
    // negated (the previous version did, printing garbage for -2147483648).
    uint32_t magnitude;
    if (value < 0) {
        vga_putchar('-', color);
        magnitude = (uint32_t)(-(value + 1)) + 1;
    } else {
        magnitude = (uint32_t)value;
    }

    char digits[11];
    int  len = 0;
    while (magnitude > 0) {
        digits[len++] = (char)('0' + (magnitude % 10));
        magnitude /= 10;
    }

    while (len > 0) {
        vga_putchar(digits[--len], color);
    }
}
