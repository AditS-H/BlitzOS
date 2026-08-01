// kprintf.h - printf-style formatted output for the kernel.
//
// Replaces the pattern of stringing together five vga_print() calls plus a
// hand-rolled integer-to-string loop every time the kernel wants to print a
// number. Output goes to the VGA text console AND the serial port, so
// `make run-serial` gives you a scrollable, copy-pasteable log.
//
// Supported conversions:
//   %s   NUL-terminated string ("(null)" if the pointer is NULL)
//   %c   character
//   %d   signed 32-bit decimal      %ld / %lld  signed 64-bit
//   %u   unsigned 32-bit decimal    %lu / %llu  unsigned 64-bit
//   %x   lowercase hex              %lx / %llx  64-bit
//   %X   uppercase hex
//   %p   pointer, printed as 0x0000000000000000
//   %b   binary
//   %%   a literal percent sign
//
// Flags: '0' for zero padding and a decimal field width, e.g. %08x.

#ifndef KERNEL_LIB_KPRINTF_H
#define KERNEL_LIB_KPRINTF_H

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include "../../drivers/vga.h"

// Print using the current default console colour.
void kprintf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

// Print in a specific colour (the colour applies to this call only).
void kprintf_color(vga_color_t color, const char* fmt, ...)
    __attribute__((format(printf, 2, 3)));

// va_list forms, for wrapping kprintf in your own helpers.
void kvprintf(vga_color_t color, const char* fmt, va_list args);

// Format into a caller-supplied buffer. Always NUL-terminates. Returns the
// number of characters written, not counting the terminator.
int ksnprintf(char* buf, size_t size, const char* fmt, ...)
    __attribute__((format(printf, 3, 4)));

int kvsnprintf(char* buf, size_t size, const char* fmt, va_list args);

// Convenience wrappers that tag the line with a coloured severity prefix.
#define kinfo(...)  do { kprintf_color(VGA_COLOR_LIGHT_CYAN,  "[INFO] ");  kprintf(__VA_ARGS__); } while (0)
#define kok(...)    do { kprintf_color(VGA_COLOR_LIGHT_GREEN, "[ OK ] ");  kprintf(__VA_ARGS__); } while (0)
#define kwarn(...)  do { kprintf_color(VGA_COLOR_BROWN,       "[WARN] ");  kprintf(__VA_ARGS__); } while (0)
#define kerror(...) do { kprintf_color(VGA_COLOR_LIGHT_RED,   "[ERR ] ");  kprintf(__VA_ARGS__); } while (0)

#endif // KERNEL_LIB_KPRINTF_H
