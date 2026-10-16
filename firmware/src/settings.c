// ===================================================================================
// Persistent settings (CH552 Data-Flash, 128 bytes)
// ===================================================================================

#include "ch554.h"
#include "settings.h"

__xdata settings_t cfg;

#define A(t, a, b)  t, a, b
#define NONE        A(ACT_NONE, 0, 0)
#define CON(u)      A(ACT_CONSUMER, (u) & 0xFF, (u) >> 8)

// Defaults: media controller in keyboard mode (Fn layer on long press of key 2),
// Rocket League friendly wheel mode.
static __code uint8_t DEFAULTS[SETTINGS_SIZE] = {
  SETTINGS_MAGIC, SETTINGS_VERSION,
  MODE_KEYBOARD,
  FLAG_CLAMP,
  10,                 // steer_range: half a turn of a 20-detent knob
  96,                 // brightness
  2,                  // tap_ms
  5,                  // debounce_ms
  25,                 // hold: 250 ms
  20,                 // detents per revolution
  LED_REACTIVE,
  96,                 // led_hue: cyan (0 red, 64 green, 128 blue)
  AXIS_LX,
  0, 0, 0,
  // base: key1, key2, key3, knob press, (unused x2), CW, CCW
  CON(0xB6), CON(0xCD), CON(0xB5), CON(0xE2), NONE, NONE, CON(0xE9), CON(0xEA),
  // fn layer: knob scrolls instead of changing volume
  NONE, NONE, NONE, NONE, NONE, NONE, A(ACT_WHEEL, 1, 0), A(ACT_WHEEL, 0xFF, 0),
  // hold actions: key2 held = Fn layer
  NONE, A(ACT_FN, 0, 0), NONE, NONE, NONE, NONE,
  // wheel mode: key1 throttle, key2 jump, key3 boost, knob press recenter
  A(ACT_PAD, PAD_RT, 0), A(ACT_PAD, PAD_A, 0), A(ACT_PAD, PAD_B, 0), A(ACT_RECENTER, 0, 0), NONE, NONE,
  0, 0, 0,
  0                   // checksum (filled in by SET_defaults)
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
  if(s->mode > MODE_WHEEL) return 0;
  if(s->steer_range == 0 || s->steer_range > 100) return 0;
  if(s->tap_ms == 0 || s->tap_ms > 50) return 0;
  if(s->debounce_ms == 0 || s->debounce_ms > 30) return 0;
  if(s->hold_10ms < 10 || s->hold_10ms > 100) return 0;
  if(s->led_mode > LED_BREATHE || s->axis > AXIS_TRIGGERS) return 0;
  return 1;
}

void SET_defaults(__xdata uint8_t* dst) {
  uint8_t i;
  for(i = 0; i < SETTINGS_SIZE; i++) dst[i] = DEFAULTS[i];
  dst[SETTINGS_SIZE - 1] = SET_checksum(dst);
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
  if(!SET_valid(p)) SET_defaults(p);
}

void SET_save(void) {
  uint8_t i;
  __xdata uint8_t* p = (__xdata uint8_t*)&cfg;
  cfg.checksum = SET_checksum(p);
  SAFE_MOD = 0x55;
  SAFE_MOD = 0xAA;
  GLOBAL_CFG |= bDATA_WE;
  SAFE_MOD = 0x00;
  for(i = 0; i < SETTINGS_SIZE; i++) {
    if(DF_read(i) != p[i]) DF_write(i, p[i]);   // spare write cycles
  }
  SAFE_MOD = 0x55;
  SAFE_MOD = 0xAA;
  GLOBAL_CFG &= ~bDATA_WE;
  SAFE_MOD = 0x00;
}
