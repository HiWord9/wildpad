// ===================================================================================
// USB descriptors
// ===================================================================================
// The XInput interface follows a genuine wired Xbox 360 controller (layout as in
// GP2040-CE, MIT, https://github.com/OpenStickCommunity/GP2040-CE). The Microsoft OS
// descriptor format follows [MS-XUSBI] 2.2.2.

#include "config.h"
#include "descr.h"

#define LO(x) ((x) & 0xFF)
#define HI(x) (((x) >> 8) & 0xFF)

#define DEV_DESCR(vid, pid, bcd, usb, cls) {                              \
  0x12, 0x01, LO(usb), HI(usb), cls, cls, cls, 0x40,                      \
  LO(vid), HI(vid), LO(pid), HI(pid), LO(bcd), HI(bcd), 1, 2, 3, 1 }

__code uint8_t DevDescrKeyboard[18] = DEV_DESCR(USB_VENDOR_ID, USB_PRODUCT_ID, USB_BCD_KEYBOARD, 0x0200, 0x00);
__code uint8_t DevDescrGamepad[18]  = DEV_DESCR(USB_VENDOR_ID, USB_PRODUCT_ID, USB_BCD_GAMEPAD,    0x0200, 0x00);

// ===================================================================================
// HID report descriptors
// ===================================================================================
__code uint8_t Report0[] = {
  // Keyboard (report 1): modifiers, reserved, 6 key slots; LED output
  0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, REPORT_ID_KEYBOARD,
  0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,
  0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
  0x75, 0x08, 0x95, 0x01, 0x81, 0x01,
  0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x75, 0x01, 0x95, 0x05, 0x91, 0x02,
  0x75, 0x03, 0x95, 0x01, 0x91, 0x01,
  0x05, 0x07, 0x19, 0x00, 0x2A, 0xFF, 0x00, 0x15, 0x00, 0x26, 0xFF, 0x00,
  0x75, 0x08, 0x95, 0x06, 0x81, 0x00,
  0xC0,
  // Consumer control (report 2): one 16-bit usage
  0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, REPORT_ID_CONSUMER,
  0x19, 0x00, 0x2A, 0xFF, 0x03, 0x15, 0x00, 0x26, 0xFF, 0x03,
  0x75, 0x10, 0x95, 0x01, 0x81, 0x00,
  0xC0,
  // Mouse (report 3): 5 buttons, X, Y, wheel, horizontal wheel
  0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, REPORT_ID_MOUSE,
  0x09, 0x01, 0xA1, 0x00,
  0x05, 0x09, 0x19, 0x01, 0x29, 0x05, 0x15, 0x00, 0x25, 0x01,
  0x75, 0x01, 0x95, 0x05, 0x81, 0x02,
  0x75, 0x03, 0x95, 0x01, 0x81, 0x01,
  0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38, 0x15, 0x81, 0x25, 0x7F,
  0x75, 0x08, 0x95, 0x03, 0x81, 0x06,
  0x05, 0x0C, 0x0A, 0x38, 0x02, 0x15, 0x81, 0x25, 0x7F,
  0x75, 0x08, 0x95, 0x01, 0x81, 0x06,
  0xC0,
  0xC0
};
__code uint8_t Report0Len = sizeof(Report0);

__code uint8_t Report1[] = {
  0x06, 0x54, 0xFF,             // usage page 0xFF54 (vendor)
  0x09, 0x01, 0xA1, 0x01,       // usage 1, application collection
  0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08,
  0x09, 0x02, 0x95, STATUS_REPORT_LEN,  0x81, 0x02,   // input: live knob state
  0x09, 0x03, 0x95, FEATURE_REPORT_LEN, 0xB1, 0x02,   // feature: config protocol
  0xC0
};
__code uint8_t Report1Len = sizeof(Report1);

// ===================================================================================
// Configuration descriptors
// ===================================================================================
#define CFG_HEADER(len, n) 0x09, 0x02, (len), 0x00, (n), 0x01, 0x00, 0x80, USB_MAX_POWER_mA / 2

#define HID_INTERFACES                                                                 \
  /* Interface 0: keyboard / consumer / mouse */                                       \
  0x09, 0x04, 0x00, 0x00, 0x01, 0x03, 0x00, 0x00, 0x00,                                \
  0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, sizeof(Report0), 0x00,                     \
  0x07, 0x05, 0x81, 0x03, 0x10, 0x00, 0x01,                                            \
  /* Interface 1: configuration + live state */                                        \
  0x09, 0x04, 0x01, 0x00, 0x01, 0x03, 0x00, 0x00, 0x04,                                \
  0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, sizeof(Report1), 0x00,                     \
  0x07, 0x05, 0x83, 0x03, STATUS_REPORT_LEN, 0x00, 0x02

__code uint8_t CfgDescrKeyboard[KBD_CFG_LEN] = {
  CFG_HEADER(KBD_CFG_LEN, 2),
  HID_INTERFACES
};

__code uint8_t CfgDescrGamepad[GAMEPAD_CFG_LEN] = {
  CFG_HEADER(GAMEPAD_CFG_LEN, 3),
  HID_INTERFACES,
  // Interface 2: XInput (bound to xusb22 through the XUSB10 compatible ID)
  0x09, 0x04, 0x02, 0x00, 0x02, 0xFF, 0x5D, 0x01, 0x00,
  0x11, 0x21, 0x00, 0x01, 0x01, 0x25, 0x82, 0x14, 0x00, 0x00, 0x00, 0x00, 0x13,
  0x02, 0x08, 0x00, 0x00,
  0x07, 0x05, 0x82, 0x03, 0x20, 0x00, 0x01,           // EP2 IN, 1 ms
  0x07, 0x05, 0x02, 0x03, 0x20, 0x00, 0x08            // EP2 OUT (rumble / LED)
};

// ===================================================================================
// Microsoft OS 1.0 descriptors (gamepad layout only)
// ===================================================================================
__code uint8_t MsOsString[18] = {
  0x12, 0x03, 'M', 0, 'S', 0, 'F', 0, 'T', 0, '1', 0, '0', 0, '0', 0, MS_OS_VENDOR_CODE, 0x00
};

__code uint8_t MsCompatId[40] = {
  0x28, 0x00, 0x00, 0x00,       // dwLength
  0x00, 0x01,                   // bcdVersion 1.00
  0x04, 0x00,                   // wIndex: extended compatible ID
  0x01,                         // one function section
  0, 0, 0, 0, 0, 0, 0,
  0x02,                         // first interface: XInput
  0x01,                         // interface count
  'X', 'U', 'S', 'B', '1', '0', 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0
};

// ===================================================================================
// String descriptors
// ===================================================================================
static __code uint16_t StrLang[]  = { (3 << 8) | 4, 0x0409 };
static __code uint16_t StrManuf[] = { (3 << 8) | (2 + 2 * 7), 'W','i','l','d','p','a','d' };
static __code uint16_t StrProd[]  = { (3 << 8) | (2 + 2 * 7), 'W','i','l','d','p','a','d' };
static __code uint16_t StrSer[]   = { (3 << 8) | (2 + 2 * 8), 'W','I','L','D','P','A','D','1' };
static __code uint16_t StrItf[]   = { (3 << 8) | (2 + 2 * 14),
  'W','i','l','d','p','a','d',' ','c','o','n','f','i','g' };
__code uint8_t* __code Strings[STR_COUNT] = {
  (__code uint8_t*)StrLang, (__code uint8_t*)StrManuf, (__code uint8_t*)StrProd,
  (__code uint8_t*)StrSer,  (__code uint8_t*)StrItf
};
