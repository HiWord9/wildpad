// ===================================================================================
// Detent - firmware for CH552 "keys + knob" macropads
// ===================================================================================
// Built on wagiminator's CH55x support code (CC BY-SA 3.0). Features:
//  - quadrature decoded in a 4 kHz timer interrupt -> no lost or reversed steps
//  - every key and both knob directions can send keyboard keys, media keys, mouse
//    buttons / scrolling / movement, gamepad buttons, or turn the knob into an
//    absolute gamepad axis
//  - combos: while any key is held, any other key or the knob does something else;
//    long press of a key does something else. A key that starts combos sends its own
//    action on release, or right away and held (ACT_NOW, e.g. Alt for Alt+Tab)
//  - sequences: any action can be a list of steps - tap, hold while the input is
//    held, wait, and steps for when it is let go
//  - up to 3 profiles with their own color and light effect
//  - the gamepad interface is added automatically when a profile uses it
//  - configuration over a vendor HID feature report, stored in Data-Flash
//
// Plug in holding button BOARD_BOOT_BUTTON   -> USB bootloader
// Plug in holding button BOARD_RESCUE_BUTTON -> without the Xbox 360 identity, so the
//                                             configurator can reach the pad again

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
static __xdata uint8_t fakeBtn;           // buttons held by CMD_INJECT
static __xdata int16_t fakePos;           // knob clicks added by CMD_INJECT
static __bit    hasPad;                   // gamepad interface present
static __bit    ledOn = 1;                // lights toggled on
static __bit    rebootPending;
static __bit    bootPending;
static __bit    brightDirty;              // brightness changed from the pad itself
static __xdata uint16_t brightTime;
static __xdata uint16_t profTime;                 // when the profile last changed (LED flash)

static __xdata uint8_t nProf;             // profiles in use
static __xdata uint8_t recOff;            // where the records (combos, sequences) start
static __xdata action_t comboAct;         // scratch copy of a combo action
static __xdata action_t baseAct;          // scratch copy of a profile action
static __xdata int16_t axisValue;                // cached gamepad axis value for rel
static __xdata int16_t lastRel = 0x7FFF;         // rel the cached axis value was computed for
static __xdata uint8_t lastRange;                // clicks from center to full lock

static void applySettings(void) {
  INP_setInvert(CFG_FLAGS & FLAG_INVERT);
  INP_setHalfstep(CFG_FLAGS & FLAG_HALFSTEP);
  INP_setDebounce(cfg[S_DEBOUNCE_MS]);
  nProf = cfg[S_PROFILES] ? cfg[S_PROFILES] : N_PROFILES;
  recOff = RECORDS_AT(nProf);
  if(prof >= nProf) prof = 0;
}

// The active profile's action for an input, without the ACT_NOW flag
static __xdata action_t* baseAction(uint8_t in) {
  __xdata action_t* a = ACTION(prof, in);
  baseAct.type = a->type & ACT_TYPE_MASK;
  baseAct.p1 = a->p1;
  baseAct.p2 = a->p2;
  return &baseAct;
}

static __bit sendsNow(uint8_t in) {
  return (ACTION(prof, in)->type & ACT_NOW) ? 1 : 0;
}

static __bit axisMode(void) {
  return hasPad && baseAction(IN_RIGHT)->type == ACT_AXIS;
}

static void setProfile(uint8_t p) {
  if(p >= nProf) return;
  if(p != prof) {
    prof = p;
    center = pos;                         // a new axis starts centered
  }
  profTime = now | 1;
}

// ===================================================================================
// Records: combos and sequences
// ===================================================================================
// Find the combo "while held, trig does ..." and copy its action to comboAct.
// held == trig is the long press of that key. A combo of the active profile wins
// over an all-profiles combo with the same controls.
static __xdata action_t* findCombo(uint8_t held, uint8_t trig) {
  uint8_t i = recOff, key = (held << 4) | trig, p;
  __xdata uint8_t* c;
  __xdata uint8_t* any = 0;
  while(i < RECORDS_END) {
    c = cfg + i;
    if(c[0] == REC_END) break;
    if(c[0] >= REC_SEQ) { i += 2 + c[1]; continue; }
    i += 4;
    if(c[0] != key) continue;
    p = c[1] >> 5;
    if(p == prof) { any = c; break; }
    if(p == COMBO_ALL_PROFILES && !any) any = c;
  }
  if(!any) return 0;
  comboAct.type = any[1] & ACT_TYPE_MASK;
  comboAct.p1 = any[2];
  comboAct.p2 = any[3];
  return &comboAct;
}

// Does button h start combos with other inputs (so its own action waits for release)?
static __bit startsCombos(uint8_t h) {
  uint8_t i = recOff, p;
  __xdata uint8_t* c;
  while(i < RECORDS_END) {
    c = cfg + i;
    if(c[0] == REC_END) break;
    if(c[0] >= REC_SEQ) { i += 2 + c[1]; continue; }
    i += 4;
    if((c[0] >> 4) != h || (c[0] & 0x0F) == h) continue;
    p = c[1] >> 5;
    if(p == prof || p == COMBO_ALL_PROFILES) return 1;
  }
  return 0;
}

// Locate sequence id: its steps are cfg[seqStart .. seqEnd), the ones before seqMark
// run on press, the ones after it (STEP_RELEASE) when the input is let go.
static __xdata uint8_t seqStart, seqMark, seqEnd;

static __bit findSeq(uint8_t id) {
  uint8_t i = recOff;
  __xdata uint8_t* c;
  while(i < RECORDS_END) {
    c = cfg + i;
    if(c[0] == REC_END) return 0;
    if(c[0] < REC_SEQ) { i += 4; continue; }
    if(c[0] == (REC_SEQ | id)) {
      seqStart = i + 2;
      seqEnd = seqStart + c[1];
      for(i = seqStart; i < seqEnd; i += (cfg[i] & STEP_OP) >= STEP_WAIT ? 1 : 3)
        if((cfg[i] & STEP_OP) == STEP_RELEASE) break;
      seqMark = i < seqEnd ? i : seqEnd;
      return 1;
    }
    i += 2 + c[1];
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

// Everything that is pressed: actions held by an owner (see "Held actions"), and the
// one the runner is tapping
#define HELD_MAX    8
static __xdata uint8_t  heldOwner[HELD_MAX];      // owner + 1, 0 = free slot
static __xdata action_t held[HELD_MAX];
static __xdata action_t tapAct;
static __bit recompose;                   // active actions changed since last compose
static __xdata int8_t wheelAcc, hwheelAcc, moveX, moveY;

static __bit outputBusy(void) {
  return kbdDirty || conDirty || mouseDirty || padDirty;
}

static int8_t addSat(int8_t acc, int8_t amount) {
  int16_t v = acc + amount;
  if(v > 127) v = 127;
  if(v < -127) v = -127;
  return v;
}

static __xdata uint8_t nkeys, newMbtn, newConL, newConH;   // report being composed

static void addToReport(__xdata action_t* a) {
  uint8_t i;
  switch(a->type) {
    case ACT_KEY:
      newKbd[1] |= a->p1;
      if(a->p2) {
        for(i = 0; i < nkeys; i++) if(newKbd[3 + i] == a->p2) return;
        if(nkeys < 6) newKbd[3 + nkeys++] = a->p2;
      }
      break;
    case ACT_MEDIA:
      if(!newConL && !newConH) { newConL = a->p1; newConH = a->p2; }
      break;
    case ACT_MOUSE_BTN:
      newMbtn |= a->p1;
      break;
    default:
      break;
  }
}

static void HID_compose(void) {
  uint8_t i;
  if(!recompose) return;
  recompose = 0;
  nkeys = newMbtn = newConL = newConH = 0;
  for(i = 0; i < 9; i++) newKbd[i] = 0;
  for(i = 0; i < HELD_MAX; i++) if(heldOwner[i]) addToReport(&held[i]);
  addToReport(&tapAct);

  for(i = 1; i < 9; i++) {
    if(kbdRep[i] != newKbd[i]) { kbdRep[i] = newKbd[i]; kbdDirty = 1; }
  }
  if(conRep[1] != newConL || conRep[2] != newConH) {
    conRep[1] = newConL;
    conRep[2] = newConH;
    conDirty = 1;
  }
  if(mouseRep[1] != newMbtn) { mouseRep[1] = newMbtn; mouseDirty = 1; }
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
  dst->type = src->type == ACT_KEY_HOLD ? ACT_KEY : src->type;
  dst->p1 = src->p1;
  dst->p2 = src->p2;
  recompose = 1;
}

// Actions that stay active while their input is held
static __bit isHeldType(uint8_t t) {
  return t == ACT_KEY || t == ACT_KEY_HOLD || t == ACT_MEDIA || t == ACT_MOUSE_BTN || t == ACT_PAD_BTN;
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
      if(a->p1 == PROF_NEXT)      setProfile(prof + 1 < nProf ? prof + 1 : 0);
      else if(a->p1 == PROF_PREV) setProfile(prof ? prof - 1 : nProf - 1);
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

static __xdata uint8_t colR, colG, colB;  // color of the LED being set

// Color wheel 0..191 into colR/G/B
static void LED_hue(uint8_t hue) {
  uint8_t step = (hue & 63) << 2, up = step, down = 255 - step;
  switch(hue >> 6) {
    case 0:  colR = down; colG = up;   colB = 0;    break;
    case 1:  colR = 0;    colG = down; colB = up;   break;
    default: colR = up;   colG = 0;    colB = down; break;
  }
}

static void LED_task(void) {
  uint8_t i, lvl, tri, effect, side = 0;
  __xdata uint8_t* p;
  __xdata uint8_t* o;

  if((uint16_t)(now - ledTime) < LED_TICK_MS) return;
  ledTime = now;
  ledPhase += (uint16_t)cfg[S_LED_SPEED] * 48;
  lvl = ledPhase >> 8;                                         // 0..255..0 triangle
  tri = (lvl & 0x80) ? ~(lvl << 1) : (lvl << 1);
  hueAcc += (uint16_t)cfg[S_LED_SPEED] * 40;
  if(hueAcc >= 49152u) hueAcc -= 49152u;           // 192 << 8

  p = PROFILE(prof);
  effect = p[3];
  if(profTime && (uint16_t)(now - profTime) < PROFILE_FLASH) effect = LED_SOLID;   // new profile
  else if(!ledOn) effect = LED_OFF;
  else if(effect == LED_STEERING) {
    if(axisMode() && NEO_COUNT >= 3)                           // outer LEDs grow with the axis
      side = (uint16_t)(rel < 0 ? -rel : rel) * 255 / (lastRange ? lastRange : 1);
    else effect = LED_SOLID;
  }

  for(i = 0, o = rgb; i < NEO_COUNT; i++, o += 3) {
    colR = p[0]; colG = p[1]; colB = p[2];
    switch(effect) {
      case LED_OFF:     lvl = 0; break;
      case LED_BREATHE: lvl = tri < 16 ? 16 : tri; break;
      case LED_PRESS:   lvl = ledV[i]; break;
      case LED_RAINBOW:
        lvl = (hueAcc >> 8) + i * 32;
        LED_hue(lvl >= 192 ? lvl - 192 : lvl);
        lvl = 255;
        break;
      case LED_STEERING:                                       // the middle one marks the center
        if(i == 0)      lvl = rel < 0 ? side : 0;
        else if(i == 2) lvl = rel > 0 ? side : 0;
        else            lvl = (i == 1 && rel == 0) ? 255 : 40;
        break;
      default:          lvl = 255; break;                      // solid
    }
    lvl = mul8(lvl, cfg[S_BRIGHTNESS]);
    o[0] = mul8(colR, lvl);
    o[1] = mul8(colG, lvl);
    o[2] = mul8(colB, lvl);
  }

  EA = 0;                                                      // WS2812 timing is strict
  for(i = 0, o = rgb; i < NEO_COUNT; i++, o += 3) NEO_writeColor(o[0], o[1], o[2]);
  EA = 1;

  for(i = 0; i < NEO_COUNT; i++) {                             // fade press highlights
    lvl = ledV[i];
    if(btn & BTN_BIT(i)) continue;                             // stay lit while held
    ledV[i] = lvl > 24 ? lvl - (lvl >> 3) : 0;
  }
}

// ===================================================================================
// Held actions
// ===================================================================================
// Everything that stays pressed lives here, with its owner: a button (until it is let
// go) or a knob direction (until the knob has stopped for S_TURN_HOLD).
#define NO_OWNER    0xFE
static __xdata action_t tmpAct;
static __xdata uint16_t turnTime;                 // last knob click
static __bit turnHeld;                            // knob directions hold something

static void addHeld(uint8_t owner, __xdata action_t* a) {
  uint8_t i, t = a->type == ACT_KEY_HOLD ? ACT_KEY : a->type;
  for(i = 0; i < HELD_MAX; i++)                     // already held by this owner?
    if(heldOwner[i] == owner + 1 && held[i].type == t && held[i].p1 == a->p1 && held[i].p2 == a->p2) return;
  for(i = 0; i < HELD_MAX; i++) {
    if(heldOwner[i]) continue;
    heldOwner[i] = owner + 1;
    copyAction(&held[i], a);
    if(owner >= BOARD_BUTTONS) turnHeld = 1;
    return;
  }
}

static void releaseOwner(uint8_t owner) {
  uint8_t i;
  for(i = 0; i < HELD_MAX; i++) {
    if(heldOwner[i] != owner + 1) continue;
    if(held[i].type == ACT_PROFILE) setProfile(held[i].p1);    // "profile while held" ends
    heldOwner[i] = 0;
    clearAction(&held[i]);
  }
}

static void releaseTurns(void) {
  releaseOwner(IN_RIGHT);
  releaseOwner(IN_LEFT);
  turnHeld = 0;
}

// ===================================================================================
// Buttons: state
// ===================================================================================
#define BS_IDLE     0                     // not pressed
#define BS_PENDING  1                     // starts combos / has a long press: wait
#define BS_HELD     2                     // its actions are held until release
#define BS_USED     3                     // was used for a combo: nothing on release
static __xdata uint8_t  bstate[BOARD_BUTTONS];
static __xdata uint16_t btime[BOARD_BUTTONS];
static __xdata uint8_t  btnSeq[BOARD_BUTTONS];    // sequence id + 1 started by the press

static __bit ownerActive(uint8_t owner) {
  if(owner < BOARD_BUTTONS) return (btn & BTN_BIT(owner)) && bstate[owner] != BS_IDLE;
  return owner != NO_OWNER;                         // knob: until it stops
}

// ===================================================================================
// Runner: taps, waits and sequences, one after the other
// ===================================================================================
#define JOB_MAX     8                     // power of two
#define JOB_RUN     1                     // [1, first step, end, owner]
#define JOB_TAP     2                     // [2, type, p1, p2]
static __xdata uint8_t jobs[JOB_MAX][4];
static __xdata uint8_t jHead, jLen;

// Knob clicks are queued as [input << 4 | held button or NO_INPUT] so a combo applies
// to the clicks made while its key was held, even if they are replayed later.
#define STEPQ_SIZE  32
static __xdata uint8_t stepQ[STEPQ_SIZE];
static __xdata uint8_t qHead, qLen;

static __xdata uint8_t  runPos, runEnd, runOwner;  // sequence steps being run
static uint8_t          runPhase;                  // 0 ready, 1 tap down, 2 tap gap, 3 wait
static __xdata uint16_t runTime, runWait;
static __xdata uint8_t  runTapLen;                 // how long the current tap lasts
#define PAD_TAP_MS  40                    // games read a gamepad once per frame: make
                                          // its taps (and the gaps after them) last
static __xdata action_t stepAct;

static void addJob(uint8_t kind, uint8_t a, uint8_t b, uint8_t c) {
  __xdata uint8_t* j;
  if(jLen >= JOB_MAX) return;
  j = jobs[(jHead + jLen) & (JOB_MAX - 1)];
  j[0] = kind; j[1] = a; j[2] = b; j[3] = c;
  jLen++;
}

// Do action a for owner (NO_OWNER: nobody holds it). hold: keep it pressed while the
// owner is held instead of tapping it. Only called while the runner is ready.
static void runAction(__xdata action_t* a, uint8_t owner, uint8_t hold) {
  uint8_t t = a->type, p1 = a->p1, p2 = a->p2;
  if(t == ACT_SEQ) {
    if(findSeq(p1)) { runPos = seqStart; runEnd = seqMark; runOwner = owner; }
    return;
  }
  if(t == ACT_KEY_HOLD) {                                      // modifiers held, key tapped
    if(owner != NO_OWNER) {
      tmpAct.type = ACT_KEY; tmpAct.p1 = p1 & HOLD_MODS; tmpAct.p2 = 0;
      addHeld(owner, &tmpAct);
    }
    t = ACT_KEY;
  }
  if(!isHeldType(t)) { oneShot(a); return; }
  if(hold && owner != NO_OWNER && ownerActive(owner)) { addHeld(owner, a); return; }
  tapAct.type = t; tapAct.p1 = p1; tapAct.p2 = p2;
  recompose = 1;
  runPhase = 1;
  runTime = now;
  runTapLen = cfg[S_TAP_MS];
  if(t == ACT_PAD_BTN && runTapLen < PAD_TAP_MS) runTapLen = PAD_TAP_MS;
}

static void RUN_task(void) {
  uint8_t b, entry;
  __xdata uint8_t* j;
  __xdata action_t* a;

  if(runPhase == 3) {                                          // waiting
    if((uint16_t)(now - runTime) < runWait) return;
    runPhase = 0;
  }
  else if(runPhase) {                                          // tap: press, then release
    if(outputBusy() || (uint16_t)(now - runTime) < runTapLen) return;
    if(runPhase == 1) {
      clearAction(&tapAct);
      runPhase = 2;
      runTime = now;
      return;
    }
    runPhase = 0;
  }

  while(!runPhase) {
    if(runPos < runEnd) {                                      // next sequence step
      b = cfg[runPos];
      if((b & STEP_OP) == STEP_WAIT) {
        runWait = (uint16_t)(b & ~STEP_OP) * 20;
        runTime = now;
        runPhase = 3;
        runPos++;
      }
      else if((b & STEP_OP) == STEP_RELEASE) runPos = runEnd;
      else {
        stepAct.type = b & ~STEP_OP;
        stepAct.p1 = cfg[runPos + 1];
        stepAct.p2 = cfg[runPos + 2];
        runPos += 3;
        runAction(&stepAct, runOwner, (b & STEP_OP) == STEP_HOLD);
      }
    }
    else if(jLen) {                                            // next job from the keys
      j = jobs[jHead];
      jHead = (jHead + 1) & (JOB_MAX - 1);
      jLen--;
      if(j[0] == JOB_RUN) { runPos = j[1]; runEnd = j[2]; runOwner = j[3]; }
      else {
        stepAct.type = j[1]; stepAct.p1 = j[2]; stepAct.p2 = j[3];
        runAction(&stepAct, NO_OWNER, 0);
      }
    }
    else if(qLen) {                                            // next knob click
      entry = stepQ[qHead];
      qHead = (qHead + 1) & (STEPQ_SIZE - 1);
      qLen--;
      a = (entry & 0x0F) == NO_INPUT ? baseAction(entry >> 4) : findCombo(entry & 0x0F, entry >> 4);
      if(!a) continue;
      if(turnHeld && a->type != ACT_KEY_HOLD && a->type != ACT_SEQ) releaseTurns();
      turnTime = now;
      runAction(a, entry >> 4, 0);
    }
    else {                                                     // idle
      if(turnHeld && (uint16_t)(now - turnTime) >= (uint16_t)(cfg[S_TURN_HOLD] ? cfg[S_TURN_HOLD] : 8) * 100)
        releaseTurns();                                        // the knob has stopped
      return;
    }
  }
}

// ===================================================================================
// Buttons
// ===================================================================================
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

// Button i pressed: start action a; what it holds stays held until the button is let go
static void startAction(uint8_t i, __xdata action_t* a) {
  bstate[i] = BS_HELD;
  switch(a->type) {
    case ACT_SEQ:
      if(findSeq(a->p1)) {
        btnSeq[i] = a->p1 + 1;
        addJob(JOB_RUN, seqStart, seqMark, i);
      }
      break;
    case ACT_KEY_HOLD:                                         // modifiers now, key tapped
      tmpAct.type = ACT_KEY; tmpAct.p1 = a->p1 & HOLD_MODS; tmpAct.p2 = 0;
      addHeld(i, &tmpAct);
      addJob(JOB_TAP, ACT_KEY, a->p1, a->p2);
      break;
    case ACT_PROFILE:
      if(a->p2 == PROF_WHILE_HELD && a->p1 < nProf) {         // remember where to return
        tmpAct.type = ACT_PROFILE; tmpAct.p1 = prof; tmpAct.p2 = 0;
        addHeld(i, &tmpAct);
        setProfile(a->p1);
      }
      else oneShot(a);
      break;
    default:
      if(isHeldType(a->type)) addHeld(i, a);
      else oneShot(a);
      break;
  }
}

static void releaseAction(uint8_t i) {
  releaseOwner(i);
  if(btnSeq[i]) {                                              // steps for the release
    if(findSeq(btnSeq[i] - 1) && seqMark < seqEnd) addJob(JOB_RUN, seqMark + 1, seqEnd, NO_OWNER);
    btnSeq[i] = 0;
  }
  bstate[i] = BS_IDLE;
}

// Short press of a button that had to wait: send its own action as a tap
static void tapAction(uint8_t i, __xdata action_t* a) {
  bstate[i] = BS_IDLE;
  if(a->type == ACT_SEQ) {
    if(!findSeq(a->p1)) return;
    addJob(JOB_RUN, seqStart, seqMark, NO_OWNER);
    if(seqMark < seqEnd) addJob(JOB_RUN, seqMark + 1, seqEnd, NO_OWNER);
  }
  else if(a->type != ACT_PROFILE || a->p2 != PROF_WHILE_HELD)
    addJob(JOB_TAP, a->type, a->p1, a->p2);
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
          else if(sendsNow(i)) startAction(i, baseAction(i));   // held now, combos still work
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

      default:                                                 // held / used
        if(!(btn & bit)) {
          handled &= ~bit;
          releaseAction(i);
        }
        break;
    }
  }
}

// ===================================================================================
// Knob: every click becomes a step for its action (or moves the gamepad axis)
// ===================================================================================
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
      stepQ[(qHead + qLen) & (STEPQ_SIZE - 1)] = (trig << 4) | entry;
      qLen++;
    }
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
  for(i = 0; i < HELD_MAX; i++) if(heldOwner[i]) padButton(&held[i]);
  padButton(&tapAct);

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
    if(rel != lastRel || range != lastRange) {                 // only on change: divides
      lastRel = rel;
      lastRange = range;
      axisValue = rel * (int16_t)(32767u / range);             // 16-bit math, full lock exact
      if(rel == range) axisValue = 32767;
      else if(rel == -range) axisValue = -32767;
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
#define CMD_INJECT      12                // test aid: offset = buttons held, length = int8 knob clicks

#define ST_OK           0
#define ST_BAD_CMD      1
#define ST_BAD_DATA     2
#define ST_BAD_RANGE    3

#define CHUNK_MAX       32

static __xdata uint8_t stage[SETTINGS_SIZE];

static __code uint8_t INFO[16] = {
  FW_MAJOR, FW_MINOR, FW_PATCH, BOARD_ID, BOARD_BUTTONS, BOARD_KNOB_BUTTON, SETTINGS_SIZE,
  N_PROFILES, 0 /* USB layout */, NEO_COUNT, RECORDS_END - RECORDS_OFFSET /* record bytes */,
  PROFILES_OFFSET, PROFILE_SIZE, RECORDS_OFFSET, SETTINGS_VERSION, 0 /* active profile */
};
static __xdata uint8_t persona;

static uint8_t wantedPersona(void) {
  if(!SET_padUsed) return USB_KEYBOARD;
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
      for(i = 0; i < sizeof(INFO); i++) out[i] = INFO[i];
      out[8] = persona;
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
      for(i = 0; i < 9; i++) out[8 + i] = kbdRep[i];          // keyboard report being sent
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
      if(off >= nProf) { status = ST_BAD_RANGE; break; }
      setProfile(off);
      break;
    case CMD_INJECT:                                        // as if pressed / turned by hand
      fakeBtn = off;
      fakePos += (int8_t)len;
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
// SDCC links its start-up code that zeroes the variables in external RAM only when a
// module asks for it. Without it every static in XRAM (button states, held actions,
// ...) starts with whatever the bootloader left there, and C code - like the
// simulator, where RAM starts zeroed - assumes zeros. Never called; it only carries
// the request to the linker.
void requestXramClear(void) __naked {
  __asm
    .globl __mcs51_genXRAMCLEAR
  __endasm;
}

#define KEEP_CLEAN_START  0x5A

void main(void) {
  uint8_t i;

#ifndef TEST_HARNESS
  // Start every boot from a clean chip. After its time-out the ROM bootloader hands
  // over without a reset, leaving its USB and interrupt setup behind; the pad then
  // enumerated but stopped answering control requests. One software reset per start
  // (it needs the watchdog running) gives every boot the reset state.
  if(RST_getKeep() != KEEP_CLEAN_START) {
    RST_keep(KEEP_CLEAN_START);
    WDT_start();
    RST_now();
    while(1);
  }
  RST_keep(0);
#endif

  NEO_init();
  if(BTN_READ() & BTN_BIT(BOARD_BOOT_BUTTON)) {             // held at plug-in: bootloader
    NEO_latch();
    for(i = NEO_COUNT * 3; i; i--) NEO_sendByte(127);
    BOOT_now();
  }

  CLK_config();
  DLY_ms(5);
  SET_load();
  if(BTN_READ() & BTN_BIT(BOARD_RESCUE_BUTTON)) CFG_FLAGS &= ~FLAG_XBOX360;   // not saved
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
    btn = INP_buttons() | fakeBtn;
    pos = INP_position() + fakePos;

    BTN_task();
    KNOB_collect();
    RUN_task();
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
