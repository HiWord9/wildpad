// Simulator test for the knob decoder in src/input.c (run by test/run_tests.py on s51).
// Port 3 bits 0/1 are driven by writing the P3 latch (the simulator has no external
// load, so reading P3 returns the latch). Timer2 interrupts run for real.

#include "ch554.h"
#include "input.h"

#define N_RESULTS 16
__xdata __at (0x0200) volatile int16_t results[N_RESULTS];
__xdata __at (0x0240) volatile uint8_t done_flag;

static void wait_ticks(uint16_t ticks) {
  // one timer tick = 333 machine cycles; this loop is ~10 cycles per iteration
  volatile uint16_t i;
  for(; ticks; ticks--) for(i = 0; i < 40; i++);
}

// clockwise detent: 3 -> 1 -> 0 -> 2 -> 3   (state = A<<1 | B)
static __code uint8_t CW[4]  = {1, 0, 2, 3};
static __code uint8_t CCW[4] = {2, 0, 1, 3};

static void set_state(uint8_t s) {
  P3 = 0xFC | s;
}

static void detents(__code uint8_t* seq, uint8_t n, uint16_t ticks_per_phase) {
  uint8_t i, j;
  for(i = 0; i < n; i++)
    for(j = 0; j < 4; j++) { set_state(seq[j]); wait_ticks(ticks_per_phase); }
}

void done(void) {
  done_flag = 0xAA;
  while(1);
}

void main(void) {
  set_state(3);
  INP_init();
  EA = 1;
  wait_ticks(4);

  detents(CW, 10, 8);                    // slow clockwise: +10
  results[0] = INP_position();

  detents(CCW, 3, 8);                    // slow counter-clockwise: -3
  results[1] = INP_position();

  detents(CW, 40, 2);                    // fast: 2 ticks per phase (~125 detents/s real)
  results[2] = INP_position();

  // contact bounce: start of a CW step chatters, then completes once
  set_state(1); wait_ticks(2); set_state(3); wait_ticks(2);
  set_state(1); wait_ticks(2); set_state(3); wait_ticks(2);
  set_state(1); wait_ticks(2); set_state(0); wait_ticks(2);
  set_state(2); wait_ticks(2); set_state(0); wait_ticks(2);   // bounce mid-way
  set_state(2); wait_ticks(2); set_state(3); wait_ticks(4);
  results[3] = INP_position();           // +1

  // half a step and back (knob nudged but released before the next detent)
  set_state(1); wait_ticks(3); set_state(0); wait_ticks(3);
  set_state(1); wait_ticks(3); set_state(3); wait_ticks(4);
  results[4] = INP_position();           // unchanged

  INP_setInvert(1);
  detents(CW, 5, 3);
  results[5] = INP_position();           // inverted: -5
  INP_setInvert(0);

  // fast direction reversals: CW, CCW, CW, CCW ... 20 times
  {
    uint8_t k;
    for(k = 0; k < 10; k++) { detents(CW, 1, 2); detents(CCW, 1, 2); }
  }
  results[6] = INP_position();           // unchanged by reversals

  results[7] = INP_millis() > 0;
  done();
}
