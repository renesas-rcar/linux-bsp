/* SPDX-License-Identifier: GPL-2.0 */
/* Renesas Ethernet Switch gPTP device driver
 *
 * Copyright (C) 2022 Renesas Electronics Corporation
 */

#ifndef __RTSN_PTP_H__
#define __RTSN_PTP_H__

#include <linux/ptp_clock_kernel.h>

#define GTIVC_INIT	0x50000000	/* 100MHz */

/* for rtsn_ptp_init */
enum rtsn_ptp_reg_layout {
	RTSN_PTP_REG_LAYOUT_V4H
};

#define RTSN_PTP_CLOCK_V4H	GTIVC_INIT

/* driver's definitions */
#define RTSN_RXTSTAMP_ENABLED		BIT(0)
#define RTSN_RXTSTAMP_TYPE_V2_L2_EVENT	BIT(1)
#define RTSN_RXTSTAMP_TYPE_ALL		(RTSN_RXTSTAMP_TYPE_V2_L2_EVENT | BIT(2))
#define RTSN_RXTSTAMP_TYPE		RTSN_RXTSTAMP_TYPE_ALL

#define RTSN_TXTSTAMP_ENABLED		BIT(0)

#define PTPRO			0

enum rtsn_ptp_reg_v4h {
	TME	= PTPRO + 0x0000,
	TMD	= PTPRO + 0x0004,
	GTIVC	= PTPRO + 0x0010,
	GTOV00	= PTPRO + 0x0014,
	GTOV10	= PTPRO + 0x0018,
	GTOV20	= PTPRO + 0x001c,
};

struct rtsn_ptp_reg_offset {
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

struct rtsn_ptp_private {
	void __iomem *addr;
	struct ptp_clock *clock;
	struct ptp_clock_info info;
	const struct rtsn_ptp_reg_offset *offs;
	u32 tstamp_tx_ctrl;
	u32 tstamp_rx_ctrl;
	s64 default_addend;
	bool initialized;
};

int rtsn_ptp_init(struct rtsn_ptp_private *ptp_priv, enum rtsn_ptp_reg_layout layout, u32 clock);
struct rtsn_ptp_private *rtsn_ptp_alloc(struct platform_device *pdev);

#endif
