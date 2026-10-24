// ===================================================================================
// CH552 registers used by the firmware (names and addresses as in the WCH datasheet)
// ===================================================================================

#pragma once
#include <stdint.h>

// System
__sfr __at(0xA1) SAFE_MOD;              // 0x55, 0xAA opens the registers marked "safe"
__sfr __at(0xB1) GLOBAL_CFG;            // safe
__sfr __at(0xB9) CLOCK_CFG;             // safe
__sfr __at(0xFE) RESET_KEEP;            // survives every reset but power-on
__sfr __at(0xFF) WDOG_COUNT;
#define bSW_RESET         0x10
#define bDATA_WE          0x04
#define bWDOG_EN          0x01
#define MASK_SYS_CK_SEL   0x07

// Interrupts
__sbit __at(0xAF) EA;
__sbit __at(0xA9) ET0;
__sbit __at(0xAD) ET2;
__sbit __at(0xEA) IE_USB;
#define INT_NO_TMR0       1
#define INT_NO_TMR2       5
#define INT_NO_USB        8

// Ports
__sfr __at(0x90) P1;
__sfr __at(0xB0) P3;
__sfr __at(0x96) P3_MOD_OC;
__sfr __at(0x97) P3_DIR_PU;

// Timers
__sfr __at(0x89) TMOD;
__sfr __at(0x8A) TL0;
__sfr __at(0x8C) TH0;
__sbit __at(0x8C) TR0;
__sbit __at(0x8D) TF0;
__sfr __at(0xC8) T2CON;
__sfr __at(0xC9) T2MOD;
__sbit __at(0xCA) TR2;
__sbit __at(0xCF) TF2;
__sfr16 __at(0xCBCA) RCAP2;
__sfr16 __at(0xCDCC) T2COUNT;

// Data-Flash: 128 bytes at even addresses
#define DATA_FLASH_ADDR   0xC000
__sfr __at(0x84) ROM_ADDR_L;
__sfr __at(0x85) ROM_ADDR_H;
__sfr __at(0x86) ROM_CTRL;              // write: command, read: status
__sfr __at(0x8E) ROM_DATA_L;
#define ROM_STATUS        ROM_CTRL
#define ROM_CMD_READ      0x8E
#define ROM_CMD_WRITE     0x9A
#define bROM_ADDR_OK      0x40

// USB device
__sfr __at(0xD1) UDEV_CTRL;
__sfr __at(0xD2) UEP1_CTRL;
__sfr __at(0xD3) UEP1_T_LEN;
__sfr __at(0xD4) UEP2_CTRL;
__sfr __at(0xD5) UEP2_T_LEN;
__sfr __at(0xD6) UEP3_CTRL;
__sfr __at(0xD7) UEP3_T_LEN;
__sfr __at(0xD8) USB_INT_FG;
__sbit __at(0xD8) UIF_BUS_RST;
__sbit __at(0xD9) UIF_TRANSFER;
__sbit __at(0xDA) UIF_SUSPEND;
__sfr __at(0xD9) USB_INT_ST;
__sfr __at(0xDA) USB_MIS_ST;
__sfr __at(0xDB) USB_RX_LEN;
__sfr __at(0xDC) UEP0_CTRL;
__sfr __at(0xDD) UEP0_T_LEN;
__sfr __at(0xDE) UEP4_CTRL;
__sfr __at(0xDF) UEP4_T_LEN;
__sfr __at(0xE1) USB_INT_EN;
__sfr __at(0xE2) USB_CTRL;
__sfr __at(0xE3) USB_DEV_AD;
__sfr16 __at(0xE5E4) UEP2_DMA;
__sfr16 __at(0xE7E6) UEP3_DMA;
__sfr __at(0xEA) UEP4_1_MOD;
__sfr __at(0xEB) UEP2_3_MOD;
__sfr16 __at(0xEDEC) UEP0_DMA;
__sfr16 __at(0xEFEE) UEP1_DMA;

#define bUD_PD_DIS        0x80          // UDEV_CTRL
#define bUD_PORT_EN       0x01
#define bUC_DEV_PU_EN     0x20          // USB_CTRL
#define bUC_INT_BUSY      0x08
#define bUC_RESET_SIE     0x04
#define bUC_CLR_ALL       0x02
#define bUC_DMA_EN        0x01
#define bUIE_SUSPEND      0x04          // USB_INT_EN
#define bUIE_TRANSFER     0x02
#define bUIE_BUS_RST      0x01
#define bUMS_SUSPEND      0x04          // USB_MIS_ST
#define bUDA_GP_BIT       0x80          // USB_DEV_AD
#define MASK_UIS_TOKEN    0x30          // USB_INT_ST
#define MASK_UIS_ENDP     0x0F
#define UIS_TOKEN_OUT     0x00
#define UIS_TOKEN_IN      0x20
#define UIS_TOKEN_SETUP   0x30
#define bUEP1_TX_EN       0x40          // UEP4_1_MOD
#define bUEP3_TX_EN       0x40          // UEP2_3_MOD
#define bUEP2_RX_EN       0x08
#define bUEP2_TX_EN       0x04

#define bUEP_R_TOG        0x80          // UEPn_CTRL
#define bUEP_T_TOG        0x40
#define bUEP_AUTO_TOG     0x10
#define MASK_UEP_R_RES    0x0C
#define MASK_UEP_T_RES    0x03
#define UEP_R_RES_ACK     0x00
#define UEP_R_RES_STALL   0x0C
#define UEP_T_RES_ACK     0x00
#define UEP_T_RES_NAK     0x02
#define UEP_T_RES_STALL   0x03
