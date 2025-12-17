// vga.h - VGA text mode driver header

#ifndef VGA_H
#define VGA_H

#include <stdint.h>

// VGA text mode buffer address
#define VGA_MEMORY 0xB8000

// VGA dimensions
#define VGA_WIDTH 80
#define VGA_HEIGHT 25

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

// Function declarations
void vga_init(void);
void vga_clear(void);
void vga_putchar(char c, vga_color_t color);
void vga_print(const char* str, vga_color_t color);
void vga_print_hex(uint64_t value);
void vga_print_int(int32_t value, vga_color_t color);
void vga_set_cursor(uint8_t x, uint8_t y);

#endif // VGA_H
