// ===================================================================================
// Persistent settings (stored in the CH552 128-byte Data-Flash)
// ===================================================================================

#include "ch554.h"
#include "settings.h"

__xdata settings_t cfg;

// Default configuration: media controller in keyboard mode, Rocket League-ish pad.
static __code uint8_t DEFAULTS[SETTINGS_SIZE] = {
  SETTINGS_MAGIC, SETTINGS_VERSION,
  MODE_KEYBOARD,
  FLAG_CLAMP,
  10,                       // steer_range: half a turn (10 of 20 detents) = full lock
  96,                       // brightness
  2,                        // tap_ms
  PAD_RT, PAD_A, PAD_B, PAD_RECENTER,
  ACT_CONSUMER, 0xB6, 0x00, // key1: previous track
  ACT_CONSUMER, 0xCD, 0x00, // key2: play/pause
  ACT_CONSUMER, 0xB5, 0x00, // key3: next track
  ACT_CONSUMER, 0xE2, 0x00, // knob switch: mute
  ACT_CONSUMER, 0xE9, 0x00, // knob clockwise: volume up
  ACT_CONSUMER, 0xEA, 0x00, // knob counter-clockwise: volume down
  0                         // checksum (filled in by SET_defaults)
};

uint8_t SET_checksum(__xdata uint8_t* p) {
  uint8_t i, sum = 0;
  for(i = 0; i < SETTINGS_SIZE - 1; i++) sum += p[i];
  return sum;
}

uint8_t SET_valid(__xdata uint8_t* p) {
  settings_t __xdata *s = (settings_t __xdata *)p;
  if(s->magic != SETTINGS_MAGIC || s->version != SETTINGS_VERSION) return 0;
  if(s->checksum != SET_checksum(p)) return 0;
  if(s->mode > MODE_GAMEPAD) return 0;
  if(s->steer_range == 0 || s->steer_range > 100) return 0;
  if(s->tap_ms == 0 || s->tap_ms > 50) return 0;
  return 1;
}

void SET_defaults(void) {
  uint8_t i;
  __xdata uint8_t* p = (__xdata uint8_t*)&cfg;
  for(i = 0; i < SETTINGS_SIZE; i++) p[i] = DEFAULTS[i];
  cfg.checksum = SET_checksum(p);
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
  __xdata uint8_t* p = (__xdata uint8_t*)&cfg;
  for(i = 0; i < SETTINGS_SIZE; i++) p[i] = DF_read(i);
  if(!SET_valid(p)) SET_defaults();
}

void SET_save(void) {
  uint8_t i;
  __xdata uint8_t* p = (__xdata uint8_t*)&cfg;
  cfg.checksum = SET_checksum(p);
  SAFE_MOD = 0x55;
  SAFE_MOD = 0xAA;
  GLOBAL_CFG |= bDATA_WE;                   // enable Data-Flash write
  SAFE_MOD = 0x00;
  for(i = 0; i < SETTINGS_SIZE; i++) {
    if(DF_read(i) != p[i]) DF_write(i, p[i]);  // spare write cycles
  }
  SAFE_MOD = 0x55;
  SAFE_MOD = 0xAA;
  GLOBAL_CFG &= ~bDATA_WE;                  // write-protect again
  SAFE_MOD = 0x00;
}
