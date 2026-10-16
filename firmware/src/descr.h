// ===================================================================================
// USB descriptors
// ===================================================================================
// Three USB layouts share one firmware. It picks one at start-up: "gamepad" when
// any action uses the gamepad, otherwise "keyboard"; "xbox360" only when that
// option is switched on.
//
//  keyboard  IF0 HID keyboard + consumer + mouse (EP1 IN)
//            IF1 HID vendor page 0xFF54: live state (EP3 IN) + config feature report
//
//  gamepad   same as keyboard, plus
//            IF2 XInput (FF/5D/01, EP2 IN + EP2 OUT). A Microsoft OS "XUSB10"
//            compatible ID makes Windows load its built-in Xbox driver on that
//            interface only, so keys and configuration keep working.
//
//  xbox360   whole device impersonates a wired Xbox 360 pad (045E:028E), for PCs
//            where the composite variant does not bind. No keys or config.

#pragma once
#include <stdint.h>

#define REPORT_ID_KEYBOARD  1
#define REPORT_ID_CONSUMER  2
#define REPORT_ID_MOUSE     3
#define STATUS_REPORT_LEN   8
#define FEATURE_REPORT_LEN  40
#define XINPUT_REPORT_LEN   20

#define KBD_CFG_LEN         59
#define GAMEPAD_CFG_LEN       99
#define XBOX360_CFG_LEN      153
#define CFG_HID0_OFFSET     18          // HID class descriptor of interface 0
#define CFG_HID1_OFFSET     43          // HID class descriptor of interface 1
#define STR_COUNT           5
#define MS_OS_STRING_INDEX  0xEE

extern __code uint8_t DevDescrKeyboard[18];
extern __code uint8_t DevDescrGamepad[18];
extern __code uint8_t DevDescrXbox360[18];
extern __code uint8_t CfgDescrKeyboard[KBD_CFG_LEN];
extern __code uint8_t CfgDescrGamepad[GAMEPAD_CFG_LEN];
extern __code uint8_t CfgDescrXbox360[XBOX360_CFG_LEN];
extern __code uint8_t Report0[];
extern __code uint8_t Report0Len;
extern __code uint8_t Report1[];
extern __code uint8_t Report1Len;
extern __code uint8_t MsOsString[18];
extern __code uint8_t MsCompatId[40];
extern __code uint8_t* __code Strings[STR_COUNT];
extern __code uint8_t* __code Xbox360Strings[STR_COUNT];
