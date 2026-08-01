// shell.h - Interactive kernel shell.
//
// The keyboard driver has always filled a buffer; nothing ever read from it.
// This is the consumer: a normal process that blocks on WAIT_CHANNEL_KEYBOARD
// (costing zero CPU while idle), does line editing with history, and drives
// the filesystem, scheduler and syscall layer.

#ifndef KERNEL_SHELL_SHELL_H
#define KERNEL_SHELL_SHELL_H

#include <stdint.h>

#define SHELL_LINE_MAX     256
#define SHELL_MAX_ARGS     16
#define SHELL_HISTORY_SIZE 16

// Entry point for the shell process. Pass this to process_create().
void shell_main(void);

// Print the banner and command list (also used by the `help` command).
void shell_print_banner(void);

#endif // KERNEL_SHELL_SHELL_H
