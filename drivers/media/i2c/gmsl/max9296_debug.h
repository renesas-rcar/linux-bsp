/*
 * MAXIM max9296 GMSL2 driver debug stuff
 *
 * Copyright (C) 2019-2020 Cogent Embedded, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

static char *max9296_link_mode[4] = {
	"Splitter mode",
	"Link A",
	"Link B",
	"Dual link",
};

static char *line_status[8] = {
	"Short to battery",
	"Short to GND",
	"Normal operation",
	"Line open",
	"Line-to-line short",
	"Line-to-line short",
	"Line-to-line short",
	"Line-to-line short"
};

static char *paxket_cnt_types[] = {
	"None",
	"VIDEO",
	"AUDIO",
	"INFO Frame",
	"SPI",
	"I2C",
	"UART",
	"GPIO",
	"AHDCP",
	"RGMII",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"All",
	"Unknown and packets with error",
};

static int max9296_gmsl1_get_link_lock(struct max9296_priv *priv, int link_n);
static int max9296_gmsl2_get_link_lock(struct max9296_priv *priv, int link_n);

#define reg_bits(x, y)	((reg >> (x)) & ((1 << (y)) - 1))

static ssize_t max_link_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct max9296_priv *priv = i2c_get_clientdata(client);
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

	des_read(MAX9296_REG6, &reg);
	gmsl2 = !!(reg & BIT(6 + i));
	buf += sprintf(buf, "Link mode: %s\n", gmsl2 ? "GMSL2" : "GMSL1");

	if (gmsl2) {
		buf += sprintf(buf, "GMSL2 Link lock: %d\n",
				max9296_gmsl2_get_link_lock(priv, i));
	} else {
		reg = max9296_gmsl1_get_link_lock(priv, i);
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

		{
			des_read(MAX9296_CTRL1, &reg);
			buf += sprintf(buf,
					"CTRL1: 0x%02x:\t"
					"Cable: %s\n",
					reg,
					reg_bits(i * 2, 1) ? "coax" : "stp");

			des_read(MAX9296_REG26, &reg);
			buf += sprintf(buf,
					"REG26: 0x%02x:\t"
					"Line status: %s\n",
					reg,
					line_status[reg_bits(i * 4, 3)]);

			des_read(MAX9296_CNT(i), &reg);
			buf += sprintf(buf,
					"CNT%d: DEC_ERR_x: %d\n",
					i, reg);
		}
		/* TODO: add same for 96712 */
	}

	return (buf - _buf);
}

static ssize_t max_pipe_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct max9296_priv *priv = i2c_get_clientdata(client);
	char *_buf = buf;
	int pipe = 0;
	int map;
	int maps_en = 0;
	int pipes_en;
	int reg = 0;

	if (!sscanf(attr->attr.name, "pipe_%d", &pipe))
                return -EINVAL;

	if (pipe < 0)
		return -EINVAL;

	if (pipe >= MAX9296_MAX_PIPES) {
		buf += sprintf(buf, "\n");
		return (buf - _buf);
	}

	des_read(MAX9296_REG2, &pipes_en);
	pipes_en = pipes_en >> 4;

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
	/* TODO */

	/* show maps */
	des_read(MAX_MIPI_TX11(pipe), &maps_en);
	des_read(MAX_MIPI_TX12(pipe), &reg);
	maps_en |= reg << 8;

	for (map = 0; map < MAX9296_MAX_PIPE_MAPS; map++) {
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

	des_read(MAX9296_CNT4 + pipe, &reg);
	buf += sprintf(buf, "VID_PXL_CRC_ERR: 0x%02x\n", reg);

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
	struct max9296_priv *priv = i2c_get_clientdata(client);
	int i;
	char *_buf = buf;
	int reg = 0, reg2 = 0;

	des_read(MAX9296_REG3, &reg);
	buf += sprintf(buf,
			"REG_REG3: 0x%02x\t"
			"LOCK_CFG: %d\n",
			reg, reg_bits(7, 1));

	des_read(MAX9296_CTRL0, &reg);
	buf += sprintf(buf,
			"CTRL0: 0x%02x\n",
			reg);

	des_read(MAX9296_CTRL3, &reg);
	buf += sprintf(buf,
			"CTRL3: 0x%02x:\t"
				"LINK_MODE: %s, "
			"GMSL2 LOCKED: %d, ERROR: %d, "
			"CMU_LOCKED: %d\n",
			reg,
			max9296_link_mode[reg_bits(4, 2)],
			reg_bits(3, 1), reg_bits(2 ,1),
			reg_bits(1, 1));
	/* get errors */
	if (reg_bits(2, 1)) {
		des_read(MAX9296_INTR3, &reg);
		buf += sprintf(buf,
			"INTR3: 0x%02x:\t"
			"PHY_INT_OEN_B: %d "
			"PHY_INT_OEN_A: %d "
			"REM_ERR_FLAG: %d "
			"MEM_INT_ERR_FLAG: %d "
			"LFLT_INT: %d "
			"IDLE_ERR_FLAG: %d "
			"DEC_ERR_FLAG_B: %d "
			"DEC_ERR_FLAG_A: %d\n",
			reg,
			reg_bits(7, 1), reg_bits(6, 1), reg_bits(5, 1), reg_bits(4, 1),
			reg_bits(3, 1), reg_bits(2, 1), reg_bits(1, 1), reg_bits(0, 1));
		des_read(MAX9296_INTR5, &reg);
		buf += sprintf(buf,
			"INTR5: 0x%02x:\t"
			"EOM_ERR_FLAG_B: %d "
			"EOM_ERR_FLAG_A: %d "
			"MAX_RT_FLAG: %d "
			"RT_CNT_FLAG: %d "
			"PKT_CNT_FLAG: %d "
			"WM_ERR_FLAG: %d\n",
			reg,
			reg_bits(7, 1), reg_bits(6, 1),
			reg_bits(3, 1), reg_bits(2, 1), reg_bits(1, 1), reg_bits(0, 1));
		des_read(MAX9296_INTR7, &reg);
		buf += sprintf(buf,
				"INTR7: 0x%02x:\t"
				"VDDCMP_INT_FLAG: %d "
				"PORZ_INT_FLAG: %d "
				"VDDBAD_INT_FLAG: %d "
				"LCRC_ERR_FLAG: %d "
				"VPRBS_ERR_FLAG: %d "
				"VID_PXL_CRC_ERR: %d\n",
				reg,
				reg_bits(7, 1), reg_bits(6, 1), reg_bits(5, 1),
				reg_bits(3, 1), reg_bits(2, 1), reg_bits(0, 1));

			des_read(MAX9296_DEC_ERR_A, &reg);
			buf += sprintf(buf,
					"ERR_A: 0x%02x\n", reg);
			des_read(MAX9296_DEC_ERR_B, &reg);
			buf += sprintf(buf,
					"ERR_B: 0x%02x\n", reg);
			des_read(MAX9296_IDLE_ERR, &reg);
			buf += sprintf(buf,
					"IDLE_ERR: 0x%02x\n", reg);
			des_read(MAX9296_PKT_CNT, &reg);
			buf += sprintf(buf,
					"PKT_CNT: 0x%02x\n", reg);

		}

		des_read(MAX9296_CNT(2), &reg);
		buf += sprintf(buf,
				"CNT2: IDLE_ERR: %d\n",
				reg);

		des_read(MAX9296_CNT(3), &reg);
		des_read(MAX9296_RX_0, &reg2);
		buf += sprintf(buf,
				"CNT3: PKT_CNT: 0x%02x (type %x: %s)\n",
				reg, reg2 & 0x0f,
				paxket_cnt_types[reg2 & 0x0f]);

		des_read(MAX9296_RX_3, &reg);
		buf += sprintf(buf,
				"RX3: 0x%02x:\t"
				"PRBS_SYNCED_B: %d, "
				"SYNC_LOCKED_B: %d, "
				"WBLOCK_B: %d, "
				"FAILLOCK_B: %d, "
				"PRBS_SYNCED_A: %d, "
				"SYNC_LOCKED_A: %d, "
				"WBLOCK_A: %d, "
				"FAILLOCK_A: %d\n",
				reg,
				reg_bits(7, 1), reg_bits(6, 1), reg_bits(5, 1), reg_bits(4, 1),
				reg_bits(3, 1), reg_bits(2, 1), reg_bits(1, 1), reg_bits(0, 1));

		des_read(MAX_BACKTOP1(0), &reg);
		buf += sprintf(buf,
				"BACKTOP1: 0x%02x:\t"
				"CSIPLLU_LOCK: %d, "
				"CSIPLLZ_LOCK: %d, "
				"CSIPLLY_LOCK: %d, "
				"CSIPLLX_LOCK: %d, "
				"LINE_SPL2: %d, "
				"LINE_SPL1: %d\n",
				reg,
				reg_bits(7, 1), reg_bits(6, 1), reg_bits(5, 1), reg_bits(4, 1),
				reg_bits(3, 1), reg_bits(2, 1));

		des_read(MAX_BACKTOP11(0), &reg);
		buf += sprintf(buf,
				"BACKTOP11: 0x%02x:\t"
				"CMD_OWERFLOW4: %d, "
				"CMD_OWERFLOW3: %d, "
				"CMD_OWERFLOW2: %d, "
				"CMD_OWERFLOW1: %d, "
				"LMO_Z: %d, "
				"LMO_Y: %d\n",
				reg,
				reg_bits(7, 1), reg_bits(6, 1), reg_bits(5, 1), reg_bits(4, 1),
				reg_bits(2, 1), reg_bits(1, 1));

	for (i = 0; i < MAX9296_MAX_MIPI; i++) {
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

static struct attribute *max9296_attributes[] = {
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

static const struct attribute_group max9296_group = {
	.attrs = max9296_attributes,
};

int max9296_debug_add(struct max9296_priv *priv)
{
	int ret;

	ret = sysfs_create_group(&priv->client->dev.kobj, &max9296_group);
	if (ret < 0) {
		dev_err(&priv->client->dev, "Sysfs registration failed\n");
		return ret;
	}

	/* count video packets */
	des_update_bits(MAX9296_RX_0, 0x0f, 0x01);

	return ret;
}

void max9296_debug_remove(struct max9296_priv *priv)
{
	sysfs_remove_group(&priv->client->dev.kobj, &max9296_group);
}
