#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/fwnode.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>
#include <linux/i2c.h>
#include <linux/i2c-mux.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/of_graph.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

#include "max96712.h"

#define MAX96712_NUM_GMSL		4
#define MAX96712_N_SINKS		4
#define MAX96712_N_PADS			5
#define MAX96712_SRC_PAD		4

#define DEBUG_REG_DUMP			0
#define DEBUG_COLOR_PATTERN		0
#define DEBUG_MBPS			200000000

struct max96712_source {
	struct v4l2_async_subdev asd;
	struct v4l2_subdev *sd;
	struct fwnode_handle *fwnode;
	bool linkup;
};

#define asd_to_max96712_source(_asd) \
	container_of(_asd, struct max96712_source, asd)

struct max96712_link {
	struct v4l2_subdev sd;
	struct fwnode_handle *sd_fwnode;
	struct i2c_client *client;
	int ser_id;
	int ser_addr;
	int pipes_mask;			/* mask of pipes used by this link */
	int out_mipi;			/* MIPI# */
	int out_vc;			/* VC# */
	struct regulator *poc_reg;	/* PoC power supply */
};

struct max96712_priv {
	struct i2c_client *client;
	struct gpio_desc *gpiod_pwdn;
	struct v4l2_subdev sd;
	struct media_pad pads[MAX96712_N_PADS];
	struct max96712_link *link[MAX96712_NUM_GMSL];

	struct i2c_mux_core *mux;
	unsigned int mux_channel;
	bool mux_open;
	bool phy_pol_inv;
	int links_mask;
	int dt;
	int stream_count;

	struct v4l2_ctrl_handler ctrls;

	struct v4l2_mbus_framefmt fmt[MAX96712_N_SINKS];

	unsigned int nsources;
	unsigned int source_mask;
	unsigned int route_mask;
	unsigned int bound_sources;
	unsigned int csi2_data_lanes;
	struct max96712_source sources[MAX96712_NUM_GMSL];
	struct v4l2_async_notifier notifier;
};

static struct max96712_source *next_source(struct max96712_priv *priv,
					  struct max96712_source *source)
{
	if (!source)
		source = &priv->sources[0];
	else
		source++;

	for (; source < &priv->sources[MAX96712_NUM_GMSL]; source++) {
		if (source->fwnode)
			return source;
	}

	return NULL;
}

#define for_each_source(priv, source) \
	for ((source) = NULL; ((source) = next_source((priv), (source))); )

#define to_index(priv, source) ((source) - &(priv)->sources[0])

static inline struct max96712_priv *sd_to_max96712(struct v4l2_subdev *sd)
{
	return container_of(sd, struct max96712_priv, sd);
}

struct max96712_reg {
	u16	reg;
	u8	val;
};

const struct max96712_reg max96712_color_pattern_init[] = {
	{0x1050, 0xE3},	/* VRX_Patgen 0, Generate VS, HS, DE", Invert the VS */
	{0x1051, 0x20},	/* Set Patgen mode=2 (Color Gradient), Grad Mode=0 */
	{0x1052, 0x00},
	{0x1053, 0x00},
	{0x1054, 0x00},
	{0x1055, 0x25},	/* Set VS High  */
	{0x1056, 0x99},
	{0x1057, 0x00},
	{0x1058, 0x00},	/* Set VS Low  */
	{0x1059, 0x2A},
	{0x105A, 0xF8},
	{0x105B, 0x00},	/* Set HS Delay V2H */
	{0x105C, 0x00},
	{0x105D, 0x00},
	{0x105E, 0x08},	/* Set HS_HIGH */
	{0x105F, 0x6C},
	{0x1060, 0x00},	/* Set HS_LOW */
	{0x1061, 0x2C},
	{0x1062, 0x04},	/* Set HS_CNT */
	{0x1063, 0x65},
	{0x1064, 0x01},	/* Set DE Delay */
	{0x1065, 0x35},
	{0x1066, 0x60},
	{0x1067, 0x07},	/* Set DE_HIGH */
	{0x1068, 0x80},
	{0x1069, 0x01},	/* Set DE_LOW */
	{0x106A, 0x18},
	{0x106B, 0x04},	/* Set DE_CNT */
	{0x106C, 0x38},
	{0x106D, 0x03},	/* Set Grad_INCR_0_0 */

/* CHECKERBOARD SETUP - PATGEN MODE = 1 */
	{0x106E, 0x88},	/* Set CHKR_COLOR_A_L_0 */
	{0x106F, 0xAA},	/* Set CHKR_COLOR_A_M_0 */
	{0x1070, 0x55},	/* Set CHKR_COLOR_A_H_0 */
	{0x1071, 0x00},	/* Set CHKR_COLOR_B_L_0 */
	{0x1072, 0x08},	/* Set CHKR_COLOR_B_M_0 */
	{0x1073, 0x80},	/* Set CHKR_COLOR_B_H_0 */
	{0x1074, 0x50},	/* Set CHKR_RPT_A_0 */
	{0x1075, 0xA0},	/* Set CHKR_RPT_B_0 */
	{0x1076, 0x50},	/* Set CHKR_ALT_0 */

/* Set Patgen Clk frequency 75MHz firstly */
	{0x0009, 0x01},	/*75MHz -> 150MHz */
	{0x01DC, 0x00},
	{0x01FC, 0x00},
	{0x021C, 0x00},
	{0x023C, 0x00},
/*
 * RGB888 Software override VC/DT/BPP for Pipe 1,2,3
 * and 4; 0x24 = 6'b100100, 24 = 5'b11000; 0x2C = 6'b101100, 12 = 5'b01100
 */
	{0x040B, 0xC2},	/* BPP override for 1 */
	{0x040C, 0x10},	/* VC override for 1/2/3/4 */
	{0x040D, 0x32},
	{0x040E, 0xA4},	/* DT override for 1/2/3/4 */
	{0x040F, 0x94},
	{0x0410, 0x90},
	{0x0411, 0xD8},	/* BPP override for 2/3/4 */
	{0x0412, 0x60},

	{0x0006, 0x00},	/* Disable GMSL link */
	{0x0415, 0xE9},	/* Set 1200M DPLL frequency,
			 * Enable the software override for 1/2/3/4
			 */
	{0x0418, 0xE9},
	{0x094A, 0xC0},	/* Set Lane Count-4 for script (CSI_NUM_LANES),
			 * DPHY ONLY
			 */
	{0x08A3, 0xE4},	/* Set Phy lane Map for all MIPI PHYs */
	{0x090B, 0x07},	/* Set MAP_EN_L_0 for map 0 */
	{0x094B, 0x07},	/* Set MAP_EN_L_0 for map 0 */
	{0x098B, 0x07},	/* Set MAP_EN_L_0 for map 0 */
	{0x09CB, 0x07},	/* Set MAP_EN_L_0 for map 0 */
	{0x092D, 0x15},	/* Set MAP_DPHY_DEST TO CTRL 0 */
	{0x096D, 0x00},	/* Set MAP_DPHY_DEST TO CTRL 1 */
	{0x09AD, 0x2a},	/* Set MAP_DPHY_DEST TO CTRL 2 */
	{0x09ED, 0x3f},	/* Set MAP_DPHY_DEST TO CTRL 3 */
	{0x090D, 0x24},	/* Set MAP_SRC0 [7:6]=VC=0, [5:0]=DT=RGB888 */
	{0x094D, 0x64},	/* Set MAP_SRC1 [7:6]=VC=1, [5:0]=DT=RGB888 */
	{0x098D, 0xa4},	/* Set MAP_SRC2 [7:6]=VC=2, [5:0]=DT=RGB888 */
	{0x09CD, 0xe4},	/* Set MAP_SRC3 [7:6]=VC=3, [5:0]=DT=RGB888 */
	{0x090E, 0x24},
	{0x094E, 0x64},
	{0x098E, 0xa4},
	{0x09CE, 0xe4},
	{0x090F, 0x00},
	{0x094F, 0x00},
	{0x098F, 0x00},
	{0x09CF, 0x00},
	{0x0910, 0x00},
	{0x0950, 0x40},
	{0x0990, 0x80},
	{0x09d0, 0xc0},
	{0x0911, 0x01},
	{0x0951, 0x01},
	{0x0991, 0x01},
	{0x09d1, 0x01},
	{0x0912, 0x01},
	{0x0952, 0x41},
	{0x0992, 0x81},
	{0x09d2, 0xc1},
	{0xFFFF, 0xFF}	/* End Table */

};

/* -----------------------------------------------------------------------------
 * I2C IO
 */

static int max96712_write_reg(struct max96712_priv *priv, u16 reg, u8 val)
{
	u8 regbuf[3];
	int ret;

	regbuf[0] = reg >> 8;
	regbuf[1] = reg & 0xff;
	regbuf[2] = val;

	ret = i2c_master_send(priv->client, regbuf, 3);
	msleep(5);
	if (ret < 0) {
		dev_err(&priv->client->dev,
			"%s: write reg error %d: reg=%x, val=%x\n",
			__func__, ret, reg, val);
		return ret;
	}

	return 0;
}

static int max96712_read(struct max96712_priv *priv, u16 reg, u8 *val)
{
	u8 regbuf[2];
	int ret;

	regbuf[0] = reg >> 8;
	regbuf[1] = reg & 0xff;

	ret = i2c_master_send(priv->client, regbuf, 2);
	if (ret < 0) {
		dev_err(&priv->client->dev, "%s: write reg error %d: reg=%x\n",
			__func__, ret, reg);
		return ret;
	}

	ret = i2c_master_recv(priv->client, val, 1);
	if (ret < 0) {
		dev_err(&priv->client->dev, "%s: read reg error %d: reg=%x\n",
			__func__, ret, reg);
		return ret;
	}

	return 0;
}

static inline int max96712_update_bits(struct max96712_priv *priv, u16 reg,
				       u8 mask, u8 bits)
{
	int ret, tmp;
	u8 val;

	ret = max96712_read(priv, reg, &val);
	if (ret != 0)
		return ret;

	tmp = val & ~mask;
	tmp |= bits & mask;

	if (tmp != val)
		ret = max96712_write_reg(priv, reg, tmp);

	return ret;
}

#if DEBUG_COLOR_PATTERN
static int max96712_set_regs(struct max96712_priv *priv,
			     const struct max96712_reg *regs,
			     unsigned int nr_regs)
{
	unsigned int i;
	int ret;

	for (i = 0; i < nr_regs; i++) {
		ret = max96712_write_reg(priv, regs[i].reg, regs[i].val);
		if (ret)
			return ret;
	}

	return 0;
}

static int max96712_color_pattern(struct max96712_priv *priv)
{
	int ret;

	ret = max96712_set_regs(priv, max96712_color_pattern_init,
			ARRAY_SIZE(max96712_color_pattern_init));

	max96712_write_reg(priv, 0x0006, 0xFF);
	max96712_write_reg(priv, 0x0010, 0x22);
	max96712_write_reg(priv, 0x0011, 0x22);
	max96712_write_reg(priv, 0x0018, 0x0F);

	msleep(100); /* Delay ~100ms */

	max96712_write_reg(priv, 0x0009, 0x02);
	max96712_write_reg(priv, 0x08A0, 0x84);
	max96712_write_reg(priv, 0x08A2, 0x30);
	max96712_write_reg(priv, 0x0018, 0x0F);

	return ret;
}
#endif

#if DEBUG_REG_DUMP
static void max96712_debug_dump(struct max96712_priv *priv)
{
	int i;
	u8 val, valu[0x1000];

	for (i = 0; i < 0x1000; i++) {
		max96712_read(priv, i, &val);
		valu[i] = val;
	}

	for (i = 0; i < 0x1000; i += 16)
		printk("0x%04x\t%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x \n",
			i, valu[i], valu[i+1], valu[i+2], valu[i+3], valu[i+4], valu[i+5], valu[i+6], valu[i+7], valu[i+8],
			valu[i+9], valu[i+10], valu[i+11], valu[i+12], valu[i+13], valu[i+14], valu[i+15]);
}
#endif
static int __max9295a_write(struct max96712_link *link, u16 reg, u8 val)
{
	u8 buf[3] = { reg >> 8, reg & 0xff, val };
	int ret;

	ret = i2c_master_send(link->client, buf, 3);
	return ret < 0 ? ret : 0;
}

static int max9295a_set_regs(struct max96712_link *link,
			     const struct max9295a_reg *regs,
			     unsigned int nr_regs)
{
	unsigned int i;
	int ret;

	for (i = 0; i < nr_regs; i++) {
		ret = __max9295a_write(link, regs[i].reg, regs[i].val);
		msleep(5);
		if (ret)
			return ret;
	}

	return 0;
}

static int max9295a_sensor_set_regs(struct max96712_priv *priv, u32 link_nr)
{
	int ret;
	struct max96712_link *link;

	link = priv->link[link_nr];

	__max9295a_write(link, 0x0010, 0x21);	/* SW reset */
	msleep(200);

	/* Program the camera sensor initial configuration. */
	ret = max9295a_set_regs(link, configuretable_ar0231,
				ARRAY_SIZE(configuretable_ar0231));
	msleep(200);

	return ret;
}

/* -----------------------------------------------------------------------------
 * I2C Multiplexer
 */
static int max96712_i2c_mux_select(struct i2c_mux_core *muxc, u32 chan)
{
	struct max96712_priv *priv = i2c_mux_priv(muxc);

	/* Channel select is disabled when configured in the opened state. */
	if (priv->mux_open)
		return 0;

	if (priv->mux_channel == chan)
		return 0;

	priv->mux_channel = chan;

	return 0;
}

static int max96712_i2c_mux_init(struct max96712_priv *priv)
{
	struct max96712_source *source;
	int ret;

	if (!i2c_check_functionality(priv->client->adapter,
				     I2C_FUNC_SMBUS_WRITE_BYTE_DATA))
		return -ENODEV;

	priv->mux = i2c_mux_alloc(priv->client->adapter, &priv->client->dev,
				  priv->nsources, 0, I2C_MUX_LOCKED,
				  max96712_i2c_mux_select, NULL);
	if (!priv->mux)
		return -ENOMEM;

	priv->mux->priv = priv;

	for_each_source(priv, source) {
		unsigned int index = to_index(priv, source);

		ret = i2c_mux_add_adapter(priv->mux, 0, index, 0);
		if (ret < 0)
			goto error;
	}

	return 0;
error:
	i2c_mux_del_adapters(priv->mux);
	return ret;
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdev
 */

static int max96712_notify_bound(struct v4l2_async_notifier *notifier,
				struct v4l2_subdev *subdev,
				struct v4l2_async_subdev *asd)
{
	struct max96712_priv *priv = sd_to_max96712(notifier->sd);
	struct max96712_source *source = asd_to_max96712_source(asd);
	unsigned int index = to_index(priv, source);
	unsigned int src_pad;
	int ret;

	ret = media_entity_get_fwnode_pad(&subdev->entity,
					  source->fwnode,
					  MEDIA_PAD_FL_SOURCE);
	if (ret < 0) {
		dev_err(&priv->client->dev,
			"Failed to find pad for %s\n", subdev->name);
		return ret;
	}

	source->sd = subdev;
	src_pad = ret;
	priv->bound_sources |= BIT(index);

	ret = media_create_pad_link(&source->sd->entity, src_pad,
				    &priv->sd.entity, index,
				    MEDIA_LNK_FL_ENABLED |
				    MEDIA_LNK_FL_IMMUTABLE);
	if (ret) {
		dev_err(&priv->client->dev,
			"Unable to link %s:%u -> %s:%u\n",
			source->sd->name, src_pad, priv->sd.name, index);
		return ret;
	}

	dev_dbg(&priv->client->dev, "Bound %s pad: %u on index %u\n",
		subdev->name, src_pad, index);

	if (priv->bound_sources != priv->source_mask)
		return 0;

	return 0;
}

static void max96712_notify_unbind(struct v4l2_async_notifier *notifier,
				  struct v4l2_subdev *subdev,
				  struct v4l2_async_subdev *asd)
{
	struct max96712_source *source = asd_to_max96712_source(asd);

	source->sd = NULL;
}

static const struct v4l2_async_notifier_operations max96712_notify_ops = {
	.bound = max96712_notify_bound,
	.unbind = max96712_notify_unbind,
};

static void max96712_reset_oneshot(struct max96712_priv *priv, int mask)
{
	int timeout;
	u8 val;

	mask &= 0x0f;
	max96712_update_bits(priv, MAX96712_CTRL1, mask, mask);

	/* wait for one-shot bit self-cleared */
	for (timeout = 0; timeout < 100; timeout++) {
		max96712_read(priv, MAX96712_CTRL1, &val);
		if (!(val & mask))
			break;

		mdelay(1);
	}

	if (val & mask)
		dev_err(&priv->client->dev,
			"Failed reset oneshot 0x%x\n", mask);
}

static void max96712_disable(struct max96712_priv *priv)
{
	max96712_update_bits(priv, MAX_BACKTOP12(0), 0x02, 0);
	max96712_update_bits(priv, MAX96712_VIDEO_PIPE_EN,
			     priv->links_mask, 0);
}

static void max96712_enable(struct max96712_priv *priv)
{
	max96712_update_bits(priv, MAX96712_REG6, 0x0f, priv->links_mask);
	max96712_update_bits(priv, MAX96712_VIDEO_PIPE_EN,
			     priv->links_mask, priv->links_mask);
	max96712_update_bits(priv, MAX_BACKTOP12(0), 0x02, 0x02);
	max96712_reset_oneshot(priv, priv->links_mask);
	msleep(100);
}

static int max96712_preinit(struct max96712_priv *priv)
{
	int i;

	max96712_update_bits(priv, MAX96712_PWR1, BIT(6), BIT(6));
	usleep_range(10000, 20000);

	max96712_write_reg(priv, 0x0323, 0x84);
	max96712_write_reg(priv, 0x0325, 0x0B);
	max96712_write_reg(priv, 0x0326, 0x63);
	max96712_write_reg(priv, 0x0327, 0x2C);

	max96712_write_reg(priv, MAX96712_REG5, 0x40);
	usleep_range(10000, 20000);

	/* enable internal regulator for 1.2V VDD supply */
	max96712_update_bits(priv, MAX96712_CTRL0, BIT(2), BIT(2));
	max96712_update_bits(priv, MAX96712_CTRL2, BIT(4), BIT(4));

	/* I2C-I2C timings */
	for (i = 0; i < 8; i++) {
		max96712_write_reg(priv, MAX96712_I2C_0(i), 0x01);
		max96712_write_reg(priv, MAX96712_I2C_1(i), 0x51);
	}

	max96712_update_bits(priv, MAX96712_CTRL11, 0x55, 0x55);
	max96712_update_bits(priv, MAX96712_REG6, 0x0f, 0);

	return 0;
}

static void max96712_gmsl2_initial_setup(struct max96712_priv *priv)
{
	max96712_update_bits(priv, MAX96712_REG6, 0xF0, 0xF0);
	max96712_write_reg(priv, MAX96712_REG26, 0x22);
	max96712_write_reg(priv, MAX96712_REG27, 0x22);
}

static int max96712_mipi_setup(struct max96712_priv *priv)
{
	u32 csi_rate = 1300;

	max96712_write_reg(priv, MAX96712_VIDEO_PIPE_EN, 0);

	max96712_write_reg(priv, MAX_MIPI_PHY0, 0x04);
	max96712_write_reg(priv, MAX_MIPI_PHY3, 0xe4);
	max96712_write_reg(priv, MAX_MIPI_PHY4, 0xe4);

	max96712_write_reg(priv, MAX_MIPI_TX10(1), 0xa0);
	max96712_write_reg(priv, MAX_MIPI_TX10(2), 0xa0);

	max96712_write_reg(priv, 0x08AD, 0x3F);
	max96712_write_reg(priv, 0x08AE, 0x7D);

	max96712_update_bits(priv, MAX_BACKTOP22(0), 0x3f,
			     ((csi_rate / 100) & 0x1f) | BIT(5));
	max96712_update_bits(priv, MAX_BACKTOP25(0), 0x3f,
			     ((csi_rate / 100) & 0x1f) | BIT(5));
	max96712_update_bits(priv, MAX_BACKTOP28(0), 0x3f,
			     ((csi_rate / 100) & 0x1f) | BIT(5));
	max96712_update_bits(priv, MAX_BACKTOP31(0), 0x3f,
			     ((csi_rate / 100) & 0x1f) | BIT(5));

	max96712_update_bits(priv, MAX_MIPI_PHY2, 0xf0, 0xf0);
	if (priv->phy_pol_inv)
		max96712_write_reg(priv, MAX_MIPI_PHY5, 0x10);

	usleep_range(10000, 20000);

	return 0;
}

static int max96712_gmsl2_get_link_lock(struct max96712_priv *priv, int link_n)
{
	int lock_reg[] = {MAX96712_CTRL3, MAX96712_CTRL12,
			  MAX96712_CTRL13, MAX96712_CTRL14};
	u8 val;

	max96712_read(priv, lock_reg[link_n], &val);

	return !!(val & BIT(3));
}

static void max96712_pipe_override(struct max96712_priv *priv,
				   unsigned int pipe,
				   unsigned int dt, unsigned int vc)
{
	int bpp, bank;

	bpp = mipi_dt_to_bpp(dt);
	bank = pipe / 4;
	pipe %= 4;

	switch (pipe) {
	case 0:
		/* Pipe X: 0 or 4 */
		max96712_update_bits(priv, MAX_BACKTOP12(bank), 0x1f << 3,
				     bpp << 3);
		max96712_update_bits(priv, MAX_BACKTOP13(bank), 0x0f, vc);
		max96712_update_bits(priv, MAX_BACKTOP15(bank), 0x3f, dt);
		max96712_update_bits(priv, bank ? MAX_BACKTOP28(0) :
				     MAX_BACKTOP22(0), BIT(6), BIT(6));
		max96712_write_reg(priv, MAX_BACKTOP22(0), 0xed);
		break;
	case 1:
		/* Pipe Y: 1 or 5 */
		max96712_update_bits(priv, MAX_BACKTOP18(bank), 0x1f, bpp);
		max96712_update_bits(priv, MAX_BACKTOP13(bank), 0x0f << 4,
				     vc << 4);
		max96712_update_bits(priv, MAX_BACKTOP16(bank), 0x0f,
				     dt & 0x0f);
		max96712_update_bits(priv, MAX_BACKTOP15(bank), 0x03 << 6,
				     (dt & 0x30) << 2);
		max96712_update_bits(priv, bank ? MAX_BACKTOP28(0) :
				     MAX_BACKTOP22(0), BIT(7), BIT(7));
		max96712_write_reg(priv, MAX_BACKTOP22(0), 0xed);
		break;
	case 2:
		/* Pipe Z: 2 or 6 */
		max96712_update_bits(priv, MAX_BACKTOP19(bank), 0x03,
				     bpp & 0x03);
		max96712_update_bits(priv, MAX_BACKTOP18(bank), 0xe0,
				     (bpp & 0x1c) << 3);
		max96712_update_bits(priv, MAX_BACKTOP14(bank), 0x0f, vc);
		max96712_update_bits(priv, MAX_BACKTOP17(bank), 0x03,
				     dt & 0x03);
		max96712_update_bits(priv, MAX_BACKTOP16(bank), 0x0f << 4,
				     (dt & 0x3c) << 2);
		max96712_update_bits(priv, bank ? MAX_BACKTOP30(0) :
				     MAX_BACKTOP25(0), BIT(6), BIT(6));
		max96712_write_reg(priv, MAX_BACKTOP25(0), 0xed);
		break;
	case 3:
		/* Pipe U: 3 or 7 */
		max96712_update_bits(priv, MAX_BACKTOP19(bank), 0xfc,
				     bpp << 2);
		max96712_update_bits(priv, MAX_BACKTOP14(bank), 0x0f << 4,
				     vc << 4);
		max96712_update_bits(priv, MAX_BACKTOP17(bank), 0x3f << 2,
				     dt << 2);
		max96712_update_bits(priv, bank ? MAX_BACKTOP30(0) :
				     MAX_BACKTOP25(0), BIT(7), BIT(7));
		max96712_write_reg(priv, MAX_BACKTOP25(0), 0xed);
		break;
	}
}

static void max96712_mapping_pipe_to_mipi(struct max96712_priv *priv,
					  unsigned int pipe,
					  unsigned int map_n,
					  unsigned int in_dt,
					  unsigned int in_vc,
					  unsigned int out_dt,
					  unsigned int out_vc,
					  unsigned int out_mipi)
{
	int offset = 2 * (map_n % 4);

	max96712_write_reg(priv, MAX_MIPI_MAP_SRC(pipe, map_n),
			  (in_vc << 6) | in_dt);
	max96712_write_reg(priv, MAX_MIPI_MAP_DST(pipe, map_n),
			  (out_vc << 6) | out_dt);
	max96712_update_bits(priv, MAX_MIPI_MAP_DST_PHY(pipe, map_n / 4),
			     0x03 << offset, out_mipi << offset);
	/* enable SRC_n to DST_n mapping */
	max96712_update_bits(priv, MAX_MIPI_TX11(pipe), BIT(map_n), BIT(map_n));
	max96712_update_bits(priv, MAX_MIPI_TX11(pipe), BIT(map_n), BIT(map_n));

	usleep_range(10000, 20000);
}

static void max96712_gmsl2_pipe_set_source(struct max96712_priv *priv,
					   int pipe, int phy, int in_pipe)
{
	int offset = (pipe % 2) * 4;

	max96712_update_bits(priv, MAX96712_VIDEO_PIPE_SEL(pipe / 2),
			     0x0f << offset, (phy << (offset + 2)) |
			     (in_pipe << offset));
}

static struct {
	int in_dt;
	int out_dt;
} gmsl2_pipe_maps[] = {
	{0x00,		0x00},		/* FS */
	{0x01,		0x01},		/* FE */
	{MIPI_DT_RAW10,	MIPI_DT_RAW10},	/* payload data */
	{0x01,		0x01},		/* FE */
	{0x01,		0x01},		/* FE */
};

static void max96712_gmsl2_link_pipe_setup(struct max96712_priv *priv,
					   int link_n)
{
	struct max96712_link *link = priv->link[link_n];
	int pipe = link_n; /* straight mapping */
	int dt = priv->dt; /* must come from imager */
	int in_vc = 0;
	int i;

	max96712_gmsl2_pipe_set_source(priv, pipe, link_n, 0);

	max96712_write_reg(priv, MAX96712_RX0(pipe), 0);
	max96712_write_reg(priv, MAX_VIDEO_RX0(pipe), 0x00);
	max96712_pipe_override(priv, pipe, dt, in_vc);
	usleep_range(10000, 20000);

	max96712_write_reg(priv, MAX_MIPI_TX11(pipe), 0x00);
	max96712_write_reg(priv, MAX_MIPI_TX12(pipe), 0x00);

	for (i = 0; i < ARRAY_SIZE(gmsl2_pipe_maps); i++) {
		max96712_mapping_pipe_to_mipi(priv, pipe, i,
					      gmsl2_pipe_maps[i].in_dt,
					      in_vc,
					      gmsl2_pipe_maps[i].out_dt,
					      link->out_vc,
					      link->out_mipi);
	}

	link->pipes_mask |= BIT(pipe);
}

static int max96712_gmsl2_reverse_channel_setup(struct max96712_priv *priv,
						int link_n)
{
	struct max96712_link *link = priv->link[link_n];
	int ser_addrs[] = {0x40, 0x42, 0x60, 0x62};
	int timeout = 50;
	int ret = 0;
	int val = 0, i, j = 0;

//	max96712_update_bits(priv, MAX96712_REG6, 0xff, 0xf0 | BIT(link_n));
	max96712_write_reg(priv, MAX96712_REG6, 0xFF);
	max96712_reset_oneshot(priv, BIT(link_n));

	/*
	 * wait the link to be established,
	 * indicated when status bit LOCKED goes high
	 */
	for (; timeout > 0; timeout--) {
		if (max96712_gmsl2_get_link_lock(priv, link_n))
			break;
		mdelay(1);
	}

	if (!timeout) {
		ret = -ETIMEDOUT;
		goto out;
	}

	for (i = 0; i < ARRAY_SIZE(ser_addrs); i++) {
		/* read serializer ID */
		__reg16_read(ser_addrs[i], 0x000d, &val);
		if (val == MAX9295A_ID || val == MAX9295B_ID) {
			dev_dbg(&priv->client->dev, "ID val:0x%x>\n", val);
			link->ser_id = val;
			 /* relocate serizlizer on I2C bus */
			__reg16_write(ser_addrs[i], 0x0000,
				      link->ser_addr << 1);
			usleep_range(2000, 2500);
			j = i;
		}
	}

	priv->links_mask |= BIT(link_n);

out:
	dev_info(&priv->client->dev, "link%d %s %sat 0x%x (0x%x) %s\n",
		 link_n, chip_name(link->ser_id),
		 ret == -EADDRINUSE ? "already " : "",
		 link->ser_addr, ser_addrs[j], ret == -ETIMEDOUT ?
		 "not found: timeout GMSL2 link establish" : "");
	return ret;
}

static void max96712_setup(struct max96712_priv *priv)
{
	struct max96712_source *source;
	int ret;
	int link = 0;

#if DEBUG_COLOR_PATTERN
	max96712_color_pattern(priv);
	return;
#endif
	max96712_preinit(priv);
	max96712_gmsl2_initial_setup(priv);
	max96712_mipi_setup(priv);

	/* Start all cameras. */
	for_each_source(priv, source) {
		max96712_gmsl2_link_pipe_setup(priv, link);
		ret = max96712_gmsl2_reverse_channel_setup(priv, link);
		if (ret < 0)
			source->linkup = false;
		else
			source->linkup = true;
		link++;
	}

	return;
}

static int max96712_s_stream(struct v4l2_subdev *sd, int enable)
{
	int ret;
	int link = 0;

	struct max96712_priv *priv = sd_to_max96712(sd);
	struct max96712_source *source;

	if (enable && priv->stream_count == 0) {
		max96712_setup(priv);
		max96712_enable(priv);

		for_each_source(priv, source) {
			if (!source->linkup) {
				link++;
				continue;
			}

			if (source->linkup)
				max9295a_sensor_set_regs(priv, link);
			link++;

			ret = v4l2_subdev_call(source->sd, video,
					       s_stream, 1);
			if (ret)
				return ret;
		}
	} else if (!enable && priv->stream_count == 1) {
		max96712_disable(priv);

		/* Stop all cameras. */
		for_each_source(priv, source) {
			if (!source->linkup)
				continue;

			v4l2_subdev_call(source->sd, video, s_stream, 0);
		}
		gpiod_direction_output_raw(priv->gpiod_pwdn, 0);
		gpiod_direction_output_raw(priv->gpiod_pwdn, 1);
	}

	priv->stream_count += enable ? 1 : -1;

	return 0;
}

static int max96712_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad || code->index > 0)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_Y10_1X10;

	return 0;
}

static struct v4l2_mbus_framefmt *
max96712_get_pad_format(struct max96712_priv *priv,
		       struct v4l2_subdev_pad_config *cfg,
		       unsigned int pad, u32 which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_format(&priv->sd, cfg, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &priv->fmt[pad];
	default:
		return NULL;
	}
}

static int max96712_set_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *format)
{
	struct max96712_priv *priv = sd_to_max96712(sd);
	struct v4l2_mbus_framefmt *cfg_fmt;

	if (format->pad >= MAX96712_SRC_PAD)
		return -EINVAL;

	/* Refuse non YUV422 formats as we hardcode DT to 8 bit YUV422 */
	switch (format->format.code) {
	case MEDIA_BUS_FMT_UYVY8_2X8:
	case MEDIA_BUS_FMT_VYUY8_2X8:
	case MEDIA_BUS_FMT_YUYV8_2X8:
	case MEDIA_BUS_FMT_YVYU8_2X8:
		break;
	default:
		format->format.code = MEDIA_BUS_FMT_Y10_1X10;
		break;
	}

	cfg_fmt = max96712_get_pad_format(priv, cfg,
					  format->pad, format->which);
	if (!cfg_fmt)
		return -EINVAL;

	*cfg_fmt = format->format;

	return 0;
}

static int max96712_get_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *format)
{
	struct max96712_priv *priv = sd_to_max96712(sd);
	struct v4l2_mbus_framefmt *cfg_fmt;

	if (format->pad >= MAX96712_SRC_PAD)
		return -EINVAL;

	cfg_fmt = max96712_get_pad_format(priv, cfg, format->pad,
					  format->which);
	if (!cfg_fmt)
		return -EINVAL;

	format->format = *cfg_fmt;

	return 0;
}

static const struct v4l2_subdev_video_ops max96712_video_ops = {
	.s_stream	= max96712_s_stream,
};

static const struct v4l2_subdev_pad_ops max96712_pad_ops = {
	.enum_mbus_code = max96712_enum_mbus_code,
	.get_fmt	= max96712_get_fmt,
	.set_fmt	= max96712_set_fmt,
};

static const struct v4l2_subdev_ops max96712_subdev_ops = {
	.video		= &max96712_video_ops,
	.pad		= &max96712_pad_ops,
};

static void max96712_init_format(struct v4l2_mbus_framefmt *fmt)
{
	fmt->width		= 1920;
	fmt->height		= 1020;
	fmt->code		= MEDIA_BUS_FMT_Y10_1X10;
	fmt->colorspace		= V4L2_COLORSPACE_SRGB;
	fmt->field		= V4L2_FIELD_NONE;
	fmt->ycbcr_enc		= V4L2_YCBCR_ENC_DEFAULT;
	fmt->quantization	= V4L2_QUANTIZATION_DEFAULT;
	fmt->xfer_func		= V4L2_XFER_FUNC_DEFAULT;
}

static int max96712_open(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	struct v4l2_mbus_framefmt *format;
	unsigned int i;

	for (i = 0; i < MAX96712_N_SINKS; i++) {
		format = v4l2_subdev_get_try_format(subdev, fh->pad, i);
		max96712_init_format(format);
	}

	return 0;
}

static const struct v4l2_subdev_internal_ops max96712_subdev_internal_ops = {
	.open = max96712_open,
};

/* -----------------------------------------------------------------------------
 * Probe/Remove
 */

static const struct of_device_id max96712_dt_ids[] = {
	{ .compatible = "maxim,max96712" },
	{},
};
MODULE_DEVICE_TABLE(of, max96712_dt_ids);

static int max96712_init(struct device *dev)
{
	struct max96712_priv *priv;
	struct i2c_client *client;
	struct fwnode_handle *ep;
	unsigned int i, mbps;
	int ret, bpp = 10;

	/* Skip non-max96712 devices. */
	if (!dev->of_node || !of_match_node(max96712_dt_ids, dev->of_node))
		return 0;

	client = to_i2c_client(dev);
	priv = i2c_get_clientdata(client);

	v4l2_i2c_subdev_init(&priv->sd, client, &max96712_subdev_ops);
	priv->sd.internal_ops = &max96712_subdev_internal_ops;
	priv->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	v4l2_ctrl_handler_init(&priv->ctrls, 1);

	mbps = 1300000000;	/* 1920x1020@30Hz, RAW10 bpp = 10 */
#if DEBUG_COLOR_PATTERN
	mbps = DEBUG_MBPS;
#endif
	v4l2_ctrl_new_std(&priv->ctrls, NULL, V4L2_CID_PIXEL_RATE,
			  1, INT_MAX, 1, mbps);
	priv->sd.ctrl_handler = &priv->ctrls;
	ret = priv->ctrls.error;
	if (ret)
		return ret;

	priv->sd.entity.function = MEDIA_ENT_F_VID_IF_BRIDGE;

	priv->pads[MAX96712_SRC_PAD].flags = MEDIA_PAD_FL_SOURCE;
	for (i = 0; i < MAX96712_SRC_PAD; i++)
		priv->pads[i].flags = MEDIA_PAD_FL_SINK;
	ret = media_entity_pads_init(&priv->sd.entity, MAX96712_N_PADS,
				     priv->pads);
	if (ret)
		return ret;

	ep = fwnode_graph_get_endpoint_by_id(dev_fwnode(dev), MAX96712_SRC_PAD,
					     0, 0);
	if (!ep) {
		dev_err(dev, "Unable to retrieve endpoint on \"port@4\"\n");
		ret = -ENOENT;
		return ret;
	}
	priv->sd.fwnode = ep;

	ret = v4l2_async_register_subdev(&priv->sd);
	if (ret < 0) {
		dev_err(dev, "Unable to register subdevice\n");
		goto err_put_node;
	}

	ret = max96712_i2c_mux_init(priv);
	if (ret) {
		dev_err(dev, "Unable to initialize I2C multiplexer\n");
		goto err_subdev_unregister;
	}

	return 0;

err_subdev_unregister:
	v4l2_async_unregister_subdev(&priv->sd);
err_put_node:
	fwnode_handle_put(ep);

	return ret;
}

static void max96712_cleanup_dt(struct max96712_priv *priv)
{
	struct max96712_source *source;

	/*
	 * Not strictly part of the DT, but the notifier is registered during
	 * max96712_parse_dt(), and the notifier holds references to the fwnodes
	 * thus the cleanup is here to mirror the registration.
	 */
	v4l2_async_notifier_unregister(&priv->notifier);

	for_each_source(priv, source) {
		fwnode_handle_put(source->fwnode);
		source->fwnode = NULL;
	}
}

static int max96712_parse_dt(struct max96712_priv *priv)
{
	struct device *dev = &priv->client->dev;
	struct device_node *i2c_mux;
	struct device_node *node = NULL;
	struct device_node *np = priv->client->dev.of_node;
	unsigned int i2c_mux_mask = 0;
	int ret, pwdnb;

	if (of_property_read_bool(np, "maxim,invert_phy-pol"))
		priv->phy_pol_inv = true;
	else
		priv->phy_pol_inv = false;

	pwdnb = of_get_gpio(np, 0);
	if (!gpio_is_valid(pwdnb))
		return -EINVAL;

	priv->gpiod_pwdn = gpio_to_desc(pwdnb);

	of_node_get(dev->of_node);
	i2c_mux = of_find_node_by_name(dev->of_node, "i2c-mux");
	if (!i2c_mux) {
		dev_err(dev, "Failed to find i2c-mux node\n");
		return -EINVAL;
	}

	/* Identify which i2c-mux channels are enabled */
	for_each_child_of_node(i2c_mux, node) {
		u32 id = 0;

		of_property_read_u32(node, "reg", &id);
		if (id >= MAX96712_NUM_GMSL)
			continue;

		if (!of_device_is_available(node)) {
			dev_dbg(dev, "Skipping disabled I2C bus port %u\n",
				id);
			continue;
		}

		i2c_mux_mask |= BIT(id);
	}
	of_node_put(node);
	of_node_put(i2c_mux);

	v4l2_async_notifier_init(&priv->notifier);

	/* Parse the endpoints */
	for_each_endpoint_of_node(dev->of_node, node) {
		struct max96712_source *source;
		struct of_endpoint ep;

		of_graph_parse_endpoint(node, &ep);
		dev_dbg(dev, "Endpoint %pOF on port %d",
			ep.local_node, ep.port);

		if (ep.port > MAX96712_NUM_GMSL) {
			dev_err(dev, "Invalid endpoint %s on port %d",
				of_node_full_name(ep.local_node), ep.port);
			continue;
		}

		/* For the source endpoint just parse the bus configuration. */
		if (ep.port == MAX96712_SRC_PAD) {
			struct v4l2_fwnode_endpoint vep = {
				.bus_type = V4L2_MBUS_CSI2_DPHY
			};
			int ret;

			ret = v4l2_fwnode_endpoint_parse(
					of_fwnode_handle(node), &vep);
			if (ret) {
				of_node_put(node);
				return ret;
			}

			if (vep.bus_type != V4L2_MBUS_CSI2_DPHY) {
				dev_err(dev,
					"Media bus %u type not supported\n",
					vep.bus_type);
				v4l2_fwnode_endpoint_free(&vep);
				of_node_put(node);
				return -EINVAL;
			}

			priv->csi2_data_lanes =
				vep.bus.mipi_csi2.num_data_lanes;
			v4l2_fwnode_endpoint_free(&vep);

			continue;
		}

		/* Skip if the corresponding GMSL link is unavailable. */
		if (!(i2c_mux_mask & BIT(ep.port)))
			continue;

		if (priv->sources[ep.port].fwnode) {
			dev_err(dev,
				"Multiple port endpoints are not supported: %d",
				ep.port);

			continue;
		}

		source = &priv->sources[ep.port];
		source->fwnode = fwnode_graph_get_remote_endpoint(
						of_fwnode_handle(node));
		if (!source->fwnode) {
			dev_err(dev,
				"Endpoint %pOF has no remote endpoint\n",
				ep.local_node);

			continue;
		}

		source->asd.match_type = V4L2_ASYNC_MATCH_FWNODE;
		source->asd.match.fwnode = source->fwnode;

		ret = v4l2_async_notifier_add_subdev(&priv->notifier,
						     &source->asd);
		if (ret) {
			v4l2_async_notifier_cleanup(&priv->notifier);
			of_node_put(node);
			return ret;
		}

		priv->source_mask |= BIT(ep.port);
		priv->nsources++;
	}
	of_node_put(node);

	/* Do not register the subdev notifier if there are no devices. */
	if (!priv->nsources)
		return 0;

	priv->route_mask = priv->source_mask;
	priv->notifier.ops = &max96712_notify_ops;

	ret = v4l2_async_subdev_notifier_register(&priv->sd, &priv->notifier);
	if (ret)
		v4l2_async_notifier_cleanup(&priv->notifier);

	return ret;
}

static int max96712_probe(struct i2c_client *client)
{
	struct max96712_priv *priv;
	unsigned int i;
	int ret;
	int addrs[5];
	struct device_node *np = client->dev.of_node;

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	for (i = 0; i < MAX96712_NUM_GMSL; i++) {
		priv->link[i] = devm_kzalloc(&client->dev,
					     sizeof(*priv->link[i]),
					     GFP_KERNEL);
		if (!priv->link[i])
			return -ENOMEM;
	}

	priv->client = client;
	priv->dt = MIPI_DT_RAW10;
	priv->stream_count = 0;

	of_property_read_u32_array(np, "reg", addrs, ARRAY_SIZE(addrs));

	for (i = 0; i < MAX96712_NUM_GMSL; i++) {
		priv->link[i]->ser_addr = addrs[i+1];
		priv->link[i]->out_mipi = 1;
		priv->link[i]->out_vc = i;
		priv->link[i]->client = i2c_new_dummy_device(client->adapter,
							     addrs[i+1]);
	}
	i2c_set_clientdata(client, priv);

	for (i = 0; i < MAX96712_N_SINKS; i++)
		max96712_init_format(&priv->fmt[i]);

	ret = max96712_parse_dt(priv);
	if (ret < 0)
		goto err_link;

	gpiod_direction_output_raw(priv->gpiod_pwdn, 1);

	dev_dbg(&client->dev,
		"All max96712 probed: start initialization sequence\n");

	ret = max96712_init(&client->dev);
	if (ret < 0)
		goto err_free;

	return 0;

err_free:
	max96712_cleanup_dt(priv);
err_link:
	for (i = 0; i < MAX96712_NUM_GMSL; i++)
		i2c_unregister_device(priv->link[i]->client);

	return ret;
}

static int max96712_remove(struct i2c_client *client)
{
	int i;
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct max96712_priv *priv = sd_to_max96712(sd);

	i2c_mux_del_adapters(priv->mux);

	for (i = 0; i < MAX96712_NUM_GMSL; i++)
		i2c_unregister_device(priv->link[i]->client);

	fwnode_handle_put(priv->sd.fwnode);
	v4l2_async_unregister_subdev(&priv->sd);

	max96712_cleanup_dt(priv);

	return 0;
}

static const struct i2c_device_id max96712_id[] = {
	{ "max96712", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max96712_id);

static struct i2c_driver max96712_i2c_driver = {
	.driver	= {
		.name		= "max96712",
		.of_match_table	= of_match_ptr(max96712_dt_ids),
	},
	.probe_new	= max96712_probe,
	.remove		= max96712_remove,
	.id_table	= max96712_id,
};

module_i2c_driver(max96712_i2c_driver);

MODULE_ALIAS("MAX96712");
MODULE_DESCRIPTION("Maxim MAX96712 GMSL2 Deserializer Driver");
MODULE_LICENSE("GPL");
