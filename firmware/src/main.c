// ===================================================================================
// TISHRIC Knob firmware for the CH552G "3 keys + 1 knob" macropad
// ===================================================================================
// Built on wagiminator's CH55x support code (CC BY-SA 3.0). Features:
//  - quadrature decoded in a 4 kHz timer interrupt -> no lost or reversed steps
//  - keyboard mode: every detent is queued and replayed as its own key tap
//  - gamepad mode: Xbox 360 (XInput) pad, knob = absolute steering on left stick X
//  - configuration over a vendor HID feature report (tools/configurator.html,
//    tools/tishric.py) and stored in Data-Flash
//
// Plug-in with key 1 held  -> bootloader (for flashing)
// Plug-in with key 3 held  -> keyboard mode for this session (rescue)
// Hold knob button 3 s     -> toggle keyboard / gamepad mode (saved)

#include "ch554.h"
#include "config.h"
#include "system.h"
#include "delay.h"
#include "gpio.h"
#include "neo.h"
#include "settings.h"
#include "input.h"
#include "usbdev.h"

// ===================================================================================
// Shared state
// ===================================================================================
#define CMD_READ        1
#define CMD_WRITE       2
#define CMD_SAVE        3
#define CMD_DEFAULTS    4
#define CMD_STATUS      5
#define CMD_RECENTER    6
#define CMD_REBOOT      7
#define CMD_BOOTLOADER  8

#define ST_OK           0
#define ST_BAD_CMD      1
#define ST_BAD_DATA     2

#define MODE_HOLD_MS    3000

static uint8_t  runMode;                  // mode actually running (may differ from cfg)
static uint8_t  btn, btnPrev;             // debounced buttons, now and last loop
static uint16_t now;
static int16_t  center;                   // knob position treated as zero
static uint16_t holdStart;
static __bit    holding;
static __bit    rebootPending;
static __bit    bootPending;

static void applySettings(void) {
  INP_setInvert(cfg.flags & FLAG_INVERT);
  INP_setHalfstep(cfg.flags & FLAG_HALFSTEP);
}

// ===================================================================================
// LEDs
// ===================================================================================
static __xdata uint8_t ledV[3];           // flash intensities (keyboard mode)
static uint16_t ledTime;

static uint8_t scale(uint8_t v) {
  return ((uint16_t)v * cfg.brightness) >> 8;
}

static void LED_show(uint8_t r0, uint8_t g0, uint8_t b0,
                     uint8_t r1, uint8_t g1, uint8_t b1,
                     uint8_t r2, uint8_t g2, uint8_t b2) {
  EA = 0;
  NEO_writeColor(scale(r0), scale(g0), scale(b0));
  NEO_writeColor(scale(r1), scale(g1), scale(b1));
  NEO_writeColor(scale(r2), scale(g2), scale(b2));
  EA = 1;
}

static void LED_flash(uint8_t i) {
  ledV[i] = 255;
}

static void LED_task(int16_t rel) {
  uint8_t i, p, l, r;
  if((uint16_t)(now - ledTime) < 20) return;
  ledTime = now;

  if(holding && (uint16_t)(now - holdStart) > 600) {        // mode-switch progress
    p = (uint16_t)(now - holdStart) / (MODE_HOLD_MS / 250);  // 0..250
    LED_show(p, p >> 1, 0, p, p >> 1, 0, p, p >> 1, 0);
    return;
  }

  if(runMode == MODE_GAMEPAD) {
    // steering bar: left LED for left lock, right LED for right lock, green at center
    if(rel < 0) { l = (uint16_t)(-rel) * 255 / cfg.steer_range; r = 0; }
    else        { r = (uint16_t)rel * 255 / cfg.steer_range;    l = 0; }
    if(rel == 0) LED_show(0, 0, 0,  0, 160, 0,  0, 0, 0);
    else         LED_show(l, 0, 0,  20, 20, 20,  r, 0, 0);
    return;
  }

  LED_show(ledV[0], ledV[0] >> 3, 0,  0, ledV[1], ledV[1] >> 2,  ledV[2] >> 2, 0, ledV[2]);
  for(i = 0; i < 3; i++) {
    if(ledV[i] > 24) ledV[i] -= ledV[i] >> 3;
    else ledV[i] = 0;
  }
}

// ===================================================================================
// Keyboard mode
// ===================================================================================
static __xdata uint8_t kbdRep[9]   = {REPORT_ID_KEYBOARD, 0, 0, 0, 0, 0, 0, 0, 0};
static __xdata uint8_t conRep[3]   = {REPORT_ID_CONSUMER, 0, 0};
static __xdata uint8_t mouseRep[6] = {REPORT_ID_MOUSE, 0, 0, 0, 0, 0};
static __xdata uint8_t statusRep[STATUS_REPORT_LEN];
static __xdata uint8_t newKbd[9];
static __bit kbdDirty, conDirty, mouseDirty, statusDirty;
static int8_t   wheelAcc, hwheelAcc;
static int8_t   tapInput = -1;            // knob action currently "pressed", -1 = none
static uint8_t  tapPhase;                 // 0 idle, 1 pressed, 2 released (gap)
static uint16_t tapTime;
static int16_t  reportedPos;              // last knob position turned into actions
static int16_t  statusPos;
static uint8_t  statusBtn = 0xFF;

static void addWheel(int8_t* acc, int8_t amount) {
  int16_t v = *acc + amount;
  if(v > 127) v = 127;
  if(v < -127) v = -127;
  *acc = v;
  mouseDirty = 1;
}

static void addAction(__xdata action_t* a, uint8_t* nkeys, uint16_t* con, uint8_t* mbtn) {
  uint8_t i;
  switch(a->type) {
    case ACT_KEY:
      newKbd[1] |= a->p1;
      if(a->p2) {
        for(i = 0; i < *nkeys; i++) if(newKbd[3 + i] == a->p2) return;
        if(*nkeys < 6) newKbd[3 + (*nkeys)++] = a->p2;
      }
      break;
    case ACT_CONSUMER:
      if(!*con) *con = a->p1 | ((uint16_t)a->p2 << 8);
      break;
    case ACT_MOUSE_BTN:
      *mbtn |= a->p1;
      break;
    default:
      break;
  }
}

// Rebuild keyboard/consumer/mouse-button state from held keys + active knob tap
static void KBD_compose(void) {
  uint8_t i, nkeys = 0, mbtn = 0;
  uint16_t con = 0;
  for(i = 0; i < 9; i++) newKbd[i] = 0;
  newKbd[0] = REPORT_ID_KEYBOARD;
  for(i = 0; i < 4; i++) {
    if(btn & (1 << i)) addAction(&cfg.act[i], &nkeys, &con, &mbtn);
  }
  if(tapInput >= 0) addAction(&cfg.act[tapInput], &nkeys, &con, &mbtn);

  for(i = 1; i < 9; i++) {
    if(kbdRep[i] != newKbd[i]) { kbdRep[i] = newKbd[i]; kbdDirty = 1; }
  }
  if(conRep[1] != (con & 0xFF) || conRep[2] != (con >> 8)) {
    conRep[1] = con & 0xFF;
    conRep[2] = con >> 8;
    conDirty = 1;
  }
  if(mouseRep[1] != mbtn) { mouseRep[1] = mbtn; mouseDirty = 1; }
}

static void KBD_send(void) {
  if(!USB_ep1Ready()) return;
  if(kbdDirty)        { USB_ep1Send(kbdRep, sizeof(kbdRep)); kbdDirty = 0; }
  else if(conDirty)   { USB_ep1Send(conRep, sizeof(conRep)); conDirty = 0; }
  else if(mouseDirty) {
    mouseRep[4] = wheelAcc;
    mouseRep[5] = hwheelAcc;
    wheelAcc = 0;
    hwheelAcc = 0;
    USB_ep1Send(mouseRep, sizeof(mouseRep));
    mouseDirty = 0;
  }
}

static void KBD_task(void) {
  uint8_t i, changed = btn ^ btnPrev;
  int16_t pos, delta;
  int8_t dir;
  __xdata action_t* a;

  // keys: held for as long as they are pressed
  if(changed) {
    for(i = 0; i < 4; i++) {
      if((changed & btn) & (1 << i)) {                       // pressed now
        if(i < 3) LED_flash(i);
        else { LED_flash(0); LED_flash(1); LED_flash(2); }
        a = &cfg.act[i];
        if(a->type == ACT_WHEEL)  addWheel(&wheelAcc,  (int8_t)a->p1);
        if(a->type == ACT_HWHEEL) addWheel(&hwheelAcc, (int8_t)a->p1);
      }
    }
    KBD_compose();
  }

  // knob: replay every detent from the ISR counter
  pos = INP_position();
  if(tapPhase == 0) {
    delta = pos - reportedPos;
    if(delta > 50)  { reportedPos = pos - 50; delta = 50; }  // limit backlog
    if(delta < -50) { reportedPos = pos + 50; delta = -50; }
    if(delta) {
      dir = delta > 0 ? 1 : -1;
      reportedPos += dir;
      i = dir > 0 ? IN_KNOB_CW : IN_KNOB_CCW;
      LED_flash(dir > 0 ? 2 : 0);
      a = &cfg.act[i];
      if(a->type == ACT_WHEEL)       addWheel(&wheelAcc,  (int8_t)a->p1);
      else if(a->type == ACT_HWHEEL) addWheel(&hwheelAcc, (int8_t)a->p1);
      else if(a->type != ACT_NONE) {
        tapInput = i;
        tapPhase = 1;
        tapTime = now;
        KBD_compose();
      }
    }
  }
  else if(!kbdDirty && !conDirty && !mouseDirty && (uint16_t)(now - tapTime) >= cfg.tap_ms) {
    if(tapPhase == 1) {                                      // press delivered -> release
      tapInput = -1;
      tapPhase = 2;
      tapTime = now;
      KBD_compose();
    }
    else tapPhase = 0;                                       // release delivered -> next
  }

  KBD_send();

  // live knob state for PC tools (vendor interface, EP3)
  pos -= center;
  if(pos != statusPos || btn != statusBtn) {
    statusPos = pos;
    statusBtn = btn;
    statusDirty = 1;
  }
  if(statusDirty && USB_ep3Ready()) {
    statusRep[0] = statusPos & 0xFF;
    statusRep[1] = statusPos >> 8;
    statusRep[2] = statusBtn;
    statusRep[3] = runMode;
    statusRep[4] = FW_VERSION;
    USB_ep3Send(statusRep, STATUS_REPORT_LEN);
    statusDirty = 0;
  }
}

// ===================================================================================
// Gamepad mode (XInput)
// ===================================================================================
static __xdata uint8_t padRep[20];
static __xdata uint8_t padNew[20];
static __bit padDirty;

static __code uint8_t PAD_BITS[17][2] = {      // {byte offset (2 or 3), bit mask}
  {0, 0},
  {3, 0x10}, {3, 0x20}, {3, 0x40}, {3, 0x80},  // A B X Y
  {3, 0x01}, {3, 0x02},                        // LB RB
  {0, 0},    {0, 0},                           // LT RT (analog, handled separately)
  {2, 0x20}, {2, 0x10},                        // Back Start
  {2, 0x40}, {2, 0x80},                        // LS RS
  {2, 0x01}, {2, 0x02}, {2, 0x04}, {2, 0x08},  // D-pad up down left right
};

static int16_t PAD_task(void) {
  uint8_t i, code;
  int16_t pos, rel;
  int16_t lx;
  uint8_t range = cfg.steer_range;

  // recenter on press of any input mapped to it
  for(i = 0; i < 4; i++) {
    if(cfg.pad[i] == PAD_RECENTER && (btn & ~btnPrev & (1 << i))) center = INP_position();
  }

  pos = INP_position();
  rel = pos - center;
  if(cfg.flags & FLAG_CLAMP) {                             // hard stop: no wind-up
    if(rel > range)  { center = pos - range; rel = range; }
    if(rel < -range) { center = pos + range; rel = -range; }
  }
  else {
    if(rel > range)  rel = range;
    if(rel < -range) rel = -range;
  }
  lx = (int16_t)((int32_t)rel * 32767 / range);

  for(i = 0; i < 20; i++) padNew[i] = 0;
  padNew[1] = 20;
  for(i = 0; i < 4; i++) {
    if(!(btn & (1 << i))) continue;
    code = cfg.pad[i];
    if(code == PAD_LT) padNew[4] = 255;
    else if(code == PAD_RT) padNew[5] = 255;
    else if(code == PAD_GUIDE) padNew[3] |= 0x04;
    else if(code > 0 && code <= PAD_DRIGHT) padNew[PAD_BITS[code][0]] |= PAD_BITS[code][1];
  }
  padNew[6] = lx & 0xFF;
  padNew[7] = lx >> 8;

  for(i = 0; i < 20; i++) {
    if(padRep[i] != padNew[i]) { padRep[i] = padNew[i]; padDirty = 1; }
  }
  if(padDirty && USB_ep1Ready()) {
    USB_ep1Send(padRep, 20);
    padDirty = 0;
  }
  return rel;
}

// ===================================================================================
// Configuration protocol (feature report on interface 1, keyboard mode)
// ===================================================================================
// request:  [cmd, seq, payload...]
// response: [cmd, seq, status, FW_VERSION, payload...]
static void CFG_task(void) {
  uint8_t i, cmd, seq, status = ST_OK;
  int16_t pos;
  __xdata uint8_t* c = (__xdata uint8_t*)&cfg;

  if(!USB_featureNew) return;
  cmd = USB_featureOut[0];
  seq = USB_featureOut[1];
  USB_featureIn[1] = 0xFF;                                  // invalidate while we work
  for(i = 2; i < FEATURE_REPORT_LEN; i++) USB_featureIn[i] = 0;

  switch(cmd) {
    case CMD_READ:
      for(i = 0; i < SETTINGS_SIZE; i++) USB_featureIn[4 + i] = c[i];
      break;
    case CMD_WRITE:
      if(SET_valid(&USB_featureOut[2])) {
        for(i = 0; i < SETTINGS_SIZE; i++) c[i] = USB_featureOut[2 + i];
        applySettings();
      }
      else status = ST_BAD_DATA;
      break;
    case CMD_SAVE:
      SET_save();
      break;
    case CMD_DEFAULTS:
      SET_defaults();
      applySettings();
      break;
    case CMD_STATUS:
      pos = INP_position() - center;
      USB_featureIn[4] = pos & 0xFF;
      USB_featureIn[5] = pos >> 8;
      USB_featureIn[6] = btn;
      USB_featureIn[7] = runMode;
      break;
    case CMD_RECENTER:
      center = INP_position();
      break;
    case CMD_REBOOT:
      rebootPending = 1;
      break;
    case CMD_BOOTLOADER:
      bootPending = 1;
      break;
    default:
      status = ST_BAD_CMD;
      break;
  }

  USB_featureIn[2] = status;
  USB_featureIn[3] = FW_VERSION;
  USB_featureIn[0] = cmd;
  USB_featureIn[1] = seq;                                   // written last: response valid
  USB_featureNew = 0;
}

static void reboot(void) {
  DLY_ms(50);                                               // let the control transfer finish
  USB_disconnect();
  DLY_ms(300);
  RST_now();
  while(1);
}

static void enterBootloader(void) {
  LED_show(255, 255, 255, 255, 255, 255, 255, 255, 255);
  DLY_ms(50);
  USB_disconnect();
  WDT_stop();
  ET2 = 0;
  TR2 = 0;
  DLY_ms(300);
  BOOT_now();
}

// ===================================================================================
// Main
// ===================================================================================
void main(void) {
  int16_t rel = 0;
  uint8_t i;

  NEO_init();
  if(!PIN_read(PIN_KEY1)) {                                 // key 1 at plug-in: bootloader
    NEO_latch();
    for(i = 9; i; i--) NEO_sendByte(127);
    BOOT_now();
  }

  CLK_config();
  DLY_ms(5);
  SET_load();
  applySettings();
  runMode = cfg.mode;
  if(!PIN_read(PIN_KEY3)) runMode = MODE_KEYBOARD;          // rescue: force keyboard mode

  INP_init();
  USB_init(runMode);
  WDT_start();

  btnPrev = btn = INP_buttons();
  center = reportedPos = INP_position();

  while(1) {
    WDT_reset();
    now = INP_millis();
    btn = INP_buttons();

    // hold the knob button alone for 3 s to switch keyboard <-> gamepad
    if(btn == BTN_KNOB) {
      if(!holding) { holding = 1; holdStart = now; }
      else if((uint16_t)(now - holdStart) >= MODE_HOLD_MS) {
        cfg.mode = (runMode == MODE_KEYBOARD) ? MODE_GAMEPAD : MODE_KEYBOARD;
        SET_save();
        rebootPending = 1;
      }
    }
    else holding = 0;

    if(runMode == MODE_GAMEPAD) rel = PAD_task();
    else {
      KBD_task();
      CFG_task();
    }

    if(rebootPending) reboot();
    if(bootPending) enterBootloader();

    LED_task(rel);
    btnPrev = btn;
  }
}
