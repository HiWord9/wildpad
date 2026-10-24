// ===================================================================================
// Clock, delays, watchdog, resets and the WS2812 LED chain
// ===================================================================================

#pragma once
#include <stdint.h>
#include "ch552.h"

#define CLK_12MHZ         4             // CLOCK_CFG system clock selections
#define CLK_16MHZ         5
#define BOOTLOADER_ADDR   0x3800

#define SYS_wdtFeed()     (WDOG_COUNT = 0)

void SYS_init(void);                    // 16 MHz, LED data pin
void SYS_clock(uint8_t sel);
void SYS_config(uint8_t set, uint8_t clear);  // GLOBAL_CFG bits
void SYS_wdtStart(void);
void SYS_reset(void);                   // only works with the watchdog running
void SYS_bootloader(void);
void SYS_delayMs(uint16_t ms);

void WS_byte(uint8_t v);                // with interrupts off
void WS_color(uint8_t r, uint8_t g, uint8_t b);
