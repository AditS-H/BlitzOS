#include "apps.h"
#include "gui.h"
#include "../lib/kprintf.h"
#include "../lib/string.h"
#include "../proc/process.h"
#include "../mm/pmm.h"
#include "../mm/kheap.h"
#include "../fs/vfs/vfs.h"
#include "../arch/x86_64/tsc.h"
#include "../arch/x86_64/sse.h"
#include "../arch/x86_64/interrupts.h"
#include "../../drivers/video/framebuffer.h"
#include "../../drivers/video/font.h"
#include "../../drivers/video/bga.h"
#include "../../drivers/keyboard.h"
#include "../../drivers/pit.h"

// ===========================================================================
// Terminal
// ===========================================================================

#define TERM_COLS 76
#define TERM_ROWS 28

// The text grid. Characters and their colours are stored separately so
// scrolling moves two compact arrays rather than one array of structs.
static char       term_chars[TERM_ROWS][TERM_COLS];
static fb_color_t term_colors[TERM_ROWS][TERM_COLS];
static int        term_cx = 0;
static int        term_cy = 0;
static int        term_dirty = 1;

static gui_window_t* terminal_window = NULL;

// Input queue. The compositor owns the keyboard, so keystrokes destined for
// the shell are handed over here instead of read directly.
#define TERM_INPUT_SIZE 128
static volatile uint16_t term_input[TERM_INPUT_SIZE];
static volatile uint16_t term_input_read  = 0;
static volatile uint16_t term_input_write = 0;

// VGA colour index -> RGB, so kprintf's existing colour argument keeps working
// unchanged when output is redirected into the window.
static const fb_color_t vga_palette[16] = {
    FB_RGB(0,   0,   0),    FB_RGB(0,   0,   170),
    FB_RGB(0,   170, 0),    FB_RGB(0,   170, 170),
    FB_RGB(170, 0,   0),    FB_RGB(170, 0,   170),
    FB_RGB(170, 85,  0),    FB_RGB(170, 170, 170),
    FB_RGB(85,  85,  85),   FB_RGB(85,  85,  255),
    FB_RGB(85,  255, 85),   FB_RGB(85,  255, 255),
    FB_RGB(255, 85,  85),   FB_RGB(255, 85,  255),
    FB_RGB(255, 255, 85),   FB_RGB(255, 255, 255),
};

static void terminal_scroll(void)
{
    for (int row = 0; row < TERM_ROWS - 1; row++) {
        memcpy(term_chars[row],  term_chars[row + 1],  TERM_COLS);
        memcpy(term_colors[row], term_colors[row + 1], TERM_COLS * sizeof(fb_color_t));
    }

    memset(term_chars[TERM_ROWS - 1], ' ', TERM_COLS);
    for (int col = 0; col < TERM_COLS; col++) {
        term_colors[TERM_ROWS - 1][col] = FB_LIGHT_GREY;
    }

    term_cy = TERM_ROWS - 1;
}

void terminal_putchar_hook(char c, vga_color_t color)
{
    fb_color_t rgb = vga_palette[color & 0x0F];

    switch (c) {
    case '\n':
        term_cx = 0;
        if (++term_cy >= TERM_ROWS) {
            terminal_scroll();
        }
        break;

    case '\r':
        term_cx = 0;
        break;

    case '\b':
        if (term_cx > 0) {
            term_cx--;
            term_chars[term_cy][term_cx] = ' ';
        }
        break;

    case '\t':
        term_cx = (term_cx + 4) & ~3;
        if (term_cx >= TERM_COLS) {
            term_cx = 0;
            if (++term_cy >= TERM_ROWS) terminal_scroll();
        }
        break;

    default:
        if (c < 32) {
            return;   // other control characters are not printable
        }
        term_chars[term_cy][term_cx]  = c;
        term_colors[term_cy][term_cx] = rgb;

        if (++term_cx >= TERM_COLS) {
            term_cx = 0;
            if (++term_cy >= TERM_ROWS) terminal_scroll();
        }
        break;
    }

    term_dirty = 1;
}

static void terminal_paint(gui_window_t* window)
{
    fb_rect_t client;
    gui_client_rect(window, &client);

    fb_fill_rect(client.x, client.y, client.width, client.height,
                 FB_RGB(16, 18, 20));

    for (int row = 0; row < TERM_ROWS; row++) {
        int32_t y = client.y + 2 + row * FONT_HEIGHT;
        if (y + FONT_HEIGHT > client.y + client.height) {
            break;
        }

        for (int col = 0; col < TERM_COLS; col++) {
            char c = term_chars[row][col];
            if (c == 0 || c == ' ') {
                continue;   // nothing to draw; the background is already right
            }

            fb_draw_char(client.x + 2 + col * FONT_WIDTH, y, c,
                         term_colors[row][col], FB_TRANSPARENT);
        }
    }

    // Block cursor.
    fb_fill_rect(client.x + 2 + term_cx * FONT_WIDTH,
                 client.y + 2 + term_cy * FONT_HEIGHT,
                 FONT_WIDTH, FONT_HEIGHT, FB_RGB(90, 160, 90));

    term_dirty = 0;
}

static void terminal_key(gui_window_t* window, int key)
{
    (void)window;

    uint16_t next = (uint16_t)((term_input_write + 1) % TERM_INPUT_SIZE);
    if (next == term_input_read) {
        return;   // queue full, drop rather than overwrite unread input
    }

    term_input[term_input_write] = (uint16_t)key;
    term_input_write = next;

    // Wake the shell, which is blocked in terminal_getkey().
    process_wake_all(WAIT_CHANNEL_TERMINAL);
}

int terminal_getkey(void)
{
    for (;;) {
        uint64_t flags = irq_save();

        if (term_input_read != term_input_write) {
            int key = term_input[term_input_read];
            term_input_read = (uint16_t)((term_input_read + 1) % TERM_INPUT_SIZE);
            irq_restore(flags);
            return key;
        }

        irq_restore(flags);

        // Nothing queued: come off the run queue entirely until the compositor
        // hands us a key. Costs zero CPU while waiting.
        process_block(WAIT_CHANNEL_TERMINAL);
    }
}

int terminal_is_active(void)
{
    return terminal_window != NULL && terminal_window->visible;
}

// ===========================================================================
// System monitor
// ===========================================================================

static gui_window_t* monitor_window = NULL;
static uint64_t      monitor_last_update = 0;

// Horizontal bar with a filled proportion, used for the memory gauges.
static void draw_bar(int32_t x, int32_t y, int32_t w, int32_t h,
                     uint32_t percent, fb_color_t fill)
{
    if (percent > 100) percent = 100;

    fb_fill_rect(x, y, w, h, FB_RGB(24, 26, 29));
    fb_fill_rect(x, y, (int32_t)((uint32_t)w * percent / 100), h, fill);
    fb_draw_rect(x, y, w, h, FB_DARK_GREY);
}

static void monitor_paint(gui_window_t* window)
{
    fb_rect_t client;
    gui_client_rect(window, &client);

    fb_fill_rect(client.x, client.y, client.width, client.height,
                 FB_RGB(26, 28, 32));

    int32_t x = client.x + 8;
    int32_t y = client.y + 6;

    // ---- Memory ----
    uint64_t total = pmm_get_total_memory();
    uint64_t used  = pmm_get_used_memory();
    uint32_t percent = total ? (uint32_t)(used * 100 / total) : 0;

    fb_printf(x, y, FB_WHITE, FB_TRANSPARENT, "Physical memory");
    y += FONT_HEIGHT + 2;
    fb_printf(x, y, FB_GREY, FB_TRANSPARENT, "%lu / %lu MB  (%u%%)",
              used / (1024 * 1024), total / (1024 * 1024), percent);
    y += FONT_HEIGHT + 2;
    draw_bar(x, y, client.width - 16, 10, percent, FB_BLUE);
    y += 18;

    size_t heap_total, heap_used, free_blocks, used_blocks;
    kheap_get_stats(&heap_total, &heap_used, &free_blocks, &used_blocks);
    uint32_t heap_percent = heap_total ?
        (uint32_t)((uint64_t)heap_used * 100 / heap_total) : 0;

    fb_printf(x, y, FB_WHITE, FB_TRANSPARENT, "Kernel heap");
    y += FONT_HEIGHT + 2;
    fb_printf(x, y, FB_GREY, FB_TRANSPARENT, "%lu / %lu KB  %lu blocks",
              (uint64_t)heap_used / 1024, (uint64_t)heap_total / 1024,
              (uint64_t)used_blocks);
    y += FONT_HEIGHT + 2;
    draw_bar(x, y, client.width - 16, 10, heap_percent, FB_CYAN);
    y += 22;

    // ---- Processes ----
    fb_printf(x, y, FB_WHITE, FB_TRANSPARENT, "PID  PRI  STATE     CPUms NAME");
    y += FONT_HEIGHT + 2;

    process_t* current = get_current_process();

    for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
        process_t* p = process_at_index(i);
        if (!p) {
            continue;
        }
        if (y + FONT_HEIGHT > client.y + client.height) {
            break;
        }

        fb_color_t color = (p == current) ? FB_GREEN :
                           (p->state == PROCESS_SLEEPING ? FB_DARK_GREY : FB_GREY);

        fb_printf(x, y, color, FB_TRANSPARENT, "%-4u %-4u %-9s %-5lu %s",
                  p->pid, p->base_priority, process_state_name(p->state),
                  p->total_ticks * 10, p->name);
        y += FONT_HEIGHT;
    }

    // ---- Graphics pipeline ----
    const fb_stats_t* stats = fb_get_stats();
    y += 6;
    if (y + FONT_HEIGHT * 2 < client.y + client.height) {
        fb_printf(x, y, FB_YELLOW, FB_TRANSPARENT,
                  "flip:%s  blit:%s",
                  stats->hardware_flip ? "hardware" : "software",
                  stats->sse_blitter ? "SSE2" : "scalar");
    }
}

// ===========================================================================
// Starfield
//
// Deliberately animates the whole client area every frame. Without something
// like this the desktop is static, dirty rectangles report 99% savings, and
// the frame timings say nothing useful.
// ===========================================================================

#define STAR_COUNT 220

typedef struct {
    int32_t x, y, z;   // fixed point, z is depth
} star_t;

static star_t        stars[STAR_COUNT];
static gui_window_t* starfield_window = NULL;
static uint32_t      star_seed = 2463534242u;

// Xorshift. Cheap, no division, good enough for scattering stars.
static uint32_t star_rand(void)
{
    star_seed ^= star_seed << 13;
    star_seed ^= star_seed >> 17;
    star_seed ^= star_seed << 5;
    return star_seed;
}

static void star_respawn(star_t* star)
{
    star->x = (int32_t)(star_rand() % 2000) - 1000;
    star->y = (int32_t)(star_rand() % 2000) - 1000;
    star->z = (int32_t)(star_rand() % 1000) + 1;
}

static void starfield_init(void)
{
    for (int i = 0; i < STAR_COUNT; i++) {
        star_respawn(&stars[i]);
    }
}

static void starfield_paint(gui_window_t* window)
{
    fb_rect_t client;
    gui_client_rect(window, &client);

    fb_fill_rect(client.x, client.y, client.width, client.height, FB_BLACK);

    int32_t cx = client.x + client.width / 2;
    int32_t cy = client.y + client.height / 2;

    for (int i = 0; i < STAR_COUNT; i++) {
        star_t* star = &stars[i];

        // Move toward the viewer, and recycle anything that passes it.
        star->z -= 8;
        if (star->z <= 1) {
            star_respawn(star);
            star->z = 1000;
        }

        // Perspective divide, integer only - no FPU in the kernel.
        int32_t sx = cx + (star->x * 200) / star->z;
        int32_t sy = cy + (star->y * 200) / star->z;

        if (sx < client.x || sx >= client.x + client.width ||
            sy < client.y || sy >= client.y + client.height) {
            continue;
        }

        // Closer stars are brighter and bigger.
        int32_t brightness = 255 - (star->z / 4);
        if (brightness < 40)  brightness = 40;
        if (brightness > 255) brightness = 255;

        fb_color_t color = FB_RGB(brightness, brightness, brightness);

        fb_pixel(sx, sy, color);
        if (star->z < 300) {
            fb_pixel(sx + 1, sy,     color);
            fb_pixel(sx,     sy + 1, color);
            fb_pixel(sx + 1, sy + 1, color);
        }
    }

    const fb_stats_t* stats = fb_get_stats();
    fb_printf(client.x + 4, client.y + 4, FB_GREEN, FB_TRANSPARENT,
              "%d stars  %u us/present", STAR_COUNT, stats->average_frame_us);
}

// ===========================================================================
// Wiring
// ===========================================================================

void apps_create_default_windows(void)
{
    // Blank the terminal grid before anything can write to it.
    for (int row = 0; row < TERM_ROWS; row++) {
        memset(term_chars[row], ' ', TERM_COLS);
        for (int col = 0; col < TERM_COLS; col++) {
            term_colors[row][col] = FB_LIGHT_GREY;
        }
    }
    term_cx = 0;
    term_cy = 0;
    term_input_read  = 0;
    term_input_write = 0;

    int32_t screen_w = (int32_t)fb_width();
    int32_t screen_h = (int32_t)fb_height() - GUI_TASKBAR_H;

    terminal_window = gui_create_window(
        "Terminal", 12, 12,
        TERM_COLS * FONT_WIDTH + 8 + GUI_BORDER * 2,
        TERM_ROWS * FONT_HEIGHT + 8 + GUI_TITLEBAR_H,
        terminal_paint);

    if (terminal_window) {
        terminal_window->on_key = terminal_key;

        // From here on, every kprintf goes into the window instead of VGA text
        // memory - which is no longer being scanned out.
        kprintf_set_console_hook(terminal_putchar_hook);
    }

    monitor_window = gui_create_window(
        "System Monitor",
        screen_w - 360, 12, 348, 400,
        monitor_paint);

    starfield_init();
    starfield_window = gui_create_window(
        "Starfield", screen_w - 360, screen_h - 300, 348, 288,
        starfield_paint);

    // Terminal in front, since that is where you type.
    if (terminal_window) {
        gui_focus_window(terminal_window);
    }
}

void apps_tick(gui_window_t* window)
{
    if (!window || !window->visible) {
        return;
    }

    // The starfield animates continuously, so it is dirty every frame.
    if (window == starfield_window) {
        gui_invalidate(window);
        return;
    }

    // The monitor only changes meaningfully once a second; repainting it at
    // 60 Hz would be 60x the work for no visible difference.
    if (window == monitor_window) {
        uint64_t now = scheduler_get_ticks();
        if (now - monitor_last_update >= TIMER_FREQUENCY / 2) {
            monitor_last_update = now;
            gui_invalidate(window);
        }
        return;
    }

    // The terminal repaints only when text actually changed.
    if (window == terminal_window && term_dirty) {
        gui_invalidate(window);
    }
}

void apps_shutdown(void)
{
    kprintf_set_console_hook(NULL);

    terminal_window  = NULL;
    monitor_window   = NULL;
    starfield_window = NULL;
}
