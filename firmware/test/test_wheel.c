// Simulator test for wheel mode in src/main.c:
//  10 CW detents -> full right lock, 3 more -> stays (clamp), 20 CCW -> full left,
//  knob press -> re-center, key 1 -> RT, key 2 -> A, key 3 -> keyboard 'a'.
// Settings are the defaults with mode = wheel and key 3 mapped to a keyboard key.

#include "ch554.h"
#include "input.h"
#include "usbdev.h"
#include "settings.h"

void fw_main(void);

// ---------------- settings override (settings.c built with -DSET_load=SET_load_real)
void SET_load(void) {
  __xdata uint8_t* p = (__xdata uint8_t*)&cfg;
  SET_defaults(p);
  cfg.mode = MODE_WHEEL;
  cfg.wheel[2].type = ACT_KEY;
  cfg.wheel[2].p1 = 0;
  cfg.wheel[2].p2 = 0x04;                     // 'a'
  cfg.checksum = SET_checksum(p);
}

// ---------------- USB stubs ----------------
uint8_t USB_personality;
volatile __bit USB_featureNew;
__xdata uint8_t USB_featureOut[FEATURE_REPORT_LEN];
__xdata uint8_t USB_featureIn[FEATURE_REPORT_LEN];

#define LOG_MAX 90
__xdata __at (0x0300) volatile uint8_t pad_count;
__xdata __at (0x0301) volatile uint8_t kbd_count;
__xdata __at (0x0302) volatile uint8_t personality;
__xdata __at (0x0303) volatile uint8_t kbd_key;      // first non-zero key seen
__xdata __at (0x0304) volatile uint8_t pad_log[LOG_MAX][6];   // LX lo, LX hi, b2, b3, LT, RT

void USB_ISR(void) __interrupt(INT_NO_USB) { }
void USB_init(uint8_t p) { USB_personality = p; personality = p; EA = 1; }
void USB_disconnect(void) { }
uint8_t USB_statusReady(void) { return 0; }
void USB_statusSend(__xdata uint8_t* buf, uint8_t len) { buf; len; }

static uint8_t busy;
uint8_t USB_hidReady(void) { if(busy) { busy--; return 0; } return 1; }
uint8_t USB_padReady(void) { if(busy) { busy--; return 0; } return 1; }

void done(void) {
  EA = 0;
  while(1);
}

void USB_hidSend(__xdata uint8_t* buf, uint8_t len) {
  len;
  kbd_count++;
  if(buf[0] == 1 && buf[3] && !kbd_key) kbd_key = buf[3];
  busy = 3;
}

void USB_padSend(__xdata uint8_t* buf, uint8_t len) {
  len;
  if(pad_count < LOG_MAX) {
    pad_log[pad_count][0] = buf[6];
    pad_log[pad_count][1] = buf[7];
    pad_log[pad_count][2] = buf[2];
    pad_log[pad_count][3] = buf[3];
    pad_log[pad_count][4] = buf[4];
    pad_log[pad_count][5] = buf[5];
    pad_count++;
  }
  busy = 3;
}

// ---------------- the user (Timer0, 1 ms ticks) ----------------
static __code uint8_t CW[4]  = {1, 0, 2, 3};
static __code uint8_t CCW[4] = {2, 0, 1, 3};
static uint8_t phase, detent, slow;
static uint16_t t;

void T0_ISR(void) __interrupt(INT_NO_TMR0) {
  TH0 = (65536 - 1000) >> 8;
  TL0 = (65536 - 1000) & 0xFF;
  t++;
  if(t < 200) return;

  if(detent < 33) {                           // 13 CW then 20 CCW, 3 ms per phase
    if(++slow < 3) return;
    slow = 0;
    P3 = (P3 & 0xFC) | (detent < 13 ? CW[phase] : CCW[phase]);
    if(++phase == 4) { phase = 0; detent++; }
    if(detent == 33) t = 1000;
    return;
  }
  if(t == 1300) P3 &= ~0x08;                  // knob press: re-center
  if(t == 1400) P3 |= 0x08;
  if(t == 1600) P1 &= ~0x02;                  // key 1 (P1.1): RT
  if(t == 1700) P1 |= 0x02;
  if(t == 1900) P1 &= ~0x80;                  // key 2 (P1.7): A
  if(t == 2000) P1 |= 0x80;
  if(t == 2200) P1 &= ~0x40;                  // key 3 (P1.6): keyboard 'a'
  if(t == 2300) P1 |= 0x40;
  if(t == 2600) done();
}

void main(void) {
  pad_count = 0;
  kbd_count = 0;
  kbd_key = 0;
  P1 = 0xFF;
  P3 = 0xFF;
  TMOD = 0x01;
  TH0 = (65536 - 1000) >> 8;
  TL0 = (65536 - 1000) & 0xFF;
  ET0 = 1;
  TR0 = 1;
  fw_main();
}
