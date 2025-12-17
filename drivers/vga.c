// vga.c - VGA text mode driver implementation

#include "vga.h"

static uint16_t* vga_buffer = (uint16_t*)VGA_MEMORY;
static uint8_t cursor_x = 0;
static uint8_t cursor_y = 0;

// Make a VGA entry (character + color)
static inline uint16_t vga_entry(char c, vga_color_t fg, vga_color_t bg) {
    uint8_t color = (bg << 4) | (fg & 0x0F);
    return (uint16_t)c | (uint16_t)color << 8;
}

// Initialize VGA
void vga_init(void) {
    cursor_x = 0;
    cursor_y = 0;
}

// Clear the screen
void vga_clear(void) {
    for (int y = 0; y < VGA_HEIGHT; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            const int index = y * VGA_WIDTH + x;
            vga_buffer[index] = vga_entry(' ', VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        }
    }
    cursor_x = 0;
    cursor_y = 0;
}

// Scroll screen up one line
static void vga_scroll(void) {
    // Move all lines up
    for (int y = 0; y < VGA_HEIGHT - 1; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            vga_buffer[y * VGA_WIDTH + x] = vga_buffer[(y + 1) * VGA_WIDTH + x];
        }
    }
    
    // Clear last line
    for (int x = 0; x < VGA_WIDTH; x++) {
        vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = vga_entry(' ', VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    }
    
    cursor_y = VGA_HEIGHT - 1;
}

// Put a character on screen
void vga_putchar(char c, vga_color_t color) {
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
        if (cursor_y >= VGA_HEIGHT) {
            vga_scroll();
        }
        return;
    }
    
    if (c == '\t') {
        cursor_x = (cursor_x + 4) & ~(4 - 1);
        if (cursor_x >= VGA_WIDTH) {
            cursor_x = 0;
            cursor_y++;
            if (cursor_y >= VGA_HEIGHT) {
                vga_scroll();
            }
        }
        return;
    }
    
    const int index = cursor_y * VGA_WIDTH + cursor_x;
    vga_buffer[index] = vga_entry(c, color, VGA_COLOR_BLACK);
    
    cursor_x++;
    if (cursor_x >= VGA_WIDTH) {
        cursor_x = 0;
        cursor_y++;
        if (cursor_y >= VGA_HEIGHT) {
            vga_scroll();
        }
    }
}

// Print a string
void vga_print(const char* str, vga_color_t color) {
    for (int i = 0; str[i] != '\0'; i++) {
        vga_putchar(str[i], color);
    }
}

// Set cursor position
void vga_set_cursor(uint8_t x, uint8_t y) {
    if (x < VGA_WIDTH && y < VGA_HEIGHT) {
        cursor_x = x;
        cursor_y = y;
    }
}

// Print a 64-bit hex value
void vga_print_hex(uint64_t value) {
    char hex[17];
    const char* digits = "0123456789abcdef";
    
    for (int i = 15; i >= 0; i--) {
        hex[15 - i] = digits[(value >> (i * 4)) & 0xF];
    }
    hex[16] = '\0';
    
    vga_print(hex, VGA_COLOR_LIGHT_CYAN);
}

// Print a 32-bit signed integer
void vga_print_int(int32_t value, vga_color_t color) {
    char buffer[12];  // -2147483648 + null terminator
    int pos = 0;
    
    if (value < 0) {
        vga_putchar('-', color);
        value = -value;
    }
    
    // Convert to decimal
    if (value == 0) {
        vga_putchar('0', color);
        return;
    }
    
    int divisor = 1000000000;
    int started = 0;
    
    while (divisor > 0) {
        int digit = value / divisor;
        value %= divisor;
        
        if (digit != 0 || started || divisor == 1) {
            vga_putchar('0' + digit, color);
            started = 1;
        }
        
        divisor /= 10;
    }
}
