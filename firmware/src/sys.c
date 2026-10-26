// ===================================================================================
// Clock, delays, watchdog, resets and the WS2812 LED chain
// ===================================================================================

#include "sys.h"
#include "config.h"

#if FREQ_SYS != 16000000
#error the delays and the LED timing are counted in 16 MHz cycles
#endif

__sbit __at(0xB0 + LED_PIN) WS_PIN;     // on P3

void SYS_clock(uint8_t sel) {
  SAFE_MOD = 0x55;
  SAFE_MOD = 0xAA;
  CLOCK_CFG = CLOCK_CFG & ~MASK_SYS_CK_SEL | sel;
  SAFE_MOD = 0;
}

void SYS_config(uint8_t set, uint8_t clear) {
  SAFE_MOD = 0x55;
  SAFE_MOD = 0xAA;
  GLOBAL_CFG = GLOBAL_CFG & ~clear | set;
  SAFE_MOD = 0;
}

void SYS_init(void) {
  WS_PIN = 0;
  P3_MOD_OC &= ~(1 << LED_PIN);         // push-pull
  P3_DIR_PU |= 1 << LED_PIN;
  SYS_clock(CLK_16MHZ);
}

void SYS_wdtStart(void) {
  WDOG_COUNT = 0;
  SYS_config(bWDOG_EN, 0);
}

void SYS_reset(void) {
  SYS_config(bSW_RESET, 0);
  while(1);
}

void SYS_bootloader(void) {
  EA = 0;
  USB_CTRL = 0;
  __asm
    lcall BOOTLOADER_ADDR
  __endasm;
}

// 250 passes of 16 cycles. Keeps r7: SDCC doesn't see what inline assembly uses and
// keeps its own values in registers across the call.
static void wait250us(void) __naked {
  __asm
    push ar7
    mov  r7, #250
  00001$:
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    djnz r7, 00001$
    pop  ar7
    ret
  __endasm;
}

void SYS_delayMs(uint16_t ms) {
  uint8_t i;
  while(ms--)
    for(i = 4; i; i--) wait250us();
}

// MSB first, 20 cycles (1.25 us) a bit: high for 4 cycles for a 0, 12 for a 1
void WS_byte(uint8_t v) {
  v;
  __asm
    mov  a, dpl
    mov  r7, #8
  00001$:
    setb _WS_PIN
    rlc  a
    nop
    mov  _WS_PIN, c
    nop
    nop
    nop
    nop
    nop
    nop
    clr  _WS_PIN
    nop
    nop
    djnz r7, 00001$
  __endasm;
}

void WS_color(uint8_t r, uint8_t g, uint8_t b) {
  WS_byte(g);
  WS_byte(r);
  WS_byte(b);
}
