#include "kprintf.h"
#include "string.h"
#include "../../drivers/vga.h"
#include "../../drivers/serial.h"

// ---------------------------------------------------------------------------
// Output sink
//
// kprintf writes through a small sink abstraction so the same formatting code
// can target the screen, the serial line, or a memory buffer (ksnprintf).
// ---------------------------------------------------------------------------

typedef struct {
    char*       buf;        // NULL => write to console + serial
    size_t      size;       // capacity of buf including the NUL
    size_t      written;    // characters emitted (may exceed size)
    vga_color_t color;
} sink_t;

static void sink_putchar(sink_t* sink, char c)
{
    if (sink->buf) {
        if (sink->written + 1 < sink->size) {
            sink->buf[sink->written] = c;
        }
    } else {
        vga_putchar(c, sink->color);
        serial_putchar(c);
    }
    sink->written++;
}

static void sink_puts(sink_t* sink, const char* str)
{
    while (*str) {
        sink_putchar(sink, *str++);
    }
}

// ---------------------------------------------------------------------------
// Number formatting
// ---------------------------------------------------------------------------

static const char* DIGITS_LOWER = "0123456789abcdef";
static const char* DIGITS_UPPER = "0123456789ABCDEF";

// Renders `value` in `base` into `out` (which must hold at least 65 bytes for
// the binary case plus a terminator). Returns the length written.
static int format_unsigned(char* out, uint64_t value, uint32_t base, int uppercase)
{
    const char* digits = uppercase ? DIGITS_UPPER : DIGITS_LOWER;
    char        tmp[65];
    int         len = 0;

    if (value == 0) {
        tmp[len++] = '0';
    } else {
        while (value > 0) {
            tmp[len++] = digits[value % base];
            value /= base;
        }
    }

    // tmp holds the digits least-significant first; reverse into out.
    for (int i = 0; i < len; i++) {
        out[i] = tmp[len - 1 - i];
    }
    out[len] = '\0';
    return len;
}

static void emit_padded(sink_t* sink, const char* text, int len,
                        int width, int zero_pad, int left_align, int negative)
{
    // The '-' sign counts toward the field width.
    int total = len + (negative ? 1 : 0);

    // Left-aligned: text first, then spaces. Zero padding is meaningless here,
    // so it is ignored, matching standard printf.
    if (left_align) {
        if (negative) {
            sink_putchar(sink, '-');
        }
        for (int i = 0; i < len; i++) {
            sink_putchar(sink, text[i]);
        }
        for (int i = total; i < width; i++) {
            sink_putchar(sink, ' ');
        }
        return;
    }

    if (negative && zero_pad) {
        // "-007" rather than "00-7"
        sink_putchar(sink, '-');
    }

    for (int i = total; i < width; i++) {
        sink_putchar(sink, zero_pad ? '0' : ' ');
    }

    if (negative && !zero_pad) {
        sink_putchar(sink, '-');
    }

    for (int i = 0; i < len; i++) {
        sink_putchar(sink, text[i]);
    }
}

// ---------------------------------------------------------------------------
// The formatter
// ---------------------------------------------------------------------------

static void format(sink_t* sink, const char* fmt, va_list args)
{
    char numbuf[70];

    if (!fmt) {
        sink_puts(sink, "(null fmt)");
        return;
    }

    while (*fmt) {
        if (*fmt != '%') {
            sink_putchar(sink, *fmt++);
            continue;
        }

        fmt++;  // skip '%'

        if (*fmt == '%') {
            sink_putchar(sink, '%');
            fmt++;
            continue;
        }

        // --- flags ---
        // '-' left-aligns, '0' zero-pads. Accepted in either order and
        // repeated, like standard printf.
        int zero_pad   = 0;
        int left_align = 0;
        for (;;) {
            if (*fmt == '0') {
                zero_pad = 1;
                fmt++;
            } else if (*fmt == '-') {
                left_align = 1;
                fmt++;
            } else {
                break;
            }
        }

        // --- field width ---
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        // --- length modifier ---
        // 'l' and 'll' both mean 64-bit here; 'z' means size_t (also 64-bit).
        int is_long = 0;
        while (*fmt == 'l' || *fmt == 'z') {
            is_long = 1;
            fmt++;
        }

        // --- conversion ---
        switch (*fmt) {
        case 'd':
        case 'i': {
            int64_t value = is_long ? va_arg(args, int64_t)
                                    : (int64_t)va_arg(args, int32_t);
            int negative = value < 0;

            // Negate into unsigned so INT64_MIN does not overflow.
            uint64_t magnitude = negative ? (uint64_t)(-(value + 1)) + 1
                                          : (uint64_t)value;

            int len = format_unsigned(numbuf, magnitude, 10, 0);
            emit_padded(sink, numbuf, len, width, zero_pad, left_align, negative);
            break;
        }

        case 'u': {
            uint64_t value = is_long ? va_arg(args, uint64_t)
                                     : (uint64_t)va_arg(args, uint32_t);
            int len = format_unsigned(numbuf, value, 10, 0);
            emit_padded(sink, numbuf, len, width, zero_pad, left_align, 0);
            break;
        }

        case 'x':
        case 'X': {
            uint64_t value = is_long ? va_arg(args, uint64_t)
                                     : (uint64_t)va_arg(args, uint32_t);
            int len = format_unsigned(numbuf, value, 16, *fmt == 'X');
            emit_padded(sink, numbuf, len, width, zero_pad, left_align, 0);
            break;
        }

        case 'b': {
            uint64_t value = is_long ? va_arg(args, uint64_t)
                                     : (uint64_t)va_arg(args, uint32_t);
            int len = format_unsigned(numbuf, value, 2, 0);
            emit_padded(sink, numbuf, len, width, zero_pad, left_align, 0);
            break;
        }

        case 'p': {
            uint64_t value = (uint64_t)va_arg(args, void*);
            int len = format_unsigned(numbuf, value, 16, 0);
            sink_puts(sink, "0x");
            emit_padded(sink, numbuf, len, 16, 1, 0, 0);
            break;
        }

        case 'c': {
            // Chars are promoted to int through varargs.
            char c = (char)va_arg(args, int);
            sink_putchar(sink, c);
            break;
        }

        case 's': {
            const char* str = va_arg(args, const char*);
            if (!str) {
                str = "(null)";
            }
            int len = (int)strlen(str);
            emit_padded(sink, str, len, width, 0, left_align, 0);
            break;
        }

        case '\0':
            // Trailing '%' at the end of the format string.
            return;

        default:
            // Unknown conversion: echo it so the bug is visible.
            sink_putchar(sink, '%');
            sink_putchar(sink, *fmt);
            break;
        }

        fmt++;
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void kvprintf(vga_color_t color, const char* fmt, va_list args)
{
    sink_t sink = { .buf = NULL, .size = 0, .written = 0, .color = color };
    format(&sink, fmt, args);
}

void kprintf(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    kvprintf(vga_get_default_color(), fmt, args);
    va_end(args);
}

void kprintf_color(vga_color_t color, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    kvprintf(color, fmt, args);
    va_end(args);
}

int kvsnprintf(char* buf, size_t size, const char* fmt, va_list args)
{
    if (!buf || size == 0) {
        return 0;
    }

    sink_t sink = { .buf = buf, .size = size, .written = 0,
                    .color = VGA_COLOR_WHITE };
    format(&sink, fmt, args);

    // written can exceed the buffer; clamp the terminator into range.
    size_t end = sink.written < size - 1 ? sink.written : size - 1;
    buf[end] = '\0';

    return (int)end;
}

int ksnprintf(char* buf, size_t size, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int written = kvsnprintf(buf, size, fmt, args);
    va_end(args);
    return written;
}
