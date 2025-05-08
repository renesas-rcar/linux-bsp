/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * rcar_dsc.c  --  R-Car Display Stream Compression
 *
 * Copyright (C) 2022 Renesas Electronics Corporation
 */

#ifndef __RCAR_DSC_H__
#define __RCAR_DSC_H__

struct rcar_dsc {
	struct device *dev;
	void __iomem *mmio;
	struct clk *mod_clk;
	struct reset_control *rstc;

	struct drm_bridge bridge;
	struct drm_bridge *next_bridge;
};

#endif /* __RCAR_CMM_H__ */
