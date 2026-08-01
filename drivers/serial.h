// serial.h - 16550 UART driver (COM1).
//
// Why this exists: VGA text mode gives you 80x25 characters that scroll away
// and vanish. Serial output can be piped straight to your terminal with
// `make run-serial`, captured to a log file, and survives a triple fault.
// For kernel debugging it is worth more than the screen.

#ifndef DRIVERS_SERIAL_H
#define DRIVERS_SERIAL_H

#include <stdint.h>

#define SERIAL_COM1_BASE 0x3F8
#define SERIAL_COM2_BASE 0x2F8

// Register offsets from the port base.
#define SERIAL_REG_DATA        0  // Receive/transmit buffer (DLAB=0)
#define SERIAL_REG_INT_ENABLE  1  // Interrupt enable        (DLAB=0)
#define SERIAL_REG_DIVISOR_LO  0  // Baud divisor low byte   (DLAB=1)
#define SERIAL_REG_DIVISOR_HI  1  // Baud divisor high byte  (DLAB=1)
#define SERIAL_REG_FIFO_CTRL   2
#define SERIAL_REG_LINE_CTRL   3
#define SERIAL_REG_MODEM_CTRL  4
#define SERIAL_REG_LINE_STATUS 5

#define SERIAL_LSR_DATA_READY   0x01  // Byte waiting in the receive buffer
#define SERIAL_LSR_TX_EMPTY     0x20  // Transmit holding register is free

// Brings up COM1 at 38400 baud, 8N1. Returns 1 on success, 0 if no UART is
// present (the loopback self-test failed), in which case all output calls
// below become no-ops rather than hanging.
int  serial_init(void);

// True if serial_init() found working hardware.
int  serial_is_available(void);

void serial_putchar(char c);
void serial_write(const char* str);

// Non-blocking read: returns 0 if no byte is waiting.
char serial_read_nonblocking(void);

// Turn on the UART receive interrupt (IRQ4) and start feeding incoming bytes
// into the console input queue.
//
// This is what lets you drive the shell over the serial line: with
// `make run-headless` you can type commands - or pipe a script in - and read
// the output back, no video window and no PS/2 keyboard required.
void serial_enable_input(void);

// COM1 interrupt handler, called from IRQ4.
void serial_handler(void);

#endif // DRIVERS_SERIAL_H
