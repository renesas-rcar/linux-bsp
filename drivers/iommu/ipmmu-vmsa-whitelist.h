/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 Renesas Electronics Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#ifndef IPMMU_VMSA_WHITELIST_H
#define IPMMU_VMSA_WHITELIST_H

#include <linux/bitmap.h>

#define IPMMU_UTLB_MAX 48
#define IPMMU_CACHE_MAX 16

#define IPMMU_VI0_BASE	(0xFEBD0000UL)
#define IPMMU_VI1_BASE	(0xFEBE0000UL)
#define IPMMU_HC_BASE	(0xE6570000UL)
#define IPMMU_MP_BASE	(0xEC670000UL)
#define IPMMU_DS0_BASE	(0xE6740000UL)
#define IPMMU_DS1_BASE	(0xE7740000UL)

/* Support masters for IPMMU_VI0 and IPMMU_VI1 */
#define M_VIN_0_3	BIT(0)
#define M_VIN_4_7	BIT(1)
#define M_VIN_4_5	BIT(1)
#define M_FCPVD_0	BIT(8)
#define M_FCPVD_1	BIT(9)
#define M_FCPVD_2	BIT(10)
#define M_FCPVD_3	BIT(11)

/* Support masters for IPMMU_HC */
#define M_PCIE_0	BIT(0)
#define M_PCIE_1	BIT(1)
#define M_SATA		BIT(2)
#define M_USB2H_0	BIT(4)
#define M_USB2H_1	BIT(5)
#define M_USB2H_2	BIT(6)
#define M_USB2H_3	BIT(7)
#define M_USB_DMAC_0	BIT(9)
#define M_USB_DMAC_1	BIT(10)
#define M_USB3H_0	BIT(12)
#define M_USB3F_0	BIT(13)
#define M_USB_DMAC_2	BIT(14)
#define M_USB_DMAC_3	BIT(15)

/* Support masters for IPMMU_MP */
#define M_AUDIO_DMAC_0	BIT(0)
#define M_AUDIO_DMAC_1	BIT(1)
#define M_AUDIO_DMAC_2	BIT(2)
#define M_AUDIO_DMAC_3	BIT(3)
#define M_AUDIO_DMAC_4	BIT(4)
#define M_AUDIO_DMAC_5	BIT(5)
#define M_AUDIO_DMAC_6	BIT(6)
#define M_AUDIO_DMAC_7	BIT(7)
#define M_AUDIO_DMAC_8	BIT(8)
#define M_AUDIO_DMAC_9	BIT(9)
#define M_AUDIO_DMAC_10	BIT(10)
#define M_AUDIO_DMAC_11	BIT(11)
#define M_AUDIO_DMAC_12	BIT(12)
#define M_AUDIO_DMAC_13	BIT(13)
#define M_AUDIO_DMAC_14	BIT(14)
#define M_AUDIO_DMAC_15	BIT(15)
#define M_AUDIO_DMAC_16	BIT(16)
#define M_AUDIO_DMAC_17	BIT(17)
#define M_AUDIO_DMAC_18	BIT(18)
#define M_AUDIO_DMAC_19	BIT(19)
#define M_AUDIO_DMAC_20	BIT(20)
#define M_AUDIO_DMAC_21	BIT(21)
#define M_AUDIO_DMAC_22	BIT(22)
#define M_AUDIO_DMAC_23	BIT(23)
#define M_AUDIO_DMAC_24	BIT(24)
#define M_AUDIO_DMAC_25	BIT(25)
#define M_AUDIO_DMAC_26	BIT(26)
#define M_AUDIO_DMAC_27	BIT(27)
#define M_AUDIO_DMAC_28	BIT(28)
#define M_AUDIO_DMAC_29	BIT(29)
#define M_AUDIO_DMAC_30	BIT(30)
#define M_AUDIO_DMAC_31	BIT(31)

/* Support masters for IPMMU_DS0 */
#define M_SYS_DMAC_0	BIT(0)
#define M_SYS_DMAC_1	BIT(1)
#define M_SYS_DMAC_2	BIT(2)
#define M_SYS_DMAC_3	BIT(3)
#define M_SYS_DMAC_4	BIT(4)
#define M_SYS_DMAC_5	BIT(5)
#define M_SYS_DMAC_6	BIT(6)
#define M_SYS_DMAC_7	BIT(7)
#define M_SYS_DMAC_8	BIT(8)
#define M_SYS_DMAC_9	BIT(9)
#define M_SYS_DMAC_10	BIT(10)
#define M_SYS_DMAC_11	BIT(11)
#define M_SYS_DMAC_12	BIT(12)
#define M_SYS_DMAC_13	BIT(13)
#define M_SYS_DMAC_14	BIT(14)
#define M_SYS_DMAC_15	BIT(15)
#define M_ETHERNET	BIT(16)

/* Support masters for IPMMU_DS1 */
#define M_SYS_DMAC_16	BIT(0)
#define M_SYS_DMAC_17	BIT(1)
#define M_SYS_DMAC_18	BIT(2)
#define M_SYS_DMAC_19	BIT(3)
#define M_SYS_DMAC_20	BIT(4)
#define M_SYS_DMAC_21	BIT(5)
#define M_SYS_DMAC_22	BIT(6)
#define M_SYS_DMAC_23	BIT(7)
#define M_SYS_DMAC_24	BIT(8)
#define M_SYS_DMAC_25	BIT(9)
#define M_SYS_DMAC_26	BIT(10)
#define M_SYS_DMAC_27	BIT(11)
#define M_SYS_DMAC_28	BIT(12)
#define M_SYS_DMAC_29	BIT(13)
#define M_SYS_DMAC_30	BIT(14)
#define M_SYS_DMAC_31	BIT(15)
#define M_SYS_DMAC_32	BIT(16)
#define M_SYS_DMAC_33	BIT(17)
#define M_SYS_DMAC_34	BIT(18)
#define M_SYS_DMAC_35	BIT(19)
#define M_SYS_DMAC_36	BIT(20)
#define M_SYS_DMAC_37	BIT(21)
#define M_SYS_DMAC_38	BIT(22)
#define M_SYS_DMAC_39	BIT(23)
#define M_SYS_DMAC_40	BIT(24)
#define M_SYS_DMAC_41	BIT(25)
#define M_SYS_DMAC_42	BIT(26)
#define M_SYS_DMAC_43	BIT(27)
#define M_SYS_DMAC_44	BIT(28)
#define M_SYS_DMAC_45	BIT(29)
#define M_SYS_DMAC_46	BIT(30)
#define M_SYS_DMAC_47	BIT(31)
#define M_SDHI0		BIT(32)
#define M_SDHI1		BIT(33)
#define M_SDHI2		BIT(34)
#define M_SDHI3		BIT(35)

struct ipmmu_whitelist {
	const char *ipmmu_name;
	unsigned int base_addr;
	unsigned long ip_masters;
	DECLARE_BITMAP(ultb, IPMMU_UTLB_MAX);
};

#endif /* IPMMU_VMSA_WHITELIST_H */
