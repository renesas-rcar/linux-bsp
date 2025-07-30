/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2025 Rockchip Electronics Co., Ltd.
 */

#ifndef __DW_DP__
#define __DW_DP__

#include <linux/device.h>

struct drm_encoder;
struct dw_dp;

struct dw_dp_plat_data {
	u32 max_link_rate;
	u8 pixel_mode;
};

struct dw_dp *dw_dp_bind(struct device *dev, struct drm_encoder *encoder,
			 struct phy *phy, const struct dw_dp_plat_data *plat_data);
void dw_dp_resume(struct dw_dp *dp);
#endif /* __DW_DP__ */
