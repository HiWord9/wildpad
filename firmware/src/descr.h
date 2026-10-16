// ===================================================================================
// USB descriptors for both device modes
// ===================================================================================
#pragma once
#include <stdint.h>

// Keyboard mode -------------------------------------------------------------------
// Interface 0: HID keyboard (report 1) + consumer control (2) + mouse (3), EP1 IN
// Interface 1: HID vendor page 0xFF54: 8-byte input (live knob state, EP3 IN) and
//              32-byte feature report (configuration protocol)
#define KBD_CFG_LEN         59
#define KBD_HID0_OFFSET     18          // HID class descriptor of interface 0 in config
#define KBD_HID1_OFFSET     43          // HID class descriptor of interface 1 in config
#define KBD_STR_COUNT       5

#define REPORT_ID_KEYBOARD  1
#define REPORT_ID_CONSUMER  2
#define REPORT_ID_MOUSE     3
#define STATUS_REPORT_LEN   8
#define FEATURE_REPORT_LEN  40

extern __code uint8_t KBD_DevDescr[18];
extern __code uint8_t KBD_CfgDescr[KBD_CFG_LEN];
extern __code uint8_t KBD_Report0[];
extern __code uint8_t KBD_Report0Len;
extern __code uint8_t KBD_Report1[];
extern __code uint8_t KBD_Report1Len;
extern __code uint8_t* __code KBD_Strings[KBD_STR_COUNT];

// Gamepad mode (Xbox 360 wired controller layout) ----------------------------------
// Interface 0 is the XInput data interface: EP1 IN (20-byte reports), EP2 OUT
// (rumble / LED commands). Interfaces 1-3 are declared exactly like a real pad so
// the xusb22 driver accepts the device; their endpoints are never used on PC.
#define PAD_CFG_LEN         153
#define PAD_STR_COUNT       5

extern __code uint8_t PAD_DevDescr[18];
extern __code uint8_t PAD_CfgDescr[PAD_CFG_LEN];
extern __code uint8_t* __code PAD_Strings[PAD_STR_COUNT];
