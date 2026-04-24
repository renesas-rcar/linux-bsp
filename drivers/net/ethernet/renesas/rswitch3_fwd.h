/* SPDX-License-Identifier: GPL-2.0 */
/* Renesas Ethernet Switch device driver
 *
 * Copyright (C) 2026 Renesas Electronics Corporation
 */

#ifndef __RSWITCH3_FWD_H__
#define __RSWITCH3_FWD_H__

#include <linux/platform_device.h>

enum rswitch3_reg {
	RSW3_FWGC		= 0x0000,
	RSW3_FWTTC0		= 0x0010,
	RSW3_FWTTC1		= 0x0014,
	RSW3_FWCEPTC		= 0x0020,
	RSW3_FWCEPRC0		= 0x0024,
	RSW3_FWCEPRC1		= 0x0028,
	RSW3_FWCEPRC2		= 0x002C,
	RSW3_FWCLPTC		= 0x0030,
	RSW3_FWCLPRC		= 0x0034,
	RSW3_FWCMPTC		= 0x0040,
	RSW3_FWCMPL23URC	= 0x0044,
	RSW3_FWEMPTC		= 0x0048,
	RSW3_FWEMPL23URC	= 0x004C,
	RSW3_FWSDMPTC		= 0x0050,
	RSW3_FWSDMPVC		= 0x0054,
	RSW3_FWSDMPL23URC	= 0x0058,
	RSW3_FWSMPTC		= 0x0060,
	RSW3_FWSMPVC		= 0x0064,
	RSW3_FWSMPL23URC	= 0x0068,
	RSW3_FWLBWMC0		= 0x0080,
	RSW3_FWIBWMC		= 0x00C0,
	RSW3_FWPC00		= 0x0100,
	RSW3_FWPC10		= 0x0104,
	RSW3_FWPC20		= 0x0108,
	RSW3_FWPC30		= 0x010C,
	RSW3_FWPIFP000		= 0x0200,
	RSW3_FWCTGC00		= 0x0400,
	RSW3_FWCTGC10		= 0x0404,
	RSW3_FWCTTC00		= 0x0408,
	RSW3_FWCTTC10		= 0x040C,
	RSW3_FWCTTC200		= 0x0410,
	RSW3_FWCTSC00		= 0x0450,
	RSW3_FWCTSC10		= 0x0454,
	RSW3_FWCTSC20		= 0x0458,
	RSW3_FWCTSC30		= 0x045C,
	RSW3_FWCTSC40		= 0x0460,
	RSW3_FWICETC10		= 0x0800,
	RSW3_FWIC0P4C0		= 0x0804,
	RSW3_FWIP4AC0		= 0x0808,
	RSW3_FWIC0P6C0		= 0x080C,
	RSW3_FWIP6AC00		= 0x0810,
	RSW3_FWIP6AC10		= 0x0814,
	RSW3_FWIP6AC20		= 0x0818,
	RSW3_FWIP6AC30		= 0x081C,
	RSW3_FWIP4APC0		= 0x0820,
	RSW3_FWIP6APC0		= 0x0824,
	RSW3_FWICETC20		= 0x1000,
	RSW3_FWICETC300		= 0x1010,
	RSW3_FWIP4FAC00		= 0x1050,
	RSW3_FWIP4SFC00		= 0x1070,
	RSW3_FWIP6FAC000	= 0x1090,
	RSW3_FWIP6FAC100	= 0x10B0,
	RSW3_FWIP6FAC200	= 0x10D0,
	RSW3_FWIP6FAC300	= 0x10F0,
	RSW3_FWIP6SFC00		= 0x1110,
	RSW3_FWIP4TLCC0		= 0x1130,
	RSW3_FWIP6PLCC0		= 0x1134,
	RSW3_FWICL4C0		= 0x1140,
	RSW3_FWICL4THLC0	= 0x1144,
	RSW3_FWIP4SC		= 0x4008,
	RSW3_FWIP6SC		= 0x4018,
	RSW3_FWIP6OC		= 0x401C,
	RSW3_FWL2SC		= 0x4020,
	RSW3_FWSFHEC		= 0x4030,
	RSW3_FWSHCR0		= 0x4040,
	RSW3_FWSHCR1		= 0x4044,
	RSW3_FWSHCR2		= 0x4048,
	RSW3_FWSHCR3		= 0x404C,
	RSW3_FWSHCR4		= 0x4050,
	RSW3_FWSHCR5		= 0x4054,
	RSW3_FWSHCR6		= 0x4058,
	RSW3_FWSHCR7		= 0x405C,
	RSW3_FWSHCR8		= 0x4060,
	RSW3_FWSHCR9		= 0x4064,
	RSW3_FWSHCR10		= 0x4068,
	RSW3_FWSHCR11		= 0x406C,
	RSW3_FWSHCR12		= 0x4070,
	RSW3_FWSHCR13		= 0x4074,
	RSW3_FWSHCRR		= 0x4078,
	RSW3_FWLTHTEC0		= 0x4090,
	RSW3_FWLTHTEC1		= 0x4094,
	RSW3_FWLTHTL0		= 0x40A0,
	RSW3_FWLTHTL1		= 0x40A4,
	RSW3_FWLTHTL2		= 0x40A8,
	RSW3_FWLTHTL3		= 0x40AC,
	RSW3_FWLTHTL4		= 0x40B0,
	RSW3_FWLTHTL5		= 0x40B4,
	RSW3_FWLTHTL6		= 0x40B8,
	RSW3_FWLTHTL7		= 0x40BC,
	RSW3_FWLTHTL8		= 0x40C0,
	RSW3_FWLTHTL9		= 0x40C4,
	RSW3_FWLTHTL10		= 0x40C8,
	RSW3_FWLTHTL11		= 0x40CC,
	RSW3_FWLTHTL12		= 0x40D0,
	RSW3_FWLTHTL130		= 0x40D4,
	RSW3_FWLTHTL14		= 0x4114,
	RSW3_FWLTHTL15		= 0x4118,
	RSW3_FWLTHTLR		= 0x411C,
	RSW3_FWLTHTIM		= 0x4120,
	RSW3_FWLTHTEM0		= 0x4124,
	RSW3_FWLTHTEM1		= 0x4128,
	RSW3_FWLTHTS0		= 0x4130,
	RSW3_FWLTHTS1		= 0x4134,
	RSW3_FWLTHTS2		= 0x4138,
	RSW3_FWLTHTS3		= 0x413C,
	RSW3_FWLTHTS4		= 0x4140,
	RSW3_FWLTHTS5		= 0x4144,
	RSW3_FWLTHTS6		= 0x4148,
	RSW3_FWLTHTSR0		= 0x4150,
	RSW3_FWLTHTSR1		= 0x4154,
	RSW3_FWLTHTSR2		= 0x4158,
	RSW3_FWLTHTSR3		= 0x415C,
	RSW3_FWLTHTSR40		= 0x4160,
	RSW3_FWLTHTSR5		= 0x41A0,
	RSW3_FWLTHTSR6		= 0x41A4,
	RSW3_FWLTHTSR7		= 0x41A8,
	RSW3_FWLTHTR		= 0x41B0,
	RSW3_FWLTHTRR0		= 0x41B4,
	RSW3_FWLTHTRR1		= 0x41B8,
	RSW3_FWLTHTRR2		= 0x41BC,
	RSW3_FWLTHTRR3		= 0x41C0,
	RSW3_FWLTHTRR4		= 0x41C4,
	RSW3_FWLTHTRR5		= 0x41C8,
	RSW3_FWLTHTRR6		= 0x41CC,
	RSW3_FWLTHTRR7		= 0x41D0,
	RSW3_FWLTHTRR8		= 0x41D4,
	RSW3_FWLTHTRR9		= 0x41D8,
	RSW3_FWLTHTRR10		= 0x41DC,
	RSW3_FWLTHTRR11		= 0x41E0,
	RSW3_FWLTHTRR12		= 0x41E4,
	RSW3_FWLTHTRR130	= 0x41E8,
	RSW3_FWLTHTRR14		= 0x4218,
	RSW3_FWLTHTRR15		= 0x421C,
	RSW3_FWLTHREUSPC	= 0x4300,
	RSW3_FWLTHREC		= 0x4304,
	RSW3_FWLTHREM		= 0x4308,
	RSW3_FWMACTEC0		= 0x4600,
	RSW3_FWMACTL0		= 0x4610,
	RSW3_FWMACTL1		= 0x4614,
	RSW3_FWMACTL2		= 0x4618,
	RSW3_FWMACTL3		= 0x461C,
	RSW3_FWMACTL4		= 0x4620,
	RSW3_FWMACTL5		= 0x4624,
	RSW3_FWMACTL6		= 0x4628,
	RSW3_FWMACTL70		= 0x462C,
	RSW3_FWMACTL8		= 0x466C,
	RSW3_FWMACTLR		= 0x4670,
	RSW3_FWMACTIM		= 0x4680,
	RSW3_FWMACTEM		= 0x4684,
	RSW3_FWMACTS0		= 0x4690,
	RSW3_FWMACTS1		= 0x4694,
	RSW3_FWMACTS2		= 0x4698,
	RSW3_FWMACTS3		= 0x469C,
	RSW3_FWMACTSR0		= 0x46A0,
	RSW3_FWMACTSR1		= 0x46A4,
	RSW3_FWMACTSR20		= 0x46A8,
	RSW3_FWMACTSR3		= 0x46E8,
	RSW3_FWMACTSR4		= 0x46F0,
	RSW3_FWMACTSR5		= 0x46F4,
	RSW3_FWMACTSR6		= 0x46F8,
	RSW3_FWMACTR		= 0x4700,
	RSW3_FWMACTRR0		= 0x4710,
	RSW3_FWMACTRR1		= 0x4714,
	RSW3_FWMACTRR2		= 0x4718,
	RSW3_FWMACTRR3		= 0x471C,
	RSW3_FWMACTRR4		= 0x4720,
	RSW3_FWMACTRR5		= 0x4724,
	RSW3_FWMACTRR6		= 0x4728,
	RSW3_FWMACTRR70		= 0x472C,
	RSW3_FWMACTRR8		= 0x476C,
	RSW3_FWMACHWLC0		= 0x4800,
	RSW3_FWMACHWLC1		= 0x4804,
	RSW3_FWMACHWLC20	= 0x4810,
	RSW3_FWMACAGUSPC	= 0x4880,
	RSW3_FWMACAGC		= 0x4884,
	RSW3_FWMACAGM0		= 0x4888,
	RSW3_FWMACAGM1		= 0x488C,
	RSW3_FWMACREUSPC	= 0x4890,
	RSW3_FWMACREC		= 0x4894,
	RSW3_FWMACREM		= 0x4898,
	RSW3_FWVLANTEC		= 0x4900,
	RSW3_FWVLANTL0		= 0x4910,
	RSW3_FWVLANTL1		= 0x4914,
	RSW3_FWVLANTL2		= 0x4918,
	RSW3_FWVLANTL3		= 0x491C,
	RSW3_FWVLANTL4		= 0x4920,
	RSW3_FWVLANTL5		= 0x4924,
	RSW3_FWVLANTL60		= 0x4928,
	RSW3_FWVLANTL7		= 0x4968,
	RSW3_FWVLANTLR		= 0x496C,
	RSW3_FWVLANT0M		= 0x4970,
	RSW3_FWVLANTEM		= 0x4974,
	RSW3_FWVLANTS		= 0x4980,
	RSW3_FWVLANTSR0		= 0x4984,
	RSW3_FWVLANTSR1		= 0x4988,
	RSW3_FWVLANTSR2		= 0x498C,
	RSW3_FWVLANTSR3		= 0x4990,
	RSW3_FWVLANTSR4		= 0x4994,
	RSW3_FWVLANTSR50	= 0x4998,
	RSW3_FWVLANTSR6		= 0x49C8,
	RSW3_FWPBFC00		= 0x4A00,
	RSW3_FWPBFC10		= 0x4A04,
	RSW3_FWPBFCSDC00	= 0x4B00,
	RSW3_FWL23URL0		= 0x4E00,
	RSW3_FWL23URL1		= 0x4E04,
	RSW3_FWL23URL2		= 0x4E08,
	RSW3_FWL23URL3		= 0x4E0C,
	RSW3_FWL23URLR		= 0x4E10,
	RSW3_FWL23UTIM		= 0x4E20,
	RSW3_FWL23URR		= 0x4E30,
	RSW3_FWL23URRR0		= 0x4E34,
	RSW3_FWL23URRR1		= 0x4E38,
	RSW3_FWL23URRR2		= 0x4E3C,
	RSW3_FWL23URRR3		= 0x4E40,
	RSW3_FWL23URMC0		= 0x4F00,
	RSW3_FWPMFGC0		= 0x5000,
	RSW3_FWPGFC0		= 0x5100,
	RSW3_FWPGF0GSC0		= 0x5104,
	RSW3_FWPGFENC0		= 0x5108,
	RSW3_FWPGFENM0		= 0x510C,
	RSW3_FWPGFCSTC00	= 0x5110,
	RSW3_FWPGFCSTC10	= 0x5114,
	RSW3_FWPGFCSTM00	= 0x5118,
	RSW3_FWPGFCSTM10	= 0x511C,
	RSW3_FWPGFCTC0		= 0x5120,
	RSW3_FWPGFCTM0		= 0x5124,
	RSW3_FWPGFHCC0		= 0x5128,
	RSW3_FWPGFSM0		= 0x512C,
	RSW3_FWPGFGC0		= 0x5130,
	RSW3_FWPGFGL0		= 0x5500,
	RSW3_FWPGFGL1		= 0x5504,
	RSW3_FWPGFGLR		= 0x5508,
	RSW3_FWPGFGR		= 0x5510,
	RSW3_FWPGFGRR0		= 0x5514,
	RSW3_FWPGFGRR1		= 0x5518,
	RSW3_FWPGFR0M		= 0x5520,
	RSW3_FWPMTRFC0		= 0x18000,
	RSW3_FWPMTRCBSC0	= 0x18004,
	RSW3_FWPMTRC0RC0	= 0x18008,
	RSW3_FWPMTREBSC0	= 0x1800C,
	RSW3_FWPMTRE0RC0	= 0x18010,
	RSW3_FWPMTRFM0		= 0x18014,
	RSW3_FWFTL0		= 0x6000,
	RSW3_FWFTL1		= 0x6004,
	RSW3_FWFTLR		= 0x6008,
	RSW3_FWFTOC		= 0x6010,
	RSW3_FWFTOPC		= 0x6014,
	RSW3_FWFT0M		= 0x6020,
	RSW3_FWFTR		= 0x6030,
	RSW3_FWFTRR0		= 0x6034,
	RSW3_FWFTRR1		= 0x6038,
	RSW3_FWFTRR2		= 0x603C,
	RSW3_FWSEQNGC0		= 0x6100,
	RSW3_FWSEQNGM0		= 0x6104,
	RSW3_FWSEQNRC		= 0x6200,
	RSW3_FWCTFDCN0		= 0x6300,
	//0x6E00
	RSW3_FWDDFDCN0		= 0x6300,
	//0x6E00
	RSW3_FWLTHFDCN0		= 0x6304,
	//0x6E04
	RSW3_FWLTWFDCN0		= 0x630C,
	//0x6E0C
	RSW3_FWPBFDCN0		= 0x6310,
	//0x6E10
	RSW3_FWMHLCN0		= 0x6314,
	//0x6E14
	RSW3_FWICRDCN0		= 0x6500,
	//0x7000,
	RSW3_FWWMRDCN0		= 0x6504,
	//0x7004,
	RSW3_FWCTRDCN0		= 0x6508,
	//0x7008,
	RSW3_FWDDRDCN0		= 0x6508,
	//0x7008
	RSW3_FWLTRDCN0		= 0x650C,
	//0x700C,
	RSW3_FWLTWRDCN0		= 0x6514,
	//0x7014,
	RSW3_FWPBRDCN0		= 0x6518,
	//0x7018,
	RSW3_FWPMFDCN0		= 0x6700,
	//0x7200,
	RSW3_FWPGFDCN0		= 0x6780,
	//0x7280,
	RSW3_FWPMGDCN0		= 0x19000,
	//0x1A000,
	RSW3_FWPMYDCN0		= 0x19004,
	//0x1A004,
	RSW3_FWPMRDCN0		= 0x19008,
	//0x1A008,
	RSW3_FWFRPPCN0		= 0x6A00,
	//0x7500,
	RSW3_FWFRDPCN0		= 0x6A04,
	//0x7504,
	RSW3_FWBLFCN0		= 0x16000,
	//0x17000,
	RSW3_FWALFCN0		= 0x1600C,
	//0x1700C,
	RSW3_FWE0S00		= 0x7900,
	RSW3_FWE0E00		= 0x7904,
	RSW3_FWE0D00		= 0x7908,
	RSW3_FWEIS1		= 0x7A00,
	RSW3_FWEIE1		= 0x7A04,
	RSW3_FWEID1		= 0x7A08,
	RSW3_FWEIS2		= 0x7A10,
	RSW3_FWEIE2		= 0x7A14,
	RSW3_FWEID2		= 0x7A18,
	RSW3_FWEIS3		= 0x7A20,
	RSW3_FWEIE3		= 0x7A24,
	RSW3_FWEID3		= 0x7A28,
	RSW3_FWEIS4		= 0x7A30,
	RSW3_FWEIE4		= 0x7A34,
	RSW3_FWEID4		= 0x7A38,
	RSW3_FWEIS50		= 0x7A40,
	RSW3_FWEIE50		= 0x7A44,
	RSW3_FWEID50		= 0x7A48,
	RSW3_FWEIS51		= 0x7A50,
	RSW3_FWEIE51		= 0x7A54,
	RSW3_FWEID51		= 0x7A58,
	RSW3_FWEIS52		= 0x7A60,
	RSW3_FWEIE52		= 0x7A64,
	RSW3_FWEID52		= 0x7A68,
	RSW3_FWEIS53		= 0x7A70,
	RSW3_FWEIE53		= 0x7A74,
	RSW3_FWEID53		= 0x7A78,
	RSW3_FWEIS60		= 0x7A80,
	RSW3_FWEIE60		= 0x7A84,
	RSW3_FWEID60		= 0x7A88,
	RSW3_FWEIS61		= 0x7A90,
	RSW3_FWEIE61		= 0x7A94,
	RSW3_FWEID61		= 0x7A98,
	RSW3_FWEIS62		= 0x7AA0,
	RSW3_FWEIE62		= 0x7AA4,
	RSW3_FWEID62		= 0x7AA8,
	RSW3_FWEIS63		= 0x7AB0,
	RSW3_FWEIE63		= 0x7AB4,
	RSW3_FWEID63		= 0x7AB8,
	RSW3_FWEIS70		= 0x7AC0,
	RSW3_FWEIE70		= 0x7AC4,
	RSW3_FWEID70		= 0x7AC8,
	RSW3_FWEIS71		= 0x7AD0,
	RSW3_FWEIE71		= 0x7AD4,
	RSW3_FWEID71		= 0x7AD8,
	RSW3_FWEIS72		= 0x7AE0,
	RSW3_FWEIE72		= 0x7AE4,
	RSW3_FWEID72		= 0x7AE8,
	RSW3_FWEIS73		= 0x7AF0,
	RSW3_FWEIE73		= 0x7AF4,
	RSW3_FWEID73		= 0x7AF8,
	RSW3_FWEIS80		= 0x7B00,
	RSW3_FWEIE80		= 0x7B04,
	RSW3_FWEID80		= 0x7B08,
	RSW3_FWEIS81		= 0x7B10,
	RSW3_FWEIE81		= 0x7B14,
	RSW3_FWEID81		= 0x7B18,
	RSW3_FWEIS82		= 0x7B20,
	RSW3_FWEIE82		= 0x7B24,
	RSW3_FWEID82		= 0x7B28,
	RSW3_FWEIS83		= 0x7B30,
	RSW3_FWEIE83		= 0x7B34,
	RSW3_FWEID83		= 0x7B38,
	RSW3_FWMIS0		= 0x7C00,
	RSW3_FWMIE0		= 0x7C04,
	RSW3_FWMID0		= 0x7C08,
	RSW3_FWSCR0		= 0x7D00,
	RSW3_FWSCR1		= 0x7D04,
	RSW3_FWSCR2		= 0x7D08,
	RSW3_FWSCR3		= 0x7D0C,
	RSW3_FWSCR4		= 0x7D10,
	RSW3_FWSCR21		= 0x7D54,
	RSW3_FWSCR22		= 0x7D58,
	RSW3_FWSCR23		= 0x7D5C,
	RSW3_FWSCR24		= 0x7D60,
	RSW3_FWSCR25		= 0x7D64,
	RSW3_FWSCR26		= 0x7D68,
	RSW3_FWSCR27		= 0x7D6C,
	RSW3_FWSCR28		= 0x7D70,
	RSW3_FWSCR29		= 0x7D74,
	RSW3_FWSCR30		= 0x7D78,
	RSW3_FWSCR31		= 0x7D7C,
	RSW3_FWSCR32		= 0x7D80,
	RSW3_FWSCR33		= 0x7D84,
	RSW3_FWSCR34		= 0x7D88,
	RSW3_FWSCR35		= 0x7D8C,
	RSW3_FWSCR36		= 0x7D90,
	RSW3_FWSCR37		= 0x7D94,
	RSW3_FWSCR38		= 0x7D98,
	RSW3_FWSCR39		= 0x7D9C,
	RSW3_FWSCR40		= 0x7DA0,
	RSW3_FWSCR41		= 0x7DA4,
	RSW3_FWSCR42		= 0x7DA8,
	RSW3_FWSCR43		= 0x7DAC,
	RSW3_FWSCR44		= 0x7DB0,
	RSW3_FWSCR45		= 0x7DB4,
	RSW3_FWSCR46		= 0x7DB8,
	RSW3_FWSCR47		= 0x7DBC,
	RSW3_FWSCR48		= 0x7DC0,
	RSW3_FWSCR49		= 0x7DC4,
	RSW3_FWSCRTO0		= 0x7E00,
	RSW3_FWSCRTH0		= 0x7E40,
	RSW3_FWSCRFO0		= 0x7E80,
	RSW3_FWSCRRA0		= 0x7EC0,
	RSW3_FWSCRCA0		= 0x7F00,
	RSW3_FWTWBFC0		= 0x8000,
	RSW3_FWTWBFVC0		= 0x8004,
	RSW3_FWTHBFC0		= 0xA000,
	RSW3_FWTHBFV0C0		= 0xA004,
	RSW3_FWTHBFV1C0		= 0xA008,
	RSW3_FWFOBFC0		= 0xB000,
	RSW3_FWFOBFV0C0		= 0xB004,
	RSW3_FWFOBFV1C0		= 0xB008,
	RSW3_FWRFC0		= 0xD000,
	RSW3_FWRFSVC0		= 0xD004,
	RSW3_FWRFEVC0		= 0xD008,
	RSW3_FWCFC0		= 0xE000,
	RSW3_FWCFMC00		= 0xE004,
};

/* MFWD */
/* bit field definitions for FWGC */
/* bit field definitions for FWCEPRC2 */
/* bit field definitions for FWCEPTC */

/* bit field definitions for FWPC0 */
#define RSW3_LTHTA			BIT(0)
#define RSW3_IP4UE			BIT(3)
#define RSW3_IP4TE			BIT(4)
#define RSW3_IP4OE			BIT(5)
#define RSW3_L2SE			BIT(9)
#define RSW3_IP4EA			BIT(10)
#define RSW3_IPDSA			BIT(12)
#define RSW3_IPHLA			BIT(18)
#define RSW3_MACDSA			BIT(20)
#define RSW3_MACHLA			BIT(26)
#define RSW3_MACHMA			BIT(27)
#define RSW3_VLANSA			BIT(28)

/* bit field definitions for FWPC1 */
#define RSW3_DDSL			BIT(1)
#define RSW3_DDE			BIT(0)

/* bit field definitions for FWPC2 */

/* bit field definitions for FWPC3 */

/* bit field definitions for FWMACAGUSPC */
#define RSW3_MACAGUSP			GENMASK(9, 0)

/* bit field definitions for FWMACAGC */
#define RSW3_MACAGT			GENMASK(19, 0)
#define RSW3_MACAGE			BIT(20)
#define RSW3_MACAGSL			BIT(21)
#define RSW3_MACAGPM			BIT(22)
#define RSW3_MACDES			BIT(24)
#define RSW3_MACAGOG			BIT(28)
#define RSW3_MACDESOG			BIT(29)

/* bit field definitions for FWMACTLn */
#define RSW3_MACDSLVL			GENMASK(22, 16)
#define RSW3_MACSSLVL			GENMASK(6, 0)
#define RSW3_MACCSDL			GENMASK(6, 0)
#define RSW3_MACDVL			GENMASK(6, 0)

/* bit field definitions for FWMACHWLC0 */
#define RSW3_MACTHWLFC3			BIT(31)
#define RSW3_MACTHWLFC2			BIT(30)
#define RSW3_MACTHWLEA			GENMASK(26, 16)
#define RSW3_MACTHWLFC1			BIT(15)
#define RSW3_MACTHWLFC0			BIT(14)
#define RSW3_MACTHWLSA			GENMASK(10, 0)

/* bit field definitions for FWLTHTLx */
#define RSW3_LTHELA			GENMASK(9, 0)
#define RSW3_LTHED			BIT(31)
#define RSW3_LTHFPL			GENMASK(23, 20)
#define RSW3_LTHRPL			GENMASK(19, 16)
#define RSW3_LTHBLL			BIT(12)
#define RSW3_LTHLDL			BIT(10)
#define RSW3_LTHSLPO			GENMASK(7, 0)
#define RSW3_LTHMSDUNL			GENMASK(19, 16)
#define RSW3_LTHGATENL			GENMASK(2, 0)
#define RSW3_LTHMTRVL			BIT(31)
#define RSW3_LTHMTRNL			GENMASK(22, 16)
#define RSW3_LTHFRERVL			BIT(15)
#define RSW3_LTHFRERNL			GENMASK(6, 0)
#define RSW3_LTHSLVL(num_agents)	GENMASK(16 + (num_agents) - 1, 16)
#define RSW3_LTHRVL			BIT(15)
#define RSW3_LTHRNL			GENMASK(10, 0)
#define RSW3_LTHCSDL			GENMASK(15, 0)
#define RSW3_LTHCMEL			BIT(21)
#define RSW3_LTHEMEL			BIT(20)
#define RSW3_LTHIPUL			BIT(19)
#define RSW3_LTHIPVL			GENMASK(18, 16)
#define RSW3_LTHDVL(num_agents)		GENMASK((num_agents) - 1, 0)
#define RSW3_LTHDLVL(num_agents)	GENMASK((num_agents) - 1, 0)
#define RSW3_LTHTL			BIT(31)
#define RSW3_LTHLOF			BIT(3)
#define RSW3_LTHLEF			BIT(2)
#define RSW3_LTHLSF			BIT(1)
#define RSW3_LTHLF			BIT(0)
#define RSW3_LTHTTR			BIT(1)
#define RSW3_LTHTIOG			BIT(0)

/* bit field definitions for RSW3_FWIP4SC */
#define RSW3_IP4IDPTS			BIT(24)
#define RSW3_IP4IIDS			BIT(23)
#define RSW3_IP4IISS			BIT(22)
#define RSW3_IP4ICDS			BIT(21)
#define RSW3_IP4ICPS			BIT(20)
#define RSW3_IP4ICVTS			BIT(19)
#define RSW3_IP4ISDS			BIT(18)
#define RSW3_IP4ISPS			BIT(17)
#define RSW3_IP4ISVS			BIT(16)
#define RSW3_IP4IDPTH			BIT(12)
#define RSW3_IP4ISPTH			BIT(11)
#define RSW3_IP4IPH			BIT(10)
#define RSW3_IP4IIDH			BIT(9)
#define RSW3_IP4IISH			BIT(8)
#define RSW3_IP4ICDH			BIT(7)
#define RSW3_IP4ICPH			BIT(6)
#define RSW3_IP4ICVH			BIT(5)
#define RSW3_IP4ISDH			BIT(4)
#define RSW3_IP4ISPH			BIT(3)
#define RSW3_IP4ISVH			BIT(2)
#define RSW3_IP4IMSH			BIT(1)
#define RSW3_IP4IMDH			BIT(0)

/* bit field definitions for RSW3_FWL23UTIM */
#define RSW3_L23UTR			BIT(1)
#define RSW3_L23UTIOG			BIT(0)

/* bit field definitions for RSW3_FWL23URL0 */
#define RSW3_L23URPVL(num_agents)	GENMASK(16 + (num_agents) - 1, 16)
#define RSW3_L23URNL			GENMASK(10, 0)

/* bit field definitions for RSW3_FWSCR22 */
#define RSW3_L2SCRSL			BIT(18)
#define RSW3_IP6CRSL			BIT(17)
#define RSW3_IP4CRSL			BIT(16)
#define RSW3_L2SRSL			GENMASK(15, 0)  //4 agent_index_gwca ???

/* Forwarding Engine Port Configuration 0 i (i=0..PORT_N-1) */
#define RSW3_FWPC0(i)			(RSW3_FWPC00 + (i) * 0x10)

/* Forwarding Engine Port Configuration 1 i (i=0..PORT_N-1) */
#define RSW3_FWPC1(i)			(RSW3_FWPC10 + (i) * 0x10)

/* Forwarding Engine Port Configuration 2 i (i=0..PORT_N-1) */
#define RSW3_FWPC2(i)			(RSW3_FWPC20 + (i) * 0x10)

/* Forwarding Engine Port Configuration 3 i (i=0..PORT_N-1) */
#define RSW3_FWPC3(i)			(RSW3_FWPC30 + (i) * 0x10)

/* Forwarding Engine Level Based WaterMark Configuration i (i=0..PORT_N-1) */
#define RSW3_FWLBWMC(i)			(RSW3_FWLBWMC0 + (i) * 0x4)

/* Forwarding Engine Port i and IPV j Filtering Priority and Information i
 * (i=0..PORT_N-1) (j=0..FRM_PRIO_N-1)
 */
#define RSW3_FWPIFPI(i, j)		(RSW3_FWPIFPI00 + (i) * 0x20 + (j) * 0x4)

/* RSW3_FWRO + 0400H + 70H * i
 * Forwarding Engine Cut-Through General Configuration 0 i (i= 0..CT_CRULE_N-1)
 */
#define RSW3_FWCTGC0(i)			(RSW3_FWCTGC00 + (i) * 0x70)

/* Forwarding Engine Cut-Through General Configuration 1 i (i= 0..CT_CRULE_N-1) */
#define RSW3_FWCTGC1(i)			(RSW3_FWCTGC10 + (i) * 0x70)

/* Forwarding Engine Cut-Through Target Configuration 0 i (i= 0..CT_CRULE_N-1) */
#define RSW3_FWCTTC0(i)			(RSW3_FWCTTC00 + (i) * 0x70)

/* Forwarding Engine Cut-Through Target Configuration 1 i (i= 0..CT_CRULE_N-1) */
#define RSW3_FWCTTC1(i)			(RSW3_FWCTTC10 + (i) * 0x70)

/* Forwarding Engine Cut-Through Target Configuration 2 j i
 * (j= 0..PORT_GWCA_N-1) (i= 0..CT_CRULE_N-1)
 */
#define RSW3_FWCTTC2(j, i)		(RSW3_FWCTTC200 + (i) * 0x70 + (j) * 0x4)

/* Forwarding Engine Cut-Through Separation Configuration 0 i (i= 0..CT_CRULE_N-1) */
#define RSW3_FWCTSC0(i)			(RSW3_FWCTSC00	+ (i) * 0x70)

/* Forwarding Engine Cut-Through Separation Configuration 1 i (i= 0..CT_CRULE_N-1) */
#define RSW3_FWCTSC1(i)			(RSW3_FWCTSC10	+ (i) * 0x70)

/* Forwarding Engine Cut-Through Separation Configuration 2 i (i= 0..CT_CRULE_N-1) */
#define RSW3_FWCTSC2(i)			(RSW3_FWCTSC20	+ (i) * 0x70)

/* Forwarding Engine Cut-Through Separation Configuration 3 i (i= 0..CT_CRULE_N-1) */
#define RSW3_FWCTSC3(i)			(RSW3_FWCTSC30	+ (i) * 0x70)

/* Forwarding Engine Cut-Through Separation Configuration 4 i (i= 0..CT_CRULE_N-1) */
#define RSW3_FWCTSC4(i)			(RSW3_FWCTSC40 + (i) * 0x70)

/* Forwarding Engine Integrity Check Ethernet Type Configuration 1 i (i=0..PORT_N-1) */
#define RSW3_FWICETC1(i)		(RSW3_FWICETC10 + (i) * 0x40)

/* Forwarding Engine Integrity Check IPv4 Configuration i (i=0..PORT_N-1) */
#define RSW3_FWICIP4C(i)		(RSW3_FWICIP4C0 + (i) * 0x40)

/* Forwarding Engine IPv4 Address Configuration i (i=0..PORT_N-1) */
#define RSW3_FWIP4AC(i)			(RSW3_FWIP4AC0 + (i) * 0x40)

/* Forwarding Engine Integrity Check IPv6 Configuration i (i=0..PORT_N-1) */
#define RSW3_FWICIP6C(i)		(RSW3_FWICIP6C0 + (i) * 0x40)

/* Forwarding Engine IPv6 Address Configuration 0 i (i=0..PORT_N-1) */
#define RSW3_FWIP6AC0(i)		(RSW3_FWIP6AC00 + (i) * 0x40)

/* Forwarding Engine IPv6 Address Configuration 1 i (i=0..PORT_N-1) */
#define RSW3_FWIP6AC1(i)		(RSW3_FWIP6AC10 + (i) * 0x40)

/* Forwarding Engine IPv6 Address Configuration 2 i (i=0..PORT_N-1) */
#define RSW3_FWIP6AC2(i)		(RSW3_FWIP6AC20 + (i) * 0x40)

/* Forwarding Engine IPv6 Address Configuration 3 i (i=0..PORT_N-1) */
#define RSW3_FWIP6AC3(i)		(RSW3_FWIP6AC30 + (i) * 0x40)

/* Forwarding Engine IPv4 IP Address Prefix Configuration i (i=0..PORT_N-1) */
#define RSW3_FWIP4APC(i)		(RSW3_FWIP4APC0 + (i) * 0x40)

/* Forwarding Engine IPv6 IP Address Prefix Configuration i (i=0..PORT_N-1) */
#define RSW3_FWIP6APC(i)		(RSW3_FWIP6APC0 + (i) * 0x40)

/* Forwarding Engine Integrity Check Ethernet Type Configuration 2 i (i=0..PORT_N-1) */
#define RSW3_FWICETC2(i)		(RSW3_FWICETC20 + (i) * 0x200)

/* Forwarding Engine Integrity Check Ethernet Type Configuration 3 i j
 * (i=0..PORT_N-1) (j=0..INTG_ETYPE_N/2-1)
 */
#define RSW3_FWICETC3(i, j)		(RSW3_FWICETC300 + (i) * 0x200 + (j) * 0x4)

/* Forwarding Engine IPv4 Filtering Address Configuration i j
 * (i=0..PORT_N-1) (j=0..INTG_FILT_SIPA_N-1)
 */
#define RSW3_FWIP4FAC(i, j)		(RSW3_FWIP4FAC00  + (i) * 0x200 + (j) * 0x4)

/* Forwarding Engine IPv4 Specified Filter Configuration i j
 * (i=0..PORT_N-1) (j=0..INTG_FILT_SIPA_N-1)
 */
#define RSW3_FWIP4SFC(i, j)		(RSW3_FWIP4SFC00 + (i) * 0x200 + (j) * 0x4)

/* Forwarding Engine IPv6 Filtering Address Configuration 0 i j
 * (i=0..PORT_N-1) (j=0.. INTG_FILT_SIPA_N-1)
 */
#define RSW3_FWIP6FAC0(i, j)		(RSW3_FWIP6FAC000 + (i) * 0x200 + (j) * 0x4)

/* Forwarding Engine IPv6 Filtering Address Configuration 1 i j
 * (i=0..PORT_N-1) (j=0.. INTG_FILT_SIPA_N-1)
 */
#define RSW3_FWIP6FAC1(i, j)		(RSW3_FWIP6FAC100 + (i) * 0x200 + (j) * 0x4)

/* Forwarding Engine IPv6 Filtering Address Configuration 2 i j
 * (i=0..PORT_N-1) (j=0.. INTG_FILT_SIPA_N-1)
 */
#define RSW3_FWIP6FAC2(i, j)		(RSW3_FWIP6FAC200 + (i) * 0x200 + (j) * 0x4)

/* Forwarding Engine IPv6 Filtering Address Configuration 3 i j
 * (i=0..PORT_N-1) (j=0.. INTG_FILT_SIPA_N-1)
 */
#define RSW3_FWIP6FAC3(i, j)		(RSW3_FWIP6FAC300 + (i) * 0x200 + (j) * 0x4)

/* Forwarding Engine IPv6 Specified Filter Configuration i j
 * (i=0..PORT_N-1) (j=0.. INTG_FILT_SIPA_N-1)
 */
#define RSW3_FWIP6SFC(i, j)		(RSW3_FWIP6SFC00 + (i) * 0x200 + (j) * 0x4)

/* Forwarding Engine Ipv4 Total Length Check Configuration i (i=0..PORT_N-1) */
#define RSW3_FWIP4TLCC(i)		(RSW3_FWIP4TLCC0 + (i) * 0x200)

/* Forwarding Engine Ipv6 Payload Length Check Configuration i (i=0..PORT_N-1) */
#define RSW3_FWIP6PLCC(i)		(RSW3_FWIP6PLCC0 + (i) * 0x200)

/* Forwarding Engine Integrity Check Layer 4 Configuration i (i=0..PORT_N-1) */
#define RSW3_FWICL4C(i)			(RSW3_FWICL4C0 + (i) * 0x200)

/* Forwarding Engine Integrity Check Layer 4 TCP Header Length Configuration i (i=0..PORT_N-1) */
#define RSW3_FWICL4THLC(i)		(RSW3_FWICL4THLC0 + (i) * 0x200)

/* Forwarding Engine L3 Table Learn 13 i (i=0..PORT_GWCA_N-1) */
#define RSW3_FWLTHTL13(i)		(RSW3_FWLTHTL130 + (i) * 0x4)

/* Forwarding Engine L3 Table Search Result 4 i (i=0..PORT_GWCA_N-1) */
#define RSW3_FWLTHTSR4(i)		(RSW3_FWLTHTSR40 + (i) * 0x4)

/* Forwarding Engine L3 Table Read Result 13 i (i=0..PORT_GWCA_N-1) */
#define RSW3_FWLTHTRR13(i)		(RSW3_FWLTHTRR130 + (i) * 0x4)

/* Forwarding Engine MAC Table Learn 7 i (i=0..PORT_GWCA_N-1) */
#define RSW3_FWMACTL7(i)		(RSW3_FWMACTL70 + (i) * 0x4)

/* Forwarding Engine MAC Table Search Result 2 i (i=0..PORT_GWCA_N-1) */
#define RSW3_FWMACTSR2(i)		(RSW3_FWMACTSR20 + (i) * 0x4)

/* Forwarding Engine MAC Table Read Result 7 i (i=0..PORT_GWCA_N-1) */
#define RSW3_FWMACTRR7(i)		(RSW3_FWMACTRR70 + (i) * 0x4)

/* Forwarding Engine MAC Table HW Learning Configuration 2 i (i=0..PORT_N -1) */
#define RSW3_FWMACHWLC2(i)		(RSW3_FWMACHWLC20 + (i) * 0x4)

/* Forwarding Engine VLAN Table Learn 6 i (i=0..PORT_GWCA_N-1) */
#define RSW3_FWVLANTL6(i)		(RSW3_FWVLANTL60 + (i) * 0x4)

/* Forwarding Engine VLAN Table Search Result 5 i (i=0..PORT_GWCA_N-1) */
#define RSW3_FWVLANTSR5(i)		(RSW3_FWVLANTSR50 + (i) * 0x4)

/* Forwarding Engine Port Based Forwarding Configuration 0 i (i=0..PORT_N) */
#define RSW3_FWPBFC0(i)			(RSW3_FWPBFC00 + (i) * 0x10)

/* Forwarding Engine Port Based Forwarding Configuration 1 i (i=0..PORT_N) */
#define RSW3_FWPBFC1(i)			(RSW3_FWPBFC10 + (i) * 0x10)

/* Forwarding Engine Port Based Forwarding CSD Configuration j i
 * (j= 0..PORT_GWCA_N-1) (i= 0..PORT_N-1)
 */
#define RSW3_FWPBFCSDC(j, i)		(RSW3_FWPBFCSDC00 + (i) * 0x20 + (j) * 0x4)

/* Forwarding Engine Layer2/Layer3 Update ReMapping Configuration i (i=0..LTH_REMAP_N) */
#define RSW3_FWL23URMC(i)		(RSW3_FWL23URMC0 + (i) * 0x4)

/* Forwarding Engine PSFP MSDU Filter Global Configuration i (i=0..PSFP_MSDU_N-1) */
#define RSW3_FWPMFGC(i)			(RSW3_FWPMFGC0 + (i) * 0x4)

/* Forwarding Engine PSFP Gate Filter Configuration i (i=0..PSFP_GATE_N-1) */
#define RSW3_FWPGFC(i)			(RSW3_FWPGFC0 + (i) * 0x40)

/* Forwarding Engine PSFP Gate Filter Initial Gate State Configuration i (i=0..PSFP_GATE_N-1) */
#define RSW3_FWPGFIGSC(i)		(RSW3_FWPGFIGSC0 + (i) * 0x40)

/* Forwarding Engine PSFP Gate Filter Entry Number Configuration i (i=0..PSFP_GATE_N-1) */
#define RSW3_FWPGFENC(i)		(RSW3_FWPGFENC0 + (i) * 0x40)

/* Forwarding Engine PSFP Gate Filter Entry Number Monitoring i (i=0..PSFP_GATE_N-1) */
#define RSW3_FWPGFENM(i)		(RSW3_FWPGFENM0 + (i) * 0x40)

/* Forwarding Engine PSFP Gate Filter Cycle Start Time Configuration 0 i (i=0..PSFP_GATE_N-1) */
#define RSW3_FWPGFCSTC0(i)		(RSW3_FWPGFCSTC00 + (i) * 0x40)

/* Forwarding Engine PSFP Gate Filter Cycle Start Time Configuration 1 i (i=0..PSFP_GATE_N-1) */
#define RSW3_FWPGFCSTC1(i)		(RSW3_FWPGFCSTC1 + (i) * 0x40)

/* Forwarding Engine PSFP Gate Filter Cycle Start Time Monitoring 0 i (i=0..PSFP_GATE_N-1) */
#define RSW3_FWPGFCSTM0(i)		(RSW3_FWPGFCSTM00 + (i) * 0x40)

/* Forwarding Engine PSFP Gate Filter Cycle Start Time Monitoring 1 i (i=0..PSFP_GATE_N-1) */
#define RSW3_FWPGFCSTM1(i)		(RSW3_FWPGFCSTM10 + (i) * 0x40)

/* Forwarding Engine PSFP Gate Filter Cycle Time Configuration i (i=0..PSFP_GATE_N-1) */
#define RSW3_FWPGFCTC(i)		(RSW3_FWPGFCTC0 + (i) * 0x40)

/* Forwarding Engine PSFP Gate Filter Cycle Time Monitoring i (i=0..PSFP_GATE_N-1) */
#define RSW3_FWPGFCTM(i)		(RSW3_FWPGFCTM0 + (i) * 0x40)

/* Forwarding Engine PSFP Gate Filter Hardware Calibration Configuration i (i=0..PSFP_GATE_N-1) */
#define RSW3_FWPGFHCC(i)		(RSW3_FWPGFHCC0 + (i) * 0x40)

/* Forwarding Engine PSFP Gate Filter Status Monitoring i (i=0..PSFP_GATE_N-1) */
#define RSW3_FWPGFSM(i)			(RSW3_FWPGFSM0 + (i) * 0x40)

/* Forwarding Engine PSFP Gate Filter Global Configuration i (i=0..PSFP_GATE_N-1) */
#define RSW3_FWPGFGC(i)			(RSW3_FWPGFGC0 + (i) * 0x40)

/* Forwarding Engine PSFP MeTeR Filter Configuration i (i=0..PSFP_MTR_N-1) */
#define RSW3_FWPMTRFC(i)		(RSW3_FWPMTRFC + (i) * 0x20)

/* Forwarding Engine PSFP MeTeR CBS Configuration i (i=0..PSFP_MTR_N-1) */
#define RSW3_FWPMTRCBSC(i)		(RSW3_FWPMTRCBSC0 + (i) * 0x20)

/* Forwarding Engine PSFP MeTeR CIR Configuration i (i=0..PSFP_MTR_N-1) */
#define RSW3_FWPMTRCIRC(i)		(RSW3_FWPMTRCIRC0 + (i) * 0x20)

/* Forwarding Engine PSFP MeTeR EBS Configuration i (i=0..PSFP_DMTR_N-1) */
#define RSW3_FWPMTREBSC(i)		(RSW3_FWPMTREBSC0 + (i) * 0x20)

/* Forwarding Engine PSFP MeTeR EIR Configuration i (i=0..PSFP_DMTR_N-1) */
#define RSW3_FWPMTREIRC(i)		(RSW3_FWPMTREIRC0 + (i) * 0x20)

/* Forwarding Engine PSFP MeTeR Filter Monitoring i (i=0..PSFP_MTR_N-1) */
#define RSW3_FWPMTRFM(i)		(RSW3_FWPMTRFM0 + (i) * 0x20)

/* Forwarding Engine SEQuence Number Generation Configuration i (i=0.. LTH_SEQGN_N-1) */
#define RSW3_FWSEQNGC(i)		(RSW3_FWSEQNGC0 + (i) * 0x8)

/* Forwarding Engine SEQuence Number Generation Monitoring i (i=0.. LTH_SEQGN_N-1) */
#define RSW3_FWSEQNGM(i)		(RSW3_FWSEQNGM0 + (i) * 0x8)

/* Forwarding Engine Cut-Through Forwarded Descriptor CouNter i (i=0..PORT_TIME_N-1) */
#define RSW3_FWCTFDCN(i)		(RSW3_FWCTFDCN0 + (i) * 0x20)

/* Forwarding Engine Direct Descriptor Forwarded Descriptor CouNter i (i=0..PORT_SLOW_N-1) */
#define RSW3_FWDDFDCN(i)		(RSW3_FWDDFDCN0 + (i) * 0x20)

/* Forwarding Engine Layer 3 Forwarded Descriptor CouNter i (i=0..PORT_N-1) */
#define RSW3_FWLTHFDCN(i)		(RSW3_FWLTHFDCN0 + (i) * 0x20)

/* Forwarding Engine Layer 2 Forwarded Descriptor CouNter i (i=0..PORT_N-1) */
#define RSW3_FWLTWFDCN(i)		(RSW3_FWLTWFDCN0 + (i) * 0x20)

/* Forwarding Engine Port Based Forwarded Descriptor CouNter i (i=0..PORT_N-1) */
#define RSW3_FWPBFDCN(i)		(RSW3_FWPBFDCN0 + (i) * 0x20)

/* Forwarding Engine MAC Hardware Learn CouNter i (i=0..PORT_N-1) */
#define RSW3_FWMHLCN(i)			(RSW3_FWMHLCN0 + (i) * 0x20)

/* Forwarding Engine Integrity Check Rejected Descriptor CouNter i (i=0..PORT_N-1) */
#define RSW3_FWICRDCN(i)		(RSW3_FWICRDCN0 + (i) * 0x20)

/* Forwarding Engine WaterMark Rejected Descriptor CouNter i (i=0..PORT_N-1) */
#define RSW3_FWWMRDCN(i)		(RSW3_FWWMRDCN0 + (i) * 0x20)

/* Forwarding Engine Cut-Through Rejected Descriptor CouNter i (i=0..PORT_TIME_N-1) */
#define RSW3_FWCTRDCN(i)		(RSW3_FWCTRDCN0 + (i) * 0x20)

/* Forwarding Engine Direct Descriptor Rejected Descriptor CouNter i (i=0..PORT_SLOW_N-1) */
#define RSW3_FWDDRDCN(i)		(RSW3_FWDDRDCN0 + (i) * 0x20)

/* Forwarding Engine Layer 3 Rejected Descriptor CouNter i (i=0..PORT_N-1) */
#define RSW3_FWLTHRDCN(i)		(RSW3_FWLTHRDCN0 + (i) * 0x20)

/* Forwarding Engine Layer 2 Rejected Descriptor CouNter i (i=0..PORT_N-1) */
#define RSW3_FWLTWRDCN(i)		(RSW3_FWLTWRDCN0 + (i) * 0x20)

/* Forwarding Engine Port Based Rejected Descriptor CouNter i (i=0..PORT_N-1) */
#define RSW3_FWPBRDCN(i)		(RSW3_FWPBRDCN0 + (i) * 0x20)

/* Forwarding Engine PSFP MSDU Filtered Descriptor CouNter i (i=0..PSFP_MSDU_N-1) */
#define RSW3_FWPMFDCN(i)		(RSW3_FWPMFDCN0 + (i) * 0x4)

/* Forwarding Engine PSFP Gate Filtered Descriptor CouNter i (i=0..PSFP_GATE_N-1) */
#define RSW3_FWPGFDCN(i)		(RSW3_FWPGFDCN0 + (i) * 0x4)

/* Forwarding Engine PSFP Meter Green Descriptor CouNter i (i=0..PSFP_MTR_N-1) */
#define RSW3_FWPMGDCN(i)		(RSW3_FWPMGDCN0 + (i) * 0x20)

/* Forwarding Engine PSFP Meter Yellow Descriptor CouNter i (i=0..PSFP_DMTR_N-1) */
#define RSW3_FWPMYDCN(i)		(RSW3_FWPMYDCN0 + (i) * 0x20)

/* Forwarding Engine PSFP Meter Red Descriptor CouNter i (i=0..PSFP_MTR_N-1) */
#define RSW3_FWPMRDCN(i)		(RSW3_FWPMRDCN0 + (i) * 0x20)

/* Forwarding Engine FRER Passed Packet CouNter i (i=0..FRER_RECE_N-1) */
#define RSW3_FWFRPPCN(i)		(RSW3_FWFRPPCN0 + (i) * 0x8)

/* Forwarding Engine FRER Discarded Packet CouNter i (i=0..FRER_RECE_N-1) */
#define RSW3_FWFRDPCN(i)		(RSW3_FWFRDPCN0 + (i) * 0x8)

/* Forwarding Engine Block List Filtering CouNter i (i=0..PORT_N-1) */
#define RSW3_FWBLFCN(i)			(RSW3_FWBLFCN0 + (i) * 0x20)

/* Forwarding Engine Acceptance List Filtering CouNter i (i=0..PORT_N-1) */
#define RSW3_FWALFCN(i)			(RSW3_FWALFCN0 + (i) * 0x20)

/* Forwarding Engine Error Interrupt Status 0 i (i=0..PORT_N) */
#define RSW3_FWEIS0(i)			(0RSW3_FWEIS0 + (i) * 0x10)

/* Forwarding Engine Error Interrupt Enable 0 i (i=0..PORT_N) */
#define RSW3_FWEIE0(i)			(RSW3_FWEIE00 + (i) * 0x10)

/* Forwarding Engine Error Interrupt Disable 0 i (i=0..PORT_N) */
#define RSW3_FWEID0(i)			(RSW3_FWEID00 + (i) * 0x10)

/* Forwarding Engine Security Configuration Register Two byte filter i. (i=0..PFL_TWBF_N/32-1) */
#define RSW3_FWSCRTO(i)			(RSW3_FWSCRTO0 + (i) * 0x4)

/* Forwarding Engine Security Configuration Register THree byte filter i. (i=0..PFL_THBF_N/32-1) */
#define RSW3_FWSCRTH(i)			(RSW3_FWSCRTH0 + (i) * 0x4)

/* Forwarding Engine Security Configuration Register FOur byte filter i. (i=0..PFL_FOBF_N/32-1) */
#define RSW3_FWSCRFO(i)			(RSW3_FWSCRFO0 + (i) * 0x4)

/* Forwarding Engine Security Configuration Register RAnge filter i. (i=0..PFL_RAGF_N/32-1) */
#define RSW3_FWSCRRA(i)			(RSW3_FWSCRRA0 + (i) * 0x4)

/* Forwarding Engine Security Configuration Register CAscade filter i. (i=0..PFL_CADF_N/32-1) */
#define RSW3_FWSCRCA(i)			(RSW3_FWSCRCA0 + (i) * 0x4)

/* Forwarding engine TWo Byte Filter Configuration i (i=0..PFL_TWBF_N-1) */
#define RSW3_FWTWBFC(i)			(RSW3_FWTWBFC0 + (i) * 0x10)

/* Forwarding engine TWo Byte Filter Value Configuration i (i=0..PFL_TWBF_N-1) */
#define RSW3_FWTWBFVC(i)		(RSW3_FWTWBFVC0 + (i) * 0x10)

/* Forwarding engine THree Byte Filter Configuration i (i=0..PFL_THBF_N-1) */
#define RSW3_FWTHBFC(i)			(RSW3_FWTHBFC0 + (i) * 0x10)

/* Forwarding engine THree Byte Filter Value Configuration 0 i (i=0..PFL_THBF_N-1) */
#define RSW3_FWTHBFV0C(i)		(RSW3_FWTHBFV0C0 + (i) * 0x10)

/* Forwarding engine THree Byte Filter Value Configuration 1 i (i=0..PFL_THBF_N-1) */
#define RSW3_FWTHBFV1C(i)		(RSW3_FWTHBFV1C0 + (i) * 0x10)

/* Forwarding engine FOur Byte Filter Configuration i (i=0..PFL_FOBF_N-1) */
#define RSW3_FWFOBFC(i)			(RSW3_FWFOBFC0 + (i) * 0x10)

/* Forwarding engine FOur Byte Filter Value 0 Configuration i (i=0..PFL_FOBF_N-1) */
#define RSW3_FWFOBFV0C(i)		(RSW3_FWFOBFV0C0 + (i) * 0x10)

/* Forwarding engine FOur Byte Filter Value 1 Configuration i (i=0..PFL_FOBF_N-1) */
#define RSW3_FWFOBFV1C(i)		(RSW3_FWFOBFV1C0 + (i) * 0x10)

/* Forwarding engine Range Filter Configuration i (i=0..PFL_RAGF_N-1) */
#define RSW3_FWRFC(i)			(RSW3_FWRFC0 + (i) * 0x10)

/* Forwarding Engine Range Filter Start Value Configuration i (i=0..PFL_RAGF_N-1) */
#define RSW3_FWRFSVC(i)			(RSW3_FWRFSVC0 + (i) * 0x10)

/* Forwarding Engine Range Filter End Value Configuration i (i=0..PFL_RAGF_N-1) */
#define RSW3_FWRFEVC(i)			(RSW3_FWRFEVC0 + (i) * 0x10)

/* Forwarding Engine Cascade Filter Configuration i (i=0..PFL_CADF_N-1) */
#define RSW3_FWCFC(i)			(RSW3_FWCFC0 + (i) * 0x40)

/* Forwarding Engine Cascade Filter Mapping Configuration i j
 * (i=0..PFL_CADF_N-1) (j=0..PFL_CFMF_N-1)
 */
#define RSW3_FWCFMC(i, j)		(RSW3_FWCFMC0 + (i) * 0x40 + (j) * 0x4)

#endif	/* #ifndef __RSWITCH3_FWD_H__ */
