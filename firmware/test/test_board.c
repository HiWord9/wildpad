// Simulator test for a board other than the default one: board 2, six keys in two
// rows plus a knob (keys on P3.2, P1.4, P1.5, P1.6, P1.7, P1.1, knob switch P3.3).
// Factory settings for that board, script:
//   key 1 (P3.2)  -> previous track       key 4 (P1.6) -> volume down
//   key 6 (P1.1)  -> volume up            hold knob + key 2 (P1.4) -> profile 2
//   key 4 in profile 2 -> Ctrl+X

#pragma nooverlay
#include "ch552.h"
#include "input.h"
#include "usbdev.h"
#include "settings.h"

void fw_main(void);

// ---------------- settings override (settings.c built with -DSET_load=SET_load_real)
__xdata __at (0x0301) volatile uint8_t valid;             // settings accepted
__xdata __at (0x0302) volatile uint8_t info[4];           // inputs, profiles, profile size, LEDs

void SET_load(void) {
  cfg[S_BOARD] = 2;
  SET_layout(cfg);
  SET_defaults(cfg);
  valid = SET_valid(cfg);
  info[0] = nInputs;
  info[1] = maxProf;
  info[2] = profSize;
  info[3] = nLeds;
}

// ---------------- USB stubs ----------------
uint8_t USB_personality;
volatile uint8_t USB_stallTicks;
volatile __bit USB_updateGo;
volatile __bit USB_featureNew;
__xdata uint8_t USB_featureOut[FEATURE_REPORT_LEN];
__xdata uint8_t USB_featureIn[FEATURE_REPORT_LEN];

#define LOG_MAX 24
__xdata __at (0x0300) volatile uint8_t log_count;
__xdata __at (0x0306) volatile uint8_t log_buf[LOG_MAX][3];   // 'C' usage lo hi  or  'K' mods key

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

static void put(uint8_t tag, uint8_t a, uint8_t b) {
  if(log_count >= LOG_MAX) return;
  log_buf[log_count][0] = tag;
  log_buf[log_count][1] = a;
  log_buf[log_count][2] = b;
  log_count++;
}

void USB_hidSend(__xdata uint8_t* buf, uint8_t len) {
  len;
  if(buf[0] == REPORT_ID_KEYBOARD) put('K', buf[1], buf[3]);
  if(buf[0] == REPORT_ID_CONSUMER) put('C', buf[1], buf[2]);
  busy = 3;
}

// ---------------- the user (Timer0, 1 ms ticks) ----------------
static uint16_t t;

void T0_ISR(void) __interrupt(INT_NO_TMR0) {
  TH0 = (65536 - 1000) >> 8;
  TL0 = (65536 - 1000) & 0xFF;
  t++;
  if(t == 300)  P3 &= ~0x04;                  // key 1 (P3.2)
  if(t == 400)  P3 |= 0x04;
  if(t == 500)  P1 &= ~0x40;                  // key 4 (P1.6)
  if(t == 600)  P1 |= 0x40;
  if(t == 700)  P1 &= ~0x02;                  // key 6 (P1.1)
  if(t == 800)  P1 |= 0x02;
  if(t == 900)  P3 &= ~0x08;                  // hold the knob (P3.3) ...
  if(t == 950)  P1 &= ~0x10;                  // ... and press key 2 (P1.4): profile 2
  if(t == 1050) P1 |= 0x10;
  if(t == 1100) P3 |= 0x08;
  if(t == 1300) P1 &= ~0x40;                  // key 4 in profile 2
  if(t == 1400) P1 |= 0x40;
  if(t == 1600) done();
}

void main(void) {
  log_count = 0;
  valid = 0;
  P1 = 0xFF;
  P3 = 0xFF;
  TMOD = 0x01;
  TH0 = (65536 - 1000) >> 8;
  TL0 = (65536 - 1000) & 0xFF;
  ET0 = 1;
  TR0 = 1;
  fw_main();
}
