/*
 * rcar_lvds.h  --  R-Car LVDS Unit
 *
 * Copyright (C) 2018 Renesas Electronics Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __RCAR_LVDS_H__
#define __RCAR_LVDS_H__

#define RCAR_LVDS_MAX_NUM	2

#if IS_ENABLED(CONFIG_DRM_RCAR_LVDS)
int rcar_lvds_pll_round_rate(u32 index, unsigned long rate);
#else
static inline int rcar_lvds_pll_round_rate(u32 index, unsigned long rate)
{
	return 0;
};
#endif
#endif /* __RCAR_LVDS_H__ */
