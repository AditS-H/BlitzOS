// gui.h - Window manager and compositor.
//
// THE MODEL
// ---------
// Windows are rectangles with a title bar, kept in an array ordered back to
// front. There is no per-window backing store: every window paints itself
// directly into the shared back buffer, in z-order, once per frame. That is
// the simplest correct compositor, and for a handful of windows it is also the
// fastest - a backing store per window would mean an extra full copy per
// window per frame.
//
// The cost of not having backing stores is that moving a window forces
// everything underneath it to repaint. With dirty rectangles that is cheap:
// the damaged area is the union of where the window was and where it went, not
// the whole screen.
//
// THE FRAME LOOP
// --------------
// The compositor is an ordinary process, scheduled like anything else:
//
//   1. Drain input. Mouse packets have already been decoded by the IRQ handler;
//      keystrokes come off the same queue the text shell uses.
//   2. Route it. Clicks hit the topmost window containing the point; keys go
//      to the focused window.
//   3. Repaint whatever is damaged.
//   4. fb_present() - push dirty rectangles to VRAM and flip.
//   5. Sleep until the next frame slot.
//
// Step 5 is frame pacing. Without it the loop would spin as fast as the CPU
// allows, burning every spare cycle to redraw pixels nobody asked for, and
// starving the shell. Sleeping to a target interval means the desktop costs
// almost nothing when idle - `ps` shows the compositor using a few per cent.
//
// FOCUS AND INPUT
// ---------------
// Exactly one window has focus. It gets keystrokes; everything else gets
// nothing. Clicking a window raises it to the front and focuses it. The
// terminal window feeds its keys into a ring buffer the shell process reads
// from, which is how the existing text shell runs unmodified inside a window.

#ifndef KERNEL_GUI_GUI_H
#define KERNEL_GUI_GUI_H

#include <stdint.h>
#include "../../drivers/video/framebuffer.h"

#define GUI_MAX_WINDOWS   8
#define GUI_TITLE_MAX     32
#define GUI_TITLEBAR_H    22
#define GUI_BORDER        2
#define GUI_TASKBAR_H     28

// Default frame interval. 16 ms is about 60 Hz.
#define GUI_TARGET_FRAME_MS 16

struct gui_window;

typedef void (*gui_paint_fn)(struct gui_window* window);
typedef void (*gui_key_fn)(struct gui_window* window, int key);
typedef void (*gui_click_fn)(struct gui_window* window, int32_t x, int32_t y,
                             uint8_t buttons);

typedef struct gui_window {
    int32_t  x, y;
    int32_t  width, height;      // including title bar and borders

    char     title[GUI_TITLE_MAX];

    int      visible;
    int      minimised;
    int      needs_repaint;

    // Painted into the shared back buffer. The clip rectangle is already set
    // to the window's client area when this is called, so a handler cannot
    // draw outside its own window even if it tries.
    gui_paint_fn  on_paint;
    gui_key_fn    on_key;
    gui_click_fn  on_click;

    void*    userdata;
} gui_window_t;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

// Brings up graphics, creates the built-in windows and starts the compositor
// process. Returns 0 if graphics could not be initialised, in which case the
// caller should stay in text mode.
int  gui_start(uint32_t width, uint32_t height);

// Tears the desktop down and returns to text mode.
void gui_stop(void);

int  gui_is_running(void);

// ---------------------------------------------------------------------------
// Windows
// ---------------------------------------------------------------------------

gui_window_t* gui_create_window(const char* title, int32_t x, int32_t y,
                                int32_t width, int32_t height,
                                gui_paint_fn on_paint);

void gui_destroy_window(gui_window_t* window);

// Raise to the front and give it keyboard focus.
void gui_focus_window(gui_window_t* window);
gui_window_t* gui_focused_window(void);

// Ask for a repaint on the next frame.
void gui_invalidate(gui_window_t* window);
void gui_invalidate_all(void);

// The drawable area inside the borders and title bar.
void gui_client_rect(const gui_window_t* window, fb_rect_t* out);

// ---------------------------------------------------------------------------
// Frame pacing / game mode
// ---------------------------------------------------------------------------

// Target milliseconds between frames. Lower means smoother and more expensive.
void     gui_set_frame_interval_ms(uint32_t ms);
uint32_t gui_get_frame_interval_ms(void);

// Game mode: raise the timer tick rate for finer scheduling granularity, pin
// the compositor to a high priority, and stop repainting windows that are not
// in front. See gamemode.c for what each of those actually buys.
void gui_set_game_mode(int enabled);
int  gui_get_game_mode(void);

// Compositor statistics.
uint64_t gui_frames_rendered(void);
uint32_t gui_last_frame_ms(void);

#endif // KERNEL_GUI_GUI_H
