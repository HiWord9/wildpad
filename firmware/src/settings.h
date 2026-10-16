// ===================================================================================
// Persistent settings (CH552 Data-Flash, 128 bytes)
// ===================================================================================
// This block is exchanged byte for byte with the configurator, so its layout is part
// of the configuration protocol (docs/protocol.md). Keep tools/detent.py and
// tools/configurator.html in sync when changing it, and bump SETTINGS_VERSION.
//
// Inputs are numbered: buttons 0 .. BOARD_BUTTONS-1, then knob right, knob left.
// Every profile maps each input to one action. Combos are a shared list of
// "while input H is held, input T does action A" entries; H == T means
// "long press of H". A combo belongs to one profile or to all of them.

#pragma once
#include <stdint.h>
#include "config.h"

#define SETTINGS_MAGIC      0xA5
#define SETTINGS_VERSION    3
#define SETTINGS_SIZE       128

#define N_INPUTS            (BOARD_BUTTONS + 2)
#define IN_RIGHT            BOARD_BUTTONS           // knob turned clockwise
#define IN_LEFT             (BOARD_BUTTONS + 1)     // knob turned counter-clockwise

#define PROFILE_HEAD        4                       // r, g, b, light effect
#define PROFILE_SIZE        (PROFILE_HEAD + 3 * N_INPUTS)
#define PROFILES_OFFSET     12
#define COMBOS_OFFSET       (PROFILES_OFFSET + N_PROFILES * PROFILE_SIZE)
#define COMBO_SIZE          4
#define N_COMBOS            ((SETTINGS_SIZE - 1 - COMBOS_OFFSET) / COMBO_SIZE)

#if PROFILES_OFFSET + 3 * PROFILE_SIZE + 8 * COMBO_SIZE <= SETTINGS_SIZE - 1
  #define N_PROFILES        3
#elif PROFILES_OFFSET + 2 * PROFILE_SIZE + 6 * COMBO_SIZE <= SETTINGS_SIZE - 1
  #define N_PROFILES        2
#else
  #define N_PROFILES        1
#endif

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
#define S_CHECKSUM          (SETTINGS_SIZE - 1)

// S_FLAGS
#define FLAG_INVERT         0x01        // swap knob direction
#define FLAG_HALFSTEP       0x02        // encoder rests at every half quadrature cycle
#define FLAG_XBOX360        0x04        // appear as a plain Xbox 360 controller (no keys)

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
#define ACT_LAST            ACT_LIGHT
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

// Combo entry: [held << 4 | trigger, profile << 5 | type, p1, p2]
#define COMBO_EMPTY         0xFF        // first byte of an unused entry
#define COMBO_ALL_PROFILES  7

typedef struct {
  uint8_t type;
  uint8_t p1;
  uint8_t p2;
} action_t;

extern __xdata uint8_t cfg[SETTINGS_SIZE];

#define CFG_FLAGS           cfg[S_FLAGS]
#define PROFILE(p)          (cfg + PROFILES_OFFSET + (p) * PROFILE_SIZE)
#define ACTION(p, i)        ((__xdata action_t*)(PROFILE(p) + PROFILE_HEAD + 3 * (i)))
#define COMBO(n)            (cfg + COMBOS_OFFSET + COMBO_SIZE * (n))

void SET_defaults(__xdata uint8_t* dst);
void SET_load(void);
void SET_save(void);
uint8_t SET_checksum(__xdata uint8_t* p);
uint8_t SET_valid(__xdata uint8_t* p);
uint8_t SET_usesGamepad(__xdata uint8_t* p);
void SET_saveByte(uint8_t idx, uint8_t val);
