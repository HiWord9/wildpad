// ===================================================================================
// Persistent settings (CH552 Data-Flash, 128 bytes)
// ===================================================================================
// This block is exchanged byte for byte with the configurator, so its layout is part
// of the configuration protocol (docs/protocol.md). Keep tools/wildpad.py and
// tools/configurator.html in sync when changing it, and bump SETTINGS_VERSION.
//
// Inputs are numbered: buttons 0 .. nButtons-1, then knob right, knob left (boards
// with a knob). Every profile maps each input to one action. After the profiles comes
// a packed list of records, shared by everything that needs more room:
//  - combos: "while input H is held, input T does action A" (H == T: long press of
//    H), for one profile or all of them;
//  - sequences: lists of steps (tap an action, hold it while the input is held, wait,
//    and steps that run when the input is let go), used through ACT_SEQ.
//
// One firmware runs every supported board. The header names the board: a known one
// from the BOARDS table in settings.c (one byte), or 0 for a board described by a pin
// map stored right after the header. The profile size, and so how many profiles fit,
// follow from the board's number of inputs.

#pragma once
#include <stdint.h>
#include "config.h"

#define SETTINGS_MAGIC      0xA5
#define SETTINGS_VERSION    5
#define SETTINGS_SIZE       128

#define MAX_BUTTONS         8
#define MAX_LEDS            8
#define MAX_PROFILES        3
#define PROFILE_HEAD        4                       // r, g, b, light effect
#define HEADER_SIZE         13
#define RECORDS_END         (SETTINGS_SIZE - 1)

// Board map (BOARD_MAP_LEN bytes, in the BOARDS table or at S_MAP):
//  [0]     buttons (keys + knob switch, 1..8) | index of the knob switch << 4 (15: none)
//  [1]     LEDs (0..8) | MAP_LED_REVERSED | MAP_NO_KNOB
//  [2..9]  pin of button i: P1.x = x, P3.x = 8 + x (pressed = low)
// The WS2812 chain is on P3.4 and the knob phases on P3.1 (A) and P3.0 (B).
#define BOARD_MAP_LEN       10
#define MAP_LED_REVERSED    0x10        // chain runs from the last key to the first
#define MAP_NO_KNOB         0x20        // no rotary encoder
#define NO_KNOB_BUTTON      15
#define N_BOARDS            3
#define DEFAULT_BOARD       1

// Layout of the settings last checked by SET_layout() / SET_valid()
extern uint8_t boardId, nButtons, knobBtn, nLeds, nInputs, profOff, profSize, maxProf;
extern __xdata uint8_t boardMap[BOARD_MAP_LEN];
extern __bit hasKnob, ledReversed;
#define IN_RIGHT            nButtons                // knob turned clockwise
#define IN_LEFT             (nButtons + 1)          // knob turned counter-clockwise
#define RECORDS_AT(n)       ((uint8_t)(profOff + (uint8_t)((n) * profSize)))   // records after n profiles

// Header bytes
#define S_MAGIC             0
#define S_VERSION           1
#define S_FLAGS             2
#define S_BRIGHTNESS        3           // 0..255
#define S_HOLD_10MS         4           // long press / combo threshold, 10 ms units
#define S_DEBOUNCE_MS       5           // 1..30
#define S_TAP_MS            6           // length of a tapped key, 1..50
#define S_DETENTS           7           // clicks per turn (drawing only)
#define S_START_PROFILE     8           // profile after power-up
#define S_LED_SPEED         9           // 1 slow .. 10 fast
#define S_TURN_HOLD         10          // what a knob turn holds (held sequence steps) is let
                                        // go this long after the last click, 100 ms units
                                        // (0 = 800 ms)
#define S_PROFILES          11          // profiles in use, 1..maxProf (0 = all); the room of
                                        // unused profiles goes to combos and sequences
#define S_BOARD             12          // 1..N_BOARDS: known board; 0: pin map at S_MAP
#define S_MAP               13
#define S_CHECKSUM          (SETTINGS_SIZE - 1)

// S_FLAGS
#define FLAG_INVERT         0x01        // swap knob direction
#define FLAG_HALFSTEP       0x02        // encoder rests at every half quadrature cycle

// Light effects (per profile)
#define LED_OFF             0
#define LED_SOLID           1
#define LED_BREATHE         2
#define LED_RAINBOW         3
#define LED_PRESS           4           // keys light up while pressed, fade out
#define LED_STEERING        5           // show the knob axis position
#define LED_LAST            LED_STEERING

// Action types {type, p1, p2}
#define ACT_NONE            0
#define ACT_KEY             1           // p1 = modifier bits, p2 = HID keycode
#define ACT_MEDIA           2           // p1 | p2 << 8 = consumer usage
#define ACT_MOUSE_BTN       3           // p1 = button bits (1 L, 2 R, 4 M, 8 back, 16 fwd)
#define ACT_SCROLL          4           // p1 = int8 vertical, p2 = int8 horizontal
#define ACT_MOUSE_MOVE      5           // p1 = int8 x, p2 = int8 y
#define ACT_PAD_BTN         6           // p1 = PAD_*
#define ACT_AXIS            7           // knob only: p1 = AXIS_* | AXIS_FREE, p2 = clicks to full
#define ACT_CENTER          8           // knob axis position becomes center
#define ACT_PROFILE         9           // p1 = profile or PROF_NEXT / PROF_PREV, p2 = PROF_*
#define ACT_LIGHT           10          // p1 = LIGHT_*
#define ACT_SEQ             11          // p1 = sequence id
#define ACT_LAST            ACT_SEQ
#define ACT_TYPE_MASK       0x1F
#define ACT_NOW             0x80        // button's own action: send on press even when the
                                        // button starts combos (stays held, combos still work)

#define PROF_NEXT           0xF0
#define PROF_PREV           0xF1
#define PROF_SWITCH         0           // p2: switch and stay
#define PROF_WHILE_HELD     1           // p2: only while the input is held

#define LIGHT_TOGGLE        0
#define LIGHT_BRIGHTER      1
#define LIGHT_DIMMER        2

// Gamepad buttons for ACT_PAD_BTN
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

// Axes for ACT_AXIS (low bits of p1)
#define AXIS_LX             0
#define AXIS_LY             1
#define AXIS_RX             2
#define AXIS_RY             3
#define AXIS_TRIGGERS       4           // left of center -> LT, right -> RT
#define AXIS_MASK           0x0F
#define AXIS_FREE           0x80        // no hard stop: turning past full lock winds up

// Records, packed one after the other right after the profiles in use:
//  combo     [held << 4 | trigger, profile << 5 | type, p1, p2]      first byte < 0x80
//  sequence  [REC_SEQ | id, length, steps...]                          id 0..126
//  REC_END   end of the list (the rest is 0xFF)
#define REC_END             0xFF
#define REC_SEQ             0x80
#define COMBO_ALL_PROFILES  7

// Sequence steps; the first byte is op | x
#define STEP_TAP            0x00        // x = action type, then p1, p2: press and release
#define STEP_HOLD           0x20        // x = action type, then p1, p2: held until the input
                                        // is let go (a knob turn: until the knob stops)
#define STEP_WAIT           0x40        // x = 1..31: wait x * 20 ms (one byte)
#define STEP_RELEASE        0x60        // the steps after it run when the input is let go
#define STEP_OP             0xE0

typedef struct {
  uint8_t type;
  uint8_t p1;
  uint8_t p2;
} action_t;

extern __xdata uint8_t cfg[SETTINGS_SIZE];

#define CFG_FLAGS           cfg[S_FLAGS]
#define PROFILE(p)          (cfg + (uint8_t)(profOff + (uint8_t)((p) * profSize)))
#define ACTION(p, i)        ((__xdata action_t*)(PROFILE(p) + PROFILE_HEAD + 3 * (i)))


void SET_defaults(__xdata uint8_t* dst);
void SET_load(void);
void SET_save(void);
uint8_t SET_checksum(__xdata uint8_t* p);
uint8_t SET_layout(__xdata uint8_t* p);   // board and memory layout of p, 0 if bad
uint8_t SET_valid(__xdata uint8_t* p);    // also sets the layout of p
extern __xdata uint8_t SET_padUsed;       // set by SET_valid() / SET_defaults()
void SET_saveByte(uint8_t idx, uint8_t val);
