/* SPDX-License-Identifier: GPL-2.0 */
/* Renesas Ethernet Switch gPTP device driver
 *
 * Copyright (C) 2022 Renesas Electronics Corporation
 */

#ifndef __RSWITCH_PTP_H__
#define __RSWITCH_PTP_H__

#include <linux/ptp_clock_kernel.h>

#define PTPTIVC_INIT	0x19000000	/* 320MHz */

/* for rswitch_ptp_init */
enum rswitch_ptp_reg_layout {
	RSWITCH_PTP_REG_LAYOUT_S4
};

#define RSWITCH_PTP_CLOCK_S4	PTPTIVC_INIT

/* driver's definitions */
#define RSWITCH_RXTSTAMP_ENABLED		BIT(0)
#define RSWITCH_RXTSTAMP_TYPE_V2_L2_EVENT	BIT(1)
#define RSWITCH_RXTSTAMP_TYPE_ALL		(RSWITCH_RXTSTAMP_TYPE_V2_L2_EVENT | BIT(2))
#define RSWITCH_RXTSTAMP_TYPE			RSWITCH_RXTSTAMP_TYPE_ALL

#define RSWITCH_TXTSTAMP_ENABLED		BIT(0)

#define RSWITCH_GPTP_OFFSET	0x00018000
#define PTPRO			0

enum rswitch_ptp_reg_s4 {
	PTPTMEC		= PTPRO + 0x0010,
	PTPTMDC		= PTPRO + 0x0014,
	PTPTIVC0	= PTPRO + 0x0020,
	PTPTOVC00	= PTPRO + 0x0030,
	PTPTOVC10	= PTPRO + 0x0034,
	PTPTOVC20	= PTPRO + 0x0038,
	PTPGPTPTM00	= PTPRO + 0x0050,
	PTPGPTPTM10	= PTPRO + 0x0054,
	PTPGPTPTM20	= PTPRO + 0x0058,
};

enum rswitch_ptp_reg_v4h {
	TME	= PTPRO + 0x0000,
	TMD	= PTPRO + 0x0004,
	GTIVC	= PTPRO + 0x0010,
	GTOV00	= PTPRO + 0x0014,
	GTOV10	= PTPRO + 0x0018,
	GTOV20	= PTPRO + 0x001c,
};

struct rswitch_ptp_reg_offset {
	u16 enable;
	u16 disable;
	u16 increment;
	u16 config_t0;
	u16 config_t1;
	u16 config_t2;
	u16 monitor_t0;
	u16 monitor_t1;
	u16 monitor_t2;
};

struct rswitch_ptp_private {
	void __iomem *addr;
	struct ptp_clock *clock;
	struct ptp_clock_info info;
	const struct rswitch_ptp_reg_offset *offs;
	u32 tstamp_tx_ctrl;
	u32 tstamp_rx_ctrl;
	s64 default_addend;
	bool initialized;
};

int rswitch_ptp_init(struct rswitch_ptp_private *ptp_priv,
		     enum rswitch_ptp_reg_layout layout, u32 clock);
struct rswitch_ptp_private *rswitch_ptp_alloc(struct platform_device *pdev);

#endif
