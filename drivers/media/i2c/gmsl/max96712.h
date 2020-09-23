/*
 * MAXIM max96712 GMSL2 driver header
 *
 * Copyright (C) 2019-2020 Cogent Embedded, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#define MAX96712_MAX_LINKS		4
#define MAX96712_MAX_PIPES		8
#define MAX96712_MAX_PIPE_MAPS		16
#define MAX96712_MAX_MIPI		4

enum max96712_pads {
	MAX96712_SINK_LINK0,
	MAX96712_SINK_LINK1,
	MAX96712_SINK_LINK2,
	MAX96712_SINK_LINK3,
	MAX96712_SOURCE,
	MAX96712_N_PADS,
};

struct max96712_link {
	struct v4l2_async_subdev asd;
	struct v4l2_subdev	*sd;
	struct fwnode_handle	*fwnode;
	int			pad;

	struct i2c_client	*client;
	struct regmap		*regmap;
	int			ser_id;
	int			ser_addr;
	int			pipes_mask;	/* mask of pipes used by this link */
	int			out_mipi;	/* MIPI# */
	int			out_vc;		/* VC# */
	struct regulator	*poc_reg;	/* PoC power supply */
};

#define asd_to_max96712_link(_asd) \
	container_of(_asd, struct max96712_link, asd)

struct max96712_priv {
	struct i2c_client	*client;
	struct regmap		*regmap;
	struct v4l2_subdev	sd;
	struct media_pad	pads[MAX96712_N_PADS];

	struct i2c_mux_core	*mux;

	int			n_links;
	int			links_mask;
	enum gmsl_mode		gmsl_mode;
	struct max96712_link	*link[MAX96712_MAX_LINKS];
	struct v4l2_async_subdev *subdevs[MAX96712_MAX_LINKS];
	struct v4l2_async_notifier notifier;
	struct v4l2_ctrl_handler ctrls;

	int			gpio_resetb;
	int			active_low_resetb;
	bool			pclk_rising_edge;
	bool			is_coax;
	int			him;
	int			bws;
	int			dbl;
	int			hibw;
	int			hven;
	int			hsync;
	int			vsync;
	int			dt;
	u64			crossbar;
	char			cb[16];
	const char		*mbus;
	int			gpio[11];
	int			timeout;
	int			poc_delay;
	struct v4l2_clk		*ref_clk;
	int			lanes;
	int			csi_rate[MAX96712_MAX_MIPI];
	int			fsync_period;
	atomic_t		use_count;
	struct notifier_block	reboot_nb;
};

#define MAX96712_REG4			0x04
#define MAX96712_REG5			0x05
#define MAX96712_REG6			0x06
#define MAX96712_REG14			0x0e
#define MAX96712_REG26			0x10
#define MAX96712_REG27			0x11

#define MAX96712_CTRL0			0x17
#define MAX96712_CTRL1			0x18
#define MAX96712_CTRL2			0x19
#define MAX96712_CTRL3			0x1a
#define MAX96712_CTRL11			0x22
#define MAX96712_CTRL12			0x0a
#define MAX96712_CTRL13			0x0b
#define MAX96712_CTRL14			0x0c

#define MAX96712_PWR1			0x13

#define MAX96712_DEV_ID			0x4a
#define MAX96712_REV			0x4c

#define MAX96712_VIDEO_PIPE_SEL(n)	(0xf0 + n)
#define MAX96712_VIDEO_PIPE_EN		0xf4

#define MAX96712_I2C_0(n)		(0x640 + (0x10 * n))
#define MAX96712_I2C_1(n)		(0x641 + (0x10 * n))

#define MAX96712_RX0(n)			(0x50 + n)

#define MAX_VIDEO_RX_BASE(n)		(n < 5 ? (0x100 + (0x12 * n)) : \
						 (0x160 + (0x12 * (n - 5))))
#define MAX_VIDEO_RX0(n)		(MAX_VIDEO_RX_BASE(n) + 0x00)
#define MAX_VIDEO_RX3(n)		(MAX_VIDEO_RX_BASE(n) + 0x03)
#define MAX_VIDEO_RX8(n)		(MAX_VIDEO_RX_BASE(n) + 0x08)
#define MAX_VIDEO_RX10(n)		(MAX_VIDEO_RX_BASE(n) + 0x0a)

#define MAX_VPRBS(n)			(0x1dc + (0x20 * n))

#define MAX_CROSS_BASE(n)		(0x1c0 + (0x20 * n))
#define MAX_CROSS(n, m)			(MAX_CROSS_BASE(n) + m)

#define MAX_BACKTOP_BASE(bank)		(0x400 + (0x20 * bank))
#define MAX_BACKTOP1(bank)		(MAX_BACKTOP_BASE(bank) + 0x00)
#define MAX_BACKTOP11(bank)		(MAX_BACKTOP_BASE(bank) + 0x0a)
#define MAX_BACKTOP12(bank)		(MAX_BACKTOP_BASE(bank) + 0x0b)
#define MAX_BACKTOP13(bank)		(MAX_BACKTOP_BASE(bank) + 0x0c)
#define MAX_BACKTOP14(bank)		(MAX_BACKTOP_BASE(bank) + 0x0d)
#define MAX_BACKTOP15(bank)		(MAX_BACKTOP_BASE(bank) + 0x0e)
#define MAX_BACKTOP16(bank)		(MAX_BACKTOP_BASE(bank) + 0x0f)
#define MAX_BACKTOP17(bank)		(MAX_BACKTOP_BASE(bank) + 0x10)
#define MAX_BACKTOP18(bank)		(MAX_BACKTOP_BASE(bank) + 0x11)
#define MAX_BACKTOP19(bank)		(MAX_BACKTOP_BASE(bank) + 0x12)
#define MAX_BACKTOP20(bank)		(MAX_BACKTOP_BASE(bank) + 0x13)
#define MAX_BACKTOP21(bank)		(MAX_BACKTOP_BASE(bank) + 0x14)
#define MAX_BACKTOP22(bank)		(MAX_BACKTOP_BASE(bank) + 0x15)
#define MAX_BACKTOP23(bank)		(MAX_BACKTOP_BASE(bank) + 0x16)
#define MAX_BACKTOP24(bank)		(MAX_BACKTOP_BASE(bank) + 0x17)
#define MAX_BACKTOP25(bank)		(MAX_BACKTOP_BASE(bank) + 0x18)
#define MAX_BACKTOP26(bank)		(MAX_BACKTOP_BASE(bank) + 0x19)
#define MAX_BACKTOP27(bank)		(MAX_BACKTOP_BASE(bank) + 0x1a)
#define MAX_BACKTOP28(bank)		(MAX_BACKTOP_BASE(bank) + 0x1b)
#define MAX_BACKTOP29(bank)		(MAX_BACKTOP_BASE(bank) + 0x1c)
#define MAX_BACKTOP30(bank)		(MAX_BACKTOP_BASE(bank) + 0x1d)
#define MAX_BACKTOP31(bank)		(MAX_BACKTOP_BASE(bank) + 0x1e)
#define MAX_BACKTOP32(bank)		(MAX_BACKTOP_BASE(bank) + 0x1f)

#define MAX96712_FSYNC_0		0x4a0
#define MAX96712_FSYNC_5		0x4a5
#define MAX96712_FSYNC_6		0x4a6
#define MAX96712_FSYNC_7		0x4a7
#define MAX96712_FSYNC_8		0x4a8
#define MAX96712_FSYNC_9		0x4a9
#define MAX96712_FSYNC_10		0x4aa
#define MAX96712_FSYNC_11		0x4ab
#define MAX96712_FSYNC_15		0x4af
#define MAX96712_FSYNC_17		0x4b1

#define MAX_MIPI_PHY_BASE		0x8a0
#define MAX_MIPI_PHY0			(MAX_MIPI_PHY_BASE + 0x00)
#define MAX_MIPI_PHY2			(MAX_MIPI_PHY_BASE + 0x02)
#define MAX_MIPI_PHY3			(MAX_MIPI_PHY_BASE + 0x03)
#define MAX_MIPI_PHY4			(MAX_MIPI_PHY_BASE + 0x04)
#define MAX_MIPI_PHY5			(MAX_MIPI_PHY_BASE + 0x05)
#define MAX_MIPI_PHY6			(MAX_MIPI_PHY_BASE + 0x06)
#define MAX_MIPI_PHY8			(MAX_MIPI_PHY_BASE + 0x08)
#define MAX_MIPI_PHY9			(MAX_MIPI_PHY_BASE + 0x09)
#define MAX_MIPI_PHY10			(MAX_MIPI_PHY_BASE + 0x0a)
#define MAX_MIPI_PHY11			(MAX_MIPI_PHY_BASE + 0x0b)
#define MAX_MIPI_PHY13			(MAX_MIPI_PHY_BASE + 0x0d)
#define MAX_MIPI_PHY14			(MAX_MIPI_PHY_BASE + 0x0e)

#define MAX_MIPI_TX_BASE(n)		(0x900 + 0x40 * n)
#define MAX_MIPI_TX2(n)			(MAX_MIPI_TX_BASE(n) + 0x02)
#define MAX_MIPI_TX10(n)		(MAX_MIPI_TX_BASE(n) + 0x0a)
#define MAX_MIPI_TX11(n)		(MAX_MIPI_TX_BASE(n) + 0x0b)
#define MAX_MIPI_TX12(n)		(MAX_MIPI_TX_BASE(n) + 0x0c)

/* 16 pairs of source-dest registers */
#define MAX_MIPI_MAP_SRC(pipe, n)	(MAX_MIPI_TX_BASE(pipe) + 0x0d + (2 * n))
#define MAX_MIPI_MAP_DST(pipe, n)	(MAX_MIPI_TX_BASE(pipe) + 0x0e + (2 * n))
/* Phy dst. Each reg contains 4 dest */
#define MAX_MIPI_MAP_DST_PHY(pipe, n)	(MAX_MIPI_TX_BASE(pipe) + 0x2d + n)

#define MAX_GMSL1_2(ch)			(0xb02 + (0x100 * ch))
#define MAX_GMSL1_4(ch)			(0xb04 + (0x100 * ch))
#define MAX_GMSL1_6(ch)			(0xb06 + (0x100 * ch))
#define MAX_GMSL1_7(ch)			(0xb07 + (0x100 * ch))
#define MAX_GMSL1_8(ch)			(0xb08 + (0x100 * ch))
#define MAX_GMSL1_D(ch)			(0xb0d + (0x100 * ch))
#define MAX_GMSL1_F(ch)			(0xb0f + (0x100 * ch))
#define MAX_GMSL1_19(ch)		(0xb19 + (0x100 * ch))
#define MAX_GMSL1_1B(ch)		(0xb1b + (0x100 * ch))
#define MAX_GMSL1_1D(ch)		(0xb1d + (0x100 * ch))
#define MAX_GMSL1_20(ch)		(0xb20 + (0x100 * ch))
#define MAX_GMSL1_96(ch)		(0xb96 + (0x100 * ch))
#define MAX_GMSL1_CA(ch)		(0xbca + (0x100 * ch))
#define MAX_GMSL1_CB(ch)		(0xbcb + (0x100 * ch))

#define MAX_RLMS4(ch)			(0x1404 + (0x100 * ch))
#define MAX_RLMSA(ch)			(0x140A + (0x100 * ch))
#define MAX_RLMSB(ch)			(0x140B + (0x100 * ch))
#define MAX_RLMSA4(ch)			(0x14a4 + (0x100 * ch))

#define MAX_RLMS58(ch)			(0x1458 + (0x100 * ch))
#define MAX_RLMS59(ch)			(0x1459 + (0x100 * ch))
#define MAX_RLMS95(ch)			(0x1495 + (0x100 * ch))
#define MAX_RLMSC4(ch)			(0x14c4 + (0x100 * ch))
#define MAX_RLMSC5(ch)			(0x14c5 + (0x100 * ch))

static inline int max96712_write(struct max96712_priv *priv, int reg, int val)
{
	int ret;

	ret = regmap_write(priv->regmap, reg, val);
	if (ret)
		dev_dbg(&priv->client->dev, "write register 0x%04x failed (%d)\n", reg, ret);

	return ret;
}

static inline int max96712_read(struct max96712_priv *priv, int reg, int *val)
{
	int ret;

	ret = regmap_read(priv->regmap, reg, val);
	if (ret)
		dev_dbg(&priv->client->dev, "read register 0x%04x failed (%d)\n", reg, ret);

	return ret;
}

static inline int max96712_update_bits(struct max96712_priv *priv, int reg, int mask, int bits)
{
	int ret;

	ret = regmap_update_bits(priv->regmap, reg, mask, bits);
	if (ret)
		dev_dbg(&priv->client->dev, "update register 0x%04x failed (%d)\n", reg, ret);

	return ret;
}

#define des_read(reg, val)			max96712_read(priv, reg, val)
#define des_write(reg, val)			max96712_write(priv, reg, val)
#define des_update_bits(reg, mask, bits)	max96712_update_bits(priv, reg, mask, bits)

static inline int max96712_ser_write(struct max96712_link *link, int reg, int val)
{
	int ret;

	ret = regmap_write(link->regmap, reg, val);
	if (ret < 0)
		dev_dbg(&link->client->dev, "write register 0x%04x failed (%d)\n", reg, ret);

	return ret;
}

static inline int max96712_ser_read(struct max96712_link *link, int reg, int *val)
{
	int ret;

	ret = regmap_read(link->regmap, reg, val);
	if (ret)
		dev_dbg(&link->client->dev, "read register 0x%04x failed (%d)\n", reg, ret);

	return ret;
}

static inline int max96712_ser_update_bits(struct max96712_link *link, int reg, int mask, int bits)
{
	int ret;

	ret = regmap_update_bits(link->regmap, reg, mask, bits);
	if (ret)
		dev_dbg(&link->client->dev, "update register 0x%04x failed (%d)\n", reg, ret);

	return ret;
}

#define ser_read(reg, val)			max96712_ser_read(link, reg, (int *)val)
#define ser_write(reg, val)			max96712_ser_write(link, reg, val)
#define ser_update_bits(reg, mask, bits)	max96712_ser_update_bits(link, reg, mask, bits)
