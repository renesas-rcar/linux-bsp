/*
 * Sony isx016 (isp) camera setup 1280x960@30/UYVY/BT601/8bit
 *
 * Copyright (C) 2019-2020 Cogent Embedded, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#define ISX016_MAX_WIDTH	1280
#define ISX016_MAX_HEIGHT	960

#define ISX016_DELAY		0xffff

struct isx016_reg {
	u16	reg;
	u16	val;
};

static const struct isx016_reg isx016_regs[] = {
#if 0
/* enable FSIN */
#endif
{ISX016_DELAY, 100},
};
