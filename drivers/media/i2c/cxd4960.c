// SPDX-License-Identifier: GPL-2.0
/*
 * A V4L2 driver for Sony CXD4960 cameras.
 * Copyright (C) 2019, Raspberry Pi (Trading) Ltd
 *
 * Based on Sony imx258 camera driver
 * Copyright (C) 2018 Intel Corporation
 *
 * DT / fwnode changes, and regulator / GPIO control taken from imx214 driver
 * Copyright 2018 Qtechnology A/S
 *
 * Flip handling taken from the Sony IMX319 driver.
 * Copyright (C) 2018 Intel Corporation
 *
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/of_graph.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mediabus.h>
#include <asm/unaligned.h>
#include <asm/io.h>
#include <uapi/misc/emc_data.h>

/* Page Size */
#define MSIOF_PAGE_SIZE	0x1000
#define PWM_PAGE_SIZE	0x4000
#define GPIO_PAGE_SIZE	0x1000

/* DES_REFCLK_1R8V */
#define MSIOF3_BASE			0xE6C10000
#define MSIOF_REG_SITMDR1	0x0000
#define MSIOF_REG_SITSCR	0x0020
#define MSIOF_REG_SICTR		0x0028
#define MSIOF_BRPS			0x0100
#define MSIOF_BRDV			0x0000
#define MSIOF_TRMD			0x80000000
#define MSIOF_TSCKIZ		0x00000000
#define MSIOF_TSCKE			0x00008000

/* FSYNC_1R8V */
#define PWM_BASE		0xE6E30000
#define PWM_REG_PWMCR	0x3000
#define PWM_REG_PWMCNT	0x3004
#define PWM_CC0			0x00060000
#define PWM_CCMD		0x00008000
//#define PWM_SYNC		0x00000800
#define PWM_SYNC		0x00000000
#define PWM_SS0			0x00000000
#define PWM_EN0			0x00000001
//#define PWM_CYC0		0x032E0000
#define PWM_CYC0		0x032D0000
//#define PWM_PH0			0x00000197
#define PWM_PH0			0x00000196

/* DES_CE_1R8V */
#define GPIO01_BASE		0xE6050000
#define GPIO1_REG_PMMR	0x0800
#define GPIO1_REG_POC	0x08A0
#define GPIO1_REG_PUEN	0x08C0
#define GPIO1_REG_OUTDT	0x0988
#define GPIO1_POC_CE	0x00000001
#define GPIO1_PUEN_CE	0x00000001
#define GPIO1_OUTDT_CE	0x00000001

#define CXD4960_REG_VALUE_08BIT	1
#define CXD4960_REG_VALUE_16BIT	2

#define CXD4960_REG_SERDES_LINK		0x01
#define CXD4960_REG_REMOTE_COMPLETE	0xB5

#define CXD4960_VALUE_SERDES_LINK		0x10
#define CXD4960_VALUE_REMOTE_COMPLETE	0x01

#define CXD4960_REG_VIDEO_OUTPUT_ENABLE		0x76
#define CXD4960_VALUE_VIDEO_OUTPUT_ENABLE	1
#define CXD4960_VALUE_VIDEO_OUTPUT_DISABLE	0

#define CXD4960_REG_ERROR_CLEAR			0x1F
#define CXD4960_VALUE_ERROR_NOTCLEAR	0x00
#define CXD4960_VALUE_ERROR_CLEAR		0x01

#define CXD4960_REG_SSCG_CONTROL		0x80

#define CXD4960_REG_LINK_STATUS				0x01
#define CXD4960_REG_ERROR_STATUS			0x10
#define CXD4960_MASK_LINK_READY				0x10
#define CXD4960_MASK_LINK_GVIF2RX_LOS		0x01
#define CXD4960_MASK_LINK_STATUS_CHECK		(CXD4960_MASK_LINK_READY | CXD4960_MASK_LINK_GVIF2RX_LOS)
#define CXD4960_MASK_ERROR_GVIF2RX_FAIL		0x80
#define CXD4960_MASK_ERROR_VIDEOTX_FAIL		0x10
#define CXD4960_MASK_ERROR_STATUS_CHECK		(CXD4960_MASK_ERROR_GVIF2RX_FAIL | CXD4960_MASK_ERROR_VIDEOTX_FAIL)
#define CXD4960_VALUE_LINK_READY			0x10
#define CXD4960_VALUE_LINK_GVIF2RX_LOS		0x00
#define CXD4960_VALUE_LINK_STATUS_CHECK		(CXD4960_VALUE_LINK_READY | CXD4960_VALUE_LINK_GVIF2RX_LOS)
#define CXD4960_VALUE_ERROR_GVIF2RX_FAIL	0x00
#define CXD4960_VALUE_ERROR_VIDEOTX_FAIL	0x00
#define CXD4960_VALUE_ERROR_STATUS_CHECK	(CXD4960_VALUE_ERROR_GVIF2RX_FAIL | CXD4960_VALUE_ERROR_VIDEOTX_FAIL)

#define PRE_ERROR_BOOT_INIT				0x44
#define ERROR_BOOT_INIT					0x44
#define PRE_ERROR_DES_GVIF_INIT			0x04
#define ERROR_DES_GVIF_INIT				0x04
#define PRE_ERROR_DES_VIDES_OUT_INIT	0x00
#define ERROR_DES_VIDES_OUT_INIT		0x00
#define PRE_ERROR_DMC_SER_INIT			0x04
#define ERROR_DMC_SER_INIT				0x04
#define PRE_ERROR_LVDS_I2C_INIT			0x00
#define ERROR_LVDS_I2C_INIT				0x00

#define NO_ERROR 0
#define PRE_BOOT_ERROR 1
#define BOOT_ERROR 2

/* Power ON */
#define POWER_ON 1
#define POWER_OFF 0
#define DES_POWER_ON 1
#define DSM_POWER_ON 1

/* FCM Streaming */
#define FCM_STREAMING 1
#define NG_FCM 0
#define OK_FCM 1

#define DEBUG_CXD4960  /* Debug print enable */
#ifdef DEBUG_CXD4960
#define cxd4960_dbg(dev, fmt, arg...)	dev_info(dev, "<CXD4960>"fmt, ##arg)
#else
#define cxd4960_dbg(dev, fmt, arg...)
#endif

int boot_ready_error;
int boot_gvif2rx_los_error;
int boot_gvif2rx_fail_error;
int boot_videotx_fail;
int des_ready_error;
int des_gvif2rx_los_error;
int des_gvif2rx_fail_error;
int des_output_error;
int lvds_i2c_com_error;

struct mutex cxd4960_csi_err_lock;
int cxd4960_csi_err_notify;

struct cxd4960_reg {
	u16 address;
	u8 val;
};

static const struct cxd4960_reg init_des_set_regs_step1[] = {
	{0x29, 0x06},
	{0x20, 0x01},
};/* init_des_set_regs_step1 */

static const struct cxd4960_reg init_des_set_regs_step2[] = {
	{0xB0, 0x42},
	{0xB1, 0xC0},
	{0xB2, 0x41},
	{0xB4, 0x01},
};/* init_des_set_regs_step2 */

static const struct cxd4960_reg init_des_set_regs_step3[] = {
	{0xB4, 0x00},
	{0xC0, 0x41},
};/* init_des_set_regs_step3 */

static const struct cxd4960_reg init_des_set_regs_step4[] = {
	{0xC2, 0x02},
	{0xA1, 0x03},
	{0xA0, 0x01},
	{0x76, 0x00},
	{0xFF, 0x01},
	{0xFE, 0x73},
	{0xC0, 0x60},
	{0xC1, 0x65},
	{0xC6, 0x29},
	{0xFE, 0x00},
	{0xFF, 0x00},
	{0x90, 0x20},
	{0x71, 0x42},
};/* init_des_set_regs_step4 */

enum cxd4960_pad {
	CXD4960_PAD_SINK,
	CXD4960_PAD_SOURCE,
	CXD4960_PAD_MAX,
};

struct cxd4960 {
	struct v4l2_subdev sd;
	struct media_pad pad[CXD4960_PAD_MAX];

	struct v4l2_async_notifier notifier;
	struct v4l2_subdev *remote;
	unsigned int remote_pad;

	struct v4l2_mbus_framefmt fmt;

	struct gpio_desc *reset_gpio;

	struct v4l2_ctrl_handler ctrl_handler;
	/* V4L2 Controls */
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *exposure;
	struct v4l2_ctrl *vflip;
	struct v4l2_ctrl *hflip;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *hblank;

	/*
	 * Mutex for serialized access:
	 * Protect sensor module set pad format and start/stop streaming safely.
	 */
	struct mutex mutex;

	/* Streaming on/off */
	bool streaming;
};

static int cxd4960_error_lvds_i2c_com_check(struct cxd4960 *cxd4960, int result);


void cxd4960_set_csi_err(void)
{
	mutex_lock(&cxd4960_csi_err_lock);
	cxd4960_csi_err_notify = 1;
	mutex_unlock(&cxd4960_csi_err_lock);
}

static inline struct cxd4960 *to_cxd4960(struct v4l2_subdev *_sd)
{
	return container_of(_sd, struct cxd4960, sd);
}

static inline struct cxd4960 *notifier_to_cxd4960(struct v4l2_async_notifier *n)
{
	return container_of(n, struct cxd4960, notifier);
}

/* Read registers up to 2 at a time */
static int cxd4960_read_reg(struct cxd4960 *cxd4960, u16 reg, u32 len, u32 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&cxd4960->sd);
	int ret;

	ret = i2c_smbus_read_byte_data(client, reg & 0xff);
	cxd4960_error_lvds_i2c_com_check(cxd4960, ret);
	if (ret < 0) {
		dev_err(&client->dev,
			"%s: read reg error %d: reg=%x, val=%x\n",
			__func__, ret, reg, *val);
		return ret;
	}
	*val = ret;

	return 0;
}

/* Write registers up to 2 at a time */
static int cxd4960_write_reg(struct cxd4960 *cxd4960, u16 reg, u32 len, u32 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&cxd4960->sd);
	int ret;

	ret = i2c_smbus_write_byte_data(client, reg & 0xff, val);
	cxd4960_error_lvds_i2c_com_check(cxd4960, ret);
	if (ret) {
		dev_err(&client->dev,
			"%s: write reg error %d: reg=%x, val=%x\n",
			__func__, ret, reg, val);
		return ret;
	}

	return 0;
}

/* Write a list of registers */
static int cxd4960_write_regs(struct cxd4960 *cxd4960,
			     const struct cxd4960_reg *regs, u32 len)
{
	struct i2c_client *client = v4l2_get_subdevdata(&cxd4960->sd);
	unsigned int i;
	int ret;

	for (i = 0; i < len; i++) {
		ret = cxd4960_write_reg(cxd4960, regs[i].address, 1, regs[i].val);
		if (ret) {
			dev_err_ratelimited(&client->dev,
					    "Failed to write reg 0x%4.4x. error = %d\n",
					    regs[i].address, ret);

			return ret;
		}
	}

	return 0;
}

/* Power/clock management functions */
static void cxd4960_control_ce(u32 io)
{
	u32 gpioreg;
	void *mapped;

	mapped = ioremap(GPIO01_BASE, GPIO_PAGE_SIZE);

	gpioreg = ioread32(mapped + GPIO1_REG_OUTDT);
	if (io)
		gpioreg |= GPIO1_OUTDT_CE;
	else
		gpioreg &= ~GPIO1_OUTDT_CE;
	iowrite32(gpioreg, mapped + GPIO1_REG_OUTDT);

	iounmap(mapped);

	return;
}

static int cxd4960_power_on(struct device *dev)
{
//	struct i2c_client *client = to_i2c_client(dev);
//	struct v4l2_subdev *sd = i2c_get_clientdata(client);
//	struct cxd4960 *cxd4960 = to_cxd4960(sd);

//	gpiod_set_value_cansleep(cxd4960->reset_gpio, 1);
	cxd4960_control_ce(1);
	msleep(10);

	return 0;
}

static int cxd4960_power_off(struct device *dev)
{
//	struct i2c_client *client = to_i2c_client(dev);
//	struct v4l2_subdev *sd = i2c_get_clientdata(client);
//	struct cxd4960 *cxd4960 = to_cxd4960(sd);

//	gpiod_set_value_cansleep(cxd4960->reset_gpio, 0);
	cxd4960_control_ce(0);

	return 0;
}

static void cxd4960_dsm_power_control(u32 control)
{
	u16 data;

	emc_get_exp_info(DSM_POWER_OFF_ON, &data);

	if (control)
	{
		data |= 0x001;
		emc_set_exp_info(DSM_POWER_OFF_ON, data);
	}
	else{
		data &= ~0x001;
		emc_set_exp_info(DSM_POWER_OFF_ON, data);
	}

	return;

}


static int cxd4960_error_status_clear(struct cxd4960 *cxd4960)
{
	int ret;

	ret = cxd4960_write_reg(cxd4960, CXD4960_REG_ERROR_CLEAR, CXD4960_REG_VALUE_08BIT, CXD4960_VALUE_ERROR_CLEAR);
	ret = cxd4960_write_reg(cxd4960, CXD4960_REG_ERROR_CLEAR, CXD4960_REG_VALUE_08BIT, CXD4960_VALUE_ERROR_NOTCLEAR);

	/* Serializer error status clear */
	ret = v4l2_subdev_call(cxd4960->remote, video, s_routing, 0, 0, 5);

	return ret;
}

static int cxd4960_initial_setting(struct cxd4960 *cxd4960)
{
	struct i2c_client *client = v4l2_get_subdevdata(&cxd4960->sd);
	u32 val;
	int ret = 0;
	int i = 0;

	ret = cxd4960_write_regs(cxd4960, init_des_set_regs_step1, ARRAY_SIZE(init_des_set_regs_step1));
	if (ret)
		return ret;

	while(1){
		ret = cxd4960_read_reg(cxd4960, CXD4960_REG_SERDES_LINK, CXD4960_REG_VALUE_08BIT, &val);
		if (ret)
			return ret;
		if(val == CXD4960_VALUE_SERDES_LINK)
		{
			dev_info(&client->dev, "SerDes link up\n");
			break;
		}
		if(i >= 10)
		{
			dev_info(&client->dev, "NG:No SerDes connection\n");
			break;
		}
		i++;
	}

	ret = cxd4960_write_regs(cxd4960, init_des_set_regs_step2, ARRAY_SIZE(init_des_set_regs_step2));
	if (ret)
		return ret;

	i = 0;
	while(1){
		if (ret)
			return ret;
		ret = cxd4960_read_reg(cxd4960, CXD4960_REG_REMOTE_COMPLETE, CXD4960_REG_VALUE_08BIT, &val);
		if(val == CXD4960_VALUE_REMOTE_COMPLETE)
		{
			dev_info(&client->dev, "remote register write complete\n");
			break;
		}
		if(i >= 10)
		{
			dev_info(&client->dev, "NG:failed to write remote register\n");
			break;
		}
		i++;
	}

	ret = cxd4960_write_regs(cxd4960, init_des_set_regs_step3, ARRAY_SIZE(init_des_set_regs_step3));
	if (ret)
		return ret;

	usleep_range(30, 40);

	ret = cxd4960_write_regs(cxd4960, init_des_set_regs_step4, ARRAY_SIZE(init_des_set_regs_step4));
	if (ret)
		return ret;

	return ret;
}

#ifndef EYE_MAGIN_TEST
static int cxd4960_reboot_initial_setting(struct cxd4960 *cxd4960)
{
	struct i2c_client *client = v4l2_get_subdevdata(&cxd4960->sd);
	int ret = 0;

	void *mapped;

	/* Deserializer Reset Release */
	dev_info(&client->dev, "GPIO Des_CE High\n");
	cxd4960_control_ce(1);
	msleep(10);

	/* REFCLK */
	/* set parameter (addr should be aligned by MSIOF_PAGE_SIZE) */
	dev_info(&client->dev, "REFCLK Output\n");
	mapped = ioremap(MSIOF3_BASE, MSIOF_PAGE_SIZE);

	iowrite32(MSIOF_TRMD, mapped + MSIOF_REG_SITMDR1);
	iowrite16(MSIOF_BRPS | MSIOF_BRDV, mapped + MSIOF_REG_SITSCR);
	iowrite32(MSIOF_TSCKIZ | MSIOF_TSCKE, mapped + MSIOF_REG_SICTR);

	iounmap(mapped);

	/* Deserializer Initialize */
	dev_info(&client->dev, "Deserializer Initialize\n");
	ret = cxd4960_initial_setting(cxd4960);
	if (ret)
		return ret;

	/* Serializer Initialize */
	dev_info(&client->dev, "Serializer Initialize\n");
	v4l2_subdev_call(cxd4960->remote, video, s_stream, 1);

	/* Deserializer Video Output Enable */
	dev_info(&client->dev, "Deserializa Video Output Enable\n");
	ret = cxd4960_write_reg(cxd4960, CXD4960_REG_VIDEO_OUTPUT_ENABLE, CXD4960_REG_VALUE_08BIT, CXD4960_VALUE_VIDEO_OUTPUT_ENABLE);
	if (ret)
		return ret;

	/* Deserializer Error Status Clear */
	dev_info(&client->dev, "Deserializer Error Status Clear\n");
	ret = cxd4960_error_status_clear(cxd4960);
	if (ret)
		return ret;

	return ret;
}
#endif //EYE_MAGIN_TEST

static int cxd4960_error_boot_check(struct cxd4960 *cxd4960)
{
	struct i2c_client *client = v4l2_get_subdevdata(&cxd4960->sd);
	int ret = NO_ERROR;
	int retval = NO_ERROR;
	u32 val;
	u16 data;

	/* Deserializer register address 0x01 */
	ret = cxd4960_read_reg(cxd4960, CXD4960_REG_LINK_STATUS, CXD4960_REG_VALUE_08BIT, &val);
	if (ret) {
		cxd4960_dbg(&client->dev, " i2c read LINK_STATUS: NG[%d]\n", ret);
		retval= ret;
	} else {
		cxd4960_dbg(&client->dev, " i2c read LINK_STATUS: OK[%d]\n", ret);
		if (((u8)val & CXD4960_MASK_LINK_STATUS_CHECK) != CXD4960_VALUE_LINK_STATUS_CHECK) {
			cxd4960_dbg(&client->dev, " LINK_STATUS check, target bit is bit4,0: NG[%02X]\n", val);
			retval = -EIO;
			if ((val & CXD4960_MASK_LINK_READY) != CXD4960_VALUE_LINK_READY)
			{
				dev_info(&client->dev, "pre_error_DSM_Boot [%x]:%x\n", CXD4960_REG_LINK_STATUS, CXD4960_MASK_LINK_READY);
#ifndef EYE_MAGIN_TEST
				boot_ready_error++;
				dev_info(&client->dev, "pre_error_DSM_Boot [%x]:%x count:%d\n", CXD4960_REG_LINK_STATUS, CXD4960_MASK_LINK_READY, boot_ready_error);
				//Write a pre_error_DSM_Boot bit6 = 0 to System RAM.
				emc_get_exp_info(DSM_START_DUMMY_EXP_NVM, &data);
				data &= ~0x40;
				emc_set_exp_info(DSM_START_DUMMY_EXP_NVM, data);
				if(boot_ready_error >=10)
				{
					dev_info(&client->dev, "error_DSM_Boot [%x]:%x\n", CXD4960_REG_LINK_STATUS, CXD4960_MASK_LINK_READY);
					//Write a error_DSM_Boot bit6 = 0 to System RAM.
					emc_get_exp_info(DSM_START_EXP_NVM, &data);
					data &= ~0x40;
					emc_set_exp_info(DSM_START_EXP_NVM, data);
				}
#endif //EYE_MAGIN_TEST
			}
			else{
				dev_info(&client->dev, "No Error [%x]:%x\n", CXD4960_REG_LINK_STATUS, CXD4960_MASK_LINK_READY);
#ifndef EYE_MAGIN_TEST
				//Write a pre_error_DSM_Boot bit6 = 1 to System RAM.
				emc_get_exp_info(DSM_START_DUMMY_EXP_NVM, &data);
				data |= 0x40;
				emc_set_exp_info(DSM_START_DUMMY_EXP_NVM, data);
#endif //EYE_MAGIN_TEST
			}
			if ((val & CXD4960_MASK_LINK_GVIF2RX_LOS) != CXD4960_VALUE_LINK_GVIF2RX_LOS)
			{
				dev_info(&client->dev, "pre_error_DSM_Boot [%x]:%x\n", CXD4960_REG_LINK_STATUS, CXD4960_MASK_LINK_GVIF2RX_LOS);
#ifndef EYE_MAGIN_TEST
				boot_gvif2rx_los_error++;
				dev_info(&client->dev, "pre_error_DSM_Boot [%x]:%x count:%d\n", CXD4960_REG_LINK_STATUS, CXD4960_MASK_LINK_GVIF2RX_LOS, boot_gvif2rx_los_error);
				//Write a pre_error_DSM_Boot bit5 = 1 to System RAM.
				emc_get_exp_info(DSM_START_DUMMY_EXP_NVM, &data);
				data |= 0x20;
				emc_set_exp_info(DSM_START_DUMMY_EXP_NVM, data);
				if(boot_gvif2rx_los_error >=10)
				{
					dev_info(&client->dev, "error_DSM_Boot [%x]:%x\n", CXD4960_REG_LINK_STATUS, CXD4960_MASK_LINK_GVIF2RX_LOS);
					//Write a rror_DSM_Boot bit5 = 1 to System RAM.
					emc_get_exp_info(DSM_START_EXP_NVM, &data);
					data |= 0x20;
					emc_set_exp_info(DSM_START_EXP_NVM, data);
				}
#endif //EYE_MAGIN_TEST
			}
			else{
				dev_info(&client->dev, "No Error [%x]:%x\n", CXD4960_REG_LINK_STATUS, CXD4960_MASK_LINK_GVIF2RX_LOS);
#ifndef EYE_MAGIN_TEST
				//Write a pre_error_DSM_Boot bit5 = 0 to System RAM.
				emc_get_exp_info(DSM_START_DUMMY_EXP_NVM, &data);
				data &= ~0x20;
				emc_set_exp_info(DSM_START_DUMMY_EXP_NVM, data);
#endif //EYE_MAGIN_TEST
			}
		} else {
			cxd4960_dbg(&client->dev, " LINK_STATUS check, target bit is bit4,0: OK[%02X]\n", val);
#ifndef EYE_MAGIN_TEST
				//Write a pre_error_DSM_Boot bit6 = 1,bit5 = 0 to System RAM.
				emc_get_exp_info(DSM_START_DUMMY_EXP_NVM, &data);
				data |= 0x40;
				data &= ~0x20;
				emc_set_exp_info(DSM_START_DUMMY_EXP_NVM, data);
#endif //EYE_MAGIN_TEST
		}
	}

	/* Deserializer register address 0x10 */
	ret = cxd4960_read_reg(cxd4960, CXD4960_REG_ERROR_STATUS, CXD4960_REG_VALUE_08BIT, &val);
	if (ret) {
		cxd4960_dbg(&client->dev, " i2c read ERROR_STATUS: NG[%d]\n", ret);
		retval= ret;
	} else {
		cxd4960_dbg(&client->dev, " i2c read ERROR_STATUS: OK[%d]\n", ret);
		if (((u8)val & CXD4960_MASK_ERROR_STATUS_CHECK) != CXD4960_VALUE_ERROR_STATUS_CHECK) {
			cxd4960_dbg(&client->dev, " ERROR_STATUS check, target bit is bit7,4: NG[%02X]\n", val);
			retval = -EIO;
			if ((val & CXD4960_MASK_ERROR_GVIF2RX_FAIL) != CXD4960_VALUE_ERROR_GVIF2RX_FAIL)
			{
				dev_info(&client->dev, "pre_error_DSM_Boot [%x]:%x\n", CXD4960_REG_ERROR_STATUS, CXD4960_MASK_ERROR_GVIF2RX_FAIL);
#ifndef EYE_MAGIN_TEST
				boot_gvif2rx_fail_error++;
				dev_info(&client->dev, "pre_error_DSM_Boot [%x]:%x count:%d\n", CXD4960_REG_ERROR_STATUS, CXD4960_MASK_ERROR_GVIF2RX_FAIL, boot_gvif2rx_fail_error);
				//Write a pre_error_DSM_Boot bit4 = 1 to System RAM.
				emc_get_exp_info(DSM_START_DUMMY_EXP_NVM, &data);
				data |= 0x10;
				emc_set_exp_info(DSM_START_DUMMY_EXP_NVM, data);
				if(boot_gvif2rx_fail_error >=10)
				{
					dev_info(&client->dev, "error_DSM_Boot [%x]:%x\n", CXD4960_REG_ERROR_STATUS, CXD4960_MASK_ERROR_GVIF2RX_FAIL);
					//Write a error_DSM_Boot bit4 = 1 to System RAM.
					emc_get_exp_info(DSM_START_EXP_NVM, &data);
					data |= 0x10;
					emc_set_exp_info(DSM_START_EXP_NVM, data);
				}
#endif //EYE_MAGIN_TEST
			}
			else{
				dev_info(&client->dev, "No Error [%x]:%x\n", CXD4960_REG_ERROR_STATUS, CXD4960_MASK_ERROR_GVIF2RX_FAIL);
#ifndef EYE_MAGIN_TEST
				//Write a pre_error_DSM_Boot bit4 = 0 to System RAM.
				emc_get_exp_info(DSM_START_DUMMY_EXP_NVM, &data);
				data &= ~0x10;
				emc_set_exp_info(DSM_START_DUMMY_EXP_NVM, data);
#endif //EYE_MAGIN_TEST
			}
			if ((val & CXD4960_MASK_ERROR_VIDEOTX_FAIL) != CXD4960_VALUE_ERROR_VIDEOTX_FAIL)
			{
				dev_info(&client->dev, "pre_error_DSM_Boot [%x]:%x\n", CXD4960_REG_ERROR_STATUS, CXD4960_MASK_ERROR_VIDEOTX_FAIL);
#ifndef EYE_MAGIN_TEST
				boot_videotx_fail++;
				dev_info(&client->dev, "pre_error_DSM_Boot [%x]:%x count:%d\n", CXD4960_REG_ERROR_STATUS, CXD4960_MASK_ERROR_VIDEOTX_FAIL, boot_videotx_fail);
				//Write a pre_error_DSM_Boot bit3 = 1 to System RAM.
				emc_get_exp_info(DSM_START_DUMMY_EXP_NVM, &data);
				data |= 0x08;
				emc_set_exp_info(DSM_START_DUMMY_EXP_NVM, data);
				if(boot_videotx_fail >=10)
				{
					dev_info(&client->dev, "error_DSM_Boot [%x]:%x\n", CXD4960_REG_ERROR_STATUS, CXD4960_MASK_ERROR_VIDEOTX_FAIL);
					//Write a error_DSM_Boot bit3 = 1 to System RAM.
					emc_get_exp_info(DSM_START_EXP_NVM, &data);
					data |= 0x08;
					emc_set_exp_info(DSM_START_EXP_NVM, data);
				}
#endif //EYE_MAGIN_TEST
			}
			else{
				dev_info(&client->dev, "No Error [%x]:%x\n", CXD4960_REG_ERROR_STATUS, CXD4960_MASK_ERROR_VIDEOTX_FAIL);
#ifndef EYE_MAGIN_TEST
				//Write a pre_error_DSM_Boot bit3 = 0 to System RAM.
				emc_get_exp_info(DSM_START_DUMMY_EXP_NVM, &data);
				data &= ~0x08;
				emc_set_exp_info(DSM_START_DUMMY_EXP_NVM, data);
			}
#endif //EYE_MAGIN_TEST
		} else {
			cxd4960_dbg(&client->dev, " ERROR_STATUS check, target bit is bit7,4: OK[%02X]\n", val);
#ifndef EYE_MAGIN_TEST
			//Write a pre_error_DSM_Boot bit4 = 0,bit3 = 0 to System RAM.
			emc_get_exp_info(DSM_START_DUMMY_EXP_NVM, &data);
			data &= ~0x10;
			data &= ~0x08;
			emc_set_exp_info(DSM_START_DUMMY_EXP_NVM, data);
#endif //EYE_MAGIN_TEST
		}
	}

	/* Serializer register check */
	ret = v4l2_subdev_call(cxd4960->remote, video, s_routing, 0, 0, 3);
	if (ret) {
		cxd4960_dbg(&client->dev, " subdev_call video.s_routing: NG[%d]\n", ret);
		retval= ret;
	} else {
		cxd4960_dbg(&client->dev, " subdev_call video.s_routing: OK[%d]\n", ret);
	}

#ifndef EYE_MAGIN_TEST
	emc_get_exp_info(DSM_START_DUMMY_EXP_NVM, &data);
	if(data != PRE_ERROR_BOOT_INIT){

		dev_info(&client->dev, "Des pre error boot\n");

		/* Video Output Disable */
		dev_info(&client->dev, "Video Output Disable\n");
		ret = cxd4960_write_reg(cxd4960, CXD4960_REG_VIDEO_OUTPUT_ENABLE, CXD4960_REG_VALUE_08BIT, CXD4960_VALUE_VIDEO_OUTPUT_DISABLE);
		if (ret)
			return ret;

		/* Deserializer Reset */
		dev_info(&client->dev, "GPIO Des_CE Low\n");
		cxd4960_control_ce(0);

		/* Continue Deserializer Reset */
		emc_get_exp_info(DSM_START_EXP_NVM, &data);
		if(data != ERROR_BOOT_INIT)
		{
			dev_info(&client->dev, "Des error boot\n");
			ret = BOOT_ERROR;
			return ret;
		}

		/* Deserializer Initialize */
		dev_info(&client->dev, "Deserializer Initialize\n");
		cxd4960_reboot_initial_setting(cxd4960);
		ret = PRE_BOOT_ERROR;

	}
#endif //EYE_MAGIN_TEST

	return ret;
}

static int cxd4960_error_gvif2_check(struct cxd4960 *cxd4960)
{
	struct i2c_client *client = v4l2_get_subdevdata(&cxd4960->sd);
	int ret = 0;
	u32 val;
	u16 data;

	/* Deserializer register address 0x01 */
	cxd4960_read_reg(cxd4960, CXD4960_REG_LINK_STATUS, CXD4960_REG_VALUE_08BIT, &val);
	if ((val & CXD4960_MASK_LINK_READY) != CXD4960_VALUE_LINK_READY)
	{
		dev_info(&client->dev, "pre_error_Des_GVIF2 [%x]:%x\n", CXD4960_REG_LINK_STATUS, CXD4960_MASK_LINK_READY);
#ifndef EYE_MAGIN_TEST
		des_ready_error++;
		dev_info(&client->dev, "pre_error_Des_GVIF2 count:%d\n", des_ready_error);
		//Write a pre_error_Des_GVIF2 bit2 = 0 to System RAM.
		emc_get_exp_info(DES_GVIF2_DUMMY_EXP_NVM, &data);
		data &= ~0x04;
		emc_set_exp_info(DES_GVIF2_DUMMY_EXP_NVM, data);
		if(des_ready_error >=10)
		{
			dev_info(&client->dev, "error_Des_GVIF2 [%x]:%x\n", CXD4960_REG_LINK_STATUS, CXD4960_MASK_LINK_READY);
			emc_get_exp_info(DES_GVIF2_EXP_NVM, &data);
			data &= ~0x04;
			emc_set_exp_info(DES_GVIF2_EXP_NVM, data);
		}
#endif //EYE_MAGIN_TEST
	}
	else{
		dev_info(&client->dev, "No Error [%x]:%x\n", CXD4960_REG_LINK_STATUS, CXD4960_MASK_LINK_READY);
		emc_get_exp_info(DES_GVIF2_DUMMY_EXP_NVM, &data);
		data |= 0x04;
		emc_set_exp_info(DES_GVIF2_DUMMY_EXP_NVM, data);
	}

	if ((val & CXD4960_MASK_LINK_GVIF2RX_LOS) != CXD4960_VALUE_LINK_GVIF2RX_LOS)
	{
		dev_info(&client->dev, "pre_error_Des_GVIF2 [%x]:%x\n", CXD4960_REG_LINK_STATUS, CXD4960_MASK_LINK_GVIF2RX_LOS);
#ifndef EYE_MAGIN_TEST
		des_gvif2rx_los_error++;
		dev_info(&client->dev, "pre_error_Des_GVIF2 [%x]:%x count:%d\n", CXD4960_REG_LINK_STATUS, CXD4960_MASK_LINK_GVIF2RX_LOS, des_gvif2rx_los_error);
		//Write a pre_error_Des_GVIF2 bit1 = 1 to System RAM.
		emc_get_exp_info(DES_GVIF2_DUMMY_EXP_NVM, &data);
		data |= 0x02;
		emc_set_exp_info(DES_GVIF2_DUMMY_EXP_NVM, data);
		if(des_gvif2rx_los_error >=10)
		{
			dev_info(&client->dev, "error_Des_GVIF2 [%x]:%x\n", CXD4960_REG_LINK_STATUS, CXD4960_MASK_LINK_GVIF2RX_LOS);
			emc_get_exp_info(DES_GVIF2_EXP_NVM, &data);
			data |= 0x02;
			emc_set_exp_info(DES_GVIF2_EXP_NVM, data);
		}
#endif //EYE_MAGIN_TEST
	}
	else{
		dev_info(&client->dev, "No Error [%x]:%x\n", CXD4960_REG_LINK_STATUS, CXD4960_MASK_LINK_GVIF2RX_LOS);
		emc_get_exp_info(DES_GVIF2_DUMMY_EXP_NVM, &data);
		data &= ~0x02;
		emc_set_exp_info(DES_GVIF2_DUMMY_EXP_NVM, data);
	}

	/* Deserializer register address 0x10 */
	cxd4960_read_reg(cxd4960, CXD4960_REG_ERROR_STATUS, CXD4960_REG_VALUE_08BIT, &val);
	if ((val & CXD4960_MASK_ERROR_GVIF2RX_FAIL) != CXD4960_VALUE_ERROR_GVIF2RX_FAIL)
	{
		dev_info(&client->dev, "pre_error_Des_GVIF2 [%x]:%x\n", CXD4960_REG_LINK_STATUS, CXD4960_MASK_ERROR_GVIF2RX_FAIL);
#ifndef EYE_MAGIN_TEST
		des_gvif2rx_fail_error++;
		dev_info(&client->dev, "pre_error_Des_GVIF2 [%x]:%x count:%d\n", CXD4960_REG_LINK_STATUS, CXD4960_MASK_ERROR_GVIF2RX_FAIL, des_gvif2rx_fail_error);
		//Write a pre_error_Des_GVIF2 bit0 = 1 to System RAM.
		emc_get_exp_info(DES_GVIF2_DUMMY_EXP_NVM, &data);
		data |= 0x01;
		emc_set_exp_info(DES_GVIF2_DUMMY_EXP_NVM, data);
		if(des_gvif2rx_fail_error >=10)
		{
			dev_info(&client->dev, "error_Des_GVIF2 [%x]:%x\n", CXD4960_REG_LINK_STATUS, CXD4960_MASK_ERROR_GVIF2RX_FAIL);
			emc_get_exp_info(DES_GVIF2_EXP_NVM, &data);
			data |= 0x01;
			emc_set_exp_info(DES_GVIF2_EXP_NVM, data);
		}
#endif //EYE_MAGIN_TEST
	}
	else{
		dev_info(&client->dev, "No Error [%x]:%x\n", CXD4960_REG_LINK_STATUS, CXD4960_MASK_ERROR_GVIF2RX_FAIL);
		emc_get_exp_info(DES_GVIF2_DUMMY_EXP_NVM, &data);
		data &= ~0x01;
		emc_set_exp_info(DES_GVIF2_DUMMY_EXP_NVM, data);
	}

#ifndef EYE_MAGIN_TEST
	emc_get_exp_info(DES_GVIF2_DUMMY_EXP_NVM, &data);
	if(data != PRE_ERROR_DES_GVIF_INIT){

		/* Video Output Disable */
		dev_info(&client->dev, "Video Output Disable\n");
		ret = cxd4960_write_reg(cxd4960, CXD4960_REG_VIDEO_OUTPUT_ENABLE, CXD4960_REG_VALUE_08BIT, CXD4960_VALUE_VIDEO_OUTPUT_DISABLE);

		/* Deserializer Reset */
		dev_info(&client->dev, "GPIO Des_CE Low\n");
		cxd4960_control_ce(0);

		/* Continue Deserializer Reset */
		emc_get_exp_info(DES_GVIF2_EXP_NVM, &data);
		if(data != ERROR_DES_GVIF_INIT)
		{
			return ret;
		}

		/* Deserializer Initialize */
		dev_info(&client->dev, "Deserializer Initialize\n");
		cxd4960_reboot_initial_setting(cxd4960);
	}
#endif //EYE_MAGIN_TEST
	return ret;
}

static int cxd4960_error_video_check(struct cxd4960 *cxd4960)
{
	struct i2c_client *client = v4l2_get_subdevdata(&cxd4960->sd);
	int ret = 0;
	u32 val;
	u16 data;

	/* Deserializer register address 0x10 */
	cxd4960_read_reg(cxd4960, CXD4960_REG_ERROR_STATUS, CXD4960_REG_VALUE_08BIT, &val);

	mutex_lock(&cxd4960_csi_err_lock);

	if (((val & CXD4960_MASK_ERROR_VIDEOTX_FAIL) != CXD4960_VALUE_ERROR_VIDEOTX_FAIL) ||
	    (cxd4960_csi_err_notify != 0))
	{
		cxd4960_csi_err_notify = 0;
		mutex_unlock(&cxd4960_csi_err_lock);

		dev_info(&client->dev, "pre_error_Des_Video [%x]:%x\n", CXD4960_REG_LINK_STATUS, CXD4960_MASK_ERROR_VIDEOTX_FAIL);
#ifndef EYE_MAGIN_TEST
		des_output_error++;
		dev_info(&client->dev, "pre_error_Des_Video [%x]:%x count:%d\n", CXD4960_REG_LINK_STATUS, CXD4960_MASK_ERROR_VIDEOTX_FAIL, des_output_error);
		//Write a pre_error_Des_Video bit0 = 1 to System RAM.
		emc_get_exp_info(DES_VIDES_OUTPUT_DUMMY_EXP_NVM, &data);
		data |= 0x01;
		emc_set_exp_info(DES_VIDES_OUTPUT_DUMMY_EXP_NVM, data);
		if(des_output_error >=10)
		{
			dev_info(&client->dev, "error_Des_Video [%x]:%x\n", CXD4960_REG_LINK_STATUS, CXD4960_MASK_ERROR_VIDEOTX_FAIL);
			emc_get_exp_info(DES_VIDES_OUTPUT_EXP_NVM, &data);
			data |= 0x01;
			emc_set_exp_info(DES_VIDES_OUTPUT_EXP_NVM, data);
		}
#endif //EYE_MAGIN_TEST
	}
	else{
		mutex_unlock(&cxd4960_csi_err_lock);
		dev_info(&client->dev, "No Error [%x]:%x\n", CXD4960_REG_LINK_STATUS, CXD4960_MASK_ERROR_VIDEOTX_FAIL);
		emc_get_exp_info(DES_VIDES_OUTPUT_DUMMY_EXP_NVM, &data);
		data &= ~0x01;
		emc_set_exp_info(DES_VIDES_OUTPUT_DUMMY_EXP_NVM, data);
	}

	/* CHeck ECC error and Checksum error */
	/* Check CSI2 ECC Check result and Checksum Check result */

#ifndef EYE_MAGIN_TEST
	emc_get_exp_info(DES_VIDES_OUTPUT_DUMMY_EXP_NVM, &data);
	if(data != PRE_ERROR_DES_VIDES_OUT_INIT)
	{
		/* Video Output Disable */
		dev_info(&client->dev, "Video Output Disable\n");
		ret = cxd4960_write_reg(cxd4960, CXD4960_REG_VIDEO_OUTPUT_ENABLE, CXD4960_REG_VALUE_08BIT, CXD4960_VALUE_VIDEO_OUTPUT_DISABLE);

		/* Deserializer Reset */
		dev_info(&client->dev, "GPIO Des_CE Low\n");
		cxd4960_control_ce(0);

		/* Continue Deserializer Reset */
		emc_get_exp_info(DES_VIDES_OUTPUT_EXP_NVM, &data);
		if(data != ERROR_DES_VIDES_OUT_INIT)
		{
			return ret;
		}

		/* Deserializer Initialize */
		dev_info(&client->dev, "Deserializer Initialize\n");
		cxd4960_reboot_initial_setting(cxd4960);
	}
#endif //EYE_MAGIN_TEST
	return ret;
}

static int cxd4960_error_dmc_ser_check(struct cxd4960 *cxd4960)
{
#ifndef EYE_MAGIN_TEST
	struct i2c_client *client = v4l2_get_subdevdata(&cxd4960->sd);
#endif //EYE_MAGIN_TEST
	int ret = 0;
	u16 data;
	void *mapped;

	ret = v4l2_subdev_call(cxd4960->remote, video, s_routing, 0, 0, 4);

	emc_get_exp_info(DMC_SER_DUMMY_EXP_NVM, &data);

#ifndef EYE_MAGIN_TEST
	if(data != PRE_ERROR_DMC_SER_INIT)
	{
		/* Video Output Disable */
		dev_info(&client->dev, "Video Output Disable\n");
		ret = cxd4960_write_reg(cxd4960, CXD4960_REG_VIDEO_OUTPUT_ENABLE, CXD4960_REG_VALUE_08BIT, CXD4960_VALUE_VIDEO_OUTPUT_DISABLE);

		/* Deserializer Reset */
		dev_info(&client->dev, "GPIO Des_CE Low\n");
		cxd4960_control_ce(0);

		/* Continue Deserializer Reset */
		emc_get_exp_info(DMC_SER_EXP_NVM, &data);
		if(data != ERROR_DMC_SER_INIT)
		{
			return ret;
		}

		/* REFCLK */
		/* set parameter (addr should be aligned by MSIOF_PAGE_SIZE) */

		mapped = ioremap(MSIOF3_BASE, MSIOF_PAGE_SIZE);

		iowrite32(MSIOF_TRMD, mapped + MSIOF_REG_SITMDR1);
		iowrite16(MSIOF_BRPS | MSIOF_BRDV, mapped + MSIOF_REG_SITSCR);
		iowrite32(MSIOF_TSCKE, mapped + MSIOF_REG_SICTR);

		iounmap(mapped);

		/* DSM power OFF/ON */
		cxd4960_dsm_power_control(POWER_OFF);
		cxd4960_dsm_power_control(POWER_ON);
		msleep(25);

		ret = cxd4960_reboot_initial_setting(cxd4960);
	}
#endif //EYE_MAGIN_TEST
	iounmap(mapped);
	return ret;
}

static int cxd4960_error_lvds_i2c_com_check(struct cxd4960 *cxd4960, int result)
{
	struct i2c_client *client = v4l2_get_subdevdata(&cxd4960->sd);
	int ret = 0;
	u16 data;

	/* TBD: Clock Stretch 100ms over is -ETIMEDOUT? */
	if (result == -ENXIO || result == -ETIMEDOUT)
	{
		dev_info(&client->dev, "pre_error_LVDS_I2C\n");
#ifndef EYE_MAGIN_TEST
		lvds_i2c_com_error++;
		dev_info(&client->dev, "pre_error_LVDS_I2C count:%d\n", lvds_i2c_com_error);
		//Write a pre_error_LVDS_I2C to System RAM.
		emc_set_exp_info(LVDS_I2C_COM_DUMMY_EXP_NVM, 1);
		if(lvds_i2c_com_error >= 10)
		{
			dev_info(&client->dev, "error_LVDS_I2C\n");
			emc_set_exp_info(LVDS_I2C_COM_EXP_NVM, 1);
		}
#endif //EYE_MAGIN_TEST
	}
	else if (result >= 0){
		dev_info(&client->dev, "No Error LVDS_I2C\n");
#ifndef EYE_MAGIN_TEST
		emc_set_exp_info(LVDS_I2C_COM_DUMMY_EXP_NVM, 0);
#endif //EYE_MAGIN_TEST
	}

#ifndef EYE_MAGIN_TEST
	emc_get_exp_info(LVDS_I2C_COM_DUMMY_EXP_NVM, &data);
	if(data != PRE_ERROR_LVDS_I2C_INIT)
	{
		/* Video Output Disable */
		dev_info(&client->dev, "Video Output Disable\n");
		ret = cxd4960_write_reg(cxd4960, CXD4960_REG_VIDEO_OUTPUT_ENABLE, CXD4960_REG_VALUE_08BIT, CXD4960_VALUE_VIDEO_OUTPUT_DISABLE);

		/* Deserializer Reset */
		dev_info(&client->dev, "GPIO Des_CE Low\n");
		cxd4960_control_ce(0);

		/* Continue Deserializer Reset */
		emc_get_exp_info(LVDS_I2C_COM_EXP_NVM, &data);
		if(data != ERROR_LVDS_I2C_INIT)
		{
			return ret;
		}

		/* Deserializer Initialize */
		dev_info(&client->dev, "Deserializer Initialize\n");
		cxd4960_reboot_initial_setting(cxd4960);
	}
#endif //EYE_MAGIN_TEST
	return ret;
}

static int cxd4960_strobe_led_control(struct cxd4960 *cxd4960, u32 input, u32 output, u32 config)
{
	int ret;

	ret = v4l2_subdev_call(cxd4960->remote, video, s_routing, input, output, config);

	return ret;
}

static int cxd4960_sscg_control(struct cxd4960 *cxd4960, u32 control)
{
	struct i2c_client *client = v4l2_get_subdevdata(&cxd4960->sd);
	int ret;

	/* Deserializer SSCG ON/OFF Control */
	dev_info(&client->dev, "Deserializer SSCG ON/OFF Control [control:%u]\n", control);
	ret = cxd4960_write_reg(cxd4960, CXD4960_REG_VIDEO_OUTPUT_ENABLE, CXD4960_REG_VALUE_08BIT, CXD4960_VALUE_VIDEO_OUTPUT_DISABLE);
	ret = cxd4960_write_reg(cxd4960, CXD4960_REG_SSCG_CONTROL, CXD4960_REG_VALUE_08BIT, control);
	ret = cxd4960_write_reg(cxd4960, CXD4960_REG_VIDEO_OUTPUT_ENABLE, CXD4960_REG_VALUE_08BIT, CXD4960_VALUE_VIDEO_OUTPUT_ENABLE);
	ret = cxd4960_write_reg(cxd4960, CXD4960_REG_ERROR_CLEAR, CXD4960_REG_VALUE_08BIT, CXD4960_VALUE_ERROR_CLEAR);
	ret = cxd4960_write_reg(cxd4960, CXD4960_REG_ERROR_CLEAR, CXD4960_REG_VALUE_08BIT, CXD4960_VALUE_ERROR_NOTCLEAR);

	return ret;
}

static int cxd4960_error_check_control(struct cxd4960 *cxd4960 ,u32 input, u32 output, u32 config)
{
	int ret = 0;

	ret = cxd4960_error_gvif2_check(cxd4960);
	ret = cxd4960_error_video_check(cxd4960);
	ret = cxd4960_error_dmc_ser_check(cxd4960);

	return ret;
}

static int cxd4960_s_routing(struct v4l2_subdev *sd, u32 input, u32 output, u32 config)
{
	struct cxd4960 *cxd4960 = to_cxd4960(sd);
	struct i2c_client *client = v4l2_get_subdevdata(&cxd4960->sd);
	int ret = 0;

	mutex_lock(&cxd4960->mutex);
	if (!cxd4960->streaming) {
		mutex_unlock(&cxd4960->mutex);
		return 0;
	}

	switch (config) {
	case 1:
		/* Strobe LED Control */
		ret = cxd4960_strobe_led_control(cxd4960, input, output, config);
		break;
	case 2:
		/* SSCG ON/OFF Control */
		ret = cxd4960_sscg_control(cxd4960, input);
		break;
	case 4:
		/* Error Check Control */
		ret = cxd4960_error_check_control(cxd4960, input, output, config);
		break;
	case 5:
		/* Error Status Clear */
		ret = cxd4960_error_status_clear(cxd4960);
		break;
	default:
		dev_err(&client->dev, "Not supported command[%d]\n", config);
		ret = -EINVAL;
		break;
	}

	mutex_unlock(&cxd4960->mutex);

	return ret;
}

static int cxd4960_start_streaming(struct cxd4960 *cxd4960)
{
	struct i2c_client *client = v4l2_get_subdevdata(&cxd4960->sd);
	u32 val;
	int ret;
	int i;

#if 0
	void *mapped;
	/* FSYNC_1R8V */
	/* set parameter (addr should be aligned by PWM_PAGE_SIZE) */
	cxd4960_dbg(&client->dev, "FSYNC Output start\n");
	mapped = ioremap(PWM_BASE, PWM_PAGE_SIZE);

	iowrite32(PWM_CYC0 | PWM_PH0, mapped + PWM_REG_PWMCNT);
	iowrite32(PWM_CC0 | PWM_CCMD | PWM_SYNC | PWM_SS0 | PWM_EN0, mapped + PWM_REG_PWMCR);

	iounmap(mapped);
	cxd4960_dbg(&client->dev, "FSYNC Output end\n");
#endif 

	/* Deserializer Initialize */
	cxd4960_dbg(&client->dev, "Deserializer Initialize start\n");
	ret = cxd4960_write_regs(cxd4960, init_des_set_regs_step1, ARRAY_SIZE(init_des_set_regs_step1));
	if (ret) {
		cxd4960_dbg(&client->dev, " i2c write init_des_set_regs_step1: NG[%d]\n", ret);
		return ret;
	}
	cxd4960_dbg(&client->dev, " i2c write init_des_set_regs_step1: OK[%d]\n", ret);

	i = 0;
	while(i < 10){
		ret = cxd4960_read_reg(cxd4960, CXD4960_REG_SERDES_LINK, CXD4960_REG_VALUE_08BIT, &val);
		if (ret) {
			cxd4960_dbg(&client->dev, " i2c read SERDES_LINK: NG[%d]\n", ret);
			return ret;
		}
		if ((val & CXD4960_VALUE_SERDES_LINK) == CXD4960_VALUE_SERDES_LINK) break;
		i++;
	}
	cxd4960_dbg(&client->dev, " i2c read SERDES_LINK: OK[%d]\n", ret);
	if (i >= 10) {
		cxd4960_dbg(&client->dev, " SERDES_LINK check, target bit is bit4: NG[%02X]\n", val);
		return -EIO;
	} else {
		cxd4960_dbg(&client->dev, " SERDES_LINK check, target bit is bit4: OK[%02X]\n", val);
	}

	ret = cxd4960_write_regs(cxd4960, init_des_set_regs_step2, ARRAY_SIZE(init_des_set_regs_step2));
	if (ret) {
		cxd4960_dbg(&client->dev, " i2c write init_des_set_regs_step2: NG[%d]\n", ret);
		return ret;
	}
	cxd4960_dbg(&client->dev, " i2c write init_des_set_regs_step2: OK[%d]\n", ret);

	i = 0;
	while(i < 10){
		ret = cxd4960_read_reg(cxd4960, CXD4960_REG_REMOTE_COMPLETE, CXD4960_REG_VALUE_08BIT, &val);
		if (ret) {
			cxd4960_dbg(&client->dev, " i2c read REMOTE_COMPLETE: NG[%d]\n", ret);
			return ret;
		}
		if ((val & CXD4960_VALUE_REMOTE_COMPLETE) == CXD4960_VALUE_REMOTE_COMPLETE) break;
		i++;
	}
	cxd4960_dbg(&client->dev, " i2c read REMOTE_COMPLETE: OK[%d]\n", ret);
	if (i >= 10) {
		cxd4960_dbg(&client->dev, " REMOTE_COMPLETE check, target bit is bit0: NG[%02X]\n", val);
		return -EIO;
	} else {
		cxd4960_dbg(&client->dev, " REMOTE_COMPLETE check, target bit is bit0: OK[%02X]\n", val);
	}

	ret = cxd4960_write_regs(cxd4960, init_des_set_regs_step3, ARRAY_SIZE(init_des_set_regs_step3));
	if (ret) {
		cxd4960_dbg(&client->dev, " i2c write init_des_set_regs_step3: NG[%d]\n", ret);
		return ret;
	}
	cxd4960_dbg(&client->dev, " i2c write init_des_set_regs_step3: OK[%d]\n", ret);

	usleep_range(30, 40);

	ret = cxd4960_write_regs(cxd4960, init_des_set_regs_step4, ARRAY_SIZE(init_des_set_regs_step4));
	if (ret) {
		cxd4960_dbg(&client->dev, " i2c write init_des_set_regs_step4: NG[%d]\n", ret);
		return ret;
	}
	cxd4960_dbg(&client->dev, " i2c write init_des_set_regs_step4: OK[%d]\n", ret);
	cxd4960_dbg(&client->dev, "Deserializer Initialize end\n");

	cxd4960_dbg(&client->dev, "DMC Initialize start\n");
	cxd4960_dbg(&client->dev, " subdev_call video.s_stream\n");
	ret = v4l2_subdev_call(cxd4960->remote, video, s_stream, 1);
	if (ret) {
		cxd4960_dbg(&client->dev, " subdev_call video.s_stream: NG[%d]\n", ret);
		return ret;
	}
	cxd4960_dbg(&client->dev, " subdev_call video.s_stream: OK[%d]\n", ret);

	/* Deserializer Video Output Enable */
	cxd4960_dbg(&client->dev, "Deserializer Video Output Enable start\n");
	ret = cxd4960_write_reg(cxd4960, CXD4960_REG_VIDEO_OUTPUT_ENABLE, CXD4960_REG_VALUE_08BIT, CXD4960_VALUE_VIDEO_OUTPUT_ENABLE);
	if (ret) {
		cxd4960_dbg(&client->dev, " i2c write VIDEO_OUTPUT_ENABLE: NG[%d]\n", ret);
		return ret;
	}
	cxd4960_dbg(&client->dev, " i2c write VIDEO_OUTPUT_ENABLE: OK[%d]\n", ret);
	cxd4960_dbg(&client->dev, "Deserializer Video Output Enable end\n");

	/* Deserializer Error Status Clear */
	cxd4960_dbg(&client->dev, "Deserializer Error Status Clear start\n");
	ret = cxd4960_write_reg(cxd4960, CXD4960_REG_ERROR_CLEAR, CXD4960_REG_VALUE_08BIT, CXD4960_VALUE_ERROR_CLEAR);
	if (ret) {
		cxd4960_dbg(&client->dev, " i2c write ERROR_CLEAR CLEAR: NG[%d]\n", ret);
		return ret;
	}
	cxd4960_dbg(&client->dev, " i2c write ERROR_CLEAR CLEAR: OK[%d]\n", ret);

	ret = cxd4960_write_reg(cxd4960, CXD4960_REG_ERROR_CLEAR, CXD4960_REG_VALUE_08BIT, CXD4960_VALUE_ERROR_NOTCLEAR);
	if (ret) {
		cxd4960_dbg(&client->dev, " i2c write ERROR_CLEAR NOTCLEAR: NG[%d]\n", ret);
		return ret;
	}
	cxd4960_dbg(&client->dev, " i2c write ERROR_CLEAR NOTCLEAR: OK[%d]\n", ret);
	cxd4960_dbg(&client->dev, "Deserializer Error Status Clear end\n");

	/* Startup check */
	cxd4960_dbg(&client->dev, "Startup Check start\n");
	ret = cxd4960_error_boot_check(cxd4960);
	cxd4960_dbg(&client->dev, "Startup Check end\n");

	return ret;
}

static void cxd4960_stop_streaming(struct cxd4960 *cxd4960)
{
	struct i2c_client *client = v4l2_get_subdevdata(&cxd4960->sd);

	__v4l2_ctrl_grab(cxd4960->vflip, false);
	__v4l2_ctrl_grab(cxd4960->hflip, false);

	pm_runtime_put(&client->dev);
}

static int cxd4960_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct cxd4960 *cxd4960 = to_cxd4960(sd);
	int ret = 0;

	mutex_lock(&cxd4960->mutex);
	if (cxd4960->streaming == enable) {
		mutex_unlock(&cxd4960->mutex);
		return 0;
	}

	if (enable) {
		/*
		 * Apply default & customized values
		 * and then start streaming.
		 */
		ret = cxd4960_start_streaming(cxd4960);
		if (ret)
			goto err_unlock;
	} else {
		cxd4960_stop_streaming(cxd4960);
	}

	cxd4960->streaming = enable;

	mutex_unlock(&cxd4960->mutex);

	return ret;

err_unlock:
	mutex_unlock(&cxd4960->mutex);

	return ret;
}

static int __maybe_unused cxd4960_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct cxd4960 *cxd4960 = to_cxd4960(sd);

	if (cxd4960->streaming)
		cxd4960_stop_streaming(cxd4960);

	return 0;
}

static int __maybe_unused cxd4960_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct cxd4960 *cxd4960 = to_cxd4960(sd);
	int ret;

	if (cxd4960->streaming) {
		ret = cxd4960_start_streaming(cxd4960);
		if (ret)
			goto error;
	}

	return 0;

error:
	cxd4960_stop_streaming(cxd4960);
	cxd4960->streaming = false;

	return ret;
}

static const struct v4l2_subdev_core_ops cxd4960_core_ops = {
	.subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_video_ops cxd4960_video_ops = {
	.s_stream = cxd4960_set_stream,
	.s_routing = cxd4960_s_routing,
};

static const struct v4l2_subdev_ops cxd4960_subdev_ops = {
	.core = &cxd4960_core_ops,
	.video = &cxd4960_video_ops,
};

static void cxd4960_free_controls(struct cxd4960 *cxd4960)
{
	mutex_destroy(&cxd4960->mutex);
}

static int cxd4960_check_hwcfg(struct device *dev)
{
	struct fwnode_handle *endpoint;
	struct v4l2_fwnode_endpoint ep_cfg = {
		.bus_type = V4L2_MBUS_CSI2_DPHY
	};
	int ret = -EINVAL;

	for (endpoint = fwnode_graph_get_next_endpoint(dev_fwnode(dev), NULL);
		endpoint != NULL;
		endpoint = fwnode_graph_get_next_endpoint(dev_fwnode(dev), endpoint)) {
		if (!endpoint) {
			dev_err(dev, "endpoint node not found\n");
			return -EINVAL;
		}

		if (v4l2_fwnode_endpoint_alloc_parse(endpoint, &ep_cfg)) {
			dev_err(dev, "could not parse endpoint\n");
			goto error_out;
		}

		/* Check the number of MIPI CSI2 data lanes */
		if (ep_cfg.bus.mipi_csi2.num_data_lanes != 2) {
			/* check next endpoint */
			continue;
		} else {
			ret = 0;
		}
	}

	if (ret)
		dev_err(dev, "only 2 data lanes are currently supported\n");

error_out:
	v4l2_fwnode_endpoint_free(&ep_cfg);
	fwnode_handle_put(endpoint);

	return ret;
}

static int cxd4960_notify_bound(struct v4l2_async_notifier *notifier,
			      struct v4l2_subdev *subdev,
			      struct v4l2_async_subdev *asd)
{
	struct cxd4960 *priv = notifier_to_cxd4960(notifier);
	struct i2c_client *client = v4l2_get_subdevdata(&priv->sd);

	int pad;

	pad = media_entity_get_fwnode_pad(&subdev->entity, asd->match.fwnode,
					  MEDIA_PAD_FL_SOURCE);
	if (pad < 0) {
		dev_err(&client->dev, "Failed to find pad for %s\n", subdev->name);
		return pad;
	}

	priv->remote = subdev;
	priv->remote_pad = pad;

	dev_dbg(&client->dev, "Bound %s pad: %d\n", subdev->name, pad);

	return media_create_pad_link(&subdev->entity, pad,
				     &priv->sd.entity, CXD4960_PAD_SINK,
				     MEDIA_LNK_FL_ENABLED |
				     MEDIA_LNK_FL_IMMUTABLE);
}

static void cxd4960_notify_unbind(struct v4l2_async_notifier *notifier,
				struct v4l2_subdev *subdev,
				struct v4l2_async_subdev *asd)
{
	struct cxd4960 *priv = notifier_to_cxd4960(notifier);
	struct i2c_client *client = v4l2_get_subdevdata(&priv->sd);

	priv->remote = NULL;

	dev_dbg(&client->dev, "Unbind %s\n", subdev->name);
}

static const struct v4l2_async_notifier_operations cxd4960_notify_ops = {
	.bound = cxd4960_notify_bound,
	.unbind = cxd4960_notify_unbind,
};

static int cxd4960_parse(struct cxd4960 *priv)
{
	struct v4l2_async_subdev *asd;
	struct fwnode_handle *fwnode;
	struct device_node *ep;
	struct v4l2_fwnode_endpoint v4l2_ep = { .bus_type = 0 };
	struct i2c_client *client = v4l2_get_subdevdata(&priv->sd);

	int ret;

	ep = of_graph_get_endpoint_by_regs(client->dev.of_node, 0, -1);
	if (!ep) {
		dev_dbg(&client->dev, "Not connected to subdevice\n");
		return 0;
	}

	ret = v4l2_fwnode_endpoint_parse(of_fwnode_handle(ep), &v4l2_ep);
	if (ret) {
		dev_err(&client->dev, "Could not parse v4l2 endpoint\n");
		of_node_put(ep);
		return -EINVAL;
	}

	fwnode = fwnode_graph_get_remote_endpoint(of_fwnode_handle(ep));
	of_node_put(ep);

	dev_dbg(&client->dev, "Found '%pOF'\n", to_of_node(fwnode));

	v4l2_async_notifier_init(&priv->notifier);
	priv->notifier.ops = &cxd4960_notify_ops;

	asd = v4l2_async_notifier_add_fwnode_subdev(&priv->notifier, fwnode,
						    sizeof(*asd));
	fwnode_handle_put(fwnode);
	if (IS_ERR(asd))
		return PTR_ERR(asd);

	ret = v4l2_async_subdev_notifier_register(&priv->sd,
						  &priv->notifier);
	if (ret)
		v4l2_async_notifier_cleanup(&priv->notifier);

	return ret;
}

static int cxd4960_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct cxd4960 *cxd4960;
	int ret;
	int flg;
	int i;
	u16 data;
	u32 gpioreg;
	struct v4l2_subdev *sd;

	void *mapped;

	mutex_init(&cxd4960_csi_err_lock);
	mutex_lock(&cxd4960_csi_err_lock);
	cxd4960_csi_err_notify = 0;
	mutex_unlock(&cxd4960_csi_err_lock);

	cxd4960 = devm_kzalloc(&client->dev, sizeof(*cxd4960), GFP_KERNEL);
	if (!cxd4960)
		return -ENOMEM;

	sd = &cxd4960->sd;

	cxd4960->sd.owner = THIS_MODULE;
	cxd4960->sd.dev = dev;
	v4l2_i2c_subdev_init(&cxd4960->sd, client, &cxd4960_subdev_ops);

	/* Check the hardware configuration in device tree */
	if (cxd4960_check_hwcfg(dev))
		return -EINVAL;

	ret = cxd4960_parse(cxd4960);
	if (ret)
		return ret;

	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;

	cxd4960->pad[CXD4960_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	cxd4960->pad[CXD4960_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_ATV_DECODER;
	ret = media_entity_pads_init(&sd->entity, CXD4960_PAD_MAX, cxd4960->pad);
	if (ret)
		return ret;

	ret = v4l2_async_register_subdev(sd);
	if (ret < 0) {
		dev_err(dev, "Failed to register subdevice.\n");
		return ret;
	}

	/* GPIO setting DES_CE */
	/* set parameter (addr should be aligned by GPIO_PAGE_SIZE) */
	dev_info(dev, "GPIO Setting DES_CE\n");
	mapped = ioremap(GPIO01_BASE, GPIO_PAGE_SIZE);

	gpioreg = ioread32(mapped + GPIO1_REG_POC);
	gpioreg &= ~GPIO1_POC_CE;
	iowrite32(~gpioreg, mapped + GPIO1_REG_PMMR);
	iowrite32(gpioreg, mapped + GPIO1_REG_POC);
	gpioreg = ioread32(mapped + GPIO1_REG_PUEN);
	gpioreg &= ~GPIO1_PUEN_CE;
	iowrite32(~gpioreg, mapped + GPIO1_REG_PMMR);
	iowrite32(gpioreg, mapped + GPIO1_REG_PUEN);

	iounmap(mapped);

	/* Check TPS78412Vout_state of SystemRAM to determine Des power supply startup */
	dev_info(dev, "Check TPS78412Vout_state\n");
	i = 0;
	while(i < 100)
	{
		emc_get_exp_info(TPS78412_VOUT_STATUS_NVM, &data);
		if(data == 0)
		{
			dev_info(dev, "Des power supply completed\n");
			flg = POWER_ON;
			break;
		}
		flg = POWER_OFF;
		i++;
		msleep(1);
	}
	if(flg == POWER_OFF)
	{
		dev_info(dev, "NG:Des power no supply\n");
	}

	/* Check DSM_POWER_ENABLE in SystemRAM to determine DSM power startup */
	dev_info(dev, "Check DSM_POWER_ENABLE\n");
	i = 0;
	while(i < 100)
	{
		emc_get_exp_info(DSM_POWER_ENABLE, &data);
		if(data == 1)
		{
			dev_info(dev, "DSM power supply completed\n");
			break;
		}
		i++;
		
		if(i==100){
			dev_info(dev, "Not supply DSM Power !!!!\n");
		}
		msleep(1);

	}
	if(flg == POWER_OFF)
	{
		dev_info(dev, "NG:DSM power no supply\n");
	}
	msleep(25);

	/* Request optional enable pin */
#if 0
	cxd4960->reset_gpio = devm_gpiod_get_optional(dev, NULL,
						     GPIOD_OUT_LOW);
	if (!cxd4960->reset_gpio)
		return -ENOENT;
#endif

	ret = cxd4960_power_on(dev);
	if (ret)
		return ret;
	cxd4960_dbg(&client->dev, "DES_CE Set High\n");

	/* FCM image sensor initialization (transition to Streaming mode) completion check */
	emc_get_exp_info(ASYNC_IMAGE_SENSOR_DSM, &data);
	i = 0;
	while(i < 10){
		if(data == FCM_STREAMING){
			dev_info(dev, "FCM Streaming Mode\n");
			flg = OK_FCM;
		}
		flg = NG_FCM;
		i++;
	}
	if(flg == NG_FCM){
		dev_info(dev, "NG:FCM Streaming Mode\n");
	}

	/* REFCLK */
	/* set by msiof driver */
#if 1
	/* set parameter (addr should be aligned by MSIOF_PAGE_SIZE) */
	cxd4960_dbg(&client->dev, "REFCLK Output start\n");
	mapped = ioremap(MSIOF3_BASE, MSIOF_PAGE_SIZE);

	iowrite32(MSIOF_TRMD, mapped + MSIOF_REG_SITMDR1);
	iowrite16(MSIOF_BRPS | MSIOF_BRDV, mapped + MSIOF_REG_SITSCR);
	iowrite32(MSIOF_TSCKIZ | MSIOF_TSCKE, mapped + MSIOF_REG_SICTR);

	iounmap(mapped);
	cxd4960_dbg(&client->dev, "REFCLK Output end\n");
#endif

	msleep(1);

	dev_info(dev, "probed.\n");

	return 0;

};

static int cxd4960_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct cxd4960 *cxd4960 = to_cxd4960(sd);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	cxd4960_free_controls(cxd4960);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		cxd4960_power_off(&client->dev);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

static const struct of_device_id cxd4960_dt_ids[] = {
	{ .compatible = "sony,cxd4960" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, cxd4960_dt_ids);

static const struct dev_pm_ops cxd4960_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(cxd4960_suspend, cxd4960_resume)
	SET_RUNTIME_PM_OPS(cxd4960_power_off, cxd4960_power_on, NULL)
};

static struct i2c_driver cxd4960_i2c_driver = {
	.driver = {
		.name = "cxd4960",
		.of_match_table	= cxd4960_dt_ids,
		.pm = &cxd4960_pm_ops,
	},
	.probe_new = cxd4960_probe,
	.remove = cxd4960_remove,
};

module_i2c_driver(cxd4960_i2c_driver);

MODULE_AUTHOR("Dave Stevenson <dave.stevenson@raspberrypi.com");
MODULE_DESCRIPTION("Sony CXD4960 sensor driver");
MODULE_LICENSE("GPL v2");
