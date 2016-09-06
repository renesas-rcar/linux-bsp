/*
 * Driver for Analog Devices ADV7482 HDMI receiver
 *
 * Copyright (C) 2016 Renesas Electronics Corp.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/v4l2-dv-timings.h>

#include <media/v4l2-device.h>
#include <media/v4l2-dv-timings.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-of.h>


/* I2C slave addresses */
/*
 * TODO: Use 'i2c_new_secondary_device' from patch
 * 'PATCH v2] i2c: Add generic support passing secondary devices addresses'
 * to be able to set all addresses in DT.
 */
#define ADV7482_I2C_IO			0x70	/* IO Map */
#define ADV7482_I2C_DPLL		0x26	/* DPLL Map */
#define ADV7482_I2C_CP			0x22	/* CP Map */
#define ADV7482_I2C_HDMI		0x34	/* HDMI Map */
#define ADV7482_I2C_EDID		0x36	/* EDID Map */
#define ADV7482_I2C_REPEATER		0x32	/* HDMI RX Repeater Map */
#define ADV7482_I2C_INFOFRAME		0x31	/* HDMI RX InfoFrame Map */
#define ADV7482_I2C_CEC			0x41	/* CEC Map */
#define ADV7482_I2C_SDP			0x79	/* SDP Map */
#define ADV7482_I2C_TXB			0x48	/* CSI-TXB Map */
#define ADV7482_I2C_TXA			0x4A	/* CSI-TXA Map */
#define ADV7482_I2C_WAIT		0xFE	/* Wait x mesec */
#define ADV7482_I2C_EOR			0xFF	/* End Mark */

enum adv7482_pads {
	ADV7482_SINK_HDMI,
	ADV7482_SINK_AIN1,
	ADV7482_SINK_AIN2,
	ADV7482_SINK_AIN3,
	ADV7482_SINK_AIN4,
	ADV7482_SINK_AIN5,
	ADV7482_SINK_AIN6,
	ADV7482_SINK_AIN7,
	ADV7482_SINK_AIN8,
	ADV7482_SINK_TTL,
	ADV7482_SOURCE_TXA,
	ADV7482_SOURCE_TXB,
	ADV7482_PAD_MAX,
};

/**
 * struct adv7482_hdmi_cp - State of HDMI CP sink
 * @timings:		Timings for {g,s}_dv_timings
 */
struct adv7482_hdmi_cp {
	struct v4l2_dv_timings timings;
};

/**
 * struct adv7482_sdp - State of SDP sink
 * @streaming:		Flag if SDP is currently streaming
 * @curr_norm:		Current video standard
 */
struct adv7482_sdp {
	bool streaming;
	v4l2_std_id curr_norm;
};

/**
 * struct adv7482_state - State of ADV7482
 * @dev:		(OF) device
 * @client:		I2C client
 * @sd:			v4l2 subevice
 * @mutex:		protect aginst sink state changes while streaming
 *
 * @pads:		meda pads exposed
 *
 * @cp:			state of CP HDMI
 * @sdp:		state of SDP
 */
struct adv7482_state {
	struct device *dev;
	struct i2c_client *client;
	struct v4l2_subdev sd;
	struct mutex mutex;

	struct media_pad pads[ADV7482_PAD_MAX];

	struct adv7482_hdmi_cp cp;
	struct adv7482_sdp sdp;
};

#define to_state(a) container_of(a, struct adv7482_state, sd)

#define adv_err(a, fmt, arg...)	dev_err(a->dev, fmt, ##arg)
#define adv_info(a, fmt, arg...) dev_info(a->dev, fmt, ##arg)
#define adv_dbg(a, fmt, arg...)	dev_dbg(a->dev, fmt, ##arg)

/* -----------------------------------------------------------------------------
 * Register manipulaton
 */

/**
 * struct adv7482_reg_value - Register write instruction
 * @addr:		I2C slave address
 * @reg:		I2c register
 * @value:		value to write to @addr at @reg
 */
struct adv7482_reg_value {
	u8 addr;
	u8 reg;
	u8 value;
};

static int adv7482_write_regs(struct adv7482_state *state,
			      const struct adv7482_reg_value *regs)
{
	struct i2c_msg msg;
	u8 data_buf[2];
	int ret = -EINVAL;

	if (!state->client->adapter) {
		adv_err(state, "No adapter for regs write\n");
		return -ENODEV;
	}

	msg.flags = 0;
	msg.len = 2;
	msg.buf = &data_buf[0];

	while (regs->addr != ADV7482_I2C_EOR) {

		if (regs->addr == ADV7482_I2C_WAIT)
			msleep(regs->value);
		else {
			msg.addr = regs->addr;
			data_buf[0] = regs->reg;
			data_buf[1] = regs->value;

			ret = i2c_transfer(state->client->adapter, &msg, 1);
			if (ret < 0) {
				adv_err(state,
					"Error regs addr: 0x%02x reg: 0x%02x\n",
					regs->addr, regs->reg);
				break;
			}
		}
		regs++;
	}

	return (ret < 0) ? ret : 0;
}

static int adv7482_write(struct adv7482_state *state, u8 addr, u8 reg, u8 value)
{
	struct adv7482_reg_value regs[2];
	int ret;

	regs[0].addr = addr;
	regs[0].reg = reg;
	regs[0].value = value;
	regs[1].addr = ADV7482_I2C_EOR;
	regs[1].reg = 0xFF;
	regs[1].value = 0xFF;

	ret = adv7482_write_regs(state, regs);

	return ret;
}

static int adv7482_read(struct adv7482_state *state, u8 addr, u8 reg)
{
	struct i2c_msg msg[2];
	u8 reg_buf, data_buf;
	int ret;

	if (!state->client->adapter) {
		adv_err(state, "No adapter reading addr: 0x%02x reg: 0x%02x\n",
			addr, reg);
		return -ENODEV;
	}

	msg[0].addr = addr;
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf = &reg_buf;
	msg[1].addr = addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = 1;
	msg[1].buf = &data_buf;

	reg_buf = reg;

	ret = i2c_transfer(state->client->adapter, msg, 2);
	if (ret < 0) {
		adv_err(state, "Error reading addr: 0x%02x reg: 0x%02x\n",
			addr, reg);
		return ret;
	}

	return data_buf;
}

#define io_read(s, r) adv7482_read(s, ADV7482_I2C_IO, r)
#define io_write(s, r, v) adv7482_write(s, ADV7482_I2C_IO, r, v)
#define io_clrset(s, r, m, v) io_write(s, r, (io_read(s, r) & ~m) | v)

#define hdmi_read(s, r) adv7482_read(s, ADV7482_I2C_HDMI, r)
#define hdmi_read16(s, r, m) (((hdmi_read(s, r) << 8) | hdmi_read(s, r+1)) & m)
#define hdmi_write(s, r, v) adv7482_write(s, ADV7482_I2C_HDMI, r, v)
#define hdmi_clrset(s, r, m, v) hdmi_write(s, r, (hdmi_read(s, r) & ~m) | v)

#define sdp_read(s, r) adv7482_read(s, ADV7482_I2C_SDP, r)
#define sdp_write(s, r, v) adv7482_write(s, ADV7482_I2C_SDP, r, v)
#define sdp_clrset(s, r, m, v) sdp_write(s, r, (sdp_read(s, r) & ~m) | v)

#define cp_read(s, r) adv7482_read(s, ADV7482_I2C_CP, r)
#define cp_write(s, r, v) adv7482_write(s, ADV7482_I2C_CP, r, v)
#define cp_clrset(s, r, m, v) cp_write(s, r, (cp_read(s, r) & ~m) | v)

#define txa_read(s, r) adv7482_read(s, ADV7482_I2C_TXA, r)
#define txa_write(s, r, v) adv7482_write(s, ADV7482_I2C_TXA, r, v)
#define txa_clrset(s, r, m, v) txa_write(s, r, (txa_read(s, r) & ~m) | v)

#define txb_read(s, r) adv7482_read(s, ADV7482_I2C_TXB, r)
#define txb_write(s, r, v) adv7482_write(s, ADV7482_I2C_TXB, r, v)
#define txb_clrset(s, r, m, v) txb_write(s, r, (txb_read(s, r) & ~m) | v)

/* -----------------------------------------------------------------------------
 * HDMI and CP
 */

#define ADV7482_CP_MAX_WIDTH		1600
#define ADV7482_CP_MAX_HEIGHT		1200
#define ADV7482_CP_MIN_PIXELCLOCK	0		/* unknown */
#define ADV7482_CP_MAX_PIXELCLOCK	162000000

static const struct v4l2_dv_timings_cap adv7482_cp_timings_cap = {
	.type = V4L2_DV_BT_656_1120,
	/* keep this initialization for compatibility with GCC < 4.4.6 */
	.reserved = { 0 },
	/* Min pixelclock value is unknown */
	V4L2_INIT_BT_TIMINGS(0, ADV7482_CP_MAX_WIDTH, 0, ADV7482_CP_MAX_HEIGHT,
			     ADV7482_CP_MIN_PIXELCLOCK,
			     ADV7482_CP_MAX_PIXELCLOCK,
			     V4L2_DV_BT_STD_CEA861 | V4L2_DV_BT_STD_DMT,
			     V4L2_DV_BT_CAP_INTERLACED |
			     V4L2_DV_BT_CAP_PROGRESSIVE)
};

struct adv7482_cp_video_standards {
	struct v4l2_dv_timings timings;
	u8 vid_std;
	u8 v_freq;
};

static const struct adv7482_cp_video_standards adv7482_cp_video_standards[] = {
	{ V4L2_DV_BT_CEA_720X480I59_94, 0x40, 0x00 },
	{ V4L2_DV_BT_CEA_720X576I50, 0x41, 0x01 },
	{ V4L2_DV_BT_CEA_720X480P59_94, 0x4a, 0x00 },
	{ V4L2_DV_BT_CEA_720X576P50, 0x4b, 0x00 },
	{ V4L2_DV_BT_CEA_1280X720P60, 0x53, 0x00 },
	{ V4L2_DV_BT_CEA_1280X720P50, 0x53, 0x01 },
	{ V4L2_DV_BT_CEA_1280X720P30, 0x53, 0x02 },
	{ V4L2_DV_BT_CEA_1280X720P25, 0x53, 0x03 },
	{ V4L2_DV_BT_CEA_1280X720P24, 0x53, 0x04 },
	{ V4L2_DV_BT_CEA_1920X1080I60, 0x54, 0x00 },
	{ V4L2_DV_BT_CEA_1920X1080I50, 0x54, 0x01 },
	{ V4L2_DV_BT_CEA_1920X1080P60, 0x5e, 0x00 },
	{ V4L2_DV_BT_CEA_1920X1080P50, 0x5e, 0x01 },
	{ V4L2_DV_BT_CEA_1920X1080P30, 0x5e, 0x02 },
	{ V4L2_DV_BT_CEA_1920X1080P25, 0x5e, 0x03 },
	{ V4L2_DV_BT_CEA_1920X1080P24, 0x5e, 0x04 },
	/* SVGA */
	{ V4L2_DV_BT_DMT_800X600P56, 0x80, 0x00 },
	{ V4L2_DV_BT_DMT_800X600P60, 0x81, 0x00 },
	{ V4L2_DV_BT_DMT_800X600P72, 0x82, 0x00 },
	{ V4L2_DV_BT_DMT_800X600P75, 0x83, 0x00 },
	{ V4L2_DV_BT_DMT_800X600P85, 0x84, 0x00 },
	/* SXGA */
	{ V4L2_DV_BT_DMT_1280X1024P60, 0x85, 0x00 },
	{ V4L2_DV_BT_DMT_1280X1024P75, 0x86, 0x00 },
	/* VGA */
	{ V4L2_DV_BT_DMT_640X480P60, 0x88, 0x00 },
	{ V4L2_DV_BT_DMT_640X480P72, 0x89, 0x00 },
	{ V4L2_DV_BT_DMT_640X480P75, 0x8a, 0x00 },
	{ V4L2_DV_BT_DMT_640X480P85, 0x8b, 0x00 },
	/* XGA */
	{ V4L2_DV_BT_DMT_1024X768P60, 0x8c, 0x00 },
	{ V4L2_DV_BT_DMT_1024X768P70, 0x8d, 0x00 },
	{ V4L2_DV_BT_DMT_1024X768P75, 0x8e, 0x00 },
	{ V4L2_DV_BT_DMT_1024X768P85, 0x8f, 0x00 },
	/* UXGA */
	{ V4L2_DV_BT_DMT_1600X1200P60, 0x96, 0x00 },
	/* End of standards */
	{ },
};

static void adv7482_hdmi_fill_format(struct adv7482_state *state,
				     struct v4l2_mbus_framefmt *fmt)
{
	memset(fmt, 0, sizeof(*fmt));

	fmt->code = MEDIA_BUS_FMT_RGB888_1X24;
	fmt->colorspace = V4L2_COLORSPACE_SRGB;
	fmt->field = state->cp.timings.bt.interlaced ?
		V4L2_FIELD_INTERLACED : V4L2_FIELD_NONE;

	fmt->width = state->cp.timings.bt.width;
	fmt->height = state->cp.timings.bt.height;
}

static void adv7482_fill_optional_dv_timings(struct adv7482_state *state,
					     struct v4l2_dv_timings *timings)
{
	v4l2_find_dv_timings_cap(timings, &adv7482_cp_timings_cap,
				 250000, NULL, NULL);
}

static bool adv7482_hdmi_have_signal(struct adv7482_state *state)
{
	int val;

	/* Check that VERT_FILTER and DG_REGEN is locked */
	val = hdmi_read(state, 0x07);
	return !!(val & BIT(7) && val & BIT(5));
}

static unsigned int adv7482_hdmi_read_pixelclock(struct adv7482_state *state)
{
	int a, b;

	a = hdmi_read(state, 0x51);
	b = hdmi_read(state, 0x52);
	if (a < 0 || b < 0)
		return 0;
	return ((a << 1) | (b >> 7)) * 1000000 + (b & 0x7f) * 1000000 / 128;
}

static int adv7482_hdmi_set_video_timings(struct adv7482_state *state,
					  const struct v4l2_dv_timings *timings)
{
	const struct adv7482_cp_video_standards *stds =
		adv7482_cp_video_standards;
	int i;

	for (i = 0; stds[i].timings.bt.width; i++) {
		if (!v4l2_match_dv_timings(timings, &stds[i].timings, 250000,
					   false))
			continue;
		/*
		 * The resolution of 720p, 1080i and 1080p is Hsync width of
		 * 40 pixelclock cycles. These resolutions must be shifted
		 * horizontally to the left in active video mode.
		 */
		switch (stds[i].vid_std) {
		case 0x53: /* 720p */
			cp_write(state, 0x8B, 0x43);
			cp_write(state, 0x8C, 0xD8);
			cp_write(state, 0x8B, 0x4F);
			cp_write(state, 0x8D, 0xD8);
			break;
		case 0x54: /* 1080i */
		case 0x5e: /* 1080p */
			cp_write(state, 0x8B, 0x43);
			cp_write(state, 0x8C, 0xD4);
			cp_write(state, 0x8B, 0x4F);
			cp_write(state, 0x8D, 0xD4);
			break;
		default:
			cp_write(state, 0x8B, 0x40);
			cp_write(state, 0x8C, 0x00);
			cp_write(state, 0x8B, 0x40);
			cp_write(state, 0x8D, 0x00);
			break;
		}

		io_write(state, 0x05, stds[i].vid_std);
		io_clrset(state, 0x03, 0x70, stds[i].v_freq << 4);

		return 0;
	}

	return -EINVAL;
}

/* -----------------------------------------------------------------------------
 * SDP
 */

#define ADV7482_SDP_INPUT_CVBS_AIN1			0x00
#define ADV7482_SDP_INPUT_CVBS_AIN2			0x01
#define ADV7482_SDP_INPUT_CVBS_AIN3			0x02
#define ADV7482_SDP_INPUT_CVBS_AIN4			0x03
#define ADV7482_SDP_INPUT_CVBS_AIN5			0x04
#define ADV7482_SDP_INPUT_CVBS_AIN6			0x05
#define ADV7482_SDP_INPUT_CVBS_AIN7			0x06
#define ADV7482_SDP_INPUT_CVBS_AIN8			0x07

#define ADV7482_SDP_STD_AD_PAL_BG_NTSC_J_SECAM		0x0
#define ADV7482_SDP_STD_AD_PAL_BG_NTSC_J_SECAM_PED	0x1
#define ADV7482_SDP_STD_AD_PAL_N_NTSC_J_SECAM		0x2
#define ADV7482_SDP_STD_AD_PAL_N_NTSC_M_SECAM		0x3
#define ADV7482_SDP_STD_NTSC_J				0x4
#define ADV7482_SDP_STD_NTSC_M				0x5
#define ADV7482_SDP_STD_PAL60				0x6
#define ADV7482_SDP_STD_NTSC_443			0x7
#define ADV7482_SDP_STD_PAL_BG				0x8
#define ADV7482_SDP_STD_PAL_N				0x9
#define ADV7482_SDP_STD_PAL_M				0xa
#define ADV7482_SDP_STD_PAL_M_PED			0xb
#define ADV7482_SDP_STD_PAL_COMB_N			0xc
#define ADV7482_SDP_STD_PAL_COMB_N_PED			0xd
#define ADV7482_SDP_STD_PAL_SECAM			0xe
#define ADV7482_SDP_STD_PAL_SECAM_PED			0xf

static void adv7482_sdp_fill_format(struct adv7482_state *state,
				    struct v4l2_mbus_framefmt *fmt)
{
	memset(fmt, 0, sizeof(*fmt));

	fmt->code = MEDIA_BUS_FMT_YUYV8_2X8;
	fmt->colorspace = V4L2_COLORSPACE_SMPTE170M;
	fmt->field = V4L2_FIELD_INTERLACED;

	fmt->width = 720;
	fmt->height = state->sdp.curr_norm & V4L2_STD_525_60 ? 480 : 576;
}

static int adv7482_sdp_read_ro_map(struct adv7482_state *state, u8 reg)
{
	int ret;

	/* Select SDP Read-Only Main Map */
	ret = sdp_write(state, 0x0e, 0x01);
	if (ret < 0)
		return ret;

	return sdp_read(state, reg);
}

static int adv7482_sdp_status(struct adv7482_state *state, u32 *signal,
			      v4l2_std_id *std)
{
	int info;

	/* Read status from reg 0x10 of SDP RO Map */
	info = adv7482_sdp_read_ro_map(state, 0x10);
	if (info < 0)
		return info;

	if (signal)
		*signal = info & BIT(0) ? 0 : V4L2_IN_ST_NO_SIGNAL;

	if (std) {
		*std = V4L2_STD_UNKNOWN;

		/* Standard not valid if there is no signal */
		if (info & BIT(0)) {
			switch (info & 0x70) {
			case 0x00:
				*std = V4L2_STD_NTSC;
				break;
			case 0x10:
				*std = V4L2_STD_NTSC_443;
				break;
			case 0x20:
				*std = V4L2_STD_PAL_M;
				break;
			case 0x30:
				*std = V4L2_STD_PAL_60;
				break;
			case 0x40:
				*std = V4L2_STD_PAL;
				break;
			case 0x50:
				*std = V4L2_STD_SECAM;
				break;
			case 0x60:
				*std = V4L2_STD_PAL_Nc | V4L2_STD_PAL_N;
				break;
			case 0x70:
				*std = V4L2_STD_SECAM;
				break;
			default:
				*std = V4L2_STD_UNKNOWN;
				break;
			}
		}
	}

	return 0;
}

static int adv7482_sdp_std(v4l2_std_id std)
{
	if (std == V4L2_STD_ALL)
		return ADV7482_SDP_STD_AD_PAL_BG_NTSC_J_SECAM;
	if (std == V4L2_STD_PAL_60)
		return ADV7482_SDP_STD_PAL60;
	if (std == V4L2_STD_NTSC_443)
		return ADV7482_SDP_STD_NTSC_443;
	if (std == V4L2_STD_PAL_N)
		return ADV7482_SDP_STD_PAL_N;
	if (std == V4L2_STD_PAL_M)
		return ADV7482_SDP_STD_PAL_M;
	if (std == V4L2_STD_PAL_Nc)
		return ADV7482_SDP_STD_PAL_COMB_N;
	if (std & V4L2_STD_PAL)
		return ADV7482_SDP_STD_PAL_BG;
	if (std & V4L2_STD_NTSC)
		return ADV7482_SDP_STD_NTSC_M;
	if (std & V4L2_STD_SECAM)
		return ADV7482_SDP_STD_PAL_SECAM;

	return -EINVAL;
}

static int adv7482_sdp_set_video_standard(struct adv7482_state *state,
					  v4l2_std_id std)
{
	int sdpstd;

	sdpstd = adv7482_sdp_std(std);
	if (sdpstd < 0)
		return sdpstd;

	sdp_clrset(state, 0x02, 0xf0, (sdpstd & 0xf) << 4);

	return 0;
}

/* -----------------------------------------------------------------------------
 * TXA and TXB
 */

static const struct adv7482_reg_value adv7482_power_up_txa_4lane[] = {

	{ADV7482_I2C_TXA, 0x00, 0x84},	/* Enable 4-lane MIPI */
	{ADV7482_I2C_TXA, 0x00, 0xA4},	/* Set Auto DPHY Timing */

	{ADV7482_I2C_TXA, 0x31, 0x82},	/* ADI Required Write */
	{ADV7482_I2C_TXA, 0x1E, 0x40},	/* ADI Required Write */
	{ADV7482_I2C_TXA, 0xDA, 0x01},	/* i2c_mipi_pll_en - 1'b1 */
	{ADV7482_I2C_WAIT, 0x00, 0x02},	/* delay 2 */
	{ADV7482_I2C_TXA, 0x00, 0x24 },	/* Power-up CSI-TX */
	{ADV7482_I2C_WAIT, 0x00, 0x01},	/* delay 1 */
	{ADV7482_I2C_TXA, 0xC1, 0x2B},	/* ADI Required Write */
	{ADV7482_I2C_WAIT, 0x00, 0x01},	/* delay 1 */
	{ADV7482_I2C_TXA, 0x31, 0x80},	/* ADI Required Write */

	{ADV7482_I2C_EOR, 0xFF, 0xFF}	/* End of register table */
};

static const struct adv7482_reg_value adv7482_power_down_txa_4lane[] = {

	{ADV7482_I2C_TXA, 0x31, 0x82},	/* ADI Required Write */
	{ADV7482_I2C_TXA, 0x1E, 0x00},	/* ADI Required Write */
	{ADV7482_I2C_TXA, 0x00, 0x84},	/* Enable 4-lane MIPI */
	{ADV7482_I2C_TXA, 0xDA, 0x01},	/* i2c_mipi_pll_en - 1'b1 */
	{ADV7482_I2C_TXA, 0xC1, 0x3B},	/* ADI Required Write */

	{ADV7482_I2C_EOR, 0xFF, 0xFF}	/* End of register table */
};

static const struct adv7482_reg_value adv7482_power_up_txb_1lane[] = {

	{ADV7482_I2C_TXB, 0x00, 0x81},	/* Enable 1-lane MIPI */
	{ADV7482_I2C_TXB, 0x00, 0xA1},	/* Set Auto DPHY Timing */

	{ADV7482_I2C_TXB, 0x31, 0x82},	/* ADI Required Write */
	{ADV7482_I2C_TXB, 0x1E, 0x40},	/* ADI Required Write */
	{ADV7482_I2C_TXB, 0xDA, 0x01},	/* i2c_mipi_pll_en - 1'b1 */
	{ADV7482_I2C_WAIT, 0x00, 0x02},	/* delay 2 */
	{ADV7482_I2C_TXB, 0x00, 0x21 },	/* Power-up CSI-TX */
	{ADV7482_I2C_WAIT, 0x00, 0x01},	/* delay 1 */
	{ADV7482_I2C_TXB, 0xC1, 0x2B},	/* ADI Required Write */
	{ADV7482_I2C_WAIT, 0x00, 0x01},	/* delay 1 */
	{ADV7482_I2C_TXB, 0x31, 0x80},	/* ADI Required Write */

	{ADV7482_I2C_EOR, 0xFF, 0xFF}	/* End of register table */
};

static const struct adv7482_reg_value adv7482_power_down_txb_1lane[] = {

	{ADV7482_I2C_TXB, 0x31, 0x82},	/* ADI Required Write */
	{ADV7482_I2C_TXB, 0x1E, 0x00},	/* ADI Required Write */
	{ADV7482_I2C_TXB, 0x00, 0x81},	/* Enable 4-lane MIPI */
	{ADV7482_I2C_TXB, 0xDA, 0x01},	/* i2c_mipi_pll_en - 1'b1 */
	{ADV7482_I2C_TXB, 0xC1, 0x3B},	/* ADI Required Write */

	{ADV7482_I2C_EOR, 0xFF, 0xFF}	/* End of register table */
};

static int adv7482_txa_power(struct adv7482_state *state, bool on)
{
	int val, ret;

	val = txa_read(state, 0x1e);
	if (val < 0)
		return val;

	if (on && ((val & 0x40) == 0))
		ret = adv7482_write_regs(state, adv7482_power_up_txa_4lane);
	else
		ret = adv7482_write_regs(state, adv7482_power_down_txa_4lane);

	return ret;
}

static int adv7482_txb_power(struct adv7482_state *state, bool on)
{
	int val, ret;

	val = txb_read(state, 0x1e);
	if (val < 0)
		return val;

	if (on && ((val & 0x40) == 0))
		ret = adv7482_write_regs(state, adv7482_power_up_txb_1lane);
	else
		ret = adv7482_write_regs(state, adv7482_power_down_txb_1lane);

	return ret;
}

/* -----------------------------------------------------------------------------
 * V4L Video
 */

static int adv7482_g_std(struct v4l2_subdev *sd, v4l2_std_id *norm)
{
	struct adv7482_state *state = to_state(sd);

	/* TODO: This is only valid for SDP pad*/

	*norm = state->sdp.curr_norm;

	return 0;
}


static int adv7482_s_std(struct v4l2_subdev *sd, v4l2_std_id std)
{
	struct adv7482_state *state = to_state(sd);
	int ret;

	/* TODO: This is only valid for SDP pad*/

	ret = mutex_lock_interruptible(&state->mutex);
	if (ret)
		return ret;

	ret = adv7482_sdp_set_video_standard(state, std);
	if (ret < 0)
		goto out;

	state->sdp.curr_norm = std;

out:
	mutex_unlock(&state->mutex);
	return ret;
}

static int adv7482_querystd(struct v4l2_subdev *sd, v4l2_std_id *std)
{
	/* TODO: This is only valid for SDP pad*/

	struct adv7482_state *state = to_state(sd);
	int ret;

	ret = mutex_lock_interruptible(&state->mutex);
	if (ret)
		return ret;

	if (state->sdp.streaming) {
		ret = -EBUSY;
		goto unlock;
	}

	/* Set auto detect mode */
	ret = adv7482_sdp_set_video_standard(state, V4L2_STD_ALL);
	if (ret)
		goto unlock;

	msleep(100);

	/* Read detected standard */
	ret = adv7482_sdp_status(state, NULL, std);
	if (ret)
		goto unlock;

	/* Set detected standard */
	state->sdp.curr_norm = *std;
	ret = adv7482_sdp_set_video_standard(state, *std);
unlock:
	mutex_unlock(&state->mutex);

	return ret;
}

static int adv7482_g_tvnorms(struct v4l2_subdev *sd, v4l2_std_id *norm)
{
	/* TODO: This is only valid for SDP pad*/

	*norm = V4L2_STD_ALL;

	return 0;
}
static int adv7482_g_input_status(struct v4l2_subdev *sd, u32 *status)
{
	struct adv7482_state *state = to_state(sd);
	int ret;

	/* TODO: this needs to be sink pad aware */

	ret = mutex_lock_interruptible(&state->mutex);
	if (ret)
		return ret;
#ifdef HACK_HDMI_DEFAULT
	*status = adv7482_hdmi_have_signal(state) ? 0 : V4L2_IN_ST_NO_SIGNAL;
#else
	ret = adv7482_sdp_status(state, status, NULL);
#endif
	mutex_unlock(&state->mutex);
	return ret;
}

static int adv7482_cropcap(struct v4l2_subdev *sd, struct v4l2_cropcap *cropcap)
{
	/* TODO: this needs to be sink pad aware */

#ifdef HACK_HDMI_DEFAULT
	cropcap->pixelaspect.numerator = 1;
	cropcap->pixelaspect.denominator = 1;
#else
	struct adv7482_state *state = to_state(sd);

	if (state->sdp.curr_norm & V4L2_STD_525_60) {
		cropcap->pixelaspect.numerator = 11;
		cropcap->pixelaspect.denominator = 10;
	} else {
		cropcap->pixelaspect.numerator = 54;
		cropcap->pixelaspect.denominator = 59;
	}
#endif

	return 0;
}

static int adv7482_s_dv_timings(struct v4l2_subdev *sd,
				struct v4l2_dv_timings *timings)
{
	struct adv7482_state *state = to_state(sd);
	struct v4l2_bt_timings *bt;
	int ret;

	/* TODO: This is only valid for CP pad*/

	if (!timings)
		return -EINVAL;

	if (v4l2_match_dv_timings(&state->cp.timings, timings, 0, false))
		return 0;

	bt = &timings->bt;

	if (!v4l2_valid_dv_timings(timings, &adv7482_cp_timings_cap,
				   NULL, NULL))
		return -ERANGE;

	adv7482_fill_optional_dv_timings(state, timings);

	ret = adv7482_hdmi_set_video_timings(state, timings);
	if (ret)
		return ret;

	state->cp.timings = *timings;

	cp_clrset(state, 0x91, 0x40, bt->interlaced ? 0x40 : 0x00);

	return 0;
}

static int adv7482_g_dv_timings(struct v4l2_subdev *sd,
				struct v4l2_dv_timings *timings)
{
	struct adv7482_state *state = to_state(sd);

	/* TODO: This is only valid for CP pad*/

	*timings = state->cp.timings;

	return 0;
}

static int adv7482_query_dv_timings(struct v4l2_subdev *sd,
				    struct v4l2_dv_timings *timings)
{
	struct adv7482_state *state = to_state(sd);
	struct v4l2_bt_timings *bt = &timings->bt;
	int tmp;

	/* TODO: This is only valid for CP pad*/

	if (!timings)
		return -EINVAL;

	memset(timings, 0, sizeof(struct v4l2_dv_timings));

	if (!adv7482_hdmi_have_signal(state)) {
		adv_info(state, "Not detect any HDMI signal\n");
		return -ENOLINK;
	}

	timings->type = V4L2_DV_BT_656_1120;

	bt->interlaced = hdmi_read(state, 0x0b) & BIT(5) ?
		V4L2_DV_INTERLACED : V4L2_DV_PROGRESSIVE;

	bt->width = hdmi_read16(state, 0x07, 0x1fff);
	bt->height = hdmi_read16(state, 0x09, 0x1fff);
	bt->hfrontporch = hdmi_read16(state, 0x20, 0x1fff);
	bt->hsync = hdmi_read16(state, 0x22, 0x1fff);
	bt->hbackporch = hdmi_read16(state, 0x24, 0x1fff);
	bt->vfrontporch = hdmi_read16(state, 0x2a, 0x3fff) / 2;
	bt->vsync = hdmi_read16(state, 0x2e, 0x3fff) / 2;
	bt->vbackporch = hdmi_read16(state, 0x32, 0x3fff) / 2;

	bt->pixelclock = adv7482_hdmi_read_pixelclock(state);

	tmp = hdmi_read(state, 0x05);
	bt->polarities = (tmp & BIT(4) ? V4L2_DV_VSYNC_POS_POL : 0) |
		(tmp & BIT(5) ? V4L2_DV_HSYNC_POS_POL : 0);

	if (bt->interlaced == V4L2_DV_INTERLACED) {
		bt->height += hdmi_read16(state, 0x0b, 0x1fff);
		bt->il_vfrontporch = hdmi_read16(state, 0x2c, 0x3fff) / 2;
		bt->il_vsync = hdmi_read16(state, 0x30, 0x3fff) / 2;
		bt->il_vbackporch = hdmi_read16(state, 0x34, 0x3fff) / 2;
	}

	adv7482_fill_optional_dv_timings(state, timings);

	if (!adv7482_hdmi_have_signal(state)) {
		adv_info(state, "HDMI signal lost during readout\n");
		return -ENOLINK;
	}

	adv_dbg(state, "HDMI %dx%d%c clock: %llu Hz pol: %x " \
		"hfront: %d hsync: %d hback: %d " \
		"vfront: %d vsync: %d vback: %d " \
		"il_vfron: %d il_vsync: %d il_vback: %d\n",
		bt->width, bt->height,
		bt->interlaced == V4L2_DV_INTERLACED ? 'i' : 'p',
		bt->pixelclock, bt->polarities,
		bt->hfrontporch, bt->hsync, bt->hbackporch,
		bt->vfrontporch, bt->vsync, bt->vbackporch,
		bt->il_vfrontporch, bt->il_vsync, bt->il_vbackporch);

	return 0;
}

/* -----------------------------------------------------------------------------
 * V4L Pad
 */

static int adv7482_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index != 0)
		return -EINVAL;

	switch (code->pad) {
	case ADV7482_SOURCE_TXA:
		code->code = MEDIA_BUS_FMT_RGB888_1X24;
		break;
	case ADV7482_SOURCE_TXB:
		code->code = MEDIA_BUS_FMT_YUYV8_2X8;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int adv7482_get_pad_format(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_format *format)
{
	struct adv7482_state *state = to_state(sd);

	switch (format->pad) {
	case ADV7482_SOURCE_TXA:
		adv7482_hdmi_fill_format(state, &format->format);
		break;
	case ADV7482_SOURCE_TXB:
		adv7482_sdp_fill_format(state, &format->format);
		break;
	default:
		return -EINVAL;
	}

	if (format->which == V4L2_SUBDEV_FORMAT_TRY) {
		struct v4l2_mbus_framefmt *fmt;

		fmt = v4l2_subdev_get_try_format(sd, cfg, format->pad);
		format->format.code = fmt->code;
	}

	return 0;
}

static int adv7482_set_pad_format(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_format *format)
{
	struct adv7482_state *state = to_state(sd);

	switch (format->pad) {
	case ADV7482_SOURCE_TXA:
		adv7482_hdmi_fill_format(state, &format->format);
		break;
	case ADV7482_SOURCE_TXB:
		adv7482_sdp_fill_format(state, &format->format);
		break;
	default:
		return -EINVAL;
	}

	if (format->which == V4L2_SUBDEV_FORMAT_TRY) {
		struct v4l2_mbus_framefmt *fmt;

		fmt = v4l2_subdev_get_try_format(sd, cfg, format->pad);
		fmt->code = format->format.code;
	}

	return 0;
}

static int adv7482_enum_dv_timings(struct v4l2_subdev *sd,
				   struct v4l2_enum_dv_timings *timings)
{
	if (timings->pad != ADV7482_SINK_HDMI)
		return -ENOTTY;

	return v4l2_enum_dv_timings_cap(timings, &adv7482_cp_timings_cap,
					NULL, NULL);
}

static int adv7482_dv_timings_cap(struct v4l2_subdev *sd,
				  struct v4l2_dv_timings_cap *cap)
{
	if (cap->pad != ADV7482_SINK_HDMI)
		return -EINVAL;

	*cap = adv7482_cp_timings_cap;
	return 0;
}

static int adv7482_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct adv7482_state *state = to_state(sd);
	int ret, signal = 0;

	ret = mutex_lock_interruptible(&state->mutex);
	if (ret)
		return ret;

#ifdef HACK_HDMI_DEFAULT
	ret = adv7482_txa_power(state, enable);
	if (ret)
		goto error;

	if (adv7482_hdmi_have_signal(state))
		adv_dbg(state, "Detected HDMI video signal\n");
	else
		adv_info(state, "Not detect any HDMI video signal\n");
#else
	ret = adv7482_txb_power(state, enable);
	if (ret)
		goto error;

	state->sdp.streaming = enable;

	adv7482_sdp_status(state, &signal, NULL);
	if (signal)
		adv_dbg(state, "Detected SDP video signal\n");
	else
		adv_info(state, "Not detect any SDP video signal\n");
#endif
error:
	mutex_unlock(&state->mutex);
	return ret;
}

static const struct v4l2_subdev_video_ops adv7482_video_ops = {
#ifdef HACK_HDMI_DEFAULT
	.s_dv_timings = adv7482_s_dv_timings,
	.g_dv_timings = adv7482_g_dv_timings,
	.query_dv_timings = adv7482_query_dv_timings,
#else
	.g_std = adv7482_g_std,
	.s_std = adv7482_s_std,
	.querystd = adv7482_querystd,
	.g_tvnorms = adv7482_g_tvnorms,
#endif
	.g_input_status = adv7482_g_input_status,
	.s_stream = adv7482_s_stream,
	.cropcap = adv7482_cropcap,
};

static const struct v4l2_subdev_pad_ops adv7482_pad_ops = {
	.enum_mbus_code = adv7482_enum_mbus_code,
	.set_fmt = adv7482_set_pad_format,
	.get_fmt = adv7482_get_pad_format,
	.dv_timings_cap = adv7482_dv_timings_cap,
	.enum_dv_timings = adv7482_enum_dv_timings,
};

static const struct v4l2_subdev_ops adv7482_ops = {
	.video = &adv7482_video_ops,
	.pad = &adv7482_pad_ops,
};

/* -----------------------------------------------------------------------------
 * Media Operations
 */

static bool adv7482_media_has_route(struct media_entity *entity,
				    unsigned int pad0, unsigned int pad1)
{
	/* TODO: hard coded */

	if (pad0 == ADV7482_SINK_HDMI && pad1 == ADV7482_SOURCE_TXA)
		return true;

	if (pad1 == ADV7482_SINK_HDMI && pad0 == ADV7482_SOURCE_TXA)
		return true;

	if (pad0 == ADV7482_SINK_AIN8 && pad1 == ADV7482_SOURCE_TXB)
		return true;

	if (pad1 == ADV7482_SINK_AIN8 && pad0 == ADV7482_SOURCE_TXB)
		return true;

	return false;
}

static const struct media_entity_operations adv7482_media_ops = {
	.has_route =  adv7482_media_has_route,
};

/* -----------------------------------------------------------------------------
 * HW setup
 */

static const struct adv7482_reg_value adv7482_sw_reset[] = {

	{ADV7482_I2C_IO, 0xFF, 0xFF},	/* SW reset */
	{ADV7482_I2C_WAIT, 0x00, 0x05},	/* delay 5 */
	{ADV7482_I2C_IO, 0x01, 0x76},	/* ADI Required Write */
	{ADV7482_I2C_IO, 0xF2, 0x01},	/* Enable I2C Read Auto-Increment */
	{ADV7482_I2C_EOR, 0xFF, 0xFF}	/* End of register table */
};

static const struct adv7482_reg_value adv7482_set_slave_address[] = {
	{ADV7482_I2C_IO, 0xF3, ADV7482_I2C_DPLL * 2},	/* DPLL */
	{ADV7482_I2C_IO, 0xF4, ADV7482_I2C_CP * 2},	/* CP */
	{ADV7482_I2C_IO, 0xF5, ADV7482_I2C_HDMI * 2},	/* HDMI */
	{ADV7482_I2C_IO, 0xF6, ADV7482_I2C_EDID * 2},	/* EDID */
	{ADV7482_I2C_IO, 0xF7, ADV7482_I2C_REPEATER * 2}, /* HDMI RX Repeater */
	{ADV7482_I2C_IO, 0xF8, ADV7482_I2C_INFOFRAME * 2},/* HDMI RX InfoFrame*/
	{ADV7482_I2C_IO, 0xFA, ADV7482_I2C_CEC * 2},	/* CEC */
	{ADV7482_I2C_IO, 0xFB, ADV7482_I2C_SDP * 2},	/* SDP */
	{ADV7482_I2C_IO, 0xFC, ADV7482_I2C_TXB * 2},	/* CSI-TXB */
	{ADV7482_I2C_IO, 0xFD, ADV7482_I2C_TXA * 2},	/* CSI-TXA */
	{ADV7482_I2C_EOR, 0xFF, 0xFF}	/* End of register table */
};

/* Supported Formats For Script Below */
/* - 01-29 HDMI to MIPI TxA CSI 4-Lane - RGB888: */
static const struct adv7482_reg_value adv7482_init_txa_4lane[] = {
	/* Disable chip powerdown & Enable HDMI Rx block */
	{ADV7482_I2C_IO, 0x00, 0x40},

	{ADV7482_I2C_REPEATER, 0x40, 0x83}, /* Enable HDCP 1.1 */

	{ADV7482_I2C_HDMI, 0x00, 0x08},	/* Foreground Channel = A */
	{ADV7482_I2C_HDMI, 0x98, 0xFF},	/* ADI Required Write */
	{ADV7482_I2C_HDMI, 0x99, 0xA3},	/* ADI Required Write */
	{ADV7482_I2C_HDMI, 0x9A, 0x00},	/* ADI Required Write */
	{ADV7482_I2C_HDMI, 0x9B, 0x0A},	/* ADI Required Write */
	{ADV7482_I2C_HDMI, 0x9D, 0x40},	/* ADI Required Write */
	{ADV7482_I2C_HDMI, 0xCB, 0x09},	/* ADI Required Write */
	{ADV7482_I2C_HDMI, 0x3D, 0x10},	/* ADI Required Write */
	{ADV7482_I2C_HDMI, 0x3E, 0x7B},	/* ADI Required Write */
	{ADV7482_I2C_HDMI, 0x3F, 0x5E},	/* ADI Required Write */
	{ADV7482_I2C_HDMI, 0x4E, 0xFE},	/* ADI Required Write */
	{ADV7482_I2C_HDMI, 0x4F, 0x18},	/* ADI Required Write */
	{ADV7482_I2C_HDMI, 0x57, 0xA3},	/* ADI Required Write */
	{ADV7482_I2C_HDMI, 0x58, 0x04},	/* ADI Required Write */
	{ADV7482_I2C_HDMI, 0x85, 0x10},	/* ADI Required Write */

	{ADV7482_I2C_HDMI, 0x83, 0x00},	/* Enable All Terminations */
	{ADV7482_I2C_HDMI, 0xA3, 0x01},	/* ADI Required Write */
	{ADV7482_I2C_HDMI, 0xBE, 0x00},	/* ADI Required Write */

	{ADV7482_I2C_HDMI, 0x6C, 0x01},	/* HPA Manual Enable */
	{ADV7482_I2C_HDMI, 0xF8, 0x01},	/* HPA Asserted */
	{ADV7482_I2C_HDMI, 0x0F, 0x00},	/* Audio Mute Speed Set to Fastest */
	/* (Smallest Step Size) */

	{ADV7482_I2C_IO, 0x04, 0x02},	/* RGB Out of CP */
	{ADV7482_I2C_IO, 0x12, 0xF0},	/* CSC Depends on ip Packets, SDR 444 */
	{ADV7482_I2C_IO, 0x17, 0x80},	/* Luma & Chroma can reach 254d */
	{ADV7482_I2C_IO, 0x03, 0x86},	/* CP-Insert_AV_Code */

	{ADV7482_I2C_CP, 0x7C, 0x00},	/* ADI Required Write */

	{ADV7482_I2C_IO, 0x0C, 0xE0},	/* Enable LLC_DLL & Double LLC Timing */
	{ADV7482_I2C_IO, 0x0E, 0xDD},	/* LLC/PIX/SPI PINS TRISTATED AUD */
	/* Outputs Enabled */
	{ADV7482_I2C_IO, 0x10, 0xA0},	/* Enable 4-lane CSI Tx & Pixel Port */

	{ADV7482_I2C_TXA, 0x00, 0x84},	/* Enable 4-lane MIPI */
	{ADV7482_I2C_TXA, 0x00, 0xA4},	/* Set Auto DPHY Timing */
	{ADV7482_I2C_TXA, 0xDB, 0x10},	/* ADI Required Write */
	{ADV7482_I2C_TXA, 0xD6, 0x07},	/* ADI Required Write */
	{ADV7482_I2C_TXA, 0xC4, 0x0A},	/* ADI Required Write */
	{ADV7482_I2C_TXA, 0x71, 0x33},	/* ADI Required Write */
	{ADV7482_I2C_TXA, 0x72, 0x11},	/* ADI Required Write */
	{ADV7482_I2C_TXA, 0xF0, 0x00},	/* i2c_dphy_pwdn - 1'b0 */

	{ADV7482_I2C_TXA, 0x31, 0x82},	/* ADI Required Write */
	{ADV7482_I2C_TXA, 0x1E, 0x40},	/* ADI Required Write */
	{ADV7482_I2C_TXA, 0xDA, 0x01},	/* i2c_mipi_pll_en - 1'b1 */
	{ADV7482_I2C_WAIT, 0x00, 0x02},	/* delay 2 */
	{ADV7482_I2C_TXA, 0x00, 0x24 },	/* Power-up CSI-TX */
	{ADV7482_I2C_WAIT, 0x00, 0x01},	/* delay 1 */
	{ADV7482_I2C_TXA, 0xC1, 0x2B},	/* ADI Required Write */
	{ADV7482_I2C_WAIT, 0x00, 0x01},	/* delay 1 */
	{ADV7482_I2C_TXA, 0x31, 0x80},	/* ADI Required Write */

#ifdef REL_DGB_FORCE_TO_SEND_COLORBAR
	{ADV7482_I2C_CP, 0x37, 0x81},	/* Output Colorbars Pattern */
#endif
	{ADV7482_I2C_EOR, 0xFF, 0xFF}	/* End of register table */
};

/* 02-01 Analog CVBS to MIPI TX-B CSI 1-Lane - */
/* Autodetect CVBS Single Ended In Ain 1 - MIPI Out */
static const struct adv7482_reg_value adv7482_init_txb_1lane[] = {

	{ADV7482_I2C_IO, 0x00, 0x30},  /* Disable chip powerdown powerdown Rx */
	{ADV7482_I2C_IO, 0xF2, 0x01},  /* Enable I2C Read Auto-Increment */

	{ADV7482_I2C_IO, 0x0E, 0xFF},  /* LLC/PIX/AUD/SPI PINS TRISTATED */

	{ADV7482_I2C_SDP, 0x0f, 0x00}, /* Exit Power Down Mode */
	{ADV7482_I2C_SDP, 0x52, 0xCD},/* ADI Required Write */
	/* TODO: do not use hard codeded INSEL */
	{ADV7482_I2C_SDP, 0x00, ADV7482_SDP_INPUT_CVBS_AIN8},
	{ADV7482_I2C_SDP, 0x0E, 0x80},	/* ADI Required Write */
	{ADV7482_I2C_SDP, 0x9C, 0x00},	/* ADI Required Write */
	{ADV7482_I2C_SDP, 0x9C, 0xFF},	/* ADI Required Write */
	{ADV7482_I2C_SDP, 0x0E, 0x00},	/* ADI Required Write */

	/* ADI recommended writes for improved video quality */
	{ADV7482_I2C_SDP, 0x80, 0x51},	/* ADI Required Write */
	{ADV7482_I2C_SDP, 0x81, 0x51},	/* ADI Required Write */
	{ADV7482_I2C_SDP, 0x82, 0x68},	/* ADI Required Write */

	{ADV7482_I2C_SDP, 0x03, 0x42},  /* Tri-S Output , PwrDwn 656 pads */
	{ADV7482_I2C_SDP, 0x04, 0xB5},	/* ITU-R BT.656-4 compatible */
	{ADV7482_I2C_SDP, 0x13, 0x00},	/* ADI Required Write */

	{ADV7482_I2C_SDP, 0x17, 0x41},	/* Select SH1 */
	{ADV7482_I2C_SDP, 0x31, 0x12},	/* ADI Required Write */
	{ADV7482_I2C_SDP, 0xE6, 0x4F},  /* V bit end pos manually in NTSC */

#ifdef REL_DGB_FORCE_TO_SEND_COLORBAR
	{ADV7482_I2C_SDP, 0x0C, 0x01},	/* ColorBar */
	{ADV7482_I2C_SDP, 0x14, 0x01},	/* ColorBar */
#endif
	/* Enable 1-Lane MIPI Tx, */
	/* enable pixel output and route SD through Pixel port */
	{ADV7482_I2C_IO, 0x10, 0x70},

	{ADV7482_I2C_TXB, 0x00, 0x81},	/* Enable 1-lane MIPI */
	{ADV7482_I2C_TXB, 0x00, 0xA1},	/* Set Auto DPHY Timing */
	{ADV7482_I2C_TXB, 0xD2, 0x40},	/* ADI Required Write */
	{ADV7482_I2C_TXB, 0xC4, 0x0A},	/* ADI Required Write */
	{ADV7482_I2C_TXB, 0x71, 0x33},	/* ADI Required Write */
	{ADV7482_I2C_TXB, 0x72, 0x11},	/* ADI Required Write */
	{ADV7482_I2C_TXB, 0xF0, 0x00},	/* i2c_dphy_pwdn - 1'b0 */
	{ADV7482_I2C_TXB, 0x31, 0x82},	/* ADI Required Write */
	{ADV7482_I2C_TXB, 0x1E, 0x40},	/* ADI Required Write */
	{ADV7482_I2C_TXB, 0xDA, 0x01},	/* i2c_mipi_pll_en - 1'b1 */

	{ADV7482_I2C_WAIT, 0x00, 0x02},	/* delay 2 */
	{ADV7482_I2C_TXB, 0x00, 0x21 },	/* Power-up CSI-TX */
	{ADV7482_I2C_WAIT, 0x00, 0x01},	/* delay 1 */
	{ADV7482_I2C_TXB, 0xC1, 0x2B},	/* ADI Required Write */
	{ADV7482_I2C_WAIT, 0x00, 0x01},	/* delay 1 */
	{ADV7482_I2C_TXB, 0x31, 0x80},	/* ADI Required Write */

	{ADV7482_I2C_EOR, 0xFF, 0xFF}	/* End of register table */
};

static int adv7482_reset(struct adv7482_state *state)
{
	int ret;

	ret = adv7482_write_regs(state, adv7482_sw_reset);
	if (ret < 0)
		return ret;

	ret = adv7482_write_regs(state, adv7482_set_slave_address);
	if (ret < 0)
		return ret;

	/* Init and power down TXA */
	ret = adv7482_write_regs(state, adv7482_init_txa_4lane);
	if (ret)
		return ret;
	adv7482_txa_power(state, 0);
	/* Set VC 0 */
	txa_clrset(state, 0x0d, 0xc0, 0x00);

	/* Init and power down TXB */
	ret = adv7482_write_regs(state, adv7482_init_txb_1lane);
	if (ret)
		return ret;
	adv7482_txb_power(state, 0);
	/* Set VC 0 */
	txb_clrset(state, 0x0d, 0xc0, 0x00);

	/* Disable chip powerdown & Enable HDMI Rx block */
	io_write(state, 0x00, 0x40);

	/* Enable 4-lane CSI Tx & Pixel Port */
	io_write(state, 0x10, 0xe0);

	/* Use vid_std and v_freq as freerun resolution for CP */
	cp_clrset(state, 0xc9, 0x01, 0x01);

	return 0;
}

static int adv7482_print_info(struct adv7482_state *state)
{
	int msb, lsb;

	lsb = io_read(state, 0xdf);
	msb = io_read(state, 0xe0);

	if (lsb < 0 || msb < 0) {
		adv_err(state, "Failed to read chip revsion\n");
		return -EIO;
	}

	adv_info(state, "chip found @ 0x%02x revision %02x%02x\n",
		 state->client->addr << 1, lsb, msb);

	return 0;
}

/* -----------------------------------------------------------------------------
 * i2c driver
 */

static int adv7482_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct adv7482_state *state;
	static const struct v4l2_dv_timings cea720x480 =
		V4L2_DV_BT_CEA_720X480I59_94;
	int i, ret;

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;

	state = devm_kzalloc(&client->dev, sizeof(struct adv7482_state),
			     GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	mutex_init(&state->mutex);

	state->dev = &client->dev;
	state->client = client;

	state->sdp.streaming = false;
	state->sdp.curr_norm = V4L2_STD_NTSC;

	state->cp.timings = cea720x480;

	v4l2_i2c_subdev_init(&state->sd, client, &adv7482_ops);

	state->sd.flags = V4L2_SUBDEV_FL_HAS_DEVNODE;
	state->sd.entity.flags |= MEDIA_ENT_F_ATV_DECODER;

	/* SW reset AVD7482 to its default values */
	ret = adv7482_reset(state);
	if (ret)
		return ret;

	ret = adv7482_print_info(state);
	if (ret)
		return ret;

	for (i = ADV7482_SINK_HDMI; i < ADV7482_SOURCE_TXA; i++)
		state->pads[i].flags = MEDIA_PAD_FL_SINK;
	for (i = ADV7482_SOURCE_TXA; i <= ADV7482_SOURCE_TXB; i++)
		state->pads[i].flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&state->sd.entity, ADV7482_PAD_MAX,
				     state->pads);
	if (ret)
		return ret;

	state->sd.entity.ops = &adv7482_media_ops;

	ret = v4l2_async_register_subdev(&state->sd);
	if (ret)
		return ret;

	return 0;
}

static int adv7482_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct adv7482_state *state = to_state(sd);

	v4l2_async_unregister_subdev(sd);

	media_entity_cleanup(&sd->entity);

	mutex_destroy(&state->mutex);

	return 0;
}

static const struct i2c_device_id adv7482_id[] = {
	{ "adv7482", 0 },
	{ },
};

static const struct of_device_id adv7482_of_table[] = {
	{ .compatible = "adi,adv7482", },
	{ }
};
MODULE_DEVICE_TABLE(of, adv7482_of_ids);

static struct i2c_driver adv7482_driver = {
	.driver = {
		.name = "adv7482",
		.of_match_table = of_match_ptr(adv7482_of_table),
	},
	.probe = adv7482_probe,
	.remove = adv7482_remove,
	.id_table = adv7482_id,
};

module_i2c_driver(adv7482_driver);

MODULE_AUTHOR("Niklas Söderlund <niklas.soderlund@ragnatech.se>");
MODULE_DESCRIPTION("HDMI Receiver ADV7482 video decoder driver");
MODULE_LICENSE("GPL v2");
