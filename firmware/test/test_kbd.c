// Simulator test for the keyboard-mode pipeline in src/main.c (default settings):
//  A) 30 fast CW + 5 fast CCW detents  -> 35 volume taps, press/release pairs
//  B) short press of key 2             -> play/pause tap on release (tap/hold)
//  C) hold key 2 (Fn) + 3 CW detents   -> 3 notches of mouse wheel (Fn layer)
// USB is replaced by stubs that log every report. Timer0 plays the user.

#include "ch554.h"
#include "input.h"
#include "usbdev.h"
#include "settings.h"

void fw_main(void);

// ---------------- USB stubs ----------------
uint8_t USB_personality;
volatile __bit USB_featureNew;
__xdata uint8_t USB_featureOut[FEATURE_REPORT_LEN];
__xdata uint8_t USB_featureIn[FEATURE_REPORT_LEN];

#define LOG_MAX 100
__xdata __at (0x0300) volatile uint8_t log_count;
__xdata __at (0x0301) volatile uint8_t log_buf[LOG_MAX][5];  // report id + 4 bytes

void USB_ISR(void) __interrupt(INT_NO_USB) { }
void USB_init(uint8_t p) { USB_personality = p; EA = 1; }   // real USB_init enables EA too
void USB_disconnect(void) { }
uint8_t USB_statusReady(void) { return 0; }
void USB_statusSend(__xdata uint8_t* buf, uint8_t len) { buf; len; }
uint8_t USB_padReady(void) { return 0; }
void USB_padSend(__xdata uint8_t* buf, uint8_t len) { buf; len; }

static uint8_t busy;                          // emulate 1 report per host poll
uint8_t USB_hidReady(void) {
  if(busy) { busy--; return 0; }
  return 1;
}

void done(void) {
  EA = 0;
  while(1);
}

void USB_hidSend(__xdata uint8_t* buf, uint8_t len) {
  uint8_t i;
  if(log_count < LOG_MAX) {
    for(i = 0; i < 5; i++) log_buf[log_count][i] = i < len ? buf[i] : 0;
    log_count++;
  }
  busy = 3;                                   // ~1 ms of main loop = one host poll
}

// ---------------- the user (Timer0, 1 ms ticks) ----------------
static __code uint8_t CW[4]  = {1, 0, 2, 3};
static __code uint8_t CCW[4] = {2, 0, 1, 3};
static uint8_t phase, detent, slow;
static uint16_t t;                            // ms since the script started

#define KEY2_DOWN()  (P1 &= ~0x80)            // key 2 = P1.7, active low
#define KEY2_UP()    (P1 |= 0x80)

void T0_ISR(void) __interrupt(INT_NO_TMR0) {
  TH0 = (65536 - 1000) >> 8;
  TL0 = (65536 - 1000) & 0xFF;
  t++;
  if(t < 200) return;                         // let the firmware boot

  if(detent < 35) {                           // A: one phase every 3 ms
    if(++slow < 3) return;
    slow = 0;
    P3 = 0xFC | (detent < 30 ? CW[phase] : CCW[phase]);
    if(++phase == 4) { phase = 0; detent++; }
    if(detent == 35) t = 1000;                // restart the clock for B and C
    return;
  }
  if(t == 1600) KEY2_DOWN();                  // B: 100 ms press (< 250 ms hold)
  if(t == 1700) KEY2_UP();
  if(t == 2200) KEY2_DOWN();                  // C: hold key 2 ...
  if(t >= 2700 && t < 2700 + 3 * 40) {        // ... and turn 3 detents, 10 ms per phase
    if((t - 2700) % 10 == 0) {
      P3 = 0xFC | CW[phase];
      if(++phase == 4) phase = 0;
    }
  }
  if(t == 3000) KEY2_UP();
  if(t == 3400) done();
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
