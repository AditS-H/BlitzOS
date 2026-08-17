// bga.h - Direct mode-setting on the Bochs Graphics Adapter.
//
// WHAT THIS ACTUALLY IS
// ---------------------
// This is real GPU register programming, but it is worth being precise about
// which GPU. The Bochs Graphics Adapter (BGA), also called the Bochs VBE
// Extensions, is the display device emulated by QEMU (`-vga std`), Bochs and
// VirtualBox. It is a genuine PCI display controller with a documented
// register interface, and this file drives it directly - no BIOS, no VBE real
// mode calls, no bootloader help.
//
// What this is NOT is a driver for a physical NVIDIA, AMD or Intel GPU. Those
// have undocumented command submission rings, per-generation register layouts
// and signed firmware; a usable driver for one is years of work and, for the
// proprietary parts, requires reverse engineering. Anyone claiming a hobby
// kernel "controls the GPU" is almost always describing exactly this: a
// standard VBE-style adapter. Being accurate about that is more impressive
// than overclaiming.
//
// WHY IT MATTERS ANYWAY
// ---------------------
// Two things fall out of talking to the hardware directly instead of asking
// the bootloader for a framebuffer:
//
//   1. Mode setting at runtime. The kernel can boot in VGA text mode, then
//      switch to 1024x768x32 when the user asks for the desktop. A framebuffer
//      requested through the multiboot2 header is fixed before the kernel even
//      starts, so text mode would be gone forever.
//
//   2. Hardware page flipping. The adapter can scan out from anywhere in its
//      video memory. Allocate a virtual framebuffer twice the height, draw into
//      the half that is not being displayed, then write the Y offset register
//      to swap. The "copy" becomes a single register write instead of moving
//      3 MB per frame. See bga_set_display_offset().
//
// REGISTER INTERFACE
// ------------------
// Two 16-bit I/O ports, index/data style:
//
//   0x01CE  VBE_DISPI_IOPORT_INDEX - which register you want
//   0x01CF  VBE_DISPI_IOPORT_DATA  - read or write it
//
// Mode changes must happen with the adapter disabled, otherwise the hardware
// may latch a half-configured mode.

#ifndef DRIVERS_VIDEO_BGA_H
#define DRIVERS_VIDEO_BGA_H

#include <stdint.h>

#define VBE_DISPI_IOPORT_INDEX 0x01CE
#define VBE_DISPI_IOPORT_DATA  0x01CF

// Register indices
#define VBE_DISPI_INDEX_ID          0x0
#define VBE_DISPI_INDEX_XRES        0x1
#define VBE_DISPI_INDEX_YRES        0x2
#define VBE_DISPI_INDEX_BPP         0x3
#define VBE_DISPI_INDEX_ENABLE      0x4
#define VBE_DISPI_INDEX_BANK        0x5
#define VBE_DISPI_INDEX_VIRT_WIDTH  0x6
#define VBE_DISPI_INDEX_VIRT_HEIGHT 0x7
#define VBE_DISPI_INDEX_X_OFFSET    0x8
#define VBE_DISPI_INDEX_Y_OFFSET    0x9
#define VBE_DISPI_INDEX_VIDEO_MEMORY_64K 0xA

// Version IDs. Anything from 0xB0C2 up supports the linear framebuffer;
// 0xB0C4 and later support the virtual-resolution registers we need for page
// flipping.
#define VBE_DISPI_ID0 0xB0C0
#define VBE_DISPI_ID2 0xB0C2
#define VBE_DISPI_ID4 0xB0C4
#define VBE_DISPI_ID5 0xB0C5

// ENABLE register bits
#define VBE_DISPI_DISABLED     0x00
#define VBE_DISPI_ENABLED      0x01
#define VBE_DISPI_GETCAPS      0x02
#define VBE_DISPI_8BIT_DAC     0x20
#define VBE_DISPI_LFB_ENABLED  0x40
#define VBE_DISPI_NOCLEARMEM   0x80

// PCI IDs for the adapters that speak this interface.
#define BGA_VENDOR_QEMU      0x1234
#define BGA_DEVICE_QEMU_VGA  0x1111
#define BGA_VENDOR_VBOX      0x80EE
#define BGA_DEVICE_VBOX_VGA  0xBEEF

typedef struct {
    int      present;          // did we find a BGA-compatible adapter?
    uint16_t version;          // VBE_DISPI_ID* value reported by the hardware
    uint64_t framebuffer_phys; // BAR0 of the display controller
    uint64_t vram_bytes;       // usable video memory, from the 64K-units register

    uint32_t width;            // current visible mode
    uint32_t height;
    uint32_t bpp;
    uint32_t virtual_height;   // >= height when double buffering in VRAM
} bga_info_t;

// Probes for the adapter. Safe to call even when none is present; check
// `.present` on the result. Requires pci_init() to have run.
int bga_detect(void);

const bga_info_t* bga_get_info(void);

// Sets a mode. `virtual_height_multiplier` of 2 reserves twice the visible
// height in video memory so bga_set_display_offset() can page flip.
// Returns 1 on success.
int bga_set_mode(uint32_t width, uint32_t height, uint32_t bpp,
                 uint32_t virtual_height_multiplier);

// Moves the scanout origin. This is the page flip: with a doubled virtual
// height, passing 0 or `height` swaps which half of video memory is on screen.
// Costs one 16-bit port write regardless of resolution.
void bga_set_display_offset(uint32_t y_offset);

// Returns to VGA-compatible text mode.
void bga_disable(void);

int bga_is_enabled(void);

#endif // DRIVERS_VIDEO_BGA_H
