// ===================================================================================
// Persistent settings (CH552 Data-Flash, 128 bytes)
// ===================================================================================

#include "ch554.h"
#include "settings.h"

__xdata uint8_t cfg[SETTINGS_SIZE];

// ===================================================================================
// Boards
// ===================================================================================
// Pinouts as listed per model by KeypadFlasher (AmyJeanes).
#define P1_(x)      (x)
#define P3_(x)      (8 + (x))
static __code uint8_t BOARDS[N_BOARDS][BOARD_MAP_LEN] = {
  // 1: 3 keys + knob
  { 4 | 3 << 4, 3,
    P1_(1), P1_(7), P1_(6), P3_(3), 0xFF, 0xFF, 0xFF, 0xFF },
  // 2: 6 keys (2 rows of 3) + knob
  { 7 | 6 << 4, 6 | MAP_LED_REVERSED,
    P3_(2), P1_(4), P1_(5), P1_(6), P1_(7), P1_(1), P3_(3), 0xFF },
  // 3: 6 keys + knob, keys wired in the opposite order
  { 7 | 6 << 4, 6,
    P1_(1), P1_(7), P1_(6), P1_(5), P1_(4), P3_(2), P3_(3), 0xFF },
};

uint8_t boardId, nButtons, knobBtn, nLeds, nInputs, profOff, profSize, maxProf;
__xdata uint8_t boardMap[BOARD_MAP_LEN];
__bit hasKnob, ledReversed;

uint8_t SET_layout(__xdata uint8_t* p) {
  uint8_t i, b = p[S_BOARD];
  if(b > N_BOARDS) return 0;
  for(i = 0; i < BOARD_MAP_LEN; i++) boardMap[i] = b ? BOARDS[b - 1][i] : p[S_MAP + i];
  boardId = b;
  nButtons = boardMap[0] & 0x0F;
  knobBtn = boardMap[0] >> 4;
  nLeds = boardMap[1] & 0x0F;
  hasKnob = !(boardMap[1] & MAP_NO_KNOB);
  ledReversed = (boardMap[1] & MAP_LED_REVERSED) ? 1 : 0;
  if(nButtons == 0 || nButtons > MAX_BUTTONS || nLeds > MAX_LEDS) return 0;
  if(knobBtn >= nButtons && knobBtn != NO_KNOB_BUTTON) return 0;
  for(i = 0; i < nButtons; i++) if(boardMap[2 + i] > P3_(7)) return 0;
  nInputs = hasKnob ? nButtons + 2 : nButtons;
  profOff = b ? HEADER_SIZE : HEADER_SIZE + BOARD_MAP_LEN;
  profSize = PROFILE_HEAD + 3 * nInputs;
  // as many profiles as leave at least 24 bytes for records
  for(maxProf = MAX_PROFILES; maxProf > 1 && RECORDS_AT(maxProf) + 24 > RECORDS_END; maxProf--);
  return RECORDS_AT(1) <= RECORDS_END;
}

// ===================================================================================
// Factory settings
// ===================================================================================
#define KNOB        14                  // placeholders, resolved by defInput()
#define RIGHT       12
#define LEFT        13
#define CTRL        0x01                // modifier bits
#define SHIFT       0x02
#define ALT         0x04
#define WIN         0x08
#define ALL         COMBO_ALL_PROFILES

// Profile 1: media keys, profile 2: editing, profile 3: windows and tabs. In every
// profile: hold the knob and press key N for profile N, hold the knob and turn it to
// change the light brightness. In profile 3 turning the knob switches windows (Alt
// stays held until the knob stops). Keys 4-6 of bigger boards get more of the same.
static __code uint8_t DEF_HEADER[11] = {
  SETTINGS_MAGIC, SETTINGS_VERSION, 0, 128, 30, 5, 4, 20, 0, 5, 8
};

static __code uint8_t DEF_COLORS[MAX_PROFILES * PROFILE_HEAD] = {
  0, 170, 255, LED_SOLID,               // cyan
  255, 140, 0, LED_SOLID,               // amber
  0, 224, 176, LED_SOLID,               // teal
};

// {profile, input, type, p1, p2}
static __code uint8_t DEF_ACTIONS[][5] = {
  {0, 0,     ACT_MEDIA, 0xB6, 0},       // previous track
  {0, 1,     ACT_MEDIA, 0xCD, 0},       // play / pause
  {0, 2,     ACT_MEDIA, 0xB5, 0},       // next track
  {0, 3,     ACT_MEDIA, 0xEA, 0},       // volume down
  {0, 4,     ACT_MEDIA, 0xE2, 0},       // mute
  {0, 5,     ACT_MEDIA, 0xE9, 0},       // volume up
  {0, KNOB,  ACT_MEDIA, 0xE2, 0},       // mute
  {0, RIGHT, ACT_MEDIA, 0xE9, 0},       // volume up
  {0, LEFT,  ACT_MEDIA, 0xEA, 0},       // volume down
  {1, 0,     ACT_KEY, CTRL, 0x06},      // Ctrl+C
  {1, 1,     ACT_KEY, CTRL, 0x19},      // Ctrl+V
  {1, 2,     ACT_KEY, CTRL, 0x1D},      // Ctrl+Z
  {1, 3,     ACT_KEY, CTRL, 0x1B},      // Ctrl+X
  {1, 4,     ACT_KEY, CTRL, 0x04},      // Ctrl+A
  {1, 5,     ACT_KEY, CTRL, 0x1C},      // Ctrl+Y
  {1, KNOB,  ACT_KEY, 0, 0x28},         // Enter
  {1, RIGHT, ACT_SCROLL, 0xFF, 0},      // scroll down
  {1, LEFT,  ACT_SCROLL, 1, 0},         // scroll up
  {2, 0,     ACT_KEY, WIN, 0x07},       // Win+D: show desktop
  {2, 1,     ACT_KEY, WIN, 0x08},       // Win+E: file explorer
  {2, 2,     ACT_KEY, WIN | SHIFT, 0x16},    // Win+Shift+S: screenshot
  {2, KNOB,  ACT_KEY, WIN, 0x2B},       // Win+Tab: task view
  {2, RIGHT, ACT_KEY_HOLD, ALT, 0x2B},  // Alt+Tab, Alt held while turning
  {2, LEFT,  ACT_KEY_HOLD, ALT | SHIFT, 0x2B},   // Alt+Shift+Tab
};

// {held, trigger, profile, type, p1, p2}
static __code uint8_t DEF_COMBOS[][6] = {
  {KNOB, 0,     ALL, ACT_PROFILE, 0, PROF_SWITCH},
  {KNOB, 1,     ALL, ACT_PROFILE, 1, PROF_SWITCH},
  {KNOB, 2,     ALL, ACT_PROFILE, 2, PROF_SWITCH},
  {KNOB, RIGHT, ALL, ACT_LIGHT, LIGHT_BRIGHTER, 0},
  {KNOB, LEFT,  ALL, ACT_LIGHT, LIGHT_DIMMER, 0},
};

// Placeholder or key number -> input of this board, 0xFF if it has none
static uint8_t defInput(uint8_t x) {
  if(x == KNOB) return knobBtn < nButtons ? knobBtn : 0xFF;
  if(x == RIGHT || x == LEFT) return hasKnob ? nButtons + x - RIGHT : 0xFF;
  if(x == knobBtn || x >= nButtons) return 0xFF;            // keys come before the knob
  return x;
}

// Factory settings for the board of the current layout
void SET_defaults(__xdata uint8_t* dst) {
  uint8_t i, b = boardId, h, t;
  __xdata uint8_t* q;
  __code uint8_t* d;
  for(i = 0; i < SETTINGS_SIZE; i++) dst[i] = 0;
  for(i = 0; i < sizeof(DEF_HEADER); i++) dst[i] = DEF_HEADER[i];
  dst[S_BOARD] = b;
  if(!b) for(i = 0; i < BOARD_MAP_LEN; i++) dst[S_MAP + i] = boardMap[i];
  SET_layout(dst);
  for(i = RECORDS_AT(maxProf); i < SETTINGS_SIZE; i++) dst[i] = REC_END;
  for(i = 0; i < maxProf * PROFILE_HEAD; i++)
    dst[(uint8_t)(RECORDS_AT(i >> 2) + (i & 3))] = DEF_COLORS[i];
  for(i = 0; i < sizeof(DEF_ACTIONS) / 5; i++) {
    d = DEF_ACTIONS[i];
    t = defInput(d[1]);
    if(d[0] >= maxProf || t == 0xFF) continue;
    q = dst + (uint8_t)(RECORDS_AT(d[0]) + PROFILE_HEAD + 3 * t);
    q[0] = d[2]; q[1] = d[3]; q[2] = d[4];
  }
  q = dst + RECORDS_AT(maxProf);
  for(i = 0; i < sizeof(DEF_COMBOS) / 6 && q + 4 <= dst + RECORDS_END; i++) {
    d = DEF_COMBOS[i];
    h = defInput(d[0]);
    t = defInput(d[1]);
    if(h == 0xFF || t == 0xFF) continue;
    if(d[3] == ACT_PROFILE && d[4] >= maxProf) continue;            // no such profile
    q[0] = (h << 4) | t;
    q[1] = (d[2] << 5) | d[3];
    q[2] = d[4];
    q[3] = d[5];
    q += 4;
  }
  dst[S_CHECKSUM] = SET_checksum(dst);
  SET_valid(dst);                                   // notes SET_padUsed
}

uint8_t SET_checksum(__xdata uint8_t* p) {
  uint8_t i, sum = 0;
  for(i = 0; i < SETTINGS_SIZE - 1; i++) sum += p[i];
  return sum;
}

__xdata uint8_t SET_padUsed;              // the last valid settings use the gamepad

static uint8_t isPadType(uint8_t t) {
  return t == ACT_PAD_BTN || t == ACT_AXIS;
}

// Check settings p; on success also note whether any action needs the gamepad
uint8_t SET_valid(__xdata uint8_t* p) {
  uint8_t i, j, end, pad = 0, n;
  __xdata uint8_t* q;
  if(p[S_MAGIC] != SETTINGS_MAGIC || p[S_VERSION] != SETTINGS_VERSION) return 0;
  if(!SET_layout(p)) return 0;
  n = p[S_PROFILES] ? p[S_PROFILES] : maxProf;
  if(n > maxProf) return 0;
  if(p[S_CHECKSUM] != SET_checksum(p)) return 0;
  if(p[S_HOLD_10MS] < 10 || p[S_HOLD_10MS] > 100) return 0;
  if(p[S_DEBOUNCE_MS] == 0 || p[S_DEBOUNCE_MS] > 30) return 0;
  if(p[S_TAP_MS] == 0 || p[S_TAP_MS] > 50) return 0;
  if(p[S_START_PROFILE] >= n) return 0;
  if(p[S_LED_SPEED] == 0 || p[S_LED_SPEED] > 10) return 0;
  if(p[S_TURN_HOLD] > 30) return 0;
  for(i = 0; i < n; i++) {
    q = p + RECORDS_AT(i);
    if(q[3] > LED_LAST) return 0;
    for(j = 0; j < nInputs; j++) {
      end = q[PROFILE_HEAD + 3 * j] & ACT_TYPE_MASK;
      if(end > ACT_LAST) return 0;
      pad |= isPadType(end);
    }
  }
  i = RECORDS_AT(n);                                 // walk the record list
  while(i < RECORDS_END && p[i] != REC_END) {
    q = p + i;
    if(q[0] < REC_SEQ) {                             // combo
      if(i + 4 > RECORDS_END) return 0;
      if((q[0] >> 4) >= nButtons || (q[0] & 0x0F) >= nInputs) return 0;
      j = q[1] & 0x1F;
      if(j > ACT_LAST) return 0;
      pad |= isPadType(j);
      j = q[1] >> 5;
      if(j >= n && j != COMBO_ALL_PROFILES) return 0;
      i += 4;
    }
    else {                                           // sequence
      if(i + 2 > RECORDS_END || i + 2 + q[1] > RECORDS_END) return 0;
      end = i + 2 + q[1];
      i += 2;
      while(i < end) {
        j = p[i] & STEP_OP;
        if(j == STEP_WAIT || j == STEP_RELEASE) { i++; continue; }
        if(j > STEP_HOLD) return 0;
        j = p[i] & ~STEP_OP;
        if(j > ACT_LAST || j == ACT_SEQ) return 0;
        pad |= isPadType(j);
        i += 3;
      }
      if(i != end) return 0;
    }
  }
  SET_padUsed = pad;
  return 1;
}

// Data-Flash bytes live at even addresses starting at DATA_FLASH_ADDR
static uint8_t DF_read(uint8_t addr) {
  ROM_ADDR_H = DATA_FLASH_ADDR >> 8;
  ROM_ADDR_L = addr << 1;
  ROM_CTRL   = ROM_CMD_READ;
  return ROM_DATA_L;
}

static void DF_write(uint8_t addr, uint8_t val) {
  ROM_ADDR_H = DATA_FLASH_ADDR >> 8;
  ROM_ADDR_L = addr << 1;
  ROM_DATA_L = val;
  if(ROM_STATUS & bROM_ADDR_OK) ROM_CTRL = ROM_CMD_WRITE;
}

void SET_load(void) {
  uint8_t i;
  for(i = 0; i < SETTINGS_SIZE; i++) cfg[i] = DF_read(i);
  if(SET_valid(cfg)) return;
  // Keep the board of settings that are only partly broken, so the pad does not
  // forget what it is.
  if(cfg[S_MAGIC] != SETTINGS_MAGIC || cfg[S_VERSION] != SETTINGS_VERSION || !SET_layout(cfg)) {
    cfg[S_BOARD] = DEFAULT_BOARD;
    SET_layout(cfg);
  }
  SET_defaults(cfg);
}

void SET_save(void) {
  uint8_t i;
  cfg[S_CHECKSUM] = SET_checksum(cfg);
  SAFE_MOD = 0x55;
  SAFE_MOD = 0xAA;
  GLOBAL_CFG |= bDATA_WE;
  SAFE_MOD = 0x00;
  for(i = 0; i < SETTINGS_SIZE; i++) {
    if(DF_read(i) != cfg[i]) DF_write(i, cfg[i]);   // spare write cycles
  }
  SAFE_MOD = 0x55;
  SAFE_MOD = 0xAA;
  GLOBAL_CFG &= ~bDATA_WE;
  SAFE_MOD = 0x00;
}

// Store one byte without saving the rest of the live settings: the stored checksum
// is recomputed from the stored bytes.
void SET_saveByte(uint8_t idx, uint8_t val) {
  uint8_t i, sum = 0;
  SAFE_MOD = 0x55;
  SAFE_MOD = 0xAA;
  GLOBAL_CFG |= bDATA_WE;
  SAFE_MOD = 0x00;
  if(DF_read(idx) != val) DF_write(idx, val);
  for(i = 0; i < SETTINGS_SIZE - 1; i++) sum += DF_read(i);
  if(DF_read(S_CHECKSUM) != sum) DF_write(S_CHECKSUM, sum);
  SAFE_MOD = 0x55;
  SAFE_MOD = 0xAA;
  GLOBAL_CFG &= ~bDATA_WE;
  SAFE_MOD = 0x00;
}
