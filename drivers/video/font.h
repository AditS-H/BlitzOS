// font.h - 8x16 bitmap font, lifted out of the VGA adapter's own ROM.
//
// HOW WE GOT A FONT WITHOUT SHIPPING ONE
// --------------------------------------
// Drawing text on a linear framebuffer needs glyph bitmaps. The obvious
// approach is to embed a font as a C array, which costs 4 KB of kernel image
// and means somebody has to produce the bitmaps in the first place.
//
// There is a better source: the VGA adapter already has one. In text mode the
// character generator reads glyphs out of plane 2 of video memory, where the
// BIOS loaded an 8x16 font at boot. If we read that plane before switching to
// graphics mode, we get the authentic IBM VGA font for free.
//
// Getting at it means temporarily reprogramming the sequencer and graphics
// controller to expose plane 2 linearly at 0xA0000, because normally the four
// planes are interleaved and 0xA0000 is chained differently in text mode. The
// procedure is:
//
//   1. Save the sequencer and graphics controller registers we are about to
//      change (a text-mode console still has to work afterwards).
//   2. Sequencer 0x02 = 0x04   select plane 2 for writes
//      Sequencer 0x04 = 0x07   sequential addressing, extended memory
//      GC 0x04 = 0x02          read from plane 2
//      GC 0x05 = 0x00          disable odd/even mode
//      GC 0x06 = 0x04          map VRAM at 0xA0000, no chaining
//   3. Copy the glyphs. VGA stores each in a 32-byte slot even for an 8x16
//      font, so glyph N starts at 0xA0000 + N*32 and only the first 16 bytes
//      are used.
//   4. Restore every register we touched.
//
// This must run while still in text mode, which is exactly where the kernel
// boots, so font_init() is called during early startup and the result is kept
// for later when the desktop switches to graphics.
//
// Each glyph is 16 bytes, one per scanline, MSB = leftmost pixel.

#ifndef DRIVERS_VIDEO_FONT_H
#define DRIVERS_VIDEO_FONT_H

#include <stdint.h>

#define FONT_WIDTH   8
#define FONT_HEIGHT  16
#define FONT_GLYPHS  256
#define FONT_BYTES_PER_GLYPH FONT_HEIGHT

// Reads the font out of VGA plane 2. Must be called while the adapter is still
// in text mode. Returns 1 if the extracted font looks usable.
int font_init(void);

// Bitmap for one character: FONT_HEIGHT bytes, one per scanline.
const uint8_t* font_glyph(char c);

// True if font_init() succeeded and glyphs are real.
int font_is_available(void);

#endif // DRIVERS_VIDEO_FONT_H
