/*
 * MAXIM max96712 GMSL2 driver debug stuff
 *
 * Copyright (C) 2019-2020 Cogent Embedded, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

static char *pipe_names[4] = {
	"X", "Y", "Z", "U"
};

static int max96712_gmsl1_get_link_lock(struct max96712_priv *priv, int link_n);
static int max96712_gmsl2_get_link_lock(struct max96712_priv *priv, int link_n);

#define reg_bits(x, y)	((reg >> (x)) & ((1 << (y)) - 1))

static ssize_t max_link_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct max96712_priv *priv = i2c_get_clientdata(client);
	int i = -1;
	int j;
	int gmsl2;
	u32 crc = 0 ;
	char *_buf = buf;
	int reg = 0;

	if (!sscanf(attr->attr.name, "link_%d", &i))
		return -EINVAL;

	if (i < 0)
		return -EINVAL;

	if (i >= priv->n_links) {
		buf += sprintf(buf, "\n");
		return (buf - _buf);
	}

	buf += sprintf(buf, "Link %c status\n", 'A' + i);

	des_read(MAX96712_REG6, &reg);
	gmsl2 = !!(reg & BIT(4 + i));
	buf += sprintf(buf, "Link mode: %s\n", gmsl2 ? "GMSL2" : "GMSL1");

	if (gmsl2) {
		buf += sprintf(buf, "GMSL2 Link lock: %d\n", max96712_gmsl2_get_link_lock(priv, i));
	} else {
		reg = max96712_gmsl1_get_link_lock(priv, i);
		buf += sprintf(buf,
				"GMSL1_CB: 0x%02x:\t"
				"LOCKED_G1: %d\n",
				reg, reg_bits(0, 1));

		des_read(MAX_GMSL1_CA(i), &reg);
		buf += sprintf(buf,
				"GMSL1_CA: 0x%02x:\t"
				"PHASELOCK: %d, WBLOCK_G1: %d, DATAOK: %d\n",
				reg, reg_bits(2, 1), reg_bits(1, 1), reg_bits(0, 1));

		des_read(MAX_GMSL1_1B(i), &reg);
		buf += sprintf(buf,
				"GMSL1_1B: 0x%02x:\t"
				"LINE_CRC_ERR: %d ",
				reg, reg_bits(2, 1));
		for (j = 0; j < 4; j++) {
			des_read(MAX_GMSL1_20(i) + j, &reg);
			crc = crc | ((reg & 0xff) << (j * 8));
		}
		buf += sprintf(buf, "last crc 0x%08x\n", crc);

		des_read(MAX_GMSL1_19(i), &reg);
		buf += sprintf(buf,
				"GMSL1_19: CC_CRC_ERRCNT %d\n",
				reg);

		des_read(MAX_GMSL1_1D(i), &reg);
		buf += sprintf(buf,
				"GMSL1_1D: 0x%02x:\t"
				"UNDERBOOST: %d, AEQ-BST: %d\n",
				reg, reg_bits(4, 1), reg_bits(0, 4));
	}

	return (buf - _buf);
}

static ssize_t max_pipe_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct max96712_priv *priv = i2c_get_clientdata(client);
	char *_buf = buf;
	int pipe = 0;
	int map;
	int maps_en = 0;
	int pipes_en;
	int reg = 0;
	int shift;

	if (!sscanf(attr->attr.name, "pipe_%d", &pipe))
		return -EINVAL;

	if (pipe < 0)
		return -EINVAL;

	if (pipe >= MAX96712_MAX_PIPES) {
		buf += sprintf(buf, "\n");
		return (buf - _buf);
	}

	des_read(MAX96712_VIDEO_PIPE_EN, &pipes_en);

	buf += sprintf(buf, "Video Pipe %d %s\n",
		pipe, (pipes_en & BIT(pipe)) ? "ENABLED" : "disabled");
	if (!(pipes_en & BIT(pipe)))
		goto out;

	des_read(MAX_VPRBS(pipe), &reg);
	/* bit 5 is not valid for MAX96712 */
	buf += sprintf(buf,
			"\tVPRBS: 0x%02x\t"
			"VPRBS_FAIL: %d,"
			"VIDEO_LOCK: %d\n",
			reg,
			reg_bits(5, 1), reg_bits(0, 1));

	/* show source */
	shift = (pipe % 2) * 4;
	des_read(MAX96712_VIDEO_PIPE_SEL(pipe / 2), &reg);
	buf += sprintf(buf, "SRC: PHY %c, PIPE %s\n",
		'A' + (char)((reg >> (shift + 2)) & 0x03),
		pipe_names[(reg >> shift) & 0x03]);

	/* show maps */
	des_read(MAX_MIPI_TX11(pipe), &maps_en);
	des_read(MAX_MIPI_TX12(pipe), &reg);
	maps_en |= reg << 8;

	for (map = 0; map < MAX96712_MAX_PIPE_MAPS; map++) {
		int src, dst, mipi;
		if (!(maps_en & BIT(map)))
			continue;

		des_read(MAX_MIPI_MAP_SRC(pipe, map), &src);
		des_read(MAX_MIPI_MAP_DST(pipe, map), &dst);
		des_read(MAX_MIPI_MAP_DST_PHY(pipe, map / 4), &mipi);

		buf += sprintf(buf, " MAP%d: DT %02x, VC %d -> DT %02x, VC %d MIPI %d\n",
			map,
			src & 0x3f, (src >> 6) & 0x03, dst & 0x3f, (dst >> 6) & 0x03,
			(mipi >> ((map % 4) * 2)) & 0x03);
	}

	des_read(MAX_VIDEO_RX0(pipe), &reg);
	buf += sprintf(buf,
			"VIDEO_RX0: 0x%02x\t"
			"LCRC_ERR: %d, "
			"LINE_CRC_SEL: %d, "
			"LINE_CRC_EN: %d, "
			"DIS_PKT_DET: %d\n",
			reg,
			reg_bits(7, 1),
			reg_bits(2, 1), reg_bits(1, 1), reg_bits(0, 1));
	des_read(MAX_VIDEO_RX3(pipe), &reg);
	buf += sprintf(buf,
			"VIDEO_RX3: 0x%02x\t"
			"HD_TR_MODE: %d, "
			"DLOCKED: %d, "
			"VLOCKED: %d, "
			"HLOCKED: %d, "
			"DTRACKEN: %d, "
			"VTRACKEN: %d, "
			"HTRACKEN: %d\n",
			reg,
			reg_bits(6, 1),
			reg_bits(5, 1), reg_bits(4, 1), reg_bits(3, 1),
			reg_bits(2, 1), reg_bits(1, 1), reg_bits(0, 1));
	des_read(MAX_VIDEO_RX8(pipe), &reg);
	buf += sprintf(buf,
			"VIDEO_RX8: 0x%02x\t"
			"VID_BLK_LEN_ERR: %d, "
			"VID_LOCK: %d, "
			"VID_PKT_DET: %d, "
			"VID_SEQ_ERR: %d\n",
			reg,
			reg_bits(7, 1), reg_bits(6, 1),
			reg_bits(5, 1), reg_bits(4, 1));
	des_read(MAX_VIDEO_RX10(pipe), &reg);
	buf += sprintf(buf,
			"VIDEO_RX10: 0x%02x\t"
			"MASK_VIDEO_DE: %d\n",
			reg,
			reg_bits(6, 1));

out:
	return (buf - _buf);
}

static ssize_t max_stat_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct max96712_priv *priv = i2c_get_clientdata(client);
	int i;
	char *_buf = buf;
	int reg = 0;

	/* TODO: add same for 96712 */
	des_read(MAX96712_REG4, &reg);
	buf += sprintf(buf,
			"REG_REG4: 0x%02x\t"
			"LOCK_CFG: %d\n",
			reg, reg_bits(5, 1));

	des_read(MAX_BACKTOP1(0), &reg);
	buf += sprintf(buf,
			"BACKTOP1: 0x%02x:\t"
			"CSIPLL3_LOCK: %d, "
			"CSIPLL2_LOCK: %d, "
			"CSIPLL1_LOCK: %d, "
			"CSIPLL0_LOCK: %d\n",
			reg,
			reg_bits(7, 1), reg_bits(6, 1), reg_bits(5, 1), reg_bits(4, 1));

	des_read(MAX_BACKTOP11(0), &reg);
	buf += sprintf(buf,
			"BACKTOP11: 0x%02x:\t"
			"CMD_OWERFLOW4: %d, "
			"CMD_OWERFLOW3: %d, "
			"CMD_OWERFLOW2: %d, "
			"CMD_OWERFLOW1: %d, "
			"LMO_3: %d, "
			"LMO_2: %d, "
			"LMO_1: %d, "
			"LMO_0: %d\n",
			reg,
			reg_bits(7, 1), reg_bits(6, 1), reg_bits(5, 1), reg_bits(4, 1),
			reg_bits(3, 1), reg_bits(2, 1), reg_bits(1, 1), reg_bits(0, 1));

	for (i = 0; i < MAX96712_MAX_MIPI; i++) {
		buf += sprintf(buf, "MIPI %d\n", i);
		des_read(MAX_MIPI_TX2(i), &reg);
		buf += sprintf(buf,
				"\tMIPI_TX2: 0x%02x\n",
				reg);
	}

	return (buf - _buf);
}

static DEVICE_ATTR(link_0, S_IRUGO, max_link_show, NULL);
static DEVICE_ATTR(link_1, S_IRUGO, max_link_show, NULL);
static DEVICE_ATTR(link_2, S_IRUGO, max_link_show, NULL);
static DEVICE_ATTR(link_3, S_IRUGO, max_link_show, NULL);
static DEVICE_ATTR(pipe_0, S_IRUGO, max_pipe_show, NULL);
static DEVICE_ATTR(pipe_1, S_IRUGO, max_pipe_show, NULL);
static DEVICE_ATTR(pipe_2, S_IRUGO, max_pipe_show, NULL);
static DEVICE_ATTR(pipe_3, S_IRUGO, max_pipe_show, NULL);
static DEVICE_ATTR(pipe_4, S_IRUGO, max_pipe_show, NULL);
static DEVICE_ATTR(pipe_5, S_IRUGO, max_pipe_show, NULL);
static DEVICE_ATTR(pipe_6, S_IRUGO, max_pipe_show, NULL);
static DEVICE_ATTR(pipe_7, S_IRUGO, max_pipe_show, NULL);
static DEVICE_ATTR(stat, S_IRUGO, max_stat_show, NULL);

static struct attribute *max96712_attributes[] = {
	&dev_attr_link_0.attr,
	&dev_attr_link_1.attr,
	&dev_attr_link_2.attr,
	&dev_attr_link_3.attr,
	&dev_attr_pipe_0.attr,
	&dev_attr_pipe_1.attr,
	&dev_attr_pipe_2.attr,
	&dev_attr_pipe_3.attr,
	&dev_attr_pipe_4.attr,
	&dev_attr_pipe_5.attr,
	&dev_attr_pipe_6.attr,
	&dev_attr_pipe_7.attr,
	&dev_attr_stat.attr,
	NULL
};

static const struct attribute_group max96712_group = {
	.attrs = max96712_attributes,
};

int max96712_debug_add(struct max96712_priv *priv)
{
	int ret;

	ret = sysfs_create_group(&priv->client->dev.kobj,  &max96712_group);
	if (ret < 0) {
		dev_err(&priv->client->dev, "Sysfs registration failed\n");
		return ret;
	}

	return ret;
}

void max96712_debug_remove(struct max96712_priv *priv)
{
	sysfs_remove_group(&priv->client->dev.kobj, &max96712_group);
}

#if 0
int max96712_patgen(struct max96712_priv *priv)
{
	int ret = 0;

	const u32 xres = 1280;
	const u32 yres = 800;
	const u32 hbp = 128;
	const u32 hfp = 80;
	const u32 hsa = 32;
	const u32 vbp = 17;
	const u32 vfp = 4;
	const u32 vsa = 3;

	u32 vtotal = vfp + vsa + vbp + yres;
	u32 htotal = xres + hfp + hbp + hsa;
	u32 vs_high = vsa * htotal;
	u32 vs_low = (vfp + yres + vbp) * htotal;
	u32 v2h = (vsa + vbp) * htotal + hfp;
	u32 hs_high = hsa;
	u32 hs_low = xres + hfp + hbp;
	u32 v2d = v2h + hsa + hbp;
	u32 de_high = xres;
	u32 de_low = hfp + hsa + hbp;
	u32 de_cnt = yres;

	/* DEBUG_EXTRA & PATGEN_CLK_SRC = 75Mhz pclk */
	des_write(0x0009, 0x01); /* if DEBUG_EXTRA[1:0] = 2b01, PCLK Frequency is 75MHz (don't care PATGEN_CLK_SRC) */
	des_write(0x01dc, 0x00);

	des_write_n(0x1052, 3, 0);		/* vs delay */
	des_write_n(0x1055, 3, vs_high);
	des_write_n(0x1058, 3, vs_low);
	des_write_n(0x105B, 3, v2h);
	des_write_n(0x105E, 2, hs_high);
	des_write_n(0x1060, 2, hs_low);
	des_write_n(0x1062, 2, vtotal);		/* hs cnt */
	des_write_n(0x1064, 3, v2d);
	des_write_n(0x1067, 2, de_high);
	des_write_n(0x1069, 2, de_low);
	des_write_n(0x106B, 2, de_cnt);

	des_write_n(0x106E, 3, 0xff0000);	/* color A */
	des_write_n(0x1071, 3, 0x0000ff);	/* color B */

	des_write(0x1074, 0x50);	/* chkr_rpt_a = 80 */
	des_write(0x1075, 0x50);	/* chkr_rpt_b = 80 */
	des_write(0x1076, 0x50);	/* chkr_alt = 80 */

	des_write(0x1050, 0xfb);	/* gen_vs,gen_hs,gen_de, vtg[0:1] */
	des_write(0x1051, 0x10);	/* patgen_mode[5:4] = 0b1,checkerboard */

out:
	return ret;
}
#endif
