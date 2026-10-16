// ===================================================================================
// TISHRIC Knob firmware - hardware and build configuration
// ===================================================================================
// Board: cheap "3 keys + 1 knob" CH552G macropad (stock USB ID 1189:8890).
// Pinout verified against the other open firmwares for this PCB.

#pragma once

#define FW_VERSION          1

// Pins
#define PIN_NEO             P34         // 3x WS2812 (GRB)
#define PIN_KEY1            P11
#define PIN_KEY2            P17
#define PIN_KEY3            P16
#define PIN_ENC_SW          P33         // knob push switch
#define PIN_ENC_A           P31         // knob phase A
#define PIN_ENC_B           P30         // knob phase B

// The encoder phases are read straight from port 3 in the timer ISR:
// state = (A << 1) | B = P3 & 0x03 (A = P3.1, B = P3.0)
#define ENC_PORT_STATE()    (P3 & 0x03)

#define NEO_GRB
#define NEO_COUNT           3

// USB identity in keyboard mode (kept identical to stock so nothing else changes)
#define KBD_VENDOR_ID       0x1189
#define KBD_PRODUCT_ID      0x8890
#define KBD_DEVICE_VERSION  0x0200      // differs from stock (0x0000) so Windows re-reads descriptors

// USB identity in gamepad mode: Xbox 360 wired controller, so Windows binds its
// built-in XInput driver (xusb22) and games see a normal Xbox pad.
#define PAD_VENDOR_ID       0x045E
#define PAD_PRODUCT_ID      0x028E
#define PAD_DEVICE_VERSION  0x0114

#define USB_MAX_POWER_mA    100
