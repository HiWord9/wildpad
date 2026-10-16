// ===================================================================================
// Detent - firmware for CH552 "keys + knob" macropads
// ===================================================================================
// Built on wagiminator's CH55x support code (CC BY-SA 3.0). Features:
//  - quadrature decoded in a 4 kHz timer interrupt -> no lost or reversed steps
//  - every key and both knob directions can send keyboard keys, media keys, mouse
//    buttons / scrolling / movement, gamepad buttons, or turn the knob into an
//    absolute gamepad axis
//  - combos: while any key is held, any other key or the knob does something else;
//    long press of a key does something else
//  - up to 3 profiles with their own color and light effect
//  - the gamepad interface is added automatically when a profile uses it
//  - configuration over a vendor HID feature report, stored in Data-Flash
//
// Plug in holding button BOARD_BOOT_BUTTON   -> USB bootloader
// Plug in holding button BOARD_RESCUE_BUTTON -> factory settings until unplugged

#include "ch554.h"
#include "config.h"
#include "system.h"
#include "delay.h"
#include "gpio.h"
#include "neo.h"
#include "settings.h"
#include "input.h"
#include "usbdev.h"

#define BTN_BIT(i)      (1 << (i))
#define NO_INPUT        0x0F

// ===================================================================================
// Shared state
// ===================================================================================
static uint8_t  btn;                      // debounced buttons
static uint8_t  handled;                  // buttons whose press has been processed
static uint8_t  prof;                     // active profile
static uint16_t now;
static int16_t  pos;                      // knob position (clicks)
static int16_t  center;                   // knob position treated as axis zero
static int16_t  knobSeen;                 // knob position already turned into steps
static int16_t  rel;                      // pos - center, limited to the axis range
static __bit    hasPad;                   // gamepad interface present
static __bit    ledOn = 1;                // lights toggled on
static __bit    rebootPending;
static __bit    bootPending;
static __bit    brightDirty;              // brightness changed from the pad itself
static __xdata uint16_t brightTime;
static __xdata uint16_t profTime;                 // when the profile last changed (LED flash)

static __xdata action_t comboAct;         // scratch copy of a combo action
static __xdata int16_t axisValue;                // cached gamepad axis value for rel
static __xdata int16_t lastRel = 0x7FFF;         // rel the cached axis value was computed for
static __xdata uint8_t lastRange;                // clicks from center to full lock

static void applySettings(void) {
  INP_setInvert(CFG_FLAGS & FLAG_INVERT);
  INP_setHalfstep(CFG_FLAGS & FLAG_HALFSTEP);
  INP_setDebounce(cfg[S_DEBOUNCE_MS]);
  if(prof >= N_PROFILES) prof = 0;
}

static __xdata action_t* baseAction(uint8_t in) {
  return ACTION(prof, in);
}

static __bit axisMode(void) {
  return hasPad && baseAction(IN_RIGHT)->type == ACT_AXIS;
}

static void setProfile(uint8_t p) {
  if(p >= N_PROFILES) return;
  if(p != prof) {
    prof = p;
    center = pos;                         // a new axis starts centered
  }
  profTime = now | 1;
}

// ===================================================================================
// Combos
// ===================================================================================
// Find the combo "while held, trig does ..." in the active profile (or all profiles)
// and copy its action to comboAct. held == trig is the long press of that key.
static __xdata action_t* findCombo(uint8_t held, uint8_t trig) {
  uint8_t i, key = (held << 4) | trig, p;
  __xdata uint8_t* c = cfg + COMBOS_OFFSET;
  for(i = 0; i < N_COMBOS; i++, c += COMBO_SIZE) {
    if(c[0] != key) continue;
    p = c[1] >> 5;
    if(p != prof && p != COMBO_ALL_PROFILES) continue;
    comboAct.type = c[1] & 0x1F;
    comboAct.p1 = c[2];
    comboAct.p2 = c[3];
    return &comboAct;
  }
  return 0;
}

// Does button h start combos with other inputs (so its own action waits for release)?
static __bit startsCombos(uint8_t h) {
  uint8_t i, p;
  __xdata uint8_t* c = cfg + COMBOS_OFFSET;
  for(i = 0; i < N_COMBOS; i++, c += COMBO_SIZE) {
    if(c[0] == COMBO_EMPTY || (c[0] >> 4) != h || (c[0] & 0x0F) == h) continue;
    p = c[1] >> 5;
    if(p == prof || p == COMBO_ALL_PROFILES) return 1;
  }
  return 0;
}

// ===================================================================================
// HID reports (keyboard / consumer / mouse)
// ===================================================================================
static __xdata uint8_t kbdRep[9]   = {REPORT_ID_KEYBOARD, 0, 0, 0, 0, 0, 0, 0, 0};
static __xdata uint8_t conRep[3]   = {REPORT_ID_CONSUMER, 0, 0};
static __xdata uint8_t mouseRep[6] = {REPORT_ID_MOUSE, 0, 0, 0, 0, 0};
static __xdata uint8_t newKbd[9];
static __bit kbdDirty, conDirty, mouseDirty, padDirty;
static __bit recompose;                   // active actions changed since last compose
static __xdata int8_t wheelAcc, hwheelAcc, moveX, moveY;

// Action currently held by each button, and by the knob step being replayed
static __xdata action_t active[BOARD_BUTTONS];
static __xdata action_t knobAct;

static __bit outputBusy(void) {
  return kbdDirty || conDirty || mouseDirty || padDirty;
}

static int8_t addSat(int8_t acc, int8_t amount) {
  int16_t v = acc + amount;
  if(v > 127) v = 127;
  if(v < -127) v = -127;
  return v;
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
    case ACT_MEDIA:
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
  if(!recompose) return;
  recompose = 0;
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
    mouseRep[2] = moveX;
    mouseRep[3] = moveY;
    mouseRep[4] = wheelAcc;
    mouseRep[5] = hwheelAcc;
    moveX = moveY = wheelAcc = hwheelAcc = 0;
    USB_hidSend(mouseRep, sizeof(mouseRep));
    mouseDirty = 0;
  }
}

static void clearAction(__xdata action_t* a) {
  a->type = ACT_NONE;
  recompose = 1;
}

static void copyAction(__xdata action_t* dst, __xdata action_t* src) {
  dst->type = src->type;
  dst->p1 = src->p1;
  dst->p2 = src->p2;
  recompose = 1;
}

// Actions that stay active while their input is held
static __bit isHeldType(uint8_t t) {
  return t == ACT_KEY || t == ACT_MEDIA || t == ACT_MOUSE_BTN || t == ACT_PAD_BTN;
}

static void changeBrightness(int8_t d) {
  int16_t b = cfg[S_BRIGHTNESS] + d;
  if(b < 8) b = 8;
  if(b > 255) b = 255;
  cfg[S_BRIGHTNESS] = b;
  ledOn = 1;
  brightDirty = 1;
  brightTime = now;
}

// Actions that happen once, at the moment of the press or knob click
static void oneShot(__xdata action_t* a) {
  switch(a->type) {
    case ACT_SCROLL:
      wheelAcc  = addSat(wheelAcc,  (int8_t)a->p1);
      hwheelAcc = addSat(hwheelAcc, (int8_t)a->p2);
      mouseDirty = 1;
      break;
    case ACT_MOUSE_MOVE:
      moveX = addSat(moveX, (int8_t)a->p1);
      moveY = addSat(moveY, (int8_t)a->p2);
      mouseDirty = 1;
      break;
    case ACT_CENTER:
      center = pos;
      break;
    case ACT_PROFILE:
      if(a->p1 == PROF_NEXT)      setProfile(prof + 1 < N_PROFILES ? prof + 1 : 0);
      else if(a->p1 == PROF_PREV) setProfile(prof ? prof - 1 : N_PROFILES - 1);
      else                        setProfile(a->p1);
      break;
    case ACT_LIGHT:
      if(a->p1 == LIGHT_TOGGLE)        ledOn = !ledOn;
      else if(a->p1 == LIGHT_BRIGHTER) changeBrightness(16);
      else                             changeBrightness(-16);
      break;
    default:
      break;
  }
}

// ===================================================================================
// LEDs
// ===================================================================================
#define LED_TICK_MS     20
#define PROFILE_FLASH   400

static __xdata uint8_t ledV[NEO_COUNT];   // press levels for the "light on press" effect
static __xdata uint8_t rgb[NEO_COUNT * 3];
static __xdata uint16_t ledTime;
static __xdata uint16_t ledPhase;
static __xdata uint16_t hueAcc;                   // rainbow hue << 8

static void LED_flash(uint8_t i) {
  if(i < NEO_COUNT) ledV[i] = 255;
}

static void LED_flashAll(void) {
  uint8_t i;
  for(i = 0; i < NEO_COUNT; i++) ledV[i] = 255;
}

static uint8_t mul8(uint8_t a, uint8_t b) {
  return ((uint16_t)a * b + 255) >> 8;
}

// LED i = color r,g,b at level 0..255, scaled by the brightness setting
static void LED_put(uint8_t i, uint8_t r, uint8_t g, uint8_t b, uint8_t level) {
  __xdata uint8_t* o = rgb + i * 3;
  level = mul8(level, cfg[S_BRIGHTNESS]);
  o[0] = mul8(r, level);
  o[1] = mul8(g, level);
  o[2] = mul8(b, level);
}

// Color wheel 0..191 at full level
static void LED_hue(uint8_t i, uint8_t hue) {
  uint8_t step = (hue & 63) << 2;
  switch(hue >> 6) {
    case 0:  LED_put(i, 255 - step, step, 0, 255); break;
    case 1:  LED_put(i, 0, 255 - step, step, 255); break;
    default: LED_put(i, step, 0, 255 - step, 255); break;
  }
}

static void LED_task(void) {
  uint8_t i, lvl, tri;
  uint16_t side;
  __xdata uint8_t* p = PROFILE(prof);
  uint8_t effect = p[3];

  if((uint16_t)(now - ledTime) < LED_TICK_MS) return;
  ledTime = now;
  ledPhase += (uint16_t)cfg[S_LED_SPEED] * 48;
  lvl = ledPhase >> 8;                                         // 0..255..0 triangle
  tri = (lvl & 0x80) ? ~(lvl << 1) : (lvl << 1);
  hueAcc += (uint16_t)cfg[S_LED_SPEED] * 40;
  if(hueAcc >= 49152u) hueAcc -= 49152u;           // 192 << 8

  if(profTime && (uint16_t)(now - profTime) < PROFILE_FLASH) { // new profile: flash its color
    for(i = 0; i < NEO_COUNT; i++) LED_put(i, p[0], p[1], p[2], 255);
  }
  else if(!ledOn || effect == LED_OFF) {
    for(i = 0; i < NEO_COUNT; i++) LED_put(i, 0, 0, 0, 0);
  }
  else if(effect == LED_STEERING && axisMode() && NEO_COUNT >= 3) {
    // outer LEDs grow with the axis, the middle one marks the center
    side = (uint16_t)(rel < 0 ? -rel : rel) * 255 / (lastRange ? lastRange : 1);
    LED_put(0, p[0], p[1], p[2], rel < 0 ? side : 0);
    LED_put(1, p[0], p[1], p[2], rel == 0 ? 255 : 40);
    LED_put(2, p[0], p[1], p[2], rel > 0 ? side : 0);
#if NEO_COUNT > 3
    for(i = 3; i < NEO_COUNT; i++) LED_put(i, p[0], p[1], p[2], 40);
#endif
  }
  else {
    for(i = 0; i < NEO_COUNT; i++) {
      switch(effect) {
        case LED_BREATHE: LED_put(i, p[0], p[1], p[2], tri < 16 ? 16 : tri); break;
        case LED_RAINBOW:
          lvl = (hueAcc >> 8) + i * 32;
          LED_hue(i, lvl >= 192 ? lvl - 192 : lvl);
          break;
        case LED_PRESS:   LED_put(i, p[0], p[1], p[2], ledV[i]); break;
        default:          LED_put(i, p[0], p[1], p[2], 255); break;   // solid / steering
      }
    }
  }

  EA = 0;                                                      // WS2812 timing is strict
  for(i = 0; i < NEO_COUNT; i++) NEO_writeColor(rgb[i * 3], rgb[i * 3 + 1], rgb[i * 3 + 2]);
  EA = 1;

  for(i = 0; i < NEO_COUNT; i++) {                             // fade press highlights
    lvl = ledV[i];
    if(btn & BTN_BIT(i)) continue;                             // stay lit while held
    ledV[i] = lvl > 24 ? lvl - (lvl >> 3) : 0;
  }
}

// ===================================================================================
// Buttons
// ===================================================================================
#define BS_IDLE     0                     // not pressed
#define BS_PENDING  1                     // starts combos / has a long press: wait
#define BS_HELD     2                     // action in active[] held until release
#define BS_TAP      3                     // tapped action held briefly, then released
#define BS_USED     4                     // was used for a combo: nothing on release
static __xdata uint8_t  bstate[BOARD_BUTTONS];
static __xdata uint16_t btime[BOARD_BUTTONS];

// While another button is held and has a combo for input trig, return that combo
// and mark the held button as used (so it does not fire its own action on release).
static __xdata action_t* heldCombo(uint8_t trig) {
  uint8_t h;
  __xdata action_t* a;
  for(h = 0; h < BOARD_BUTTONS; h++) {
    if(h == trig || !(btn & BTN_BIT(h)) || bstate[h] == BS_IDLE) continue;
    a = findCombo(h, trig);
    if(a) {
      if(bstate[h] == BS_PENDING) bstate[h] = BS_USED;
      return a;
    }
  }
  return 0;
}

// Start action a for button i; it stays active until the button is released
static void startAction(uint8_t i, __xdata action_t* a) {
  bstate[i] = BS_HELD;
  if(a->type == ACT_PROFILE && a->p2 == PROF_WHILE_HELD && a->p1 < N_PROFILES) {
    active[i].type = ACT_PROFILE;                              // remember where to return
    active[i].p1 = prof;
    setProfile(a->p1);
  }
  else if(isHeldType(a->type)) copyAction(&active[i], a);
  else oneShot(a);
}

static void releaseAction(uint8_t i) {
  if(active[i].type == ACT_PROFILE) setProfile(active[i].p1);
  clearAction(&active[i]);
  bstate[i] = BS_IDLE;
}

// Short press of a button that had to wait: send its own action as a tap
static void tapAction(uint8_t i, __xdata action_t* a) {
  if(isHeldType(a->type)) {
    copyAction(&active[i], a);
    bstate[i] = BS_TAP;
    btime[i] = now;
  }
  else {
    startAction(i, a);
    releaseAction(i);
  }
}

static void BTN_task(void) {
  uint8_t i, bit;
  __xdata action_t* a;

  for(i = 0, bit = 1; i < BOARD_BUTTONS; i++, bit <<= 1) {
    switch(bstate[i]) {
      case BS_IDLE:
        if((btn & bit) && !(handled & bit)) {                  // new press
          handled |= bit;
          if(i == BOARD_KNOB_BUTTON) LED_flashAll();
          else LED_flash(i);
          a = heldCombo(i);
          if(a) startAction(i, a);
          else if(startsCombos(i) || findCombo(i, i)) {
            bstate[i] = BS_PENDING;
            btime[i] = now;
          }
          else startAction(i, baseAction(i));
        }
        else if(!(btn & bit)) handled &= ~bit;
        break;

      case BS_PENDING:
        if(!(btn & bit)) {                                     // released: tap
          handled &= ~bit;
          tapAction(i, baseAction(i));
        }
        else if((uint16_t)(now - btime[i]) >= (uint16_t)cfg[S_HOLD_10MS] * 10) {
          a = findCombo(i, i);                                 // long press
          if(a) startAction(i, a);
        }
        break;

      case BS_HELD:
      case BS_USED:
        if(!(btn & bit)) {
          handled &= ~bit;
          releaseAction(i);
        }
        break;

      case BS_TAP:                                             // release after delivery
        if(!outputBusy() && (uint16_t)(now - btime[i]) >= cfg[S_TAP_MS]) releaseAction(i);
        break;
    }
  }
}

// ===================================================================================
// Knob: every click becomes a step for its action (or moves the gamepad axis)
// ===================================================================================
// Steps are queued as [input << 4 | held button or NO_INPUT] so a combo applies to
// the clicks made while its key was held, even if they are replayed later.
#define STEPQ_SIZE  32
static __xdata uint8_t stepQ[STEPQ_SIZE];
static __xdata uint8_t qHead, qLen;
static __xdata uint8_t knobPhase;                // 0 idle, 1 pressed, 2 released (gap)
static __xdata uint16_t knobTime;

static void KNOB_collect(void) {
  int8_t dir;
  uint8_t trig, h, entry;
  while(pos != knobSeen) {
    dir = (int16_t)(pos - knobSeen) > 0 ? 1 : -1;
    knobSeen += dir;
    trig = dir > 0 ? IN_RIGHT : IN_LEFT;
    LED_flash(dir > 0 ? NEO_COUNT - 1 : 0);

    entry = NO_INPUT;
    for(h = 0; h < BOARD_BUTTONS; h++) {                       // a held key with a combo?
      if(!(btn & BTN_BIT(h)) || bstate[h] == BS_IDLE) continue;
      if(findCombo(h, trig)) {
        if(bstate[h] == BS_PENDING) bstate[h] = BS_USED;
        entry = h;
        break;
      }
    }
    if(entry == NO_INPUT && axisMode()) continue;              // the axis follows pos
    if(axisMode()) center += dir;                              // combo: axis stays put
    if(qLen < STEPQ_SIZE) {
      stepQ[(qHead + qLen) % STEPQ_SIZE] = (trig << 4) | entry;
      qLen++;
    }
  }
}

static void KNOB_task(void) {
  uint8_t entry;
  __xdata action_t* a;

  KNOB_collect();
  if(knobPhase) {
    if(outputBusy() || (uint16_t)(now - knobTime) < cfg[S_TAP_MS]) return;
    if(knobPhase == 1) {                                       // press delivered -> release
      clearAction(&knobAct);
      knobPhase = 2;
      knobTime = now;
      return;
    }
    knobPhase = 0;                                             // release delivered -> next
  }
  while(qLen) {
    entry = stepQ[qHead];
    qHead = (qHead + 1) % STEPQ_SIZE;
    qLen--;
    a = (entry & 0x0F) == NO_INPUT ? baseAction(entry >> 4) : findCombo(entry & 0x0F, entry >> 4);
    if(!a) continue;
    if(isHeldType(a->type)) {                                  // press now, release later
      copyAction(&knobAct, a);
      knobPhase = 1;
      knobTime = now;
      return;
    }
    oneShot(a);
  }
}

// ===================================================================================
// Gamepad (XInput report)
// ===================================================================================
static __xdata uint8_t padRep[XINPUT_REPORT_LEN];
static __xdata uint8_t padNew[XINPUT_REPORT_LEN];

static __code uint8_t PAD_BITS[17][2] = {      // {byte offset, bit mask}
  {0, 0},
  {3, 0x10}, {3, 0x20}, {3, 0x40}, {3, 0x80},  // A B X Y
  {3, 0x01}, {3, 0x02},                        // LB RB
  {0, 0},    {0, 0},                           // LT RT (analog, handled separately)
  {2, 0x20}, {2, 0x10},                        // Back Start
  {2, 0x40}, {2, 0x80},                        // LS RS
  {2, 0x01}, {2, 0x02}, {2, 0x04}, {2, 0x08},  // D-pad up down left right
};

static void padButton(__xdata action_t* a) {
  uint8_t code;
  if(a->type != ACT_PAD_BTN) return;
  code = a->p1;
  if(code == PAD_LT) padNew[4] = 255;
  else if(code == PAD_RT) padNew[5] = 255;
  else if(code == PAD_GUIDE) padNew[3] |= 0x04;
  else if(code > 0 && code <= PAD_DRIGHT) padNew[PAD_BITS[code][0]] |= PAD_BITS[code][1];
}

static void PAD_task(void) {
  uint8_t i, t, range, axis;
  __xdata action_t* k = baseAction(IN_RIGHT);

  for(i = 0; i < XINPUT_REPORT_LEN; i++) padNew[i] = 0;
  padNew[1] = XINPUT_REPORT_LEN;
  for(i = 0; i < BOARD_BUTTONS; i++) padButton(&active[i]);
  padButton(&knobAct);

  rel = 0;
  if(k->type == ACT_AXIS) {
    range = k->p2 ? k->p2 : 1;
    axis = k->p1 & AXIS_MASK;
    rel = pos - center;
    if(!(k->p1 & AXIS_FREE)) {                                 // hard stop: no wind-up
      if(rel > range)  { center = pos - range; rel = range; }
      if(rel < -range) { center = pos + range; rel = -range; }
    }
    else {
      if(rel > range)  rel = range;
      if(rel < -range) rel = -range;
    }
    if(rel != lastRel || range != lastRange) {                 // 32-bit divide is slow
      lastRel = rel;
      lastRange = range;
      axisValue = (int16_t)((int32_t)rel * 32767 / range);
    }
    if(axis == AXIS_TRIGGERS) {
      t = (uint16_t)(rel < 0 ? -rel : rel) * 255 / range;
      if(rel < 0 && t > padNew[4]) padNew[4] = t;
      if(rel > 0 && t > padNew[5]) padNew[5] = t;
    }
    else if(axis <= AXIS_RY) {
      i = 6 + (axis << 1);                                     // LX, LY, RX, RY
      padNew[i] = axisValue & 0xFF;
      padNew[i + 1] = axisValue >> 8;
    }
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
// [pos lo, pos hi, buttons, profile, flags (1 gamepad, 2 knob is an axis, 4 lights on)]
static __xdata uint8_t statusRep[STATUS_REPORT_LEN];

static uint8_t statusFlags(void) {
  return (hasPad ? 1 : 0) | (axisMode() ? 2 : 0) | (ledOn ? 4 : 0);
}

static void fillStatus(__xdata uint8_t* o) {
  int16_t p = axisMode() ? rel : pos - center;
  o[0] = p & 0xFF;
  o[1] = p >> 8;
  o[2] = btn;
  o[3] = prof;
  o[4] = statusFlags();
}

static void STATUS_task(void) {
  static __xdata int16_t sentPos;
  static __xdata uint8_t sentBtn = 0xFF, sentProf, sentFlags;
  int16_t p = axisMode() ? rel : pos - center;
  uint8_t f = statusFlags();
  if(p == sentPos && btn == sentBtn && prof == sentProf && f == sentFlags) return;
  if(!USB_statusReady()) return;
  sentPos = p;
  sentBtn = btn;
  sentProf = prof;
  sentFlags = f;
  fillStatus(statusRep);
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
#define CMD_PROFILE     11

#define ST_OK           0
#define ST_BAD_CMD      1
#define ST_BAD_DATA     2
#define ST_BAD_RANGE    3

#define CHUNK_MAX       32

static __xdata uint8_t stage[SETTINGS_SIZE];
static __xdata uint8_t persona;

static uint8_t wantedPersona(void) {
  if(!SET_usesGamepad(cfg)) return USB_KEYBOARD;
  return (CFG_FLAGS & FLAG_XBOX360) ? USB_XBOX360 : USB_GAMEPAD;
}

static void CFG_task(void) {
  uint8_t i, cmd, seq, off, len, status = ST_OK;
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
      out[7] = N_PROFILES;
      out[8] = persona;
      out[9] = NEO_COUNT;
      out[10] = N_COMBOS;
      out[11] = PROFILES_OFFSET;
      out[12] = PROFILE_SIZE;
      out[13] = COMBOS_OFFSET;
      out[14] = SETTINGS_VERSION;
      out[15] = prof;
      break;
    case CMD_READ:                                          // from live settings
      if(len > CHUNK_MAX || (uint16_t)off + len > SETTINGS_SIZE) { status = ST_BAD_RANGE; break; }
      for(i = 0; i < len; i++) out[i] = cfg[off + i];
      break;
    case CMD_WRITE:                                         // into the staging buffer
      if(len > CHUNK_MAX || (uint16_t)off + len > SETTINGS_SIZE) { status = ST_BAD_RANGE; break; }
      for(i = 0; i < len; i++) stage[off + i] = USB_featureOut[4 + i];
      break;
    case CMD_COMMIT:                                        // staging -> live
      if(!SET_valid(stage)) { status = ST_BAD_DATA; break; }
      for(i = 0; i < SETTINGS_SIZE; i++) cfg[i] = stage[i];
      applySettings();
      out[0] = wantedPersona() != persona;                  // reconnect needed
      break;
    case CMD_SAVE:
      SET_save();
      brightDirty = 0;
      out[0] = wantedPersona() != persona;
      break;
    case CMD_DEFAULTS:                                      // live only, until SAVE
      SET_defaults(cfg);
      applySettings();
      out[0] = wantedPersona() != persona;
      break;
    case CMD_STATUS:
      fillStatus(out);
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
    case CMD_PROFILE:
      if(off >= N_PROFILES) { status = ST_BAD_RANGE; break; }
      setProfile(off);
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

// Brightness changed with a key: store just that byte once it settles. The stored
// checksum is recomputed from the stored bytes, so unsaved changes made in the
// configurator stay unsaved.
static void BRIGHT_task(void) {
  if(!brightDirty || (uint16_t)(now - brightTime) < 3000) return;
  brightDirty = 0;
  SET_saveByte(S_BRIGHTNESS, cfg[S_BRIGHTNESS]);
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
  T2CON = 0;
  DLY_ms(300);                                              // let the host see the unplug
  EA = 0;
  BOOT_now();
}

// ===================================================================================
// Main
// ===================================================================================
void main(void) {
  uint8_t i;

  NEO_init();
  if(BTN_READ() & BTN_BIT(BOARD_BOOT_BUTTON)) {             // held at plug-in: bootloader
    NEO_latch();
    for(i = NEO_COUNT * 3; i; i--) NEO_sendByte(127);
    BOOT_now();
  }

  CLK_config();
  DLY_ms(5);
  SET_load();
  if(BTN_READ() & BTN_BIT(BOARD_RESCUE_BUTTON)) SET_defaults(cfg);   // not saved
  prof = cfg[S_START_PROFILE];
  applySettings();

  persona = wantedPersona();
  hasPad = persona != USB_KEYBOARD;

  INP_init();
  USB_init(persona);
  WDT_start();

  btn = handled = BTN_READ();                               // ignore keys held at boot
  center = knobSeen = pos = INP_position();
  profTime = INP_millis() | 1;                              // show the profile color

  while(1) {
    WDT_reset();
    now = INP_millis();
    btn = INP_buttons();
    pos = INP_position();

    BTN_task();
    KNOB_task();
    if(hasPad) PAD_task();
    HID_compose();
    HID_send();
    STATUS_task();
    CFG_task();
    BRIGHT_task();

    if(rebootPending) reboot();
    if(bootPending) enterBootloader();

    LED_task();
  }
}
