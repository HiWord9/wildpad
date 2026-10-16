// ===================================================================================
// Persistent settings (stored in the CH552 128-byte Data-Flash)
// ===================================================================================
// The settings block is exactly SETTINGS_SIZE bytes and is exchanged 1:1 with the
// configurator over a HID feature report, so the layout below IS the protocol.
// Keep tools/tishric.py and tools/configurator.html in sync when changing it.

#pragma once
#include <stdint.h>

#define SETTINGS_MAGIC      0xA5
#define SETTINGS_VERSION    1
#define SETTINGS_SIZE       30

// Device modes
#define MODE_KEYBOARD       0
#define MODE_GAMEPAD        1

// flags
#define FLAG_INVERT         0x01        // swap knob direction
#define FLAG_HALFSTEP       0x02        // encoder has a detent at every half quadrature cycle
#define FLAG_CLAMP          0x04        // gamepad: knob position stops at full lock (no wind-up)

// Inputs that carry an action (index into act[] / pad[])
#define IN_KEY1             0
#define IN_KEY2             1
#define IN_KEY3             2
#define IN_KNOB_SW          3
#define IN_KNOB_CW          4           // act[] only
#define IN_KNOB_CCW         5           // act[] only

// Keyboard-mode action types: {type, p1, p2}
#define ACT_NONE            0
#define ACT_KEY             1           // p1 = modifier bits, p2 = HID keycode
#define ACT_CONSUMER        2           // p1 | p2 << 8 = consumer usage (volume, media...)
#define ACT_MOUSE_BTN       3           // p1 = button bits (1 L, 2 R, 4 M, 8 back, 16 fwd)
#define ACT_WHEEL           4           // p1 = int8 vertical wheel amount
#define ACT_HWHEEL          5           // p1 = int8 horizontal wheel amount

// Gamepad-mode button codes for pad[]
#define PAD_NONE            0
#define PAD_A               1
#define PAD_B               2
#define PAD_X               3
#define PAD_Y               4
#define PAD_LB              5
#define PAD_RB              6
#define PAD_LT              7
#define PAD_RT              8
#define PAD_BACK            9
#define PAD_START           10
#define PAD_LS              11
#define PAD_RS              12
#define PAD_DUP             13
#define PAD_DDOWN           14
#define PAD_DLEFT           15
#define PAD_DRIGHT          16
#define PAD_GUIDE           17
#define PAD_RECENTER        18

typedef struct {
  uint8_t type;
  uint8_t p1;
  uint8_t p2;
} action_t;

typedef struct {
  uint8_t  magic;           // 0
  uint8_t  version;         // 1
  uint8_t  mode;            // 2  MODE_*
  uint8_t  flags;           // 3  FLAG_*
  uint8_t  steer_range;     // 4  detents from center to full lock (1..100)
  uint8_t  brightness;      // 5  LED brightness 0..255 (0 = off)
  uint8_t  tap_ms;          // 6  knob key press/release duration in keyboard mode (1..50)
  uint8_t  pad[4];          // 7  gamepad button for key1, key2, key3, knob switch
  action_t act[6];          // 11 keyboard-mode actions, see IN_*
  uint8_t  checksum;        // 29 sum of bytes 0..28 must equal this
} settings_t;

extern __xdata settings_t cfg;

void SET_defaults(void);
void SET_load(void);
void SET_save(void);
uint8_t SET_checksum(__xdata uint8_t* p);
uint8_t SET_valid(__xdata uint8_t* p);
