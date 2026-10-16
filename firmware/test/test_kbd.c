// Simulator test for the keyboard-mode pipeline in src/main.c:
// knob detents -> timer ISR -> main loop tap queue -> HID reports.
// USB is replaced by stubs that log every report sent on EP1.
// Timer0 plays the role of a hand turning the knob.

#include "ch554.h"
#include "input.h"
#include "usbdev.h"
#include "settings.h"

void fw_main(void);

// ---------------- USB stubs ----------------
uint8_t USB_mode;
volatile __bit USB_featureNew;
__xdata uint8_t USB_featureOut[FEATURE_REPORT_LEN];
__xdata uint8_t USB_featureIn[FEATURE_REPORT_LEN];

#define LOG_MAX 100
__xdata __at (0x0200) volatile uint8_t log_count;
__xdata __at (0x0201) volatile uint8_t log_buf[LOG_MAX][3];  // report id, byte1, byte2

void USB_ISR(void) __interrupt(INT_NO_USB) { }
void USB_init(uint8_t mode) { USB_mode = mode; EA = 1; }   // real USB_init enables EA too
void USB_disconnect(void) { }
uint8_t USB_configured(void) { return 1; }
uint8_t USB_ep3Ready(void) { return 0; }
void USB_ep3Send(__xdata uint8_t* buf, uint8_t len) { buf; len; }

static uint8_t busy;                          // emulate 1 report per host poll
uint8_t USB_ep1Ready(void) {
  if(busy) { busy--; return 0; }
  return 1;
}

void done(void) {
  EA = 0;
  while(1);
}

void USB_ep1Send(__xdata uint8_t* buf, uint8_t len) {
  len;
  if(log_count < LOG_MAX) {
    log_buf[log_count][0] = buf[0];
    log_buf[log_count][1] = buf[1];
    log_buf[log_count][2] = buf[2];
    log_count++;
  }
  busy = 3;                                   // ~1 ms of main loop = one 1 ms host poll
  if(log_count >= 70) done();               // 35 taps = 70 reports
}

// ---------------- knob driver (Timer0) ----------------
static __code uint8_t CW[4]  = {1, 0, 2, 3};
static __code uint8_t CCW[4] = {2, 0, 1, 3};
static uint8_t phase, detent, slow, idle;

// Timer0 ISR every ~1 ms (12 MHz sim clock, 12 clocks/cycle): each phase 3 ms
// -> one detent per 12 ms -> ~83 detents/s, well beyond human "fast"
void T0_ISR(void) __interrupt(INT_NO_TMR0) {
  TH0 = (65536 - 1000) >> 8;
  TL0 = (65536 - 1000) & 0xFF;
  if(++slow < 3) return;
  slow = 0;
  if(detent >= 35) {                          // finished turning: give it 0.6 s, then stop
    if(++idle > 200) done();
    return;
  }
  P3 = 0xFC | (detent < 30 ? CW[phase] : CCW[phase]);
  if(++phase == 4) { phase = 0; detent++; }
}

void main(void) {
  log_count = 0;
  P3 = 0xFF;
  TMOD = 0x01;
  TH0 = (65536 - 1000) >> 8;
  TL0 = (65536 - 1000) & 0xFF;
  ET0 = 1;
  TR0 = 1;
  fw_main();
}
