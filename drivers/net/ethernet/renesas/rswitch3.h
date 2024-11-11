/* SPDX-License-Identifier: GPL-2.0 */
/* Renesas Ethernet Switch3 Device Driver
 * This driver is based on the Ethernet Switch2 driver
 * from Linux Kernel v6.11-rc2
 *
 * Copyright (C) 2024 Renesas Electronics Corporation
 */

#ifndef __RSWITCH3_H__
#define __RSWITCH3_H__

#include <linux/platform_device.h>
#include "rcar_gen4_ptp.h"

#define RSWITCH3_MAX_NUM_QUEUES		128

/*
 * Non-MACsec ports: 0 to 7
 * MACsec ports: 8 to 12
 */
#define RSWITCH3_NUM_PORTS	13

#define RSWITCH3_GWCA_IDX_TO_HW_NUM(i)	((i) + RSWITCH3_NUM_PORTS)
#define RSWITCH3_HW_NUM_TO_GWCA_IDX(i)	((i) - RSWITCH3_NUM_PORTS)

#define rswitch_for_each_enabled_port(priv, i)		\
	for (i = 0; i < RSWITCH3_NUM_PORTS; i++)	\
		if (priv->rdev[i]->disabled)		\
			continue;			\
		else

#define rswitch_for_each_enabled_port_continue_reverse(priv, i)	\
	for (; i-- > 0; )					\
		if (priv->rdev[i]->disabled)			\
			continue;				\
		else

#define TX_RING_SIZE		1024
#define RX_RING_SIZE		4096

#define RSWITCH3_MAX_MTU		9600
#define RSWITCH3_HEADROOM	(NET_SKB_PAD + NET_IP_ALIGN)
#define RSWITCH3_DESC_BUF_SIZE	2048
#define RSWITCH3_TAILROOM	SKB_DATA_ALIGN(sizeof(struct skb_shared_info))
#define RSWITCH3_ALIGN		128
#define RSWITCH3_BUF_SIZE	(RSWITCH3_HEADROOM + RSWITCH3_DESC_BUF_SIZE + \
				 RSWITCH3_TAILROOM + RSWITCH3_ALIGN)
#define RSWITCH3_MAP_BUF_SIZE	(RSWITCH3_BUF_SIZE - RSWITCH3_HEADROOM)
#define RSWITCH3_MAX_CTAG_PCP	7

#define RSWITCH3_TIMEOUT_US		10000000

#define RSWITCH3_TOP_OFFSET		0x1b000
#define RSWITCH3_COMA_OFFSET	0x1c000
#define RSWITCH3_ETHA_OFFSET	0x1d000 /* with RMAC */
#define RSWITCH3_ETHA_SIZE		0x02000 /* with RMAC */
#define RSWITCH3_GWCA0_OFFSET	0x37000
#define RSWITCH3_GWCA1_OFFSET	0x39000

/* TODO: hardcoded ETHA/GWCA1 settings for now */
#define GWCA_IRQ_RESOURCE_NAME		"gwca1_gwdis%d"
#define GWCA_IRQ_NAME				"rswitch3: gwca1_gwdis%d"
#define GWCA_NUM_IRQS				1
#define GWCA_INDEX					1
#define AGENT_INDEX_GWCA			14
#define GWCA_IPV_NUM				0
#define GWRO						RSWITCH3_GWCA1_OFFSET

#define FWRO	0
#define TPRO	RSWITCH3_TOP_OFFSET
#define CARO	RSWITCH3_COMA_OFFSET
#define TARO	0
#define RMRO	0x1000
enum rswitch_reg {
	FWGC			= FWRO + 0x0000,
	FWTTC0			= FWRO + 0x0010,
	FWTTC1			= FWRO + 0x0014,
	FWLBMC			= FWRO + 0x0018,
	FWCEPTC			= FWRO + 0x0020,
	FWCEPRC0		= FWRO + 0x0024,
	FWCEPRC1		= FWRO + 0x0028,
	FWCEPRC2		= FWRO + 0x002c,
	FWCLPTC			= FWRO + 0x0030,
	FWCLPRC			= FWRO + 0x0034,
	FWCMPTC			= FWRO + 0x0040,
	FWCMPL23URC		= FWRO + 0x0044,
	FWEMPTC			= FWRO + 0x0048,
	FWEMPL23URC		= FWRO + 0x004c,
	FWSDMPTC		= FWRO + 0x0050,
	FWSDMPVC		= FWRO + 0x0054,
	FWSDMPL23URC	= FWRO + 0x0058,
	FWSMPTC			= FWRO + 0x0060,
	FWSMPVC			= FWRO + 0x0064,
	FWSMPL23URC		= FWRO + 0x0068,
	FWLBWMC0		= FWRO + 0x0080,
	FWIBWMC			= FWRO + 0x00c0,
	FWPC00			= FWRO + 0x0100,
	FWPC10			= FWRO + 0x0104,
	FWPC20			= FWRO + 0x0108,
	FWPC30			= FWRO + 0x010c,
	FWPIFPI00		= FWRO + 0x0200,
	FWCTGC00		= FWRO + 0x0400,
	FWCTGC10		= FWRO + 0x0404,
	FWCTTC00		= FWRO + 0x0408,
	FWCTTC10		= FWRO + 0x040c,
	FWCTTC200		= FWRO + 0x0410,
	FWCTSC00		= FWRO + 0x0450,
	FWCTSC10		= FWRO + 0x0454,
	FWCTSC20		= FWRO + 0x0458,
	FWCTSC30		= FWRO + 0x045c,
	FWCTSC40		= FWRO + 0x0460,
	FWICETC10		= FWRO + 0x0800,
	FWICIP4C0		= FWRO + 0x0804,
	FWIP4AC0		= FWRO + 0x0808,
	FWICIP6C0		= FWRO + 0x080c,
	FWIP6AC00		= FWRO + 0x0810,
	FWIP6AC10		= FWRO + 0x0814,
	FWIP6AC20		= FWRO + 0x0818,
	FWIP6AC30		= FWRO + 0x081c,
	FWIP4APC0		= FWRO + 0x0820,
	FWIP6APC0		= FWRO + 0x0824,
	FWICETC20		= FWRO + 0x1000,
	FWICETC300		= FWRO + 0x1010,
	FWIP4FAC00		= FWRO + 0x1050,
	FWIP4SFC00		= FWRO + 0x1070,
	FWIP6FAC000		= FWRO + 0x1090,
	FWIP6FAC100		= FWRO + 0x10b0,
	FWIP6FAC200		= FWRO + 0x10d0,
	FWIP6FAC300		= FWRO + 0x10f0,
	FWIP6SFC00		= FWRO + 0x1110,
	FWIP4TLCC0		= FWRO + 0x1130,
	FWIP6PLCC0		= FWRO + 0x1134,
	FWICL4C0		= FWRO + 0x1140,
	FWICL4THLC0		= FWRO + 0x1144,
	FWICL4IHTC0		= FWRO + 0x1148,
	FWIP4SC			= FWRO + 0x4008,
	FWIP6SC			= FWRO + 0x4018,
	FWIP6OC			= FWRO + 0x401c,
	FWL2SC			= FWRO + 0x4020,
	FWSFHEC			= FWRO + 0x4030,
	FWSHCR0			= FWRO + 0x4040,
	FWSHCR1			= FWRO + 0x4044,
	FWSHCR2			= FWRO + 0x4048,
	FWSHCR3			= FWRO + 0x404c,
	FWSHCR4			= FWRO + 0x4050,
	FWSHCR5			= FWRO + 0x4054,
	FWSHCR6			= FWRO + 0x4058,
	FWSHCR7			= FWRO + 0x405c,
	FWSHCR8			= FWRO + 0x4060,
	FWSHCR9			= FWRO + 0x4064,
	FWSHCR10		= FWRO + 0x4068,
	FWSHCR11		= FWRO + 0x406c,
	FWSHCR12		= FWRO + 0x4070,
	FWSHCR13		= FWRO + 0x4074,
	FWSHCRR			= FWRO + 0x4078,
	FWLTHTEC0		= FWRO + 0x4090,
	FWLTHTEC1		= FWRO + 0x4094,
	FWLTHTL0		= FWRO + 0x40a0,
	FWLTHTL1		= FWRO + 0x40a4,
	FWLTHTL2		= FWRO + 0x40a8,
	FWLTHTL3		= FWRO + 0x40ac,
	FWLTHTL4		= FWRO + 0x40b0,
	FWLTHTL5		= FWRO + 0x40b4,
	FWLTHTL6		= FWRO + 0x40b8,
	FWLTHTL7		= FWRO + 0x40bc,
	FWLTHTL8		= FWRO + 0x40c0,
	FWLTHTL9		= FWRO + 0x40c4,
	FWLTHTL10		= FWRO + 0x40c8,
	FWLTHTL11		= FWRO + 0x40cc,
	FWLTHTL12		= FWRO + 0x40d0,
	FWLTHTL130		= FWRO + 0x40d4,
	FWLTHTL14		= FWRO + 0x4114,
	FWLTHTL15		= FWRO + 0x4118,
	FWLTHTLR		= FWRO + 0x411c,
	FWLTHTIM		= FWRO + 0x4120,
	FWLTHTEM0		= FWRO + 0x4124,
	FWLTHTEM1		= FWRO + 0x4128,
	FWLTHTS0		= FWRO + 0x4130,
	FWLTHTS1		= FWRO + 0x4134,
	FWLTHTS2		= FWRO + 0x4138,
	FWLTHTS3		= FWRO + 0x413c,
	FWLTHTS4		= FWRO + 0x4140,
	FWLTHTS5		= FWRO + 0x4144,
	FWLTHTS6		= FWRO + 0x4148,
	FWLTHTSR0		= FWRO + 0x4150,
	FWLTHTSR1		= FWRO + 0x4154,
	FWLTHTSR2		= FWRO + 0x4158,
	FWLTHTSR3		= FWRO + 0x415c,
	FWLTHTSR40		= FWRO + 0x4160,
	FWLTHTSR5		= FWRO + 0x41a0,
	FWLTHTSR6		= FWRO + 0x41a4,
	FWLTHTSR7		= FWRO + 0x41a8,
	FWLTHTR			= FWRO + 0x41b0,
	FWLTHTRR0		= FWRO + 0x41b4,
	FWLTHTRR1		= FWRO + 0x41b8,
	FWLTHTRR2		= FWRO + 0x41bc,
	FWLTHTRR3		= FWRO + 0x41c0,
	FWLTHTRR4		= FWRO + 0x41c4,
	FWLTHTRR5		= FWRO + 0x41c8,
	FWLTHTRR6		= FWRO + 0x41cc,
	FWLTHTRR7		= FWRO + 0x41d0,
	FWLTHTRR8		= FWRO + 0x41d4,
	FWLTHTRR9		= FWRO + 0x41d8,
	FWLTHTRR10		= FWRO + 0x41dc,
	FWLTHTRR11		= FWRO + 0x41e0,
	FWLTHTRR12		= FWRO + 0x41e4,
	FWLTHTRR130		= FWRO + 0x41e8,
	FWLTHTRR14		= FWRO + 0x4218,
	FWLTHTRR15		= FWRO + 0x421c,
	FWLTHREUSPC		= FWRO + 0x4300,
	FWLTHREC		= FWRO + 0x4304,
	FWLTHREM		= FWRO + 0x4308,
	FWMACTEC0		= FWRO + 0x4600,
	FWMACTL0		= FWRO + 0x4610,
	FWMACTL1		= FWRO + 0x4614,
	FWMACTL2		= FWRO + 0x4618,
	FWMACTL3		= FWRO + 0x461c,
	FWMACTL4		= FWRO + 0x4620,
	FWMACTL5		= FWRO + 0x4624,
	FWMACTL6		= FWRO + 0x4628,
	FWMACTL70		= FWRO + 0x462c,
	FWMACTL8		= FWRO + 0x466c,
	FWMACTLR		= FWRO + 0x4670,
	FWMACTIM		= FWRO + 0x4680,
	FWMACTEM		= FWRO + 0x4684,
	FWMACTS0		= FWRO + 0x4690,
	FWMACTS1		= FWRO + 0x4693,
	FWMACTS2		= FWRO + 0x4698,
	FWMACTS3		= FWRO + 0x469c,
	FWMACTSR0		= FWRO + 0x46a0,
	FWMACTSR1		= FWRO + 0x46a4,
	FWMACTSR20		= FWRO + 0x46a8,
	FWMACTSR3		= FWRO + 0x46e8,
	FWMACTSR4		= FWRO + 0x46f0,
	FWMACTSR5		= FWRO + 0x46f4,
	FWMACTSR6		= FWRO + 0x46f8,
	FWMACTR			= FWRO + 0x4700,
	FWMACTRR0		= FWRO + 0x4710,
	FWMACTRR1		= FWRO + 0x4714,
	FWMACTRR2		= FWRO + 0x4718,
	FWMACTRR3		= FWRO + 0x471c,
	FWMACTRR4		= FWRO + 0x4720,
	FWMACTRR5		= FWRO + 0x4724,
	FWMACTRR6		= FWRO + 0x4728,
	FWMACTRR70		= FWRO + 0x472c,
	FWMACTRR8		= FWRO + 0x476c,
	FWMACHWLC0		= FWRO + 0x4800,
	FWMACHWLC1		= FWRO + 0x4804,
	FWMACHWLC20		= FWRO + 0x4810,
	FWMACAGUSPC		= FWRO + 0x4880,
	FWMACAGC		= FWRO + 0x4884,
	FWMACAGM0		= FWRO + 0x4888,
	FWMACAGM1		= FWRO + 0x488c,
	FWMACREUSPC		= FWRO + 0x4890,
	FWMACREC		= FWRO + 0x4894,
	FWMACREM		= FWRO + 0x4898,
	FWVLANTEC		= FWRO + 0x4900,
	FWVLANTL0		= FWRO + 0x4910,
	FWVLANTL1		= FWRO + 0x4914,
	FWVLANTL2		= FWRO + 0x4918,
	FWVLANTL3		= FWRO + 0x491c,
	FWVLANTL4		= FWRO + 0x4920,
	FWVLANTL5		= FWRO + 0x4924,
	FWVLANTL60		= FWRO + 0x4928,
	FWVLANTL7		= FWRO + 0x4968,
	FWVLANTLR		= FWRO + 0x496c,
	FWVLANTIM		= FWRO + 0x4970,
	FWVLANTEM		= FWRO + 0x4974,
	FWVLANTS		= FWRO + 0x4980,
	FWVLANTSR0		= FWRO + 0x4984,
	FWVLANTSR1		= FWRO + 0x4988,
	FWVLANTSR2		= FWRO + 0x498c,
	FWVLANTSR3		= FWRO + 0x4990,
	FWVLANTSR4		= FWRO + 0x4994,
	FWVLANTSR50		= FWRO + 0x4998,
	FWVLANTSR6		= FWRO + 0x49c8,
	FWPBFC00		= FWRO + 0x4a00,
	FWPBFC10		= FWRO + 0x4a04,
	FWPBFCSDC00		= FWRO + 0x4b00,
	FWL23URL0		= FWRO + 0x4e00,
	FWL23URL1		= FWRO + 0x4e04,
	FWL23URL2		= FWRO + 0x4e08,
	FWL23URL3		= FWRO + 0x4e0c,
	FWL23URLR		= FWRO + 0x4e10,
	FWL23UTIM		= FWRO + 0x4e20,
	FWL23URR		= FWRO + 0x4e30,
	FWL23URRR0		= FWRO + 0x4e34,
	FWL23URRR1		= FWRO + 0x4e38,
	FWL23URRR2		= FWRO + 0x4e3c,
	FWL23URRR3		= FWRO + 0x4e40,
	FWL23URMC0		= FWRO + 0x4f00,
	FWPMFGC0		= FWRO + 0x5000,
	FWPGFC0			= FWRO + 0x5100,
	FWPGFIGSC0		= FWRO + 0x5104,
	FWPGFENC0		= FWRO + 0x5108,
	FWPGFENM0		= FWRO + 0x510c,
	FWPGFCSTC00		= FWRO + 0x5110,
	FWPGFCSTC10		= FWRO + 0x5114,
	FWPGFCSTM00		= FWRO + 0x5118,
	FWPGFCSTM10		= FWRO + 0x511c,
	FWPGFCTC0		= FWRO + 0x5120,
	FWPGFCTM0		= FWRO + 0x5124,
	FWPGFHCC0		= FWRO + 0x5128,
	FWPGFSM0		= FWRO + 0x512c,
	FWPGFGC0		= FWRO + 0x5130,
	FWPGFGL0		= FWRO + 0x5500,
	FWPGFGL1		= FWRO + 0x5504,
	FWPGFGLR		= FWRO + 0x5508,
	FWPGFGR			= FWRO + 0x5510,
	FWPGFGRR0		= FWRO + 0x5514,
	FWPGFGRR1		= FWRO + 0x5518,
	FWPGFRIM		= FWRO + 0x5520,
	FWPMTRFC0		= FWRO + 0x18000,
	FWPMTRCBSC0		= FWRO + 0x18004,
	FWPMTRCIRC0		= FWRO + 0x18008,
	FWPMTREBSC0		= FWRO + 0x1800c,
	FWPMTREIRC0		= FWRO + 0x18010,
	FWPMTRFM0		= FWRO + 0x18014,
	FWFTL0			= FWRO + 0x6000,
	FWFTL1			= FWRO + 0x6004,
	FWFTLR			= FWRO + 0x6008,
	FWFTOC			= FWRO + 0x6010,
	FWFTOPC			= FWRO + 0x6014,
	FWFTIM			= FWRO + 0x6020,
	FWFTR			= FWRO + 0x6030,
	FWFTRR0			= FWRO + 0x6034,
	FWFTRR1			= FWRO + 0x6038,
	FWFTRR2			= FWRO + 0x603c,
	FWSEQNGC0		= FWRO + 0x6100,
	FWSEQNGM0		= FWRO + 0x6104,
	FWSEQNRC		= FWRO + 0x6200,
	FWCTFDCN0		= FWRO + 0x6300,
	FWLTHFDCN0		= FWRO + 0x6304,
	FWIPFDCN0		= FWRO + 0x6308,
	FWLTWFDCN0		= FWRO + 0x630c,
	FWPBFDCN0		= FWRO + 0x6310,
	FWMHLCN0		= FWRO + 0x6314,
	FWIHLCN0		= FWRO + 0x6318,
	FWICRDCN0		= FWRO + 0x6500,
	FWWMRDCN0		= FWRO + 0x6504,
	FWCTRDCN0		= FWRO + 0x6508,
	FWLTHRDCN0		= FWRO + 0x650c,
	FWIPRDCN0		= FWRO + 0x6510,
	FWLTWRDCN0		= FWRO + 0x6514,
	FWPBRDCN0		= FWRO + 0x6518,
	FWPMFDCN0		= FWRO + 0x6700,
	FWPGFDCN0		= FWRO + 0x6780,
	FWPMGDCN0		= FWRO + 0x19000,
	FWPMYDCN0		= FWRO + 0x19004,
	FWPMRDCN0		= FWRO + 0x19008,
	FWFRPPCN0		= FWRO + 0x6a00,
	FWFRDPCN0		= FWRO + 0x6a04,
	FWBLFCN0		= FWRO + 0x16000,
	FWALFCN0		= FWRO + 0x1600c,
	FWEIS00			= FWRO + 0x7900,
	FWEIE00			= FWRO + 0x7904,
	FWEID00			= FWRO + 0x7908,
	FWEIS1			= FWRO + 0x7a00,
	FWEIE1			= FWRO + 0x7a04,
	FWEID1			= FWRO + 0x7a08,
	FWEIS2			= FWRO + 0x7a10,
	FWEIE2			= FWRO + 0x7a14,
	FWEID2			= FWRO + 0x7a18,
	FWEIS3			= FWRO + 0x7a20,
	FWEIE3			= FWRO + 0x7a24,
	FWEID3			= FWRO + 0x7a28,
	FWEIS4			= FWRO + 0x7a30,
	FWEIE4			= FWRO + 0x7a34,
	FWEID4			= FWRO + 0x7a38,
	FWEIS50			= FWRO + 0x7a40,
	FWEIE50			= FWRO + 0x7a44,
	FWEID50			= FWRO + 0x7a48,
	FWEIS51			= FWRO + 0x7a50,
	FWEIE51			= FWRO + 0x7a54,
	FWEID51			= FWRO + 0x7a58,
	FWEIS52			= FWRO + 0x7a60,
	FWEIE52			= FWRO + 0x7a64,
	FWEID52			= FWRO + 0x7a68,
	FWEIS53			= FWRO + 0x7a70,
	FWEIE53			= FWRO + 0x7a74,
	FWEID53			= FWRO + 0x7a78,
	FWEIS60			= FWRO + 0x7a80,
	FWEIE60			= FWRO + 0x7a84,
	FWEID60			= FWRO + 0x7a88,
	FWEIS61			= FWRO + 0x7a90,
	FWEIE61			= FWRO + 0x7A94,
	FWEID61			= FWRO + 0x7a98,
	FWEIS62			= FWRO + 0x7aa0,
	FWEIE62			= FWRO + 0x7aa4,
	FWEID62			= FWRO + 0x7aa8,
	FWEIS63			= FWRO + 0x7ab0,
	FWEIE63			= FWRO + 0x7ab4,
	FWEID63			= FWRO + 0x7ab8,
	FWEIS70			= FWRO + 0x7ac0,
	FWEIE70			= FWRO + 0x7ac4,
	FWEID70			= FWRO + 0x7ac8,
	FWEIS71			= FWRO + 0x7ad0,
	FWEIE71			= FWRO + 0x7ad4,
	FWEID71			= FWRO + 0x7ad8,
	FWEIS72			= FWRO + 0x7ae0,
	FWEIE72			= FWRO + 0x7ae4,
	FWEID72			= FWRO + 0x7ae8,
	FWEIS73			= FWRO + 0x7af0,
	FWEIE73			= FWRO + 0x7af4,
	FWEID73			= FWRO + 0x7af8,
	FWEIS80			= FWRO + 0x7b00,
	FWEIE80			= FWRO + 0x7b04,
	FWEID80			= FWRO + 0x7b08,
	FWEIS81			= FWRO + 0x7b10,
	FWEIE81			= FWRO + 0x7b14,
	FWEID81			= FWRO + 0x7b18,
	FWEIS82			= FWRO + 0x7b20,
	FWEIE82			= FWRO + 0x7b24,
	FWEID82			= FWRO + 0x7b28,
	FWEIS83			= FWRO + 0x7b30,
	FWEIE83			= FWRO + 0x7b34,
	FWEID83			= FWRO + 0x7b38,
	FWMIS0			= FWRO + 0x7c00,
	FWMIE0			= FWRO + 0x7c04,
	FWMID0			= FWRO + 0x7c08,
	FWSCR0			= FWRO + 0x7d00,
	FWSCR1			= FWRO + 0x7d04,
	FWSCR2			= FWRO + 0x7d08,
	FWSCR3			= FWRO + 0x7d0c,
	FWSCR4			= FWRO + 0x7d10,
	FWSCR21			= FWRO + 0x7d54,
	FWSCR22			= FWRO + 0x7d58,
	FWSCR23			= FWRO + 0x7d5c,
	FWSCR24			= FWRO + 0x7d60,
	FWSCR25			= FWRO + 0x7d64,
	FWSCR26			= FWRO + 0x7d68,
	FWSCR27			= FWRO + 0x7d6c,
	FWSCR28			= FWRO + 0x7d70,
	FWSCR29			= FWRO + 0x7d74,
	FWSCR30			= FWRO + 0x7d78,
	FWSCR31			= FWRO + 0x7d7c,
	FWSCR32			= FWRO + 0x7d80,
	FWSCR33			= FWRO + 0x7d84,
	FWSCR34			= FWRO + 0x7d88,
	FWSCR35			= FWRO + 0x7d8c,
	FWSCR36			= FWRO + 0x7d90,
	FWSCR37			= FWRO + 0x7d94,
	FWSCR38			= FWRO + 0x7d98,
	FWSCR39			= FWRO + 0x7d9c,
	FWSCR40			= FWRO + 0x7da0,
	FWSCR41			= FWRO + 0x7da4,
	FWSCR42			= FWRO + 0x7da8,
	FWSCR43			= FWRO + 0x7dac,
	FWSCR44			= FWRO + 0x7db0,
	FWSCR45			= FWRO + 0x7db4,
	FWSCR46			= FWRO + 0x7db8,
	FWSCR47			= FWRO + 0x7dbc,
	FWSCR48			= FWRO + 0x7dc0,
	FWSCR49			= FWRO + 0x7dc4,
	FWSCRTO0		= FWRO + 0x7e00,
	FWSCRTH0		= FWRO + 0x7e40,
	FWSCRFO0		= FWRO + 0x7e80,
	FWSCRRA0		= FWRO + 0x7ec0,
	FWSCRCA0		= FWRO + 0x7f00,
	FWTWBFC0		= FWRO + 0x9000,
	FWTWBFVC0		= FWRO + 0x8004,
	FWTHBFC0		= FWRO + 0xa000,
	FWTHBFV0Ci		= FWRO + 0xa004,
	FWTHBFV1C0		= FWRO + 0xa008,
	FWFOBFC0		= FWRO + 0xb000,
	FWFOBFV0C0		= FWRO + 0xb004,
	FWFOBFV1C0		= FWRO + 0xb008,
	FWRFC0			= FWRO + 0xd000,
	FWRFSVC0		= FWRO + 0xd004,
	FWRFEVC0		= FWRO + 0xd008,
	FWCFC0			= FWRO + 0xe000,
	FWCFMC00		= FWRO + 0xe004,

	TPEMIMC0		= TPRO + 0x0000,
	TPEMIMC1		= TPRO + 0x0004,
	TPEEMIMC0		= TPRO + 0x0050,
	TPTEMIMC0		= TPRO + 0x0090,
	TPDEMIMC0		= TPRO + 0x0100,
	TSIM			= TPRO + 0x0900,
	TAIM			= TPRO + 0x0904,
	TFIM			= TPRO + 0x0908,
	TCIM			= TPRO + 0x090c,
	TGIM0			= TPRO + 0x0910,
	TEIM0			= TPRO + 0x0950,

	RIPV			= CARO + 0x0000,
	RRC				= CARO + 0x0004,
	RCEC			= CARO + 0x0008,
	RCDC			= CARO + 0x000c,
	RSSIS			= CARO + 0x0010,
	RSSIE			= CARO + 0x0014,
	RSSID			= CARO + 0x0018,
	CABPIBWMC		= CARO + 0x0020,
	CABPWMLC		= CARO + 0x0040,
	CABPPFLC0		= CARO + 0x0050,
	CABPPWMLC0		= CARO + 0x0060,
	CABPPPFLC00		= CARO + 0x00a0,
	CABPULC0		= CARO + 0x0120,
	CABPIRM			= CARO + 0x0160,
	CABPPCM			= CARO + 0x0164,
	CABPLCM			= CARO + 0x0168,
	CABPCPM			= CARO + 0x0180,
	CABPMCPM		= CARO + 0x0200,
	CARDNM			= CARO + 0x0300,
	CARDMNM			= CARO + 0x0304,
	CARDCN			= CARO + 0x0310,
	CAEIS0			= CARO + 0x0400,
	CAEIE0			= CARO + 0x0404,
	CAEID0			= CARO + 0x0408,
	CAEIS1			= CARO + 0x0410,
	CAEIE1			= CARO + 0x0414,
	CAEID1			= CARO + 0x0418,
	CAMIS0			= CARO + 0x0440,
	CAMIE0			= CARO + 0x0444,
	CAMID0			= CARO + 0x0448,
	CAMIS1			= CARO + 0x0450,
	CAMIE1			= CARO + 0x0454,
	CAMID1			= CARO + 0x0458,
	CASCR			= CARO + 0x0480,

	EAMC			= TARO + 0x0000,
	EAMS			= TARO + 0x0004,
	EATDRC			= TARO + 0x0008,
	EAIRC			= TARO + 0x0010,
	EATDQSC			= TARO + 0x0014,
	EATDQC			= TARO + 0x0018,
	EATDQAC			= TARO + 0x001c,
	EATPEC			= TARO + 0x0020,
	EATMFSC0		= TARO + 0x0040,
	EATDQDC0		= TARO + 0x0060,
	EATDQM0			= TARO + 0x0080,
	EATDQMLM0		= TARO + 0x00a0,
	EACTQC			= TARO + 0x0100,
	EACTDQDC		= TARO + 0x0104,
	EACTDQM			= TARO + 0x0108,
	EACTDQMLM		= TARO + 0x010c,
	EAVCC			= TARO + 0x0130,
	EAVTC			= TARO + 0x0134,
	EATTFC			= TARO + 0x0138,
	EACAEC			= TARO + 0x0200,
	EACC			= TARO + 0x0204,
	EACAIVC0		= TARO + 0x0220,
	EACAULC0		= TARO + 0x0240,
	EACOEM			= TARO + 0x0260,
	EACOIVM0		= TARO + 0x0280,
	EACOULM0		= TARO + 0x02a0,
	EACGSM			= TARO + 0x02c0,
	EATASC			= TARO + 0x0300,
	EATASENC0		= TARO + 0x0320,
	EATASCTENC		= TARO + 0x0340,
	EATASENM0		= TARO + 0x0360,
	EATASCTENM		= TARO + 0x0380,
	EATASCSTC0		= TARO + 0x03a0,
	EATASCSTC1		= TARO + 0x03a4,
	EATASCSTM0		= TARO + 0x03a8,
	EATASCSTM1		= TARO + 0x03ac,
	EATASCTC		= TARO + 0x03b0,
	EATASCTM		= TARO + 0x03b4,
	EATASGL0		= TARO + 0x03c0,
	EATASGL1		= TARO + 0x03c4,
	EATASGLR		= TARO + 0x03c8,
	EATASGR			= TARO + 0x03d0,
	EATASGRR		= TARO + 0x03d4,
	EATASHCC		= TARO + 0x03e0,
	EATASRIRM		= TARO + 0x03e4,
	EATASSM			= TARO + 0x03e8,
	EAUSMFSECN		= TARO + 0x0400,
	EATFECN			= TARO + 0x0404,
	EAFSECN			= TARO + 0x0408,
	EADQOECN		= TARO + 0x040c,
	EADQSECN		= TARO + 0x0410,
	EACKSECN		= TARO + 0x0414,
	EALDCN			= TARO + 0x0047,
	EAEIS0			= TARO + 0x0500,
	EAEIE0			= TARO + 0x0504,
	EAEID0			= TARO + 0x0508,
	EAEIS1			= TARO + 0x0510,
	EAEIE1			= TARO + 0x0514,
	EAEID1			= TARO + 0x0518,
	EAEIS2			= TARO + 0x0520,
	EAEIE2			= TARO + 0x0524,
	EAEID2			= TARO + 0x0528,
	EASCR			= TARO + 0x0580,
	EAICD0RC		= TARO + 0x0600,
	EAICD1RC		= TARO + 0x0604,
	EAISD0RC		= TARO + 0x0608,
	EAISD1RC		= TARO + 0x060c,
	EAECD0RC		= TARO + 0x0610,
	EAECD1RC		= TARO + 0x0614,
	EAESD0RC		= TARO + 0x0618,
	EAESD1RC		= TARO + 0x061c,
	EARFCNEO0		= TARO + 0x0700,
	EARFCNEO1		= TARO + 0x0704,
	EARFCNEO2		= TARO + 0x0708,
	EARFCNEO3		= TARO + 0x070c,
	EARFCNEO4		= TARO + 0x0710,
	EARFCNEO5		= TARO + 0x0714,
	EARFCNEO6		= TARO + 0x0718,
	EARFCNPO0		= TARO + 0x071c,
	EARFCNPO1		= TARO + 0x0720,
	EARFCNPO2		= TARO + 0x0724,
	EARFCNPO3		= TARO + 0x0728,
	EARFCNPO4		= TARO + 0x072c,
	EARFCNPO5		= TARO + 0x0730,
	EARFCNPO6		= TARO + 0x0734,
	EADQOECNP0		= TARO + 0x0740,
	EADQOECNCT		= TARO + 0x0760,

	MPSM			= RMRO + 0x0000,
	MPIC			= RMRO + 0x0004,
	MPIM			= RMRO + 0x0008,
	MIOC			= RMRO + 0x0010,
	MIOM			= RMRO + 0x0014,
	MXMS			= RMRO + 0x0018,
	MTFFC			= RMRO + 0x0020,
	MTPFC			= RMRO + 0x0024,
	MTPFC2			= RMRO + 0x0028,
	MTPFC30			= RMRO + 0x0030,
	MTATC0			= RMRO + 0x0050,
	MTIM			= RMRO + 0x0060,
	MRGC			= RMRO + 0x0080,
	MRMAC0			= RMRO + 0x0084,
	MRMAC1			= RMRO + 0x0088,
	MRAFC			= RMRO + 0x008c,
	MRSCE			= RMRO + 0x0090,
	MRSCP			= RMRO + 0x0094,
	MRSCC			= RMRO + 0x0098,
	MRFSCE			= RMRO + 0x009c,
	MRFSCP			= RMRO + 0x00a0,
	MTRC			= RMRO + 0x00a4,
	MRIM			= RMRO + 0x00a8,
	MRPFM			= RMRO + 0x00ac,
	MPFC0			= RMRO + 0x0100,
	MLVC			= RMRO + 0x0180,
	MEEEC			= RMRO + 0x0184,
	MLBC			= RMRO + 0x0188,
	MXGMIIC			= RMRO + 0x0190,
	MPCH			= RMRO + 0x0194,
	MANC			= RMRO + 0x0198,
	MANM			= RMRO + 0x019c,
	MPLCA1			= RMRO + 0x01a0,
	MPLCA2			= RMRO + 0x01a4,
	MPLCA3			= RMRO + 0x01a8,
	MPLCA4			= RMRO + 0x01ac,
	MPLCAM			= RMRO + 0x01b0,
	MHDC1			= RMRO + 0x01c0,
	MHDC2			= RMRO + 0x01c4,
	MEIS			= RMRO + 0x0200,
	MEIE			= RMRO + 0x0204,
	MEID			= RMRO + 0x0208,
	MMIS0			= RMRO + 0x0210,
	MMIE0			= RMRO + 0x0214,
	MMID0			= RMRO + 0x0218,
	MMIS1			= RMRO + 0x0220,
	MMIE1			= RMRO + 0x0224,
	MMID1			= RMRO + 0x0228,
	MMIS2			= RMRO + 0x0230,
	MMIE2			= RMRO + 0x0234,
	MMID2			= RMRO + 0x0238,
	MMPFTCT			= RMRO + 0x0300,
	MAPFTCT			= RMRO + 0x0304,
	MPFRCT			= RMRO + 0x0308,
	MFCICT			= RMRO + 0x030c,
	MEEECT			= RMRO + 0x0310,
	MMPCFTCT0		= RMRO + 0x0320,
	MAPCFTCT0		= RMRO + 0x0330,
	MPCFRCT0		= RMRO + 0x0340,
	MHDCC			= RMRO + 0x0350,
	MROVFC			= RMRO + 0x0354,
	MRHCRCEC		= RMRO + 0x0358,
	MRXBCE			= RMRO + 0x0400,
	MRXBCP			= RMRO + 0x0404,
	MRGFCE			= RMRO + 0x0408,
	MRGFCP			= RMRO + 0x040c,
	MRBFC			= RMRO + 0x0410,
	MRMFC			= RMRO + 0x0414,
	MRUFC			= RMRO + 0x0418,
	MRPEFC			= RMRO + 0x041c,
	MRNEFC			= RMRO + 0x0420,
	MRFMEFC			= RMRO + 0x0424,
	MRFFMEFC		= RMRO + 0x0428,
	MRCFCEFC		= RMRO + 0x042c,
	MRFCEFC			= RMRO + 0x0430,
	MRRCFEFC		= RMRO + 0x0434,
	MRUEFC			= RMRO + 0x043c,
	MROEFC			= RMRO + 0x0440,
	MRBOEC			= RMRO + 0x0444,
	MTXBCE			= RMRO + 0x0500,
	MTXBCP			= RMRO + 0x0504,
	MTGFCE			= RMRO + 0x0508,
	MTGFCP			= RMRO + 0x050c,
	MTBFC			= RMRO + 0x0510,
	MTMFC			= RMRO + 0x0514,
	MTUFC			= RMRO + 0x0518,
	MTEFC			= RMRO + 0x051c,
	MPBLTFCESP0		= RMRO + 0x0530,
	MPBLTFCPSP0		= RMRO + 0x0570,
	MPBLTFCE		= RMRO + 0x05b0,
	MPBLTFCP		= RMRO + 0x05b4,

	GWMC			= GWRO + 0x0000,
	GWMS			= GWRO + 0x0004,
	GWRDRC			= GWRO + 0x0008,
	GWIRC			= GWRO + 0x0010,
	GWRDQSC			= GWRO + 0x0014,
	GWRDQC			= GWRO + 0x0018,
	GWRDQAC			= GWRO + 0x001c,
	GWRGC			= GWRO + 0x0020,
	GWCSDRC			= GWRO + 0x0024,
	GWRMFSC0		= GWRO + 0x0040,
	GWRDQDC0		= GWRO + 0x0060,
	GWRDQM0			= GWRO + 0x0080,
	GWRDQMLM0		= GWRO + 0x00a0,
	GWMTIRM			= GWRO + 0x0100,
	GWMSTLS			= GWRO + 0x0104,
	GWMSTLR			= GWRO + 0x0108,
	GWMSTSS			= GWRO + 0x010c,
	GWMSTSR			= GWRO + 0x0110,
	GWMAC0			= GWRO + 0x0120,
	GWMAC1			= GWRO + 0x0124,
	GWVCC			= GWRO + 0x0130,
	GWVTC			= GWRO + 0x0134,
	GWTTFC			= GWRO + 0x0138,
	GWTDCAC00		= GWRO + 0x0140,
	GWTDCAC10		= GWRO + 0x0144,
	GWTSDCC0		= GWRO + 0x0160,
	GWTNM			= GWRO + 0x0180,
	GWTMNM			= GWRO + 0x0184,
	GWAVTPTM00		= GWRO + 0x01a0,
	GWAVTPTM10		= GWRO + 0x01a4,
	GWGPTPTM00		= GWRO + 0x01a8,
	GWGPTPTM10		= GWRO + 0x01ac,
	GWGPTPTM20		= GWRO + 0x01b0,
	GWAC			= GWRO + 0x01e0,
	GWDCBAC0		= GWRO + 0x01e4,
	GWDCBAC1		= GWRO + 0x01e8,
	GWIICBSC		= GWRO + 0x01ec,
	GWMDNC			= GWRO + 0x01f0,
	GWTRC0			= GWRO + 0x0200,
	GWTPC0			= GWRO + 0x0300,
	GWARIRM			= GWRO + 0x0380,
	GWDCC0			= GWRO + 0x0400,
	GWAARSS			= GWRO + 0x0800,
	GWAARSR0		= GWRO + 0x0804,
	GWAARSR1		= GWRO + 0x0808,
	GWIDAUAS0		= GWRO + 0x0840,
	GWIDASM0		= GWRO + 0x0880,
	GWIDASAM00		= GWRO + 0x0900,
	GWIDASAM10		= GWRO + 0x0904,
	GWIDACAM00		= GWRO + 0x0980,
	GWIDACAM10		= GWRO + 0x0984,
	GWGRLC			= GWRO + 0x0a00,
	GWGRLULC		= GWRO + 0x0a04,
	GWRLIVC0		= GWRO + 0x0a80,
	GWRLULC0		= GWRO + 0x0a84,
	GWIDPC			= GWRO + 0x0b00,
	GWIDC0			= GWRO + 0x0c00,
	GWDIS0			= GWRO + 0x1100,
	GWDIE0			= GWRO + 0x1104,
	GWDID0			= GWRO + 0x1108,
	GWTSDIS			= GWRO + 0x1180,
	GWTSDIE			= GWRO + 0x1184,
	GWTSDID			= GWRO + 0x1188,
	GWEIS0			= GWRO + 0x1190,
	GWEIE0			= GWRO + 0x1194,
	GWEID0			= GWRO + 0x1198,
	GWEIS1			= GWRO + 0x11a0,
	GWEIE1			= GWRO + 0x11a4,
	GWEID1			= GWRO + 0x11a8,
	GWEIS20			= GWRO + 0x1200,
	GWEIE20			= GWRO + 0x1204,
	GWEID20			= GWRO + 0x1208,
	GWEIS3			= GWRO + 0x1280,
	GWEIE3			= GWRO + 0x1284,
	GWEID3			= GWRO + 0x1288,
	GWEIS4			= GWRO + 0x1290,
	GWEIE4			= GWRO + 0x1294,
	GWEID4			= GWRO + 0x1298,
	GWEIS5			= GWRO + 0x12a0,
	GWEIE5			= GWRO + 0x12a4,
	GWEID5			= GWRO + 0x12a8,
	GWSCR0			= GWRO + 0x1800,
	GWSCR1			= GWRO + 0x1900,
	GWICD0RC		= GWRO + 0x1a00,
	GWICD1RC		= GWRO + 0x1a04,
	GWISD0RC		= GWRO + 0x1a08,
	GWISD1RC		= GWRO + 0x1a0c,
	GWECD0RC		= GWRO + 0x1a10,
	GWECD1RC		= GWRO + 0x1a14,
	GWESD0RC		= GWRO + 0x1a18,
	GWESD1RC		= GWRO + 0x1a1c,
};

/* ETHA/RMAC */
enum rswitch_etha_mode {
	EAMC_OPC_RESET,
	EAMC_OPC_DISABLE,
	EAMC_OPC_CONFIG,
	EAMC_OPC_OPERATION,
};

#define EAMS_OPS_MASK		EAMC_OPC_OPERATION

#define EAVCC_VEM_SC_TAG	(0x3 << 16)

#define MPIC_PIS_MII		0x00
#define MPIC_PIS_GMII		0x02
#define MPIC_PIS_XGMII		0x04
#define MPIC_LSC_SHIFT		3
#define MPIC_LSC_100M		(1 << MPIC_LSC_SHIFT)
#define MPIC_LSC_1G			(2 << MPIC_LSC_SHIFT)
#define MPIC_LSC_2_5G		(3 << MPIC_LSC_SHIFT)
#define MPIC_LSC_5G			(4 << MPIC_LSC_SHIFT)
#define MPIC_LSC_10G		(5 << MPIC_LSC_SHIFT)

#define MDIO_READ_C45		0x03
#define MDIO_WRITE_C45		0x01
#define MDIO_READ_C22		0x02
#define MDIO_WRITE_C22		0x01

#define MPSM_PSME			BIT(0)
#define MPSM_MFF_C45		BIT(2)
#define MPSM_PDA_SHIFT		3
#define MPSM_PDA_MASK		GENMASK(7, MPSM_PDA_SHIFT)
#define MPSM_PDA(val)		((val) << MPSM_PDA_SHIFT)
#define MPSM_PRA_SHIFT		8
#define MPSM_PRA_MASK		GENMASK(12, MPSM_PRA_SHIFT)
#define MPSM_PRA(val)		((val) << MPSM_PRA_SHIFT)
#define MPSM_POP_SHIFT		13
#define MPSM_POP_MASK		GENMASK(14, MPSM_POP_SHIFT)
#define MPSM_POP(val)		((val) << MPSM_POP_SHIFT)
#define MPSM_PRD_SHIFT		16
#define MPSM_PRD_MASK		GENMASK(31, MPSM_PRD_SHIFT)
#define MPSM_PRD_WRITE(val)	((val) << MPSM_PRD_SHIFT)
#define MPSM_PRD_READ(val)	((val) & MPSM_PRD_MASK >> MPSM_PRD_SHIFT)

/* Completion flags */
#define MMIS1_PAACS			BIT(2) /* Address */
#define MMIS1_PWACS			BIT(1) /* Write */
#define MMIS1_PRACS			BIT(0) /* Read */
#define MMIS1_CLEAR_FLAGS		0xf

#define MPIC_PSMCS_SHIFT	16
#define MPIC_PSMCS_MASK		GENMASK(22, MPIC_PSMCS_SHIFT)
#define MPIC_PSMCS(val)		((val) << MPIC_PSMCS_SHIFT)

#define MPIC_PSMHT_SHIFT	24
#define MPIC_PSMHT_MASK		GENMASK(26, MPIC_PSMHT_SHIFT)
#define MPIC_PSMHT(val)		((val) << MPIC_PSMHT_SHIFT)

#define MLVC_PLV		BIT(16)

/* GWCA */
enum rswitch_gwca_mode {
	GWMC_OPC_RESET,
	GWMC_OPC_DISABLE,
	GWMC_OPC_CONFIG,
	GWMC_OPC_OPERATION,
};

#define GWMS_OPS_MASK		GWMC_OPC_OPERATION

#define GWMTIRM_MTIOG		BIT(0)
#define GWMTIRM_MTR			BIT(1)

#define GWVCC_VEM_SC_TAG	(0x3 << 16)

#define GWARIRM_ARIOG		BIT(0)
#define GWARIRM_ARR			BIT(1)

#define GWMDNC_TSDMN(num)	(((num) << 16) & GENMASK(17, 16))
#define GWMDNC_TXDMN(num)	(((num) << 8) & GENMASK(12, 8))
#define GWMDNC_RXDMN(num)	((num) & GENMASK(4, 0))

#define GWDCC_BALR			BIT(24)
#define GWDCC_DCP_MASK		GENMASK(18, 16)
#define GWDCC_DCP(prio)		FIELD_PREP(GWDCC_DCP_MASK, (prio))
#define GWDCC_DQT			BIT(11)
#define GWDCC_ETS			BIT(9)
#define GWDCC_EDE			BIT(8)

#define GWTRC(queue)		(GWTRC0 + (queue) / 32 * 4)
#define GWTPC_PPPL(ipv)		BIT(ipv)
#define GWDCC_OFFS(queue)	(GWDCC0 + (queue) * 4)

#define GWDIS(i)			(GWDIS0 + (i) * 0x10)
#define GWDIE(i)			(GWDIE0 + (i) * 0x10)
#define GWDID(i)			(GWDID0 + (i) * 0x10)

/* COMA */
#define RRC_RR				BIT(0)
#define RRC_RR_CLR			0
#define	RCEC_ACE_DEFAULT	(BIT(0) | BIT(AGENT_INDEX_GWCA))
#define RCEC_RCE			BIT(16)
#define RCDC_RCD			BIT(16)

#define CABPIRM_BPIOG		BIT(0)
#define CABPIRM_BPR			BIT(1)

#define CABPPFLC_INIT_VALUE	0x00800080

/* MFWD */
#define FWPC0_LTHTA			BIT(0)
#define FWPC0_IP4UE			BIT(3)
#define FWPC0_IP4TE			BIT(4)
#define FWPC0_IP4OE			BIT(5)
#define FWPC0_L2SE			BIT(9)
#define FWPC0_IP4EA			BIT(10)
#define FWPC0_IPDSA			BIT(12)
#define FWPC0_IPHLA			BIT(18)
#define FWPC0_MACSDA		BIT(20)
#define FWPC0_MACHLA		BIT(26)
#define FWPC0_MACHMA		BIT(27)
#define FWPC0_VLANSA		BIT(28)

#define FWPC0(i)			(FWPC00 + (i) * 0x10)
#define FWPC0_DEFAULT		(FWPC0_LTHTA | FWPC0_IP4UE | FWPC0_IP4TE | \
				 FWPC0_IP4OE | FWPC0_L2SE | FWPC0_IP4EA | \
				 FWPC0_IPDSA | FWPC0_IPHLA | FWPC0_MACSDA | \
				 FWPC0_MACHLA |	FWPC0_MACHMA | FWPC0_VLANSA)
#define FWPC1(i)			(FWPC10 + (i) * 0x10)
#define FWPC1_DDE			BIT(0)

#define	FWPBFC(i)			(FWPBFC00 + (i) * 0x10)

#define FWPBFCSDC(j, i)		(FWPBFCSDC00 + (i) * 0x20 + (j) * 0x04)

/* TOP */
#define  TPDEMIMC0(queue)		(TPDEMIMC0 + (queue) * 4)

/* Descriptors */
enum RX_DS_CC_BIT {
	RX_DS			= 0x0fff, /* Data size */
	RX_TR			= 0x1000, /* Truncation indication */
	RX_EI			= 0x2000, /* Error indication */
	RX_PS			= 0xc000, /* Padding selection */
};

enum TX_DS_TAGL_BIT {
	TX_DS			= 0x0fff, /* Data size */
	TX_TAGL			= 0xf000, /* Frame tag LSBs */
};

enum DIE_DT {
	/* Frame data */
	DT_FSINGLE		= 0x80,
	DT_FSTART		= 0x90,
	DT_FMID			= 0xa0,
	DT_FEND			= 0xb0,

	/* Chain control */
	DT_LEMPTY		= 0xc0,
	DT_EEMPTY		= 0xd0,
	DT_LINKFIX		= 0x00,
	DT_LINK			= 0xe0,
	DT_EOS			= 0xf0,
	/* HW/SW arbitration */
	DT_FEMPTY		= 0x40,
	DT_FEMPTY_IS	= 0x10,
	DT_FEMPTY_IC	= 0x20,
	DT_FEMPTY_ND	= 0x30,
	DT_FEMPTY_START	= 0x50,
	DT_FEMPTY_MID	= 0x60,
	DT_FEMPTY_END	= 0x70,

	DT_MASK			= 0xf0,
	DIE				= 0x08,	/* Descriptor Interrupt Enable */
};

/* Both transmission and reception */
#define INFO1_FMT		BIT(2)
#define INFO1_TXC		BIT(3)

/* For transmission */
#define INFO1_TSUN(val)		((u64)(val) << 8ULL)
#define INFO1_IPV(prio)		((u64)(prio) << 28ULL)
#define INFO1_CSD0(index)	((u64)(index) << 32ULL)
#define INFO1_CSD1(index)	((u64)(index) << 40ULL)
#define INFO1_DV(port_vector)	((u64)(port_vector) << 48ULL)

/* For reception */
#define INFO1_SPN(port)		((u64)(port) << 36ULL)

struct rswitch_desc {
	__le16 info_ds;		/* Descriptor size */
	u8 die_dt;			/* Descriptor interrupt enable and type */
	__u8  dptrh;		/* Descriptor pointer MSB */
	__le32 dptrl;		/* Descriptor pointer LSW */
} __packed;

struct rswitch_ts_desc {
	struct rswitch_desc desc;
	__le32 ts_nsec;
	__le32 ts_sec;
} __packed;

struct rswitch_ext_desc {
	struct rswitch_desc desc;
	__le64 info1;
} __packed;

struct rswitch_ext_ts_desc {
	struct rswitch_desc desc;
	__le64 info1;
	__le32 ts_nsec;
	__le32 ts_sec;
} __packed;

struct rswitch_etha {
	unsigned int index;
	void __iomem *addr;
	void __iomem *coma_addr;
	bool external_phy;
	struct mii_bus *mii;
	phy_interface_t phy_interface;
	u32 psmcs;
	u8 mac_addr[MAX_ADDR_LEN];
	int link;
	int speed;

	/* This hardware could not be initialized twice so that marked
	 * this flag to avoid multiple initialization.
	 */
	bool operated;
};

/* The datasheet said descriptor "chain" and/or "queue". For consistency of
 * name, this driver calls "queue".
 */
struct rswitch_gwca_queue {
	int index;
	bool dir_tx;
	union {
		struct rswitch_ext_desc *tx_ring;
		struct rswitch_ext_ts_desc *rx_ring;
	};

	dma_addr_t ring_dma;
	unsigned int ring_size;
	unsigned int cur;
	unsigned int dirty;

	struct net_device *ndev;	/* queue to ndev for irq */

	union {
		/* For TX */
		struct {
			struct sk_buff **skbs;
			dma_addr_t *unmap_addrs;
		};
		/* For RX */
		struct {
			void **rx_bufs;
			struct sk_buff *skb_fstart;
			u16 pkt_len;
		};
	};
};

#define RSWITCH3_NUM_IRQ_REGS	(RSWITCH3_MAX_NUM_QUEUES / BITS_PER_TYPE(u32))
struct rswitch_gwca {
	unsigned int index;
	struct rswitch_desc *linkfix_table;
	dma_addr_t linkfix_table_dma;
	u32 linkfix_table_size;
	struct rswitch_gwca_queue *queues;
	int num_queues;
	DECLARE_BITMAP(used, RSWITCH3_MAX_NUM_QUEUES);
	u32 tx_irq_bits[RSWITCH3_NUM_IRQ_REGS];
	u32 rx_irq_bits[RSWITCH3_NUM_IRQ_REGS];
	int speed;
};

#define NUM_QUEUES_PER_NDEV	2
struct rswitch_device {
	struct rswitch_private *priv;
	struct net_device *ndev;
	struct napi_struct napi;
	void __iomem *addr;
	struct rswitch_gwca_queue *tx_queue;
	struct rswitch_gwca_queue *rx_queue;
	u8 ts_tag;
	bool disabled;

	int port;
	struct rswitch_etha *etha;
	struct device_node *np_port;
	struct phy *serdes;
};

struct rswitch_mfwd_mac_table_entry {
	int queue_index;
	unsigned char addr[MAX_ADDR_LEN];
};

struct rswitch_mfwd {
	struct rswitch_mac_table_entry *mac_table_entries;
	int num_mac_table_entries;
};

struct rswitch_private {
	struct platform_device *pdev;
	void __iomem *addr;
	struct rcar_gen4_ptp_private *ptp_priv;

	struct rswitch_device *rdev[RSWITCH3_NUM_PORTS];
	DECLARE_BITMAP(opened_ports, RSWITCH3_NUM_PORTS);

	struct rswitch_gwca gwca;
	struct rswitch_etha etha[RSWITCH3_NUM_PORTS];
	struct rswitch_mfwd mfwd;

	spinlock_t lock;	/* lock interrupt registers' control */
	struct clk *clk;

	bool etha_no_runtime_change;
	bool gwca_halt;

	/* Parameter for VPF environment which can config in dts file */
	bool	vpf_mode;
};

#endif	/* #ifndef __RSWITCH3_H__ */
