// mouse.h - PS/2 mouse driver.
//
// HOW THE PS/2 CONTROLLER HANDLES TWO DEVICES
// -------------------------------------------
// The 8042 controller has two ports: the keyboard on the first, the mouse (the
// "auxiliary device") on the second. Both deliver bytes through the same data
// port 0x60, which raises an obvious question: how do you tell them apart?
//
// Answer: the status register at 0x64. Bit 5 is set when the byte waiting in
// the output buffer came from the auxiliary device. The IRQ number tells you
// too - keyboard is IRQ1, mouse is IRQ12 - but checking bit 5 is what keeps a
// stray byte from being decoded as the wrong thing.
//
// Talking *to* the mouse is more awkward. Writes to 0x60 go to the keyboard by
// default; to address the mouse you first send command 0xD4 to port 0x64,
// which means "the next byte written to 0x60 is for the auxiliary device".
// Every mouse command therefore takes two writes.
//
// THE PACKET FORMAT
// -----------------
// A standard PS/2 mouse sends 3-byte packets:
//
//   byte 0   bit 7  Y overflow      bit 3  always 1 (sync marker)
//            bit 6  X overflow      bit 2  middle button
//            bit 5  Y sign          bit 1  right button
//            bit 4  X sign          bit 0  left button
//   byte 1   X movement, 8 bits, sign in byte 0 bit 4
//   byte 2   Y movement, 8 bits, sign in byte 0 bit 5
//
// Two traps in that layout:
//
//   The movement values are 9-bit two's complement split across two bytes, not
//   plain signed chars. You have to fold the sign bit in by hand.
//
//   Y is positive *upward*, because the format predates everyone agreeing that
//   screen coordinates grow downward. It has to be negated.
//
// Bit 3 of byte 0 is always 1. If it reads 0 we have lost packet alignment -
// usually because a byte was dropped - and the only safe response is to
// resynchronise rather than decode garbage into cursor jumps.

#ifndef DRIVERS_MOUSE_H
#define DRIVERS_MOUSE_H

#include <stdint.h>

#define MOUSE_BUTTON_LEFT   0x01
#define MOUSE_BUTTON_RIGHT  0x02
#define MOUSE_BUTTON_MIDDLE 0x04

typedef struct {
    int32_t x, y;          // cursor position, clamped to the screen
    int32_t delta_x;       // movement since the last event
    int32_t delta_y;
    uint8_t buttons;       // MOUSE_BUTTON_* bitmask
    uint8_t last_buttons;  // previous state, for edge detection
} mouse_state_t;

// Brings up the auxiliary device and unmasks IRQ12. Returns 1 on success.
int mouse_init(void);

int mouse_is_present(void);

// Called from IRQ12.
void mouse_handler(void);

const mouse_state_t* mouse_get_state(void);

// Tells the driver how far the cursor may travel. Call after the framebuffer
// mode is known.
void mouse_set_bounds(int32_t width, int32_t height);

// True if a button went down (or up) since the last call to mouse_clear_edges().
int mouse_button_pressed(uint8_t button);
int mouse_button_released(uint8_t button);
void mouse_clear_edges(void);

// True if a packet has arrived since the flag was last cleared. Lets the
// compositor skip redrawing the cursor when nothing moved.
int  mouse_has_moved(void);
void mouse_clear_moved(void);

#endif // DRIVERS_MOUSE_H
