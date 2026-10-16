// ===================================================================================
// Persistent settings (CH552 Data-Flash, 128 bytes)
// ===================================================================================

#include "ch554.h"
#include "settings.h"

__xdata uint8_t cfg[SETTINGS_SIZE];

#define KNOB        BOARD_KNOB_BUTTON
#define CTRL        0x01                // left Ctrl modifier bit
#define ALL         COMBO_ALL_PROFILES

// Factory settings. Profile 1: media keys, profile 2: editing, profile 3: mouse.
// In every profile: hold the knob and press key N for profile N, hold the knob and
// turn it to change the light brightness.
static __code uint8_t DEF_HEADER[10] = {
  SETTINGS_MAGIC, SETTINGS_VERSION, 0, 128, 30, 5, 4, 20, 0, 5
};

static __code uint8_t DEF_COLORS[3][4] = {
  {0, 170, 255, LED_SOLID},             // cyan
  {255, 140, 0, LED_SOLID},             // amber
  {220, 0, 255, LED_SOLID},             // violet
};

// {profile, input, type, p1, p2}
static __code uint8_t DEF_ACTIONS[][5] = {
  {0, 0,        ACT_MEDIA, 0xB6, 0},      // previous track
  {0, 1,        ACT_MEDIA, 0xCD, 0},      // play / pause
  {0, 2,        ACT_MEDIA, 0xB5, 0},      // next track
  {0, KNOB,     ACT_MEDIA, 0xE2, 0},      // mute
  {0, IN_RIGHT, ACT_MEDIA, 0xE9, 0},      // volume up
  {0, IN_LEFT,  ACT_MEDIA, 0xEA, 0},      // volume down
  {1, 0,        ACT_KEY, CTRL, 0x06},     // Ctrl+C
  {1, 1,        ACT_KEY, CTRL, 0x19},     // Ctrl+V
  {1, 2,        ACT_KEY, CTRL, 0x1D},     // Ctrl+Z
  {1, KNOB,     ACT_KEY, 0, 0x28},        // Enter
  {1, IN_RIGHT, ACT_SCROLL, 0xFF, 0},     // scroll down
  {1, IN_LEFT,  ACT_SCROLL, 1, 0},        // scroll up
  {2, 0,        ACT_MOUSE_BTN, 1, 0},     // left click
  {2, 1,        ACT_MOUSE_BTN, 4, 0},     // middle click
  {2, 2,        ACT_MOUSE_BTN, 2, 0},     // right click
  {2, KNOB,     ACT_MOUSE_BTN, 8, 0},     // back
  {2, IN_RIGHT, ACT_SCROLL, 0, 1},        // scroll right
  {2, IN_LEFT,  ACT_SCROLL, 0, 0xFF},     // scroll left
};

// {held, trigger, profile, type, p1, p2}
static __code uint8_t DEF_COMBOS[][6] = {
  {KNOB, 0,        ALL, ACT_PROFILE, 0, PROF_SWITCH},
  {KNOB, 1,        ALL, ACT_PROFILE, 1, PROF_SWITCH},
  {KNOB, 2,        ALL, ACT_PROFILE, 2, PROF_SWITCH},
  {KNOB, IN_RIGHT, ALL, ACT_LIGHT, LIGHT_BRIGHTER, 0},
  {KNOB, IN_LEFT,  ALL, ACT_LIGHT, LIGHT_DIMMER, 0},
};

void SET_defaults(__xdata uint8_t* dst) {
  uint8_t i, j;
  __xdata uint8_t* q;
  __code uint8_t* d;
  for(i = 0; i < SETTINGS_SIZE; i++) dst[i] = i < COMBOS_OFFSET ? 0 : COMBO_EMPTY;
  for(i = 0; i < sizeof(DEF_HEADER); i++) dst[i] = DEF_HEADER[i];
  for(i = 0; i < N_PROFILES; i++)
    for(j = 0; j < PROFILE_HEAD; j++) dst[PROFILES_OFFSET + i * PROFILE_SIZE + j] = DEF_COLORS[i][j];
  for(i = 0; i < sizeof(DEF_ACTIONS) / 5; i++) {
    d = DEF_ACTIONS[i];
    if(d[0] >= N_PROFILES) continue;
    q = dst + PROFILES_OFFSET + d[0] * PROFILE_SIZE + PROFILE_HEAD + 3 * d[1];
    q[0] = d[2]; q[1] = d[3]; q[2] = d[4];
  }
  q = dst + COMBOS_OFFSET;
  for(i = 0; i < sizeof(DEF_COMBOS) / 6 && i < N_COMBOS; i++) {
    d = DEF_COMBOS[i];
    if(d[1] < BOARD_BUTTONS && d[1] >= N_PROFILES && d[3] == ACT_PROFILE) continue;
    q[0] = (d[0] << 4) | d[1];
    q[1] = (d[2] << 5) | d[3];
    q[2] = d[4];
    q[3] = d[5];
    q += COMBO_SIZE;
  }
  dst[S_CHECKSUM] = SET_checksum(dst);
}

uint8_t SET_checksum(__xdata uint8_t* p) {
  uint8_t i, sum = 0;
  for(i = 0; i < SETTINGS_SIZE - 1; i++) sum += p[i];
  return sum;
}

uint8_t SET_valid(__xdata uint8_t* p) {
  uint8_t i, j;
  __xdata uint8_t* q;
  if(p[S_MAGIC] != SETTINGS_MAGIC || p[S_VERSION] != SETTINGS_VERSION) return 0;
  if(p[S_CHECKSUM] != SET_checksum(p)) return 0;
  if(p[S_HOLD_10MS] < 10 || p[S_HOLD_10MS] > 100) return 0;
  if(p[S_DEBOUNCE_MS] == 0 || p[S_DEBOUNCE_MS] > 30) return 0;
  if(p[S_TAP_MS] == 0 || p[S_TAP_MS] > 50) return 0;
  if(p[S_START_PROFILE] >= N_PROFILES) return 0;
  if(p[S_LED_SPEED] == 0 || p[S_LED_SPEED] > 10) return 0;
  for(i = 0; i < N_PROFILES; i++) {
    q = p + PROFILES_OFFSET + i * PROFILE_SIZE;
    if(q[3] > LED_LAST) return 0;
    for(j = 0; j < N_INPUTS; j++)
      if(q[PROFILE_HEAD + 3 * j] > ACT_LAST) return 0;
  }
  for(i = 0; i < N_COMBOS; i++) {
    q = p + COMBOS_OFFSET + i * COMBO_SIZE;
    if(q[0] == COMBO_EMPTY) continue;
    if((q[0] >> 4) >= BOARD_BUTTONS || (q[0] & 0x0F) >= N_INPUTS) return 0;
    if((q[1] & 0x1F) > ACT_LAST) return 0;
    j = q[1] >> 5;
    if(j >= N_PROFILES && j != COMBO_ALL_PROFILES) return 0;
  }
  return 1;
}

static uint8_t isPadType(uint8_t t) {
  return t == ACT_PAD_BTN || t == ACT_AXIS;
}

// True if any action needs the gamepad interface
uint8_t SET_usesGamepad(__xdata uint8_t* p) {
  uint8_t i, j;
  __xdata uint8_t* q;
  for(i = 0; i < N_PROFILES; i++) {
    q = p + PROFILES_OFFSET + i * PROFILE_SIZE + PROFILE_HEAD;
    for(j = 0; j < N_INPUTS; j++)
      if(isPadType(q[3 * j])) return 1;
  }
  for(i = 0; i < N_COMBOS; i++) {
    q = p + COMBOS_OFFSET + i * COMBO_SIZE;
    if(q[0] != COMBO_EMPTY && isPadType(q[1] & 0x1F)) return 1;
  }
  return 0;
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
  if(!SET_valid(cfg)) SET_defaults(cfg);
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
