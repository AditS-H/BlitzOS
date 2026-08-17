// pci.h - PCI bus enumeration via configuration space.
//
// WHY THIS EXISTS
// ---------------
// Everything past the legacy ISA devices (PIT, PS/2, serial) lives on PCI, and
// PCI devices do not sit at fixed I/O ports. To talk to a graphics adapter,
// disk controller or network card you first have to *find* it and ask where it
// put its registers. That is what this file does.
//
// HOW CONFIGURATION SPACE WORKS
// -----------------------------
// Every PCI function has a 256-byte configuration space describing what it is
// and where its memory lives. On x86 it is reached through two I/O ports:
//
//   0xCF8  CONFIG_ADDRESS - write which (bus, device, function, offset) you want
//   0xCFC  CONFIG_DATA    - then read/write the 32-bit value at that offset
//
// The address word is laid out as:
//
//   bit 31     enable (must be 1)
//   bits 30-24 reserved
//   bits 23-16 bus number      (0-255)
//   bits 15-11 device number   (0-31)
//   bits 10-8  function number (0-7)
//   bits 7-2   register offset (dword-aligned)
//   bits 1-0   must be 0
//
// A device is present if its vendor ID is not 0xFFFF (an absent device floats
// the bus high, so all reads come back as all-ones).
//
// BASE ADDRESS REGISTERS (BARs)
// -----------------------------
// Offsets 0x10-0x24 hold up to six BARs. Each says where the device mapped a
// region of memory or I/O. Bit 0 distinguishes them:
//
//   bit 0 = 0  memory BAR - bits 2:1 give the type (00 = 32-bit, 10 = 64-bit,
//              in which case this BAR and the next one together form one
//              64-bit address), bit 3 is the prefetchable hint
//   bit 0 = 1  I/O port BAR - the address is in bits 31:2
//
// The graphics adapter's linear framebuffer is simply BAR0 of the display
// controller, which is how framebuffer.c finds somewhere to draw.

#ifndef DRIVERS_PCI_H
#define DRIVERS_PCI_H

#include <stdint.h>

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

// Standard configuration space offsets (header type 0).
#define PCI_OFF_VENDOR_ID    0x00
#define PCI_OFF_DEVICE_ID    0x02
#define PCI_OFF_COMMAND      0x04
#define PCI_OFF_STATUS       0x06
#define PCI_OFF_REVISION     0x08
#define PCI_OFF_PROG_IF      0x09
#define PCI_OFF_SUBCLASS     0x0A
#define PCI_OFF_CLASS        0x0B
#define PCI_OFF_HEADER_TYPE  0x0E
#define PCI_OFF_BAR0         0x10
#define PCI_OFF_INTERRUPT_LINE 0x3C

// Command register bits worth knowing.
#define PCI_CMD_IO_SPACE      (1 << 0)
#define PCI_CMD_MEMORY_SPACE  (1 << 1)
#define PCI_CMD_BUS_MASTER    (1 << 2)

// Class codes we care about.
#define PCI_CLASS_MASS_STORAGE 0x01
#define PCI_CLASS_NETWORK      0x02
#define PCI_CLASS_DISPLAY      0x03
#define PCI_CLASS_MULTIMEDIA   0x04
#define PCI_CLASS_BRIDGE       0x06

#define PCI_SUBCLASS_VGA       0x00

#define PCI_MAX_DEVICES 32   // how many we bother to remember

typedef struct {
    uint8_t  bus;
    uint8_t  device;
    uint8_t  function;

    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  prog_if;
    uint8_t  revision;
    uint8_t  header_type;
    uint8_t  interrupt_line;

    // Decoded BARs. `bar_is_io` says whether the value is a port number or a
    // physical memory address.
    uint64_t bar[6];
    uint8_t  bar_is_io[6];
} pci_device_t;

// ---------------------------------------------------------------------------
// Raw configuration space access
// ---------------------------------------------------------------------------

uint32_t pci_read32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
uint16_t pci_read16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
uint8_t  pci_read8 (uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
void     pci_write32(uint8_t bus, uint8_t device, uint8_t function,
                     uint8_t offset, uint32_t value);

// ---------------------------------------------------------------------------
// Enumeration
// ---------------------------------------------------------------------------

// Walks every bus/device/function and fills the device table. Call once at boot.
void pci_init(void);

uint32_t            pci_device_count(void);
const pci_device_t* pci_device_at(uint32_t index);

// Finds the first device matching a class (and optionally a subclass; pass
// 0xFF to match any). Returns NULL if there is none.
const pci_device_t* pci_find_by_class(uint8_t class_code, uint8_t subclass);

// Finds a specific vendor/device pair.
const pci_device_t* pci_find_by_id(uint16_t vendor_id, uint16_t device_id);

// Turns on memory-space decoding and bus mastering for a device. Some
// firmware leaves these off, and a device with memory decoding disabled
// ignores every write to its BAR region - which looks exactly like a dead
// framebuffer.
void pci_enable_device(const pci_device_t* device);

// Human-readable names, for `lspci`.
const char* pci_class_name(uint8_t class_code, uint8_t subclass);
const char* pci_vendor_name(uint16_t vendor_id);

#endif // DRIVERS_PCI_H
