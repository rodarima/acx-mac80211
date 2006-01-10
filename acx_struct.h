/***********************************************************************
** Copyright (C) 2003  ACX100 Open Source Project
**
** The contents of this file are subject to the Mozilla Public
** License Version 1.1 (the "License"); you may not use this file
** except in compliance with the License. You may obtain a copy of
** the License at http://www.mozilla.org/MPL/
**
** Software distributed under the License is distributed on an "AS
** IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
** implied. See the License for the specific language governing
** rights and limitations under the License.
**
** Alternatively, the contents of this file may be used under the
** terms of the GNU Public License version 2 (the "GPL"), in which
** case the provisions of the GPL are applicable instead of the
** above.  If you wish to allow the use of your version of this file
** only under the terms of the GPL and not to allow others to use
** your version of this file under the MPL, indicate your decision
** by deleting the provisions above and replace them with the notice
** and other provisions required by the GPL.  If you do not delete
** the provisions above, a recipient may use your version of this
** file under either the MPL or the GPL.
** ---------------------------------------------------------------------
** Inquiries regarding the ACX100 Open Source Project can be
** made directly to:
**
** acx100-users@lists.sf.net
** http://acx100.sf.net
** ---------------------------------------------------------------------
*/

/***********************************************************************
** Forward declarations of types
*/
typedef struct tx tx_t;
typedef struct wlandevice wlandevice_t;
typedef struct client client_t;
typedef struct rxdesc rxdesc_t;
typedef struct txdesc txdesc_t;
typedef struct rxhostdesc rxhostdesc_t;
typedef struct txhostdesc txhostdesc_t;


/***********************************************************************
** Debug / log functionality
*/
enum {
	L_LOCK		= (ACX_DEBUG>1)*0x0001,	/* locking debug log */
	L_INIT		= (ACX_DEBUG>0)*0x0002,	/* special card initialization logging */
	L_IRQ		= (ACX_DEBUG>0)*0x0004,	/* interrupt stuff */
	L_ASSOC		= (ACX_DEBUG>0)*0x0008,	/* assocation (network join) and station log */
	L_FUNC		= (ACX_DEBUG>1)*0x0020,	/* logging of function enter / leave */
	L_XFER		= (ACX_DEBUG>1)*0x0080,	/* logging of transfers and mgmt */
	L_DATA		= (ACX_DEBUG>1)*0x0100,	/* logging of transfer data */
	L_DEBUG		= (ACX_DEBUG>1)*0x0200,	/* log of debug info */
	L_IOCTL		= (ACX_DEBUG>0)*0x0400,	/* log ioctl calls */
	L_CTL		= (ACX_DEBUG>1)*0x0800,	/* log of low-level ctl commands */
	L_BUFR		= (ACX_DEBUG>1)*0x1000,	/* debug rx buffer mgmt (ring buffer etc.) */
	L_XFER_BEACON	= (ACX_DEBUG>1)*0x2000,	/* also log beacon packets */
	L_BUFT		= (ACX_DEBUG>1)*0x4000,	/* debug tx buffer mgmt (ring buffer etc.) */
	L_USBRXTX	= (ACX_DEBUG>0)*0x8000,	/* debug USB rx/tx operations */
	L_BUF		= L_BUFR + L_BUFT,
	L_ANY		= 0xffff
};

#if ACX_DEBUG
extern unsigned int acx_debug;
#else
enum { acx_debug = 0 };
#endif


/***********************************************************************
** Random helpers
*/
#define ACX_PACKED __WLAN_ATTRIB_PACK__

#define VEC_SIZE(a) (sizeof(a)/sizeof(a[0]))

/* Use worker_queues for 2.5/2.6 kernels and queue tasks for 2.4 kernels
   (used for the 'bottom half' of the interrupt routine) */

#include <linux/workqueue.h>
#define USE_WORKER_TASKS
#define WORK_STRUCT struct work_struct
#define SCHEDULE_WORK schedule_work
#define FLUSH_SCHEDULED_WORK flush_scheduled_work


/***********************************************************************
** Constants
*/
#define OK	0
#define NOT_OK	1

/* The supported chip models */
#define CHIPTYPE_ACX100		1
#define CHIPTYPE_ACX111		2

#define IS_ACX100(priv)	((priv)->chip_type == CHIPTYPE_ACX100)
#define IS_ACX111(priv)	((priv)->chip_type == CHIPTYPE_ACX111)

/* Supported interfaces */
#define DEVTYPE_PCI		0
#define DEVTYPE_USB		1

#if defined(CONFIG_ACX_PCI)
 #if !defined(CONFIG_ACX_USB)
  #define IS_PCI(priv)	1
 #else
  #define IS_PCI(priv)	((priv)->dev_type == DEVTYPE_PCI)
 #endif
#else
 #define IS_PCI(priv)	0
#endif

#if defined(CONFIG_ACX_USB)
 #if !defined(CONFIG_ACX_PCI)
  #define IS_USB(priv)	1
 #else
  #define IS_USB(priv)	((priv)->dev_type == DEVTYPE_USB)
 #endif
#else
 #define IS_USB(priv)	0
#endif

/* Driver defaults */
#define DEFAULT_DTIM_INTERVAL	10
/* used to be 2048, but FreeBSD driver changed it to 4096 to work properly
** in noisy wlans */
#define DEFAULT_MSDU_LIFETIME	4096
#define DEFAULT_RTS_THRESHOLD	2312	/* max. size: disable RTS mechanism */
#define DEFAULT_BEACON_INTERVAL	100

#define ACX100_BAP_DATALEN_MAX		4096
#define ACX100_RID_GUESSING_MAXLEN	2048	/* I'm not really sure */
#define ACX100_RIDDATA_MAXLEN		ACX100_RID_GUESSING_MAXLEN

/* Support Constants */
/* Radio type names, found in Win98 driver's TIACXLN.INF */
#define RADIO_MAXIM_0D		0x0d
#define RADIO_RFMD_11		0x11
#define RADIO_RALINK_15		0x15
/* used in ACX111 cards (WG311v2, WL-121, ...): */
#define RADIO_RADIA_16		0x16
/* most likely *sometimes* used in ACX111 cards: */
#define RADIO_UNKNOWN_17	0x17
/* FwRad19.bin was found in a Safecom driver; must be an ACX111 radio: */
#define RADIO_UNKNOWN_19	0x19

/* Controller Commands */
/* can be found in table cmdTable in firmware "Rev. 1.5.0" (FW150) */
#define ACX1xx_CMD_RESET		0x00
#define ACX1xx_CMD_INTERROGATE		0x01
#define ACX1xx_CMD_CONFIGURE		0x02
#define ACX1xx_CMD_ENABLE_RX		0x03
#define ACX1xx_CMD_ENABLE_TX		0x04
#define ACX1xx_CMD_DISABLE_RX		0x05
#define ACX1xx_CMD_DISABLE_TX		0x06
#define ACX1xx_CMD_FLUSH_QUEUE		0x07
#define ACX1xx_CMD_SCAN			0x08
#define ACX1xx_CMD_STOP_SCAN		0x09
#define ACX1xx_CMD_CONFIG_TIM		0x0a
#define ACX1xx_CMD_JOIN			0x0b
#define ACX1xx_CMD_WEP_MGMT		0x0c
#ifdef OLD_FIRMWARE_VERSIONS
#define ACX100_CMD_HALT			0x0e	/* mapped to unknownCMD in FW150 */
#else
#define ACX1xx_CMD_MEM_READ		0x0d
#define ACX1xx_CMD_MEM_WRITE		0x0e
#endif
#define ACX1xx_CMD_SLEEP		0x0f
#define ACX1xx_CMD_WAKE			0x10
#define ACX1xx_CMD_UNKNOWN_11		0x11	/* mapped to unknownCMD in FW150 */
#define ACX100_CMD_INIT_MEMORY		0x12
#define ACX1xx_CMD_CONFIG_BEACON	0x13
#define ACX1xx_CMD_CONFIG_PROBE_RESPONSE	0x14
#define ACX1xx_CMD_CONFIG_NULL_DATA	0x15
#define ACX1xx_CMD_CONFIG_PROBE_REQUEST	0x16
#define ACX1xx_CMD_TEST			0x17
#define ACX1xx_CMD_RADIOINIT		0x18
#define ACX111_CMD_RADIOCALIB		0x19

/* 'After Interrupt' Commands */
#define ACX_AFTER_IRQ_CMD_STOP_SCAN	0x01
#define ACX_AFTER_IRQ_CMD_ASSOCIATE	0x02
#define ACX_AFTER_IRQ_CMD_RADIO_RECALIB	0x04
#define ACX_AFTER_IRQ_UPDATE_CARD_CFG	0x08
#define ACX_AFTER_IRQ_TX_CLEANUP	0x10
#define ACX_AFTER_IRQ_COMPLETE_SCAN	0x20
#define ACX_AFTER_IRQ_RESTART_SCAN	0x40

/***********************************************************************
** Tx/Rx buffer sizes and watermarks
**
** This will alloc and use DMAable buffers of
** WLAN_A4FR_MAXLEN_WEP_FCS * (RX_CNT + TX_CNT) bytes
** RX/TX_CNT=32 -> ~150k DMA buffers
** RX/TX_CNT=16 -> ~75k DMA buffers
**
** 2005-10-10: reduced memory usage by lowering both to 16
*/
#define RX_CNT 16
#define TX_CNT 16

/* we clean up txdescs when we have N free txdesc: */
#define TX_CLEAN_BACKLOG (TX_CNT/4)
#define TX_START_CLEAN (TX_CNT - TX_CLEAN_BACKLOG)
#define TX_EMERG_CLEAN 2
/* we stop queue if we have < N free txbufs: */
#define TX_STOP_QUEUE 3
/* we start queue if we have >= N free txbufs: */
#define TX_START_QUEUE 5

/***********************************************************************
** Interrogate/Configure cmd constants
**
** NB: length includes JUST the data part of the IE
** (does not include size of the (type,len) pair)
**
** TODO: seems that acx100, acx100usb, acx111 have some differences,
** fix code with regard to this!
*/

#define DEF_IE(name, val, len) enum { ACX##name=val, ACX##name##_LEN=len }

/* Information Elements: Network Parameters, Static Configuration Entities */
/* these are handled by real_cfgtable in firmware "Rev 1.5.0" (FW150) */
DEF_IE(1xx_IE_UNKNOWN_00		,0x0000, -1);	/* mapped to cfgInvalid in FW150 */
DEF_IE(100_IE_ACX_TIMER			,0x0001, 0x10);
DEF_IE(1xx_IE_POWER_MGMT		,0x0002, 0x06);
DEF_IE(1xx_IE_QUEUE_CONFIG		,0x0003, 0x1c);
DEF_IE(100_IE_BLOCK_SIZE		,0x0004, 0x02);
DEF_IE(1xx_IE_MEMORY_CONFIG_OPTIONS	,0x0005, 0x14);
DEF_IE(1xx_IE_RATE_FALLBACK		,0x0006, 0x01);
DEF_IE(100_IE_WEP_OPTIONS		,0x0007, 0x03);
DEF_IE(111_IE_RADIO_BAND		,0x0007, -1);
DEF_IE(1xx_IE_MEMORY_MAP		,0x0008, 0x28); /* huh? */
DEF_IE(100_IE_SSID			,0x0008, 0x20); /* huh? */
DEF_IE(1xx_IE_SCAN_STATUS		,0x0009, 0x04); /* mapped to cfgInvalid in FW150 */
DEF_IE(1xx_IE_ASSOC_ID			,0x000a, 0x02);
DEF_IE(1xx_IE_UNKNOWN_0B		,0x000b, -1);	/* mapped to cfgInvalid in FW150 */
DEF_IE(100_IE_UNKNOWN_0C		,0x000c, -1);	/* very small implementation in FW150! */
/* ACX100 has an equivalent struct in the cmd mailbox directly after reset.
 * 0x14c seems extremely large, will trash stack on failure (memset!)
 * in case of small input struct --> OOPS! */
DEF_IE(111_IE_CONFIG_OPTIONS		,0x000c, 0x14c);
DEF_IE(1xx_IE_FWREV			,0x000d, 0x18);
DEF_IE(1xx_IE_FCS_ERROR_COUNT		,0x000e, 0x04);
DEF_IE(1xx_IE_MEDIUM_USAGE		,0x000f, 0x08);
DEF_IE(1xx_IE_RXCONFIG			,0x0010, 0x04);
DEF_IE(100_IE_UNKNOWN_11		,0x0011, -1);	/* NONBINARY: large implementation in FW150! link quality readings or so? */
DEF_IE(111_IE_QUEUE_THRESH		,0x0011, -1);
DEF_IE(100_IE_UNKNOWN_12		,0x0012, -1);	/* NONBINARY: VERY large implementation in FW150!! */
DEF_IE(111_IE_BSS_POWER_SAVE		,0x0012, -1);
DEF_IE(1xx_IE_FIRMWARE_STATISTICS	,0x0013, 0x9c);
DEF_IE(1xx_IE_FEATURE_CONFIG		,0x0015, 0x08);
DEF_IE(111_IE_KEY_CHOOSE		,0x0016, 0x04);	/* for rekeying. really len=4?? */
DEF_IE(1xx_IE_DOT11_STATION_ID		,0x1001, 0x06);
DEF_IE(100_IE_DOT11_UNKNOWN_1002	,0x1002, -1);	/* mapped to cfgInvalid in FW150 */
DEF_IE(111_IE_DOT11_FRAG_THRESH		,0x1002, -1);	/* mapped to cfgInvalid in FW150 */
DEF_IE(100_IE_DOT11_BEACON_PERIOD	,0x1003, 0x02);	/* mapped to cfgInvalid in FW150 */
DEF_IE(1xx_IE_DOT11_DTIM_PERIOD		,0x1004, -1);	/* mapped to cfgInvalid in FW150 */
DEF_IE(1xx_IE_DOT11_SHORT_RETRY_LIMIT	,0x1005, 0x01);
DEF_IE(1xx_IE_DOT11_LONG_RETRY_LIMIT	,0x1006, 0x01);
DEF_IE(100_IE_DOT11_WEP_DEFAULT_KEY_WRITE	,0x1007, 0x20);	/* configure default keys */
DEF_IE(1xx_IE_DOT11_MAX_XMIT_MSDU_LIFETIME	,0x1008, 0x04);
DEF_IE(1xx_IE_DOT11_GROUP_ADDR		,0x1009, -1);
DEF_IE(1xx_IE_DOT11_CURRENT_REG_DOMAIN	,0x100a, 0x02);
//It's harmless to have larger struct. Use USB case always.
DEF_IE(1xx_IE_DOT11_CURRENT_ANTENNA	,0x100b, 0x02);	/* in fact len=1 for PCI */
DEF_IE(1xx_IE_DOT11_UNKNOWN_100C	,0x100c, -1);	/* mapped to cfgInvalid in FW150 */
DEF_IE(1xx_IE_DOT11_TX_POWER_LEVEL	,0x100d, 0x01);
DEF_IE(1xx_IE_DOT11_CURRENT_CCA_MODE	,0x100e, 0x02);	/* in fact len=1 for PCI */
//USB doesn't return anything - len==0?!
DEF_IE(100_IE_DOT11_ED_THRESHOLD	,0x100f, 0x04);
DEF_IE(1xx_IE_DOT11_WEP_DEFAULT_KEY_SET	,0x1010, 0x01);	/* set default key ID */
DEF_IE(100_IE_DOT11_UNKNOWN_1011	,0x1011, -1);	/* mapped to cfgInvalid in FW150 */
DEF_IE(100_IE_DOT11_UNKNOWN_1012	,0x1012, -1);	/* mapped to cfgInvalid in FW150 */
DEF_IE(100_IE_DOT11_UNKNOWN_1013	,0x1013, -1);	/* mapped to cfgInvalid in FW150 */

#if 0
/* Experimentally obtained on acx100, fw 1.9.8.b
** -1 means that fw returned 'invalid IE'
** 0200 FC00 nnnn... are test read contents: u16 type, u16 len, data
** (AA are poison bytes marking bytes not written by fw)
**
** Looks like acx100 fw does not update len field (thus len=256-4=FC here)
** A number of IEs seem to trash type,len fields
** IEs marked 'huge' return gobs of data (no poison bytes remain)
*/
DEF_IE(100_IE_INVAL_00,			0x0000, -1);
DEF_IE(100_IE_INVAL_01,			0x0001, -1);	/* IE_ACX_TIMER, len=16 on older fw */
DEF_IE(100_IE_POWER_MGMT,		0x0002, 4);	/* 0200FC00 00040000 AAAAAAAA */
DEF_IE(100_IE_QUEUE_CONFIG,		0x0003, 28);	/* 0300FC00 48060000 9CAD0000 0101AAAA DCB00000 E4B00000 9CAA0000 00AAAAAA */
DEF_IE(100_IE_BLOCK_SIZE,		0x0004, 2);	/* 0400FC00 0001AAAA AAAAAAAA AAAAAAAA */
/* write only: */
DEF_IE(100_IE_MEMORY_CONFIG_OPTIONS,	0x0005, 20);
DEF_IE(100_IE_RATE_FALLBACK,		0x0006, 1);	/* 0600FC00 00AAAAAA AAAAAAAA AAAAAAAA */
/* write only: */
DEF_IE(100_IE_WEP_OPTIONS,		0x0007, 3);
DEF_IE(100_IE_MEMORY_MAP,		0x0008, 40);	/* huge: 0800FC00 30000000 6CA20000 70A20000... */
/* gives INVAL on read: */
DEF_IE(100_IE_SCAN_STATUS,		0x0009, -1);
DEF_IE(100_IE_ASSOC_ID,			0x000a, 2);	/* huge: 0A00FC00 00000000 01040800 00000000... */
DEF_IE(100_IE_INVAL_0B,			0x000b, -1);
/* 'command rejected': */
DEF_IE(100_IE_CONFIG_OPTIONS,		0x000c, -3);
DEF_IE(100_IE_FWREV,			0x000d, 24);	/* 0D00FC00 52657620 312E392E 382E6200 AAAAAAAA AAAAAAAA 05050201 AAAAAAAA */
DEF_IE(100_IE_FCS_ERROR_COUNT,		0x000e, 4);
DEF_IE(100_IE_MEDIUM_USAGE,		0x000f, 8);	/* E41F0000 2D780300 FCC91300 AAAAAAAA */
DEF_IE(100_IE_RXCONFIG,			0x0010, 4);	/* 1000FC00 00280000 AAAAAAAA AAAAAAAA */
DEF_IE(100_IE_QUEUE_THRESH,		0x0011, 12);	/* 1100FC00 AAAAAAAA 00000000 00000000 */
DEF_IE(100_IE_BSS_POWER_SAVE,		0x0012, 1);	/* 1200FC00 00AAAAAA AAAAAAAA AAAAAAAA */
/* read only, variable len */
DEF_IE(100_IE_FIRMWARE_STATISTICS,	0x0013, 256); /* 0000AC00 00000000 ... */
DEF_IE(100_IE_INT_CONFIG,		0x0014, 20);	/* 00000000 00000000 00000000 00000000 5D74D105 00000000 AAAAAAAA AAAAAAAA */
DEF_IE(100_IE_FEATURE_CONFIG,		0x0015, 8);	/* 1500FC00 16000000 AAAAAAAA AAAAAAAA */
/* returns 'invalid MAC': */
DEF_IE(100_IE_KEY_CHOOSE,		0x0016, -4);
DEF_IE(100_IE_INVAL_17,			0x0017, -1);
DEF_IE(100_IE_UNKNOWN_18,		0x0018, 0);	/* null len?! 1800FC00 AAAAAAAA AAAAAAAA AAAAAAAA */
DEF_IE(100_IE_UNKNOWN_19,		0x0019, 256);	/* huge: 1900FC00 9C1F00EA FEFFFFEA FEFFFFEA... */
DEF_IE(100_IE_INVAL_1A,			0x001A, -1);

DEF_IE(100_IE_DOT11_INVAL_1000,			0x1000, -1);
DEF_IE(100_IE_DOT11_STATION_ID,			0x1001, 6);	/* huge: 0110FC00 58B10E2F 03000000 00000000... */
DEF_IE(100_IE_DOT11_INVAL_1002,			0x1002, -1);
DEF_IE(100_IE_DOT11_INVAL_1003,			0x1003, -1);
DEF_IE(100_IE_DOT11_INVAL_1004,			0x1004, -1);
DEF_IE(100_IE_DOT11_SHORT_RETRY_LIMIT,		0x1005, 1);
DEF_IE(100_IE_DOT11_LONG_RETRY_LIMIT,		0x1006, 1);
/* write only: */
DEF_IE(100_IE_DOT11_WEP_DEFAULT_KEY_WRITE,	0x1007, 32);
DEF_IE(100_IE_DOT11_MAX_XMIT_MSDU_LIFETIME,	0x1008, 4);	/* huge: 0810FC00 00020000 F4010000 00000000... */
/* undoc but returns something */
DEF_IE(100_IE_DOT11_GROUP_ADDR,			0x1009, 12);	/* huge: 0910FC00 00000000 00000000 00000000... */
DEF_IE(100_IE_DOT11_CURRENT_REG_DOMAIN,		0x100a, 1);	/* 0A10FC00 30AAAAAA AAAAAAAA AAAAAAAA */
DEF_IE(100_IE_DOT11_CURRENT_ANTENNA,		0x100b, 1);	/* 0B10FC00 8FAAAAAA AAAAAAAA AAAAAAAA */
DEF_IE(100_IE_DOT11_INVAL_100C,			0x100c, -1);
DEF_IE(100_IE_DOT11_TX_POWER_LEVEL,		0x100d, 2);	/* 00000000 0100AAAA AAAAAAAA AAAAAAAA */
DEF_IE(100_IE_DOT11_CURRENT_CCA_MODE,		0x100e, 1);	/* 0E10FC00 0DAAAAAA AAAAAAAA AAAAAAAA */
DEF_IE(100_IE_DOT11_ED_THRESHOLD,		0x100f, 4);	/* 0F10FC00 70000000 AAAAAAAA AAAAAAAA */
/* set default key ID  */
DEF_IE(100_IE_DOT11_WEP_DEFAULT_KEY_SET,	0x1010, 1);	/* 1010FC00 00AAAAAA AAAAAAAA AAAAAAAA */
DEF_IE(100_IE_DOT11_INVAL_1011,			0x1011, -1);
DEF_IE(100_IE_DOT11_INVAL_1012,			0x1012, -1);
DEF_IE(100_IE_DOT11_INVAL_1013,			0x1013, -1);
DEF_IE(100_IE_DOT11_UNKNOWN_1014,		0x1014, 256);	/* huge */
DEF_IE(100_IE_DOT11_UNKNOWN_1015,		0x1015, 256);	/* huge */
DEF_IE(100_IE_DOT11_UNKNOWN_1016,		0x1016, 256);	/* huge */
DEF_IE(100_IE_DOT11_UNKNOWN_1017,		0x1017, 256);	/* huge */
DEF_IE(100_IE_DOT11_UNKNOWN_1018,		0x1018, 256);	/* huge */
DEF_IE(100_IE_DOT11_UNKNOWN_1019,		0x1019, 256);	/* huge */
#endif

#if 0
/* Experimentally obtained on PCI acx111 Xterasys XN-2522g, fw 1.2.1.34
** -1 means that fw returned 'invalid IE'
** 0400 0800 nnnn... are test read contents: u16 type, u16 len, data
** (AA are poison bytes marking bytes not written by fw)
**
** Looks like acx111 fw reports real len!
*/
DEF_IE(111_IE_INVAL_00,			0x0000, -1);
DEF_IE(111_IE_INVAL_01,			0x0001, -1);
DEF_IE(111_IE_POWER_MGMT,		0x0002, 12);
/* write only, variable len: 12 + rxqueue_cnt*8 + txqueue_cnt*4: */
DEF_IE(111_IE_MEMORY_CONFIG,		0x0003, 24);
DEF_IE(111_IE_BLOCK_SIZE,		0x0004, 8); /* 04000800 AA00AAAA AAAAAAAA */
/* variable len: 8 + rxqueue_cnt*8 + txqueue_cnt*8: */
DEF_IE(111_IE_QUEUE_HEAD,		0x0005, 24);
DEF_IE(111_IE_RATE_FALLBACK,		0x0006, 1);
/* acx100 name:WEP_OPTIONS */
/* said to have len:1 (not true, actually returns 12 bytes): */
DEF_IE(111_IE_RADIO_BAND,		0x0007, 12); /* 07000C00 AAAA1F00 FF03AAAA AAAAAAAA */
DEF_IE(111_IE_MEMORY_MAP,		0x0008, 48);
/* said to have len:4, but gives INVAL on read: */
DEF_IE(111_IE_SCAN_STATUS,		0x0009, -1);
DEF_IE(111_IE_ASSOC_ID,			0x000a, 2);
/* write only, len is not known: */
DEF_IE(111_IE_UNKNOWN_0B,		0x000b, 0);
/* read only, variable len. I see 67 byte reads: */
DEF_IE(111_IE_CONFIG_OPTIONS,		0x000c, 67); /* 0C004300 01160500 ... */
DEF_IE(111_IE_FWREV,			0x000d, 24);
DEF_IE(111_IE_FCS_ERROR_COUNT,		0x000e, 4);
DEF_IE(111_IE_MEDIUM_USAGE,		0x000f, 8);
DEF_IE(111_IE_RXCONFIG,			0x0010, 4);
DEF_IE(111_IE_QUEUE_THRESH,		0x0011, 12);
DEF_IE(111_IE_BSS_POWER_SAVE,		0x0012, 1);
/* read only, variable len. I see 240 byte reads: */
DEF_IE(111_IE_FIRMWARE_STATISTICS,	0x0013, 240); /* 1300F000 00000000 ... */
/* said to have len=17. looks like fw pads it to 20: */
DEF_IE(111_IE_INT_CONFIG,		0x0014, 20); /* 14001400 00000000 00000000 00000000 00000000 00000000 */
DEF_IE(111_IE_FEATURE_CONFIG,		0x0015, 8);
/* said to be name:KEY_INDICATOR, len:4, but gives INVAL on read: */
DEF_IE(111_IE_KEY_CHOOSE,		0x0016, -1);
/* said to have len:4, but in fact returns 8: */
DEF_IE(111_IE_MAX_USB_XFR,		0x0017, 8); /* 17000800 00014000 00000000 */
DEF_IE(111_IE_INVAL_18,			0x0018, -1);
DEF_IE(111_IE_INVAL_19,			0x0019, -1);
/* undoc but returns something: */
/* huh, fw indicates len=20 but uses 4 more bytes in buffer??? */
DEF_IE(111_IE_UNKNOWN_1A,		0x001A, 20); /* 1A001400 AA00AAAA 0000020F FF030000 00020000 00000007 04000000 */

DEF_IE(111_IE_DOT11_INVAL_1000,			0x1000, -1);
DEF_IE(111_IE_DOT11_STATION_ID,			0x1001, 6);
DEF_IE(111_IE_DOT11_FRAG_THRESH,		0x1002, 2);
/* acx100 only? gives INVAL on read: */
DEF_IE(111_IE_DOT11_BEACON_PERIOD,		0x1003, -1);
/* said to be MAX_RECV_MSDU_LIFETIME: */
DEF_IE(111_IE_DOT11_DTIM_PERIOD,		0x1004, 4);
DEF_IE(111_IE_DOT11_SHORT_RETRY_LIMIT,		0x1005, 1);
DEF_IE(111_IE_DOT11_LONG_RETRY_LIMIT,		0x1006, 1);
/* acx100 only? gives INVAL on read: */
DEF_IE(111_IE_DOT11_WEP_DEFAULT_KEY_WRITE,	0x1007, -1);
DEF_IE(111_IE_DOT11_MAX_XMIT_MSDU_LIFETIME,	0x1008, 4);
/* undoc but returns something. maybe it's 2 multicast MACs to listen to? */
DEF_IE(111_IE_DOT11_GROUP_ADDR,			0x1009, 12); /* 09100C00 00000000 00000000 00000000 */
DEF_IE(111_IE_DOT11_CURRENT_REG_DOMAIN,		0x100a, 1);
DEF_IE(111_IE_DOT11_CURRENT_ANTENNA,		0x100b, 2);
DEF_IE(111_IE_DOT11_INVAL_100C,			0x100c, -1);
DEF_IE(111_IE_DOT11_TX_POWER_LEVEL,		0x100d, 1);
/* said to have len=1 but gives INVAL on read: */
DEF_IE(111_IE_DOT11_CURRENT_CCA_MODE,		0x100e, -1);
/* said to have len=4 but gives INVAL on read: */
DEF_IE(111_IE_DOT11_ED_THRESHOLD,		0x100f, -1);
/* set default key ID. write only: */
DEF_IE(111_IE_DOT11_WEP_DEFAULT_KEY_SET,	0x1010, 1);
/* undoc but returns something: */
DEF_IE(111_IE_DOT11_UNKNOWN_1011,		0x1011, 1); /* 11100100 20 */
DEF_IE(111_IE_DOT11_INVAL_1012,			0x1012, -1);
DEF_IE(111_IE_DOT11_INVAL_1013,			0x1013, -1);
#endif


/***********************************************************************
**Information Frames Structures
*/

/* Used in beacon frames and the like */
#define DOT11RATEBYTE_1		(1*2)
#define DOT11RATEBYTE_2		(2*2)
#define DOT11RATEBYTE_5_5	(5*2+1)
#define DOT11RATEBYTE_11	(11*2)
#define DOT11RATEBYTE_22	(22*2)
#define DOT11RATEBYTE_6_G	(6*2)
#define DOT11RATEBYTE_9_G	(9*2)
#define DOT11RATEBYTE_12_G	(12*2)
#define DOT11RATEBYTE_18_G	(18*2)
#define DOT11RATEBYTE_24_G	(24*2)
#define DOT11RATEBYTE_36_G	(36*2)
#define DOT11RATEBYTE_48_G	(48*2)
#define DOT11RATEBYTE_54_G	(54*2)
#define DOT11RATEBYTE_BASIC	0x80	/* flags rates included in basic rate set */


/***********************************************************************
** rxbuffer_t
**
** This is the format of rx data returned by acx
*/

/* I've hoped it's a 802.11 PHY header, but no...
 * so far, I've seen on acx111:
 * 0000 3a00 0000 0000 IBBS Beacons
 * 0000 3c00 0000 0000 ESS Beacons
 * 0000 2700 0000 0000 Probe requests
 * --vda
 */
typedef struct phy_hdr {
	u8	unknown[4] ACX_PACKED;
	u8	acx111_unknown[4] ACX_PACKED;
} phy_hdr_t;

/* seems to be a bit similar to hfa384x_rx_frame.
 * These fields are still not quite obvious, though.
 * Some seem to have different meanings... */

#define RXBUF_HDRSIZE 12
#define PHY_HDR(rxbuf) ((phy_hdr_t*)&rxbuf->hdr_a3)
#define RXBUF_BYTES_RCVD(rxbuf) (le16_to_cpu(rxbuf->mac_cnt_rcvd) & 0xfff)
#define RXBUF_BYTES_USED(rxbuf) \
		((le16_to_cpu(rxbuf->mac_cnt_rcvd) & 0xfff) + RXBUF_HDRSIZE)
/* USBism */
#define RXBUF_IS_TXSTAT(rxbuf) (le16_to_cpu(rxbuf->mac_cnt_rcvd) & 0x8000)
/*
mac_cnt_rcvd:
    12 bits: length of frame from control field to last byte of FCS
    3 bits: reserved
    1 bit: 1 = it's a tx status info, not a rx packet (USB only)

mac_cnt_mblks:
    6 bits: number of memory block used to store frame in adapter memory
    1 bit: Traffic Indicator bit in TIM of received Beacon was set

mac_status: 1 byte (bitmap):
    7 Matching BSSID
    6 Matching SSID
    5 BDCST	Address 1 field is a broadcast
    4 VBM	received beacon frame has more than one set bit (?!)
    3 TIM Set	bit representing this station is set in TIM of received beacon
    2 GROUP	Address 1 is a multicast
    1 ADDR1	Address 1 matches our MAC
    0 FCSGD	FSC is good

phy_stat_baseband: 1 byte (bitmap):
    7 Preamble		frame had a long preamble
    6 PLCP Error	CRC16 error in PLCP header
    5 Unsup_Mod		unsupported modulation
    4 Selected Antenna	antenna 1 was used to receive this frame
    3 PBCC/CCK		frame used: 1=PBCC, 0=CCK modulation
    2 OFDM		frame used OFDM modulation
    1 TI Protection	protection frame was detected
    0 Reserved

phy_plcp_signal: 1 byte:
    Receive PLCP Signal field from the Baseband Processor

phy_level: 1 byte:
    receive AGC gain level (can be used to measure receive signal strength)

phy_snr: 1 byte:
    estimated noise power of equalized receive signal
    at input of FEC decoder (can be used to measure receive signal quality)

time: 4 bytes:
    timestamp sampled from either the Access Manager TSF counter
    or free-running microsecond counter when the MAC receives
    first byte of PLCP header.
*/

typedef struct rxbuffer {
	u16	mac_cnt_rcvd ACX_PACKED;	/* only 12 bits are len! (0xfff) */
	u8	mac_cnt_mblks ACX_PACKED;
	u8	mac_status ACX_PACKED;
	u8	phy_stat_baseband ACX_PACKED;	/* bit 0x80: used LNA (Low-Noise Amplifier) */
	u8	phy_plcp_signal ACX_PACKED;
	u8	phy_level ACX_PACKED;		/* PHY stat */
	u8	phy_snr ACX_PACKED;		/* PHY stat */
	u32	time ACX_PACKED;		/* timestamp upon MAC rcv first byte */
/* 4-byte (acx100) or 8-byte (acx111) phy header will be here
** if RX_CFG1_INCLUDE_PHY_HDR is in effect:
**	phy_hdr_t phy			*/
	wlan_hdr_a3_t hdr_a3 ACX_PACKED;
	/* maximally sized data part of wlan packet */
	u8	data_a3[WLAN_A4FR_MAXLEN_WEP_FCS - WLAN_HDR_A3_LEN] ACX_PACKED;
	/* can add hdr/data_a4 if needed */
} rxbuffer_t;


/*--- Firmware statistics ----------------------------------------------------*/
typedef struct fw_stats {
	u32	val0x0 ACX_PACKED;		/* hdr; */
	u32	tx_desc_of ACX_PACKED;
	u32	rx_oom ACX_PACKED;
	u32	rx_hdr_of ACX_PACKED;
	u32	rx_hdr_use_next ACX_PACKED;
	u32	rx_dropped_frame ACX_PACKED;
	u32	rx_frame_ptr_err ACX_PACKED;
	u32	rx_xfr_hint_trig ACX_PACKED;

	u32	rx_dma_req ACX_PACKED;
	u32	rx_dma_err ACX_PACKED;
	u32	tx_dma_req ACX_PACKED;
	u32	tx_dma_err ACX_PACKED;

	u32	cmd_cplt ACX_PACKED;
	u32	fiq ACX_PACKED;
	u32	rx_hdrs ACX_PACKED;
	u32	rx_cmplt ACX_PACKED;
	u32	rx_mem_of ACX_PACKED;
	u32	rx_rdys ACX_PACKED;
	u32	irqs ACX_PACKED;
	u32	acx_trans_procs ACX_PACKED;
	u32	decrypt_done ACX_PACKED;
	u32	dma_0_done ACX_PACKED;
	u32	dma_1_done ACX_PACKED;
	u32	tx_exch_complet ACX_PACKED;
	u32	commands ACX_PACKED;
	u32	acx_rx_procs ACX_PACKED;
	u32	hw_pm_mode_changes ACX_PACKED;
	u32	host_acks ACX_PACKED;
	u32	pci_pm ACX_PACKED;
	u32	acm_wakeups ACX_PACKED;

	u32	wep_key_count ACX_PACKED;
	u32	wep_default_key_count ACX_PACKED;
	u32	dot11_def_key_mib ACX_PACKED;
	u32	wep_key_not_found ACX_PACKED;
	u32	wep_decrypt_fail ACX_PACKED;
} fw_stats_t;

/* Firmware version struct */

typedef struct fw_ver {
	u16	cmd ACX_PACKED;
	u16	size ACX_PACKED;
	char	fw_id[20] ACX_PACKED;
	u32	hw_id ACX_PACKED;
} fw_ver_t;

#define FW_ID_SIZE 20


/*--- WEP stuff --------------------------------------------------------------*/
#define DOT11_MAX_DEFAULT_WEP_KEYS	4

/* non-firmware struct, no packing necessary */
typedef struct wep_key {
	size_t	size; /* most often used member first */
	u8	index;
	u8	key[29];
	u16	strange_filler;
} wep_key_t;			/* size = 264 bytes (33*8) */
/* FIXME: We don't have size 264! Or is there 2 bytes beyond the key
 * (strange_filler)? */

/* non-firmware struct, no packing necessary */
typedef struct key_struct {
	u8	addr[ETH_ALEN];	/* 0x00 */
	u16	filler1;	/* 0x06 */
	u32	filler2;	/* 0x08 */
	u32	index;		/* 0x0c */
	u16	len;		/* 0x10 */
	u8	key[29];	/* 0x12; is this long enough??? */
} key_struct_t;			/* size = 276. FIXME: where is the remaining space?? */


/*--- Client (peer) info -----------------------------------------------------*/
/* priv->sta_list[] is used for:
** accumulating and processing of scan results
** keeping client info in AP mode
** keeping AP info in STA mode (AP is the only one 'client')
** keeping peer info in ad-hoc mode
** non-firmware struct --> no packing necessary */
enum {
	CLIENT_EMPTY_SLOT_0 = 0,
	CLIENT_EXIST_1 = 1,
	CLIENT_AUTHENTICATED_2 = 2,
	CLIENT_ASSOCIATED_3 = 3,
	CLIENT_JOIN_CANDIDATE = 4
};
struct client {
	/* most frequent access first */
	u8	used;			/* misnamed, more like 'status' */
	struct client*	next;
	unsigned long	mtime;		/* last time we heard it, in jiffies */
	size_t	essid_len;		/* length of ESSID (without '\0') */
	u32	sir;			/* Standard IR */
	u32	snr;			/* Signal to Noise Ratio */
	u16	aid;			/* association ID */
	u16	seq;			/* from client's auth req */
	u16	auth_alg;		/* from client's auth req */
	u16	cap_info;		/* from client's assoc req */
	u16	rate_cap;		/* what client supports (all rates) */
	u16	rate_bas;		/* what client supports (basic rates) */
	u16	rate_cfg;		/* what is allowed (by iwconfig etc) */
	u16	rate_cur;		/* currently used rate mask */
	u8	rate_100;		/* currently used rate byte (acx100 only) */
	u8	address[ETH_ALEN];
	u8	bssid[ETH_ALEN];	/* ad-hoc hosts can have bssid != mac */
	u8	channel;
	u8	auth_step;
	u8	ignore_count;
	u8	fallback_count;
	u8	stepup_count;
	char	essid[IW_ESSID_MAX_SIZE + 1];	/* ESSID and trailing '\0'  */
/* FIXME: this one is too damn big */
	char	challenge_text[WLAN_CHALLENGE_LEN];
};


/***********************************************************************
** Hardware structures
*/

/* An opaque typesafe helper type
 *
 * Some hardware fields are actually pointers,
 * but they have to remain u32, since using ptr instead
 * (8 bytes on 64bit systems!) would disrupt the fixed descriptor
 * format the acx firmware expects in the non-user area.
 * Since we cannot cram an 8 byte ptr into 4 bytes, we need to
 * enforce that pointed to data remains in low memory
 * (address value needs to fit in 4 bytes) on 64bit systems.
 *
 * This is easy to get wrong, thus we are using a small struct
 * and special macros to access it. Macros will check for
 * attempts to overflow an acx_ptr with value > 0xffffffff.
 *
 * Attempts to use acx_ptr without macros result in compile-time errors */

typedef struct {
	u32	v ACX_PACKED;
} acx_ptr;

#if ACX_DEBUG
#define CHECK32(n) BUG_ON(sizeof(n)>4 && (long)(n)>0xffffff00)
#else
#define CHECK32(n) ((void)0)
#endif

/* acx_ptr <-> integer conversion */
#define cpu2acx(n) ({ CHECK32(n); ((acx_ptr){ .v = cpu_to_le32(n) }); })
#define acx2cpu(a) (le32_to_cpu(a.v))

/* acx_ptr <-> pointer conversion */
#define ptr2acx(p) ({ CHECK32(p); ((acx_ptr){ .v = cpu_to_le32((u32)(long)(p)) }); })
#define acx2ptr(a) ((void*)le32_to_cpu(a.v))

/* Values for rate field (acx100 only) */
#define RATE100_1		10
#define RATE100_2		20
#define RATE100_5		55
#define RATE100_11		110
#define RATE100_22		220
/* This bit denotes use of PBCC:
** (PBCC encoding is usable with 11 and 22 Mbps speeds only) */
#define RATE100_PBCC511		0x80

/* Bit values for rate111 field */
#define RATE111_1		0x0001	/* DBPSK */
#define RATE111_2		0x0002	/* DQPSK */
#define RATE111_5		0x0004	/* CCK or PBCC */
#define RATE111_6		0x0008	/* CCK-OFDM or OFDM */
#define RATE111_9		0x0010	/* CCK-OFDM or OFDM */
#define RATE111_11		0x0020	/* CCK or PBCC */
#define RATE111_12		0x0040	/* CCK-OFDM or OFDM */
#define RATE111_18		0x0080	/* CCK-OFDM or OFDM */
#define RATE111_22		0x0100	/* PBCC */
#define RATE111_24		0x0200	/* CCK-OFDM or OFDM */
#define RATE111_36		0x0400	/* CCK-OFDM or OFDM */
#define RATE111_48		0x0800	/* CCK-OFDM or OFDM */
#define RATE111_54		0x1000	/* CCK-OFDM or OFDM */
#define RATE111_RESERVED	0x2000
#define RATE111_PBCC511		0x4000  /* PBCC mod at 5.5 or 11Mbit (else CCK) */
#define RATE111_SHORTPRE	0x8000  /* short preamble */
/* Special 'try everything' value */
#define RATE111_ALL		0x1fff
/* These bits denote acx100 compatible settings */
#define RATE111_ACX100_COMPAT	0x0127
/* These bits denote 802.11b compatible settings */
#define RATE111_80211B_COMPAT	0x0027

/* Descriptor Ctl field bits
 * init value is 0x8e, "idle" value is 0x82 (in idle tx descs)
 */
#define DESC_CTL_SHORT_PREAMBLE	0x01	/* preamble type: 0 = long; 1 = short */
#define DESC_CTL_FIRSTFRAG	0x02	/* this is the 1st frag of the frame */
#define DESC_CTL_AUTODMA	0x04
#define DESC_CTL_RECLAIM	0x08	/* ready to reuse */
#define DESC_CTL_HOSTDONE	0x20	/* host has finished processing */
#define DESC_CTL_ACXDONE	0x40	/* acx has finished processing */
/* host owns the desc [has to be released last, AFTER modifying all other desc fields!] */
#define DESC_CTL_HOSTOWN	0x80
#define	DESC_CTL_ACXDONE_HOSTOWN (DESC_CTL_ACXDONE | DESC_CTL_HOSTOWN)

/* Descriptor Status field
 */
#define	DESC_STATUS_FULL	(1 << 31)

/* NB: some bits may be interesting for Monitor mode tx (aka Raw tx): */
#define DESC_CTL2_SEQ		0x01	/* don't increase sequence field */
#define DESC_CTL2_FCS		0x02	/* don't add the FCS */
#define DESC_CTL2_MORE_FRAG	0x04
#define DESC_CTL2_RETRY		0x08	/* don't increase retry field */
#define DESC_CTL2_POWER		0x10	/* don't increase power mgmt. field */
#define DESC_CTL2_RTS		0x20	/* do RTS/CTS magic before sending */
#define DESC_CTL2_WEP		0x40	/* encrypt this frame */
#define DESC_CTL2_DUR		0x80	/* don't increase duration field */

/***********************************************************************
** PCI structures
*/
/* IRQ Constants
** (outside of "#ifdef PCI" because USB (mis)uses HOST_INT_SCAN_COMPLETE) */
#define HOST_INT_RX_DATA	0x0001
#define HOST_INT_TX_COMPLETE	0x0002
#define HOST_INT_TX_XFER	0x0004
#define HOST_INT_RX_COMPLETE	0x0008
#define HOST_INT_DTIM		0x0010
#define HOST_INT_BEACON		0x0020
#define HOST_INT_TIMER		0x0040
#define HOST_INT_KEY_NOT_FOUND	0x0080
#define HOST_INT_IV_ICV_FAILURE	0x0100
#define HOST_INT_CMD_COMPLETE	0x0200
#define HOST_INT_INFO		0x0400
#define HOST_INT_OVERFLOW	0x0800
#define HOST_INT_PROCESS_ERROR	0x1000
#define HOST_INT_SCAN_COMPLETE	0x2000
#define HOST_INT_FCS_THRESHOLD	0x4000
#define HOST_INT_UNKNOWN	0x8000

/* Outside of "#ifdef PCI" because USB needs to know sizeof()
** of txdesc and rxdesc: */
struct txdesc {
	acx_ptr	pNextDesc ACX_PACKED;	/* pointer to next txdesc */
	acx_ptr	HostMemPtr ACX_PACKED;			/* 0x04 */
	acx_ptr	AcxMemPtr ACX_PACKED;			/* 0x08 */
	u32	tx_time ACX_PACKED;			/* 0x0c */
	u16	total_length ACX_PACKED;		/* 0x10 */
	u16	Reserved ACX_PACKED;			/* 0x12 */

/* The following 16 bytes do not change when acx100 owns the descriptor */
/* BUG: fw clears last byte of this area which is supposedly reserved
** for driver use. amd64 blew up. We dare not use it now */
	u32	dummy[4] ACX_PACKED;

	u8	Ctl_8 ACX_PACKED;			/* 0x24, 8bit value */
	u8	Ctl2_8 ACX_PACKED;			/* 0x25, 8bit value */
	u8	error ACX_PACKED;			/* 0x26 */
	u8	ack_failures ACX_PACKED;		/* 0x27 */
	u8	rts_failures ACX_PACKED;		/* 0x28 */
	u8	rts_ok ACX_PACKED;			/* 0x29 */
	union {
		struct {
			u8	rate ACX_PACKED;	/* 0x2a */
			u8	queue_ctrl ACX_PACKED;	/* 0x2b */
		} r1 ACX_PACKED;
		struct {
			u16	rate111 ACX_PACKED;	/* 0x2a */
		} r2 ACX_PACKED;
	} u ACX_PACKED;
	u32	queue_info ACX_PACKED;			/* 0x2c (acx100, reserved on acx111) */
};		/* size : 48 = 0x30 */
/* NB: acx111 txdesc structure is 4 byte larger */
/* All these 4 extra bytes are reserved. tx alloc code takes them into account */

struct rxdesc {
	acx_ptr	pNextDesc ACX_PACKED;			/* 0x00 */
	acx_ptr	HostMemPtr ACX_PACKED;			/* 0x04 */
	acx_ptr	ACXMemPtr ACX_PACKED;			/* 0x08 */
	u32	rx_time ACX_PACKED;			/* 0x0c */
	u16	total_length ACX_PACKED;		/* 0x10 */
	u16	WEP_length ACX_PACKED;			/* 0x12 */
	u32	WEP_ofs ACX_PACKED;			/* 0x14 */

/* the following 16 bytes do not change when acx100 owns the descriptor */
	u8	driverWorkspace[16] ACX_PACKED;		/* 0x18 */

	u8	Ctl_8 ACX_PACKED;
	u8	rate ACX_PACKED;
	u8	error ACX_PACKED;
	u8	SNR ACX_PACKED;				/* Signal-to-Noise Ratio */
	u8	RxLevel ACX_PACKED;
	u8	queue_ctrl ACX_PACKED;
	u16	unknown ACX_PACKED;
	u32	unknown2 ACX_PACKED;
};		/* size 52 = 0x34 */

#ifdef ACX_PCI

/* Register I/O offsets */
#define ACX100_EEPROM_ID_OFFSET	0x380

/* please add further ACX hardware register definitions only when
   it turns out you need them in the driver, and please try to use
   firmware functionality instead, since using direct I/O access instead
   of letting the firmware do it might confuse the firmware's state
   machine */

/* ***** ABSOLUTELY ALWAYS KEEP OFFSETS IN SYNC WITH THE INITIALIZATION
** OF THE I/O ARRAYS!!!! (grep for '^IO_ACX') ***** */
enum {
	IO_ACX_SOFT_RESET = 0,

	IO_ACX_SLV_MEM_ADDR,
	IO_ACX_SLV_MEM_DATA,
	IO_ACX_SLV_MEM_CTL,
	IO_ACX_SLV_END_CTL,

	IO_ACX_FEMR,		/* Function Event Mask */

	IO_ACX_INT_TRIG,
	IO_ACX_IRQ_MASK,
	IO_ACX_IRQ_STATUS_NON_DES,
	IO_ACX_IRQ_STATUS_CLEAR, /* CLEAR = clear on read */
	IO_ACX_IRQ_ACK,
	IO_ACX_HINT_TRIG,

	IO_ACX_ENABLE,

	IO_ACX_EEPROM_CTL,
	IO_ACX_EEPROM_ADDR,
	IO_ACX_EEPROM_DATA,
	IO_ACX_EEPROM_CFG,

	IO_ACX_PHY_ADDR,
	IO_ACX_PHY_DATA,
	IO_ACX_PHY_CTL,

	IO_ACX_GPIO_OE,

	IO_ACX_GPIO_OUT,

	IO_ACX_CMD_MAILBOX_OFFS,
	IO_ACX_INFO_MAILBOX_OFFS,
	IO_ACX_EEPROM_INFORMATION,

	IO_ACX_EE_START,
	IO_ACX_SOR_CFG,
	IO_ACX_ECPU_CTRL
};
/* ***** ABSOLUTELY ALWAYS KEEP OFFSETS IN SYNC WITH THE INITIALIZATION
** OF THE I/O ARRAYS!!!! (grep for '^IO_ACX') ***** */

/* Values for IO_ACX_INT_TRIG register: */
/* inform hw that rxdesc in queue needs processing */
#define INT_TRIG_RXPRC		0x08
/* inform hw that txdesc in queue needs processing */
#define INT_TRIG_TXPRC		0x04
/* ack that we received info from info mailbox */
#define INT_TRIG_INFOACK	0x02
/* inform hw that we have filled command mailbox */
#define INT_TRIG_CMD		0x01

struct txhostdesc {
	acx_ptr	data_phy ACX_PACKED;			/* 0x00 [u8 *] */
	u16	data_offset ACX_PACKED;			/* 0x04 */
	u16	reserved ACX_PACKED;			/* 0x06 */
	u16	Ctl_16 ACX_PACKED;	/* 16bit value, endianness!! */
	u16	length ACX_PACKED;			/* 0x0a */
	acx_ptr	desc_phy_next ACX_PACKED;		/* 0x0c [txhostdesc *] */
	acx_ptr	pNext ACX_PACKED;			/* 0x10 [txhostdesc *] */
	u32	Status ACX_PACKED;			/* 0x14, unused on Tx */
/* From here on you can use this area as you want (variable length, too!) */
	u8	*data ACX_PACKED;
};

struct rxhostdesc {
	acx_ptr	data_phy ACX_PACKED;			/* 0x00 [rxbuffer_t *] */
	u16	data_offset ACX_PACKED;			/* 0x04 */
	u16	reserved ACX_PACKED;			/* 0x06 */
	u16	Ctl_16 ACX_PACKED;			/* 0x08; 16bit value, endianness!! */
	u16	length ACX_PACKED;			/* 0x0a */
	acx_ptr	desc_phy_next ACX_PACKED;		/* 0x0c [rxhostdesc_t *] */
	acx_ptr	pNext ACX_PACKED;			/* 0x10 [rxhostdesc_t *] */
	u32	Status ACX_PACKED;			/* 0x14 */
/* From here on you can use this area as you want (variable length, too!) */
	rxbuffer_t *data ACX_PACKED;
};

#endif /* ACX_PCI */

/***********************************************************************
** USB structures and constants
*/
#ifdef ACX_USB

/* Used for usb_txbuffer.desc field */
#define USB_TXBUF_TXDESC	0xA
/* Size of header (everything up to data[]) */
#define USB_TXBUF_HDRSIZE	14
typedef struct usb_txbuffer {
	u16	desc ACX_PACKED;
	u16	mpdu_len ACX_PACKED;
	u8	queue_index ACX_PACKED;
	u8	rate ACX_PACKED;
	u32	hostdata ACX_PACKED;
	u8	ctrl1 ACX_PACKED;
	u8	ctrl2 ACX_PACKED;
	u16	data_len ACX_PACKED;
	/* wlan packet content is placed here: */
	u8	data[WLAN_A4FR_MAXLEN_WEP_FCS] ACX_PACKED;
} usb_txbuffer_t;

/* USB returns either rx packets (see rxbuffer) or
** these "tx status" structs: */
typedef struct usb_txstatus {
	u16	mac_cnt_rcvd ACX_PACKED;	/* only 12 bits are len! (0xfff) */
	u8	queue_index ACX_PACKED;
	u8	mac_status ACX_PACKED;		/* seen 0x20 on tx failure */
	u32	hostdata ACX_PACKED;
	u8	rate ACX_PACKED;
	u8	ack_failures ACX_PACKED;
	u8	rts_failures ACX_PACKED;
	u8	rts_ok ACX_PACKED;
} usb_txstatus_t;

typedef struct usb_tx {
	unsigned	busy:1;
	struct urb	*urb;
	wlandevice_t	*priv;
	/* actual USB bulk output data block is here: */
	usb_txbuffer_t	bulkout;
} usb_tx_t;

struct usb_rx_plain {
	unsigned	busy:1;
	struct urb	*urb;
	wlandevice_t	*priv;
	rxbuffer_t	bulkin;
};

typedef struct usb_rx {
	unsigned	busy:1;
	struct urb	*urb;
	wlandevice_t	*priv;
	rxbuffer_t	bulkin;
 /* Make entire structure 4k. Report if it breaks something. */
	u8 padding[4*1024 - sizeof(struct usb_rx_plain)];
} usb_rx_t;
#endif /* ACX_USB */


/* Config Option structs */

typedef struct co_antennas {
	u8	type ACX_PACKED;
	u8	len ACX_PACKED;
	u8	list[2] ACX_PACKED;
} co_antennas_t;

typedef struct co_powerlevels {
	u8	type ACX_PACKED;
	u8	len ACX_PACKED;
	u16	list[8] ACX_PACKED;
} co_powerlevels_t;

typedef struct co_datarates {
	u8	type ACX_PACKED;
	u8	len ACX_PACKED;
	u8	list[8] ACX_PACKED;
} co_datarates_t;

typedef struct co_domains {
	u8	type ACX_PACKED;
	u8	len ACX_PACKED;
	u8	list[6] ACX_PACKED;
} co_domains_t;

typedef struct co_product_id {
	u8	type ACX_PACKED;
	u8	len ACX_PACKED;
	u8	list[128] ACX_PACKED;
} co_product_id_t;

typedef struct co_manuf_id {
	u8	type ACX_PACKED;
	u8	len ACX_PACKED;
	u8	list[128] ACX_PACKED;
} co_manuf_t;

typedef struct co_fixed {
	char	NVSv[8] ACX_PACKED;
/*	u16	NVS_vendor_offs;	ACX111-only */
/*	u16	unknown;		ACX111-only */
	u8	MAC[6] ACX_PACKED;	/* ACX100-only */
	u16	probe_delay ACX_PACKED;	/* ACX100-only */
	u32	eof_memory ACX_PACKED;
	u8	dot11CCAModes ACX_PACKED;
	u8	dot11Diversity ACX_PACKED;
	u8	dot11ShortPreambleOption ACX_PACKED;
	u8	dot11PBCCOption ACX_PACKED;
	u8	dot11ChannelAgility ACX_PACKED;
	u8	dot11PhyType ACX_PACKED; /* FIXME: does 802.11 call it "dot11PHYType"? */
	u8	dot11TempType ACX_PACKED;
	u8	table_count ACX_PACKED;
} co_fixed_t;

typedef struct acx111_ie_configoption {
	u16			type ACX_PACKED;
	u16			len ACX_PACKED;
/* Do not access below members directly, they are in fact variable length */
	co_fixed_t		fixed ACX_PACKED;
	co_antennas_t		antennas ACX_PACKED;
	co_powerlevels_t	power_levels ACX_PACKED;
	co_datarates_t		data_rates ACX_PACKED;
	co_domains_t		domains ACX_PACKED;
	co_product_id_t		product_id ACX_PACKED;
	co_manuf_t		manufacturer ACX_PACKED;
	u8			_padding[4];
} acx111_ie_configoption_t;


/***********************************************************************
** Main acx per-device data structure (netdev_priv(dev))
*/
#define ACX_STATE_FW_LOADED	0x01
#define ACX_STATE_IFACE_UP	0x02

/* MAC mode (BSS type) defines
 * Note that they shouldn't be redefined, since they are also used
 * during communication with firmware */
#define ACX_MODE_0_ADHOC	0
#define ACX_MODE_1_UNUSED	1
#define ACX_MODE_2_STA		2
#define ACX_MODE_3_AP		3
/* These are our own inventions. Sending these to firmware
** makes it stop emitting beacons, which is exactly what we want
** for these modes */
#define ACX_MODE_MONITOR	0xfe
#define ACX_MODE_OFF		0xff
/* 'Submode': identifies exact status of ADHOC/STA host */
#define ACX_STATUS_0_STOPPED		0
#define ACX_STATUS_1_SCANNING		1
#define ACX_STATUS_2_WAIT_AUTH		2
#define ACX_STATUS_3_AUTHENTICATED	3
#define ACX_STATUS_4_ASSOCIATED		4

/* FIXME: this should be named something like struct acx_priv (typedef'd to
 * acx_priv_t) */

/* non-firmware struct, no packing necessary */
struct wlandevice {
	/* most frequent accesses first (dereferencing and cache line!) */

	/*** Locking ***/
	struct semaphore	sem;
	spinlock_t		lock;
#if defined(PARANOID_LOCKING) /* Lock debugging */
	const char		*last_sem;
	const char		*last_lock;
	unsigned long		sem_time;
	unsigned long		lock_time;
#endif

	/*** Device chain ***/
	struct wlandevice	*next;		/* link for list of devices */

	/*** Linux network device ***/
	struct net_device	*netdev;	/* pointer to linux netdevice */
	struct net_device	*prev_nd;	/* FIXME: We should not chain via our
						 * private struct wlandevice _and_
						 * the struct net_device */
	/*** Device statistics ***/
	struct net_device_stats	stats;		/* net device statistics */
#ifdef WIRELESS_EXT
	struct iw_statistics	wstats;		/* wireless statistics */
#endif
	/*** Power managment ***/
	struct pm_dev		*pm;		/* PM crap */

	/*** Management timer ***/
	struct timer_list	mgmt_timer;

	/*** Hardware identification ***/
	const char		*chip_name;
	u8			dev_type;
	u8			chip_type;
	u8			form_factor;
	u8			radio_type;
	u8			eeprom_version;

	/*** Config retrieved from EEPROM ***/
	char			cfgopt_NVSv[8];
	u16			cfgopt_NVS_vendor_offs;
	u8			cfgopt_MAC[6];
	u16			cfgopt_probe_delay;
	u32			cfgopt_eof_memory;
	u8			cfgopt_dot11CCAModes;
	u8			cfgopt_dot11Diversity;
	u8			cfgopt_dot11ShortPreambleOption;
	u8			cfgopt_dot11PBCCOption;
	u8			cfgopt_dot11ChannelAgility;
	u8			cfgopt_dot11PhyType;	/* FIXME: does 802.11 call it "dot11PHYType"? */
	u8			cfgopt_dot11TempType;
	co_antennas_t		cfgopt_antennas;
	co_powerlevels_t	cfgopt_power_levels;
	co_datarates_t		cfgopt_data_rates;
	co_domains_t		cfgopt_domains;
	co_product_id_t		cfgopt_product_id;
	co_manuf_t		cfgopt_manufacturer;

	/*** Firmware identification ***/
	char		firmware_version[FW_ID_SIZE+1];
	u32		firmware_numver;
	u32		firmware_id;
	const u16	*ie_len;
	const u16	*ie_len_dot11;

	/*** Device state ***/
	u16		dev_state_mask;
	u8		led_power;		/* power LED status */
	u32		get_mask;		/* mask of settings to fetch from the card */
	u32		set_mask;		/* mask of settings to write to the card */

	/* Barely used in USB case */
	u16		irq_status;

	u8		after_interrupt_jobs;	/* mini job list for doing actions after an interrupt occurred */
	WORK_STRUCT	after_interrupt_task;	/* our task for after interrupt actions */

	/*** scanning ***/
	u16		scan_count;		/* number of times to do channel scan */
	u8		scan_mode;		/* 0 == active, 1 == passive, 2 == background */
	u8		scan_rate;
	u16		scan_duration;
	u16		scan_probe_delay;
#if WIRELESS_EXT > 15
	struct iw_spy_data	spy_data;	/* FIXME: needs to be implemented! */
#endif

	/*** Wireless network settings ***/
	/* copy of the device address (ifconfig hw ether) that we actually use
	** for 802.11; copied over from the network device's MAC address
	** (ifconfig) when it makes sense only */
	u8		dev_addr[MAX_ADDR_LEN];
	u8		bssid[ETH_ALEN];	/* the BSSID after having joined */
	u8		ap[ETH_ALEN];		/* The AP we want, FF:FF:FF:FF:FF:FF is any */
	u16		aid;			/* The Association ID sent from the AP / last used AID if we're an AP */
	u16		mode;			/* mode from iwconfig */
	u16		status;			/* 802.11 association status */
	u8		essid_active;		/* specific ESSID active, or select any? */
	u8		essid_len;		/* to avoid dozens of strlen() */
	/* INCLUDES \0 termination for easy printf - but many places
	** simply want the string data memcpy'd plus a length indicator!
	** Keep that in mind... */
	char		essid[IW_ESSID_MAX_SIZE+1];
	/* essid we are going to use for association, in case of "essid 'any'"
	** and in case of hidden ESSID (use configured ESSID then) */
	char		essid_for_assoc[IW_ESSID_MAX_SIZE+1];
	char		nick[IW_ESSID_MAX_SIZE+1]; /* see essid! */
	u8		channel;
	u8		reg_dom_id;		/* reg domain setting */
	u16		reg_dom_chanmask;
	u16		auth_or_assoc_retries;
	u16		scan_retries;
	unsigned long	scan_start;		/* YES, jiffies is defined as "unsigned long" */

	/* stations known to us (if we're an ap) */
	client_t	sta_list[32];		/* tab is larger than list, so that */
	client_t	*sta_hash_tab[64];	/* hash collisions are not likely */
	client_t	*ap_client;		/* this one is our AP (STA mode only) */

	unsigned long	dup_msg_expiry;
	int		dup_count;
	int		nondup_count;
	u16		last_seq_ctrl;		/* duplicate packet detection */

	/* 802.11 power save mode */
	u8		ps_wakeup_cfg;
	u8		ps_listen_interval;
	u8		ps_options;
	u8		ps_hangover_period;
	u32		ps_enhanced_transition_time;
	u32		ps_beacon_rx_time;

	/*** PHY settings ***/
	u8		fallback_threshold;
	u8		stepup_threshold;
	u16		rate_basic;
	u16		rate_oper;
	u16		rate_bcast;
	u16		rate_bcast100;
	u8		rate_auto;		/* false if "iwconfig rate N" (WITHOUT 'auto'!) */
	u8		preamble_mode;		/* 0 == Long Preamble, 1 == Short, 2 == Auto */
	u8		preamble_cur;

	u8		tx_disabled;
	u8		tx_level_dbm;
	/* u8		tx_level_val; */
	/* u8		tx_level_auto;		whether to do automatic power adjustment */

	unsigned long	recalib_time_last_success;
	unsigned long	recalib_time_last_attempt;
	int		recalib_failure_count;
	int		recalib_msg_ratelimit;
	int		retry_errors_msg_ratelimit;

	unsigned long	brange_time_last_state_change;	/* time the power LED was last changed */
	u8		brange_last_state;	/* last state of the LED */
	u8		brange_max_quality;	/* maximum quality that equates to full speed */

	u8		sensitivity;
	u8		antenna;		/* antenna settings */
	u8		ed_threshold;		/* energy detect threshold */
	u8		cca;			/* clear channel assessment */

	u16		rts_threshold;
	u16		frag_threshold;
	u32		short_retry;
	u32		long_retry;
	u16		msdu_lifetime;
	u16		listen_interval;	/* given in units of beacon interval */
	u32		beacon_interval;

	u16		capabilities;
	u8		rate_supported_len;
	u8		rate_supported[13];

	/*** Encryption settings (WEP) ***/
	u32		auth_alg;		/* used in transmit_authen1 */
	u8		wep_enabled;
	u8		wep_restricted;
	u8		wep_current_index;
	wep_key_t	wep_keys[DOT11_MAX_DEFAULT_WEP_KEYS];	/* the default WEP keys */
	key_struct_t	wep_key_struct[10];

	/*** Card Rx/Tx management ***/
	u16		rx_config_1;
	u16		rx_config_2;
	u16		memblocksize;
	int		tx_free;
	int		tx_head;

	/*** Unknown ***/
	u8		dtim_interval;

/*************************************************************************
 *** PCI/USB/... must be last or else hw agnostic code breaks horribly ***
 *************************************************************************/

	/* hack to let common code compile. FIXME */
	dma_addr_t	rxhostdesc_startphy;

	/*** PCI stuff ***/
#ifdef ACX_PCI
	/* pointers to tx buffers, tx host descriptors (in host memory)
	** and tx descs in device memory */
	u8		*txbuf_start;
	txhostdesc_t	*txhostdesc_start;
	txdesc_t	*txdesc_start;	/* points to PCI-mapped memory */
	/* same for rx */
	rxbuffer_t	*rxbuf_start;
	rxhostdesc_t	*rxhostdesc_start;
	rxdesc_t	*rxdesc_start;
	/* physical addresses of above host memory areas */
	dma_addr_t	rxbuf_startphy;
	/* dma_addr_t	rxhostdesc_startphy; */
	dma_addr_t	txbuf_startphy;
	dma_addr_t	txhostdesc_startphy;
	/* sizes of above host memory areas */
	unsigned int	txbuf_area_size;
	unsigned int	txhostdesc_area_size;
	unsigned int	rxbuf_area_size;
	unsigned int	rxhostdesc_area_size;

	unsigned int	txdesc_size;	/* size of txdesc; ACX111 = ACX100 + 4 */
	unsigned int	tx_tail;
	unsigned int	rx_tail;

	client_t	*txc[TX_CNT];
	u16		txr[TX_CNT];

	u8		need_radio_fw;
	u8		irqs_active;	/* whether irq sending is activated */

	const u16	*io;		/* points to ACX100 or ACX111 PCI I/O register address set */

	struct pci_dev	*pdev;

	unsigned long	membase;
	unsigned long	membase2;
	void __iomem	*iobase;
	void __iomem	*iobase2;
	/* command interface */
	u8 __iomem	*cmd_area;
	u8 __iomem	*info_area;

	u16		irq_mask;		/* interrupt types to mask out (not wanted) with many IRQs activated */
	u16		irq_mask_off;		/* interrupt types to mask out (not wanted) with IRQs off */
	unsigned int	irq_loops_this_jiffy;
	unsigned long	irq_last_jiffies;
#endif

	/*** USB stuff ***/
#ifdef ACX_USB
	struct usb_device	*usbdev;

	rxbuffer_t	rxtruncbuf;

	usb_tx_t	*usb_tx;
	usb_rx_t	*usb_rx;

	int		bulkinep;	/* bulk-in endpoint */
	int		bulkoutep;	/* bulk-out endpoint */
	int		rxtruncsize;
#endif

};

/* For use with ACX1xx_IE_RXCONFIG */
/*  bit     description
 *    13   include additional header (length etc.) *required*
 *		struct is defined in 'struct rxbuffer'
 *		is this bit acx100 only? does acx111 always put the header,
 *		and bit setting is irrelevant? --vda
 *    10   receive frames only with SSID used in last join cmd
 *     9   discard broadcast
 *     8   receive packets for multicast address 1
 *     7   receive packets for multicast address 0
 *     6   discard all multicast packets
 *     5   discard frames from foreign BSSID
 *     4   discard frames with foreign destination MAC address
 *     3   promiscuous mode (receive ALL frames, disable filter)
 *     2   include FCS
 *     1   include phy header
 *     0   ???
 */
#define RX_CFG1_INCLUDE_RXBUF_HDR	0x2000 /* ACX100 only */
#define RX_CFG1_FILTER_SSID		0x0400
#define RX_CFG1_FILTER_BCAST		0x0200
#define RX_CFG1_RCV_MC_ADDR1		0x0100
#define RX_CFG1_RCV_MC_ADDR0		0x0080
#define RX_CFG1_FILTER_ALL_MULTI	0x0040
#define RX_CFG1_FILTER_BSSID		0x0020
#define RX_CFG1_FILTER_MAC		0x0010
#define RX_CFG1_RCV_PROMISCUOUS		0x0008
#define RX_CFG1_INCLUDE_FCS		0x0004
#define RX_CFG1_INCLUDE_PHY_HDR		(WANT_PHY_HDR ? 0x0002 : 0)
/*  bit     description
 *    11   receive association requests etc.
 *    10   receive authentication frames
 *     9   receive beacon frames
 *     8   receive contention free packets
 *     7   receive control frames
 *     6   receive data frames
 *     5   receive broken frames
 *     4   receive management frames
 *     3   receive probe requests
 *     2   receive probe responses
 *     1   receive RTS/CTS/ACK frames
 *     0   receive other
 */
#define RX_CFG2_RCV_ASSOC_REQ		0x0800
#define RX_CFG2_RCV_AUTH_FRAMES		0x0400
#define RX_CFG2_RCV_BEACON_FRAMES	0x0200
#define RX_CFG2_RCV_CONTENTION_FREE	0x0100
#define RX_CFG2_RCV_CTRL_FRAMES		0x0080
#define RX_CFG2_RCV_DATA_FRAMES		0x0040
#define RX_CFG2_RCV_BROKEN_FRAMES	0x0020
#define RX_CFG2_RCV_MGMT_FRAMES		0x0010
#define RX_CFG2_RCV_PROBE_REQ		0x0008
#define RX_CFG2_RCV_PROBE_RESP		0x0004
#define RX_CFG2_RCV_ACK_FRAMES		0x0002
#define RX_CFG2_RCV_OTHER		0x0001

/* For use with ACX1xx_IE_FEATURE_CONFIG */
#define FEATURE1_80MHZ_CLOCK	0x00000040L
#define FEATURE1_4X		0x00000020L
#define FEATURE1_LOW_RX		0x00000008L
#define FEATURE1_EXTRA_LOW_RX	0x00000001L

#define FEATURE2_SNIFFER	0x00000080L
#define FEATURE2_NO_TXCRYPT	0x00000001L

/*-- get and set mask values --*/
#define GETSET_LED_POWER	0x00000001L
#define GETSET_STATION_ID	0x00000002L
#define SET_TEMPLATES		0x00000004L
#define SET_STA_LIST		0x00000008L
#define GETSET_TX		0x00000010L
#define GETSET_RX		0x00000020L
#define SET_RXCONFIG		0x00000040L
#define GETSET_ANTENNA		0x00000080L
#define GETSET_SENSITIVITY	0x00000100L
#define GETSET_TXPOWER		0x00000200L
#define GETSET_ED_THRESH	0x00000400L
#define GETSET_CCA		0x00000800L
#define GETSET_POWER_80211	0x00001000L
#define GETSET_RETRY		0x00002000L
#define GETSET_REG_DOMAIN	0x00004000L
#define GETSET_CHANNEL		0x00008000L
/* Used when ESSID changes etc and we need to scan for AP anew */
#define GETSET_RESCAN		0x00010000L
#define GETSET_MODE		0x00020000L
#define GETSET_WEP		0x00040000L
#define SET_WEP_OPTIONS		0x00080000L
#define SET_MSDU_LIFETIME	0x00100000L
#define SET_RATE_FALLBACK	0x00200000L

/* keep in sync with the above */
#define GETSET_ALL	(0 \
/* GETSET_LED_POWER */	| 0x00000001L \
/* GETSET_STATION_ID */	| 0x00000002L \
/* SET_TEMPLATES */	| 0x00000004L \
/* SET_STA_LIST */	| 0x00000008L \
/* GETSET_TX */		| 0x00000010L \
/* GETSET_RX */		| 0x00000020L \
/* SET_RXCONFIG */	| 0x00000040L \
/* GETSET_ANTENNA */	| 0x00000080L \
/* GETSET_SENSITIVITY */| 0x00000100L \
/* GETSET_TXPOWER */	| 0x00000200L \
/* GETSET_ED_THRESH */	| 0x00000400L \
/* GETSET_CCA */	| 0x00000800L \
/* GETSET_POWER_80211 */| 0x00001000L \
/* GETSET_RETRY */	| 0x00002000L \
/* GETSET_REG_DOMAIN */	| 0x00004000L \
/* GETSET_CHANNEL */	| 0x00008000L \
/* GETSET_RESCAN */	| 0x00010000L \
/* GETSET_MODE */	| 0x00020000L \
/* GETSET_WEP */	| 0x00040000L \
/* SET_WEP_OPTIONS */	| 0x00080000L \
/* SET_MSDU_LIFETIME */	| 0x00100000L \
/* SET_RATE_FALLBACK */	| 0x00200000L \
			)


/***********************************************************************
** Firmware loading
*/
#include <linux/firmware.h>	/* request_firmware() */
#include <linux/pci.h>		/* struct pci_device */


/***********************************************************************
*/
typedef struct acx100_ie_memblocksize {
	u16	type ACX_PACKED;
	u16	len ACX_PACKED;
	u16	size ACX_PACKED;
} acx100_ie_memblocksize_t;

typedef struct acx100_ie_queueconfig {
	u16	type ACX_PACKED;
	u16	len ACX_PACKED;
	u32	AreaSize ACX_PACKED;
	u32	RxQueueStart ACX_PACKED;
	u8	QueueOptions ACX_PACKED;
	u8	NumTxQueues ACX_PACKED;
	u8	NumRxDesc ACX_PACKED;	 /* for USB only */
	u8	pad1 ACX_PACKED;
	u32	QueueEnd ACX_PACKED;
	u32	HostQueueEnd ACX_PACKED; /* QueueEnd2 */
	u32	TxQueueStart ACX_PACKED;
	u8	TxQueuePri ACX_PACKED;
	u8	NumTxDesc ACX_PACKED;
	u16	pad2 ACX_PACKED;
} acx100_ie_queueconfig_t;

typedef struct acx111_ie_queueconfig {
	u16	type ACX_PACKED;
	u16	len ACX_PACKED;
	u32	tx_memory_block_address ACX_PACKED;
	u32	rx_memory_block_address ACX_PACKED;
	u32	rx1_queue_address ACX_PACKED;
	u32	reserved1 ACX_PACKED;
	u32	tx1_queue_address ACX_PACKED;
	u8	tx1_attributes ACX_PACKED;
	u16	reserved2 ACX_PACKED;
	u8	reserved3 ACX_PACKED;
} acx111_ie_queueconfig_t;

typedef struct acx100_ie_memconfigoption {
	u16	type ACX_PACKED;
	u16	len ACX_PACKED;
	u32	DMA_config ACX_PACKED;
	acx_ptr	pRxHostDesc ACX_PACKED;
	u32	rx_mem ACX_PACKED;
	u32	tx_mem ACX_PACKED;
	u16	RxBlockNum ACX_PACKED;
	u16	TxBlockNum ACX_PACKED;
} acx100_ie_memconfigoption_t;

typedef struct acx111_ie_memoryconfig {
	u16	type ACX_PACKED;
	u16	len ACX_PACKED;
	u16	no_of_stations ACX_PACKED;
	u16	memory_block_size ACX_PACKED;
	u8	tx_rx_memory_block_allocation ACX_PACKED;
	u8	count_rx_queues ACX_PACKED;
	u8	count_tx_queues ACX_PACKED;
	u8	options ACX_PACKED;
	u8	fragmentation ACX_PACKED;
	u16	reserved1 ACX_PACKED;
	u8	reserved2 ACX_PACKED;

	/* start of rx1 block */
	u8	rx_queue1_count_descs ACX_PACKED;
	u8	rx_queue1_reserved1 ACX_PACKED;
	u8	rx_queue1_type ACX_PACKED; /* must be set to 7 */
	u8	rx_queue1_prio ACX_PACKED; /* must be set to 0 */
	acx_ptr	rx_queue1_host_rx_start ACX_PACKED;
	/* end of rx1 block */

	/* start of tx1 block */
	u8	tx_queue1_count_descs ACX_PACKED;
	u8	tx_queue1_reserved1 ACX_PACKED;
	u8	tx_queue1_reserved2 ACX_PACKED;
	u8	tx_queue1_attributes ACX_PACKED;
	/* end of tx1 block */
} acx111_ie_memoryconfig_t;

typedef struct acx_ie_memmap {
	u16	type ACX_PACKED;
	u16	len ACX_PACKED;
	u32	CodeStart ACX_PACKED;
	u32	CodeEnd ACX_PACKED;
	u32	WEPCacheStart ACX_PACKED;
	u32	WEPCacheEnd ACX_PACKED;
	u32	PacketTemplateStart ACX_PACKED;
	u32	PacketTemplateEnd ACX_PACKED;
	u32	QueueStart ACX_PACKED;
	u32	QueueEnd ACX_PACKED;
	u32	PoolStart ACX_PACKED;
	u32	PoolEnd ACX_PACKED;
} acx_ie_memmap_t;

typedef struct acx111_ie_feature_config {
	u16	type ACX_PACKED;
	u16	len ACX_PACKED;
	u32	feature_options ACX_PACKED;
	u32	data_flow_options ACX_PACKED;
} acx111_ie_feature_config_t;

typedef struct acx111_ie_tx_level {
	u16	type ACX_PACKED;
	u16	len ACX_PACKED;
	u8	level ACX_PACKED;
} acx111_ie_tx_level_t;

#define PS_CFG_ENABLE		0x80
#define PS_CFG_PENDING		0x40 /* status flag when entering PS */
#define PS_CFG_WAKEUP_MODE_MASK	0x07
#define PS_CFG_WAKEUP_BY_HOST	0x03
#define PS_CFG_WAKEUP_EACH_ITVL	0x02
#define PS_CFG_WAKEUP_ON_DTIM	0x01
#define PS_CFG_WAKEUP_ALL_BEAC	0x00

/* Enhanced PS mode: sleep until Rx Beacon w/ the STA's AID bit set
** in the TIM; newer firmwares only(?) */
#define PS_OPT_ENA_ENHANCED_PS	0x04
#define PS_OPT_TX_PSPOLL	0x02 /* send PSPoll frame to fetch waiting frames from AP (on frame with matching AID) */
#define PS_OPT_STILL_RCV_BCASTS	0x01

typedef struct acx100_ie_powermgmt {
	u16	type ACX_PACKED;
	u16	len ACX_PACKED;
	u8	wakeup_cfg ACX_PACKED;
	u8	listen_interval ACX_PACKED; /* for EACH_ITVL: wake up every "beacon units" interval */
	u8	options ACX_PACKED;
	u8	hangover_period ACX_PACKED; /* remaining wake time after Tx MPDU w/ PS bit, in values of 1/1024 seconds */
	u16	enhanced_ps_transition_time ACX_PACKED; /* rem. wake time for Enh. PS */
} acx100_ie_powermgmt_t;

typedef struct acx111_ie_powermgmt {
	u16	type ACX_PACKED;
	u16	len ACX_PACKED;
	u8	wakeup_cfg ACX_PACKED;
	u8	listen_interval ACX_PACKED; /* for EACH_ITVL: wake up every "beacon units" interval */
	u8	options ACX_PACKED;
	u8	hangover_period ACX_PACKED; /* remaining wake time after Tx MPDU w/ PS bit, in values of 1/1024 seconds */
	u32	beacon_rx_time ACX_PACKED;
	u32	enhanced_ps_transition_time ACX_PACKED; /* rem. wake time for Enh. PS */
} acx111_ie_powermgmt_t;


/***********************************************************************
** Commands and template structures
*/

/*
** SCAN command structure
**
** even though acx100 scan rates match RATE100 constants,
** acx111 ones do not match! Therefore we do not use RATE100 #defines */
#define ACX_SCAN_RATE_1		10
#define ACX_SCAN_RATE_2		20
#define ACX_SCAN_RATE_5		55
#define ACX_SCAN_RATE_11	110
#define ACX_SCAN_RATE_22	220
#define ACX_SCAN_RATE_PBCC	0x80	/* OR with this if needed */
#define ACX_SCAN_OPT_ACTIVE	0x00	/* a bit mask */
#define ACX_SCAN_OPT_PASSIVE	0x01
/* Background scan: we go into Power Save mode (by transmitting
** NULL data frame to AP with the power mgmt bit set), do the scan,
** and then exit Power Save mode. A plus is that AP buffers frames
** for us while we do background scan. Thus we avoid frame losses.
** Background scan can be active or passive, just like normal one */
#define ACX_SCAN_OPT_BACKGROUND	0x02
typedef struct acx100_scan {
	u16	count ACX_PACKED;	/* number of scans to do, 0xffff == continuous */
	u16	start_chan ACX_PACKED;
	u16	flags ACX_PACKED;	/* channel list mask; 0x8000 == all channels? */
	u8	max_rate ACX_PACKED;	/* max. probe rate */
	u8	options ACX_PACKED;	/* bit mask, see defines above */
	u16	chan_duration ACX_PACKED;
	u16	max_probe_delay ACX_PACKED;
} acx100_scan_t;			/* length 0xc */

#define ACX111_SCAN_RATE_6	0x0B
#define ACX111_SCAN_RATE_9	0x0F
#define ACX111_SCAN_RATE_12	0x0A
#define ACX111_SCAN_RATE_18	0x0E
#define ACX111_SCAN_RATE_24	0x09
#define ACX111_SCAN_RATE_36	0x0D
#define ACX111_SCAN_RATE_48	0x08
#define ACX111_SCAN_RATE_54	0x0C
#define ACX111_SCAN_OPT_5GHZ    0x04	/* else 2.4GHZ */
#define ACX111_SCAN_MOD_SHORTPRE 0x01	/* you can combine SHORTPRE and PBCC */
#define ACX111_SCAN_MOD_PBCC	0x80
#define ACX111_SCAN_MOD_OFDM	0x40
typedef struct acx111_scan {
	u16	count ACX_PACKED;		/* number of scans to do */
	u8	channel_list_select ACX_PACKED; /* 0: scan all channels, 1: from chan_list only */
	u16	reserved1 ACX_PACKED;
	u8	reserved2 ACX_PACKED;
	u8	rate ACX_PACKED;		/* rate for probe requests (if active scan) */
	u8	options ACX_PACKED;		/* bit mask, see defines above */
	u16	chan_duration ACX_PACKED;	/* min time to wait for reply on one channel (in TU) */
						/* (active scan only) (802.11 section 11.1.3.2.2) */
	u16	max_probe_delay ACX_PACKED;	/* max time to wait for reply on one channel (active scan) */
						/* time to listen on a channel (passive scan) */
	u8	modulation ACX_PACKED;
	u8	channel_list[26] ACX_PACKED;	/* bits 7:0 first byte: channels 8:1 */
						/* bits 7:0 second byte: channels 16:9 */
						/* 26 bytes is enough to cover 802.11a */
} acx111_scan_t;


/*
** Radio calibration command structure
*/
typedef struct acx111_cmd_radiocalib {
/* 0x80000000 == automatic calibration by firmware, according to interval;
 * bits 0..3: select calibration methods to go through:
 * calib based on DC, AfeDC, Tx mismatch, Tx equilization */
	u32	methods ACX_PACKED;
	u32	interval ACX_PACKED;
} acx111_cmd_radiocalib_t;


/*
** Packet template structures
**
** Packet templates store contents of Beacon, Probe response, Probe request,
** Null data frame, and TIM data frame. Firmware automatically transmits
** contents of template at appropriate time:
** - Beacon: when configured as AP or Ad-hoc
** - Probe response: when configured as AP or Ad-hoc, whenever
**   a Probe request frame is received
** - Probe request: when host issues SCAN command (active)
** - Null data frame: when entering 802.11 power save mode
** - TIM data: at the end of Beacon frames (if no TIM template
**   is configured, then transmits default TIM)
** NB:
** - size field must be set to size of actual template
**   (NOT sizeof(struct) - templates are variable in length),
**   size field is not itself counted.
** - members flagged with an asterisk must be initialized with host,
**   rest must be zero filled.
** - variable length fields shown only in comments */
typedef struct acx_template_tim {
	u16	size ACX_PACKED;
	u8	tim_eid ACX_PACKED;	/* 00 1 TIM IE ID * */
	u8	len ACX_PACKED;		/* 01 1 Length * */
	u8	dtim_cnt ACX_PACKED;	/* 02 1 DTIM Count */
	u8	dtim_period ACX_PACKED;	/* 03 1 DTIM Period */
	u8	bitmap_ctrl ACX_PACKED;	/* 04 1 Bitmap Control * (except bit0) */
					/* 05 n Partial Virtual Bitmap * */
	u8	variable[0x100 - 1-1-1-1-1] ACX_PACKED;
} acx_template_tim_t;

typedef struct acx_template_probereq {
	u16	size ACX_PACKED;
	u16	fc ACX_PACKED;		/* 00 2 fc * */
	u16	dur ACX_PACKED;		/* 02 2 Duration */
	u8	da[6] ACX_PACKED;	/* 04 6 Destination Address * */
	u8	sa[6] ACX_PACKED;	/* 0A 6 Source Address * */
	u8	bssid[6] ACX_PACKED;	/* 10 6 BSSID * */
	u16	seq ACX_PACKED;		/* 16 2 Sequence Control */
					/* 18 n SSID * */
					/* nn n Supported Rates * */
	u8	variable[0x44 - 2-2-6-6-6-2] ACX_PACKED;
} acx_template_probereq_t;

typedef struct acx_template_proberesp {
	u16	size ACX_PACKED;
	u16	fc ACX_PACKED;		/* 00 2 fc * (bits [15:12] and [10:8] per 802.11 section 7.1.3.1) */
	u16	dur ACX_PACKED;		/* 02 2 Duration */
	u8	da[6] ACX_PACKED;	/* 04 6 Destination Address */
	u8	sa[6] ACX_PACKED;	/* 0A 6 Source Address */
	u8	bssid[6] ACX_PACKED;	/* 10 6 BSSID */
	u16	seq ACX_PACKED;		/* 16 2 Sequence Control */
	u8	timestamp[8] ACX_PACKED;/* 18 8 Timestamp */
	u16	beacon_interval ACX_PACKED; /* 20 2 Beacon Interval * */
	u16	cap ACX_PACKED;		/* 22 2 Capability Information * */
					/* 24 n SSID * */
					/* nn n Supported Rates * */
					/* nn 1 DS Parameter Set * */
	u8	variable[0x54 - 2-2-6-6-6-2-8-2-2] ACX_PACKED;
} acx_template_proberesp_t;
#define acx_template_beacon_t acx_template_proberesp_t
#define acx_template_beacon acx_template_proberesp

typedef struct acx_template_nullframe {
	u16	size ACX_PACKED;
	struct wlan_hdr_a3 hdr ACX_PACKED;
} acx_template_nullframe_t;


/*
** JOIN command structure
**
** as opposed to acx100, acx111 dtim interval is AFTER rates_basic111.
** NOTE: took me about an hour to get !@#$%^& packing right --> struct packing is eeeeevil... */
typedef struct acx_joinbss {
	u8	bssid[ETH_ALEN] ACX_PACKED;
	u16	beacon_interval ACX_PACKED;
	union {
		struct {
			u8	dtim_interval ACX_PACKED;
			u8	rates_basic ACX_PACKED;
			u8	rates_supported ACX_PACKED;
		} acx100 ACX_PACKED;
		struct {
			u16	rates_basic ACX_PACKED;
			u8	dtim_interval ACX_PACKED;
		} acx111 ACX_PACKED;
	} u ACX_PACKED;
	u8	genfrm_txrate ACX_PACKED;	/* generated frame (bcn, proberesp, RTS, PSpoll) tx rate */
	u8	genfrm_mod_pre ACX_PACKED;	/* generated frame modulation/preamble:
						** bit7: PBCC, bit6: OFDM (else CCK/DQPSK/DBPSK)
						** bit5: short pre */
	u8	macmode ACX_PACKED;	/* BSS Type, must be one of ACX_MODE_xxx */
	u8	channel ACX_PACKED;
	u8	essid_len ACX_PACKED;
	char	essid[IW_ESSID_MAX_SIZE] ACX_PACKED;
} acx_joinbss_t;

#define JOINBSS_RATES_1		0x01
#define JOINBSS_RATES_2		0x02
#define JOINBSS_RATES_5		0x04
#define JOINBSS_RATES_11	0x08
#define JOINBSS_RATES_22	0x10

/* Looks like missing bits are used to indicate 11g rates!
** (it follows from the fact that constants below match 1:1 to RATE111_nn)
** This was actually seen! Look at that Assoc Request sent by acx111,
** it _does_ contain 11g rates in basic set:
01:30:20.070772 Beacon (xxx) [1.0* 2.0* 5.5* 11.0* 6.0* 9.0* 12.0* 18.0* 24.0* 36.0* 48.0* 54.0* Mbit] ESS CH: 1
01:30:20.074425 Authentication (Open System)-1: Succesful
01:30:20.076539 Authentication (Open System)-2:
01:30:20.076620 Acknowledgment
01:30:20.088546 Assoc Request (xxx) [1.0* 2.0* 5.5* 6.0* 9.0* 11.0* 12.0* 18.0* 24.0* 36.0* 48.0* 54.0* Mbit]
01:30:20.122413 Assoc Response AID(1) :: Succesful
01:30:20.122679 Acknowledgment
01:30:20.173204 Beacon (xxx) [1.0* 2.0* 5.5* 11.0* 6.0* 9.0* 12.0* 18.0* 24.0* 36.0* 48.0* 54.0* Mbit] ESS CH: 1
*/
#define JOINBSS_RATES_BASIC111_1	0x0001
#define JOINBSS_RATES_BASIC111_2	0x0002
#define JOINBSS_RATES_BASIC111_5	0x0004
#define JOINBSS_RATES_BASIC111_11	0x0020
#define JOINBSS_RATES_BASIC111_22	0x0100


/***********************************************************************
*/
typedef struct mem_read_write {
	u16	addr ACX_PACKED;
	u16	type ACX_PACKED; /* 0x0 int. RAM / 0xffff MAC reg. / 0x81 PHY RAM / 0x82 PHY reg. */
	u32	len ACX_PACKED;
	u32	data ACX_PACKED;
} mem_read_write_t;

typedef struct firmware_image {
	u32	chksum ACX_PACKED;
	u32	size ACX_PACKED;
	u8	data[1] ACX_PACKED; /* the byte array of the actual firmware... */
} firmware_image_t;

typedef struct acx_cmd_radioinit {
	u32	offset ACX_PACKED;
	u32	len ACX_PACKED;
} acx_cmd_radioinit_t;

typedef struct acx100_ie_wep_options {
	u16	type ACX_PACKED;
	u16	len ACX_PACKED;
	u16	NumKeys ACX_PACKED;	/* max # of keys */
	u8	WEPOption ACX_PACKED;	/* 0 == decrypt default key only, 1 == override decrypt */
	u8	Pad ACX_PACKED;		/* used only for acx111 */
} acx100_ie_wep_options_t;

typedef struct ie_dot11WEPDefaultKey {
	u16	type ACX_PACKED;
	u16	len ACX_PACKED;
	u8	action ACX_PACKED;
	u8	keySize ACX_PACKED;
	u8	defaultKeyNum ACX_PACKED;
	u8	key[29] ACX_PACKED;	/* check this! was Key[19] */
} ie_dot11WEPDefaultKey_t;

typedef struct acx111WEPDefaultKey {
	u8	MacAddr[ETH_ALEN] ACX_PACKED;
	u16	action ACX_PACKED;	/* NOTE: this is a u16, NOT a u8!! */
	u16	reserved ACX_PACKED;
	u8	keySize ACX_PACKED;
	u8	type ACX_PACKED;
	u8	index ACX_PACKED;
	u8	defaultKeyNum ACX_PACKED;
	u8	counter[6] ACX_PACKED;
	u8	key[32] ACX_PACKED;	/* up to 32 bytes (for TKIP!) */
} acx111WEPDefaultKey_t;

typedef struct ie_dot11WEPDefaultKeyID {
	u16	type ACX_PACKED;
	u16	len ACX_PACKED;
	u8	KeyID ACX_PACKED;
} ie_dot11WEPDefaultKeyID_t;

typedef struct acx100_cmd_wep_mgmt {
	u8	MacAddr[ETH_ALEN] ACX_PACKED;
	u16	Action ACX_PACKED;
	u16	KeySize ACX_PACKED;
	u8	Key[29] ACX_PACKED; /* 29*8 == 232bits == WEP256 */
} acx100_cmd_wep_mgmt_t;

/* UNUSED?
typedef struct defaultkey {
	u8	num;
} defaultkey_t;
*/

typedef struct acx_ie_generic {
	u16	type ACX_PACKED;
	u16	len ACX_PACKED;
	union {
		/* struct wep wp ACX_PACKED; */
		/* Association ID IE: just a 16bit value: */
		u16	aid;
		/* UNUSED? struct defaultkey dkey ACX_PACKED; */
		/* generic member for quick implementation of commands */
		u8	bytes[32] ACX_PACKED;
	} m ACX_PACKED;
} acx_ie_generic_t;

/***********************************************************************
*/
#define CHECK_SIZEOF(type,size) { \
	extern void BUG_bad_size_for_##type(void); \
	if (sizeof(type)!=(size)) BUG_bad_size_for_##type(); \
}

static inline void
acx_struct_size_check(void)
{
	CHECK_SIZEOF(txdesc_t, 0x30);
	CHECK_SIZEOF(acx100_ie_memconfigoption_t, 24);
	CHECK_SIZEOF(acx100_ie_queueconfig_t, 0x20);
	CHECK_SIZEOF(acx_joinbss_t, 0x30);
	/* IEs need 4 bytes for (type,len) tuple */
	CHECK_SIZEOF(acx111_ie_configoption_t, ACX111_IE_CONFIG_OPTIONS_LEN + 4);
}


/***********************************************************************
** Global data
*/
extern const u8 acx_bitpos2ratebyte[];
extern const u8 acx_bitpos2rate100[];

extern const u8 acx_reg_domain_ids[];
extern const char * const acx_reg_domain_strings[];
enum {
	acx_reg_domain_ids_len = 8
};

extern const struct iw_handler_def acx_ioctl_handler_def;
