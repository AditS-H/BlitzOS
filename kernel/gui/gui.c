#include "gui.h"
#include "apps.h"
#include "../lib/kprintf.h"
#include "../lib/string.h"
#include "../proc/process.h"
#include "../arch/x86_64/tsc.h"
#include "../arch/x86_64/interrupts.h"
#include "../../drivers/video/framebuffer.h"
#include "../../drivers/video/font.h"
#include "../../drivers/mouse.h"
#include "../../drivers/keyboard.h"
#include "../../drivers/pit.h"
#include "../../drivers/vga.h"

// ---------------------------------------------------------------------------
// Palette
// ---------------------------------------------------------------------------

#define COLOR_DESKTOP_TOP    FB_RGB(24,  38,  56)
#define COLOR_DESKTOP_BOTTOM FB_RGB(9,   14,  22)
#define COLOR_TASKBAR        FB_RGB(22,  25,  29)
#define COLOR_TASKBAR_EDGE   FB_RGB(48,  54,  61)
#define COLOR_WINDOW_BODY    FB_RGB(30,  33,  37)
#define COLOR_TITLE_ACTIVE   FB_RGB(38,  110, 170)
#define COLOR_TITLE_INACTIVE FB_RGB(48,  53,  59)
#define COLOR_BORDER_ACTIVE  FB_RGB(70,  150, 210)
#define COLOR_BORDER_IDLE    FB_RGB(58,  63,  70)
#define COLOR_CLOSE          FB_RGB(200, 70,  60)

// Cursor bitmap dimensions, needed by the save-under buffer below.
#define CURSOR_W 12
#define CURSOR_H 19

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

static gui_window_t  windows[GUI_MAX_WINDOWS];
static gui_window_t* z_order[GUI_MAX_WINDOWS];   // back to front
static uint32_t      window_count = 0;
static gui_window_t* focused = NULL;

static int running = 0;
static int desktop_needs_repaint = 1;

// Window dragging
static gui_window_t* dragging = NULL;
static int32_t       drag_offset_x, drag_offset_y;
static fb_rect_t     drag_previous_rect;

static uint32_t frame_interval_ms = GUI_TARGET_FRAME_MS;
static int      game_mode = 0;

static uint64_t frames_rendered = 0;
static uint32_t last_frame_ms   = 0;

// Mouse cursor "save under".
//
// The cursor is drawn into the same back buffer as everything else, but it is
// not part of any window - so the pixels underneath have to be saved before it
// is drawn and restored before the next frame paints.
//
// Skipping this was a real bug with a very recognisable symptom: the cursor
// became permanent, and moving the mouse left a trail of arrows across the
// whole desktop. Marking the old position dirty is not enough - dirty only
// means "copy this region to VRAM", and the region still had a cursor in it.
static uint32_t cursor_backing[CURSOR_W * CURSOR_H];
static int32_t  cursor_saved_x = -1;
static int32_t  cursor_saved_y = -1;
static int32_t  cursor_saved_w = 0;
static int32_t  cursor_saved_h = 0;

static process_t* compositor_task = NULL;

// ---------------------------------------------------------------------------
// Mouse cursor
//
// A tiny hand-drawn arrow. 0 means transparent, 1 outline, 2 fill - expanded
// to real colours at blit time so the same bitmap works on any background.
// ---------------------------------------------------------------------------

static const uint8_t cursor_bitmap[CURSOR_H][CURSOR_W] = {
    {1,0,0,0,0,0,0,0,0,0,0,0},
    {1,1,0,0,0,0,0,0,0,0,0,0},
    {1,2,1,0,0,0,0,0,0,0,0,0},
    {1,2,2,1,0,0,0,0,0,0,0,0},
    {1,2,2,2,1,0,0,0,0,0,0,0},
    {1,2,2,2,2,1,0,0,0,0,0,0},
    {1,2,2,2,2,2,1,0,0,0,0,0},
    {1,2,2,2,2,2,2,1,0,0,0,0},
    {1,2,2,2,2,2,2,2,1,0,0,0},
    {1,2,2,2,2,2,2,2,2,1,0,0},
    {1,2,2,2,2,2,1,1,1,1,1,0},
    {1,2,2,2,2,2,1,0,0,0,0,0},
    {1,2,2,1,1,2,2,1,0,0,0,0},
    {1,2,1,0,1,2,2,1,0,0,0,0},
    {1,1,0,0,0,1,2,2,1,0,0,0},
    {1,0,0,0,0,1,2,2,1,0,0,0},
    {0,0,0,0,0,0,1,2,2,1,0,0},
    {0,0,0,0,0,0,1,2,2,1,0,0},
    {0,0,0,0,0,0,0,1,1,0,0,0},
};

static void draw_cursor(int32_t x, int32_t y)
{
    for (int32_t row = 0; row < CURSOR_H; row++) {
        for (int32_t col = 0; col < CURSOR_W; col++) {
            uint8_t pixel = cursor_bitmap[row][col];
            if (pixel == 0) {
                continue;   // transparent
            }
            fb_pixel(x + col, y + row, pixel == 1 ? FB_BLACK : FB_WHITE);
        }
    }
}

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

void gui_client_rect(const gui_window_t* window, fb_rect_t* out)
{
    out->x      = window->x + GUI_BORDER;
    out->y      = window->y + GUI_TITLEBAR_H;
    out->width  = window->width  - GUI_BORDER * 2;
    out->height = window->height - GUI_TITLEBAR_H - GUI_BORDER;
}

static int point_in_window(const gui_window_t* window, int32_t x, int32_t y)
{
    return x >= window->x && x < window->x + window->width &&
           y >= window->y && y < window->y + window->height;
}

// The close button: a square at the right end of the title bar.
static int point_in_close_button(const gui_window_t* window, int32_t x, int32_t y)
{
    int32_t bx = window->x + window->width - GUI_TITLEBAR_H;
    return x >= bx && x < bx + GUI_TITLEBAR_H - GUI_BORDER &&
           y >= window->y && y < window->y + GUI_TITLEBAR_H;
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------

static void paint_desktop(void)
{
    fb_reset_clip();

    // Vertical gradient wallpaper.
    fb_fill_gradient(0, 0, (int32_t)fb_width(),
                     (int32_t)fb_height() - GUI_TASKBAR_H,
                     COLOR_DESKTOP_TOP, COLOR_DESKTOP_BOTTOM);

    fb_printf(16, 16, FB_RGB(70, 90, 115), FB_TRANSPARENT, "BlitzOS");
    fb_printf(16, 16 + FONT_HEIGHT, FB_RGB(50, 65, 85), FB_TRANSPARENT,
              "F1 stats   F2 tile   F3 game mode   ESC console");

    fb_mark_dirty(0, 0, (int32_t)fb_width(),
                  (int32_t)fb_height() - GUI_TASKBAR_H);
}

// Repaint the taskbar only when something on it actually changed.
//
// It was being redrawn every frame, marking 1024x28 pixels dirty 60 times a
// second for a clock that ticks once a second. That is 28k pixels of pointless
// copying per frame and it dominated the dirty-rectangle statistics.
static uint64_t taskbar_last_second = ~0ULL;
static gui_window_t* taskbar_last_focus = NULL;
static uint32_t taskbar_last_count = 0;
static int      taskbar_last_game_mode = -1;

static int taskbar_needs_repaint(void)
{
    uint64_t second = scheduler_get_ticks() / pit_get_frequency();

    if (second != taskbar_last_second ||
        focused != taskbar_last_focus ||
        window_count != taskbar_last_count ||
        game_mode != taskbar_last_game_mode) {

        taskbar_last_second    = second;
        taskbar_last_focus     = focused;
        taskbar_last_count     = window_count;
        taskbar_last_game_mode = game_mode;
        return 1;
    }
    return 0;
}

static void paint_taskbar(void)
{
    int32_t y = (int32_t)fb_height() - GUI_TASKBAR_H;

    fb_reset_clip();
    fb_fill_rect(0, y, (int32_t)fb_width(), GUI_TASKBAR_H, COLOR_TASKBAR);
    fb_hline(0, y, (int32_t)fb_width(), COLOR_TASKBAR_EDGE);

    // One button per window.
    int32_t bx = 6;
    for (uint32_t i = 0; i < window_count; i++) {
        gui_window_t* window = z_order[i];
        if (!window->visible) {
            continue;
        }

        int32_t bw = 140;
        int active = (window == focused);

        fb_fill_rect(bx, y + 4, bw, GUI_TASKBAR_H - 8,
                     active ? COLOR_TITLE_ACTIVE : COLOR_TITLE_INACTIVE);
        fb_draw_string(bx + 6, y + 8, window->title, FB_WHITE, FB_TRANSPARENT);

        bx += bw + 6;
    }

    // Clock and frame cost on the right.
    uint64_t seconds = scheduler_get_ticks() / pit_get_frequency();
    fb_printf((int32_t)fb_width() - 190, y + 8, FB_LIGHT_GREY, FB_TRANSPARENT,
              "%s %02lu:%02lu:%02lu",
              game_mode ? "GAME" : "    ",
              (seconds / 3600) % 24, (seconds / 60) % 60, seconds % 60);

    fb_mark_dirty(0, y, (int32_t)fb_width(), GUI_TASKBAR_H);
}

static void paint_window(gui_window_t* window)
{
    if (!window->visible) {
        return;
    }

    int active = (window == focused);

    fb_reset_clip();

    // Border and body.
    fb_fill_rect(window->x, window->y, window->width, window->height,
                 COLOR_WINDOW_BODY);
    fb_draw_rect(window->x, window->y, window->width, window->height,
                 active ? COLOR_BORDER_ACTIVE : COLOR_BORDER_IDLE);

    // Title bar.
    fb_fill_rect(window->x + 1, window->y + 1,
                 window->width - 2, GUI_TITLEBAR_H - 1,
                 active ? COLOR_TITLE_ACTIVE : COLOR_TITLE_INACTIVE);

    fb_draw_string(window->x + 8, window->y + 3, window->title,
                   FB_WHITE, FB_TRANSPARENT);

    // Close button.
    int32_t bx = window->x + window->width - GUI_TITLEBAR_H;
    fb_fill_rect(bx, window->y + 3, GUI_TITLEBAR_H - 6, GUI_TITLEBAR_H - 6,
                 COLOR_CLOSE);
    fb_draw_string(bx + 4, window->y + 3, "x", FB_WHITE, FB_TRANSPARENT);

    // Client area. Clipping is set before handing control to the app so a
    // buggy paint handler cannot scribble over other windows.
    if (window->on_paint) {
        fb_rect_t client;
        gui_client_rect(window, &client);
        fb_set_clip(client.x, client.y, client.width, client.height);
        window->on_paint(window);
        fb_reset_clip();
    }

    fb_mark_dirty(window->x, window->y, window->width, window->height);
    window->needs_repaint = 0;
}

// ---------------------------------------------------------------------------
// Window management
// ---------------------------------------------------------------------------

gui_window_t* gui_create_window(const char* title, int32_t x, int32_t y,
                                int32_t width, int32_t height,
                                gui_paint_fn on_paint)
{
    if (window_count >= GUI_MAX_WINDOWS) {
        kerror("gui: window limit (%u) reached\n", (uint32_t)GUI_MAX_WINDOWS);
        return NULL;
    }

    gui_window_t* window = &windows[window_count];
    memset(window, 0, sizeof(*window));

    window->x      = x;
    window->y      = y;
    window->width  = width;
    window->height = height;
    strlcpy(window->title, title ? title : "window", GUI_TITLE_MAX);

    window->visible       = 1;
    window->needs_repaint = 1;
    window->on_paint      = on_paint;

    z_order[window_count] = window;
    window_count++;

    focused = window;
    gui_invalidate_all();

    return window;
}

void gui_destroy_window(gui_window_t* window)
{
    if (!window) {
        return;
    }

    window->visible = 0;

    // Compact the z-order array so iteration stays simple.
    uint32_t out = 0;
    for (uint32_t i = 0; i < window_count; i++) {
        if (z_order[i] != window) {
            z_order[out++] = z_order[i];
        }
    }
    window_count = out;

    if (focused == window) {
        focused = window_count ? z_order[window_count - 1] : NULL;
    }

    // Everything under the closed window is now exposed.
    desktop_needs_repaint = 1;
    gui_invalidate_all();
}

void gui_focus_window(gui_window_t* window)
{
    if (!window || focused == window) {
        return;
    }

    // Move to the end of the z-order, which is the front.
    uint32_t out = 0;
    for (uint32_t i = 0; i < window_count; i++) {
        if (z_order[i] != window) {
            z_order[out++] = z_order[i];
        }
    }
    z_order[out] = window;

    focused = window;
    gui_invalidate_all();
}

gui_window_t* gui_focused_window(void)
{
    return focused;
}

void gui_invalidate(gui_window_t* window)
{
    if (window) {
        window->needs_repaint = 1;
    }
}

void gui_invalidate_all(void)
{
    for (uint32_t i = 0; i < window_count; i++) {
        z_order[i]->needs_repaint = 1;
    }
}

// ---------------------------------------------------------------------------
// Input routing
// ---------------------------------------------------------------------------

static gui_window_t* window_at(int32_t x, int32_t y)
{
    // Front to back: the topmost window that contains the point wins.
    for (int32_t i = (int32_t)window_count - 1; i >= 0; i--) {
        if (z_order[i]->visible && point_in_window(z_order[i], x, y)) {
            return z_order[i];
        }
    }
    return NULL;
}

static void handle_mouse(void)
{
    const mouse_state_t* mouse = mouse_get_state();

    if (mouse_button_pressed(MOUSE_BUTTON_LEFT)) {
        gui_window_t* hit = window_at(mouse->x, mouse->y);

        if (hit) {
            gui_focus_window(hit);

            if (point_in_close_button(hit, mouse->x, mouse->y)) {
                gui_destroy_window(hit);
            } else if (mouse->y < hit->y + GUI_TITLEBAR_H) {
                // Title bar: start dragging.
                dragging      = hit;
                drag_offset_x = mouse->x - hit->x;
                drag_offset_y = mouse->y - hit->y;
                drag_previous_rect = (fb_rect_t){ hit->x, hit->y,
                                                  hit->width, hit->height };
            } else if (hit->on_click) {
                fb_rect_t client;
                gui_client_rect(hit, &client);
                hit->on_click(hit, mouse->x - client.x, mouse->y - client.y,
                              mouse->buttons);
            }
        }
    }

    if (mouse_button_released(MOUSE_BUTTON_LEFT)) {
        dragging = NULL;
    }

    if (dragging && (mouse->buttons & MOUSE_BUTTON_LEFT)) {
        int32_t new_x = mouse->x - drag_offset_x;
        int32_t new_y = mouse->y - drag_offset_y;

        // Keep the title bar reachable: never let a window go fully off-screen
        // or under the taskbar.
        if (new_x < -(dragging->width - 80))  new_x = -(dragging->width - 80);
        if (new_y < 0)                        new_y = 0;
        if (new_x > (int32_t)fb_width() - 80) new_x = (int32_t)fb_width() - 80;
        if (new_y > (int32_t)fb_height() - GUI_TASKBAR_H - GUI_TITLEBAR_H) {
            new_y = (int32_t)fb_height() - GUI_TASKBAR_H - GUI_TITLEBAR_H;
        }

        if (new_x != dragging->x || new_y != dragging->y) {
            // Damage the area the window is leaving as well as where it lands,
            // otherwise a trail of the old title bar stays on the wallpaper.
            fb_mark_dirty(drag_previous_rect.x, drag_previous_rect.y,
                          drag_previous_rect.width, drag_previous_rect.height);

            dragging->x = new_x;
            dragging->y = new_y;

            drag_previous_rect = (fb_rect_t){ new_x, new_y,
                                              dragging->width, dragging->height };

            desktop_needs_repaint = 1;
            gui_invalidate_all();
        }
    }

    mouse_clear_edges();
}

static void tile_windows(void)
{
    uint32_t visible = 0;
    for (uint32_t i = 0; i < window_count; i++) {
        if (z_order[i]->visible) visible++;
    }
    if (visible == 0) {
        return;
    }

    int32_t area_h  = (int32_t)fb_height() - GUI_TASKBAR_H;
    int32_t columns = visible > 2 ? 2 : (int32_t)visible;
    int32_t rows    = (int32_t)((visible + columns - 1) / columns);

    int32_t cell_w = (int32_t)fb_width() / columns;
    int32_t cell_h = area_h / rows;

    uint32_t index = 0;
    for (uint32_t i = 0; i < window_count; i++) {
        gui_window_t* window = z_order[i];
        if (!window->visible) {
            continue;
        }

        window->x      = (int32_t)(index % (uint32_t)columns) * cell_w + 4;
        window->y      = (int32_t)(index / (uint32_t)columns) * cell_h + 4;
        window->width  = cell_w - 8;
        window->height = cell_h - 8;
        index++;
    }

    desktop_needs_repaint = 1;
    gui_invalidate_all();
}

static void handle_keys(void)
{
    int key;
    while ((key = keyboard_getkey_nonblocking()) != KEY_NONE) {

        // Compositor hotkeys are intercepted before the focused window sees
        // them, so a window cannot swallow the way out of the desktop.
        if (key == KEY_FKEY_BASE + 0) {          // F1
            fb_set_stats_overlay(!fb_get_stats_overlay());
            fb_mark_all_dirty();
            desktop_needs_repaint = 1;
            continue;
        }
        if (key == KEY_FKEY_BASE + 1) {          // F2
            tile_windows();
            continue;
        }
        if (key == KEY_FKEY_BASE + 2) {          // F3
            gui_set_game_mode(!game_mode);
            continue;
        }
        if (key == 27) {                          // ESC
            running = 0;
            continue;
        }

        if (focused && focused->on_key) {
            focused->on_key(focused, key);
        }
    }
}

// ---------------------------------------------------------------------------
// The compositor
// ---------------------------------------------------------------------------

static void compositor_thread(void)
{
    while (running) {
        uint64_t frame_start_us = tsc_us();

        handle_keys();
        handle_mouse();

        // Undo last frame's cursor before anything paints. If a window repaints
        // over this region the restore is harmlessly overwritten; if nothing
        // does, the original pixels are back.
        if (cursor_saved_x >= 0) {
            fb_reset_clip();
            fb_blit(cursor_saved_x, cursor_saved_y,
                    cursor_saved_w, cursor_saved_h,
                    cursor_backing, cursor_saved_w);
            fb_mark_dirty(cursor_saved_x, cursor_saved_y,
                          cursor_saved_w, cursor_saved_h);
            cursor_saved_x = -1;
        }

        // Give each app a chance to notice its state changed.
        for (uint32_t i = 0; i < window_count; i++) {
            apps_tick(z_order[i]);
        }

        // --- Paint ---
        int repainted_desktop = 0;

        if (desktop_needs_repaint) {
            paint_desktop();
            desktop_needs_repaint = 0;
            repainted_desktop = 1;
        }

        for (uint32_t i = 0; i < window_count; i++) {
            gui_window_t* window = z_order[i];

            // In game mode only the front window is repainted. Background
            // windows keep whatever was last drawn, which is exactly the
            // trade a game wants: spend the frame budget on what is on top.
            //
            // The exception matters: if we just repainted the wallpaper, it
            // has erased every background window, so they MUST be redrawn this
            // frame regardless of game mode. Skipping them here was a real bug
            // - toggling game mode left the desktop black with only the focused
            // window visible, because the frozen windows were painted over and
            // never restored.
            if (game_mode && window != focused && !repainted_desktop) {
                continue;
            }

            if (window->needs_repaint || repainted_desktop) {
                paint_window(window);
            }
        }

        if (taskbar_needs_repaint() || repainted_desktop) {
            paint_taskbar();
        }

        // --- Overlay before the cursor, so the cursor stays on top ---
        fb_draw_stats_overlay();

        // --- Cursor, always last so it sits above everything ---
        const mouse_state_t* mouse = mouse_get_state();

        fb_reset_clip();

        // Clamp at the screen edges so the saved rectangle matches what we
        // actually draw over.
        int32_t cw = CURSOR_W;
        int32_t chh = CURSOR_H;
        if (mouse->x + cw  > (int32_t)fb_width())  cw  = (int32_t)fb_width()  - mouse->x;
        if (mouse->y + chh > (int32_t)fb_height()) chh = (int32_t)fb_height() - mouse->y;

        if (cw > 0 && chh > 0) {
            fb_read_rect(mouse->x, mouse->y, cw, chh, cursor_backing);
            cursor_saved_x = mouse->x;
            cursor_saved_y = mouse->y;
            cursor_saved_w = cw;
            cursor_saved_h = chh;

            draw_cursor(mouse->x, mouse->y);
            fb_mark_dirty(mouse->x, mouse->y, cw, chh);
        }

        mouse_clear_moved();

        // --- Present ---
        fb_present();
        frames_rendered++;

        uint32_t elapsed_ms = (uint32_t)(tsc_us_since(frame_start_us) / 1000);
        last_frame_ms = elapsed_ms;

        // --- Frame pacing ---
        //
        // Sleep out the rest of the budget rather than spinning. This is what
        // keeps an idle desktop from consuming the whole CPU: process_sleep()
        // takes the compositor off the run queue entirely, so the shell and
        // everything else get the time instead.
        if (elapsed_ms < frame_interval_ms) {
            process_sleep_ms(frame_interval_ms - elapsed_ms);
        } else {
            // Over budget: yield anyway so we can never starve other processes.
            process_yield();
        }
    }

    // Leaving the desktop: hand the console back to VGA text mode.
    gui_stop();
    process_exit(0);
}

// ---------------------------------------------------------------------------
// Start / stop
// ---------------------------------------------------------------------------

int gui_start(uint32_t width, uint32_t height)
{
    if (running) {
        return 1;
    }

    if (!fb_init(width, height)) {
        kerror("gui: graphics initialisation failed, staying in text mode\n");
        return 0;
    }

    mouse_set_bounds((int32_t)fb_width(), (int32_t)fb_height());

    window_count = 0;
    focused      = NULL;
    dragging     = NULL;
    taskbar_last_second = ~0ULL;
    frames_rendered = 0;
    desktop_needs_repaint = 1;
    cursor_saved_x = -1;

    apps_create_default_windows();

    running = 1;

    // The compositor runs above normal work but below the shell, so typing
    // always stays responsive even when rendering is busy.
    compositor_task = process_create("compositor", compositor_thread,
                                        PRIORITY_HIGH - 16);
    if (!compositor_task) {
        kerror("gui: could not start the compositor process\n");
        fb_shutdown();
        running = 0;
        return 0;
    }

    kok("desktop started: %ux%u, %u windows, %u ms frame budget\n",
        fb_width(), fb_height(), window_count, frame_interval_ms);

    return 1;
}

void gui_stop(void)
{
    if (!running && !fb_is_active()) {
        return;
    }

    running = 0;

    apps_shutdown();
    fb_shutdown();

    // Back to the VGA text console.
    kprintf_set_console_hook(NULL);
    vga_init();
    vga_clear();

    kok("Returned to text mode after %lu frames\n", frames_rendered);
}

int gui_is_running(void)
{
    return running;
}

// ---------------------------------------------------------------------------
// Frame pacing and game mode
// ---------------------------------------------------------------------------

void gui_set_frame_interval_ms(uint32_t ms)
{
    if (ms < 1)   ms = 1;
    if (ms > 200) ms = 200;
    frame_interval_ms = ms;
}

uint32_t gui_get_frame_interval_ms(void)
{
    return frame_interval_ms;
}

void gui_set_game_mode(int enabled)
{
    game_mode = enabled ? 1 : 0;

    if (game_mode) {
        // Three separate changes, each buying something different:
        //
        //   1. A faster timer. At 100 Hz the finest sleep is 10 ms, so frame
        //      pacing quantises to 10/20/30 ms - you cannot ask for 16. At
        //      1000 Hz the scheduler can actually hit a 16 ms budget, and
        //      input latency drops because a woken process runs sooner.
        pit_set_frequency(1000);

        //   2. A shorter frame budget, now that the timer can express it.
        gui_set_frame_interval_ms(8);

        //   3. Priority. The compositor stops competing with background work
        //      for the CPU.
        if (compositor_task) {
            compositor_task->base_priority = PRIORITY_CRITICAL;
            compositor_task->priority      = PRIORITY_CRITICAL;
        }

        kinfo("game mode ON: 1000 Hz timer, 8 ms frames, compositor at "
              "critical priority, background windows frozen\n");
    } else {
        pit_set_frequency(TIMER_FREQUENCY);
        gui_set_frame_interval_ms(GUI_TARGET_FRAME_MS);

        if (compositor_task) {
            compositor_task->base_priority = PRIORITY_HIGH - 16;
            compositor_task->priority      = PRIORITY_HIGH - 16;
        }

        kinfo("game mode OFF: back to %u Hz timer and %u ms frames\n",
              (uint32_t)TIMER_FREQUENCY, frame_interval_ms);
    }

    fb_reset_stats();
    gui_invalidate_all();
    desktop_needs_repaint = 1;
}

int gui_get_game_mode(void)
{
    return game_mode;
}

uint64_t gui_frames_rendered(void)
{
    return frames_rendered;
}

uint32_t gui_last_frame_ms(void)
{
    return last_frame_ms;
}
