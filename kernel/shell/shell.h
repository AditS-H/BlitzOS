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

// Swap where the shell reads keys from.
//
// In text mode this is the raw keyboard. When the desktop is running, the
// compositor owns the keyboard - it needs the F-keys and the mouse - so the
// terminal window hands the shell its own queue instead. Same shell code,
// different input source, which is what lets all 30 commands keep working
// unchanged inside a window.
//
// Pass NULL to go back to reading the keyboard directly.
void shell_set_input_source(int (*getkey)(void));

#endif // KERNEL_SHELL_SHELL_H
