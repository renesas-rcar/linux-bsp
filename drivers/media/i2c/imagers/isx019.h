/*
 * Sony isx019 (isp) camera setup 1280x800@30/UYVY/BT601/8bit
 *
 * Copyright (C) 2018-2020 Cogent Embedded, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

//#define ISX019_MAX_WIDTH	1280
//#define ISX019_MAX_HEIGHT	960

#define ISX019_DELAY		0xffff

struct isx019_reg {
	u16	reg;
	u16	val;
};

static const struct isx019_reg isx019_regs[] = {
#if 0
/* enable FSIN */
{ISX019_DELAY, 100},
#endif
/* disable embedded data */
{0x504c, 0x0},
{0x504e, 0x0},
};
