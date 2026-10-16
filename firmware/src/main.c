// ===================================================================================
// Detent - firmware for CH552 "keys + knob" macropads
// ===================================================================================
// Built on wagiminator's CH55x support code (CC BY-SA 3.0). Features:
//  - quadrature decoded in a 4 kHz timer interrupt -> no lost or reversed steps
//  - keyboard mode: keys / media / mouse actions, Fn layer, tap vs. hold
//  - wheel mode: XInput gamepad, knob = absolute steering axis, keys can still
//    send keyboard actions
//  - configuration over a vendor HID feature report, stored in Data-Flash
//
// Plug in holding button BOARD_BOOT_BUTTON   -> USB bootloader
// Plug in holding button BOARD_RESCUE_BUTTON -> keyboard mode for this session
// Hold the knob button alone for 3 s         -> toggle keyboard / wheel mode

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
#define MODE_HOLD_MS    3000

static uint8_t  runMode;                  // mode actually running (may differ from cfg)
static uint8_t  btn;                      // debounced buttons
static uint8_t  handled;                  // buttons whose press has been processed
static uint16_t now;
static int16_t  pos;                      // knob position (detents)
static int16_t  center;                   // knob position treated as zero
static int16_t  rel;                      // pos - center, clamped in wheel mode
static uint16_t gestureStart;
static __bit    gesture;
static __bit    rebootPending;
static __bit    bootPending;

#define BTN_BIT(i)  (1 << (i))

static void applySettings(void) {
  INP_setInvert(cfg.flags & FLAG_INVERT);
  INP_setHalfstep(cfg.flags & FLAG_HALFSTEP);
  INP_setDebounce(cfg.debounce_ms);
}

static void toggleMode(void) {
  cfg.mode = (runMode == MODE_KEYBOARD) ? MODE_WHEEL : MODE_KEYBOARD;
  SET_save();
  rebootPending = 1;
}

// ===================================================================================
// HID reports (keyboard / consumer / mouse) - used in keyboard and wheel mode
// ===================================================================================
static __xdata uint8_t kbdRep[9]   = {REPORT_ID_KEYBOARD, 0, 0, 0, 0, 0, 0, 0, 0};
static __xdata uint8_t conRep[3]   = {REPORT_ID_CONSUMER, 0, 0};
static __xdata uint8_t mouseRep[6] = {REPORT_ID_MOUSE, 0, 0, 0, 0, 0};
static __xdata uint8_t newKbd[9];
static __bit kbdDirty, conDirty, mouseDirty;
static int8_t wheelAcc, hwheelAcc;

// Action currently held by each button, and by the knob step being replayed
static __xdata action_t active[MAX_BUTTONS];
static __xdata action_t knobAct;

#define BS_IDLE     0                     // nothing going on
#define BS_PENDING  1                     // pressed, waiting to see tap or hold
#define BS_HELD     2                     // action in active[] held until release
#define BS_TAP      3                     // tap action held briefly, then released
static __xdata uint8_t  bstate[MAX_BUTTONS];
static __xdata uint16_t btime[MAX_BUTTONS];
static __xdata action_t pendTap[MAX_BUTTONS];
static uint8_t  fnHeld;                   // number of held Fn actions

static __bit hidBusy(void) {
  return kbdDirty || conDirty || mouseDirty;
}

static void addScroll(int8_t* acc, int8_t amount) {
  int16_t v = *acc + amount;
  if(v > 127) v = 127;
  if(v < -127) v = -127;
  *acc = v;
  mouseDirty = 1;
}

static void addToReport(__xdata action_t* a, uint8_t* nkeys, uint16_t* con, uint8_t* mbtn) {
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

static void HID_compose(void) {
  uint8_t i, nkeys = 0, mbtn = 0;
  uint16_t con = 0;
  for(i = 0; i < 9; i++) newKbd[i] = 0;
  for(i = 0; i < BOARD_BUTTONS; i++) addToReport(&active[i], &nkeys, &con, &mbtn);
  addToReport(&knobAct, &nkeys, &con, &mbtn);

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

static void HID_send(void) {
  if(!USB_hidReady()) return;
  if(kbdDirty)        { USB_hidSend(kbdRep, sizeof(kbdRep)); kbdDirty = 0; }
  else if(conDirty)   { USB_hidSend(conRep, sizeof(conRep)); conDirty = 0; }
  else if(mouseDirty) {
    mouseRep[4] = wheelAcc;
    mouseRep[5] = hwheelAcc;
    wheelAcc = 0;
    hwheelAcc = 0;
    USB_hidSend(mouseRep, sizeof(mouseRep));
    mouseDirty = 0;
  }
}

static void clearAction(__xdata action_t* a) {
  a->type = ACT_NONE;
}

static void copyAction(__xdata action_t* dst, __xdata action_t* src) {
  dst->type = src->type;
  dst->p1 = src->p1;
  dst->p2 = src->p2;
}

static uint8_t isHeldType(uint8_t t) {
  return t == ACT_KEY || t == ACT_CONSUMER || t == ACT_MOUSE_BTN || t == ACT_PAD || t == ACT_FN;
}

// ===================================================================================
// LEDs
// ===================================================================================
static __xdata uint8_t ledV[NEO_COUNT];   // flash levels for the reactive effect
static uint16_t ledTime;
static uint8_t  ledPhase;

static uint8_t scale(uint8_t v) {
  return ((uint16_t)v * cfg.brightness) >> 8;
}

static void LED_flash(uint8_t i) {
  if(i < NEO_COUNT) ledV[i] = 255;
}

static __xdata uint8_t rgb[NEO_COUNT * 3];

// Set LED i to hue (0..191 color wheel, >= 192 white) at level 0..255
static void LED_put(uint8_t i, uint8_t hue, uint8_t level) {
  uint8_t r, g, b, step;
  __xdata uint8_t* o = rgb + i * 3;
  if(hue >= 192) { r = g = b = 255; }
  else {
    step = (hue & 63) << 2;
    switch(hue >> 6) {
      case 0:  r = 255 - step; g = step;       b = 0;          break;
      case 1:  r = 0;          g = 255 - step; b = step;       break;
      default: r = step;       g = 0;          b = 255 - step; break;
    }
  }
  level = scale(level);
  o[0] = ((uint16_t)r * level) >> 8;
  o[1] = ((uint16_t)g * level) >> 8;
  o[2] = ((uint16_t)b * level) >> 8;
}

static void LED_rgb(uint8_t i, uint8_t r, uint8_t g, uint8_t b) {
  __xdata uint8_t* o = rgb + i * 3;
  o[0] = r; o[1] = g; o[2] = b;
}

static void LED_task(void) {
  uint8_t i, lvl, side, hue = cfg.led_hue;
  if((uint16_t)(now - ledTime) < 20) return;
  ledTime = now;
  ledPhase++;

  if(gesture && (uint16_t)(now - gestureStart) > 600) {        // mode-switch progress
    lvl = (uint16_t)(now - gestureStart) / (MODE_HOLD_MS / 250);
    for(i = 0; i < NEO_COUNT; i++) LED_rgb(i, lvl, lvl >> 1, 0);
  }
  else if(cfg.led_mode == LED_OFF) {
    for(i = 0; i < NEO_COUNT; i++) LED_rgb(i, 0, 0, 0);
  }
  else if(runMode == MODE_WHEEL && NEO_COUNT >= 3) {
    // steering bar: outer LEDs grow with steering, middle lit at center
    side = (uint16_t)(rel < 0 ? -rel : rel) * 255 / cfg.steer_range;
    LED_put(0, hue, rel < 0 ? side : 0);
    LED_put(1, hue, rel == 0 ? 255 : 24);
    LED_put(2, hue, rel > 0 ? side : 0);
  }
  else {
    for(i = 0; i < NEO_COUNT; i++) {
      switch(cfg.led_mode) {
        case LED_STATIC:  LED_put(i, hue, 255); break;
        case LED_RAINBOW: LED_put(i, (uint8_t)(ledPhase + i * 64) % 192, 255); break;
        case LED_BREATHE:
          lvl = ledPhase & 0x7F;
          LED_put(i, hue, (lvl < 64 ? lvl : 127 - lvl) << 2);
          break;
        default:          LED_put(i, hue, ledV[i]); break;  // reactive
      }
    }
  }

  EA = 0;                                                      // WS2812 timing is strict
  for(i = 0; i < NEO_COUNT; i++) NEO_writeColor(rgb[i * 3], rgb[i * 3 + 1], rgb[i * 3 + 2]);
  EA = 1;

  for(i = 0; i < NEO_COUNT; i++) {
    if(ledV[i] > 24) ledV[i] -= ledV[i] >> 3;
    else ledV[i] = 0;
  }
}

// ===================================================================================
// Buttons
// ===================================================================================
// Start an action for button i (press, or hold threshold reached)
static void startAction(uint8_t i, __xdata action_t* a) {
  bstate[i] = BS_HELD;
  switch(a->type) {
    case ACT_WHEEL:    addScroll(&wheelAcc,  (int8_t)a->p1); break;
    case ACT_HWHEEL:   addScroll(&hwheelAcc, (int8_t)a->p1); break;
    case ACT_MODE:     toggleMode(); break;
    case ACT_RECENTER: center = pos; break;
    case ACT_FN:
      if(runMode == MODE_KEYBOARD) { fnHeld++; copyAction(&active[i], a); }
      break;
    case ACT_PAD:
      if(runMode == MODE_WHEEL) copyAction(&active[i], a);
      break;
    default:
      if(isHeldType(a->type)) copyAction(&active[i], a);
      break;
  }
}

static void releaseAction(uint8_t i) {
  if(active[i].type == ACT_FN && fnHeld) fnHeld--;
  clearAction(&active[i]);
  bstate[i] = BS_IDLE;
}

static void BTN_task(void) {
  uint8_t i, bit;
  __xdata action_t* a;
  __xdata action_t* h;

  for(i = 0, bit = 1; i < BOARD_BUTTONS; i++, bit <<= 1) {
    switch(bstate[i]) {
      case BS_IDLE:
        if((btn & bit) && !(handled & bit)) {                  // new press
          handled |= bit;
          if(i == BOARD_KNOB_BUTTON) { LED_flash(0); LED_flash(1); LED_flash(2); }
          else LED_flash(i);
          if(runMode == MODE_WHEEL) {
            startAction(i, &cfg.wheel[i]);
            break;
          }
          a = fnHeld ? &cfg.fn[i] : &cfg.base[i];
          h = &cfg.hold[i];
          if(h->type != ACT_NONE) {                            // tap/hold: decide later
            copyAction(&pendTap[i], a);
            bstate[i] = BS_PENDING;
            btime[i] = now;
          }
          else startAction(i, a);
        }
        else if(!(btn & bit)) handled &= ~bit;
        break;

      case BS_PENDING:
        if(!(btn & bit)) {                                     // released early: tap
          handled &= ~bit;
          a = &pendTap[i];
          if(isHeldType(a->type) && a->type != ACT_FN && a->type != ACT_PAD) {
            copyAction(&active[i], a);
            bstate[i] = BS_TAP;
            btime[i] = now;
          }
          else {
            startAction(i, a);                                 // one-shot actions
            releaseAction(i);
          }
        }
        else if((uint16_t)(now - btime[i]) >= (uint16_t)cfg.hold_10ms * 10) {
          startAction(i, &cfg.hold[i]);                        // held long enough
        }
        break;

      case BS_HELD:
        if(!(btn & bit)) {
          handled &= ~bit;
          releaseAction(i);
        }
        break;

      case BS_TAP:                                             // release after delivery
        if(!hidBusy() && (uint16_t)(now - btime[i]) >= cfg.tap_ms) releaseAction(i);
        break;
    }
  }
}

// ===================================================================================
// Knob in keyboard mode: replay every detent as its own action
// ===================================================================================
static int16_t  reportedPos;
static uint8_t  knobPhase;                // 0 idle, 1 pressed, 2 released (gap)
static uint16_t knobTime;

static void KNOB_task(void) {
  int16_t delta;
  int8_t dir;
  __xdata action_t* a;

  if(knobPhase == 0) {
    delta = pos - reportedPos;
    if(delta > 50)  { reportedPos = pos - 50; delta = 50; }    // limit backlog
    if(delta < -50) { reportedPos = pos + 50; delta = -50; }
    if(!delta) return;
    dir = delta > 0 ? 1 : -1;
    reportedPos += dir;
    LED_flash(dir > 0 ? NEO_COUNT - 1 : 0);
    a = fnHeld ? &cfg.fn[dir > 0 ? SLOT_CW : SLOT_CCW] : &cfg.base[dir > 0 ? SLOT_CW : SLOT_CCW];
    if(a->type == ACT_WHEEL)       addScroll(&wheelAcc,  (int8_t)a->p1);
    else if(a->type == ACT_HWHEEL) addScroll(&hwheelAcc, (int8_t)a->p1);
    else if(a->type == ACT_KEY || a->type == ACT_CONSUMER || a->type == ACT_MOUSE_BTN) {
      copyAction(&knobAct, a);
      knobPhase = 1;
      knobTime = now;
    }
  }
  else if(!hidBusy() && (uint16_t)(now - knobTime) >= cfg.tap_ms) {
    if(knobPhase == 1) {                                       // press delivered -> release
      clearAction(&knobAct);
      knobPhase = 2;
      knobTime = now;
    }
    else knobPhase = 0;                                        // release delivered -> next
  }
}

// ===================================================================================
// Wheel mode: XInput report
// ===================================================================================
static __xdata uint8_t padRep[XINPUT_REPORT_LEN];
static __xdata uint8_t padNew[XINPUT_REPORT_LEN];
static __bit padDirty;

static __code uint8_t PAD_BITS[17][2] = {      // {byte offset, bit mask}
  {0, 0},
  {3, 0x10}, {3, 0x20}, {3, 0x40}, {3, 0x80},  // A B X Y
  {3, 0x01}, {3, 0x02},                        // LB RB
  {0, 0},    {0, 0},                           // LT RT (analog, handled separately)
  {2, 0x20}, {2, 0x10},                        // Back Start
  {2, 0x40}, {2, 0x80},                        // LS RS
  {2, 0x01}, {2, 0x02}, {2, 0x04}, {2, 0x08},  // D-pad up down left right
};

static void PAD_task(void) {
  uint8_t i, code, range = cfg.steer_range, t;
  int16_t v;

  rel = pos - center;
  if(cfg.flags & FLAG_CLAMP) {                                 // hard stop: no wind-up
    if(rel > range)  { center = pos - range; rel = range; }
    if(rel < -range) { center = pos + range; rel = -range; }
  }
  else {
    if(rel > range)  rel = range;
    if(rel < -range) rel = -range;
  }
  v = (int16_t)((int32_t)rel * 32767 / range);

  for(i = 0; i < XINPUT_REPORT_LEN; i++) padNew[i] = 0;
  padNew[1] = XINPUT_REPORT_LEN;
  for(i = 0; i < BOARD_BUTTONS; i++) {
    if(active[i].type != ACT_PAD) continue;
    code = active[i].p1;
    if(code == PAD_LT) padNew[4] = 255;
    else if(code == PAD_RT) padNew[5] = 255;
    else if(code == PAD_GUIDE) padNew[3] |= 0x04;
    else if(code > 0 && code <= PAD_DRIGHT) padNew[PAD_BITS[code][0]] |= PAD_BITS[code][1];
  }
  if(cfg.axis == AXIS_TRIGGERS) {
    t = (uint16_t)(rel < 0 ? -rel : rel) * 255 / range;
    if(rel < 0 && t > padNew[4]) padNew[4] = t;
    if(rel > 0 && t > padNew[5]) padNew[5] = t;
  }
  else {
    i = 6 + (cfg.axis << 1);                                   // LX, LY, RX, RY
    padNew[i] = v & 0xFF;
    padNew[i + 1] = v >> 8;
  }

  for(i = 0; i < XINPUT_REPORT_LEN; i++) {
    if(padRep[i] != padNew[i]) { padRep[i] = padNew[i]; padDirty = 1; }
  }
  if(padDirty && USB_padReady()) {
    USB_padSend(padRep, XINPUT_REPORT_LEN);
    padDirty = 0;
  }
}

// ===================================================================================
// Live state for PC tools (vendor interface, EP3)
// ===================================================================================
static __xdata uint8_t statusRep[STATUS_REPORT_LEN];
static int16_t statusPos;
static uint8_t statusBtn = 0xFF, statusFn = 0xFF;

static void STATUS_task(void) {
  int16_t p = pos - center;
  uint8_t fn = fnHeld ? 1 : 0;
  if(p == statusPos && btn == statusBtn && fn == statusFn) return;
  if(!USB_statusReady()) return;
  statusPos = p;
  statusBtn = btn;
  statusFn = fn;
  statusRep[0] = p & 0xFF;
  statusRep[1] = p >> 8;
  statusRep[2] = btn;
  statusRep[3] = runMode;
  statusRep[4] = fn;
  USB_statusSend(statusRep, STATUS_REPORT_LEN);
}

// ===================================================================================
// Configuration protocol (feature report on interface 1), see docs/protocol.md
// ===================================================================================
// request:  [cmd, seq, payload...]
// response: [cmd, seq, status, PROTOCOL_VERSION, payload...]
#define CMD_INFO        1
#define CMD_READ        2
#define CMD_WRITE       3
#define CMD_COMMIT      4
#define CMD_SAVE        5
#define CMD_DEFAULTS    6
#define CMD_STATUS      7
#define CMD_RECENTER    8
#define CMD_REBOOT      9
#define CMD_BOOTLOADER  10

#define ST_OK           0
#define ST_BAD_CMD      1
#define ST_BAD_DATA     2
#define ST_BAD_RANGE    3

#define CHUNK_MAX       32

static __xdata uint8_t stage[SETTINGS_SIZE];

static void CFG_task(void) {
  uint8_t i, cmd, seq, off, len, status = ST_OK;
  __xdata uint8_t* c = (__xdata uint8_t*)&cfg;
  __xdata uint8_t* out = USB_featureIn + 4;

  if(!USB_featureNew) return;
  cmd = USB_featureOut[0];
  seq = USB_featureOut[1];
  off = USB_featureOut[2];
  len = USB_featureOut[3];
  USB_featureIn[1] = 0xFF;                                  // invalidate while we work
  for(i = 2; i < FEATURE_REPORT_LEN; i++) USB_featureIn[i] = 0;

  switch(cmd) {
    case CMD_INFO:
      out[0] = FW_MAJOR;
      out[1] = FW_MINOR;
      out[2] = FW_PATCH;
      out[3] = BOARD_ID;
      out[4] = BOARD_BUTTONS;
      out[5] = BOARD_KNOB_BUTTON;
      out[6] = SETTINGS_SIZE;
      out[7] = runMode;
      out[8] = USB_personality;
      out[9] = NEO_COUNT;
      break;
    case CMD_READ:                                          // from live settings
      if(len > CHUNK_MAX || (uint16_t)off + len > SETTINGS_SIZE) { status = ST_BAD_RANGE; break; }
      for(i = 0; i < len; i++) out[i] = c[off + i];
      break;
    case CMD_WRITE:                                         // into the staging buffer
      if(len > CHUNK_MAX || (uint16_t)off + len > SETTINGS_SIZE) { status = ST_BAD_RANGE; break; }
      for(i = 0; i < len; i++) stage[off + i] = USB_featureOut[4 + i];
      break;
    case CMD_COMMIT:                                        // staging -> live
      if(!SET_valid(stage)) { status = ST_BAD_DATA; break; }
      for(i = 0; i < SETTINGS_SIZE; i++) c[i] = stage[i];
      applySettings();
      break;
    case CMD_SAVE:
      SET_save();
      break;
    case CMD_DEFAULTS:
      SET_defaults(c);
      applySettings();
      break;
    case CMD_STATUS:
      out[0] = (pos - center) & 0xFF;
      out[1] = (pos - center) >> 8;
      out[2] = btn;
      out[3] = runMode;
      out[4] = fnHeld ? 1 : 0;
      break;
    case CMD_RECENTER:
      center = pos;
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
  USB_featureIn[3] = PROTOCOL_VERSION;
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
  uint8_t i;
  EA = 0;
  for(i = 0; i < NEO_COUNT; i++) NEO_writeColor(80, 80, 80);
  EA = 1;
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
  uint8_t i, persona;

  NEO_init();
  if(BTN_READ() & BTN_BIT(BOARD_BOOT_BUTTON)) {             // held at plug-in: bootloader
    NEO_latch();
    for(i = NEO_COUNT * 3; i; i--) NEO_sendByte(127);
    BOOT_now();
  }

  CLK_config();
  DLY_ms(5);
  SET_load();
  applySettings();
  runMode = cfg.mode;
  if(BTN_READ() & BTN_BIT(BOARD_RESCUE_BUTTON)) runMode = MODE_KEYBOARD;

  persona = USB_KEYBOARD;
  if(runMode == MODE_WHEEL) persona = (cfg.flags & FLAG_LEGACY_PAD) ? USB_LEGACY : USB_WHEEL;

  INP_init();
  USB_init(persona);
  WDT_start();

  btn = handled = BTN_READ();                               // ignore keys held at boot
  center = reportedPos = pos = INP_position();

  while(1) {
    WDT_reset();
    now = INP_millis();
    btn = INP_buttons();
    pos = INP_position();

    // hold the knob button alone for 3 s to switch keyboard <-> wheel
    if(btn == BTN_BIT(BOARD_KNOB_BUTTON) && !(cfg.flags & FLAG_NO_GESTURE)) {
      if(!gesture) { gesture = 1; gestureStart = now; }
      else if((uint16_t)(now - gestureStart) >= MODE_HOLD_MS) toggleMode();
    }
    else gesture = 0;

    BTN_task();
    if(runMode == MODE_WHEEL) PAD_task();
    else {
      KNOB_task();
      rel = 0;
    }
    HID_compose();
    HID_send();
    STATUS_task();
    CFG_task();

    if(rebootPending) reboot();
    if(bootPending) enterBootloader();

    LED_task();
  }
}
