// Simulator test for sequences: key 1 runs
//   tap gamepad A, wait 60 ms, tap A, hold RT while the key is held,
//   and when it is let go: tap keyboard 'x'.
// Only one profile is in use, so the records start right after it.
// Script: hold key 1 for 400 ms, let go.
// Expected gamepad reports: A, -, A, -, RT held, released; then keyboard 'x' tapped.

#pragma nooverlay
#include "ch552.h"
#include "input.h"
#include "usbdev.h"
#include "settings.h"

void fw_main(void);

// ---------------- settings override (settings.c built with -DSET_load=SET_load_real)
static __code uint8_t SEQ[] = {
  REC_SEQ | 0, 14,
  STEP_TAP | ACT_PAD_BTN, PAD_A, 0,
  STEP_WAIT | 3,
  STEP_TAP | ACT_PAD_BTN, PAD_A, 0,
  STEP_HOLD | ACT_PAD_BTN, PAD_RT, 0,
  STEP_RELEASE,
  STEP_TAP | ACT_KEY, 0, 0x1B,                // 'x'
  REC_END
};

void SET_load(void) {
  uint8_t i;
  __xdata action_t* a;
  cfg[S_BOARD] = DEFAULT_BOARD; SET_layout(cfg);
  SET_defaults(cfg);
  a = ACTION(0, 0);
  a->type = ACT_SEQ;
  a->p1 = 0;
  a->p2 = 0;
  cfg[S_PROFILES] = 1;                        // one profile: records start right after it
  for(i = RECORDS_AT(1); i < RECORDS_END; i++) cfg[i] = REC_END;
  for(i = 0; i < sizeof(SEQ); i++) cfg[RECORDS_AT(1) + i] = SEQ[i];
  cfg[S_CHECKSUM] = SET_checksum(cfg);
  SET_valid(cfg);
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
__xdata __at (0x0301) volatile uint8_t valid;             // settings accepted
__xdata __at (0x0302) volatile uint8_t log_buf[LOG_MAX][3];   // 'P' b3 RT  or  'K' mods key

void USB_ISR(void) __interrupt(INT_NO_USB) { }
void USB_init(uint8_t p) { USB_personality = p; EA = 1; }
void USB_disconnect(void) { }
uint8_t USB_statusReady(void) { return 0; }
void USB_statusSend(__xdata uint8_t* buf, uint8_t len) { buf; len; }

static uint8_t busy;
uint8_t USB_hidReady(void) { if(busy) { busy--; return 0; } return 1; }
uint8_t USB_padReady(void) { if(busy) { busy--; return 0; } return 1; }

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
  busy = 3;
}

void USB_padSend(__xdata uint8_t* buf, uint8_t len) {
  len;
  put('P', buf[3], buf[5]);
  busy = 3;
}

// ---------------- the user (Timer0, 1 ms ticks) ----------------
static uint16_t t;

void T0_ISR(void) __interrupt(INT_NO_TMR0) {
  TH0 = (65536 - 1000) >> 8;
  TL0 = (65536 - 1000) & 0xFF;
  t++;
  if(t == 300) P1 &= ~0x02;                   // key 1 (P1.1) down
  if(t == 700) P1 |= 0x02;                    // up
  if(t == 1000) done();
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
