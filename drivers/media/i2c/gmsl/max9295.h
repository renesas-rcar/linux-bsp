/*
 * MAXIM max9295 GMSL2 driver header
 *
 * Copyright (C) 2019-2020 Cogent Embedded, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#define MAX9295_REG2			0x02
#define MAX9295_REG7			0x07
#define MAX9295_CTRL0			0x10
#define MAX9295_I2C2			0x42
#define MAX9295_I2C3			0x43
#define MAX9295_I2C4			0x44
#define MAX9295_I2C5			0x45
#define MAX9295_I2C6			0x46

#define MAX9295_CROSS(n)		(0x1b0 + n)

#define MAX9295_GPIO_A(n)		(0x2be + (3 * n))
#define MAX9295_GPIO_B(n)		(0x2bf + (3 * n))
#define MAX9295_GPIO_C(n)		(0x2c0 + (3 * n))

#define MAX9295_VIDEO_TX_BASE(n)	(0x100 + (0x8 * n))
#define MAX9295_VIDEO_TX0(n)		(MAX9295_VIDEO_TX_BASE(n) + 0)
#define MAX9295_VIDEO_TX1(n)		(MAX9295_VIDEO_TX_BASE(n) + 1)

#define MAX9295_FRONTTOP_0		0x308
#define MAX9295_FRONTTOP_9		0x311
#define MAX9295_FRONTTOP_12		0x314
#define MAX9295_FRONTTOP_13		0x315

#define MAX9295_MIPI_RX0		0x330
#define MAX9295_MIPI_RX1		0x331
#define MAX9295_MIPI_RX2		0x332
#define MAX9295_MIPI_RX3		0x333