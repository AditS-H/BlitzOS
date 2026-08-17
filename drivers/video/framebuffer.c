#include "framebuffer.h"
#include "bga.h"
#include "font.h"
#include "../pci/pci.h"
#include "../../kernel/arch/x86_64/interrupts.h"
#include "../../kernel/arch/x86_64/tsc.h"
#include "../../kernel/arch/x86_64/sse.h"
#include "../../kernel/lib/kprintf.h"
#include "../../kernel/lib/string.h"
#include "../../kernel/mm/pmm.h"

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

static struct {
    int       active;

    uint32_t* vram;           // mapped video memory (page 0 starts here)
    uint32_t* back;           // RAM back buffer - everything draws here

    uint32_t  width;
    uint32_t  height;
    uint32_t  pitch;          // pixels per row (may exceed width)
    uint32_t  pixels;         // width * height

    int       hardware_flip;  // GPU can scan out from either of two pages
    uint32_t  current_page;   // which VRAM page is on screen

    uint64_t  back_buffer_pages;  // for pmm_free_pages on shutdown
} fb;

// Clip rectangle. All drawing is confined to this.
static fb_rect_t clip;

// Dirty rectangles, tracked SEPARATELY FOR EACH VRAM PAGE.
//
// This is the whole ballgame for double buffering, and getting it wrong causes
// a very specific, very visible bug.
//
// The two pages are written on alternate frames, so each one is behind by a
// different amount. What page P needs is not "what changed this frame" but
// "everything that has changed since P was last written". Those are not the
// same set.
//
// The first version tracked one global list plus the previous frame's list.
// That handles the steady state, but breaks the moment a full-screen update
// happens: frame N does a full blit into page B and clears the history, frame
// N+1 copies only a small rectangle into page A - and page A never received
// the full-screen update at all. The display then alternates between the
// correct frame and a two-frame-old one, which reads as heavy flicker.
//
// Per-page accounting makes that impossible by construction. Marking a region
// dirty adds it to BOTH pages' lists; presenting to page P applies and clears
// only P's list. A page cannot be missing an update, because the only thing
// that clears its list is writing to it.
static fb_rect_t page_dirty[2][FB_MAX_DIRTY_RECTS];
static uint32_t  page_dirty_count[2];
static int       page_needs_full[2];

static fb_stats_t stats;
static int        stats_overlay_enabled = 1;

// ---------------------------------------------------------------------------
// Pixel copying
//
// The inner loop of the whole graphics stack. Everything else is bookkeeping
// to call this as little as possible.
// ---------------------------------------------------------------------------

static void copy_pixels(uint32_t* dst, const uint32_t* src, uint32_t count)
{
    // SSE2 path: 16 bytes per instruction instead of 8, and the non-temporal
    // store variant avoids polluting cache with pixels we will never read back.
    if (sse_is_enabled()) {
        sse_copy_pixels(dst, src, count);
        return;
    }

    // Fallback: 8 bytes at a time via the string move unit. On any CPU with
    // ERMSB this is close to memcpy speed and needs no SSE state at all.
    uint32_t qwords = count / 2;
    if (qwords) {
        __asm__ volatile("rep movsq"
                         : "+D"(dst), "+S"(src), "+c"(qwords)
                         :
                         : "memory");
    }
    if (count & 1) {
        *dst = *src;
    }
}

// ---------------------------------------------------------------------------
// Rectangle helpers
// ---------------------------------------------------------------------------

static int rect_intersect(const fb_rect_t* a, const fb_rect_t* b, fb_rect_t* out)
{
    int32_t x0 = a->x > b->x ? a->x : b->x;
    int32_t y0 = a->y > b->y ? a->y : b->y;
    int32_t x1 = (a->x + a->width)  < (b->x + b->width)  ? (a->x + a->width)  : (b->x + b->width);
    int32_t y1 = (a->y + a->height) < (b->y + b->height) ? (a->y + a->height) : (b->y + b->height);

    if (x1 <= x0 || y1 <= y0) {
        return 0;
    }

    out->x      = x0;
    out->y      = y0;
    out->width  = x1 - x0;
    out->height = y1 - y0;
    return 1;
}

static int rects_overlap_or_touch(const fb_rect_t* a, const fb_rect_t* b)
{
    return !(a->x + a->width  < b->x || b->x + b->width  < a->x ||
             a->y + a->height < b->y || b->y + b->height < a->y);
}

static void rect_union(fb_rect_t* a, const fb_rect_t* b)
{
    int32_t x0 = a->x < b->x ? a->x : b->x;
    int32_t y0 = a->y < b->y ? a->y : b->y;
    int32_t x1 = (a->x + a->width)  > (b->x + b->width)  ? (a->x + a->width)  : (b->x + b->width);
    int32_t y1 = (a->y + a->height) > (b->y + b->height) ? (a->y + a->height) : (b->y + b->height);

    a->x      = x0;
    a->y      = y0;
    a->width  = x1 - x0;
    a->height = y1 - y0;
}

// ---------------------------------------------------------------------------
// Dirty tracking
// ---------------------------------------------------------------------------

void fb_mark_all_dirty(void)
{
    // Both pages are stale, not just the one we are about to draw into.
    page_needs_full[0] = 1;
    page_needs_full[1] = 1;
    page_dirty_count[0] = 0;
    page_dirty_count[1] = 0;
}

// Adds a rectangle to one page's list, merging with an existing entry if they
// overlap or touch.
static void add_dirty_to_page(uint32_t page, const fb_rect_t* incoming)
{
    if (page_needs_full[page]) {
        return;   // the whole page is already going to be rewritten
    }

    // Two adjacent 8x16 characters become one 16x16 rectangle rather than two
    // entries, which keeps the list short and the per-rectangle overhead down.
    for (uint32_t i = 0; i < page_dirty_count[page]; i++) {
        if (rects_overlap_or_touch(&page_dirty[page][i], incoming)) {
            rect_union(&page_dirty[page][i], incoming);
            return;
        }
    }

    if (page_dirty_count[page] >= FB_MAX_DIRTY_RECTS) {
        // Out of slots. Falling back to a full-page update is slower but always
        // correct - the right way for an optimisation to fail.
        page_needs_full[page]  = 1;
        page_dirty_count[page] = 0;
        return;
    }

    page_dirty[page][page_dirty_count[page]++] = *incoming;
}

void fb_mark_dirty(int32_t x, int32_t y, int32_t w, int32_t h)
{
    if (!fb.active || w <= 0 || h <= 0) {
        return;
    }

    // Clamp to the screen; a rectangle hanging off the edge would make the
    // present loop copy from outside the back buffer.
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int32_t)fb.width)  w = (int32_t)fb.width  - x;
    if (y + h > (int32_t)fb.height) h = (int32_t)fb.height - y;
    if (w <= 0 || h <= 0) {
        return;
    }

    fb_rect_t incoming = { x, y, w, h };

    // Both pages need it. Whichever we present to next will apply and clear
    // its own copy; the other keeps waiting until its turn.
    add_dirty_to_page(0, &incoming);
    add_dirty_to_page(1, &incoming);
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

int fb_init(uint32_t width, uint32_t height)
{
    if (fb.active) {
        return 1;
    }

    memset(&fb, 0, sizeof(fb));
    memset(&stats, 0, sizeof(stats));
    stats.min_frame_us = 0xFFFFFFFFu;

    if (!bga_detect()) {
        kerror("framebuffer: no supported display adapter\n");
        return 0;
    }

    // Ask for twice the visible height so video memory holds two full pages.
    // That is what makes the flip a register write instead of a 3 MB copy.
    if (!bga_set_mode(width, height, 32, 2)) {
        kerror("framebuffer: mode set failed\n");
        return 0;
    }

    const bga_info_t* gpu = bga_get_info();

    fb.width  = gpu->width;
    fb.height = gpu->height;
    fb.pitch  = gpu->width;
    fb.pixels = fb.width * fb.height;

    // The framebuffer BAR points into the PCI MMIO hole below 4 GB, which
    // boot.asm identity maps. No page table work needed here.
    fb.vram = (uint32_t*)gpu->framebuffer_phys;

    fb.hardware_flip = (gpu->virtual_height >= gpu->height * 2);
    fb.current_page  = 0;

    // Back buffer in normal RAM. Taken straight from the page allocator rather
    // than kmalloc: 3 MB through a first-fit heap designed for small objects
    // would be wasteful, and we want it page aligned anyway.
    uint64_t bytes = (uint64_t)fb.pixels * 4;
    fb.back_buffer_pages = (bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    fb.back = (uint32_t*)pmm_alloc_pages(fb.back_buffer_pages);

    if (!fb.back) {
        kerror("framebuffer: could not allocate a %lu KB back buffer\n",
               bytes / 1024);
        bga_disable();
        return 0;
    }

    memset(fb.back, 0, bytes);

    fb.active = 1;
    fb_reset_clip();
    fb_mark_all_dirty();

    stats.hardware_flip = fb.hardware_flip;
    stats.sse_blitter   = sse_is_enabled();

    kok("framebuffer: %ux%u 32bpp, VRAM at %p, back buffer %lu KB\n",
        fb.width, fb.height, fb.vram, bytes / 1024);
    kinfo("framebuffer: %s, %s blitter\n",
          fb.hardware_flip ? "hardware page flipping"
                           : "software present (single VRAM page)",
          sse_is_enabled() ? "SSE2 16-byte" : "64-bit string");

    return 1;
}

void fb_shutdown(void)
{
    if (!fb.active) {
        return;
    }

    bga_disable();

    if (fb.back) {
        pmm_free_pages(fb.back, fb.back_buffer_pages);
    }

    fb.active = 0;
    fb.back   = NULL;
    fb.vram   = NULL;
}

int      fb_is_active(void) { return fb.active; }
uint32_t fb_width(void)     { return fb.width; }
uint32_t fb_height(void)    { return fb.height; }

// ---------------------------------------------------------------------------
// Clipping
// ---------------------------------------------------------------------------

void fb_set_clip(int32_t x, int32_t y, int32_t w, int32_t h)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int32_t)fb.width)  w = (int32_t)fb.width  - x;
    if (y + h > (int32_t)fb.height) h = (int32_t)fb.height - y;
    if (w < 0) w = 0;
    if (h < 0) h = 0;

    clip.x = x; clip.y = y; clip.width = w; clip.height = h;
}

void fb_reset_clip(void)
{
    clip.x = 0;
    clip.y = 0;
    clip.width  = (int32_t)fb.width;
    clip.height = (int32_t)fb.height;
}

// ---------------------------------------------------------------------------
// Drawing primitives
// ---------------------------------------------------------------------------

void fb_pixel(int32_t x, int32_t y, fb_color_t color)
{
    if (!fb.active) return;
    if (x < clip.x || y < clip.y ||
        x >= clip.x + clip.width || y >= clip.y + clip.height) {
        return;
    }
    fb.back[(uint32_t)y * fb.pitch + (uint32_t)x] = color;
}

void fb_fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, fb_color_t color)
{
    if (!fb.active) return;

    fb_rect_t requested = { x, y, w, h };
    fb_rect_t area;
    if (!rect_intersect(&requested, &clip, &area)) {
        return;
    }

    for (int32_t row = 0; row < area.height; row++) {
        uint32_t* line = fb.back + (uint32_t)(area.y + row) * fb.pitch + area.x;

        // A 32-bit fill has no rep-stos equivalent that beats a simple loop
        // once the compiler unrolls it, and every pixel is the same value so
        // there is nothing to load.
        for (int32_t col = 0; col < area.width; col++) {
            line[col] = color;
        }
    }
}

void fb_draw_rect(int32_t x, int32_t y, int32_t w, int32_t h, fb_color_t color)
{
    fb_hline(x, y, w, color);
    fb_hline(x, y + h - 1, w, color);
    fb_vline(x, y, h, color);
    fb_vline(x + w - 1, y, h, color);
}

void fb_hline(int32_t x, int32_t y, int32_t length, fb_color_t color)
{
    fb_fill_rect(x, y, length, 1, color);
}

void fb_vline(int32_t x, int32_t y, int32_t length, fb_color_t color)
{
    fb_fill_rect(x, y, 1, length, color);
}

void fb_fill_gradient(int32_t x, int32_t y, int32_t w, int32_t h,
                      fb_color_t top, fb_color_t bottom)
{
    if (h <= 0) return;

    int32_t r0 = (int32_t)((top >> 16) & 0xFF);
    int32_t g0 = (int32_t)((top >> 8)  & 0xFF);
    int32_t b0 = (int32_t)( top        & 0xFF);
    int32_t r1 = (int32_t)((bottom >> 16) & 0xFF);
    int32_t g1 = (int32_t)((bottom >> 8)  & 0xFF);
    int32_t b1 = (int32_t)( bottom        & 0xFF);

    for (int32_t row = 0; row < h; row++) {
        // Integer interpolation; no floating point in the kernel, and none
        // needed for 8-bit channels.
        int32_t r = r0 + (r1 - r0) * row / h;
        int32_t g = g0 + (g1 - g0) * row / h;
        int32_t b = b0 + (b1 - b0) * row / h;

        fb_hline(x, y + row, w, FB_RGB(r, g, b));
    }
}

// ---------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------

void fb_draw_char(int32_t x, int32_t y, char c, fb_color_t fg, fb_color_t bg)
{
    if (!fb.active || !font_is_available()) {
        return;
    }

    const uint8_t* glyph = font_glyph(c);

    for (int32_t row = 0; row < FONT_HEIGHT; row++) {
        int32_t py = y + row;
        if (py < clip.y || py >= clip.y + clip.height) {
            continue;
        }

        uint8_t   bits = glyph[row];
        uint32_t* line = fb.back + (uint32_t)py * fb.pitch;

        for (int32_t col = 0; col < FONT_WIDTH; col++) {
            int32_t px = x + col;
            if (px < clip.x || px >= clip.x + clip.width) {
                continue;
            }

            // MSB is the leftmost pixel of the glyph.
            if (bits & (0x80 >> col)) {
                line[px] = fg;
            } else if (bg != FB_TRANSPARENT) {
                line[px] = bg;
            }
        }
    }
}

void fb_draw_string(int32_t x, int32_t y, const char* text,
                    fb_color_t fg, fb_color_t bg)
{
    if (!text) return;

    int32_t cursor_x = x;
    int32_t cursor_y = y;

    for (const char* p = text; *p; p++) {
        if (*p == '\n') {
            cursor_x = x;
            cursor_y += FONT_HEIGHT;
            continue;
        }
        fb_draw_char(cursor_x, cursor_y, *p, fg, bg);
        cursor_x += FONT_WIDTH;
    }
}

void fb_printf(int32_t x, int32_t y, fb_color_t fg, fb_color_t bg,
               const char* fmt, ...)
{
    char buffer[256];

    va_list args;
    va_start(args, fmt);
    kvsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    fb_draw_string(x, y, buffer, fg, bg);
}

// ---------------------------------------------------------------------------
// Blitting
// ---------------------------------------------------------------------------

void fb_blit(int32_t x, int32_t y, int32_t w, int32_t h,
             const uint32_t* src, int32_t src_pitch)
{
    if (!fb.active || !src) return;

    fb_rect_t requested = { x, y, w, h };
    fb_rect_t area;
    if (!rect_intersect(&requested, &clip, &area)) {
        return;
    }

    // Offset into the source to match whatever the clip trimmed off.
    int32_t src_x = area.x - x;
    int32_t src_y = area.y - y;

    for (int32_t row = 0; row < area.height; row++) {
        copy_pixels(fb.back + (uint32_t)(area.y + row) * fb.pitch + area.x,
                    src + (uint32_t)(src_y + row) * src_pitch + src_x,
                    (uint32_t)area.width);
    }
}

void fb_blit_masked(int32_t x, int32_t y, int32_t w, int32_t h,
                    const uint32_t* src, int32_t src_pitch, uint32_t key)
{
    if (!fb.active || !src) return;

    fb_rect_t requested = { x, y, w, h };
    fb_rect_t area;
    if (!rect_intersect(&requested, &clip, &area)) {
        return;
    }

    int32_t src_x = area.x - x;
    int32_t src_y = area.y - y;

    for (int32_t row = 0; row < area.height; row++) {
        const uint32_t* s = src + (uint32_t)(src_y + row) * src_pitch + src_x;
        uint32_t*       d = fb.back + (uint32_t)(area.y + row) * fb.pitch + area.x;

        for (int32_t col = 0; col < area.width; col++) {
            if (s[col] != key) {
                d[col] = s[col];
            }
        }
    }
}

void fb_read_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t* dst)
{
    if (!fb.active || !dst || w <= 0 || h <= 0) {
        return;
    }

    // Deliberately ignores the clip rectangle: this reads raw back-buffer
    // content to be put back later, and clipping it would restore the wrong
    // pixels.
    for (int32_t row = 0; row < h; row++) {
        int32_t py = y + row;
        if (py < 0 || py >= (int32_t)fb.height) {
            continue;
        }
        for (int32_t col = 0; col < w; col++) {
            int32_t px = x + col;
            dst[row * w + col] = (px >= 0 && px < (int32_t)fb.width)
                                 ? fb.back[(uint32_t)py * fb.pitch + (uint32_t)px]
                                 : 0;
        }
    }
}

void fb_clear(fb_color_t color)
{
    if (!fb.active) return;

    fb_rect_t saved = clip;
    fb_reset_clip();
    fb_fill_rect(0, 0, (int32_t)fb.width, (int32_t)fb.height, color);
    clip = saved;

    fb_mark_all_dirty();
}

// ---------------------------------------------------------------------------
// Presentation
// ---------------------------------------------------------------------------

// Copies one rectangle from the back buffer into a VRAM page.
static void push_rect_to_vram(const fb_rect_t* rect, uint32_t page)
{
    uint32_t page_offset = page * fb.height * fb.pitch;

    for (int32_t row = 0; row < rect->height; row++) {
        uint32_t line = (uint32_t)(rect->y + row);

        copy_pixels(fb.vram + page_offset + line * fb.pitch + rect->x,
                    fb.back + line * fb.pitch + rect->x,
                    (uint32_t)rect->width);
    }

    stats.total_pixels_copied += (uint64_t)rect->width * rect->height;
}

void fb_present(void)
{
    if (!fb.active) {
        return;
    }

    uint64_t start_us = tsc_us();

    // With hardware page flipping we draw into the page that is NOT being
    // scanned out, then swap. Without it there is only one page and we have to
    // write to the live one, accepting some tearing.
    uint32_t target_page = fb.hardware_flip ? (fb.current_page ^ 1) : 0;

    // Apply exactly what this page is missing, then clear its list. Nothing
    // else touches it, so it cannot fall behind.
    uint32_t rects_copied;

    if (page_needs_full[target_page]) {
        fb_rect_t whole = { 0, 0, (int32_t)fb.width, (int32_t)fb.height };
        push_rect_to_vram(&whole, target_page);
        rects_copied = 1;
    } else {
        for (uint32_t i = 0; i < page_dirty_count[target_page]; i++) {
            push_rect_to_vram(&page_dirty[target_page][i], target_page);
        }
        rects_copied = page_dirty_count[target_page];
    }

    page_needs_full[target_page]  = 0;
    page_dirty_count[target_page] = 0;

    // The flip. One 16-bit port write moves the scanout origin; no pixels
    // move at all.
    if (fb.hardware_flip) {
        bga_set_display_offset(target_page * fb.height);
        fb.current_page = target_page;
    }

    // ---- Statistics ----
    uint32_t elapsed = (uint32_t)tsc_us_since(start_us);

    stats.frames_presented++;
    stats.total_pixels_possible += fb.pixels;
    stats.last_frame_us          = elapsed;
    stats.last_dirty_rects       = rects_copied;

    if (elapsed < stats.min_frame_us) stats.min_frame_us = elapsed;
    if (elapsed > stats.max_frame_us) stats.max_frame_us = elapsed;

    // Exponential moving average, shift-based so there is no division in the
    // hot path. Weight 1/8 on the newest sample.
    if (stats.average_frame_us == 0) {
        stats.average_frame_us = elapsed;
    } else {
        stats.average_frame_us =
            stats.average_frame_us - (stats.average_frame_us >> 3) + (elapsed >> 3);
    }
}

const fb_stats_t* fb_get_stats(void)
{
    return &stats;
}

void fb_reset_stats(void)
{
    uint64_t frames = stats.frames_presented;
    memset(&stats, 0, sizeof(stats));
    stats.frames_presented = frames;
    stats.min_frame_us     = 0xFFFFFFFFu;
    stats.hardware_flip    = fb.hardware_flip;
    stats.sse_blitter      = sse_is_enabled();
}

void fb_set_stats_overlay(int enabled) { stats_overlay_enabled = enabled; }
int  fb_get_stats_overlay(void)        { return stats_overlay_enabled; }

void fb_draw_stats_overlay(void)
{
    if (!fb.active || !stats_overlay_enabled) {
        return;
    }

    // FPS from the average frame time. This measures how long presenting takes,
    // not how often we present - it is a cost metric, not a refresh rate.
    uint32_t fps = stats.average_frame_us ? 1000000u / stats.average_frame_us : 0;

    // What fraction of a full-screen blit we actually did. This is the number
    // that shows dirty rectangles working.
    uint32_t saved_percent = 0;
    if (stats.total_pixels_possible) {
        saved_percent = (uint32_t)(100 - (stats.total_pixels_copied * 100 /
                                          stats.total_pixels_possible));
    }

    const int32_t w = 210;
    const int32_t h = 4 + FONT_HEIGHT * 3 + 4;
    const int32_t x = (int32_t)fb.width - w - 8;
    const int32_t y = 8;

    fb_rect_t saved_clip = clip;
    fb_reset_clip();

    fb_fill_rect(x, y, w, h, FB_RGB(20, 22, 24));
    fb_draw_rect(x, y, w, h, FB_DARK_GREY);

    fb_printf(x + 6, y + 4, FB_GREEN, FB_TRANSPARENT,
              "present %u us  ~%u fps", stats.average_frame_us, fps);
    fb_printf(x + 6, y + 4 + FONT_HEIGHT, FB_CYAN, FB_TRANSPARENT,
              "min %u  max %u us", stats.min_frame_us, stats.max_frame_us);
    fb_printf(x + 6, y + 4 + FONT_HEIGHT * 2, FB_YELLOW, FB_TRANSPARENT,
              "%u rects  %u%% blit saved", stats.last_dirty_rects, saved_percent);

    clip = saved_clip;
    fb_mark_dirty(x, y, w, h);
}
