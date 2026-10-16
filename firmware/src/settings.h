// ===================================================================================
// Persistent settings (CH552 Data-Flash, 128 bytes)
// ===================================================================================
// This struct is exchanged byte for byte with the configurator, so its layout is part
// of the configuration protocol (docs/protocol.md). Keep tools/detent.py and
// tools/configurator.html in sync when changing it, and bump SETTINGS_VERSION.

#pragma once
#include <stdint.h>

#define SETTINGS_MAGIC      0xA5
#define SETTINGS_VERSION    2
#define SETTINGS_SIZE       104
#define MAX_BUTTONS         6           // layout room; the board uses BOARD_BUTTONS
#define N_SLOTS             (MAX_BUTTONS + 2)
#define SLOT_CW             MAX_BUTTONS     // knob clockwise
#define SLOT_CCW            (MAX_BUTTONS + 1)

// Device modes
#define MODE_KEYBOARD       0
#define MODE_WHEEL          1

// flags
#define FLAG_INVERT         0x01        // swap knob direction
#define FLAG_HALFSTEP       0x02        // encoder rests at every half quadrature cycle
#define FLAG_CLAMP          0x04        // wheel: hard stop at full lock (no wind-up)
#define FLAG_LEGACY_PAD     0x08        // wheel: plain Xbox 360 identity (no config/keys)
#define FLAG_NO_GESTURE     0x10        // disable "hold knob 3 s" mode switch

// LED modes
#define LED_OFF             0
#define LED_REACTIVE        1           // flash on key press / knob turn
#define LED_STATIC          2
#define LED_RAINBOW         3
#define LED_BREATHE         4

// Wheel axis
#define AXIS_LX             0
#define AXIS_LY             1
#define AXIS_RX             2
#define AXIS_RY             3
#define AXIS_TRIGGERS       4           // left of center -> LT, right -> RT

// Action types {type, p1, p2}
#define ACT_NONE            0
#define ACT_KEY             1           // p1 = modifier bits, p2 = HID keycode
#define ACT_CONSUMER        2           // p1 | p2 << 8 = consumer usage
#define ACT_MOUSE_BTN       3           // p1 = button bits (1 L, 2 R, 4 M, 8 back, 16 fwd)
#define ACT_WHEEL           4           // p1 = int8 vertical scroll
#define ACT_HWHEEL          5           // p1 = int8 horizontal scroll
#define ACT_PAD             6           // p1 = PAD_* (wheel mode)
#define ACT_RECENTER        7           // wheel mode: knob position becomes center
#define ACT_FN              8           // keyboard mode: Fn layer while held
#define ACT_MODE            9           // toggle keyboard / wheel mode

// Gamepad buttons for ACT_PAD
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

typedef struct {
  uint8_t type;
  uint8_t p1;
  uint8_t p2;
} action_t;

typedef struct {
  uint8_t  magic;                 // 0
  uint8_t  version;               // 1
  uint8_t  mode;                  // 2   MODE_*
  uint8_t  flags;                 // 3   FLAG_*
  uint8_t  steer_range;           // 4   detents from center to full lock (1..100)
  uint8_t  brightness;            // 5   0..255
  uint8_t  tap_ms;                // 6   key press length for knob steps / taps (1..50)
  uint8_t  debounce_ms;           // 7   1..30
  uint8_t  hold_10ms;             // 8   tap/hold threshold in 10 ms units (10..100)
  uint8_t  detents_per_rev;       // 9   informational, for the configurator (8..100)
  uint8_t  led_mode;              // 10  LED_*
  uint8_t  led_hue;               // 11  0..191 color wheel, 192..255 white
  uint8_t  axis;                  // 12  AXIS_*
  uint8_t  reserved0[3];          // 13
  action_t base[N_SLOTS];         // 16  keyboard mode: buttons, then CW, CCW
  action_t fn[N_SLOTS];           // 40  keyboard mode while an ACT_FN key is held
  action_t hold[MAX_BUTTONS];     // 64  keyboard mode: action when held (none = off)
  action_t wheel[MAX_BUTTONS];    // 82  wheel mode button actions
  uint8_t  reserved1[3];          // 100
  uint8_t  checksum;              // 103 sum of bytes 0..102
} settings_t;

extern __xdata settings_t cfg;

void SET_defaults(__xdata uint8_t* dst);
void SET_load(void);
void SET_save(void);
uint8_t SET_checksum(__xdata uint8_t* p);
uint8_t SET_valid(__xdata uint8_t* p);
