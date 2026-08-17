// apps.h - The built-in desktop applications.
//
// Three windows, each demonstrating a different part of the system:
//
//   Terminal       runs the existing text shell, unmodified, inside a window.
//                  Proves the console is genuinely device independent: the
//                  same shell code drives VGA text, the serial line, and this.
//
//   System Monitor live process table and memory usage, repainted once a
//                  second. Shows the scheduler working while you watch.
//
//   Starfield      a continuously animating window. It exists to put real load
//                  on the frame pipeline so the dirty-rectangle and page-flip
//                  numbers in the stats overlay mean something.

#ifndef KERNEL_GUI_APPS_H
#define KERNEL_GUI_APPS_H

#include "gui.h"
#include "../../drivers/vga.h"   // for vga_color_t, the console hook signature

// Creates the default set of windows. Called by gui_start().
void apps_create_default_windows(void);

// Per-frame update hook. Animating windows use it to advance and invalidate
// themselves; static ones ignore it.
void apps_tick(gui_window_t* window);

// Releases resources and unhooks the console.
void apps_shutdown(void);

// ---------------------------------------------------------------------------
// Terminal
// ---------------------------------------------------------------------------

// Installed as the kprintf console hook while the desktop is running, so every
// kernel message lands in the terminal window.
void terminal_putchar_hook(char c, vga_color_t color);

// Blocking key read for the shell process, served from the terminal window's
// own input queue rather than the raw keyboard. This is what lets the shell
// run inside a window while the compositor keeps handling F-keys and the mouse.
int terminal_getkey(void);

// True when a terminal window exists and is accepting input.
int terminal_is_active(void);

#endif // KERNEL_GUI_APPS_H
