// vga.h - VGA text mode driver (80x25, colour).

#ifndef VGA_H
#define VGA_H

#include <stdint.h>

// VGA text mode framebuffer address
#define VGA_MEMORY 0xB8000

// VGA dimensions
#define VGA_WIDTH  80
#define VGA_HEIGHT 25

// CRT controller ports, used to move the blinking hardware cursor.
#define VGA_CRTC_INDEX 0x3D4
#define VGA_CRTC_DATA  0x3D5

// VGA color codes
typedef enum {
    VGA_COLOR_BLACK = 0,
    VGA_COLOR_BLUE = 1,
    VGA_COLOR_GREEN = 2,
    VGA_COLOR_CYAN = 3,
    VGA_COLOR_RED = 4,
    VGA_COLOR_MAGENTA = 5,
    VGA_COLOR_BROWN = 6,
    VGA_COLOR_LIGHT_GREY = 7,
    VGA_COLOR_DARK_GREY = 8,
    VGA_COLOR_LIGHT_BLUE = 9,
    VGA_COLOR_LIGHT_GREEN = 10,
    VGA_COLOR_LIGHT_CYAN = 11,
    VGA_COLOR_LIGHT_RED = 12,
    VGA_COLOR_LIGHT_MAGENTA = 13,
    VGA_COLOR_LIGHT_BROWN = 14,
    VGA_COLOR_WHITE = 15,
} vga_color_t;

// -------- Lifecycle --------
void vga_init(void);
void vga_clear(void);

// -------- Output --------
// Handles '\n', '\r', '\t' and '\b' (destructive backspace).
void vga_putchar(char c, vga_color_t color);
void vga_print(const char* str, vga_color_t color);
void vga_print_hex(uint64_t value);
void vga_print_int(int32_t value, vga_color_t color);

// -------- Cursor --------
// Moves both the software cursor and the blinking hardware cursor.
void vga_set_cursor(uint8_t x, uint8_t y);
void vga_get_cursor(uint8_t* x, uint8_t* y);
void vga_enable_cursor(void);
void vga_disable_cursor(void);

// -------- Colour --------
// The colour kprintf() uses when no explicit colour is given.
void        vga_set_default_color(vga_color_t color);
vga_color_t vga_get_default_color(void);

// Background colour for newly written cells and for cleared regions.
void        vga_set_background(vga_color_t color);
vga_color_t vga_get_background(void);

// -------- Whole-screen helpers --------
// Repaints every cell with `color` without changing the characters.
void vga_recolor_screen(vga_color_t color);

#endif // VGA_H
