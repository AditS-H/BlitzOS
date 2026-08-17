#include "bga.h"
#include "../pci/pci.h"
#include "../../kernel/arch/x86_64/interrupts.h"
#include "../../kernel/lib/kprintf.h"
#include "../../kernel/lib/string.h"

static bga_info_t info;
static int        enabled = 0;

// ---------------------------------------------------------------------------
// Register access
// ---------------------------------------------------------------------------

static void bga_write(uint16_t index, uint16_t value)
{
    outw(VBE_DISPI_IOPORT_INDEX, index);
    outw(VBE_DISPI_IOPORT_DATA, value);
}

static uint16_t bga_read(uint16_t index)
{
    outw(VBE_DISPI_IOPORT_INDEX, index);
    return inw(VBE_DISPI_IOPORT_DATA);
}

// ---------------------------------------------------------------------------
// Detection
// ---------------------------------------------------------------------------

int bga_detect(void)
{
    memset(&info, 0, sizeof(info));

    // The ID register reads back a version between 0xB0C0 and 0xB0C5 on a real
    // BGA. On hardware without one, the ports are unclaimed and read as 0xFFFF.
    uint16_t version = bga_read(VBE_DISPI_INDEX_ID);

    if (version < VBE_DISPI_ID0 || version > VBE_DISPI_ID5) {
        kwarn("BGA: no Bochs/QEMU display adapter (ID register read 0x%x)\n",
              version);
        info.present = 0;
        return 0;
    }

    info.present = 1;
    info.version = version;

    // The framebuffer lives at BAR0 of the display controller. Ask PCI where
    // that is rather than hardcoding 0xE0000000 or 0xFD000000 - the address
    // differs between QEMU versions, VirtualBox and real VBE hardware.
    const pci_device_t* gpu = pci_find_by_class(PCI_CLASS_DISPLAY, 0xFF);

    if (gpu) {
        pci_enable_device(gpu);   // make sure memory decoding is actually on
        info.framebuffer_phys = gpu->bar[0];

        kinfo("GPU: %s %s (%04x:%04x) at %02x:%02x.%u\n",
              pci_vendor_name(gpu->vendor_id),
              pci_class_name(gpu->class_code, gpu->subclass),
              gpu->vendor_id, gpu->device_id,
              gpu->bus, gpu->device, gpu->function);
    }

    if (info.framebuffer_phys == 0) {
        // Last resort: the legacy BGA aperture. Only reached if PCI
        // enumeration found no display controller at all.
        info.framebuffer_phys = 0xE0000000;
        kwarn("BGA: no PCI display device found, assuming LFB at 0x%lx\n",
              info.framebuffer_phys);
    }

    // VRAM size is reported in 64 KB units, but only from ID4 onwards.
    if (version >= VBE_DISPI_ID4) {
        uint16_t units = bga_read(VBE_DISPI_INDEX_VIDEO_MEMORY_64K);
        info.vram_bytes = (uint64_t)units * 64 * 1024;
    }
    if (info.vram_bytes == 0) {
        info.vram_bytes = 16 * 1024 * 1024;   // QEMU's default
    }

    kok("BGA: version 0x%x, LFB at 0x%lx, %lu MB VRAM\n",
        info.version, info.framebuffer_phys, info.vram_bytes / (1024 * 1024));

    return 1;
}

const bga_info_t* bga_get_info(void)
{
    return &info;
}

// ---------------------------------------------------------------------------
// Mode setting
// ---------------------------------------------------------------------------

int bga_set_mode(uint32_t width, uint32_t height, uint32_t bpp,
                 uint32_t virtual_height_multiplier)
{
    if (!info.present) {
        return 0;
    }
    if (bpp != 32 && bpp != 24 && bpp != 16 && bpp != 8) {
        kerror("BGA: unsupported bit depth %u\n", bpp);
        return 0;
    }
    if (virtual_height_multiplier < 1) {
        virtual_height_multiplier = 1;
    }

    uint32_t virtual_height = height * virtual_height_multiplier;

    // Refuse a mode that does not fit in video memory rather than letting the
    // adapter scan out garbage.
    uint64_t required = (uint64_t)width * virtual_height * (bpp / 8);
    if (required > info.vram_bytes) {
        kwarn("BGA: %ux%u at %u bpp x%u needs %lu KB but only %lu KB of VRAM; "
              "dropping to single buffered\n",
              width, height, bpp, virtual_height_multiplier,
              required / 1024, info.vram_bytes / 1024);

        virtual_height_multiplier = 1;
        virtual_height = height;
        required = (uint64_t)width * height * (bpp / 8);

        if (required > info.vram_bytes) {
            kerror("BGA: mode does not fit in video memory at all\n");
            return 0;
        }
    }

    // The adapter must be disabled while the mode registers change. Writing
    // them live can latch a half-configured mode and produce a black screen
    // that no later write recovers from.
    bga_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);

    bga_write(VBE_DISPI_INDEX_XRES, (uint16_t)width);
    bga_write(VBE_DISPI_INDEX_YRES, (uint16_t)height);
    bga_write(VBE_DISPI_INDEX_BPP,  (uint16_t)bpp);

    // Virtual resolution is what makes page flipping possible: video memory
    // holds `virtual_height` rows, the display shows a `height`-row window
    // into it, and the Y offset register chooses where that window starts.
    if (info.version >= VBE_DISPI_ID4) {
        bga_write(VBE_DISPI_INDEX_VIRT_WIDTH,  (uint16_t)width);
        bga_write(VBE_DISPI_INDEX_VIRT_HEIGHT, (uint16_t)virtual_height);
    } else {
        virtual_height_multiplier = 1;
        virtual_height = height;
    }

    bga_write(VBE_DISPI_INDEX_X_OFFSET, 0);
    bga_write(VBE_DISPI_INDEX_Y_OFFSET, 0);

    // LFB_ENABLED asks for a flat linear framebuffer instead of the 64 KB
    // banked window that the legacy VGA aperture at 0xA0000 provides.
    bga_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED);

    // Read the mode back. The adapter is allowed to refuse or round a request,
    // and believing a mode we did not get means drawing at the wrong stride.
    uint16_t actual_width  = bga_read(VBE_DISPI_INDEX_XRES);
    uint16_t actual_height = bga_read(VBE_DISPI_INDEX_YRES);
    uint16_t actual_bpp    = bga_read(VBE_DISPI_INDEX_BPP);

    if (actual_width != width || actual_height != height || actual_bpp != bpp) {
        kwarn("BGA: asked for %ux%ux%u, hardware gave %ux%ux%u\n",
              width, height, bpp, actual_width, actual_height, actual_bpp);
    }

    info.width          = actual_width;
    info.height         = actual_height;
    info.bpp            = actual_bpp;
    info.virtual_height = virtual_height;
    enabled             = 1;

    kok("BGA: mode set to %ux%u at %u bpp (virtual height %u -> %s)\n",
        info.width, info.height, info.bpp, info.virtual_height,
        virtual_height_multiplier > 1 ? "hardware page flipping"
                                      : "single buffered");

    return 1;
}

void bga_set_display_offset(uint32_t y_offset)
{
    if (!enabled) {
        return;
    }

    // The entire page flip. One 16-bit port write moves the scanout origin;
    // no pixels are copied at all. Compare with the software path in
    // framebuffer.c, which has to move up to 3 MB per frame.
    bga_write(VBE_DISPI_INDEX_Y_OFFSET, (uint16_t)y_offset);
}

void bga_disable(void)
{
    if (!info.present) {
        return;
    }

    bga_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
    enabled = 0;
}

int bga_is_enabled(void)
{
    return enabled;
}
