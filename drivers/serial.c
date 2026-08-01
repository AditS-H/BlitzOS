#include "serial.h"
#include "keyboard.h"
#include "../kernel/arch/x86_64/interrupts.h"

static int serial_available = 0;

static inline void serial_out(uint16_t reg, uint8_t value)
{
    outb(SERIAL_COM1_BASE + reg, value);
}

static inline uint8_t serial_in(uint16_t reg)
{
    return inb(SERIAL_COM1_BASE + reg);
}

int serial_init(void)
{
    // Disable all UART interrupts while we reconfigure.
    serial_out(SERIAL_REG_INT_ENABLE, 0x00);

    // Set DLAB so registers 0 and 1 become the baud rate divisor.
    serial_out(SERIAL_REG_LINE_CTRL, 0x80);

    // 115200 / 3 = 38400 baud.
    serial_out(SERIAL_REG_DIVISOR_LO, 0x03);
    serial_out(SERIAL_REG_DIVISOR_HI, 0x00);

    // Clear DLAB and select 8 data bits, no parity, 1 stop bit.
    serial_out(SERIAL_REG_LINE_CTRL, 0x03);

    // Enable the FIFO, clear both directions, 14-byte trigger level.
    serial_out(SERIAL_REG_FIFO_CTRL, 0xC7);

    // Self-test: put the chip in loopback mode, send a byte, see if it
    // comes back. On hardware with no UART the reads return 0xFF and we
    // detect that instead of blocking forever in serial_putchar().
    serial_out(SERIAL_REG_MODEM_CTRL, 0x1E);  // loopback + RTS/DTR + OUT1/OUT2
    serial_out(SERIAL_REG_DATA, 0xAE);

    if (serial_in(SERIAL_REG_DATA) != 0xAE) {
        serial_available = 0;
        return 0;
    }

    // Back to normal operation: DTR + RTS + OUT2 (OUT2 gates the IRQ line).
    serial_out(SERIAL_REG_MODEM_CTRL, 0x0F);

    serial_available = 1;
    return 1;
}

int serial_is_available(void)
{
    return serial_available;
}

static void serial_wait_for_tx(void)
{
    // Bounded spin. If the UART wedges we would rather drop a debug byte
    // than hang the whole kernel inside a panic handler.
    for (uint32_t spins = 0; spins < 100000; spins++) {
        if (serial_in(SERIAL_REG_LINE_STATUS) & SERIAL_LSR_TX_EMPTY) {
            return;
        }
    }
}

void serial_putchar(char c)
{
    if (!serial_available) {
        return;
    }

    // Terminals expect CRLF; the kernel emits bare LF.
    if (c == '\n') {
        serial_wait_for_tx();
        serial_out(SERIAL_REG_DATA, '\r');
    }

    serial_wait_for_tx();
    serial_out(SERIAL_REG_DATA, (uint8_t)c);
}

void serial_write(const char* str)
{
    if (!serial_available || !str) {
        return;
    }
    while (*str) {
        serial_putchar(*str++);
    }
}

char serial_read_nonblocking(void)
{
    if (!serial_available) {
        return 0;
    }
    if (!(serial_in(SERIAL_REG_LINE_STATUS) & SERIAL_LSR_DATA_READY)) {
        return 0;
    }
    return (char)serial_in(SERIAL_REG_DATA);
}

void serial_enable_input(void)
{
    if (!serial_available) {
        return;
    }

    // IER bit 0: raise IRQ4 when a byte arrives.
    serial_out(SERIAL_REG_INT_ENABLE, 0x01);
}

void serial_handler(void)
{
    if (!serial_available) {
        return;
    }

    // Drain the FIFO: one interrupt can cover several buffered bytes, and
    // leaving any behind would stop further interrupts from arriving.
    while (serial_in(SERIAL_REG_LINE_STATUS) & SERIAL_LSR_DATA_READY) {
        char c = (char)serial_in(SERIAL_REG_DATA);

        // Terminals send CR for Enter and 0x7F for Backspace; the console
        // input queue speaks LF and 0x08.
        if (c == '\r') {
            c = '\n';
        } else if (c == 0x7F) {
            c = '\b';
        }

        keyboard_inject_key((unsigned char)c);
    }
}
