#include "font.h"
#include "../../kernel/arch/x86_64/interrupts.h"
#include "../../kernel/lib/kprintf.h"
#include "../../kernel/lib/string.h"

// VGA register ports
#define VGA_SEQ_INDEX 0x3C4
#define VGA_SEQ_DATA  0x3C5
#define VGA_GC_INDEX  0x3CE
#define VGA_GC_DATA   0x3CF

// Where video memory appears once we ask for unchained plane access.
#define VGA_PLANE_WINDOW 0xA0000

// VGA reserves 32 bytes per glyph even though an 8x16 font only uses 16.
#define VGA_GLYPH_STRIDE 32

static uint8_t font_data[FONT_GLYPHS * FONT_BYTES_PER_GLYPH];
static int     font_available = 0;

static uint8_t seq_read(uint8_t index)
{
    outb(VGA_SEQ_INDEX, index);
    return inb(VGA_SEQ_DATA);
}

static void seq_write(uint8_t index, uint8_t value)
{
    outb(VGA_SEQ_INDEX, index);
    outb(VGA_SEQ_DATA, value);
}

static uint8_t gc_read(uint8_t index)
{
    outb(VGA_GC_INDEX, index);
    return inb(VGA_GC_DATA);
}

static void gc_write(uint8_t index, uint8_t value)
{
    outb(VGA_GC_INDEX, index);
    outb(VGA_GC_DATA, value);
}

int font_init(void)
{
    // ---- Save every register we are about to change ----
    // The console keeps running in text mode after this, so leaving the
    // adapter reprogrammed would corrupt the display.
    uint8_t saved_seq2 = seq_read(0x02);
    uint8_t saved_seq4 = seq_read(0x04);
    uint8_t saved_gc4  = gc_read(0x04);
    uint8_t saved_gc5  = gc_read(0x05);
    uint8_t saved_gc6  = gc_read(0x06);

    // ---- Expose plane 2 linearly at 0xA0000 ----
    seq_write(0x02, 0x04);   // map mask: plane 2 only
    seq_write(0x04, 0x07);   // sequential addressing, extended memory
    gc_write(0x04, 0x02);    // read map select: plane 2
    gc_write(0x05, 0x00);    // graphics mode: no odd/even, no shift
    gc_write(0x06, 0x04);    // misc: VRAM at 0xA0000, chain off

    // ---- Copy the glyphs out ----
    const volatile uint8_t* vram = (const volatile uint8_t*)VGA_PLANE_WINDOW;

    for (int glyph = 0; glyph < FONT_GLYPHS; glyph++) {
        for (int row = 0; row < FONT_HEIGHT; row++) {
            font_data[glyph * FONT_BYTES_PER_GLYPH + row] =
                vram[glyph * VGA_GLYPH_STRIDE + row];
        }
    }

    // ---- Put the adapter back exactly as we found it ----
    seq_write(0x02, saved_seq2);
    seq_write(0x04, saved_seq4);
    gc_write(0x04, saved_gc4);
    gc_write(0x05, saved_gc5);
    gc_write(0x06, saved_gc6);

    // ---- Sanity check ----
    // If the BIOS never loaded a font, or the plane switch did not take, we
    // would silently render every character as a blank box. Check a couple of
    // glyphs that must have ink in them.
    int ink = 0;
    for (int row = 0; row < FONT_HEIGHT; row++) {
        ink |= font_data['A' * FONT_BYTES_PER_GLYPH + row];
        ink |= font_data['m' * FONT_BYTES_PER_GLYPH + row];
    }

    if (ink == 0) {
        kwarn("font: VGA ROM font came back blank - text rendering disabled\n");
        font_available = 0;
        return 0;
    }

    // Space must be blank; if it is not, we probably read the wrong plane.
    int space_ink = 0;
    for (int row = 0; row < FONT_HEIGHT; row++) {
        space_ink |= font_data[' ' * FONT_BYTES_PER_GLYPH + row];
    }
    if (space_ink != 0) {
        kwarn("font: glyph for space is not blank - plane select may be wrong\n");
    }

    font_available = 1;
    kok("font: 8x16 VGA ROM font extracted (%u bytes, %u glyphs)\n",
        (uint32_t)sizeof(font_data), (uint32_t)FONT_GLYPHS);

    return 1;
}

const uint8_t* font_glyph(char c)
{
    return &font_data[(uint8_t)c * FONT_BYTES_PER_GLYPH];
}

int font_is_available(void)
{
    return font_available;
}
