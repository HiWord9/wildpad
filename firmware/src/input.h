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

void INP_init(void);
int16_t INP_position(void);       // absolute knob position in detents (atomic read)
uint8_t INP_buttons(void);        // debounced button state, bit i = button i
uint16_t INP_millis(void);        // free running millisecond counter
void INP_setHalfstep(uint8_t on);
void INP_setInvert(uint8_t on);
void INP_setDebounce(uint8_t ms);
void INP_setPins(__xdata uint8_t* pins, uint8_t n);   // button i on pins[i] (P1.x = x, P3.x = 8 + x)
uint8_t INP_readRaw(void);        // undebounced buttons; also used by the ISR, so only
                                  // call it from main() before INP_init()

void INP_ISR(void) __interrupt(INT_NO_TMR2);
