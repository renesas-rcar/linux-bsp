/*
 * ON Semiconductor AP020X-AR023X sensor camera
 *
 * Copyright (C) 2020 Cogent Embedded, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#define AP0201_MAX_WIDTH	1920
#define AP0201_MAX_HEIGHT	1200

#define AP0201_DELAY		0xffff

struct ap0201_reg {
	u16	reg;
	u16	val;
};

static const struct ap0201_reg ap0201_regs_wizard[] = {
/* enable FSIN */
{0xc88c, 0x0303},
{0xfc00, 0x2800},
{0x0040, 0x8100},
{AP0201_DELAY, 100},
};

static const struct ap0201_reg ap0202_regs_wizard[] = {
/* enable FSIN */
{0xc890, 0x0303},
{0xfc00, 0x2800},
{0x0040, 0x8100},
{AP0201_DELAY, 100},
};
