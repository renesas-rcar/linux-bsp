/*
 * OmniVision ov495-ov2775 sensor camera setup 1920x1080@30/UYVY/MIPI
 *
 * Copyright (C) 2017-2020 Cogent Embedded, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

struct ov495_reg {
	u16	reg;
	u8	val;
};

static struct ov495_reg ov495_regs[] = {
{0x3516, 0x00}, /* unlock write */
{0xFFFD, 0x80},
{0xFFFE, 0x20},
{0x8017, 0x1e | (0 << 6)},
{0x7c10, 0x01}, /* UYVY */
};
