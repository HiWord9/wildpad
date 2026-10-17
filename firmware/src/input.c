// ===================================================================================
// Knob + key input, sampled by a 4 kHz timer interrupt
// ===================================================================================

#pragma nooverlay
#include "input.h"
#include "config.h"
#include "settings.h"
#include "usbdev.h"

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
static __xdata uint8_t db_cnt[MAX_BUTTONS];
static __xdata uint8_t pinMask[MAX_BUTTONS];      // bit of button i in its port
static uint8_t  onP3;                     // bit i: button i is on P3, else on P1
static uint8_t  nBtn;
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

void INP_setPins(__xdata uint8_t* pins, uint8_t n) {
  uint8_t i, bit;
  ET2 = 0;
  onP3 = 0;
  for(i = 0, bit = 1; i < n; i++, bit <<= 1) {
    pinMask[i] = 1 << (pins[i] & 7);
    if(pins[i] & 8) onP3 |= bit;
  }
  nBtn = n;
  ET2 = 1;
}

uint8_t INP_readRaw(void) {
  uint8_t i, bit, raw = 0, p1 = P1, p3 = P3;
  for(i = 0, bit = 1; i < nBtn; i++, bit <<= 1)
    if(!(((onP3 & bit) ? p3 : p1) & pinMask[i])) raw |= bit;   // pressed = low
  return raw;
}

void INP_ISR(void) __interrupt(INT_NO_TMR2) {
  uint8_t s, i, raw, bit;
  TF2 = 0;

  // Update layout: a stalled control request leaves endpoint 0 stalled until the next
  // SETUP has been handled, but the CH552 answers that request's next stage before;
  // when it comes quickly it is stalled too, and Windows gives up on the device (code
  // 10). The only stalled request there is the device qualifier, and the next one
  // follows ~9 ms later, so the stall goes after 5 ms. (Not in the other layouts:
  // their drivers may read a stalled request's data stage later than that, and an
  // endpoint that NAKs it then would hang the control pipe.)
  if(USB_stallTicks && !--USB_stallTicks && (UEP0_CTRL & MASK_UEP_T_RES) == UEP_T_RES_STALL)
    UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;

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
  raw = INP_readRaw();
  for(i = 0, bit = 1; i < nBtn; i++, bit <<= 1) {
    if((raw ^ buttons) & bit) {
      if(++db_cnt[i] >= debounce) { buttons ^= bit; db_cnt[i] = 0; }
    }
    else db_cnt[i] = 0;
  }
}
