// Simulator test for keys, knob, combos and profiles in src/main.c (factory settings):
//  A) 30 fast CW + 5 fast CCW clicks      -> 35 volume taps, press/release pairs
//  B) short press of the knob             -> mute, sent on release (knob starts combos)
//  C) hold knob + 3 CW clicks             -> brightness +48, no volume, no mute
//  D) hold knob + press key 2             -> profile 2
//  E) key 1 in profile 2                  -> Ctrl+C
//  F) 1 CW click in profile 2             -> one notch of scrolling down
// USB is replaced by stubs that log every report. Timer0 plays the user.

#pragma nooverlay                             // turnCW() is called from the timer ISR
#include "ch552.h"
#include "input.h"
#include "usbdev.h"
#include "settings.h"

void fw_main(void);

// ---------------- USB stubs ----------------
uint8_t USB_personality;
volatile uint8_t USB_stallTicks;
volatile __bit USB_updateGo;
volatile __bit USB_featureNew;
__xdata uint8_t USB_featureOut[FEATURE_REPORT_LEN];
__xdata uint8_t USB_featureIn[FEATURE_REPORT_LEN];

#define LOG_MAX 100
__xdata __at (0x0300) volatile uint8_t log_count;
__xdata __at (0x0301) volatile uint8_t brightness;            // at the end of the script
__xdata __at (0x0302) volatile uint8_t profile;               // from the last status report
__xdata __at (0x0303) volatile uint8_t personality;
__xdata __at (0x0304) volatile uint8_t log_buf[LOG_MAX][5];   // report id + 4 bytes

void USB_ISR(void) __interrupt(INT_NO_USB) { }
void USB_init(uint8_t p) { USB_personality = p; personality = p; EA = 1; }
void USB_disconnect(void) { }
uint8_t USB_statusReady(void) { return 1; }
void USB_statusSend(__xdata uint8_t* buf, uint8_t len) { len; profile = buf[3]; }
uint8_t USB_padReady(void) { return 0; }
void USB_padSend(__xdata uint8_t* buf, uint8_t len) { buf; len; }

static uint8_t busy;                          // emulate 1 report per host poll
uint8_t USB_hidReady(void) {
  if(busy) { busy--; return 0; }
  return 1;
}

void done(void) {                             // the simulator stops at this breakpoint
  EA = 0;
  while(1);
}

void USB_hidSend(__xdata uint8_t* buf, uint8_t len) {
  uint8_t i;
  if(log_count < LOG_MAX) {
    for(i = 0; i < 5; i++) log_buf[log_count][i] = i < len ? buf[i] : 0;
    if(buf[0] == REPORT_ID_KEYBOARD) {        // keep modifiers + first key
      log_buf[log_count][2] = buf[3];
      log_buf[log_count][3] = 0;
    }
    else if(buf[0] == REPORT_ID_MOUSE) {      // keep buttons + wheel
      log_buf[log_count][2] = buf[4];
      log_buf[log_count][3] = buf[5];
      log_buf[log_count][4] = buf[2];
    }
    log_count++;
  }
  busy = 3;                                   // ~1 ms of main loop = one host poll
}

// ---------------- the user (Timer0, 1 ms ticks) ----------------
static __code uint8_t CW[4]  = {1, 0, 2, 3};
static __code uint8_t CCW[4] = {2, 0, 1, 3};
static uint8_t phase, detent, slow;
static uint16_t t;                            // ms since the script started

#define KEY1(d)  (d ? (P1 &= ~0x02) : (P1 |= 0x02))     // P1.1, active low
#define KEY2(d)  (d ? (P1 &= ~0x80) : (P1 |= 0x80))     // P1.7
#define KNOB(d)  (d ? (P3 &= ~0x08) : (P3 |= 0x08))     // P3.3

// No '%' or '/' in the timer ISR: SDCC's library division routines are not reentrant
// and would corrupt the firmware's own divisions running in the main loop.
static uint8_t tick10;                        // counts 0..9, phase steps happen at 0
static void turnCW(uint16_t start, uint16_t end) {        // 10 ms per phase, 40 per click
  if(t >= start && t < end && tick10 == 0) {
    P3 = (P3 & 0xFC) | CW[phase];
    if(++phase == 4) phase = 0;
  }
}

void T0_ISR(void) __interrupt(INT_NO_TMR0) {
  TH0 = (65536 - 1000) >> 8;
  TL0 = (65536 - 1000) & 0xFF;
  t++;
  if(++tick10 == 10) tick10 = 0;
  if(t < 200) return;                         // let the firmware boot

  if(detent < 35) {                           // A: one phase every 3 ms
    if(++slow < 3) return;
    slow = 0;
    P3 = (P3 & 0xFC) | (detent < 30 ? CW[phase] : CCW[phase]);
    if(++phase == 4) { phase = 0; detent++; }
    if(detent == 35) t = 1000;                // restart the clock for the rest
    return;
  }
  if(t == 1600) KNOB(1);                      // B: 100 ms press
  if(t == 1700) KNOB(0);
  if(t == 2200) KNOB(1);                      // C: hold knob + 3 clicks
  turnCW(2500, 2500 + 3 * 40);
  if(t == 2900) KNOB(0);
  if(t == 3200) KNOB(1);                      // D: hold knob + key 2
  if(t == 3300) KEY2(1);
  if(t == 3400) KEY2(0);
  if(t == 3500) KNOB(0);
  if(t == 3800) KEY1(1);                      // E: key 1
  if(t == 3900) KEY1(0);
  turnCW(4100, 4100 + 1 * 40);                // F: one click
  brightness = cfg[S_BRIGHTNESS];
  if(t == 4400) done();
}

void main(void) {
  log_count = 0;
  P1 = 0xFF;
  P3 = 0xFF;
  TMOD = 0x01;
  TH0 = (65536 - 1000) >> 8;
  TL0 = (65536 - 1000) & 0xFF;
  ET0 = 1;
  TR0 = 1;
  fw_main();
}
