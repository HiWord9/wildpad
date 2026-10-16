// Simulator test for window switching with the knob, both ways:
//  A) knob press = Alt+Tab keeping Alt held (ACT_KEY_HOLD, sent right away with
//     ACT_NOW); hold knob + turn right = Tab, + turn left = Shift+Tab. These profile
//     combos must win over the factory all-profiles brightness combos.
//     Script: hold the knob, 2 clicks right, 1 left, release.
//  B) turn right = Alt+Tab, turn left = Alt+Shift+Tab, both ACT_KEY_HOLD: Alt stays
//     held between clicks and is let go once the knob stops (0.8 s).
//     Script: 2 clicks right, 1 left, wait.

#pragma nooverlay
#include "ch554.h"
#include "input.h"
#include "usbdev.h"
#include "settings.h"

void fw_main(void);

// ---------------- settings override (settings.c built with -DSET_load=SET_load_real)
void SET_load(void) {
  __xdata action_t* a;
  __xdata uint8_t* c;
  SET_defaults(cfg);
  a = ACTION(0, BOARD_KNOB_BUTTON);
  a->type = ACT_KEY_HOLD | ACT_NOW;
  a->p1 = 0x04;                                // Alt+Tab, Alt held
  a->p2 = 0x2B;
  a = ACTION(0, IN_RIGHT);
  a->type = ACT_KEY_HOLD; a->p1 = 0x04; a->p2 = 0x2B;           // Alt+Tab
  a = ACTION(0, IN_LEFT);
  a->type = ACT_KEY_HOLD; a->p1 = 0x06; a->p2 = 0x2B;           // Alt+Shift+Tab
  // the factory combos stay: hold knob + turn = brightness in all profiles; these
  // profile-1 combos with the same controls come later in the list and must win
  c = cfg + RECORDS_OFFSET + 5 * 4;            // after the 5 all-profiles combos
  c[0] = (BOARD_KNOB_BUTTON << 4) | IN_RIGHT; c[1] = ACT_KEY; c[2] = 0;    c[3] = 0x2B;   // Tab
  c += 4;
  c[0] = (BOARD_KNOB_BUTTON << 4) | IN_LEFT;  c[1] = ACT_KEY; c[2] = 0x02; c[3] = 0x2B;   // Shift+Tab
  cfg[S_CHECKSUM] = SET_checksum(cfg);
  SET_valid(cfg);
}

// ---------------- USB stubs ----------------
uint8_t USB_personality;
volatile __bit USB_featureNew;
__xdata uint8_t USB_featureOut[FEATURE_REPORT_LEN];
__xdata uint8_t USB_featureIn[FEATURE_REPORT_LEN];

#define LOG_MAX 30
__xdata __at (0x0300) volatile uint8_t log_count;
__xdata __at (0x0301) volatile uint8_t log_buf[LOG_MAX][2];   // modifiers, first key

void USB_ISR(void) __interrupt(INT_NO_USB) { }
void USB_init(uint8_t p) { USB_personality = p; EA = 1; }
void USB_disconnect(void) { }
uint8_t USB_statusReady(void) { return 0; }
void USB_statusSend(__xdata uint8_t* buf, uint8_t len) { buf; len; }
uint8_t USB_padReady(void) { return 0; }
void USB_padSend(__xdata uint8_t* buf, uint8_t len) { buf; len; }

static uint8_t busy;
uint8_t USB_hidReady(void) { if(busy) { busy--; return 0; } return 1; }

void done(void) {                             // the simulator stops at this breakpoint
  EA = 0;
  while(1);
}

void USB_hidSend(__xdata uint8_t* buf, uint8_t len) {
  len;
  if(buf[0] == REPORT_ID_KEYBOARD && log_count < LOG_MAX) {
    log_buf[log_count][0] = buf[1];
    log_buf[log_count][1] = buf[3];
    log_count++;
  }
  busy = 3;
}

// ---------------- the user (Timer0, 1 ms ticks) ----------------
static __code uint8_t CW[4]  = {1, 0, 2, 3};
static __code uint8_t CCW[4] = {2, 0, 1, 3};
static uint8_t phase, tick10;                 // no '%' in the ISR (see test_keys.c)
static uint16_t t;

void T0_ISR(void) __interrupt(INT_NO_TMR0) {
  TH0 = (65536 - 1000) >> 8;
  TL0 = (65536 - 1000) & 0xFF;
  t++;
  if(++tick10 == 10) tick10 = 0;
  if(t == 300) P3 &= ~0x08;                   // hold the knob
  if(t >= 400 && t < 480 && tick10 == 0) {          // 2 clicks right
    P3 = (P3 & 0xFC) | CW[phase];
    if(++phase == 4) phase = 0;
  }
  if(t >= 600 && t < 640 && tick10 == 0) {          // 1 click left
    P3 = (P3 & 0xFC) | CCW[phase];
    if(++phase == 4) phase = 0;
  }
  if(t == 800) P3 |= 0x08;                    // release the knob
  if(t >= 1100 && t < 1180 && tick10 == 0) {                // B: 2 clicks right
    P3 = (P3 & 0xFC) | CW[phase];
    if(++phase == 4) phase = 0;
  }
  if(t >= 1300 && t < 1340 && tick10 == 0) {                // 1 click left
    P3 = (P3 & 0xFC) | CCW[phase];
    if(++phase == 4) phase = 0;
  }
  if(t == 2600) done();
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
