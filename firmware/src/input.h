// ===================================================================================
// Knob + key input, sampled by a 4 kHz timer interrupt
// ===================================================================================
// The stock firmware polls the knob from its main loop and blocks on USB, so it can
// only see ~21 detents per second and drops or reverses steps when turned faster.
// Here the quadrature signal is decoded inside a Timer2 interrupt every 250 us,
// independent of USB traffic, into an absolute detent counter that never loses steps.

#pragma once
#include <stdint.h>
#include "ch554.h"

// Debounced button bits (INP_buttons)
#define BTN_KEY1    0x01
#define BTN_KEY2    0x02
#define BTN_KEY3    0x04
#define BTN_KNOB    0x08

void INP_init(void);
int16_t INP_position(void);       // absolute knob position in detents (atomic read)
uint8_t INP_buttons(void);        // debounced button state
uint16_t INP_millis(void);        // free running millisecond counter
void INP_setHalfstep(uint8_t on);
void INP_setInvert(uint8_t on);

void INP_ISR(void) __interrupt(INT_NO_TMR2);
