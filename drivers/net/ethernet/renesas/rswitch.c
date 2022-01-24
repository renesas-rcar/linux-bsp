// SPDX-License-Identifier: GPL-2.0
/* Renesas Ethernet Switch device driver
 *
 * Copyright (C) 2020 Renesas Electronics Corporation
 */

#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/clk.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

static void *debug_addr;
static inline u32 rs_read32(void *addr)
{
	return ioread32(addr);
}

static inline void rs_write32(u32 data, void *addr)
{
	iowrite32(data, addr);
}

#define RSWITCH_NUM_HW		5
#define RSWITCH_MAX_NUM_ETHA	3
#define RSWITCH_MAX_NUM_CHAINS	128

#define TX_RING_SIZE		1024
#define RX_RING_SIZE		1024

#define PKT_BUF_SZ		1584
#define RSWITCH_ALIGN		128
#define RSWITCH_MAX_CTAG_PCP	7

#define RSWITCH_COMA_OFFSET	0x00009000
#define RSWITCH_ETHA_OFFSET	0x0000a000	/* with RMAC */
#define RSWITCH_ETHA_SIZE	0x00002000	/* with RMAC */
#define RSWITCH_GWCA0_OFFSET	0x00010000
#define RSWITCH_GWCA1_OFFSET	0x00012000

#define FWRO	0
#define CARO	RSWITCH_COMA_OFFSET
//#define GWRO	RSWITCH_GWCA1_OFFSET
#define GWRO	RSWITCH_GWCA0_OFFSET
#define TARO	0
#define RMRO	0x1000
enum rswitch_reg {
	FWGC		= FWRO + 0x0000,
	FWTTC0		= FWRO + 0x0010,
	FWTTC1		= FWRO + 0x0014,
	FWLBMC		= FWRO + 0x0018,
	FWCEPTC		= FWRO + 0x020,
	FWCEPRC0	= FWRO + 0x024,
	FWCEPRC1	= FWRO + 0x028,
	FWCEPRC2	= FWRO + 0x02C,
	FWCLPTC		= FWRO + 0x030,
	FWCLPRC		= FWRO + 0x034,
	FWCMPTC		= FWRO + 0x040,
	FWEMPTC		= FWRO + 0x044,
	FWSDMPTC	= FWRO + 0x050,
	FWSDMPVC	= FWRO + 0x054,
	FWLBWMC0	= FWRO + 0x080,
	FWPC00		= FWRO + 0x100,
	FWPC10		= FWRO + 0x104,
	FWPC20		= FWRO + 0x108,
	FWCTGC00	= FWRO + 0x400,
	FWCTGC10	= FWRO + 0x404,
	FWCTTC00	= FWRO + 0x408,
	FWCTTC10	= FWRO + 0x40C,
	FWCTTC200	= FWRO + 0x410,
	FWCTSC00	= FWRO + 0x420,
	FWCTSC10	= FWRO + 0x424,
	FWCTSC20	= FWRO + 0x428,
	FWCTSC30	= FWRO + 0x42C,
	FWCTSC40	= FWRO + 0x430,
	FWTWBFC0	= FWRO + 0x1000,
	FWTWBFVC0	= FWRO + 0x1004,
	FWTHBFC0	= FWRO + 0x1400,
	FWTHBFV0C0	= FWRO + 0x1404,
	FWTHBFV1C0	= FWRO + 0x1408,
	FWFOBFC0	= FWRO + 0x1800,
	FWFOBFV0C0	= FWRO + 0x1804,
	FWFOBFV1C0	= FWRO + 0x1808,
	FWRFC0		= FWRO + 0x1C00,
	FWRFVC0		= FWRO + 0x1C04,
	FWCFC0		= FWRO + 0x2000,
	FWCFMC00	= FWRO + 0x2004,
	FWIP4SC		= FWRO + 0x4008,
	FWIP6SC		= FWRO + 0x4018,
	FWIP6OC		= FWRO + 0x401C,
	FWL2SC		= FWRO + 0x4020,
	FWSFHEC		= FWRO + 0x4030,
	FWSHCR0		= FWRO + 0x4040,
	FWSHCR1		= FWRO + 0x4044,
	FWSHCR2		= FWRO + 0x4048,
	FWSHCR3		= FWRO + 0x404C,
	FWSHCR4		= FWRO + 0x4050,
	FWSHCR5		= FWRO + 0x4054,
	FWSHCR6		= FWRO + 0x4058,
	FWSHCR7		= FWRO + 0x405C,
	FWSHCR8		= FWRO + 0x4060,
	FWSHCR9		= FWRO + 0x4064,
	FWSHCR10	= FWRO + 0x4068,
	FWSHCR11	= FWRO + 0x406C,
	FWSHCR12	= FWRO + 0x4070,
	FWSHCR13	= FWRO + 0x4074,
	FWSHCRR		= FWRO + 0x4078,
	FWLTHHEC	= FWRO + 0x4090,
	FWLTHHC		= FWRO + 0x4094,
	FWLTHTL0	= FWRO + 0x40A0,
	FWLTHTL1	= FWRO + 0x40A4,
	FWLTHTL2	= FWRO + 0x40A8,
	FWLTHTL3	= FWRO + 0x40AC,
	FWLTHTL4	= FWRO + 0x40B0,
	FWLTHTL5	= FWRO + 0x40B4,
	FWLTHTL6	= FWRO + 0x40B8,
	FWLTHTL7	= FWRO + 0x40BC,
	FWLTHTL80	= FWRO + 0x40C0,
	FWLTHTL9	= FWRO + 0x40D0,
	FWLTHTLR	= FWRO + 0x40D4,
	FWLTHTIM	= FWRO + 0x40E0,
	FWLTHTEM	= FWRO + 0x40E4,
	FWLTHTS0	= FWRO + 0x4100,
	FWLTHTS1	= FWRO + 0x4104,
	FWLTHTS2	= FWRO + 0x4108,
	FWLTHTS3	= FWRO + 0x410C,
	FWLTHTS4	= FWRO + 0x4110,
	FWLTHTSR0	= FWRO + 0x4120,
	FWLTHTSR1	= FWRO + 0x4124,
	FWLTHTSR2	= FWRO + 0x4128,
	FWLTHTSR3	= FWRO + 0x412C,
	FWLTHTSR40	= FWRO + 0x4130,
	FWLTHTSR5	= FWRO + 0x4140,
	FWLTHTR		= FWRO + 0x4150,
	FWLTHTRR0	= FWRO + 0x4154,
	FWLTHTRR1	= FWRO + 0x4158,
	FWLTHTRR2	= FWRO + 0x415C,
	FWLTHTRR3	= FWRO + 0x4160,
	FWLTHTRR4	= FWRO + 0x4164,
	FWLTHTRR5	= FWRO + 0x4168,
	FWLTHTRR6	= FWRO + 0x416C,
	FWLTHTRR7	= FWRO + 0x4170,
	FWLTHTRR8	= FWRO + 0x4174,
	FWLTHTRR9	= FWRO + 0x4180,
	FWLTHTRR10	= FWRO + 0x4190,
	FWIPHEC		= FWRO + 0x4214,
	FWIPHC		= FWRO + 0x4218,
	FWIPTL0		= FWRO + 0x4220,
	FWIPTL1		= FWRO + 0x4224,
	FWIPTL2		= FWRO + 0x4228,
	FWIPTL3		= FWRO + 0x422C,
	FWIPTL4		= FWRO + 0x4230,
	FWIPTL5		= FWRO + 0x4234,
	FWIPTL6		= FWRO + 0x4238,
	FWIPTL7		= FWRO + 0x4240,
	FWIPTL8		= FWRO + 0x4250,
	FWIPTLR		= FWRO + 0x4254,
	FWIPTIM		= FWRO + 0x4260,
	FWIPTEM		= FWRO + 0x4264,
	FWIPTS0		= FWRO + 0x4270,
	FWIPTS1		= FWRO + 0x4274,
	FWIPTS2		= FWRO + 0x4278,
	FWIPTS3		= FWRO + 0x427C,
	FWIPTS4		= FWRO + 0x4280,
	FWIPTSR0	= FWRO + 0x4284,
	FWIPTSR1	= FWRO + 0x4288,
	FWIPTSR2	= FWRO + 0x428C,
	FWIPTSR3	= FWRO + 0x4290,
	FWIPTSR4	= FWRO + 0x42A0,
	FWIPTR		= FWRO + 0x42B0,
	FWIPTRR0	= FWRO + 0x42B4,
	FWIPTRR1	= FWRO + 0x42B8,
	FWIPTRR2	= FWRO + 0x42BC,
	FWIPTRR3	= FWRO + 0x42C0,
	FWIPTRR4	= FWRO + 0x42C4,
	FWIPTRR5	= FWRO + 0x42C8,
	FWIPTRR6	= FWRO + 0x42CC,
	FWIPTRR7	= FWRO + 0x42D0,
	FWIPTRR8	= FWRO + 0x42E0,
	FWIPTRR9	= FWRO + 0x42F0,
	FWIPHLEC	= FWRO + 0x4300,
	FWIPAGUSPC	= FWRO + 0x4500,
	FWIPAGC		= FWRO + 0x4504,
	FWIPAGM0	= FWRO + 0x4510,
	FWIPAGM1	= FWRO + 0x4514,
	FWIPAGM2	= FWRO + 0x4518,
	FWIPAGM3	= FWRO + 0x451C,
	FWIPAGM4	= FWRO + 0x4520,
	FWMACHEC	= FWRO + 0x4620,
	FWMACHC		= FWRO + 0x4624,
	FWMACTL0	= FWRO + 0x4630,
	FWMACTL1	= FWRO + 0x4634,
	FWMACTL2	= FWRO + 0x4638,
	FWMACTL3	= FWRO + 0x463C,
	FWMACTL4	= FWRO + 0x4640,
	FWMACTL5	= FWRO + 0x4650,
	FWMACTLR	= FWRO + 0x4654,
	FWMACTIM	= FWRO + 0x4660,
	FWMACTEM	= FWRO + 0x4664,
	FWMACTS0	= FWRO + 0x4670,
	FWMACTS1	= FWRO + 0x4674,
	FWMACTSR0	= FWRO + 0x4678,
	FWMACTSR1	= FWRO + 0x467C,
	FWMACTSR2	= FWRO + 0x4680,
	FWMACTSR3	= FWRO + 0x4690,
	FWMACTR		= FWRO + 0x46A0,
	FWMACTRR0	= FWRO + 0x46A4,
	FWMACTRR1	= FWRO + 0x46A8,
	FWMACTRR2	= FWRO + 0x46AC,
	FWMACTRR3	= FWRO + 0x46B0,
	FWMACTRR4	= FWRO + 0x46B4,
	FWMACTRR5	= FWRO + 0x46C0,
	FWMACTRR6	= FWRO + 0x46D0,
	FWMACHLEC	= FWRO + 0x4700,
	FWMACAGUSPC	= FWRO + 0x4880,
	FWMACAGC	= FWRO + 0x4884,
	FWMACAGM0	= FWRO + 0x4888,
	FWMACAGM1	= FWRO + 0x488C,
	FWVLANTEC	= FWRO + 0x4900,
	FWVLANTL0	= FWRO + 0x4910,
	FWVLANTL1	= FWRO + 0x4914,
	FWVLANTL2	= FWRO + 0x4918,
	FWVLANTL3	= FWRO + 0x4920,
	FWVLANTL4	= FWRO + 0x4930,
	FWVLANTLR	= FWRO + 0x4934,
	FWVLANTIM	= FWRO + 0x4940,
	FWVLANTEM	= FWRO + 0x4944,
	FWVLANTS	= FWRO + 0x4950,
	FWVLANTSR0	= FWRO + 0x4954,
	FWVLANTSR1	= FWRO + 0x4958,
	FWVLANTSR2	= FWRO + 0x4960,
	FWVLANTSR3	= FWRO + 0x4970,
	FWPBFCi		= FWRO + 0x4A00,
	FWPBFCSDC00	= FWRO + 0x4A04,
	FWL23URL0	= FWRO + 0x4E00,
	FWL23URL1	= FWRO + 0x4E04,
	FWL23URL2	= FWRO + 0x4E08,
	FWL23URL3	= FWRO + 0x4E0C,
	FWL23URLR	= FWRO + 0x4E10,
	FWL23UTIM	= FWRO + 0x4E20,
	FWL23URR	= FWRO + 0x4E30,
	FWL23URRR0	= FWRO + 0x4E34,
	FWL23URRR1	= FWRO + 0x4E38,
	FWL23URRR2	= FWRO + 0x4E3C,
	FWL23URRR3	= FWRO + 0x4E40,
	FWL23URMC0	= FWRO + 0x4F00,
	FWPMFGC0	= FWRO + 0x5000,
	FWPGFC0		= FWRO + 0x5100,
	FWPGFIGSC0	= FWRO + 0x5104,
	FWPGFENC0	= FWRO + 0x5108,
	FWPGFENM0	= FWRO + 0x510c,
	FWPGFCSTC00	= FWRO + 0x5110,
	FWPGFCSTC10	= FWRO + 0x5114,
	FWPGFCSTM00	= FWRO + 0x5118,
	FWPGFCSTM10	= FWRO + 0x511C,
	FWPGFCTC0	= FWRO + 0x5120,
	FWPGFCTM0	= FWRO + 0x5124,
	FWPGFHCC0	= FWRO + 0x5128,
	FWPGFSM0	= FWRO + 0x512C,
	FWPGFGC0	= FWRO + 0x5130,
	FWPGFGL0	= FWRO + 0x5500,
	FWPGFGL1	= FWRO + 0x5504,
	FWPGFGLR	= FWRO + 0x5518,
	FWPGFGR		= FWRO + 0x5510,
	FWPGFGRR0	= FWRO + 0x5514,
	FWPGFGRR1	= FWRO + 0x5518,
	FWPGFRIM	= FWRO + 0x5520,
	FWPMTRFC0	= FWRO + 0x5600,
	FWPMTRCBSC0	= FWRO + 0x5604,
	FWPMTRC0RC0	= FWRO + 0x5608,
	FWPMTREBSC0	= FWRO + 0x560C,
	FWPMTREIRC0	= FWRO + 0x5610,
	FWPMTRFM0	= FWRO + 0x5614,
	FWFTL0		= FWRO + 0x6000,
	FWFTL1		= FWRO + 0x6004,
	FWFTLR		= FWRO + 0x6008,
	FWFTOC		= FWRO + 0x6010,
	FWFTOPC		= FWRO + 0x6014,
	FWFTIM		= FWRO + 0x6020,
	FWFTR		= FWRO + 0x6030,
	FWFTRR0		= FWRO + 0x6034,
	FWFTRR1		= FWRO + 0x6038,
	FWFTRR2		= FWRO + 0x603C,
	FWSEQNGC0	= FWRO + 0x6100,
	FWSEQNGM0	= FWRO + 0x6104,
	FWSEQNRC	= FWRO + 0x6200,
	FWCTFDCN0	= FWRO + 0x6300,
	FWLTHFDCN0	= FWRO + 0x6304,
	FWIPFDCN0	= FWRO + 0x6308,
	FWLTWFDCN0	= FWRO + 0x630C,
	FWPBFDCN0	= FWRO + 0x6310,
	FWMHLCN0	= FWRO + 0x6314,
	FWIHLCN0	= FWRO + 0x6318,
	FWICRDCN0	= FWRO + 0x6500,
	FWWMRDCN0	= FWRO + 0x6504,
	FWCTRDCN0	= FWRO + 0x6508,
	FWLTHRDCN0	= FWRO + 0x650C,
	FWIPRDCN0	= FWRO + 0x6510,
	FWLTWRDCN0	= FWRO + 0x6514,
	FWPBRDCN0	= FWRO + 0x6518,
	FWPMFDCN0	= FWRO + 0x6700,
	FWPGFDCN0	= FWRO + 0x6780,
	FWPMGDCN0	= FWRO + 0x6800,
	FWPMYDCN0	= FWRO + 0x6804,
	FWPMRDCN0	= FWRO + 0x6808,
	FWFRPPCN0	= FWRO + 0x6A00,
	FWFRDPCN0	= FWRO + 0x6A04,
	FWEIS00		= FWRO + 0x7900,
	FWEIE00		= FWRO + 0x7904,
	FWEID00		= FWRO + 0x7908,
	FWEIS1		= FWRO + 0x7A00,
	FWEIE1		= FWRO + 0x7A04,
	FWEID1		= FWRO + 0x7A08,
	FWEIS2		= FWRO + 0x7A10,
	FWEIE2		= FWRO + 0x7A14,
	FWEID2		= FWRO + 0x7A18,
	FWEIS3		= FWRO + 0x7A20,
	FWEIE3		= FWRO + 0x7A24,
	FWEID3		= FWRO + 0x7A28,
	FWEIS4		= FWRO + 0x7A30,
	FWEIE4		= FWRO + 0x7A34,
	FWEID4		= FWRO + 0x7A38,
	FWEIS5		= FWRO + 0x7A40,
	FWEIE5		= FWRO + 0x7A44,
	FWEID5		= FWRO + 0x7A48,
	FWEIS60		= FWRO + 0x7A50,
	FWEIE60		= FWRO + 0x7A54,
	FWEID60		= FWRO + 0x7A58,
	FWEIS61		= FWRO + 0x7A60,
	FWEIE61		= FWRO + 0x7A64,
	FWEID61		= FWRO + 0x7A68,
	FWEIS62		= FWRO + 0x7A70,
	FWEIE62		= FWRO + 0x7A74,
	FWEID62		= FWRO + 0x7A78,
	FWEIS63		= FWRO + 0x7A80,
	FWEIE63		= FWRO + 0x7A84,
	FWEID63		= FWRO + 0x7A88,
	FWEIS70		= FWRO + 0x7A90,
	FWEIE70		= FWRO + 0x7A94,
	FWEID70		= FWRO + 0x7A98,
	FWEIS71		= FWRO + 0x7AA0,
	FWEIE71		= FWRO + 0x7AA4,
	FWEID71		= FWRO + 0x7AA8,
	FWEIS72		= FWRO + 0x7AB0,
	FWEIE72		= FWRO + 0x7AB4,
	FWEID72		= FWRO + 0x7AB8,
	FWEIS73		= FWRO + 0x7AC0,
	FWEIE73		= FWRO + 0x7AC4,
	FWEID73		= FWRO + 0x7AC8,
	FWEIS80		= FWRO + 0x7AD0,
	FWEIE80		= FWRO + 0x7AD4,
	FWEID80		= FWRO + 0x7AD8,
	FWEIS81		= FWRO + 0x7AE0,
	FWEIE81		= FWRO + 0x7AE4,
	FWEID81		= FWRO + 0x7AE8,
	FWEIS82		= FWRO + 0x7AF0,
	FWEIE82		= FWRO + 0x7AF4,
	FWEID82		= FWRO + 0x7AF8,
	FWEIS83		= FWRO + 0x7B00,
	FWEIE83		= FWRO + 0x7B04,
	FWEID83		= FWRO + 0x7B08,
	FWMIS0		= FWRO + 0x7C00,
	FWMIE0		= FWRO + 0x7C04,
	FWMID0		= FWRO + 0x7C08,
	FWSCR0		= FWRO + 0x7D00,
	FWSCR1		= FWRO + 0x7D04,
	FWSCR2		= FWRO + 0x7D08,
	FWSCR3		= FWRO + 0x7D0C,
	FWSCR4		= FWRO + 0x7D10,
	FWSCR5		= FWRO + 0x7D14,
	FWSCR6		= FWRO + 0x7D18,
	FWSCR7		= FWRO + 0x7D1C,
	FWSCR8		= FWRO + 0x7D20,
	FWSCR9		= FWRO + 0x7D24,
	FWSCR10		= FWRO + 0x7D28,
	FWSCR11		= FWRO + 0x7D2C,
	FWSCR12		= FWRO + 0x7D30,
	FWSCR13		= FWRO + 0x7D34,
	FWSCR14		= FWRO + 0x7D38,
	FWSCR15		= FWRO + 0x7D3C,
	FWSCR16		= FWRO + 0x7D40,
	FWSCR17		= FWRO + 0x7D44,
	FWSCR18		= FWRO + 0x7D48,
	FWSCR19		= FWRO + 0x7D4C,
	FWSCR20		= FWRO + 0x7D50,
	FWSCR21		= FWRO + 0x7D54,
	FWSCR22		= FWRO + 0x7D58,
	FWSCR23		= FWRO + 0x7D5C,
	FWSCR24		= FWRO + 0x7D60,
	FWSCR25		= FWRO + 0x7D64,
	FWSCR26		= FWRO + 0x7D68,
	FWSCR27		= FWRO + 0x7D6C,
	FWSCR28		= FWRO + 0x7D70,
	FWSCR29		= FWRO + 0x7D74,
	FWSCR30		= FWRO + 0x7D78,
	FWSCR31		= FWRO + 0x7D7C,
	FWSCR32		= FWRO + 0x7D80,
	FWSCR33		= FWRO + 0x7D84,
	FWSCR34		= FWRO + 0x7D88,
	FWSCR35		= FWRO + 0x7D8C,
	FWSCR36		= FWRO + 0x7D90,
	FWSCR37		= FWRO + 0x7D94,
	FWSCR38		= FWRO + 0x7D98,
	FWSCR39		= FWRO + 0x7D9C,
	FWSCR40		= FWRO + 0x7DA0,
	FWSCR41		= FWRO + 0x7DA4,
	FWSCR42		= FWRO + 0x7DA8,
	FWSCR43		= FWRO + 0x7DAC,
	FWSCR44		= FWRO + 0x7DB0,
	FWSCR45		= FWRO + 0x7DB4,
	FWSCR46		= FWRO + 0x7DB8,

	RIPV		= CARO + 0x0000,
	RRC		= CARO + 0x0004,
	RCEC		= CARO + 0x0008,
	RCDC		= CARO + 0x000C,
	RSSIS		= CARO + 0x0010,
	RSSIE		= CARO + 0x0014,
	RSSID		= CARO + 0x0018,
	CABPIBWMC	= CARO + 0x0020,
	CABPWMLC	= CARO + 0x0040,
	CABPPFLC0	= CARO + 0x0050,
	CABPPWMLC0	= CARO + 0x0060,
	CABPPPFLC00	= CARO + 0x00A0,
	CABPULC		= CARO + 0x0100,
	CABPIRM		= CARO + 0x0140,
	CABPPCM		= CARO + 0x0144,
	CABPLCM		= CARO + 0x0148,
	CABPCPM		= CARO + 0x0180,
	CABPMCPM	= CARO + 0x0200,
	CARDNM		= CARO + 0x0280,
	CARDMNM		= CARO + 0x0284,
	CARDCN		= CARO + 0x0290,
	CAEIS0		= CARO + 0x0300,
	CAEIE0		= CARO + 0x0304,
	CAEID0		= CARO + 0x0308,
	CAEIS1		= CARO + 0x0310,
	CAEIE1		= CARO + 0x0314,
	CAEID1		= CARO + 0x0318,
	CAMIS0		= CARO + 0x0340,
	CAMIE0		= CARO + 0x0344,
	CAMID0		= CARO + 0x0348,
	CAMIS1		= CARO + 0x0350,
	CAMIE1		= CARO + 0x0354,
	CAMID1		= CARO + 0x0358,
	CASCR		= CARO + 0x0380,

	/* Ethernet Agent Address space Empty in spec */
	EAMC		= TARO + 0x0000,
	EAMS		= TARO + 0x0004,
	EAIRC		= TARO + 0x0010,
	EATDQSC		= TARO + 0x0014,
	EATDQC		= TARO + 0x0018,
	EATDQAC		= TARO + 0x001C,
	EATPEC		= TARO + 0x0020,
	EATMFSC0	= TARO + 0x0040,
	EATDQDC0	= TARO + 0x0060,
	EATDQM0		= TARO + 0x0080,
	EATDQMLM0	= TARO + 0x00A0,
	EACTQC		= TARO + 0x0100,
	EACTDQDC	= TARO + 0x0104,
	EACTDQM		= TARO + 0x0108,
	EACTDQMLM	= TARO + 0x010C,
	EAVCC		= TARO + 0x0130,
	EAVTC		= TARO + 0x0134,
	EATTFC		= TARO + 0x0138,
	EACAEC		= TARO + 0x0200,
	EACC		= TARO + 0x0204,
	EACAIVC0	= TARO + 0x0220,
	EACAULC0	= TARO + 0x0240,
	EACOEM		= TARO + 0x0260,
	EACOIVM0	= TARO + 0x0280,
	EACOULM0	= TARO + 0x02A0,
	EACGSM		= TARO + 0x02C0,
	EATASC		= TARO + 0x0300,
	EATASENC0	= TARO + 0x0320,
	EATASCTENC	= TARO + 0x0340,
	EATASENM0	= TARO + 0x0360,
	EATASCTENM	= TARO + 0x0380,
	EATASCSTC0	= TARO + 0x03A0,
	EATASCSTC1	= TARO + 0x03A4,
	EATASCSTM0	= TARO + 0x03A8,
	EATASCSTM1	= TARO + 0x03AC,
	EATASCTC	= TARO + 0x03B0,
	EATASCTM	= TARO + 0x03B4,
	EATASGL0	= TARO + 0x03C0,
	EATASGL1	= TARO + 0x03C4,
	EATASGLR	= TARO + 0x03C8,
	EATASGR		= TARO + 0x03D0,
	EATASGRR	= TARO + 0x03D4,
	EATASHCC	= TARO + 0x03E0,
	EATASRIRM	= TARO + 0x03E4,
	EATASSM		= TARO + 0x03E8,
	EAUSMFSECN	= TARO + 0x0400,
	EATFECN		= TARO + 0x0404,
	EAFSECN		= TARO + 0x0408,
	EADQOECN	= TARO + 0x040C,
	EADQSECN	= TARO + 0x0410,
	EACKSECN	= TARO + 0x0414,
	EAEIS0		= TARO + 0x0500,
	EAEIE0		= TARO + 0x0504,
	EAEID0		= TARO + 0x0508,
	EAEIS1		= TARO + 0x0510,
	EAEIE1		= TARO + 0x0514,
	EAEID1		= TARO + 0x0518,
	EAEIS2		= TARO + 0x0520,
	EAEIE2		= TARO + 0x0524,
	EAEID2		= TARO + 0x0528,
	EASCR		= TARO + 0x0580,

	MPSM		= RMRO + 0x0000,
	MPIC		= RMRO + 0x0004,
	MPIM		= RMRO + 0x0008,
	MIOC		= RMRO + 0x0010,
	MIOM		= RMRO + 0x0014,
	MXMS		= RMRO + 0x0018,
	MTFFC		= RMRO + 0x0020,
	MTPFC		= RMRO + 0x0024,
	MTPFC2		= RMRO + 0x0028,
	MTPFC30		= RMRO + 0x0030,
	MTATC0		= RMRO + 0x0050,
	MTIM		= RMRO + 0x0060,
	MRGC		= RMRO + 0x0080,
	MRMAC0		= RMRO + 0x0084,
	MRMAC1		= RMRO + 0x0088,
	MRAFC		= RMRO + 0x008C,
	MRSCE		= RMRO + 0x0090,
	MRSCP		= RMRO + 0x0094,
	MRSCC		= RMRO + 0x0098,
	MRFSCE		= RMRO + 0x009C,
	MRFSCP		= RMRO + 0x00a0,
	MTRC		= RMRO + 0x00a4,
	MRIM		= RMRO + 0x00a8,
	MRPFM		= RMRO + 0x00aC,
	MPFC0		= RMRO + 0x0100,
	MLVC		= RMRO + 0x0180,
	MEEEC		= RMRO + 0x0184,
	MLBC		= RMRO + 0x0188,
	MXGMIIC		= RMRO + 0x0190,
	MPCH		= RMRO + 0x0194,
	MANC		= RMRO + 0x0198,
	MANM		= RMRO + 0x019C,
	MPLCA1		= RMRO + 0x01a0,
	MPLCA2		= RMRO + 0x01a4,
	MPLCA3		= RMRO + 0x01a8,
	MPLCA4		= RMRO + 0x01ac,
	MPLCAM		= RMRO + 0x01b0,
	MHDC1		= RMRO + 0x01c0,
	MHDC2		= RMRO + 0x01c4,
	MEIS		= RMRO + 0x0200,
	MEIE		= RMRO + 0x0204,
	MEID		= RMRO + 0x0208,
	MMIS0		= RMRO + 0x0210,
	MMIE0		= RMRO + 0x0214,
	MMID0		= RMRO + 0x0218,
	MMIS1		= RMRO + 0x0220,
	MMIE1		= RMRO + 0x0224,
	MMID1		= RMRO + 0x0228,
	MMIS2		= RMRO + 0x0230,
	MMIE2		= RMRO + 0x0234,
	MMID2		= RMRO + 0x0238,
	MMPFTCT		= RMRO + 0x0300,
	MAPFTCT		= RMRO + 0x0304,
	MPFRCT		= RMRO + 0x0308,
	MFCICT		= RMRO + 0x030c,
	MEEECT		= RMRO + 0x0310,
	MMPCFTCT0	= RMRO + 0x0320,
	MAPCFTCT0	= RMRO + 0x0330,
	MPCFRCT0	= RMRO + 0x0340,
	MHDCC		= RMRO + 0x0350,
	MROVFC		= RMRO + 0x0354,
	MRHCRCEC	= RMRO + 0x0358,
	MRXBCE		= RMRO + 0x0400,
	MRXBCP		= RMRO + 0x0404,
	MRGFCE		= RMRO + 0x0408,
	MRGFCP		= RMRO + 0x040C,
	MRBFC		= RMRO + 0x0410,
	MRMFC		= RMRO + 0x0414,
	MRUFC		= RMRO + 0x0418,
	MRPEFC		= RMRO + 0x041C,
	MRNEFC		= RMRO + 0x0420,
	MRFMEFC		= RMRO + 0x0424,
	MRFFMEFC	= RMRO + 0x0428,
	MRCFCEFC	= RMRO + 0x042C,
	MRFCEFC		= RMRO + 0x0430,
	MRRCFEFC	= RMRO + 0x0434,
	MRUEFC		= RMRO + 0x043C,
	MROEFC		= RMRO + 0x0440,
	MRBOEC		= RMRO + 0x0444,
	MTXBCE		= RMRO + 0x0500,
	MTXBCP		= RMRO + 0x0504,
	MTGFCE		= RMRO + 0x0508,
	MTGFCP		= RMRO + 0x050C,
	MTBFC		= RMRO + 0x0510,
	MTMFC		= RMRO + 0x0514,
	MTUFC		= RMRO + 0x0518,
	MTEFC		= RMRO + 0x051C,

	GWMC		= GWRO + 0x0000,
	GWMS		= GWRO + 0x0004,
	GWIRC		= GWRO + 0x0010,
	GWRDQSC		= GWRO + 0x0014,
	GWRDQC		= GWRO + 0x0018,
	GWRDQAC		= GWRO + 0x001C,
	GWRGC		= GWRO + 0x0020,
	GWRMFSC0	= GWRO + 0x0040,
	GWRDQDC0	= GWRO + 0x0060,
	GWRDQM0		= GWRO + 0x0080,
	GWRDQMLM0	= GWRO + 0x00A0,
	GWMTIRM		= GWRO + 0x0100,
	GWMSTLS		= GWRO + 0x0104,
	GWMSTLR		= GWRO + 0x0108,
	GWMSTSS		= GWRO + 0x010C,
	GWMSTSR		= GWRO + 0x0110,
	GWMAC0		= GWRO + 0x0120,
	GWMAC1		= GWRO + 0x0124,
	GWVCC		= GWRO + 0x0130,
	GWVTC		= GWRO + 0x0134,
	GWTTFC		= GWRO + 0x0138,
	GWTDCAC00	= GWRO + 0x0140,
	GWTDCAC10	= GWRO + 0x0144,
	GWTSDCC0	= GWRO + 0x0160,
	GWTNM		= GWRO + 0x0180,
	GWTMNM		= GWRO + 0x0184,
	GWAC		= GWRO + 0x0190,
	GWDCBAC0	= GWRO + 0x0194,
	GWDCBAC1	= GWRO + 0x0198,
	GWIICBSC	= GWRO + 0x019C,
	GWMDNC		= GWRO + 0x01A0,
	GWTRC0		= GWRO + 0x0200,
	GWTPC0		= GWRO + 0x0300,
	GWARIRM		= GWRO + 0x0380,
	GWDCC0		= GWRO + 0x0400,
	GWAARSS		= GWRO + 0x0800,
	GWAARSR0	= GWRO + 0x0804,
	GWAARSR1	= GWRO + 0x0808,
	GWIDAUAS0	= GWRO + 0x0840,
	GWIDASM0	= GWRO + 0x0880,
	GWIDASAM00	= GWRO + 0x0900,
	GWIDASAM10	= GWRO + 0x0904,
	GWIDACAM00	= GWRO + 0x0980,
	GWIDACAM10	= GWRO + 0x0984,
	GWGRLC		= GWRO + 0x0A00,
	GWGRLULC	= GWRO + 0x0A04,
	GWRLIVC0	= GWRO + 0x0A80,
	GWRLULC0	= GWRO + 0x0A84,
	GWIDPC		= GWRO + 0x0B00,
	GWIDC0		= GWRO + 0x0C00,
	GWDIS0		= GWRO + 0x1100,
	GWDIE0		= GWRO + 0x1104,
	GWDID0		= GWRO + 0x1108,
	GWTSDIS		= GWRO + 0x1180,
	GWTSDIE		= GWRO + 0x1184,
	GWTSDID		= GWRO + 0x1188,
	GWEIS0		= GWRO + 0x1190,
	GWEIE0		= GWRO + 0x1194,
	GWEID0		= GWRO + 0x1198,
	GWEIS1		= GWRO + 0x11A0,
	GWEIE1		= GWRO + 0x11A4,
	GWEID1		= GWRO + 0x11A8,
	GWEIS20		= GWRO + 0x1200,
	GWEIE20		= GWRO + 0x1204,
	GWEID20		= GWRO + 0x1208,
	GWEIS3		= GWRO + 0x1280,
	GWEIE3		= GWRO + 0x1284,
	GWEID3		= GWRO + 0x1288,
	GWEIS4		= GWRO + 0x1290,
	GWEIE4		= GWRO + 0x1294,
	GWEID4		= GWRO + 0x1298,
	GWEIS5		= GWRO + 0x12A0,
	GWEIE5		= GWRO + 0x12A4,
	GWEID5		= GWRO + 0x12A8,
	GWSCR0		= GWRO + 0x1800,
	GWSCR1		= GWRO + 0x1900,
};

/* ETHA/RMAC */
enum rswitch_etha_mode {
	EAMC_OPC_RESET,
	EAMC_OPC_DISABLE,
	EAMC_OPC_CONFIG,
	EAMC_OPC_OPERATION,
};
#define EAMS_OPS_MASK	EAMC_OPC_OPERATION

#define MPIC_PIS_MII	0x00
#define MPIC_PIS_GMII	0x02
#define MPIC_PIS_XGMII	0x04
#define MPIC_LSC_SHIFT	3
#define MPIC_LSC_10M	(0 << MPIC_LSC_SHIFT)
#define MPIC_LSC_100M	(1 << MPIC_LSC_SHIFT)
#define MPIC_LSC_1G	(2 << MPIC_LSC_SHIFT)
#define MPIC_LSC_2_5G	(3 << MPIC_LSC_SHIFT)
#define MPIC_LSC_5G	(4 << MPIC_LSC_SHIFT)
#define MPIC_LSC_10G	(5 << MPIC_LSC_SHIFT)

#define MDIO_READ_C45		0x03
#define MDIO_WRITE_C45		0x01

#define REG_MASK		0xffff
#define DEV_MASK		GENMASK(24, 16)
#define ACCESS_MODE		BIT(30)

#define MPSM_PSME		BIT(0)
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
#define MMIS1_PAACS             BIT(2) /* Address */
#define MMIS1_PWACS             BIT(1) /* Write */
#define MMIS1_PRACS             BIT(0) /* Read */
#define MMIS1_CLEAR_FLAGS       0xf

#define MPIC_PSMCS_SHIFT	16
#define MPIC_PSMCS_MASK		GENMASK(22, MPIC_PSMCS_SHIFT)
#define MPIC_PSMCS(val)		((val) << MPIC_PSMCS_SHIFT)

#define MPIC_PSMHT_SHIFT	24
#define MPIC_PSMHT_MASK		GENMASK(26, MPIC_PSMHT_SHIFT)
#define MPIC_PSMHT(val)		((val) << MPIC_PSMHT_SHIFT)

#define MLVC_PLV	BIT(16)

/* GWCA */
enum rswitch_gwca_mode {
	GWMC_OPC_RESET,
	GWMC_OPC_DISABLE,
	GWMC_OPC_CONFIG,
	GWMC_OPC_OPERATION,
};
#define GWMS_OPS_MASK	GWMC_OPC_OPERATION

#define GWMTIRM_MTIOG	BIT(0)
#define GWMTIRM_MTR	BIT(1)

#define GWVCC_VEM_SC_TAG	(0x3 << 16)

#define GWARIRM_ARIOG	BIT(0)
#define GWARIRM_ARR	BIT(1)

#define GWDCC_BALR		BIT(24)
#define GWDCC_DCP(q, idx)	((q + (idx * 2)) << 16)
#define GWDCC_DQT		BIT(11)
#define GWDCC_ETS		BIT(9)
#define GWDCC_EDE		BIT(8)

#define GWMDNC_TXDMN(val)	((val & 0x1f) << 8)

#define GWDCC_OFFS(chain)	(GWDCC0 + (chain) * 4)
/* COMA */
#define RRC_RR		BIT(0)
#define RRC_RR_CLR	(0)
#define RCEC_RCE	BIT(16)
#define RCDC_RCD	BIT(16)

#define CABPIRM_BPIOG	BIT(0)
#define CABPIRM_BPR	BIT(1)

/* MFWD */
#define FWPC0_LTHTA	BIT(0)
#define FWPC0_IP4UE	BIT(3)
#define FWPC0_IP4TE	BIT(4)
#define FWPC0_IP4OE	BIT(5)
#define FWPC0_L2SE	BIT(9)
#define FWPC0_IP4EA	BIT(10)
#define FWPC0_IPDSA	BIT(12)
#define FWPC0_IPHLA	BIT(18)
#define FWPC0_MACSDA	BIT(20)
#define FWPC0_MACHLA	BIT(26)
#define FWPC0_MACHMA	BIT(27)
#define FWPC0_VLANSA	BIT(28)

#define FWPC0_DEFAULT	(FWPC0_LTHTA | FWPC0_IP4UE | FWPC0_IP4TE | \
			 FWPC0_IP4OE | FWPC0_L2SE | FWPC0_IP4EA | \
			 FWPC0_IPDSA | FWPC0_IPHLA | FWPC0_MACSDA | \
			 FWPC0_MACHLA |	FWPC0_MACHMA | FWPC0_VLANSA)

#define FWPC1_DDE	BIT(0)

#define	FWPBFC(i)		(FWPBFCi + (i) * 0x10)
#define	FWPBFC_PBDV_MASK	(GENMASK(RSWITCH_NUM_HW - 1, 0)

/* SerDes */
enum rswitch_serdes_mode {
	USXGMII,
	SGMII,
	COMBINATION,
};

#define RSWITCH_SERDES_OFFSET                   0x0400
#define RSWITCH_SERDES_BANK_SELECT              0x03fc
#define RSWITCH_SERDES_FUSE_OVERRIDE(n)         (0x2600 - (n) * 0x400)

#define BANK_180                                0x0180
#define VR_XS_PMA_MP_12G_16G_25G_SRAM           0x026c
#define VR_XS_PMA_MP_12G_16G_25G_REF_CLK_CTRL   0x0244
#define VR_XS_PMA_MP_10G_MPLLA_CTRL2            0x01cc
#define VR_XS_PMA_MP_12G_16G_25G_MPLL_CMN_CTRL  0x01c0
#define VR_XS_PMA_MP_12G_16G_MPLLA_CTRL0        0x01c4
#define VR_XS_PMA_MP_12G_MPLLA_CTRL1            0x01c8
#define VR_XS_PMA_MP_12G_MPLLA_CTRL3            0x01dc
#define VR_XS_PMA_MP_12G_16G_25G_VCO_CAL_LD0    0x0248
#define VR_XS_PMA_MP_12G_VCO_CAL_REF0           0x0258
#define VR_XS_PMA_MP_12G_16G_25G_RX_GENCTRL1    0x0144
#define VR_XS_PMA_CONSUMER_10G_RX_GENCTRL4      0x01a0
#define VR_XS_PMA_MP_12G_16G_25G_TX_RATE_CTRL   0x00d0
#define VR_XS_PMA_MP_12G_16G_25G_RX_RATE_CTRL   0x0150
#define VR_XS_PMA_MP_12G_16G_TX_GENCTRL2        0x00c8
#define VR_XS_PMA_MP_12G_16G_RX_GENCTRL2        0x0148
#define VR_XS_PMA_MP_12G_AFE_DFE_EN_CTRL        0x0174
#define VR_XS_PMA_MP_12G_RX_EQ_CTRL0            0x0160
#define VR_XS_PMA_MP_10G_RX_IQ_CTRL0            0x01ac
#define VR_XS_PMA_MP_12G_16G_25G_TX_GENCTRL1    0x00c4
#define VR_XS_PMA_MP_12G_16G_TX_GENCTRL2        0x00c8
#define VR_XS_PMA_MP_12G_16G_RX_GENCTRL2        0x0148
#define VR_XS_PMA_MP_12G_16G_25G_TX_GENCTRL1    0x00c4
#define VR_XS_PMA_MP_12G_16G_25G_TX_EQ_CTRL0    0x00d8
#define VR_XS_PMA_MP_12G_16G_25G_TX_EQ_CTRL1    0x00dc
#define VR_XS_PMA_MP_12G_16G_MPLLB_CTRL0        0x01d0
#define VR_XS_PMA_MP_12G_MPLLB_CTRL1            0x01d4
#define VR_XS_PMA_MP_12G_16G_MPLLB_CTRL2        0x01d8
#define VR_XS_PMA_MP_12G_MPLLB_CTRL3            0x01e0

#define BANK_300                                0x0300
#define SR_XS_PCS_CTRL1                         0x0000
#define SR_XS_PCS_STS1                          0x0004
#define SR_XS_PCS_CTRL2                         0x001c

#define BANK_380                                0x0380
#define VR_XS_PCS_DIG_CTRL1                     0x0000
#define VR_XS_PCS_DEBUG_CTRL                    0x0014
#define VR_XS_PCS_KR_CTRL                       0x001c

#define BANK_1F00                               0x1f00
#define SR_MII_CTRL                             0x0000

#define BANK_1F80                               0x1f80
#define VR_MII_AN_CTRL                          0x0004

/* Descriptors */
enum RX_DS_CC_BIT {
	RX_DS	= 0x0fff, /* Data size */
	RX_TR	= 0x1000, /* Truncation indication */
	RX_EI	= 0x2000, /* Error indication */
	RX_PS	= 0xc000, /* Padding selection */
};

enum TX_DS_TAGL_BIT {
	TX_DS	= 0x0fff, /* Data size */
	TX_TAGL	= 0xf000, /* Frame tag LSBs */
};

enum DIE_DT {
	/* Frame data */
	DT_FSINGLE	= 0x80,
	DT_FSTART	= 0x90,
	DT_FMID		= 0xA0,
	DT_FEND		= 0xB8,

	/* Chain control */
	DT_LEMPTY	= 0xC0,
	DT_EEMPTY	= 0xD0,
	DT_LINKFIX	= 0x00,
	DT_LINK		= 0xE0,
	DT_EOS		= 0xF0,
	/* HW/SW arbitration */
	DT_FEMPTY	= 0x40,
	DT_FEMPTY_IS	= 0x10,
	DT_FEMPTY_IC	= 0x20,
	DT_FEMPTY_ND	= 0x38,
	DT_FEMPTY_START	= 0x50,
	DT_FEMPTY_MID	= 0x60,
	DT_FEMPTY_END	= 0x70,

	DT_MASK		= 0xF0,
	DIE		= 0x08,	/* Descriptor Interrupt Enable */
};

struct rswitch_desc {
	__le16 info_ds;	/* Descriptor size */
	u8 die_dt;	/* Descriptor interrupt enable and type */
	__u8  dptrh;	/* Descriptor pointer MSB */
	__le32 dptrl;	/* Descriptor pointer LSW */
} __packed;

struct rswitch_ts_desc {
	__le16 info_ds;	/* Descriptor size */
	u8 die_dt;	/* Descriptor interrupt enable and type */
	__u8  dptrh;	/* Descriptor pointer MSB */
	__le32 dptrl;	/* Descriptor pointer LSW */
	__le32 ts_nsec;
	__le32 ts_sec;
} __packed;

struct rswitch_ext_desc {
	__le16 info_ds;	/* Descriptor size */
	u8 die_dt;	/* Descriptor interrupt enable and type */
	__u8  dptrh;	/* Descriptor pointer MSB */
	__le32 dptrl;	/* Descriptor pointer LSW */
	__le64 info1;
} __packed;

struct rswitch_ext_ts_desc {
	__le16 info_ds;	/* Descriptor size */
	u8 die_dt;	/* Descriptor interrupt enable and type */
	__u8  dptrh;	/* Descriptor pointer MSB */
	__le32 dptrl;	/* Descriptor pointer LSW */
	__le64 info1;
	__le32 ts_nsec;
	__le32 ts_sec;
} __packed;

#define DESC_INFO1_FMT		BIT(2)
#define DESC_INFO1_CSD0_SHIFT	32
#define DESC_INFO1_CSD1_SHIFT	40
#define DESC_INFO1_DV_SHIFT	48

struct rswitch_etha {
	int index;
	void __iomem *addr;
	void __iomem *serdes_addr;
	bool external_phy;
	struct mii_bus *mii;
	phy_interface_t phy_interface;
	u8 mac_addr[MAX_ADDR_LEN];
	int link;
	bool operated;
};

struct rswitch_gwca_chain {
	int index;
	bool dir_tx;
	bool gptp;
	union {
		struct rswitch_desc *ring;
		struct rswitch_ext_ts_desc *ts_ring;
	};
	dma_addr_t ring_dma;
	u32 num_ring;
	u32 cur;
	u32 dirty;
	struct sk_buff **skb;

	struct net_device *ndev;	/* chain to ndev for irq */
};

#define RSWITCH_NUM_IRQ_REGS	(RSWITCH_MAX_NUM_CHAINS / BITS_PER_TYPE(u32))
struct rswitch_gwca {
	int index;
	struct rswitch_gwca_chain *chains;
	int num_chains;
	DECLARE_BITMAP(used, RSWITCH_MAX_NUM_CHAINS);
	u32 tx_irq_bits[RSWITCH_NUM_IRQ_REGS];
	u32 rx_irq_bits[RSWITCH_NUM_IRQ_REGS];
};

#define NUM_CHAINS_PER_NDEV	2
struct rswitch_device {
	struct rswitch_private *priv;
	struct net_device *ndev;
	struct napi_struct napi;
	void __iomem *addr;
	bool gptp_master;
	struct rswitch_gwca_chain *tx_chain;
	struct rswitch_gwca_chain *rx_chain;
	spinlock_t lock;

	int port;
	struct rswitch_etha *etha;
};

struct rswitch_mfwd_mac_table_entry {
	int chain_index;
	unsigned char addr[MAX_ADDR_LEN];
};

struct rswitch_mfwd {
	struct rswitch_mac_table_entry *mac_table_entries;
	int num_mac_table_entries;
};

struct rswitch_private {
	struct platform_device *pdev;
	void __iomem *addr;
	void __iomem *serdes_addr;
	struct rswitch_desc *desc_bat;
	dma_addr_t desc_bat_dma;
	u32 desc_bat_size;

	struct rswitch_gwca gwca;
	struct rswitch_etha etha[RSWITCH_MAX_NUM_ETHA];
	struct rswitch_mfwd mfwd;

	struct clk *rsw_clk;
	struct clk *phy_clk;
};

static int num_ndev = 3;
module_param(num_ndev, int, 0644);
MODULE_PARM_DESC(num_ndev, "Number of creating network devices");

static int num_etha_ports = 3;
module_param(num_etha_ports, int, 0644);
MODULE_PARM_DESC(num_etha_ports, "Number of using ETHA ports");

#define RSWITCH_TIMEOUT_MS	1000
static int rswitch_reg_wait(void __iomem *addr, u32 offs, u32 mask, u32 expected)
{
	int i;

	for (i = 0; i < RSWITCH_TIMEOUT_MS; i++) {
		if ((rs_read32(addr + offs) & mask) == expected)
			return 0;

		mdelay(1);
	}

	return -ETIMEDOUT;
}

static u32 rswitch_etha_offs(int index)
{
	return RSWITCH_ETHA_OFFSET + index * RSWITCH_ETHA_SIZE;
}

static u32 rswitch_etha_read(struct rswitch_etha *etha, enum rswitch_reg reg)
{
	return rs_read32(etha->addr + reg);
}

static void rswitch_etha_write(struct rswitch_etha *etha, u32 data, enum rswitch_reg reg)
{
	rs_write32(data, etha->addr + reg);
}

static void rswitch_etha_modify(struct rswitch_etha *etha, enum rswitch_reg reg,
				u32 clear, u32 set)
{
	rswitch_etha_write(etha, (rswitch_etha_read(etha, reg) & ~clear) | set, reg);
}

static void rswitch_modify(void __iomem *addr, enum rswitch_reg reg, u32 clear, u32 set)
{
	rs_write32((rs_read32(addr + reg) & ~clear) | set, addr + reg);
}

static bool __maybe_unused rswitch_is_any_data_irq(struct rswitch_private *priv, u32 *dis, bool tx)
{
	int i;
	u32 *mask = tx ? priv->gwca.tx_irq_bits : priv->gwca.rx_irq_bits;

	for (i = 0; i < RSWITCH_NUM_IRQ_REGS; i++) {
		if (dis[i] & mask[i])
			return true;
	}

	return false;
}

static void rswitch_get_data_irq_status(struct rswitch_private *priv, u32 *dis)
{
	int i;

	for (i = 0; i < RSWITCH_NUM_IRQ_REGS; i++)
		dis[i] = rs_read32(priv->addr + GWDIS0 + i * 0x10);
}

static void rswitch_enadis_data_irq(struct rswitch_private *priv, int index, bool enable)
{
	u32 offs = (enable ? GWDIE0 : GWDID0) + (index / 32) * 0x10;
	u32 tmp = 0;

	/* For VPF? */
	if (enable)
		tmp = rs_read32(priv->addr + offs);

	rs_write32(BIT(index % 32) | tmp, priv->addr + offs);
}

static void rswitch_ack_data_irq(struct rswitch_private *priv, int index)
{
	u32 offs = GWDIS0 + (index / 32) * 0x10;

	rs_write32(BIT(index % 32), priv->addr + offs);
}

static bool rswitch_is_chain_rxed(struct rswitch_gwca_chain *c, u8 unexpected)
{
	int entry;
	struct rswitch_desc *desc; /* FIXME: Use normal descritor for now */

	entry = c->dirty % c->num_ring;
	desc = &c->ring[entry];

	if ((desc->die_dt & DT_MASK) != unexpected)
		return true;

	return false;
}

static bool rswitch_rx(struct net_device *ndev, int *quota)
{
	struct rswitch_device *rdev = netdev_priv(ndev);
	struct rswitch_gwca_chain *c = rdev->rx_chain;
	/* FIXME: how to support ts desc? */
	int boguscnt = c->dirty + c->num_ring - c->cur;
	int entry = c->cur % c->num_ring;
	struct rswitch_desc *desc = &c->ring[entry];
	int limit;
	u16 pkt_len;
	struct sk_buff *skb;
	dma_addr_t dma_addr;

	boguscnt = min(boguscnt, *quota);
	limit = boguscnt;

	while ((desc->die_dt & DT_MASK) != DT_FEMPTY) {
		dma_rmb();
		pkt_len = le16_to_cpu(desc->info_ds) & RX_DS;
		if (--boguscnt < 0)
			break;
		skb = c->skb[entry];
		c->skb[entry] = NULL;
		dma_addr = le32_to_cpu(desc->dptrl) | ((__le64)le32_to_cpu(desc->dptrh) << 32);
		dma_unmap_single(ndev->dev.parent, dma_addr, PKT_BUF_SZ, DMA_FROM_DEVICE);
		/* TODO: get_ts */
		skb_put(skb, pkt_len);
		skb->protocol = eth_type_trans(skb, ndev);
		netif_receive_skb(skb);
		rdev->ndev->stats.rx_packets++;
		rdev->ndev->stats.rx_bytes += pkt_len;

		entry = (++c->cur) % c->num_ring;
		desc = &c->ring[entry];
	}

	/* Refill the RX ring buffers */
	for (; c->cur - c->dirty > 0; c->dirty++) {
		entry = c->dirty % c->num_ring;
		desc = &c->ring[entry];
		desc->info_ds = cpu_to_le16(PKT_BUF_SZ);

		if (!c->skb[entry]) {
			skb = dev_alloc_skb(PKT_BUF_SZ + RSWITCH_ALIGN - 1);
			if (!skb)
				break;	/* Better luch next round */
			skb_reserve(skb, NET_IP_ALIGN);
			dma_addr = dma_map_single(ndev->dev.parent, skb->data,
						  le16_to_cpu(desc->info_ds),
						  DMA_FROM_DEVICE);
			if (dma_mapping_error(ndev->dev.parent, dma_addr))
				desc->info_ds = cpu_to_le16(0);
			desc->dptrl = cpu_to_le32(lower_32_bits(dma_addr));
			desc->dptrh = cpu_to_le32(upper_32_bits(dma_addr));
			skb_checksum_none_assert(skb);
			c->skb[entry] = skb;
		}
		dma_wmb();
		desc->die_dt = DT_FEMPTY | DIE;
	}

	*quota -= limit - (++boguscnt);

	return boguscnt <= 0;
}

static int rswitch_tx_free(struct net_device *ndev, bool free_txed_only)
{
	struct rswitch_device *rdev = netdev_priv(ndev);
	/* FIXME: how to support ts desc? */
	struct rswitch_desc *desc;
	int free_num = 0;
	int entry, size;
	dma_addr_t dma_addr;
	struct rswitch_gwca_chain *c = rdev->tx_chain;

	for (; c->cur - c->dirty > 0; c->dirty++) {
		entry = c->dirty % c->num_ring;
		desc = &c->ring[entry];
		if (free_txed_only && (desc->die_dt & DT_MASK) != DT_FEMPTY)
			break;

		dma_rmb();
		size = le16_to_cpu(desc->info_ds) & TX_DS;
		if (c->skb[entry]) {
			dma_addr = le32_to_cpu(desc->dptrl) |
				   ((__le64)le32_to_cpu(desc->dptrh) << 32);
			dma_unmap_single(ndev->dev.parent, dma_addr,
					size, DMA_TO_DEVICE);
			dev_kfree_skb_any(c->skb[entry]);
			c->skb[entry] = NULL;
			free_num++;
		}
		desc->die_dt = DT_EEMPTY;
		rdev->ndev->stats.tx_packets++;
		rdev->ndev->stats.tx_bytes += size;
	}

	return free_num;
}

static int rswitch_poll(struct napi_struct *napi, int budget)
{
	struct net_device *ndev = napi->dev;
	struct rswitch_device *rdev = netdev_priv(ndev);
	struct rswitch_private *priv = rdev->priv;
	int quota = budget;

retry:
	rswitch_tx_free(ndev, true);

	if (rswitch_rx(ndev, &quota))
		goto out;
	else if (rswitch_is_chain_rxed(rdev->rx_chain, DT_FEMPTY))
		goto retry;

	netif_wake_subqueue(ndev, 0);

	napi_complete(napi);

	/* Re-enable RX/TX interrupts */
	rswitch_enadis_data_irq(priv, rdev->tx_chain->index, true);
	rswitch_enadis_data_irq(priv, rdev->rx_chain->index, true);
	__iowmb();

out:
	return budget - quota;
}

static bool rswitch_agent_clock_is_enabled(void __iomem *base_addr, int port)
{
	u32 val = rs_read32(base_addr + RCEC);

	if (val & RCEC_RCE)
		return (val & BIT(port)) ? true : false;
	else
		return false;
}

static void rswitch_agent_clock_ctrl(void __iomem *base_addr, int port, int enable)
{
	u32 val;

	if (enable) {
		val = rs_read32(base_addr + RCEC);
		rs_write32(val | RCEC_RCE | BIT(port), base_addr + RCEC);
	} else {
		val = rs_read32(base_addr + RCDC);
		rs_write32(val | BIT(port), base_addr + RCDC);
	}
}

static int rswitch_etha_change_mode(struct rswitch_etha *etha,
				    enum rswitch_etha_mode mode)
{
	void __iomem *base_addr;
	int ret;

	base_addr = etha->addr - rswitch_etha_offs(etha->index);

	/* Enable clock */
	if (!rswitch_agent_clock_is_enabled(base_addr, etha->index))
		rswitch_agent_clock_ctrl(base_addr, etha->index, 1);

	rs_write32(mode, etha->addr + EAMC);

	ret = rswitch_reg_wait(etha->addr, EAMS, EAMS_OPS_MASK, mode);

	/* Disable clock */
	if (mode == EAMC_OPC_DISABLE)
		rswitch_agent_clock_ctrl(base_addr, etha->index, 0);

	return ret;
}

static void rswitch_etha_read_mac_address(struct rswitch_etha *etha)
{
	u8 *mac = &etha->mac_addr[0];
	u32 mrmac0 = rswitch_etha_read(etha, MRMAC0);
	u32 mrmac1 = rswitch_etha_read(etha, MRMAC1);

	mac[0] = (mrmac0 >>  8) & 0xFF;
	mac[1] = (mrmac0 >>  0) & 0xFF;
	mac[2] = (mrmac1 >> 24) & 0xFF;
	mac[3] = (mrmac1 >> 16) & 0xFF;
	mac[4] = (mrmac1 >>  8) & 0xFF;
	mac[5] = (mrmac1 >>  0) & 0xFF;
}

static bool rswitch_etha_wait_link_verification(struct rswitch_etha *etha)
{
	/* Request Link Verification */
	rswitch_etha_write(etha, MLVC_PLV, MLVC);
	return rswitch_reg_wait(etha->addr, MLVC, MLVC_PLV, 0);
}

static void rswitch_rmac_setting(struct rswitch_etha *etha, const u8 *mac)
{
	/* FIXME */
	/* Set xMII type */
	rswitch_etha_write(etha, MPIC_PIS_GMII | MPIC_LSC_1G, MPIC);

#if 0
	/* Set Interrupt enable */
	rswitch_etha_write(etha, 0, MEIE);
	rswitch_etha_write(etha, 0, MMIE0);
	rswitch_etha_write(etha, 0, MMIE1);
	rswitch_etha_write(etha, 0, MMIE2);
	rswitch_etha_write(etha, 0, MMIE2);
	/* Set Tx function */
	rswitch_etha_write(etha, 0, MTFFC);
	rswitch_etha_write(etha, 0, MTPFC);
	rswitch_etha_write(etha, 0, MTPFC2);
	rswitch_etha_write(etha, 0, MTPFC30);
	rswitch_etha_write(etha, 0, MTATC0);
	/* Set Rx function */
	rswitch_etha_write(etha, 0, MRGC);
	rswitch_etha_write(etha, 0x00070007, MRAFC);
	rswitch_etha_write(etha, 0, MRFSCE);
	rswitch_etha_write(etha, 0, MRFSCP);
	rswitch_etha_write(etha, 0, MTRC);

	/* Set Address Filtering function */
	/* Set XGMII function */
	/* Set Half Duplex function */
	/* Set PLCA function */
#endif
}

static void rswitch_etha_enable_mii(struct rswitch_etha *etha)
{
	rswitch_etha_modify(etha, MPIC, MPIC_PSMCS_MASK | MPIC_PSMHT_MASK,
			    MPIC_PSMCS(0x05) | MPIC_PSMHT(0x06));
	rswitch_etha_modify(etha, MPSM, 0, MPSM_MFF_C45);
}

static void rswitch_etha_disable_mii(struct rswitch_etha *etha)
{
	rswitch_etha_modify(etha, MPIC, MPIC_PSMCS_MASK, 0);
}

static int rswitch_etha_hw_init(struct rswitch_etha *etha, const u8 *mac)
{
	int err;

	/* Change to CONFIG Mode */
	err = rswitch_etha_change_mode(etha, EAMC_OPC_DISABLE);
	if (err < 0)
		return err;
	err = rswitch_etha_change_mode(etha, EAMC_OPC_CONFIG);
	if (err < 0)
		return err;

	rswitch_rmac_setting(etha, mac);
	rswitch_etha_enable_mii(etha);

	/* Change to OPERATION Mode */
	err = rswitch_etha_change_mode(etha, EAMC_OPC_OPERATION);
	if (err < 0)
		return err;

	/* Link Verification */
	return rswitch_etha_wait_link_verification(etha);
}

void rswitch_serdes_write32(void __iomem *addr, u32 offs,  u32 bank, u32 data)
{
	iowrite32(bank, addr + RSWITCH_SERDES_BANK_SELECT);
	iowrite32(data, addr + offs);
}

u32 rswitch_serdes_read32(void __iomem *addr, u32 offs,  u32 bank)
{
	iowrite32(bank, addr + RSWITCH_SERDES_BANK_SELECT);
	return ioread32(addr + offs);
}

static int rswitch_serdes_reg_wait(void __iomem *addr, u32 offs, u32 bank, u32 mask, u32 expected)
{
	int i;

	iowrite32(bank, addr + RSWITCH_SERDES_BANK_SELECT);
	mdelay(1);

	for (i = 0; i < RSWITCH_TIMEOUT_MS; i++) {
		if ((ioread32(addr + offs) & mask) == expected)
			return 0;
		mdelay(1);
	}

	return -ETIMEDOUT;
}

static int rswitch_serdes_common_setting(struct rswitch_etha *etha, enum rswitch_serdes_mode mode)
{
	void __iomem *addr = etha->serdes_addr;

	switch (mode) {
	case SGMII:
		rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_16G_25G_REF_CLK_CTRL, BANK_180, 0x97);
		rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_16G_MPLLB_CTRL0, BANK_180, 0x60);
		rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_16G_MPLLB_CTRL2, BANK_180, 0x2200);
		rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_MPLLB_CTRL1, BANK_180, 0);
		rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_MPLLB_CTRL3, BANK_180, 0x3d);

		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int rswitch_serdes_chan_setting(struct rswitch_etha *etha, enum rswitch_serdes_mode mode)
{
	void __iomem *addr = etha->serdes_addr;
	u32 val;
	int ret;

	switch (mode) {
	case SGMII:
		rswitch_serdes_write32(addr, SR_XS_PCS_CTRL2, BANK_300, 0x01);
		rswitch_serdes_write32(addr, VR_XS_PCS_DIG_CTRL1, BANK_380, 0x2000);

		/* Set common settings*/
		ret = rswitch_serdes_common_setting(etha, mode);
		if (ret)
			return ret;

		rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_16G_25G_MPLL_CMN_CTRL,
				       BANK_180, 0x11);
		rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_16G_25G_VCO_CAL_LD0, BANK_180, 0x540);
		rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_VCO_CAL_REF0, BANK_180, 0x15);
		rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_16G_25G_RX_GENCTRL1, BANK_180, 0x100);
		rswitch_serdes_write32(addr, VR_XS_PMA_CONSUMER_10G_RX_GENCTRL4, BANK_180, 0);
		rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_16G_25G_TX_RATE_CTRL, BANK_180, 0x02);
		rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_16G_25G_RX_RATE_CTRL, BANK_180, 0x03);
		rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_16G_TX_GENCTRL2, BANK_180, 0x100);
		rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_16G_RX_GENCTRL2, BANK_180, 0x100);
		rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_AFE_DFE_EN_CTRL, BANK_180, 0);
		rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_RX_EQ_CTRL0, BANK_180, 0x07);
		rswitch_serdes_write32(addr, VR_XS_PMA_MP_10G_RX_IQ_CTRL0, BANK_180, 0);
		rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_16G_25G_TX_GENCTRL1, BANK_180, 0x310);
		rswitch_serdes_write32(addr, VR_XS_PCS_DIG_CTRL1, BANK_380, 0xa000);
		ret = rswitch_serdes_reg_wait(addr, VR_XS_PCS_DIG_CTRL1, BANK_380, BIT(15), 0);
		if (ret)
			return ret;

		rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_16G_25G_TX_GENCTRL1,
				       BANK_180, 0x1310);
		rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_16G_25G_TX_EQ_CTRL0,
				       BANK_180, 0x1800);
		rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_16G_25G_TX_EQ_CTRL1, BANK_180, 0);

		val = rswitch_serdes_read32(addr, VR_MII_AN_CTRL, BANK_1F80);
		rswitch_serdes_write32(addr, VR_MII_AN_CTRL, BANK_1F80, val | 0x100);

		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int rswitch_serdes_set_speed(struct rswitch_etha *etha, enum rswitch_serdes_mode mode,
				    int speed)
{
	void __iomem *addr = etha->serdes_addr;

	switch (mode) {
	case SGMII:
		if (speed == 1000)
			rswitch_serdes_write32(addr, SR_MII_CTRL, BANK_1F00, 0x140);
		else if (speed == 100)
			rswitch_serdes_write32(addr, SR_MII_CTRL, BANK_1F00, 0x2100);
		else if (speed == 10)
			rswitch_serdes_write32(addr, SR_MII_CTRL, BANK_1F00, 0x100);

		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int __maybe_unused rswitch_serdes_init(struct rswitch_etha *etha)
{
	int ret;
	enum rswitch_serdes_mode mode;

	/* TODO: Support more modes */

	switch (etha->phy_interface) {
	case PHY_INTERFACE_MODE_SGMII:
		mode = SGMII;
		break;
	default:
		pr_debug("%s: Don't support this interface", __func__);
		return -EOPNOTSUPP;
	}

	/* Disable FUSE_OVERRIDE_EN */
	if (ioread32(etha->serdes_addr + RSWITCH_SERDES_FUSE_OVERRIDE(etha->index)))
		iowrite32(0, etha->serdes_addr + RSWITCH_SERDES_FUSE_OVERRIDE(etha->index));

	/* Initialize SRAM */
	ret = rswitch_serdes_reg_wait(etha->serdes_addr, VR_XS_PMA_MP_12G_16G_25G_SRAM, BANK_180,
				      BIT(0), 0x01);
	if (ret)
		return ret;

	rswitch_serdes_write32(etha->serdes_addr, VR_XS_PMA_MP_12G_16G_25G_SRAM, BANK_180, 0x3);

	ret = rswitch_serdes_reg_wait(etha->serdes_addr, SR_XS_PCS_CTRL1, BANK_300, BIT(15), 0);
	if (ret)
		return ret;

	/* Set channel settings*/
	ret = rswitch_serdes_chan_setting(etha, mode);
	if (ret)
		return ret;

	/* Set speed (bps) */
	ret = rswitch_serdes_set_speed(etha, mode, 1000);
	if (ret)
		return ret;

	ret = rswitch_serdes_reg_wait(etha->serdes_addr, SR_XS_PCS_STS1, BANK_300, BIT(2), BIT(2));
	if (ret) {
		pr_debug("\n%s: SerDes Link up failed", __func__);
		return ret;
	}

	return 0;
}

static int rswitch_etha_set_access(struct rswitch_etha *etha, bool read,
				   int phyad, int devad, int regad, int data)
{
	int pop = read ? MDIO_READ_C45 : MDIO_WRITE_C45;
	u32 val;
	int ret;

	/* No match device */
	if (devad == 0xffffffff)
		return 0;

	/* Clear completion flags */
	writel(MMIS1_CLEAR_FLAGS, etha->addr + MMIS1);

	/* Submit address to PHY (MDIO_ADDR_C45 << 13) */
	val = MPSM_PSME | MPSM_MFF_C45;
	rs_write32((regad << 16) | (devad << 8) | (phyad << 3) | val, etha->addr + MPSM);

	ret = rswitch_reg_wait(etha->addr, MMIS1, MMIS1_PAACS, MMIS1_PAACS);
	if (ret)
		return ret;

	/* Clear address completion flag */
	rswitch_modify(etha, MMIS1, MMIS1_PAACS, MMIS1_PAACS);

	/* Read/Write PHY register */
	if (read) {
		writel((pop << 13) | (devad << 8) | (phyad << 3) | val, etha->addr + MPSM);

		ret = rswitch_reg_wait(etha->addr, MMIS1, MMIS1_PRACS, MMIS1_PRACS);
		if (ret)
			return ret;

		/* Read data */
		ret = (rs_read32(etha->addr + MPSM) & MPSM_PRD_MASK) >> 16;

		/* Clear read completion flag */
		rswitch_etha_modify(etha, MMIS1, MMIS1_PRACS, MMIS1_PRACS);
	} else {
		rs_write32((data << 16) | (pop << 13) | (devad << 8) | (phyad << 3) | val,
			   etha->addr + MPSM);

		ret = rswitch_reg_wait(etha->addr, MMIS1, MMIS1_PWACS, MMIS1_PWACS);
	}

	return ret;
}


static int rswitch_etha_mii_read(struct mii_bus *bus, int addr, int regnum)
{
	struct rswitch_etha *etha = bus->priv;
	int mode, devad, regad;

	mode = regnum & MII_ADDR_C45;
	devad = (regnum >> MII_DEVADDR_C45_SHIFT) & 0x1f;
	regad = regnum & MII_REGADDR_C45_MASK;

	/* Not support Clause 22 access method */
	if (!mode)
		return 0;

	return rswitch_etha_set_access(etha, true, addr, devad, regad, 0);
}

static int rswitch_etha_mii_write(struct mii_bus *bus, int addr, int regnum, u16 val)
{
	struct rswitch_etha *etha = bus->priv;
	int mode, devad, regad;

	mode = regnum & MII_ADDR_C45;
	devad = (regnum >> MII_DEVADDR_C45_SHIFT) & 0x1f;
	regad = regnum & MII_REGADDR_C45_MASK;

	/* Not support Clause 22 access method */
	if (!mode)
		return 0;

	return rswitch_etha_set_access(etha, false, addr, devad, regad, val);
}

static int rswitch_etha_mii_reset(struct mii_bus *bus)
{
	/* TODO */
	return 0;
}

/* Use of_node_put() on it when done */
static struct device_node *rswitch_get_phy_node(struct rswitch_device *rdev)
{
	struct device_node *ports, *port, *phy = NULL;
	int err = 0;
	u32 index;

	ports = of_get_child_by_name(rdev->ndev->dev.parent->of_node, "ports");
	if (!ports)
		return NULL;

	for_each_child_of_node(ports, port) {
		err = of_property_read_u32(port, "reg", &index);
		if (err < 0)
			return NULL;
		if (index != rdev->etha->index)
			continue;

		/* The default is SGMII interface */
		err = of_get_phy_mode(port, &rdev->etha->phy_interface);
		if (err < 0)
			rdev->etha->phy_interface = PHY_INTERFACE_MODE_SGMII;

		pr_info("%s PHY interface = %s", __func__, phy_modes(rdev->etha->phy_interface));

		phy = of_parse_phandle(port, "phy-handle", 0);
		if (phy)
			break;
	}

	of_node_put(ports);

	return phy;
}

static struct device_node *rswitch_get_port_node(struct rswitch_device *rdev)
{
	struct device_node *ports, *port;
	int err = 0;
	u32 index;

	ports = of_get_child_by_name(rdev->ndev->dev.parent->of_node, "ports");
	if (!ports)
		return NULL;

	for_each_child_of_node(ports, port) {
		err = of_property_read_u32(port, "reg", &index);
		if (err < 0)
			return NULL;
		if (index == rdev->etha->index)
			break;
	}

	of_node_put(ports);

	return port;
}

static int rswitch_mii_register(struct rswitch_device *rdev)
{
	struct mii_bus *mii_bus;
	struct device_node *port;
	int err;

	mii_bus = mdiobus_alloc();
	if (!mii_bus)
		return -ENOMEM;

	mii_bus->name = "rswitch_mii";
	sprintf(mii_bus->id, "etha%d", rdev->etha->index);
	mii_bus->priv = rdev->etha;
	mii_bus->read = rswitch_etha_mii_read;
	mii_bus->write = rswitch_etha_mii_write;
	mii_bus->reset = rswitch_etha_mii_reset;
	mii_bus->parent = &rdev->ndev->dev;

	port = rswitch_get_port_node(rdev);
	of_node_get(port);
	err = of_mdiobus_register(mii_bus, port);
	if (err < 0) {
		mdiobus_free(mii_bus);
		goto out;
	}

	rdev->etha->mii = mii_bus;

out:
	of_node_put(port);

	return err;
}

static void rswitch_mii_unregister(struct rswitch_device *rdev)
{
	mdiobus_unregister(rdev->etha->mii);
	mdiobus_free(rdev->etha->mii);
}

static void rswitch_adjust_link(struct net_device *ndev)
{
	struct rswitch_device *rdev = netdev_priv(ndev);
	struct phy_device *phydev = ndev->phydev;

	if (phydev->link != rdev->etha->link) {
		phy_print_status(phydev);
		rdev->etha->link = phydev->link;
	}
}

static int rswitch_phy_init(struct rswitch_device *rdev)
{
	struct device_node *phy;
	struct phy_device *phydev;
	int err = 0;

	phy = rswitch_get_phy_node(rdev);
	if (!phy)
		return -ENOENT;

	phydev = of_phy_connect(rdev->ndev, phy, rswitch_adjust_link, 0,
				rdev->etha->phy_interface);
	if (!phydev) {
		err = -ENOENT;
		goto out;
	}

	phy_attached_info(phydev);

out:
	of_node_put(phy);
	return err;
}

static int rswitch_open(struct net_device *ndev)
{
	struct rswitch_device *rdev = netdev_priv(ndev);
	int err = 0;
	bool phy_started = false;

	napi_enable(&rdev->napi);

	if (rdev->etha) {
		if (!rdev->etha->operated) {
			err = rswitch_etha_hw_init(rdev->etha, ndev->dev_addr);
			if (err < 0)
				goto error;
			err = rswitch_mii_register(rdev);
			if (err < 0)
				goto error;
			err = rswitch_phy_init(rdev);
			if (err < 0)
				goto error;
		}

		phy_start(ndev->phydev);
		phy_started = true;

		if (!rdev->etha->operated) {
			err = rswitch_serdes_init(rdev->etha);
			if (err < 0)
				goto error;
		}

		rdev->etha->operated = true;
	}

	netif_start_queue(ndev);

	/* Enable RX */
	rswitch_modify(rdev->addr, GWTRC0, 0, BIT(rdev->rx_chain->index));

	/* Enable interrupt */
	pr_debug("%s: tx = %d, rx = %d\n", __func__, rdev->tx_chain->index, rdev->rx_chain->index);
	rswitch_enadis_data_irq(rdev->priv, rdev->tx_chain->index, true);
	rswitch_enadis_data_irq(rdev->priv, rdev->rx_chain->index, true);
out:
	return err;

error:
	if (phy_started)
		phy_stop(ndev->phydev);
	napi_disable(&rdev->napi);
	goto out;
};

static int rswitch_stop(struct net_device *ndev)
{
	struct rswitch_device *rdev = netdev_priv(ndev);

	if (rdev->etha) {
		phy_stop(ndev->phydev);
		rswitch_etha_disable_mii(rdev->etha);
	}

	napi_disable(&rdev->napi);

	return 0;
};

static int rswitch_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct rswitch_device *rdev = netdev_priv(ndev);
	int ret = NETDEV_TX_OK;
	int entry;
	dma_addr_t dma_addr;
	struct rswitch_desc *desc;
	unsigned long flags;
	struct rswitch_gwca_chain *c = rdev->tx_chain;

	spin_lock_irqsave(&rdev->lock, flags);

	if (c->cur - c->dirty > c->num_ring - 1) {
		netif_stop_subqueue(ndev, 0);
		ret = NETDEV_TX_BUSY;
		goto out;
	}

	if (skb_put_padto(skb, ETH_ZLEN))
		goto out;

	dma_addr = dma_map_single(ndev->dev.parent, skb->data, skb->len, DMA_TO_DEVICE);
	if (dma_mapping_error(ndev->dev.parent, dma_addr))
		goto out;	/* FIXME */

	entry = c->cur % c->num_ring;
	c->skb[entry] = skb;
	desc = &c->ring[entry];
	desc->dptrl = cpu_to_le32(lower_32_bits(dma_addr));
	desc->dptrh = cpu_to_le32(upper_32_bits(dma_addr));
	desc->info_ds = cpu_to_le16(skb->len);

	/* TODO: TX timestamp */

	skb_tx_timestamp(skb);
	dma_wmb();

	desc->die_dt = DT_FSINGLE | DIE;

	c->cur++;
	rswitch_modify(rdev->addr, GWTRC0, 0, BIT(c->index));

out:
	spin_unlock_irqrestore(&rdev->lock, flags);

	return ret;
}

static struct net_device_stats *rswitch_get_stats(struct net_device *ndev)
{
	return &ndev->stats;
}

static const struct net_device_ops rswitch_netdev_ops = {
	.ndo_open = rswitch_open,
	.ndo_stop = rswitch_stop,
	.ndo_start_xmit = rswitch_start_xmit,
	.ndo_get_stats = rswitch_get_stats,
	.ndo_validate_addr = eth_validate_addr,
//	.ndo_change_mtu = eth_change_mtu,
};

static const struct ethtool_ops rswitch_ethtool_ops = {
};

static const struct of_device_id renesas_eth_sw_of_table[] = {
	{ .compatible = "renesas,etherswitch", },
	{ }
};
MODULE_DEVICE_TABLE(of, renesas_eth_sw_of_table);

static void rswitch_clock_enable(struct rswitch_private *priv)
{
	rs_write32(GENMASK(RSWITCH_NUM_HW, 0) | RCEC_RCE, priv->addr + RCEC);
}

static void rswitch_reset(struct rswitch_private *priv)
{
	rs_write32(RRC_RR, priv->addr + RRC);
	rs_write32(RRC_RR_CLR, priv->addr + RRC);
}

static void rswitch_etha_init(struct rswitch_private *priv, int index)
{
	struct rswitch_etha *etha = &priv->etha[index];

	memset(etha, 0, sizeof(*etha));
	etha->index = index;
	etha->addr = priv->addr + rswitch_etha_offs(index);
	etha->serdes_addr = priv->serdes_addr + index * RSWITCH_SERDES_OFFSET;
}

static int rswitch_gwca_change_mode(struct rswitch_private *priv,
				    enum rswitch_gwca_mode mode)
{
	int ret;

	/* Enable clock */
	if (!rswitch_agent_clock_is_enabled(priv->addr, priv->gwca.index))
		rswitch_agent_clock_ctrl(priv->addr, priv->gwca.index, 1);

	rs_write32(mode, priv->addr + GWMC);

	ret = rswitch_reg_wait(priv->addr, GWMS, GWMS_OPS_MASK, mode);

	/* Disable clock */
	if (mode == GWMC_OPC_DISABLE)
		rswitch_agent_clock_ctrl(priv->addr, priv->gwca.index, 0);

	return ret;
}

static int rswitch_gwca_mcast_table_reset(struct rswitch_private *priv)
{
	rs_write32(GWMTIRM_MTIOG, priv->addr + GWMTIRM);
	return rswitch_reg_wait(priv->addr, GWMTIRM, GWMTIRM_MTR, GWMTIRM_MTR);
}

static int rswitch_gwca_axi_ram_reset(struct rswitch_private *priv)
{
	rs_write32(GWARIRM_ARIOG, priv->addr + GWARIRM);
	return rswitch_reg_wait(priv->addr, GWARIRM, GWARIRM_ARR, GWARIRM_ARR);
}

static int rswitch_gwca_hw_init(struct rswitch_private *priv)
{
	int err;

	err = rswitch_gwca_change_mode(priv, GWMC_OPC_DISABLE);
	if (err < 0)
		return err;
	err = rswitch_gwca_change_mode(priv, GWMC_OPC_CONFIG);
	if (err < 0)
		return err;
	err = rswitch_gwca_mcast_table_reset(priv);
	if (err < 0)
		return err;
	err = rswitch_gwca_axi_ram_reset(priv);
	if (err < 0)
		return err;

	/* Full setting flow */
	rs_write32(GWVCC_VEM_SC_TAG, priv->addr + GWVCC);
	rs_write32(0, priv->addr + GWTTFC);
	rs_write32(lower_32_bits(priv->desc_bat_dma), priv->addr + GWDCBAC1);
	rs_write32(upper_32_bits(priv->desc_bat_dma), priv->addr + GWDCBAC0);
	rs_write32(2048, priv->addr + GWIICBSC);
	rswitch_modify(priv->addr, GWMDNC, 0, GWMDNC_TXDMN(0xf));

	err = rswitch_gwca_change_mode(priv, GWMC_OPC_DISABLE);
	if (err < 0)
		return err;
	err = rswitch_gwca_change_mode(priv, GWMC_OPC_OPERATION);
	if (err < 0)
		return err;

	return 0;
}

static int rswitch_gwca_chain_init(struct net_device *ndev,
				struct rswitch_private *priv,
				struct rswitch_gwca_chain *c,
				bool dir_tx, bool gptp, int num_ring)
{
	int i, bit;
	int index = c->index;	/* Keep the index before memset() */
	struct sk_buff *skb;

	memset(c, 0, sizeof(*c));
	c->index = index;
	c->dir_tx = dir_tx;
	c->gptp = gptp;
	c->num_ring = num_ring;
	c->ndev = ndev;

	c->skb = kcalloc(c->num_ring, sizeof(*c->skb), GFP_KERNEL);
	if (!c->skb)
		return -ENOMEM;

	if (!dir_tx) {
		for (i = 0; i < c->num_ring; i++) {
			skb = dev_alloc_skb(PKT_BUF_SZ + RSWITCH_ALIGN - 1);
			if (!skb)
				goto out;
			skb_reserve(skb, NET_IP_ALIGN);
			c->skb[i] = skb;
		}
	}

	if (gptp)
		c->ts_ring = dma_alloc_coherent(ndev->dev.parent,
				sizeof(struct rswitch_ext_ts_desc) *
				c->num_ring + 1, &c->ring_dma, GFP_KERNEL);
	else
		c->ring = dma_alloc_coherent(ndev->dev.parent,
				sizeof(struct rswitch_desc) *
				c->num_ring + 1, &c->ring_dma, GFP_KERNEL);
	if (!c->ts_ring && !c->ring)
		goto out;

	index = c->index / 32;
	bit = BIT(c->index % 32);
	if (dir_tx)
		priv->gwca.tx_irq_bits[index] |= bit;
	else
		priv->gwca.rx_irq_bits[index] |= bit;

	return 0;

out:
	/* FIXME: free */

	return -ENOMEM;
}

static int rswitch_gwca_chain_format(struct net_device *ndev,
				struct rswitch_private *priv,
				struct rswitch_gwca_chain *c)
{
	struct rswitch_desc *ring;
	struct rswitch_desc *desc;
	int tx_ring_size = sizeof(*ring) * c->num_ring;
	int i;
	dma_addr_t dma_addr;

	memset(c->ring, 0, tx_ring_size);
	for (i = 0, ring = c->ring; i < c->num_ring; i++, ring++) {
		if (!c->dir_tx) {
			dma_addr = dma_map_single(ndev->dev.parent,
					c->skb[i]->data, PKT_BUF_SZ,
					DMA_FROM_DEVICE);
			if (!dma_mapping_error(ndev->dev.parent, dma_addr))
				ring->info_ds = cpu_to_le16(PKT_BUF_SZ);
			ring->dptrl = cpu_to_le32(lower_32_bits(dma_addr));
			ring->dptrh = cpu_to_le32(upper_32_bits(dma_addr));
			ring->die_dt = DT_FEMPTY | DIE;
		} else {
			ring->die_dt = DT_EEMPTY | DIE;
		}
	}
	ring->dptrl = cpu_to_le32(lower_32_bits(c->ring_dma));
	ring->dptrh = cpu_to_le32(upper_32_bits(c->ring_dma));
	ring->die_dt = DT_LINKFIX;

	desc = &priv->desc_bat[c->index];
	desc->die_dt = DT_LINKFIX;
	desc->dptrl = cpu_to_le32(lower_32_bits(c->ring_dma));
	desc->dptrh = cpu_to_le32(upper_32_bits(c->ring_dma));

	/* FIXME: GWDCC_DCP */
	rs_write32(GWDCC_BALR | (c->dir_tx ? GWDCC_DQT : 0),
		  priv->addr + GWDCC_OFFS(c->index));

	return 0;
}

#if 0
static int rswitch_gwca_chain_ts_format(struct net_device *ndev,
				struct rswitch_private *priv,
				struct rswitch_gwca_chain *c)
{
	struct rswitch_ext_ts_desc *ts_ring;
	struct rswitch_desc *desc;
	int tx_ts_ring_size = sizeof(*ts_ring) * c->num_ring;
	int i;
	dma_addr_t dma_addr;

	memset(c->ts_ring, 0, tx_ts_ring_size);
	for (i = 0, ts_ring = c->ts_ring; i < c->num_ring; i++, ts_ring++) {
		if (!c->dir_tx) {
			dma_addr = dma_map_single(ndev->dev.parent,
					c->skb[i]->data, PKT_BUF_SZ,
					DMA_FROM_DEVICE);
			if (!dma_mapping_error(ndev->dev.parent, dma_addr))
				ts_ring->info_ds = cpu_to_le16(PKT_BUF_SZ);
			ts_ring->dptrl = cpu_to_le32(lower_32_bits(dma_addr));
			ts_ring->dptrh = cpu_to_le32(upper_32_bits(dma_addr));
		}
		ts_ring->die_dt = DT_EEMPTY;
	}
	ts_ring->dptrl = cpu_to_le32(lower_32_bits(c->ring_dma));
	ts_ring->dptrh = cpu_to_le32(upper_32_bits(c->ring_dma));
	ts_ring->die_dt = DT_LINKFIX;

	desc = &priv->desc_bat[c->index];
	desc->die_dt = DT_LINKFIX;
	desc->dptrl = cpu_to_le32(lower_32_bits(c->ring_dma));
	desc->dptrh = cpu_to_le32(upper_32_bits(c->ring_dma));

	/* FIXME: GWDCC_DCP */
	rs_write32(GWDCC_BALR | (c->dir_tx ? GWDCC_DQT : 0) | GWDCC_ETS | GWDCC_EDE,
		  priv->addr + GWDCC_OFFS(c->index));

	return 0;
}
#endif

static int rswitch_desc_init(struct rswitch_private *priv)
{
	struct device *dev = &priv->pdev->dev;
	int i, num_chains = priv->gwca.num_chains;

	priv->desc_bat_size = sizeof(struct rswitch_desc) * num_chains;
	priv->desc_bat = dma_alloc_coherent(dev, priv->desc_bat_size,
					    &priv->desc_bat_dma, GFP_KERNEL);
	if (!priv->desc_bat)
		return -ENOMEM;
	for (i = 0; i < num_chains; i++)
		priv->desc_bat[i].die_dt = DT_EOS;

	return 0;
}

static struct rswitch_gwca_chain *rswitch_gwca_get(struct rswitch_private *priv)
{
	int index;

	index = find_first_zero_bit(priv->gwca.used, priv->gwca.num_chains);
	if (index >= priv->gwca.num_chains)
		return NULL;
	set_bit(index, priv->gwca.used);
	priv->gwca.chains[index].index = index;

	return &priv->gwca.chains[index];
}

static int rswitch_txdmac_init(struct net_device *ndev,
			       struct rswitch_private *priv)
{
	struct rswitch_device *rdev = netdev_priv(ndev);
	int err;

	rdev->tx_chain = rswitch_gwca_get(priv);
	if (!rdev->tx_chain)
		return -EBUSY;

	err = rswitch_gwca_chain_init(ndev, priv, rdev->tx_chain, true, false,
				      TX_RING_SIZE);
	if (err < 0)
		goto out;

	err = rswitch_gwca_chain_format(ndev, priv, rdev->tx_chain);
	if (err < 0)
		goto out;

	return 0;

out:
	/* FIXME: free */
	return err;
}

static int rswitch_rxdmac_init(struct net_device *ndev,
			       struct rswitch_private *priv)
{
	struct rswitch_device *rdev = netdev_priv(ndev);
	int err;

	rdev->rx_chain = rswitch_gwca_get(priv);
	if (!rdev->rx_chain)
		return -EBUSY;

	err = rswitch_gwca_chain_init(ndev, priv, rdev->rx_chain, false, true,
				      RX_RING_SIZE);
	if (err < 0)
		goto out;

	err = rswitch_gwca_chain_format(ndev, priv, rdev->rx_chain);
	if (err < 0)
		goto out;

	return 0;

out:
	/* FIXME: free */
	return err;
}

static int rswitch_ndev_register(struct rswitch_private *priv, int index)
{
	struct platform_device *pdev = priv->pdev;
	struct net_device *ndev;
	struct rswitch_device *rdev;
	int err;
	const u8 *mac;

	ndev = alloc_etherdev_mqs(sizeof(struct rswitch_device), 1, 1);
	if (!ndev)
		return -ENOMEM;

	SET_NETDEV_DEV(ndev, &pdev->dev);
	ether_setup(ndev);

	rdev = netdev_priv(ndev);
	rdev->ndev = ndev;
	rdev->priv = priv;
	/* TODO: netdev instance : ETHA port is 1:1 mapping */
	if (index < RSWITCH_MAX_NUM_ETHA) {
		rdev->port = index;
		rdev->etha = &priv->etha[index];
	} else {
		rdev->port = -1;
		rdev->etha = NULL;
	}
	rdev->addr = priv->addr;

	spin_lock_init(&rdev->lock);

	ndev->features = NETIF_F_RXCSUM;
	ndev->hw_features = NETIF_F_RXCSUM;
	ndev->base_addr = (unsigned long)rdev->addr;
	snprintf(ndev->name, IFNAMSIZ, "tsn%d", index);
	ndev->netdev_ops = &rswitch_netdev_ops;
	ndev->ethtool_ops = &rswitch_ethtool_ops;

	netif_napi_add(ndev, &rdev->napi, rswitch_poll, 64);

	mac = of_get_mac_address(pdev->dev.of_node);
	if (!IS_ERR(mac))
		ether_addr_copy(ndev->dev_addr, mac);

	if (!is_valid_ether_addr(ndev->dev_addr))
		ether_addr_copy(ndev->dev_addr, rdev->etha->mac_addr);

	if (!is_valid_ether_addr(ndev->dev_addr))
		eth_hw_addr_random(ndev);

	/* Network device register */
	err = register_netdev(ndev);
	if (err)
		goto out_napi_del;

	/* FIXME: it seems S4 VPF has FWPBFCSDC0/1 only so that we cannot set
	 * CSD = 1 (rx_chain->index = 1) for FWPBFCS03. So, use index = 0
	 * for the RX.
	 */
	err = rswitch_rxdmac_init(ndev, priv);
	if (err < 0)
		goto out_txdmac;

	err = rswitch_txdmac_init(ndev, priv);
	if (err < 0)
		goto out_register;

	/* Print device information */
	netdev_info(ndev, "MAC address %pMn", ndev->dev_addr);

	return 0;

out_txdmac:
	/* FIXME */

out_register:
	/* FIXME */

out_napi_del:
	netif_napi_del(&rdev->napi);

#if 0
out_release:
#endif
	return err;
}

static int rswitch_bpool_config(struct rswitch_private *priv)
{
	u32 val;

	val = rs_read32(priv->addr + CABPIRM);
	if (val & CABPIRM_BPR)
		return 0;

	rs_write32(CABPIRM_BPIOG, priv->addr + CABPIRM);
	return rswitch_reg_wait(priv->addr, CABPIRM, CABPIRM_BPR, CABPIRM_BPR);
}

static void rswitch_queue_interrupt(struct net_device *ndev)
{
	struct rswitch_device *rdev = netdev_priv(ndev);

	if (napi_schedule_prep(&rdev->napi)) {
		rswitch_enadis_data_irq(rdev->priv, rdev->tx_chain->index, false);
		rswitch_enadis_data_irq(rdev->priv, rdev->rx_chain->index, false);
		__napi_schedule(&rdev->napi);
	}
}

static irqreturn_t __maybe_unused rswitch_data_irq(struct rswitch_private *priv, u32 *dis)
{
	struct rswitch_gwca_chain *c;
	int i;
	int index, bit;

	for (i = 0; i < priv->gwca.num_chains; i++) {
		c = &priv->gwca.chains[i];
		index = c->index / 32;
		bit = BIT(c->index % 32);
		if (!(dis[index] & bit))
			continue;

		rswitch_ack_data_irq(priv, c->index);
		rswitch_queue_interrupt(c->ndev);
	}

	return IRQ_HANDLED;
}

static irqreturn_t rswitch_irq(int irq, void *dev_id)
{
	struct rswitch_private *priv = dev_id;
	irqreturn_t ret = IRQ_NONE;
	u32 dis[RSWITCH_NUM_IRQ_REGS];

	rswitch_get_data_irq_status(priv, dis);

	if (rswitch_is_any_data_irq(priv, dis, true) ||
	    rswitch_is_any_data_irq(priv, dis, false))
		ret = rswitch_data_irq(priv, dis);

	return ret;
}

static int rswitch_request_irqs(struct rswitch_private *priv)
{
	int irq, err;

	/* FIXME: other queues */
	irq = platform_get_irq_byname(priv->pdev, "gwca0_rxtx0");
	if (irq < 0)
		goto out;

	err = request_irq(irq, rswitch_irq, 0, "rswitch: gwca0_rxtx0", priv);
	if (err < 0)
		goto out;

out:
	return err;
}

static void rswitch_fwd_init(struct rswitch_private *priv)
{
	int i;

	for (i = 0; i < RSWITCH_NUM_HW; i++) {
		rs_write32(FWPC0_DEFAULT, priv->addr + FWPC00 + (i * 0x10));
		rs_write32(0, priv->addr + FWPBFC(i));
	}
	/*
	 * FIXME: hardcoded setting. Make a macro about port vector calc.
	 * ETHA0 = forward to GWCA0, GWCA0 = forward to ETHA0.
	 * others = disabled
	 */
	rs_write32(8, priv->addr + FWPBFC(0));
	rs_write32(1, priv->addr + FWPBFC(3));

	/* TODO: add chrdev for fwd */
	/* TODO: add proc for fwd */
}

static int rswitch_init(struct rswitch_private *priv)
{
	int i;
	int err;

	/* Non hardware initializations */
	for (i = 0; i < num_etha_ports; i++)
		rswitch_etha_init(priv, i);

	err = rswitch_desc_init(priv);
	if (err < 0)
		goto out;

	/* Hardware initializations */
	rswitch_clock_enable(priv);
	for (i = 0; i < num_ndev; i++)
		rswitch_etha_read_mac_address(&priv->etha[i]);
	rswitch_reset(priv);
	err = rswitch_gwca_hw_init(priv);
	if (err < 0)
		goto out;

	for (i = 0; i < num_ndev; i++) {
		err = rswitch_ndev_register(priv, i);
		if (err < 0)
			goto out;
	}

	/* TODO: chrdev register */

	err = rswitch_bpool_config(priv);
	if (err < 0)
		goto out;

	rswitch_fwd_init(priv);

	err = rswitch_request_irqs(priv);
	if (err < 0)
		goto out;

	return 0;

out:
	/* FIXME: free memories */

	return err;
}

static int renesas_eth_sw_probe(struct platform_device *pdev)
{
	struct rswitch_private *priv;
	struct resource *res, *res_serdes;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	res_serdes = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!res || !res_serdes) {
		dev_err(&pdev->dev, "invalid resource\n");
		return -EINVAL;
	}

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->rsw_clk = devm_clk_get(&pdev->dev, "rsw2");
	if (IS_ERR(priv->rsw_clk)) {
		dev_err(&pdev->dev, "Failed to get rsw2 clock: %ld\n", PTR_ERR(priv->rsw_clk));
		return -PTR_ERR(priv->rsw_clk);
	}

	priv->phy_clk = devm_clk_get(&pdev->dev, "eth-phy");
	if (IS_ERR(priv->phy_clk)) {
		dev_err(&pdev->dev, "Failed to get eth-phy clock: %ld\n", PTR_ERR(priv->phy_clk));
		return -PTR_ERR(priv->phy_clk);
	}

	platform_set_drvdata(pdev, priv);
	priv->pdev = pdev;
	priv->addr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(priv->addr))
		return PTR_ERR(priv->addr);

	priv->serdes_addr = devm_ioremap_resource(&pdev->dev, res_serdes);
	if (IS_ERR(priv->serdes_addr))
		return PTR_ERR(priv->serdes_addr);

	debug_addr = priv->addr;

	/* Fixed to use GWCA0 */
	priv->gwca.index = 3;
	priv->gwca.num_chains = num_ndev * NUM_CHAINS_PER_NDEV;
	priv->gwca.chains = devm_kcalloc(&pdev->dev, priv->gwca.num_chains,
					 sizeof(*priv->gwca.chains), GFP_KERNEL);
	if (!priv->gwca.chains)
		return -ENOMEM;

	pm_runtime_enable(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);
	clk_prepare(priv->phy_clk);
	clk_enable(priv->phy_clk);

	rswitch_init(priv);

	device_set_wakeup_capable(&pdev->dev, 1);

	return 0;
}

static int renesas_eth_sw_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct rswitch_device *rdev = netdev_priv(ndev);
	struct rswitch_private *priv = rdev->priv;

	/* Disable R-Switch clock */
	rs_write32(RCDC_RCD, rdev->priv->addr + RCDC);

	pm_runtime_put(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	clk_disable(priv->phy_clk);

	dma_free_coherent(ndev->dev.parent, priv->desc_bat_size, priv->desc_bat,
			  priv->desc_bat_dma);

	if (rdev->etha && rdev->etha->operated)
		rswitch_mii_unregister(rdev);

	unregister_netdev(ndev);
	netif_napi_del(&rdev->napi);
	free_netdev(ndev);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_driver renesas_eth_sw_driver_platform = {
	.probe = renesas_eth_sw_probe,
	.remove = renesas_eth_sw_remove,
	.driver = {
		.name = "renesas_eth_sw",
		.of_match_table = renesas_eth_sw_of_table,
	}
};
module_platform_driver(renesas_eth_sw_driver_platform);
MODULE_AUTHOR("Yoshihiro Shimoda");
MODULE_DESCRIPTION("Renesas Ethernet Switch device driver");
MODULE_LICENSE("GPL v2");
