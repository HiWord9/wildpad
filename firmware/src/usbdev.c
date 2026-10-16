// ===================================================================================
// USB device layer (both modes) for CH552
// ===================================================================================
// Control endpoint handling is derived from wagiminator's CH55x USB handler
// (CC BY-SA 3.0, https://github.com/wagiminator), extended with runtime descriptor
// selection, HID class requests and feature-report data stages.

#pragma nooverlay
#include "usb.h"
#include "usbdev.h"
#include "settings.h"

uint8_t USB_mode;
volatile __bit USB_featureNew;
__xdata uint8_t USB_featureOut[FEATURE_REPORT_LEN];
__xdata uint8_t USB_featureIn[FEATURE_REPORT_LEN];

static volatile __bit ep1Busy;
static volatile __bit ep3Busy;
static uint16_t SetupLen;
static uint8_t  SetupReq, SetupType, SetupIntf, UsbConfig;
__code uint8_t* pDescr;                     // used by the asm copy routine (_pDescr)

#define setupBuf ((PUSB_SETUP_REQ)EP0_buffer)

// Copy len bytes from *pDescr (code memory) to EP0_buffer using both DPTRs
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

// Returns number of bytes placed in EP0_buffer, or 0xFF to stall
static uint8_t USB_getDescriptor(void) {
  uint8_t idx = setupBuf->wValueL;
  uint8_t len;
  uint8_t pad = (USB_mode == MODE_GAMEPAD);

  switch(setupBuf->wValueH) {
    case USB_DESCR_TYP_DEVICE:
      pDescr = pad ? PAD_DevDescr : KBD_DevDescr;
      len = 18;
      break;
    case USB_DESCR_TYP_CONFIG:
      pDescr = pad ? PAD_CfgDescr : KBD_CfgDescr;
      len = pad ? PAD_CFG_LEN : KBD_CFG_LEN;
      break;
    case USB_DESCR_TYP_STRING:
      if(pad) {
        if(idx >= PAD_STR_COUNT) return 0xFF;
        pDescr = PAD_Strings[idx];
      }
      else {
        if(idx >= KBD_STR_COUNT) return 0xFF;
        pDescr = KBD_Strings[idx];
      }
      len = pDescr[0];
      break;
    case USB_DESCR_TYP_REPORT:
      if(pad) return 0xFF;
      if(setupBuf->wIndexL == 0) { pDescr = KBD_Report0; len = KBD_Report0Len; }
      else if(setupBuf->wIndexL == 1) { pDescr = KBD_Report1; len = KBD_Report1Len; }
      else return 0xFF;
      break;
    case USB_DESCR_TYP_HID:
      if(pad) return 0xFF;
      if(setupBuf->wIndexL == 0) pDescr = KBD_CfgDescr + KBD_HID0_OFFSET;
      else if(setupBuf->wIndexL == 1) pDescr = KBD_CfgDescr + KBD_HID1_OFFSET;
      else return 0xFF;
      len = 9;
      break;
    default:
      return 0xFF;
  }

  if(SetupLen > len) SetupLen = len;
  len = SetupLen >= EP0_SIZE ? EP0_SIZE : SetupLen;
  if(len) USB_copyDescr(len);                 // djnz would copy 256 bytes for len 0
  SetupLen -= len;
  pDescr += len;
  return len;
}

// HID class requests (keyboard mode only)
static uint8_t USB_classRequest(void) {
  uint8_t i, len;
  if(USB_mode == MODE_GAMEPAD) return 0xFF;
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

static void USB_endpointHalt(uint8_t ep, uint8_t set) {
  switch(ep) {
    case 0x81: UEP1_CTRL = set ? (UEP1_CTRL & ~bUEP_T_TOG) | UEP_T_RES_STALL
                               : (UEP1_CTRL & ~(bUEP_T_TOG | MASK_UEP_T_RES)) | UEP_T_RES_NAK; break;
    case 0x02: UEP2_CTRL = set ? (UEP2_CTRL & ~bUEP_R_TOG) | UEP_R_RES_STALL
                               : (UEP2_CTRL & ~(bUEP_R_TOG | MASK_UEP_R_RES)) | UEP_R_RES_ACK; break;
    case 0x83: UEP3_CTRL = set ? (UEP3_CTRL & ~bUEP_T_TOG) | UEP_T_RES_STALL
                               : (UEP3_CTRL & ~(bUEP_T_TOG | MASK_UEP_T_RES)) | UEP_T_RES_NAK; break;
    default: break;
  }
  if(!set) {
    if(ep == 0x81) ep1Busy = 0;
    if(ep == 0x83) ep3Busy = 0;
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
      default:                                             // vendor requests (Xbox auth): stall
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
  if((SetupType & USB_REQ_TYP_MASK) == USB_REQ_TYP_STANDARD) {
    if(SetupReq == USB_GET_DESCRIPTOR) {
      len = SetupLen >= EP0_SIZE ? EP0_SIZE : SetupLen;
      if(len) USB_copyDescr(len);
      SetupLen -= len;
      pDescr   += len;
      UEP0_T_LEN = len;
      UEP0_CTRL ^= bUEP_T_TOG;
      return;
    }
    if(SetupReq == USB_SET_ADDRESS) {
      USB_DEV_AD = USB_DEV_AD & bUDA_GP_BIT | SetupLen;
      UEP0_CTRL  = UEP_R_RES_ACK | UEP_T_RES_NAK;
      return;
    }
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
  UEP2_CTRL = bUEP_AUTO_TOG | UEP_R_RES_ACK;
  UEP3_CTRL = bUEP_AUTO_TOG | UEP_T_RES_NAK;
  UEP1_T_LEN = 0;
  UEP3_T_LEN = 0;
  ep1Busy = 0;
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
        else if(ep == 3) {
          UEP3_T_LEN = 0;
          UEP3_CTRL = UEP3_CTRL & ~MASK_UEP_T_RES | UEP_T_RES_NAK;
          ep3Busy = 0;
        }
        break;
      case UIS_TOKEN_OUT:
        if(ep == 0) USB_EP0_OUT();
        // EP2 OUT (XInput rumble/LED): auto-ACKed, content ignored
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

void USB_init(uint8_t mode) {
  USB_mode = mode;
  USB_featureNew = 0;

  USB_CTRL  = bUC_DEV_PU_EN | bUC_INT_BUSY | bUC_DMA_EN;
  UDEV_CTRL = bUD_PD_DIS | bUD_PORT_EN;

  UEP0_DMA = EP0_ADDR;
  UEP1_DMA = EP1_ADDR;
  UEP2_DMA = EP2_ADDR;
  UEP3_DMA = EP3_ADDR;
  UEP4_1_MOD = bUEP1_TX_EN;
  UEP2_3_MOD = (mode == MODE_GAMEPAD) ? bUEP2_RX_EN : bUEP3_TX_EN;
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

uint8_t USB_configured(void) {
  return UsbConfig != 0;
}

uint8_t USB_ep1Ready(void) {
  return UsbConfig && !ep1Busy;
}

void USB_ep1Send(__xdata uint8_t* buf, uint8_t len) {
  uint8_t i;
  for(i = 0; i < len; i++) EP1_buffer[i] = buf[i];
  UEP1_T_LEN = len;
  ep1Busy = 1;
  UEP1_CTRL = UEP1_CTRL & ~MASK_UEP_T_RES | UEP_T_RES_ACK;
}

uint8_t USB_ep3Ready(void) {
  return UsbConfig && !ep3Busy && USB_mode == MODE_KEYBOARD;
}

void USB_ep3Send(__xdata uint8_t* buf, uint8_t len) {
  uint8_t i;
  for(i = 0; i < len; i++) EP3_buffer[i] = buf[i];
  UEP3_T_LEN = len;
  ep3Busy = 1;
  UEP3_CTRL = UEP3_CTRL & ~MASK_UEP_T_RES | UEP_T_RES_ACK;
}
