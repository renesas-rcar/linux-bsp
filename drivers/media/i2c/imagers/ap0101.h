/*
 * ON Semiconductor ap0101-ar014x sensor camera setup 1280x720@30/UYVY/BT601/8bit
 *
 * Copyright (C) 2018-2020 Cogent Embedded, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#define AP0101_MAX_WIDTH	1280
#define AP0101_MAX_HEIGHT	720

#define AP0101_DELAY		0xffff

struct ap0101_reg {
	u16	reg;
	u16	val;
};

static const struct ap0101_reg ap0101_regs[] = {
/* enable FSIN */
{0xc88c, 0x0303},
{0xfc00, 0x2800},
{0x0040, 0x8100},
{AP0101_DELAY, 100},
};

static const struct ap0101_reg ap0102_regs[] = {
/* enable FSIN */
{0xc890, 0x0303},
{0xfc00, 0x2800},
{0x0040, 0x8100},
{AP0101_DELAY, 100},
};
