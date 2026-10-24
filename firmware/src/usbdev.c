// ===================================================================================
// USB device layer for CH552
// ===================================================================================

#pragma nooverlay
#include "usbdev.h"
#include "config.h"

// Requests: standard (USB 2.0, 9.4) and HID class (HID 1.11, 7.2)
#define GET_STATUS        0x00
#define CLEAR_FEATURE     0x01
#define SET_FEATURE       0x03
#define SET_ADDRESS       0x05
#define GET_DESCRIPTOR    0x06
#define GET_CONFIGURATION 0x08
#define SET_CONFIGURATION 0x09
#define GET_INTERFACE     0x0A
#define SET_INTERFACE     0x0B
#define HID_GET_REPORT    0x01
#define HID_GET_IDLE      0x02
#define HID_GET_PROTOCOL  0x03
#define HID_SET_REPORT    0x09
#define HID_SET_IDLE      0x0A
#define HID_SET_PROTOCOL  0x0B

#define STALL             0xFF          // instead of a reply length

uint8_t USB_personality;
volatile __bit USB_featureNew;
volatile __bit USB_updateGo;
volatile uint8_t USB_stallTicks;
__xdata uint8_t USB_featureOut[FEATURE_REPORT_LEN];
__xdata uint8_t USB_featureIn[FEATURE_REPORT_LEN];

static volatile __bit ep1Busy, ep2Busy, ep3Busy;
static __bit sending;                   // a descriptor goes out on EP0
static __bit newAddress;                // set the address after the status stage
static __bit reportData;                // SET_REPORT: data stage follows
static uint8_t configured, address;
static uint8_t reqType, req, reqIndex;  // wIndex low byte: interface or endpoint
static uint16_t reqLen;
static __code uint8_t* tx;
static uint8_t txLeft;

static __code uint8_t* __code DEVICE[] = { DevDescrKeyboard, DevDescrGamepad, DevDescrUpdate };
static __code uint8_t* __code CONFIG[] = { CfgDescrKeyboard, CfgDescrGamepad, CfgDescrUpdate };

static uint8_t nextChunk(void) {
  uint8_t i, n = txLeft < EP0_SIZE ? txLeft : EP0_SIZE;
  for(i = 0; i < n; i++) EP0_buffer[i] = tx[i];
  tx += n;
  txLeft -= n;
  return n;
}

static uint8_t sendCode(__code uint8_t* d, uint8_t len) {
  tx = d;
  txLeft = reqLen < len ? reqLen : len;
  sending = 1;
  return nextChunk();
}

static uint8_t getDescriptor(void) {
  uint8_t idx = EP0_buffer[2];
  __code uint8_t* d;
  switch(EP0_buffer[3]) {
    case 1:
      d = DEVICE[USB_personality];
      break;
    case 2:
      d = CONFIG[USB_personality];
      return sendCode(d, d[2]);
    case 3:
      if(idx == MS_OS_STRING_INDEX && USB_personality != USB_KEYBOARD) d = MsOsString;
      else if(idx < STR_COUNT) d = Strings[idx];
      else return STALL;
      break;
    case 0x21:                          // HID
      if(reqIndex > 1) return STALL;
      d = CfgDescrKeyboard + (reqIndex ? CFG_HID1_OFFSET : CFG_HID0_OFFSET);
      break;
    case 0x22:                          // HID report
      if(USB_personality == USB_UPDATE || reqIndex > 1) return STALL;
      return reqIndex ? sendCode(Report1, Report1Len) : sendCode(Report0, Report0Len);
    default:
      return STALL;
  }
  return sendCode(d, d[0]);
}

// SET_FEATURE / CLEAR_FEATURE (ENDPOINT_HALT); clearing also resets the data toggle
static uint8_t haltIn(uint8_t ctrl, uint8_t on) {
  ctrl &= ~(bUEP_T_TOG | MASK_UEP_T_RES);
  return ctrl | (on ? UEP_T_RES_STALL : UEP_T_RES_NAK);
}

static void halt(uint8_t on) {
  switch(reqIndex) {
    case 0x81: UEP1_CTRL = haltIn(UEP1_CTRL, on); ep1Busy = on; break;
    case 0x82: UEP2_CTRL = haltIn(UEP2_CTRL, on); ep2Busy = on; break;
    case 0x83: UEP3_CTRL = haltIn(UEP3_CTRL, on); ep3Busy = on; break;
    case 0x02: UEP2_CTRL = UEP2_CTRL & ~(bUEP_R_TOG | MASK_UEP_R_RES) | (on ? UEP_R_RES_STALL : UEP_R_RES_ACK); break;
  }
}

static uint8_t standardRequest(void) {
  switch(req) {
    case GET_DESCRIPTOR:
      return getDescriptor();
    case SET_ADDRESS:
      address = EP0_buffer[2];
      newAddress = 1;
      return 0;
    case GET_CONFIGURATION:
      EP0_buffer[0] = configured;
      return 1;
    case SET_CONFIGURATION:
      configured = EP0_buffer[2];
      return 0;
    case GET_INTERFACE:
      EP0_buffer[0] = 0;
      return 1;
    case SET_INTERFACE:
      return 0;
    case GET_STATUS:
      EP0_buffer[0] = 0;
      EP0_buffer[1] = 0;
      return 2;
    case CLEAR_FEATURE:
    case SET_FEATURE:
      if((reqType & 0x1F) == 2) halt(req == SET_FEATURE);
      return 0;
  }
  return STALL;
}

// Interface 1 carries the configuration protocol in feature reports
static uint8_t classRequest(void) {
  uint8_t i, n;
  __bit feature = reqIndex == 1 && EP0_buffer[3] == 3;
  if(reqIndex > 1) return STALL;
  switch(req) {
    case HID_GET_REPORT:
      n = feature ? FEATURE_REPORT_LEN : EP0_SIZE;
      for(i = 0; i < n; i++) EP0_buffer[i] = feature ? USB_featureIn[i] : 0;
      return n;
    case HID_SET_REPORT:
      reportData = 1;
      return 0;
    case HID_SET_IDLE:
    case HID_SET_PROTOCOL:
      return 0;
    case HID_GET_IDLE:
      EP0_buffer[0] = 0;
      return 1;
    case HID_GET_PROTOCOL:
      EP0_buffer[0] = 1;                // report protocol
      return 1;
  }
  return STALL;
}

// Microsoft OS descriptors, and the update layout's go-ahead
static uint8_t vendorRequest(void) {
  if(USB_personality == USB_KEYBOARD) return STALL;
  if(!(reqType & 0x80)) {
    if(USB_personality != USB_UPDATE || req != UPDATE_GO_REQUEST) return STALL;
    USB_updateGo = 1;
    return 0;
  }
  if(req != MS_OS_VENDOR_CODE) return STALL;
  if(reqIndex == 4) return sendCode(USB_personality == USB_UPDATE ? MsCompatIdUpdate : MsCompatId, 40);
  if(reqIndex == 5) return sendCode(MsNoProps, sizeof(MsNoProps));
  return STALL;
}

static void ep0Setup(void) {
  uint8_t n = STALL;
  sending = newAddress = reportData = 0;
  if(USB_RX_LEN == 8) {
    reqType  = EP0_buffer[0];
    req      = EP0_buffer[1];
    reqIndex = EP0_buffer[4];
    reqLen   = EP0_buffer[6] | EP0_buffer[7] << 8;
    switch(reqType & 0x60) {
      case 0x00: n = standardRequest(); break;
      case 0x20: n = classRequest(); break;
      case 0x40: n = vendorRequest(); break;
    }
  }
  if(n == STALL) {
    UEP0_CTRL = bUEP_R_TOG | bUEP_T_TOG | UEP_R_RES_STALL | UEP_T_RES_STALL;
    if(USB_personality == USB_UPDATE) USB_stallTicks = 20;   // see INP_ISR()
    return;
  }
  if(n > reqLen) n = reqLen;
  UEP0_T_LEN = n;
  UEP0_CTRL = bUEP_R_TOG | bUEP_T_TOG | UEP_R_RES_ACK | UEP_T_RES_ACK;
}

static void ep0In(void) {
  if(sending) {
    UEP0_T_LEN = nextChunk();
    UEP0_CTRL ^= bUEP_T_TOG;
    return;
  }
  if(newAddress) USB_DEV_AD = USB_DEV_AD & bUDA_GP_BIT | address;
  UEP0_T_LEN = 0;
  UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
}

static void ep0Out(void) {
  uint8_t i, n;
  if(reportData) {                      // the zero-length status stage is armed already
    if(reqIndex == 1 && !USB_featureNew) {
      n = USB_RX_LEN;
      for(i = 0; i < FEATURE_REPORT_LEN; i++) USB_featureOut[i] = i < n ? EP0_buffer[i] : 0;
      USB_featureNew = 1;
    }
    UEP0_CTRL ^= bUEP_R_TOG;
    return;
  }
  UEP0_T_LEN = 0;                       // status stage of a read
  UEP0_CTRL |= UEP_T_RES_NAK;
}

static void resetEndpoints(void) {
  UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
  UEP1_CTRL = bUEP_AUTO_TOG | UEP_T_RES_NAK;
  UEP2_CTRL = bUEP_AUTO_TOG | UEP_T_RES_NAK | UEP_R_RES_ACK;
  UEP3_CTRL = bUEP_AUTO_TOG | UEP_T_RES_NAK;
  UEP0_T_LEN = 0;
  UEP1_T_LEN = 0;
  UEP2_T_LEN = 0;
  UEP3_T_LEN = 0;
  ep1Busy = ep2Busy = ep3Busy = 0;
  configured = 0;
}

void USB_ISR(void) __interrupt(INT_NO_USB) {
  uint8_t token, ep;
  if(UIF_TRANSFER) {
    token = USB_INT_ST & MASK_UIS_TOKEN;
    ep = USB_INT_ST & MASK_UIS_ENDP;
    if(ep == 0) {
      if(token == UIS_TOKEN_SETUP) ep0Setup();
      else if(token == UIS_TOKEN_IN) ep0In();
      else if(token == UIS_TOKEN_OUT) ep0Out();
    }
    else if(token == UIS_TOKEN_IN) {    // a report went out; EP2 OUT is acked and ignored
      if(ep == 1) { UEP1_CTRL = UEP1_CTRL & ~MASK_UEP_T_RES | UEP_T_RES_NAK; ep1Busy = 0; }
      if(ep == 2) { UEP2_CTRL = UEP2_CTRL & ~MASK_UEP_T_RES | UEP_T_RES_NAK; ep2Busy = 0; }
      if(ep == 3) { UEP3_CTRL = UEP3_CTRL & ~MASK_UEP_T_RES | UEP_T_RES_NAK; ep3Busy = 0; }
    }
    UIF_TRANSFER = 0;
  }
  if(UIF_BUS_RST) {
    resetEndpoints();
    USB_DEV_AD = 0;
    USB_INT_FG = 0x1F;
  }
  if(UIF_SUSPEND) UIF_SUSPEND = 0;
}

// The USB block has to be out of reset (USB_CTRL) before its endpoints are set up: the
// other way round, endpoint 3 sent garbage.
void USB_init(uint8_t personality) {
  USB_personality = personality;
  USB_featureNew = 0;
  USB_CTRL  = bUC_DEV_PU_EN | bUC_INT_BUSY | bUC_DMA_EN;
  UDEV_CTRL = bUD_PD_DIS | bUD_PORT_EN;
  UEP0_DMA = EP0_ADDR;
  UEP1_DMA = EP1_ADDR;
  UEP2_DMA = EP2_ADDR;
  UEP3_DMA = EP3_ADDR;
  UEP4_1_MOD = bUEP1_TX_EN;
  UEP2_3_MOD = personality == USB_GAMEPAD ? bUEP2_RX_EN | bUEP2_TX_EN | bUEP3_TX_EN
             : personality == USB_UPDATE  ? bUEP2_RX_EN | bUEP2_TX_EN : bUEP3_TX_EN;
  resetEndpoints();
  USB_INT_FG = 0x1F;
  USB_INT_EN = bUIE_SUSPEND | bUIE_TRANSFER | bUIE_BUS_RST;
  IE_USB = 1;
  EA = 1;
}

// Drop off the bus (the host sees an unplug) and put the USB block back into its
// power-on state before a restart. The ROM bootloader doesn't reset every register
// itself; leftovers from the gamepad layout (EP2 enabled in both directions)
// sometimes kept Windows from starting its driver for the bootloader.
void USB_disconnect(void) {
  IE_USB = 0;
  USB_INT_EN = 0;
  USB_CTRL = bUC_RESET_SIE | bUC_CLR_ALL;
  UDEV_CTRL = bUD_PD_DIS;
  USB_DEV_AD = 0;
  UEP4_1_MOD = 0;
  UEP2_3_MOD = 0;
  UEP0_CTRL = 0;
  UEP1_CTRL = 0;
  UEP2_CTRL = 0;
  UEP3_CTRL = 0;
  UEP4_CTRL = 0;
  UEP0_T_LEN = 0;
  UEP1_T_LEN = 0;
  UEP2_T_LEN = 0;
  UEP3_T_LEN = 0;
  UEP4_T_LEN = 0;
  USB_INT_FG = 0x1F;
  USB_CTRL = 0;                         // out of SIE reset
}

static void copy(__xdata uint8_t* dst, __xdata uint8_t* src, uint8_t n) {
  while(n--) *dst++ = *src++;
}

uint8_t USB_hidReady(void) {
  return configured && !ep1Busy;
}

void USB_hidSend(__xdata uint8_t* buf, uint8_t len) {
  copy(EP1_buffer, buf, len);
  UEP1_T_LEN = len;
  ep1Busy = 1;
  UEP1_CTRL = UEP1_CTRL & ~MASK_UEP_T_RES | UEP_T_RES_ACK;
}

uint8_t USB_statusReady(void) {
  return configured && !ep3Busy;
}

void USB_statusSend(__xdata uint8_t* buf, uint8_t len) {
  copy(EP3_buffer, buf, len);
  UEP3_T_LEN = len;
  ep3Busy = 1;
  UEP3_CTRL = UEP3_CTRL & ~MASK_UEP_T_RES | UEP_T_RES_ACK;
}

// XInput: EP2 IN, its buffer after the OUT one
uint8_t USB_padReady(void) {
  return configured && !ep2Busy && USB_personality == USB_GAMEPAD;
}

void USB_padSend(__xdata uint8_t* buf, uint8_t len) {
  copy(EP2_buffer + 64, buf, len);
  UEP2_T_LEN = len;
  ep2Busy = 1;
  UEP2_CTRL = UEP2_CTRL & ~MASK_UEP_T_RES | UEP_T_RES_ACK;
}
