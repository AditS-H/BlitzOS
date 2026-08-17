#include "mouse.h"
#include "keyboard.h"
#include "../kernel/arch/x86_64/interrupts.h"
#include "../kernel/lib/kprintf.h"
#include "../kernel/lib/string.h"

// 8042 controller commands
#define PS2_CMD_DISABLE_PORT1     0xAD
#define PS2_CMD_ENABLE_PORT1      0xAE
#define PS2_CMD_DISABLE_PORT2     0xA7
#define PS2_CMD_ENABLE_AUX        0xA8
#define PS2_CMD_READ_CONFIG       0x20
#define PS2_CMD_WRITE_CONFIG      0x60
#define PS2_CMD_WRITE_TO_AUX      0xD4

// Mouse commands (sent through 0xD4)
#define MOUSE_CMD_RESET           0xFF
#define MOUSE_CMD_SET_DEFAULTS    0xF6
#define MOUSE_CMD_ENABLE_REPORT   0xF4
#define MOUSE_CMD_SET_SAMPLE_RATE 0xF3

#define PS2_ACK 0xFA

// Status register bits
#define PS2_STATUS_OUTPUT_FULL 0x01
#define PS2_STATUS_INPUT_FULL  0x02
#define PS2_STATUS_FROM_AUX    0x20   // the waiting byte is from the mouse

static mouse_state_t state;
static int           present = 0;

static int32_t bound_width  = 640;
static int32_t bound_height = 480;

// Packet reassembly
static uint8_t packet[3];
static uint8_t packet_index = 0;

static uint8_t pressed_edges  = 0;
static uint8_t released_edges = 0;
static int     moved          = 0;

// ---------------------------------------------------------------------------
// Controller plumbing
// ---------------------------------------------------------------------------

// All waits are bounded. A missing or wedged controller must not hang the
// kernel during boot.
static int wait_for_write(void)
{
    for (uint32_t i = 0; i < 100000; i++) {
        if (!(inb(KB_STATUS_PORT) & PS2_STATUS_INPUT_FULL)) {
            return 1;
        }
    }
    return 0;
}

static int wait_for_read(void)
{
    for (uint32_t i = 0; i < 100000; i++) {
        if (inb(KB_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) {
            return 1;
        }
    }
    return 0;
}

static void controller_command(uint8_t command)
{
    wait_for_write();
    outb(KB_COMMAND_PORT, command);
}

static uint8_t controller_read(void)
{
    if (!wait_for_read()) {
        return 0xFF;
    }
    return inb(KB_DATA_PORT);
}

// Writes to the mouse rather than the keyboard, and waits for its ACK.
static int mouse_command(uint8_t command)
{
    controller_command(PS2_CMD_WRITE_TO_AUX);

    if (!wait_for_write()) {
        return 0;
    }
    outb(KB_DATA_PORT, command);

    // Every mouse command answers 0xFA. No answer means no mouse.
    return controller_read() == PS2_ACK;
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------

// Throw away anything already sitting in the output buffer.
//
// Necessary before configuring: firmware often leaves a byte pending, and if
// it is still there when we send the first mouse command, we read *that* as
// the acknowledgement and every subsequent read is off by one.
static void flush_output_buffer(void)
{
    for (uint32_t i = 0; i < 64; i++) {
        if (!(inb(KB_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL)) {
            return;
        }
        (void)inb(KB_DATA_PORT);
    }
}

int mouse_init(void)
{
    memset(&state, 0, sizeof(state));
    packet_index = 0;

    // Follow the sequence the PS/2 specification recommends rather than
    // configuring a live controller. Doing this with both ports still enabled
    // means a keystroke arriving mid-setup gets mistaken for a mouse reply.

    // 1. Silence both devices while we reconfigure.
    controller_command(PS2_CMD_DISABLE_PORT1);
    controller_command(PS2_CMD_DISABLE_PORT2);

    // 2. Drop anything they already sent.
    flush_output_buffer();

    // 3. Configuration byte: enable the auxiliary interrupt (bit 1) and the
    //    auxiliary clock (bit 5 clear). Without bit 1 the mouse works fine but
    //    never raises IRQ12, so nothing ever notices it moving.
    controller_command(PS2_CMD_READ_CONFIG);
    uint8_t config = controller_read();

    config |= (1 << 1);
    config &= (uint8_t)~(1 << 5);

    controller_command(PS2_CMD_WRITE_CONFIG);
    if (!wait_for_write()) {
        kwarn("mouse: PS/2 controller did not accept a configuration write\n");
        present = 0;
        return 0;
    }
    outb(KB_DATA_PORT, config);

    // 4. Bring both ports back up.
    controller_command(PS2_CMD_ENABLE_AUX);
    controller_command(PS2_CMD_ENABLE_PORT1);

    // 5. Reset the mouse. A working device answers 0xFA (ack), then 0xAA
    //    (self-test passed), then 0x00 (its device ID).
    if (!mouse_command(MOUSE_CMD_RESET)) {
        kwarn("mouse: no acknowledgement to RESET - no mouse attached\n");
        present = 0;
        return 0;
    }

    uint8_t self_test = controller_read();
    (void)controller_read();          // device ID, always 0x00 for a plain mouse

    if (self_test != 0xAA) {
        kwarn("mouse: self-test returned 0x%x instead of 0xAA\n", self_test);
    }

    // 6. Known-good settings, then start reporting.
    if (!mouse_command(MOUSE_CMD_SET_DEFAULTS)) {
        kwarn("mouse: device refused SET_DEFAULTS\n");
        present = 0;
        return 0;
    }

    // 100 samples/second. The default is 100 already, but asking explicitly
    // avoids inheriting whatever the firmware left behind.
    if (mouse_command(MOUSE_CMD_SET_SAMPLE_RATE)) {
        controller_command(PS2_CMD_WRITE_TO_AUX);
        wait_for_write();
        outb(KB_DATA_PORT, 100);
        controller_read();
    }

    if (!mouse_command(MOUSE_CMD_ENABLE_REPORT)) {
        kwarn("mouse: device refused to enable reporting\n");
        present = 0;
        return 0;
    }

    // Start in the middle of the screen.
    state.x = bound_width / 2;
    state.y = bound_height / 2;

    present = 1;
    pic_unmask_irq(12);

    kok("mouse: PS/2 mouse ready on IRQ12 (100 Hz sample rate)\n");
    return 1;
}

int mouse_is_present(void)
{
    return present;
}

void mouse_set_bounds(int32_t width, int32_t height)
{
    bound_width  = width;
    bound_height = height;

    if (state.x >= width)  state.x = width - 1;
    if (state.y >= height) state.y = height - 1;
}

// ---------------------------------------------------------------------------
// Interrupt handler
// ---------------------------------------------------------------------------

void mouse_handler(void)
{
    // Only take the byte if the controller says it came from the mouse.
    // Without this check a keyboard byte arriving at the wrong moment gets
    // decoded as movement and the cursor teleports.
    uint8_t status = inb(KB_STATUS_PORT);
    if (!(status & PS2_STATUS_OUTPUT_FULL) || !(status & PS2_STATUS_FROM_AUX)) {
        return;
    }

    uint8_t byte = inb(KB_DATA_PORT);

    // Bit 3 of the first byte is always 1. If it is not, we are out of sync
    // with the packet stream - drop the byte and wait for a valid header
    // rather than decoding nonsense.
    if (packet_index == 0 && !(byte & 0x08)) {
        return;
    }

    packet[packet_index++] = byte;

    if (packet_index < 3) {
        return;
    }
    packet_index = 0;

    uint8_t flags = packet[0];

    // Overflow means the mouse moved further than 9 bits can express between
    // samples. The deltas are meaningless, so discard the packet - a wild
    // jump looks far worse than a dropped frame of movement.
    if (flags & 0xC0) {
        return;
    }

    // Fold the sign bits in from byte 0. These are 9-bit two's complement
    // values, not signed chars, so a plain cast would get large moves wrong.
    int32_t dx = (int32_t)packet[1];
    int32_t dy = (int32_t)packet[2];

    if (flags & 0x10) dx |= 0xFFFFFF00;   // sign-extend X
    if (flags & 0x20) dy |= 0xFFFFFF00;   // sign-extend Y

    // The protocol's Y axis points up; screen coordinates point down.
    dy = -dy;

    state.delta_x = dx;
    state.delta_y = dy;
    state.x += dx;
    state.y += dy;

    if (state.x < 0) state.x = 0;
    if (state.y < 0) state.y = 0;
    if (state.x >= bound_width)  state.x = bound_width - 1;
    if (state.y >= bound_height) state.y = bound_height - 1;

    state.last_buttons = state.buttons;
    state.buttons      = flags & 0x07;

    // Record transitions so a click is never missed between compositor frames.
    uint8_t changed = state.buttons ^ state.last_buttons;
    pressed_edges  |= (uint8_t)(changed & state.buttons);
    released_edges |= (uint8_t)(changed & state.last_buttons);

    if (dx || dy || changed) {
        moved = 1;
    }
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

const mouse_state_t* mouse_get_state(void)
{
    return &state;
}

int mouse_button_pressed(uint8_t button)
{
    return (pressed_edges & button) != 0;
}

int mouse_button_released(uint8_t button)
{
    return (released_edges & button) != 0;
}

void mouse_clear_edges(void)
{
    uint64_t flags = irq_save();
    pressed_edges  = 0;
    released_edges = 0;
    irq_restore(flags);
}

int mouse_has_moved(void)
{
    return moved;
}

void mouse_clear_moved(void)
{
    moved = 0;
}
