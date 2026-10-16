// ===================================================================================
// Knob + key input, sampled by a 4 kHz timer interrupt
// ===================================================================================

#pragma nooverlay
#include "input.h"
#include "config.h"
#include "gpio.h"

// Timer2 in 16-bit auto-reload mode, clocked at Fsys/12
#define TICK_HZ       4000
#define T2_RELOAD     (65536 - (FREQ_SYS / 12 / TICK_HZ))

// Quarter-step transition table indexed by (previous_state << 2) | new_state,
// state = (A << 1) | B. +1 = clockwise (A leads), 0 = no move or invalid jump.
static __code int8_t QTAB[16] = {
   0, -1, +1,  0,
  +1,  0,  0, -1,
  -1,  0,  0, +1,
   0, +1, -1,  0
};

static volatile int16_t  enc_pos;         // detents
static volatile uint16_t ms;
static volatile uint8_t  buttons;
static uint8_t  enc_prev;
static int8_t   enc_acc;                  // quarter steps since last detent
static uint8_t  sub;
static __xdata uint8_t db_cnt[BOARD_BUTTONS];
static uint8_t  debounce = 5;
static __bit    halfstep;
static __bit    invert;

void INP_init(void) {
  enc_prev = ENC_PORT_STATE();
  T2CON  = 0;                             // timer, auto-reload, Fsys/12
  RCAP2  = T2_RELOAD;
  T2COUNT = T2_RELOAD;
  ET2    = 1;
  TR2    = 1;
}

void INP_setHalfstep(uint8_t on) { halfstep = on ? 1 : 0; }
void INP_setInvert(uint8_t on)   { invert = on ? 1 : 0; }

int16_t INP_position(void) {
  int16_t p;
  ET2 = 0; p = enc_pos; ET2 = 1;
  return p;
}

uint16_t INP_millis(void) {
  uint16_t m;
  ET2 = 0; m = ms; ET2 = 1;
  return m;
}

uint8_t INP_buttons(void) {
  return buttons;
}

void INP_setDebounce(uint8_t v) { debounce = v ? v : 1; }

void INP_ISR(void) __interrupt(INT_NO_TMR2) {
  uint8_t s, i, raw, bit;
  TF2 = 0;

  // --- quadrature decoder ---
  s = ENC_PORT_STATE();
  if(s != enc_prev) {
    enc_acc += QTAB[(enc_prev << 2) | s];
    enc_prev = s;
    // Detent positions: both phases high (and both low on half-step encoders).
    // Counting only at rest positions with a +-2 threshold rejects contact bounce
    // and re-synchronises the phase after every detent.
    if(s == 3 || (halfstep && s == 0)) {
      if(enc_acc >= 2)       { if(invert) enc_pos--; else enc_pos++; }
      else if(enc_acc <= -2) { if(invert) enc_pos++; else enc_pos--; }
      enc_acc = 0;
    }
  }

  // --- 1 ms housekeeping: millis + key debounce ---
  if(++sub < TICK_HZ / 1000) return;
  sub = 0;
  ms++;
  raw = BTN_READ();
  for(i = 0, bit = 1; i < BOARD_BUTTONS; i++, bit <<= 1) {
    if((raw ^ buttons) & bit) {
      if(++db_cnt[i] >= debounce) { buttons ^= bit; db_cnt[i] = 0; }
    }
    else db_cnt[i] = 0;
  }
}
