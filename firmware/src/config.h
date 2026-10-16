// ===================================================================================
// Detent firmware - build and board configuration
// ===================================================================================
// Everything board specific lives in this file. To port Detent to another CH552
// macropad, change the pins, BOARD_* values and BTN_READ() below.

#pragma once

#define FW_MAJOR            0
#define FW_MINOR            0
#define FW_PATCH            1
#define PROTOCOL_VERSION    2           // configuration protocol, see docs/protocol.md

// -----------------------------------------------------------------------------------
// Board: generic "3 keys + 1 knob" CH552G macropad (stock USB ID 1189:8890)
// Pinout cross-checked with the other open firmwares for this PCB.
// -----------------------------------------------------------------------------------
#define BOARD_ID            1           // tells the configurator which drawing to use
#define BOARD_BUTTONS       4           // keys + knob switch (max MAX_BUTTONS)
#define BOARD_KNOB_BUTTON   3           // index of the knob push switch
#define BOARD_BOOT_BUTTON   0           // hold while plugging in -> bootloader
#define BOARD_RESCUE_BUTTON 2           // hold while plugging in -> keyboard mode once

#define PIN_NEO             P34         // WS2812 chain (GRB)
#define PIN_KEY1            P11
#define PIN_KEY2            P17
#define PIN_KEY3            P16
#define PIN_ENC_SW          P33
#define PIN_ENC_A           P31
#define PIN_ENC_B           P30

// Raw button state, bit i = button i pressed (inputs are active low)
#define BTN_READ() ( (PIN_read(PIN_KEY1) ? 0 : 0x01) | (PIN_read(PIN_KEY2) ? 0 : 0x02) | \
                     (PIN_read(PIN_KEY3) ? 0 : 0x04) | (PIN_read(PIN_ENC_SW) ? 0 : 0x08) )

// Encoder phases sampled in the timer ISR: state = (A << 1) | B
#define ENC_PORT_STATE()    (P3 & 0x03)

#define NEO_GRB
#define NEO_COUNT           3

// -----------------------------------------------------------------------------------
// USB identity
// -----------------------------------------------------------------------------------
// The stock VID/PID is kept so the board stays recognisable. bcdDevice differs per
// mode because Windows caches per VID/PID/bcdDevice whether a device has Microsoft
// OS descriptors - and only the wheel mode has them.
#define USB_VENDOR_ID       0x1189
#define USB_PRODUCT_ID      0x8890
#define USB_BCD_KEYBOARD    0x0100
#define USB_BCD_WHEEL       0x0101

// Legacy wheel mode impersonates a wired Xbox 360 pad for PCs where the composite
// XInput device does not bind (no configuration possible while in that mode).
#define LEGACY_VENDOR_ID    0x045E
#define LEGACY_PRODUCT_ID   0x028E
#define LEGACY_BCD          0x0114

#define USB_MAX_POWER_mA    100
#define MS_OS_VENDOR_CODE   0x5A        // bRequest for Microsoft OS feature descriptors
