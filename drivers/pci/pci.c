#include "pci.h"
#include "../../kernel/arch/x86_64/interrupts.h"
#include "../../kernel/lib/kprintf.h"
#include "../../kernel/lib/string.h"

static pci_device_t devices[PCI_MAX_DEVICES];
static uint32_t     device_count = 0;

// ---------------------------------------------------------------------------
// Configuration space access
// ---------------------------------------------------------------------------

// Builds the CONFIG_ADDRESS word. Offset is forced dword-aligned because the
// hardware ignores the low two bits; we mask explicitly so a caller passing
// 0x0A (subclass) reads the dword at 0x08 and we shift out the byte we want.
static uint32_t pci_address(uint8_t bus, uint8_t device, uint8_t function,
                            uint8_t offset)
{
    return (uint32_t)((1u << 31)              // enable bit
                      | ((uint32_t)bus << 16)
                      | ((uint32_t)(device & 0x1F) << 11)
                      | ((uint32_t)(function & 0x07) << 8)
                      | (offset & 0xFC));
}

uint32_t pci_read32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset)
{
    outl(PCI_CONFIG_ADDRESS, pci_address(bus, device, function, offset));
    return inl(PCI_CONFIG_DATA);
}

uint16_t pci_read16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset)
{
    uint32_t dword = pci_read32(bus, device, function, offset);
    // Two 16-bit fields per dword; pick the half the offset asked for.
    return (uint16_t)((dword >> ((offset & 2) * 8)) & 0xFFFF);
}

uint8_t pci_read8(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset)
{
    uint32_t dword = pci_read32(bus, device, function, offset);
    return (uint8_t)((dword >> ((offset & 3) * 8)) & 0xFF);
}

void pci_write32(uint8_t bus, uint8_t device, uint8_t function,
                 uint8_t offset, uint32_t value)
{
    outl(PCI_CONFIG_ADDRESS, pci_address(bus, device, function, offset));
    outl(PCI_CONFIG_DATA, value);
}

// ---------------------------------------------------------------------------
// BAR decoding
// ---------------------------------------------------------------------------

static void decode_bars(pci_device_t* dev)
{
    for (int i = 0; i < 6; i++) {
        uint32_t raw = pci_read32(dev->bus, dev->device, dev->function,
                                  (uint8_t)(PCI_OFF_BAR0 + i * 4));

        if (raw == 0) {
            dev->bar[i]       = 0;
            dev->bar_is_io[i] = 0;
            continue;
        }

        if (raw & 1) {
            // I/O BAR: the port number lives in bits 31:2.
            dev->bar[i]       = raw & ~0x3u;
            dev->bar_is_io[i] = 1;
            continue;
        }

        // Memory BAR. Bits 2:1 encode the width.
        uint32_t type = (raw >> 1) & 0x3;
        dev->bar_is_io[i] = 0;

        if (type == 0x02 && i < 5) {
            // 64-bit BAR: this register holds the low half, the next one the
            // high half. Consume both and skip the second on the next pass.
            uint32_t high = pci_read32(dev->bus, dev->device, dev->function,
                                       (uint8_t)(PCI_OFF_BAR0 + (i + 1) * 4));
            dev->bar[i] = ((uint64_t)high << 32) | (raw & ~0xFu);

            i++;
            dev->bar[i]       = 0;
            dev->bar_is_io[i] = 0;
        } else {
            dev->bar[i] = raw & ~0xFu;
        }
    }
}

// ---------------------------------------------------------------------------
// Enumeration
// ---------------------------------------------------------------------------

static void probe_function(uint8_t bus, uint8_t device, uint8_t function)
{
    uint16_t vendor = pci_read16(bus, device, function, PCI_OFF_VENDOR_ID);

    // 0xFFFF means nothing answered: an absent device leaves the bus floating
    // high, so every read comes back as all-ones.
    if (vendor == 0xFFFF) {
        return;
    }

    if (device_count >= PCI_MAX_DEVICES) {
        return;
    }

    pci_device_t* dev = &devices[device_count++];
    memset(dev, 0, sizeof(*dev));

    dev->bus            = bus;
    dev->device         = device;
    dev->function       = function;
    dev->vendor_id      = vendor;
    dev->device_id      = pci_read16(bus, device, function, PCI_OFF_DEVICE_ID);
    dev->revision       = pci_read8 (bus, device, function, PCI_OFF_REVISION);
    dev->prog_if        = pci_read8 (bus, device, function, PCI_OFF_PROG_IF);
    dev->subclass       = pci_read8 (bus, device, function, PCI_OFF_SUBCLASS);
    dev->class_code     = pci_read8 (bus, device, function, PCI_OFF_CLASS);
    dev->header_type    = pci_read8 (bus, device, function, PCI_OFF_HEADER_TYPE);
    dev->interrupt_line = pci_read8 (bus, device, function, PCI_OFF_INTERRUPT_LINE);

    // Only header type 0 (normal device) has six BARs. Bridges are laid out
    // differently, so do not try to read BARs out of them.
    if ((dev->header_type & 0x7F) == 0) {
        decode_bars(dev);
    }
}

void pci_init(void)
{
    device_count = 0;

    // Brute-force scan. A recursive scan following bridges is tidier, but on
    // the hardware this kernel targets (and in QEMU) everything worth finding
    // is on bus 0, and 256*32 probes take under a millisecond.
    for (uint32_t bus = 0; bus < 256; bus++) {
        for (uint8_t device = 0; device < 32; device++) {

            // Function 0 must exist for the slot to be occupied at all.
            if (pci_read16((uint8_t)bus, device, 0, PCI_OFF_VENDOR_ID) == 0xFFFF) {
                continue;
            }

            probe_function((uint8_t)bus, device, 0);

            // Bit 7 of the header type says the device is multi-function; only
            // then is it legal to probe functions 1-7.
            uint8_t header = pci_read8((uint8_t)bus, device, 0, PCI_OFF_HEADER_TYPE);
            if (header & 0x80) {
                for (uint8_t function = 1; function < 8; function++) {
                    probe_function((uint8_t)bus, device, function);
                }
            }
        }
    }

    kok("PCI: %u device%s found\n", device_count, device_count == 1 ? "" : "s");
}

// ---------------------------------------------------------------------------
// Lookup
// ---------------------------------------------------------------------------

uint32_t pci_device_count(void)
{
    return device_count;
}

const pci_device_t* pci_device_at(uint32_t index)
{
    return index < device_count ? &devices[index] : NULL;
}

const pci_device_t* pci_find_by_class(uint8_t class_code, uint8_t subclass)
{
    for (uint32_t i = 0; i < device_count; i++) {
        if (devices[i].class_code != class_code) {
            continue;
        }
        if (subclass != 0xFF && devices[i].subclass != subclass) {
            continue;
        }
        return &devices[i];
    }
    return NULL;
}

const pci_device_t* pci_find_by_id(uint16_t vendor_id, uint16_t device_id)
{
    for (uint32_t i = 0; i < device_count; i++) {
        if (devices[i].vendor_id == vendor_id && devices[i].device_id == device_id) {
            return &devices[i];
        }
    }
    return NULL;
}

void pci_enable_device(const pci_device_t* device)
{
    if (!device) {
        return;
    }

    uint32_t command = pci_read32(device->bus, device->device, device->function,
                                  PCI_OFF_COMMAND);

    // Memory-space decoding off means the device ignores every write to its
    // BAR region. A framebuffer in that state looks completely dead, with no
    // error anywhere - worth setting explicitly rather than trusting firmware.
    command |= PCI_CMD_MEMORY_SPACE | PCI_CMD_IO_SPACE | PCI_CMD_BUS_MASTER;

    pci_write32(device->bus, device->device, device->function,
                PCI_OFF_COMMAND, command);
}

// ---------------------------------------------------------------------------
// Names
// ---------------------------------------------------------------------------

const char* pci_class_name(uint8_t class_code, uint8_t subclass)
{
    switch (class_code) {
    case 0x00: return "Unclassified";
    case 0x01:
        switch (subclass) {
        case 0x01: return "IDE controller";
        case 0x06: return "SATA controller";
        case 0x08: return "NVMe controller";
        default:   return "Storage controller";
        }
    case 0x02: return "Network controller";
    case 0x03:
        return subclass == 0x00 ? "VGA display controller" : "Display controller";
    case 0x04: return "Multimedia controller";
    case 0x05: return "Memory controller";
    case 0x06:
        switch (subclass) {
        case 0x00: return "Host bridge";
        case 0x01: return "ISA bridge";
        case 0x04: return "PCI-to-PCI bridge";
        default:   return "Bridge";
        }
    case 0x07: return "Communication controller";
    case 0x08: return "System peripheral";
    case 0x09: return "Input device";
    case 0x0C: return "Serial bus controller";
    default:   return "Unknown";
    }
}

const char* pci_vendor_name(uint16_t vendor_id)
{
    // Just the handful we are likely to meet. A full PCI ID database is
    // megabytes; the kernel does not need one.
    switch (vendor_id) {
    case 0x1234: return "QEMU";
    case 0x1AF4: return "Red Hat / VirtIO";
    case 0x8086: return "Intel";
    case 0x10DE: return "NVIDIA";
    case 0x1002: return "AMD/ATI";
    case 0x1013: return "Cirrus Logic";
    case 0x15AD: return "VMware";
    case 0x80EE: return "VirtualBox";
    case 0x1414: return "Microsoft";
    default:     return "Unknown vendor";
    }
}
