// Simulator test for the gamepad in src/main.c. Settings: profile 1 with
//   knob = left stick X, 10 clicks to full lock, hard stop; knob press = center;
//   key 1 = RT (long press: keyboard 'c'); key 2 = A; key 3 = keyboard 'a';
//   while key 3 is held, knob right = keyboard 'b'.
// Script:
//  13 CW clicks  -> 10 steps to full right lock, 3 more ignored (hard stop)
//  20 CCW clicks -> full left lock;  knob press -> center
//  short key 1 -> RT tap;  key 2 -> A
//  hold key 3 + 2 CW clicks -> 'b' 'b', stick stays centered, no 'a'
//  short key 3 -> 'a';  long key 1 -> 'c', no RT

#include "ch554.h"
#include "input.h"
#include "usbdev.h"
#include "settings.h"

void fw_main(void);

// ---------------- settings override (settings.c built with -DSET_load=SET_load_real)
static void put(uint8_t in, uint8_t type, uint8_t p1, uint8_t p2) {
  __xdata action_t* a = ACTION(0, in);
  a->type = type;
  a->p1 = p1;
  a->p2 = p2;
}

void SET_load(void) {
  uint8_t i;
  __xdata uint8_t* c;
  SET_defaults(cfg);
  put(0, ACT_PAD_BTN, PAD_RT, 0);
  put(1, ACT_PAD_BTN, PAD_A, 0);
  put(2, ACT_KEY, 0, 0x04);                   // 'a'
  put(3, ACT_CENTER, 0, 0);
  put(IN_RIGHT, ACT_AXIS, AXIS_LX, 10);
  put(IN_LEFT, ACT_NONE, 0, 0);
  for(i = RECORDS_OFFSET; i < RECORDS_END; i++) cfg[i] = REC_END;
  c = cfg + RECORDS_OFFSET;
  c[0] = 0x00; c[1] = ACT_KEY; c[2] = 0; c[3] = 0x06;                  // long key 1: 'c'
  c += 4;
  c[0] = (2 << 4) | IN_RIGHT; c[1] = ACT_KEY; c[2] = 0; c[3] = 0x05;   // key 3 + right: 'b'
  cfg[S_CHECKSUM] = SET_checksum(cfg);
  SET_valid(cfg);                             // notes that the gamepad is used
}

// ---------------- USB stubs ----------------
uint8_t USB_personality;
volatile __bit USB_featureNew;
__xdata uint8_t USB_featureOut[FEATURE_REPORT_LEN];
__xdata uint8_t USB_featureIn[FEATURE_REPORT_LEN];

#define LOG_MAX 60
#define KEY_MAX 16
__xdata __at (0x0300) volatile uint8_t pad_count;
__xdata __at (0x0301) volatile uint8_t key_count;
__xdata __at (0x0302) volatile uint8_t personality;
__xdata __at (0x0303) volatile uint8_t key_log[KEY_MAX];     // first key of each report
__xdata __at (0x0313) volatile uint8_t pad_log[LOG_MAX][6];  // LX lo, LX hi, b2, b3, LT, RT

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
  if(buf[0] == REPORT_ID_KEYBOARD && key_count < KEY_MAX) key_log[key_count++] = buf[3];
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
static uint8_t phase, detent, slow, tick10;   // no '%' in the ISR (see test_keys.c)
static uint16_t t;

#define KEY1(d)  (d ? (P1 &= ~0x02) : (P1 |= 0x02))     // P1.1, active low
#define KEY2(d)  (d ? (P1 &= ~0x80) : (P1 |= 0x80))     // P1.7
#define KEY3(d)  (d ? (P1 &= ~0x40) : (P1 |= 0x40))     // P1.6
#define KNOB(d)  (d ? (P3 &= ~0x08) : (P3 |= 0x08))     // P3.3

void T0_ISR(void) __interrupt(INT_NO_TMR0) {
  TH0 = (65536 - 1000) >> 8;
  TL0 = (65536 - 1000) & 0xFF;
  t++;
  if(++tick10 == 10) tick10 = 0;
  if(t < 200) return;

  if(detent < 33) {                           // 13 CW then 20 CCW, 6 ms per phase
    if(++slow < 6) return;
    slow = 0;
    P3 = (P3 & 0xFC) | (detent < 13 ? CW[phase] : CCW[phase]);
    if(++phase == 4) { phase = 0; detent++; }
    if(detent == 33) t = 1000;
    return;
  }
  if(t == 1300) KNOB(1);                      // center
  if(t == 1400) KNOB(0);
  if(t == 1600) KEY1(1);                      // short key 1: RT tap
  if(t == 1700) KEY1(0);
  if(t == 1900) KEY2(1);                      // key 2: A
  if(t == 2000) KEY2(0);
  if(t == 2200) KEY3(1);                      // hold key 3 + 2 CW clicks
  if(t >= 2300 && t < 2380 && tick10 == 0) {
    P3 = (P3 & 0xFC) | CW[phase];
    if(++phase == 4) phase = 0;
  }
  if(t == 2500) KEY3(0);
  if(t == 2800) KEY3(1);                      // short key 3: 'a'
  if(t == 2900) KEY3(0);
  if(t == 3200) KEY1(1);                      // long key 1: 'c'
  if(t == 3700) KEY1(0);
  if(t == 4000) done();
}

void main(void) {
  pad_count = 0;
  key_count = 0;
  P1 = 0xFF;
  P3 = 0xFF;
  TMOD = 0x01;
  TH0 = (65536 - 1000) >> 8;
  TL0 = (65536 - 1000) & 0xFF;
  ET0 = 1;
  TR0 = 1;
  fw_main();
}
