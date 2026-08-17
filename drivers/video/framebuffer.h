// framebuffer.h - 32bpp linear framebuffer with double buffering.
//
// THE RENDERING PIPELINE, AND WHY IT IS SHAPED THIS WAY
// -----------------------------------------------------
// Naive framebuffer code draws straight into video memory. That is wrong for
// two independent reasons:
//
//   Tearing. The display controller is scanning out continuously. If you draw
//   while it reads, the top of the screen shows the new frame and the bottom
//   shows the old one.
//
//   Cost. Video memory is across the PCI bus. Uncached writes to it are an
//   order of magnitude slower than writes to RAM, and at 1024x768x32 a full
//   screen is 3 MB. Doing that every frame is 3 MB of the slowest writes in
//   the machine.
//
// So there are three buffers in play:
//
//   back buffer    plain RAM. All drawing goes here. Fast, cached, never seen.
//   VRAM page 0    what the display may be scanning out.
//   VRAM page 1    the other one.
//
// Presenting a frame copies only the parts of the back buffer that changed
// into the VRAM page that is NOT on screen, then tells the GPU to scan out
// from that page instead. The flip itself is one register write - see
// bga_set_display_offset().
//
// DIRTY RECTANGLES
// ----------------
// Tracking what changed is where the real win is. A desktop that redraws a
// blinking cursor and a clock touches maybe 2 KB of pixels; copying 3 MB for
// that is 1500x more work than needed. fb_mark_dirty() records changed regions
// and fb_present() copies only those.
//
// One subtlety that is easy to get wrong: with two VRAM pages, each page is
// behind by a different amount, because they are written on alternate frames.
// What a page needs is not "what changed this frame" but "everything that has
// changed since this page was last written".
//
// So dirty rectangles are tracked PER PAGE. Marking a region dirty adds it to
// both pages' lists; presenting to a page applies and clears only that page's
// list. Tracking a single global list instead produces flicker: a full-screen
// update lands in one page and the other never receives it, so the display
// alternates between the current frame and a two-frame-old one.
//
// COORDINATES
// -----------
// Origin top-left, x right, y down. Colours are 0x00RRGGBB.

#ifndef DRIVERS_VIDEO_FRAMEBUFFER_H
#define DRIVERS_VIDEO_FRAMEBUFFER_H

#include <stdint.h>

// Maximum dirty rectangles tracked per frame. Past this we give up on
// fine-grained tracking and mark the whole screen - still correct, just
// slower, which is the right way for an optimisation to degrade.
#define FB_MAX_DIRTY_RECTS 64

// ---------------------------------------------------------------------------
// Colours
// ---------------------------------------------------------------------------

typedef uint32_t fb_color_t;

#define FB_RGB(r, g, b) \
    ((fb_color_t)(((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b)))

#define FB_BLACK       FB_RGB(0,   0,   0)
#define FB_WHITE       FB_RGB(255, 255, 255)
#define FB_RED         FB_RGB(220, 50,  47)
#define FB_GREEN       FB_RGB(133, 153, 0)
#define FB_BLUE        FB_RGB(38,  139, 210)
#define FB_CYAN        FB_RGB(42,  161, 152)
#define FB_YELLOW      FB_RGB(181, 137, 0)
#define FB_MAGENTA     FB_RGB(211, 54,  130)
#define FB_ORANGE      FB_RGB(203, 75,  22)
#define FB_GREY        FB_RGB(131, 148, 150)
#define FB_DARK_GREY   FB_RGB(60,  66,  70)
#define FB_DARKER_GREY FB_RGB(35,  38,  41)
#define FB_LIGHT_GREY  FB_RGB(190, 195, 198)

typedef struct {
    int32_t x, y;
    int32_t width, height;
} fb_rect_t;

// ---------------------------------------------------------------------------
// Statistics, surfaced by the FPS overlay and the `gfxstat` command
// ---------------------------------------------------------------------------

typedef struct {
    uint64_t frames_presented;
    uint64_t total_pixels_copied;   // how much we actually moved
    uint64_t total_pixels_possible; // how much a full blit would have moved
    uint32_t last_frame_us;         // microseconds for the last present()
    uint32_t average_frame_us;      // rolling average
    uint32_t min_frame_us;
    uint32_t max_frame_us;
    uint32_t last_dirty_rects;
    int      hardware_flip;         // page flipping in the GPU, no copy to scan out
    int      sse_blitter;           // 16-byte-wide copies in use
} fb_stats_t;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

// Brings up graphics: sets the mode through the GPU, maps the framebuffer,
// allocates the RAM back buffer. Returns 1 on success.
int  fb_init(uint32_t width, uint32_t height);

// Returns to text mode and frees the back buffer.
void fb_shutdown(void);

int  fb_is_active(void);

uint32_t fb_width(void);
uint32_t fb_height(void);

// ---------------------------------------------------------------------------
// Drawing (all of this touches the back buffer only)
// ---------------------------------------------------------------------------

void fb_clear(fb_color_t color);
void fb_pixel(int32_t x, int32_t y, fb_color_t color);
void fb_fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, fb_color_t color);
void fb_draw_rect(int32_t x, int32_t y, int32_t w, int32_t h, fb_color_t color);
void fb_hline(int32_t x, int32_t y, int32_t length, fb_color_t color);
void fb_vline(int32_t x, int32_t y, int32_t length, fb_color_t color);

// Vertical gradient, used for the wallpaper and window title bars.
void fb_fill_gradient(int32_t x, int32_t y, int32_t w, int32_t h,
                      fb_color_t top, fb_color_t bottom);

// Text, using the font extracted from VGA ROM. `bg` of FB_TRANSPARENT leaves
// the existing pixels alone behind the glyph.
#define FB_TRANSPARENT 0xFF000000u
void fb_draw_char(int32_t x, int32_t y, char c, fb_color_t fg, fb_color_t bg);
void fb_draw_string(int32_t x, int32_t y, const char* text,
                    fb_color_t fg, fb_color_t bg);
void fb_printf(int32_t x, int32_t y, fb_color_t fg, fb_color_t bg,
               const char* fmt, ...) __attribute__((format(printf, 5, 6)));

// Copy a rectangle of 32bpp pixels in. `src_pitch` is in pixels.
void fb_blit(int32_t x, int32_t y, int32_t w, int32_t h,
             const uint32_t* src, int32_t src_pitch);

// Same, but skips pixels equal to `key`. Used for the mouse cursor.
void fb_blit_masked(int32_t x, int32_t y, int32_t w, int32_t h,
                    const uint32_t* src, int32_t src_pitch, uint32_t key);

// Read a rectangle of the back buffer out into `dst` (w*h pixels, tightly
// packed). Ignores the clip rectangle on purpose.
//
// This is what makes "save under" possible: the mouse cursor has to be drawn
// into the shared back buffer, but it is not part of any window's content, so
// the pixels it covers must be preserved and put back before the next frame
// paints. Without that the cursor is permanent and every frame leaves another
// copy of it behind.
void fb_read_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t* dst);

// ---------------------------------------------------------------------------
// Clipping
//
// Set a clip rectangle and all drawing is confined to it. The window manager
// uses this so a window can draw its contents without spilling over its edges.
// ---------------------------------------------------------------------------

void fb_set_clip(int32_t x, int32_t y, int32_t w, int32_t h);
void fb_reset_clip(void);

// ---------------------------------------------------------------------------
// Presentation
// ---------------------------------------------------------------------------

// Record that a region changed and needs pushing to the screen.
void fb_mark_dirty(int32_t x, int32_t y, int32_t w, int32_t h);
void fb_mark_all_dirty(void);

// Push the dirty regions to video memory and flip. This is the only function
// that touches VRAM.
void fb_present(void);

const fb_stats_t* fb_get_stats(void);
void              fb_reset_stats(void);

// Draw the FPS / frame-time overlay in the corner. Called by the compositor
// when enabled.
void fb_draw_stats_overlay(void);
void fb_set_stats_overlay(int enabled);
int  fb_get_stats_overlay(void);

#endif // DRIVERS_VIDEO_FRAMEBUFFER_H
