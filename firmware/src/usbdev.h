// ===================================================================================
// USB device layer for CH552
// ===================================================================================
#pragma once
#include <stdint.h>
#include "ch554.h"
#include "descr.h"

// Device layouts (see descr.h)
#define USB_KEYBOARD    0
#define USB_GAMEPAD     1

// Endpoint buffers at the bottom of XRAM (variables start at XRAM_LOC = 0x0140).
// EP2 has RX at EP2_ADDR and TX at EP2_ADDR + 64 when both directions are enabled.
#define EP0_ADDR        0x0000
#define EP1_ADDR        0x0040
#define EP2_ADDR        0x0080
#define EP3_ADDR        0x0100
#define EP0_SIZE        64

__xdata __at (EP0_ADDR) uint8_t EP0_buffer[64];
__xdata __at (EP1_ADDR) uint8_t EP1_buffer[64];
__xdata __at (EP2_ADDR) uint8_t EP2_buffer[128];
__xdata __at (EP3_ADDR) uint8_t EP3_buffer[64];

extern uint8_t USB_personality;
extern volatile __bit USB_featureNew;                       // host wrote a feature report
extern __xdata uint8_t USB_featureOut[FEATURE_REPORT_LEN];  // host -> device
extern __xdata uint8_t USB_featureIn[FEATURE_REPORT_LEN];   // device -> host

void USB_init(uint8_t personality);
void USB_disconnect(void);
uint8_t USB_hidReady(void);                                 // keyboard/mouse endpoint
void USB_hidSend(__xdata uint8_t* buf, uint8_t len);
uint8_t USB_statusReady(void);                              // vendor live-state endpoint
void USB_statusSend(__xdata uint8_t* buf, uint8_t len);
uint8_t USB_padReady(void);                                 // XInput endpoint
void USB_padSend(__xdata uint8_t* buf, uint8_t len);

void USB_ISR(void) __interrupt(INT_NO_USB);
