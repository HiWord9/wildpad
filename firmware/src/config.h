// ===================================================================================
// Wildpad firmware - build and board configuration
// ===================================================================================
// Build options and what all supported boards share. Boards themselves are described
// in settings.c (docs/porting.md).

#pragma once

#define FW_MAJOR            0
#define FW_MINOR            0
#define FW_PATCH            1
#define PROTOCOL_VERSION    3           // configuration protocol, see docs/protocol.md

// -----------------------------------------------------------------------------------
// Boards
// -----------------------------------------------------------------------------------
// One firmware runs every supported CH552 "keys + knob" pad; the board and its key
// pins come from the settings (BOARDS table in settings.c, or a stored pin map). What
// all of them share is fixed here.
#define LED_PIN             4           // WS2812 chain (GRB) on P3.4

// Encoder phases sampled in the timer ISR: state = (A << 1) | B, A = P3.1, B = P3.0
#define ENC_PORT_STATE()    (P3 & 0x03)

// -----------------------------------------------------------------------------------
// USB identity
// -----------------------------------------------------------------------------------
// The stock VID/PID is kept so the board stays recognisable. bcdDevice differs per
// USB layout because Windows caches per VID/PID/bcdDevice whether a device has
// Microsoft OS descriptors - and only the layout with the gamepad has them.
#define USB_VENDOR_ID       0x1189
#define USB_PRODUCT_ID      0x8890
#define USB_BCD_KEYBOARD    0x0100
#define USB_BCD_GAMEPAD     0x0101
#define USB_BCD_UPDATE      0x0102
#define UPDATE_GO_REQUEST   0x5B        // vendor OUT request: hand over to the ROM bootloader

#define USB_MAX_POWER_mA    100
#define MS_OS_VENDOR_CODE   0x5A        // bRequest for Microsoft OS feature descriptors
