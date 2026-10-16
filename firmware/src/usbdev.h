// ===================================================================================
// USB device layer (both modes) for CH552
// ===================================================================================
#pragma once
#include <stdint.h>
#include "ch554.h"
#include "descr.h"

// Endpoint buffers live at the bottom of XRAM (XRAM_LOC for variables is 0x0100)
#define EP0_ADDR        0x0000
#define EP1_ADDR        0x0040
#define EP2_ADDR        0x0080
#define EP3_ADDR        0x00C0
#define EP0_SIZE        64

__xdata __at (EP0_ADDR) uint8_t EP0_buffer[64];
__xdata __at (EP1_ADDR) uint8_t EP1_buffer[64];
__xdata __at (EP2_ADDR) uint8_t EP2_buffer[64];
__xdata __at (EP3_ADDR) uint8_t EP3_buffer[64];

extern uint8_t USB_mode;                              // MODE_KEYBOARD / MODE_GAMEPAD
extern volatile __bit USB_featureNew;                 // host wrote a feature report
extern __xdata uint8_t USB_featureOut[FEATURE_REPORT_LEN];   // host -> device
extern __xdata uint8_t USB_featureIn[FEATURE_REPORT_LEN];    // device -> host

void USB_init(uint8_t mode);
void USB_disconnect(void);
uint8_t USB_configured(void);
uint8_t USB_ep1Ready(void);
void USB_ep1Send(__xdata uint8_t* buf, uint8_t len);
uint8_t USB_ep3Ready(void);
void USB_ep3Send(__xdata uint8_t* buf, uint8_t len);

void USB_ISR(void) __interrupt(INT_NO_USB);
