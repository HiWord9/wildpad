// ===================================================================================
// USB device layer for CH552
// ===================================================================================
// Control endpoint handling is derived from wagiminator's CH55x USB handler
// (CC BY-SA 3.0, https://github.com/wagiminator), extended with three device
// personalities, HID class requests, feature-report data stages and Microsoft OS
// descriptors.

#pragma nooverlay
#include "usb.h"
#include "usbdev.h"
#include "config.h"

uint8_t USB_personality;
volatile __bit USB_featureNew;
__xdata uint8_t USB_featureOut[FEATURE_REPORT_LEN];
__xdata uint8_t USB_featureIn[FEATURE_REPORT_LEN];

static volatile __bit ep1Busy;
static volatile __bit ep2Busy;
static volatile __bit ep3Busy;
static uint16_t SetupLen;
static uint8_t  SetupReq, SetupType, SetupIntf, UsbConfig;
__code uint8_t* pDescr;                     // used by the asm copy routine (_pDescr)

#define setupBuf ((PUSB_SETUP_REQ)EP0_buffer)

// Copy len (1..255) bytes from *pDescr (code memory) to EP0_buffer using both DPTRs
// (Thanks to Ralph Doncaster / wagiminator)
#pragma callee_saves USB_copyDescr
static void USB_copyDescr(uint8_t len) {
  len;
  __asm
    push ar7
    mov  r7, dpl
    inc  _XBUS_AUX
    mov  dptr, #_EP0_buffer
    dec  _XBUS_AUX
    mov  dpl, _pDescr
    mov  dph, (_pDescr + 1)
    01$:
    clr  a
    movc a, @a+dptr
    inc  dptr
    .DB  0xA5
    djnz r7, 01$
    pop  ar7
  __endasm;
}

// Start sending pDescr[0..len) on EP0; returns bytes in the first packet
static uint8_t USB_sendDescr(uint8_t len) {
  if(SetupLen > len) SetupLen = len;
  len = SetupLen >= EP0_SIZE ? EP0_SIZE : SetupLen;
  if(len) USB_copyDescr(len);                 // djnz would copy 256 bytes for len 0
  SetupLen -= len;
  pDescr += len;
  return len;
}

static uint8_t USB_getDescriptor(void) {
  uint8_t idx = setupBuf->wValueL;
  uint8_t legacy = (USB_personality == USB_LEGACY);

  switch(setupBuf->wValueH) {
    case USB_DESCR_TYP_DEVICE:
      pDescr = legacy ? DevDescrLegacy : (USB_personality == USB_WHEEL ? DevDescrWheel : DevDescrKeyboard);
      return USB_sendDescr(18);
    case USB_DESCR_TYP_CONFIG:
      if(legacy) { pDescr = CfgDescrLegacy; return USB_sendDescr(LEGACY_CFG_LEN); }
      if(USB_personality == USB_WHEEL) { pDescr = CfgDescrWheel; return USB_sendDescr(WHEEL_CFG_LEN); }
      pDescr = CfgDescrKeyboard;
      return USB_sendDescr(KBD_CFG_LEN);
    case USB_DESCR_TYP_STRING:
      if(idx == MS_OS_STRING_INDEX) {
        if(USB_personality != USB_WHEEL) return 0xFF;
        pDescr = MsOsString;
        return USB_sendDescr(18);
      }
      if(idx >= STR_COUNT) return 0xFF;
      pDescr = legacy ? LegacyStrings[idx] : Strings[idx];
      return USB_sendDescr(pDescr[0]);
    case USB_DESCR_TYP_REPORT:
      if(legacy) return 0xFF;
      if(setupBuf->wIndexL == 0) { pDescr = Report0; return USB_sendDescr(Report0Len); }
      if(setupBuf->wIndexL == 1) { pDescr = Report1; return USB_sendDescr(Report1Len); }
      return 0xFF;
    case USB_DESCR_TYP_HID:
      if(legacy || setupBuf->wIndexL > 1) return 0xFF;
      pDescr = CfgDescrKeyboard + (setupBuf->wIndexL ? CFG_HID1_OFFSET : CFG_HID0_OFFSET);
      return USB_sendDescr(9);
    default:
      return 0xFF;
  }
}

// HID class requests for interfaces 0 and 1
static uint8_t USB_classRequest(void) {
  uint8_t i, len;
  if(USB_personality == USB_LEGACY || SetupIntf > 1) return 0xFF;
  switch(SetupReq) {
    case HID_GET_REPORT:
      len = SetupLen > EP0_SIZE ? EP0_SIZE : SetupLen;
      if(SetupIntf == 1 && setupBuf->wValueH == 3) {           // feature report
        if(len > FEATURE_REPORT_LEN) len = FEATURE_REPORT_LEN;
        for(i = 0; i < len; i++) EP0_buffer[i] = USB_featureIn[i];
      }
      else {
        for(i = 0; i < len; i++) EP0_buffer[i] = 0;
      }
      return len;
    case HID_SET_REPORT:                                       // data follows in OUT stage
    case HID_SET_IDLE:
    case HID_SET_PROTOCOL:
      return 0;
    case HID_GET_IDLE:
      EP0_buffer[0] = 0;
      return 1;
    case HID_GET_PROTOCOL:
      EP0_buffer[0] = 1;
      return 1;
    default:
      return 0xFF;
  }
}

// Vendor requests: only the Microsoft OS extended compatible ID (wheel mode).
// Everything else - including Xbox security requests - is stalled.
static uint8_t USB_vendorRequest(void) {
  if(USB_personality == USB_WHEEL && SetupReq == MS_OS_VENDOR_CODE &&
     (SetupType & 0x80) && setupBuf->wIndexL == 0x04 && setupBuf->wIndexH == 0) {
    pDescr = MsCompatId;
    return USB_sendDescr(sizeof(MsCompatId));
  }
  return 0xFF;
}

static void USB_endpointHalt(uint8_t ep, uint8_t set) {
  switch(ep) {
    case 0x81: UEP1_CTRL = set ? (UEP1_CTRL & ~bUEP_T_TOG) | UEP_T_RES_STALL
                               : (UEP1_CTRL & ~(bUEP_T_TOG | MASK_UEP_T_RES)) | UEP_T_RES_NAK;
               if(!set) ep1Busy = 0;
               break;
    case 0x82: UEP2_CTRL = set ? (UEP2_CTRL & ~bUEP_T_TOG) | UEP_T_RES_STALL
                               : (UEP2_CTRL & ~(bUEP_T_TOG | MASK_UEP_T_RES)) | UEP_T_RES_NAK;
               if(!set) ep2Busy = 0;
               break;
    case 0x02: UEP2_CTRL = set ? (UEP2_CTRL & ~bUEP_R_TOG) | UEP_R_RES_STALL
                               : (UEP2_CTRL & ~(bUEP_R_TOG | MASK_UEP_R_RES)) | UEP_R_RES_ACK;
               break;
    case 0x83: UEP3_CTRL = set ? (UEP3_CTRL & ~bUEP_T_TOG) | UEP_T_RES_STALL
                               : (UEP3_CTRL & ~(bUEP_T_TOG | MASK_UEP_T_RES)) | UEP_T_RES_NAK;
               if(!set) ep3Busy = 0;
               break;
    default: break;
  }
}

static void USB_EP0_SETUP(void) {
  uint8_t len = 0xFF;
  if(USB_RX_LEN == sizeof(USB_SETUP_REQ)) {
    SetupLen  = ((uint16_t)setupBuf->wLengthH << 8) | setupBuf->wLengthL;
    SetupReq  = setupBuf->bRequest;
    SetupType = setupBuf->bRequestType;
    SetupIntf = setupBuf->wIndexL;
    len = 0;

    switch(SetupType & USB_REQ_TYP_MASK) {
      case USB_REQ_TYP_STANDARD:
        switch(SetupReq) {
          case USB_GET_DESCRIPTOR:
            len = USB_getDescriptor();
            break;
          case USB_SET_ADDRESS:
            SetupLen = setupBuf->wValueL;                  // applied after status stage
            break;
          case USB_GET_CONFIGURATION:
            EP0_buffer[0] = UsbConfig;
            len = SetupLen ? 1 : 0;
            break;
          case USB_SET_CONFIGURATION:
            UsbConfig = setupBuf->wValueL;
            break;
          case USB_GET_INTERFACE:
            EP0_buffer[0] = 0;
            len = SetupLen ? 1 : 0;
            break;
          case USB_SET_INTERFACE:
            break;
          case USB_GET_STATUS:
            EP0_buffer[0] = 0;
            EP0_buffer[1] = 0;
            len = SetupLen >= 2 ? 2 : SetupLen;
            break;
          case USB_CLEAR_FEATURE:
          case USB_SET_FEATURE:
            if((SetupType & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_ENDP)
              USB_endpointHalt(setupBuf->wIndexL, SetupReq == USB_SET_FEATURE);
            break;                                         // device remote wakeup: accept
          default:
            len = 0xFF;
            break;
        }
        break;
      case USB_REQ_TYP_CLASS:
        len = USB_classRequest();
        break;
      case USB_REQ_TYP_VENDOR:
        len = USB_vendorRequest();
        break;
      default:
        len = 0xFF;
        break;
    }
  }

  if(len == 0xFF) {
    SetupReq = 0xFF;
    UEP0_CTRL = bUEP_R_TOG | bUEP_T_TOG | UEP_R_RES_STALL | UEP_T_RES_STALL;
  }
  else {
    UEP0_T_LEN = len;
    UEP0_CTRL = bUEP_R_TOG | bUEP_T_TOG | UEP_R_RES_ACK | UEP_T_RES_ACK;
  }
}

static void USB_EP0_IN(void) {
  uint8_t len;
  uint8_t typ = SetupType & USB_REQ_TYP_MASK;
  // continue multi-packet descriptors (standard GET_DESCRIPTOR or MS OS vendor request)
  if((typ == USB_REQ_TYP_STANDARD && SetupReq == USB_GET_DESCRIPTOR) ||
     (typ == USB_REQ_TYP_VENDOR && SetupReq == MS_OS_VENDOR_CODE)) {
    len = SetupLen >= EP0_SIZE ? EP0_SIZE : SetupLen;
    if(len) USB_copyDescr(len);
    SetupLen -= len;
    pDescr   += len;
    UEP0_T_LEN = len;
    UEP0_CTRL ^= bUEP_T_TOG;
    return;
  }
  if(typ == USB_REQ_TYP_STANDARD && SetupReq == USB_SET_ADDRESS) {
    USB_DEV_AD = USB_DEV_AD & bUDA_GP_BIT | SetupLen;
    UEP0_CTRL  = UEP_R_RES_ACK | UEP_T_RES_NAK;
    return;
  }
  UEP0_T_LEN = 0;
  UEP0_CTRL  = UEP_R_RES_ACK | UEP_T_RES_NAK;
}

static void USB_EP0_OUT(void) {
  uint8_t i, len;
  if((SetupType & USB_REQ_TYP_MASK) == USB_REQ_TYP_CLASS && SetupReq == HID_SET_REPORT) {
    // Data stage of SET_REPORT. Feature reports to interface 1 carry config commands.
    if(SetupIntf == 1 && !USB_featureNew) {
      len = USB_RX_LEN;
      if(len > FEATURE_REPORT_LEN) len = FEATURE_REPORT_LEN;
      for(i = 0; i < len; i++) USB_featureOut[i] = EP0_buffer[i];
      for(; i < FEATURE_REPORT_LEN; i++) USB_featureOut[i] = 0;
      USB_featureNew = 1;
    }
    UEP0_T_LEN = 0;                                        // zero-length status (DATA1)
    UEP0_CTRL ^= bUEP_R_TOG;
    return;
  }
  UEP0_T_LEN = 0;
  UEP0_CTRL |= UEP_R_RES_ACK | UEP_T_RES_NAK;              // status stage of IN transfer
}

static void USB_resetEndpoints(void) {
  UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
  UEP1_CTRL = bUEP_AUTO_TOG | UEP_T_RES_NAK;
  UEP2_CTRL = bUEP_AUTO_TOG | UEP_T_RES_NAK | UEP_R_RES_ACK;
  UEP3_CTRL = bUEP_AUTO_TOG | UEP_T_RES_NAK;
  UEP1_T_LEN = 0;
  UEP2_T_LEN = 0;
  UEP3_T_LEN = 0;
  ep1Busy = 0;
  ep2Busy = 0;
  ep3Busy = 0;
  UsbConfig = 0;
}

void USB_ISR(void) __interrupt(INT_NO_USB) {
  if(UIF_TRANSFER) {
    uint8_t ep = USB_INT_ST & MASK_UIS_ENDP;
    switch(USB_INT_ST & MASK_UIS_TOKEN) {
      case UIS_TOKEN_SETUP:
        if(ep == 0) USB_EP0_SETUP();
        break;
      case UIS_TOKEN_IN:
        if(ep == 0) USB_EP0_IN();
        else if(ep == 1) {
          UEP1_T_LEN = 0;
          UEP1_CTRL = UEP1_CTRL & ~MASK_UEP_T_RES | UEP_T_RES_NAK;
          ep1Busy = 0;
        }
        else if(ep == 2) {
          UEP2_T_LEN = 0;
          UEP2_CTRL = UEP2_CTRL & ~MASK_UEP_T_RES | UEP_T_RES_NAK;
          ep2Busy = 0;
        }
        else if(ep == 3) {
          UEP3_T_LEN = 0;
          UEP3_CTRL = UEP3_CTRL & ~MASK_UEP_T_RES | UEP_T_RES_NAK;
          ep3Busy = 0;
        }
        break;
      case UIS_TOKEN_OUT:
        if(ep == 0) USB_EP0_OUT();
        // EP2 OUT (XInput rumble/LED commands): auto-ACKed, content ignored
        break;
      default:
        break;
    }
    UIF_TRANSFER = 0;
  }

  if(UIF_BUS_RST) {
    USB_resetEndpoints();
    USB_DEV_AD   = 0x00;
    UIF_SUSPEND  = 0;
    UIF_TRANSFER = 0;
    UIF_BUS_RST  = 0;
  }

  if(UIF_SUSPEND) {
    UIF_SUSPEND = 0;
    if(!(USB_MIS_ST & bUMS_SUSPEND)) USB_INT_FG = 0xFF;
  }
}

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
  if(personality == USB_LEGACY)     UEP2_3_MOD = bUEP2_RX_EN;
  else if(personality == USB_WHEEL) UEP2_3_MOD = bUEP2_RX_EN | bUEP2_TX_EN | bUEP3_TX_EN;
  else                              UEP2_3_MOD = bUEP3_TX_EN;
  USB_resetEndpoints();

  USB_INT_EN |= bUIE_SUSPEND | bUIE_TRANSFER | bUIE_BUS_RST;
  USB_INT_FG |= 0x1F;
  IE_USB = 1;
  EA = 1;
  UEP0_T_LEN = 0;
}

// Drop off the bus (host sees an unplug) - used before a reset into another mode
void USB_disconnect(void) {
  IE_USB = 0;
  USB_CTRL = 0;
  UDEV_CTRL = bUD_PD_DIS;
}

static void USB_load(__xdata uint8_t* dst, __xdata uint8_t* src, uint8_t len) {
  while(len--) *dst++ = *src++;
}

uint8_t USB_hidReady(void) {
  return UsbConfig && !ep1Busy && USB_personality != USB_LEGACY;
}

void USB_hidSend(__xdata uint8_t* buf, uint8_t len) {
  USB_load(EP1_buffer, buf, len);
  UEP1_T_LEN = len;
  ep1Busy = 1;
  UEP1_CTRL = UEP1_CTRL & ~MASK_UEP_T_RES | UEP_T_RES_ACK;
}

uint8_t USB_statusReady(void) {
  return UsbConfig && !ep3Busy && USB_personality != USB_LEGACY;
}

void USB_statusSend(__xdata uint8_t* buf, uint8_t len) {
  USB_load(EP3_buffer, buf, len);
  UEP3_T_LEN = len;
  ep3Busy = 1;
  UEP3_CTRL = UEP3_CTRL & ~MASK_UEP_T_RES | UEP_T_RES_ACK;
}

// XInput reports: EP2 IN (TX buffer at EP2_ADDR + 64) in wheel mode, EP1 in legacy
uint8_t USB_padReady(void) {
  if(!UsbConfig) return 0;
  if(USB_personality == USB_WHEEL) return !ep2Busy;
  if(USB_personality == USB_LEGACY) return !ep1Busy;
  return 0;
}

void USB_padSend(__xdata uint8_t* buf, uint8_t len) {
  if(USB_personality == USB_LEGACY) {
    USB_load(EP1_buffer, buf, len);
    UEP1_T_LEN = len;
    ep1Busy = 1;
    UEP1_CTRL = UEP1_CTRL & ~MASK_UEP_T_RES | UEP_T_RES_ACK;
    return;
  }
  USB_load(EP2_buffer + 64, buf, len);
  UEP2_T_LEN = len;
  ep2Busy = 1;
  UEP2_CTRL = UEP2_CTRL & ~MASK_UEP_T_RES | UEP_T_RES_ACK;
}
