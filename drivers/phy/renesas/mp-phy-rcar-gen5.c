// SPDX-License-Identifier: GPL-2.0-only
/*
 * Renesas Multi-Protocol PHY device driver
 *
 * Copyright (C) 2025 Renesas Electronics Corporation
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/types.h>
#include <linux/pm_domain.h>

#define MPPHY_NUM_CHANNELS	4

/* Common registers */
#define MPPHY_CMNCNT1        0x80000
#define MPPHY_CMNCNT2        0x80004
#define MPPHY_PCS0REG1       0x85000
#define MPPHY_PCS0REG5       0x85010

/* Channel register base and offsets */
#define MPPHY_CHAN_BASE(ch)	(0x81000 + (ch) * 0x1000)
#define MPPHY_PXTEST_OFFSET	0x00C
#define MPPHY_RXCNT_OFFSET	0x038
#define MPPHY_SRAMCNT_OFFSET	0x040
#define MPPHY_REFCLK_OFFSET	0x014
#define MPPHY_CNTXT1_OFFSET	0x004
#define MPPHY_CNTXT2_OFFSET	0x008
#define MPPHY_TXREQ_OFFSET	0x044
#define MPPHY_RXREQ1_OFFSET	0x024

/* Channel specific registers */
#define MPPHY_PXTEST(ch)	(MPPHY_CHAN_BASE(ch) + MPPHY_PXTEST_OFFSET)
#define MPPHY_PXRXCNT(ch)	(MPPHY_CHAN_BASE(ch) + MPPHY_RXCNT_OFFSET)
#define MPPHY_PXSRAMCNT(ch)	(MPPHY_CHAN_BASE(ch) + MPPHY_SRAMCNT_OFFSET)
#define MPPHY_PXREFCLK(ch)	(MPPHY_CHAN_BASE(ch) + MPPHY_REFCLK_OFFSET)
#define MPPHY_PXCNTXT1(ch)	(MPPHY_CHAN_BASE(ch) + MPPHY_CNTXT1_OFFSET)
#define MPPHY_PXCNTXT2(ch)	(MPPHY_CHAN_BASE(ch) + MPPHY_CNTXT2_OFFSET)
#define MPPHY_PXTXREQ(ch)	(MPPHY_CHAN_BASE(ch) + MPPHY_TXREQ_OFFSET)
#define MPPHY_PXRXREQ1(ch)	(MPPHY_CHAN_BASE(ch) + MPPHY_RXREQ1_OFFSET)

/* Channel enable bit masks for MPPHY_CMNCNT1 register */
#define MPPHY_CMNCNT1_CH_MASK(ch)    (0xFF << ((ch) * 8))

/* Channel enable bits for MPPHY_CMNCNT1 register */
#define MPPHY_CMNCNT1_ETH_EN(ch)      (BIT((ch) * 8))
#define MPPHY_CMNCNT1_USB_EN(ch) \
			((ch) == 2 ? (BIT(16) | BIT(17)) : \
			(ch) == 3 ? (BIT(24) | BIT(25)) : -1)
#define MPPHY_CMNCNT1_PCIE_EN(ch)      (0x0 << ((ch) * 8))

/* PCS0REG5 register mask and values for each channel */
#define MPPHY_PCS0REG5_CH(ch)        (0x03 << (24 + (ch) * 2))

/* PCS0REG1 register bits */
#define MPPHY_PCS0REG1_VAL         0x00010000

/* PXTEST register bit */
#define MPPHY_PXTEST_BIT            0x1

/* PXRXCNT register reset value */
#define MPPHY_PXRXCNT_RESET_VAL     0x202

/* PXSRAMCNT register bits */
#define MPPHY_PXSRAMCNT_BYPASS      BIT(0)
#define MPPHY_PXSRAMCNT_BIT3        BIT(3)
#define BOOTLOAD_BYPASS_MODE	    0x3
#define SRAM_BYPASS_MODE	    0xC
#define SRAM_EXT_LD_DONE	    0x10
#define SRAM_INIT_DONE		    0x20

#define SRAM_CONTROL_SET_BIT	(BOOTLOAD_BYPASS_MODE | SRAM_BYPASS_MODE | \
				 SRAM_EXT_LD_DONE | SRAM_INIT_DONE)

/*-------------------------------------- TCA define -------------------------------*/
/* TCA (Type-C Adapter) Register Offsets within MP-PHY base */
#define MPPHY_USB_BASE(ch)			(0x90000 + (ch) * 0x10000)

/* Channel specific registers */
#define TCA_INTR_OFFSET(ch)			(MPPHY_USB_BASE(ch) + 0x4)
#define TCA_INTR_STS_OFFSET(ch)			(MPPHY_USB_BASE(ch) + 0x8)
#define TCA_TCPC_OFFSET(ch)			(MPPHY_USB_BASE(ch) + 0x14)
#define TCA_VBUS_CTRL_OFFSET(ch)		(MPPHY_USB_BASE(ch) + 0x0040)
#define PSTATE_1_OFFSET(ch)			(MPPHY_USB_BASE(ch) + 0x0054)

#define VBUS_VALID_OVERRD			0x2
/*---------------------------------------------------------------------------------*/

#define PHY_MODE_USB(mode) \
	((mode) == PHY_MODE_USB_HOST ? PHY_MODE_USB_HOST : \
	(mode) == PHY_MODE_USB_DEVICE ? PHY_MODE_USB_DEVICE : \
	(mode) == PHY_MODE_USB_OTG ? PHY_MODE_USB_OTG : -1)

/* PXREFCLK register value */
#define MPPHY_PXREFCLK_VAL		0x35
#define MPPHY_PXREFCLK_VAL_ETH		0x55

/* PXTXREQ register value */
#define MPPHY_PXTXREQ_VAL           0x8

/* Context settings */
#define MPPHY_CNTXT1_VALUE	0x02010002
#define MPPHY_CNTXT2_VALUE	0x02020202  /* For channels 1-3 */
#define MPPHY_CNTXT2_CH0_VALUE	0x02020201  /* Special for channel 0 */
#define MPPHY_TXREQ_VALUE	0x8

#define HIGH_SPEED		0
#define SUPER_SPEED_PLUS	1

/* Firmware update */
#define MPPHY_FW_BASE		0x10000
#define MPPHY_FW_CH_OFFSET	0x20000
#define MPPHY_FW_NAME		"rcar_gen5_mp_phy.bin"

struct mp_phy_chan_priv {
	unsigned int channel_id;
	unsigned int lane_id;
	unsigned int protocol_id;
	unsigned int num_lanes;
	bool initialized;
	enum phy_mode current_protocol;
	int speed;
};

struct mpphy_pwr_dev {
	int no_pwr_devs;
	struct device **devs;
};

struct mp_phy_priv {
	void __iomem *base;
	struct device *dev;
#define NUM_OF_MPPHY_RST 5
	struct reset_control *resets[NUM_OF_MPPHY_RST];
	struct clk_bulk_data *clks;
	int num_clks;
	const struct firmware *fw;
	struct mp_phy_chan_priv chan[MPPHY_NUM_CHANNELS];
	struct mpphy_pwr_dev pwr_devs;
};

static void mp_phy_write(void __iomem *base, u32 offset, u32 value)
{
	writel(value, base + offset);
}

static void mp_phy_update_bits(void __iomem *base, u32 offset, u32 mask, u32 value)
{
	u32 tmp;

	tmp = readl(base + offset);
	tmp = (tmp & ~mask) | (value & mask);
	writel(tmp, base + offset);
}

static int mp_phy_reg_wait(void __iomem *base, u32 offs, u32 mask, u32 expected)
{
	u32 val;

	if (!base) {
		pr_err("mpphy_reg_wait: Invalid address\n");
		return -EINVAL;
	}

	int ret = readl_poll_timeout_atomic(base + offs, val, (val & mask) == expected,
										1, 10000000);

	if (ret) {
		pr_err("%s: Timeout waiting addr: 0x%p, offset: 0x%x, mask: 0x%x, exp: 0x%x\n",
		       __func__, base, offs, mask, expected);
	} else {
		pr_debug("%s: Success addr: 0x%p, offset: 0x%x, val: 0x%x\n",
			 __func__, base, offs, val);
	}

	return ret;
}

static void mp_phy_update_firmware(struct mp_phy_priv *priv, u32 channel_id)
{
	u32 offset = MPPHY_FW_BASE + MPPHY_FW_CH_OFFSET * channel_id;
	u16 data;
	int i;

	for (i = 0; i < priv->fw->size; i += 2) {
		data = priv->fw->data[i];
		data |= priv->fw->data[i + 1] << 8;
		writew(data, priv->base + offset + i);
	}
}

static int mp_phy_exit(struct phy *phy)
{
	struct mp_phy_priv *priv = phy_get_drvdata(phy);
	struct mp_phy_chan_priv *chan = &priv->chan[phy->id];
	u32 channel_id = phy->id;

	if (!chan->initialized)
		return 0;

	chan->initialized = false;
	chan->current_protocol = PHY_MODE_INVALID;

	if (channel_id < priv->pwr_devs.no_pwr_devs &&
	    priv->pwr_devs.devs[channel_id])
		pm_runtime_put_sync(priv->pwr_devs.devs[channel_id]);

	return 0;
}

static int mp_phy_init_ethernet(struct mp_phy_priv *priv, u32 channel_id)
{
	mp_phy_update_firmware(priv, channel_id);

	mp_phy_write(priv->base, MPPHY_PXRXCNT(channel_id), MPPHY_PXRXCNT_RESET_VAL);
	mp_phy_update_bits(priv->base, MPPHY_PXREFCLK(channel_id), MPPHY_PXREFCLK_VAL_ETH,
			   MPPHY_PXREFCLK_VAL_ETH);
	mp_phy_update_bits(priv->base, MPPHY_PXRXCNT(channel_id), (BIT(9) | BIT(1)), 0);
	mp_phy_update_bits(priv->base, MPPHY_PXTXREQ(channel_id), (BIT(19) | BIT(3)),
			   (BIT(19) | BIT(3)));

	return 0;
}

static int mp_phy_init_pcie4(struct mp_phy_priv *priv, u32 channel_id)
{
	struct mp_phy_chan_priv *chan = &priv->chan[channel_id];
	u32 link_width;

	link_width = chan->num_lanes;

	dev_info(priv->dev, "PCIe4 PHY initialization on channel %d, link_width: %d\n",
		 channel_id, link_width);

	switch (link_width) {
	case 1:
	case 2:
		if (channel_id == 0) {
			mp_phy_update_bits(priv->base, MPPHY_PXCNTXT1(0), 0x2010002, 0x2010002);
			mp_phy_update_bits(priv->base, MPPHY_PXCNTXT2(0), 0x2020201, 0x2020201);
			mp_phy_update_bits(priv->base, MPPHY_PXTXREQ(0), 0x80004, 0x80004);
			mp_phy_update_bits(priv->base, MPPHY_PXREFCLK(0), 0x30, 0x30);
			mp_phy_update_bits(priv->base, MPPHY_PXREFCLK(0), 0x4, 0x4);
			mp_phy_update_bits(priv->base, MPPHY_PXREFCLK(0), 0x1, 0x1);
			mp_phy_update_bits(priv->base, MPPHY_PXRXCNT(0), 0x202, 0x202);
			mp_phy_update_bits(priv->base, MPPHY_PXRXCNT(0), 0x202, 0x0);
		}
		if (channel_id == 1) {
			mp_phy_update_bits(priv->base, MPPHY_PXCNTXT1(2), 0x2010002, 0x2010002);
			mp_phy_update_bits(priv->base, MPPHY_PXCNTXT2(2), 0x2020202, 0x2020202);
			mp_phy_update_bits(priv->base, MPPHY_PXTXREQ(2), 0x8, 0x8);
			mp_phy_update_bits(priv->base, MPPHY_PXREFCLK(2), 0x30, 0x30);
			mp_phy_update_bits(priv->base, MPPHY_PXREFCLK(2), 0x4, 0x4);
			mp_phy_update_bits(priv->base, MPPHY_PXREFCLK(2), 0x1, 0x1);
			mp_phy_update_bits(priv->base, MPPHY_PXRXCNT(2), 0x202, 0x202);
			mp_phy_update_bits(priv->base, MPPHY_PXRXCNT(2), 0x202, 0x0);
		}
		break;
	case 4:
		if (channel_id == 0) {
			mp_phy_update_bits(priv->base, MPPHY_PXCNTXT1(0), 0x2010002, 0x2010002);
			mp_phy_update_bits(priv->base, MPPHY_PXCNTXT2(0), 0x2020201, 0x2020201);
			mp_phy_update_bits(priv->base, MPPHY_PXTXREQ(0), 0x8, 0x8);
			mp_phy_update_bits(priv->base, MPPHY_PXCNTXT1(1), 0x2010002, 0x2010002);
			mp_phy_update_bits(priv->base, MPPHY_PXCNTXT2(1), 0x2020201, 0x2020202);
			mp_phy_update_bits(priv->base, MPPHY_PXTXREQ(1), 0x8, 0x8);
			mp_phy_update_bits(priv->base, MPPHY_PXREFCLK(0), 0x30, 0x30);
			mp_phy_update_bits(priv->base, MPPHY_PXREFCLK(0), 0x4, 0x4);
			mp_phy_update_bits(priv->base, MPPHY_PXREFCLK(0), 0x1, 0x1);
			mp_phy_update_bits(priv->base, MPPHY_PXREFCLK(1), 0x30, 0x30);
			mp_phy_update_bits(priv->base, MPPHY_PXREFCLK(1), 0x4, 0x4);
			mp_phy_update_bits(priv->base, MPPHY_PXREFCLK(1), 0x1, 0x1);
			mp_phy_update_bits(priv->base, MPPHY_PXRXCNT(0), 0x202, 0x202);
			mp_phy_update_bits(priv->base, MPPHY_PXRXCNT(1), 0x202, 0x202);
			mp_phy_update_bits(priv->base, MPPHY_PXRXCNT(0), 0x202, 0x0);
			mp_phy_update_bits(priv->base, MPPHY_PXRXCNT(1), 0x202, 0x0);
		}
		if (channel_id == 1) {
			mp_phy_update_bits(priv->base, MPPHY_PXCNTXT1(2), 0x2010002, 0x2010002);
			mp_phy_update_bits(priv->base, MPPHY_PXCNTXT2(2), 0x2020202, 0x2020202);
			mp_phy_update_bits(priv->base, MPPHY_PXTXREQ(2), 0x8, 0x8);
			mp_phy_update_bits(priv->base, MPPHY_PXCNTXT1(3), 0x2010002, 0x2010002);
			mp_phy_update_bits(priv->base, MPPHY_PXCNTXT2(3), 0x2020202, 0x2020202);
			mp_phy_update_bits(priv->base, MPPHY_PXTXREQ(3), 0x8, 0x8);
			mp_phy_update_bits(priv->base, MPPHY_PXREFCLK(2), 0x30, 0x30);
			mp_phy_update_bits(priv->base, MPPHY_PXREFCLK(2), 0x4, 0x4);
			mp_phy_update_bits(priv->base, MPPHY_PXREFCLK(2), 0x1, 0x1);
			mp_phy_update_bits(priv->base, MPPHY_PXREFCLK(3), 0x30, 0x30);
			mp_phy_update_bits(priv->base, MPPHY_PXREFCLK(3), 0x4, 0x4);
			mp_phy_update_bits(priv->base, MPPHY_PXREFCLK(3), 0x1, 0x1);

			mp_phy_update_bits(priv->base, MPPHY_PXRXCNT(2), 0x202, 0x202);
			mp_phy_update_bits(priv->base, MPPHY_PXRXCNT(3), 0x202, 0x202);
			mp_phy_update_bits(priv->base, MPPHY_PXRXCNT(2), 0x202, 0x0);
			mp_phy_update_bits(priv->base, MPPHY_PXRXCNT(3), 0x202, 0x0);
		}
		break;
	case 8:
		mp_phy_write(priv->base, MPPHY_PXCNTXT1(0), 0x2010002);
		mp_phy_write(priv->base, MPPHY_PXCNTXT2(0), 0x2020201);
		mp_phy_write(priv->base, MPPHY_PXTXREQ(0), 0x8);
		mp_phy_write(priv->base, MPPHY_PXCNTXT1(1), 0x2010002);
		mp_phy_write(priv->base, MPPHY_PXCNTXT2(1), 0x2020202);
		mp_phy_write(priv->base, MPPHY_PXTXREQ(1), 0x8);
		mp_phy_write(priv->base, MPPHY_PXCNTXT1(2), 0x2010002);
		mp_phy_write(priv->base, MPPHY_PXCNTXT2(2), 0x2020202);
		mp_phy_write(priv->base, MPPHY_PXTXREQ(2), 0x8);
		mp_phy_write(priv->base, MPPHY_PXCNTXT1(3), 0x2010002);
		mp_phy_write(priv->base, MPPHY_PXCNTXT2(3), 0x2020202);
		mp_phy_write(priv->base, MPPHY_PXTXREQ(3), 0x8);

		mp_phy_update_bits(priv->base, MPPHY_PXRXCNT(0), 0x202, 0x202);
		mp_phy_update_bits(priv->base, MPPHY_PXRXCNT(1), 0x202, 0x202);
		mp_phy_update_bits(priv->base, MPPHY_PXRXCNT(2), 0x202, 0x202);
		mp_phy_update_bits(priv->base, MPPHY_PXRXCNT(3), 0x202, 0x202);

		mp_phy_update_bits(priv->base, MPPHY_PXRXCNT(0), 0x202, 0x202);
		mp_phy_update_bits(priv->base, MPPHY_PXRXCNT(1), 0x202, 0x202);
		mp_phy_update_bits(priv->base, MPPHY_PXRXCNT(2), 0x202, 0x202);
		mp_phy_update_bits(priv->base, MPPHY_PXRXCNT(3), 0x202, 0x202);

		mp_phy_update_bits(priv->base, MPPHY_PXRXCNT(0), 0x202, 0x0);
		mp_phy_update_bits(priv->base, MPPHY_PXRXCNT(1), 0x202, 0x0);
		mp_phy_update_bits(priv->base, MPPHY_PXRXCNT(2), 0x202, 0x0);
		mp_phy_update_bits(priv->base, MPPHY_PXRXCNT(3), 0x202, 0x0);

		break;
	}
	return 0;
}

static int mp_phy_init_usb(struct mp_phy_priv *priv, u32 channel_id)
{
	u32 data;

	dev_info(priv->dev, "USB PHY initialization requested on channel %d\n", channel_id);
	data = mp_phy_reg_wait(priv->base, MPPHY_PXSRAMCNT(channel_id), 0x00000020, 0x00000020);
	if (data) {
		dev_err(priv->dev, "Timeout waiting for MPPHY_PXSRAMCNT(%d) configuration\n",
			channel_id);
		return data;
	}
	mp_phy_update_firmware(priv, channel_id);
	mp_phy_update_bits(priv->base, MPPHY_PXSRAMCNT(channel_id), SRAM_EXT_LD_DONE,
			   SRAM_EXT_LD_DONE);
	data = mp_phy_reg_wait(priv->base, MPPHY_PXRXREQ1(channel_id), 0x00000002, 0x00000000);
	if (data) {
		dev_err(priv->dev, "Timeout waiting for MPPHY_PXRXREQ1(%d) configuration\n",
			channel_id);
		return data;
	}

	return 0;
}

static int mp_phy_init(struct phy *phy)
{
	struct mp_phy_priv *priv = phy_get_drvdata(phy);
	struct mp_phy_chan_priv *chan = &priv->chan[phy->id];
	u32 channel_id = phy->id;
	u32 protocol_id = phy->attrs.mode;
	int ret = 0;

	/*
	 * Note: Current source code support for Ethernet, PCIe
	 * initialization is based on the bare metal code shared by the board team.
	 * USB initialization is not implemented yet.
	 */

	if (channel_id > 3) {
		dev_err(priv->dev, "Invalid channel ID: %d\n", channel_id);
		return -EINVAL;
	}

	if (channel_id < priv->pwr_devs.no_pwr_devs &&
	    priv->pwr_devs.devs[channel_id]) {
		ret = pm_runtime_get_sync(priv->pwr_devs.devs[channel_id]);
		if (ret < 0) {
			dev_err(priv->dev, "Failed to power on domain for channel %d: %d\n",
				channel_id, ret);
			return ret;
		}
	}

	/* Check if initialized with same protocol then skip */
	if (chan->initialized && chan->current_protocol == protocol_id) {
		dev_info(priv->dev, "ch%d already initialized (protocol %d), skip settings\n",
			 channel_id, protocol_id);
		return 0;
	}

	switch (protocol_id) {
	case PHY_MODE_PCIE:
		ret = mp_phy_init_pcie4(priv, channel_id);
		break;
	case PHY_MODE_ETHERNET:
		ret = mp_phy_init_ethernet(priv, channel_id);
		break;
	case PHY_MODE_USB_HOST:
	case PHY_MODE_USB_DEVICE:
	case PHY_MODE_USB_OTG:
		ret = mp_phy_init_usb(priv, channel_id);
		break;
	}

	if (!ret) {
		chan->initialized = true;
		chan->current_protocol = protocol_id;
		dev_info(priv->dev, "Channel %d successfully initialized for protocol %d\n",
			 channel_id, protocol_id);
	}

	return ret;
}

static int mp_phy_late_init(struct phy *phy)
{
	struct mp_phy_priv *priv = phy_get_drvdata(phy);
	struct mp_phy_chan_priv *chan = &priv->chan[phy->id];
	u32 data;
	u32 channel_id = phy->id;
	u32 link_width;

	if (!chan->initialized) {
		dev_err(priv->dev, "Channel %d not initialized\n", channel_id);
		return -EINVAL;
	}

	/*
	 * The datasheet describes initialization procedure without full
	 * information about the registers. Therefore, the source code is based
	 * on the bare metal code shared by the board team.
	 */
	if (chan->protocol_id == PHY_MODE_PCIE) {
		link_width = chan->num_lanes ? chan->num_lanes : 4; /* Default to 4 lanes */

		switch (link_width) {
		case 1:
		case 2:
			data = mp_phy_reg_wait(priv->base, MPPHY_PXSRAMCNT(channel_id),
					       0x00000020, 0x00000020);
			if (data) {
				dev_err(priv->dev, "Timeout waiting for MPPHY_PXSRAMCNT(%d)\n",
					channel_id);
			}

			data = mp_phy_reg_wait(priv->base, MPPHY_PXRXREQ1(channel_id),
					       0x00000002, 0x00000000);
			if (data) {
				dev_err(priv->dev, "Timeout waiting for MPPHY_PXRXREQ1(%d)\n",
					channel_id);
			}
			break;
		case 4:
			if (channel_id == 0) {
				data = mp_phy_reg_wait(priv->base, MPPHY_PXSRAMCNT(0),
						       0x00000020, 0x00000020);
				if (data)
					dev_err(priv->dev, "Timeout waiting for PXSRAMCNT(0)\n");
				data = mp_phy_reg_wait(priv->base, MPPHY_PXSRAMCNT(1),
						       0x00000020, 0x00000020);
				if (data)
					dev_err(priv->dev, "Timeout waiting for PXSRAMCNT(1)\n");
				data = mp_phy_reg_wait(priv->base, MPPHY_PXRXREQ1(0),
						       0x00000002, 0x00000000);
				if (data)
					dev_err(priv->dev, "Timeout waiting for PXRXREQ(0)\n");
				data = mp_phy_reg_wait(priv->base, MPPHY_PXRXREQ1(1),
						       0x00000002, 0x00000000);
				if (data)
					dev_err(priv->dev, "Timeout waiting for PXRXREQ(1)\n");
			}

			if (channel_id == 1) {
				data = mp_phy_reg_wait(priv->base, MPPHY_PXSRAMCNT(2),
						       0x00000020, 0x00000020);
				if (data)
					dev_err(priv->dev, "Timeout waiting for PXSRAMCNT(2)\n");
				data = mp_phy_reg_wait(priv->base, MPPHY_PXSRAMCNT(3),
						       0x00000020, 0x00000020);
				if (data)
					dev_err(priv->dev, "Timeout waiting for PXSRAMCNT(3)\n");
				data = mp_phy_reg_wait(priv->base, MPPHY_PXRXREQ1(2),
						       0x00000002, 0x00000000);
				if (data)
					dev_err(priv->dev, "Timeout waiting for PXRXREQ(2)\n");
				data = mp_phy_reg_wait(priv->base, MPPHY_PXRXREQ1(3),
						       0x00000002, 0x00000000);
				if (data)
					dev_err(priv->dev, "Timeout waiting for PXRXREQ(3)\n");
			}
			break;
		case 8:
			data = mp_phy_reg_wait(priv->base, MPPHY_PXSRAMCNT(0),
					       0x00000020, 0x00000020);
			if (data)
				dev_err(priv->dev, "Timeout waiting for MPPHY_PXSRAMCNT(0)\n");
			data = mp_phy_reg_wait(priv->base, MPPHY_PXSRAMCNT(1),
					       0x00000020, 0x00000020);
			if (data)
				dev_err(priv->dev, "Timeout waiting for MPPHY_PXSRAMCNT(1)\n");
			data = mp_phy_reg_wait(priv->base, MPPHY_PXSRAMCNT(2),
					       0x00000020, 0x00000020);
			if (data)
				dev_err(priv->dev, "Timeout waiting for MPPHY_PXSRAMCNT(2)\n");
			data = mp_phy_reg_wait(priv->base, MPPHY_PXSRAMCNT(3),
					       0x00000020, 0x00000020);
			if (data)
				dev_err(priv->dev, "Timeout waiting for MPPHY_PXSRAMCNT(3)\n");
			data = mp_phy_reg_wait(priv->base, MPPHY_PXRXREQ1(0),
					       0x00000002, 0x00000000);
			if (data)
				dev_err(priv->dev, "Timeout waiting for MPPHY_PXRXREQ(0)\n");
			data = mp_phy_reg_wait(priv->base, MPPHY_PXRXREQ1(1),
					       0x00000002, 0x00000000);
			if (data)
				dev_err(priv->dev, "Timeout waiting for MPPHY_PXRXREQ(1)\n");
			data = mp_phy_reg_wait(priv->base, MPPHY_PXRXREQ1(2),
					       0x00000002, 0x00000000);
			if (data)
				dev_err(priv->dev, "Timeout waiting for MPPHY_PXRXREQ(2)\n");
			data = mp_phy_reg_wait(priv->base, MPPHY_PXRXREQ1(3),
					       0x00000002, 0x00000000);
			if (data)
				dev_err(priv->dev, "Timeout waiting for MPPHY_PXRXREQ(3)\n");
			break;
		}
	} else if (chan->protocol_id == PHY_MODE_ETHERNET) {
		mp_phy_update_bits(priv->base, MPPHY_PXSRAMCNT(channel_id),
				   SRAM_EXT_LD_DONE, SRAM_EXT_LD_DONE);
		data = mp_phy_reg_wait(priv->base, MPPHY_PXRXREQ1(channel_id),
				       0x00000002, 0x00000000);
		if (data) {
			dev_err(priv->dev, "Timeout waiting for MPPHY_PXRXREQ1(%d)\n", channel_id);
			return data;
		}
	}
	return 0;
}

static int mp_phy_set_mode(struct phy *phy, enum phy_mode mode, int submode)
{
	struct mp_phy_priv *priv = phy_get_drvdata(phy);
	struct mp_phy_chan_priv *chan = &priv->chan[phy->id];
	enum phy_mode new_protocol;

	switch (mode) {
	case PHY_MODE_PCIE:
		new_protocol = PHY_MODE_PCIE;
		break;
	case PHY_MODE_ETHERNET:
		new_protocol = PHY_MODE_ETHERNET;
		break;
	case PHY_MODE_USB_HOST:
	case PHY_MODE_USB_DEVICE:
	case PHY_MODE_USB_OTG:
		new_protocol = PHY_MODE_USB(mode);
		break;
	default:
		dev_err(&phy->dev, "Unsupported PHY mode: %d\n", mode);
		return -EOPNOTSUPP;
	}

	if (chan->initialized && chan->current_protocol != new_protocol) {
		if (new_protocol != PHY_MODE_USB_HOST && new_protocol != PHY_MODE_USB_DEVICE &&
		    new_protocol != PHY_MODE_USB_OTG) {
			dev_err(&phy->dev, "Protocol conflict on channel %d: current=%d, requested=%d\n",
				phy->id, chan->current_protocol, new_protocol);
			return -EINVAL;
		}
	}

	if (chan->initialized) {
		/* Skip if same protocol */
		if (chan->current_protocol == new_protocol) {
			dev_info(&phy->dev, "Channel %d already configured for protocol %d\n",
				 phy->id, new_protocol);
			return 0;
		}

		bool is_current_usb = (chan->current_protocol == PHY_MODE_USB_HOST ||
					chan->current_protocol == PHY_MODE_USB_DEVICE ||
					chan->current_protocol == PHY_MODE_USB_OTG);
		bool is_new_usb = (new_protocol == PHY_MODE_USB_HOST ||
				new_protocol == PHY_MODE_USB_DEVICE ||
				new_protocol == PHY_MODE_USB_OTG);

		if (is_current_usb && is_new_usb) {
			dev_info(&phy->dev, "Channel %d switching USB mode from %d to %d, skip reconfiguration\n",
				 phy->id, chan->current_protocol, new_protocol);
			return 0;
		}
	}

	chan->current_protocol = new_protocol;
	chan->protocol_id = new_protocol;

	return 0;
}

static int mp_phy_config_usb(struct phy *phy, int speed)
{
	struct mp_phy_priv *priv = phy_get_drvdata(phy);
	struct mp_phy_chan_priv *chan = &priv->chan[phy->id];
	u32 channel_id = phy->id;
	u32 data;

	switch (speed) {
	case HIGH_SPEED:
		mp_phy_write(priv->base, TCA_VBUS_CTRL_OFFSET(chan->lane_id), 0x0000003E);
		break;
	case SUPER_SPEED_PLUS:
		data = mp_phy_reg_wait(priv->base, PSTATE_1_OFFSET(chan->lane_id),
				       0x00000003, 0x00000003);
		if (data) {
			dev_err(priv->dev, "Timeout waiting for PSTATE_1_OFFSET(%d)\n",
				chan->lane_id);
			return data;
		}

		mp_phy_update_bits(priv->base, TCA_INTR_OFFSET(chan->lane_id), 0x3, 0x3);

		mp_phy_update_bits(priv->base, TCA_TCPC_OFFSET(chan->lane_id), 0x10, 0x10);
		data = mp_phy_reg_wait(priv->base, TCA_INTR_STS_OFFSET(chan->lane_id),
				       0x00000001, 0x00000001);
		if (data) {
			dev_err(priv->dev, "Timeout waiting for TCA_INTR_STS_OFFSET(%d)\n",
				chan->lane_id);
			return data;
		}

		mp_phy_update_bits(priv->base, TCA_INTR_STS_OFFSET(chan->lane_id), 0x1503, 0x1503);
		mp_phy_update_bits(priv->base, TCA_TCPC_OFFSET(chan->lane_id), 0x11, 0x11);
		data = mp_phy_reg_wait(priv->base, TCA_INTR_STS_OFFSET(chan->lane_id),
				       0x00000001, 0x00000001);
		if (data) {
			dev_err(priv->dev, "Timeout waiting for TCA_INTR_STS_OFFSET(%d)\n",
				chan->lane_id);
			return data;
		}
		mp_phy_update_bits(priv->base, TCA_INTR_STS_OFFSET(channel_id), 0x1503, 0x1503);
		break;
	}

	return 0;
}

static const struct phy_ops mp_phy_ops = {
	.init		= mp_phy_init,
	.exit		= mp_phy_exit,
	.power_on	= mp_phy_late_init,
	.set_mode	= mp_phy_set_mode,
	.set_speed	= mp_phy_config_usb,
	.owner		= THIS_MODULE,
};

static struct phy *mp_phy_xlate(struct device *dev, const struct of_phandle_args *args)
{
	struct mp_phy_priv *priv = dev_get_drvdata(dev);
	struct mp_phy_chan_priv *chan;
	struct phy *phy;
	struct device_node *pcie_np;
	unsigned int num_lanes;

	if (args->args_count > 2) {
		dev_err(dev, "Invalid args_count: %d\n", args->args_count);
		return ERR_PTR(-EINVAL);
	}

	phy = devm_phy_create(dev, NULL, &mp_phy_ops);
	if (IS_ERR(phy)) {
		dev_err(dev, "Failed to create PHY\n");
		return phy;
	}

	if (args->args_count >= 1)
		phy->id = args->args[0];
	else
		phy->id = 0;

	chan = &priv->chan[phy->id];
	chan->channel_id = phy->id;

	pcie_np = of_find_compatible_node(NULL, NULL, "renesas,rcar-gen5-pcie");
	if (pcie_np) {
		if (of_property_read_u32(pcie_np, "num-lanes", &num_lanes))
			dev_info(dev, "num-lanes not found in PCIe DT node, default 4");
		of_node_put(pcie_np);
	} else {
		dev_warn(dev, "PCIe DT node not found");
	}

	chan->num_lanes = num_lanes;

	if (args->args_count >= 2)
		chan->lane_id = args->args[1];
	else
		chan->lane_id = 0;

	phy_set_drvdata(phy, priv);

	return phy;
}

static int mp_phy_pre_init(struct mp_phy_priv *priv)
{
	const char *interface_names[MPPHY_NUM_CHANNELS];
	bool write_cntxt1[MPPHY_NUM_CHANNELS] = { 0 };
	u32 ref_use_pad, ref_repeat_clk_en;
	u32 sramcnt[MPPHY_NUM_CHANNELS];
	u32 cmncnt2 = 0x33330000;	/* All res_{ack,req}_in_sel are 1 */
	u32 cmncnt1 = 0;
	int num, i, ret;

	num = of_property_read_string_array(priv->dev->of_node,
					    "renesas,interface-names",
					    interface_names,
					    ARRAY_SIZE(interface_names));
	if (num < 0)
		return -EINVAL;

	/* These properties are bitfiled : bit0 = MP-PHY channel 0 */
	ret = of_property_read_u32(priv->dev->of_node, "renesas,ref_use_pad",
				   &ref_use_pad);
	if (ret < 0)
		return ret;
	ret = of_property_read_u32(priv->dev->of_node, "renesas,ref_repeat_clk_en",
				   &ref_repeat_clk_en);
	if (ret < 0)
		return ret;

	for (i = 0; i < MPPHY_NUM_CHANNELS; i++) {
		if (!strcmp(interface_names[i], "pci.0")) {
			sramcnt[i] = 0x0f;
		} else if (!strcmp(interface_names[i], "pci.1")) {
			sramcnt[i] = 0x0f;
			cmncnt1 |= 0x02 << (i * 8);
		} else if (!strcmp(interface_names[i], "ethernet")) {
			sramcnt[i] = 0x00;
			write_cntxt1[i] = true;
			cmncnt1 |= 0x01 << (i * 8);
		} else if (!strcmp(interface_names[i], "usb")) {
			sramcnt[i] = 0x09;
			cmncnt1 |= 0x03 << (i * 8);
		} else {
			return -EINVAL;
		}
		if (ref_use_pad & BIT(i))
			cmncnt2 |= 1 << (i * 4);
		if (ref_repeat_clk_en & BIT(i))
			cmncnt2 |= 2 << (i * 4);
	}

	mp_phy_update_bits(priv->base, MPPHY_CMNCNT1, cmncnt1, cmncnt1);
	mp_phy_write(priv->base, MPPHY_CMNCNT2, cmncnt2);

	for (i = 0; i < MPPHY_NUM_CHANNELS; i++) {
		mp_phy_update_bits(priv->base, MPPHY_PXTEST(i),
				   MPPHY_PXTEST_BIT, MPPHY_PXTEST_BIT);
		mp_phy_write(priv->base, MPPHY_PXSRAMCNT(i), sramcnt[i]);

		if (write_cntxt1[i]) {
			mp_phy_write(priv->base, MPPHY_CHAN_BASE(i) + 0x10c, 0x000ff0ff);
			mp_phy_write(priv->base, MPPHY_PXCNTXT1(i), 0x00180023);
		}
		mp_phy_update_bits(priv->base, MPPHY_PXTEST(i), MPPHY_PXTEST_BIT, 0x0);
	}

	mp_phy_update_bits(priv->base, MPPHY_PCS0REG1, MPPHY_PCS0REG1_VAL, 0x0);
	mp_phy_update_bits(priv->base, MPPHY_PCS0REG5, 0xff000000, 0x0);

	return 0;
}

static int mp_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mp_phy_priv *priv;
	struct phy_provider *provider;
	struct resource *res;
	int ret, i;
	static u8 rst_control_get_retries = 5;
	struct device_node *np = dev->of_node;
	int num_power_domains;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;

	ret = request_firmware(&priv->fw, MPPHY_FW_NAME, dev);
	if (ret < 0)
		return ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "Invalid resource\n");
		return -EINVAL;
	}

	priv->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->base)) {
		dev_err(dev, "Failed to map PHY registers\n");
		return PTR_ERR(priv->base);
	}

	for (i = 0; i < MPPHY_NUM_CHANNELS; i++) {
		priv->chan[i].initialized = false;
		priv->chan[i].current_protocol = PHY_MODE_INVALID;
		priv->chan[i].protocol_id = PHY_MODE_INVALID;
	}

	for (i = 0; i < NUM_OF_MPPHY_RST - 1; ++i) {
		char rst_name[16] = {0};

		sprintf(rst_name, "mpphy%d1", i);
		priv->resets[i] = devm_reset_control_get(dev, rst_name);
		if (IS_ERR(priv->resets[i])) {
			dev_dbg(dev, "Failed to get reset control mpphy%d1, retries: %d\n",
				i, rst_control_get_retries);
			if (rst_control_get_retries) {
				--rst_control_get_retries;
				return -EPROBE_DEFER;
			}
			rst_control_get_retries = 0;
			return PTR_ERR(priv->resets[0]);
		}
	}
	priv->resets[NUM_OF_MPPHY_RST - 1] = devm_reset_control_get(dev, "mpphy02");
	if (IS_ERR(priv->resets[NUM_OF_MPPHY_RST - 1])) {
		dev_err(dev, "Failed to get reset control mpphy02\n");
		return PTR_ERR(priv->resets[NUM_OF_MPPHY_RST - 1]);
	}

	priv->num_clks = devm_clk_bulk_get_all(dev, &priv->clks);
	if (priv->num_clks < 1) {
		dev_err(dev, "Failed to get mp_phy clocks\n");
		return -ENODEV;
	}

	num_power_domains = of_count_phandle_with_args(np, "power-domains", "#power-domain-cells");
	if (num_power_domains < 0) {
		dev_err(dev, "Failed to get number of power domains (%d)\n", num_power_domains);
		return num_power_domains;
	}

	priv->pwr_devs.no_pwr_devs = num_power_domains;
	priv->pwr_devs.devs = devm_kcalloc(dev, num_power_domains,
					   sizeof(*priv->pwr_devs.devs), GFP_KERNEL);

	if (!priv->pwr_devs.devs) {
		dev_err(dev, "Failed to allocate power domain devices\n");
		return -ENOMEM;
	}

	for (int i = 0; i < num_power_domains; i++) {
		priv->pwr_devs.devs[i] = dev_pm_domain_attach_by_id(dev, i);
		if (IS_ERR(priv->pwr_devs.devs[i])) {
			dev_err(dev, "Failed to attach power domain index %d\n", i);
			for (int j = 0; j < i; j++) {
				if (priv->pwr_devs.devs[j] &&
				    !IS_ERR(priv->pwr_devs.devs[j])) {
					pm_runtime_put_sync(priv->pwr_devs.devs[j]);
					dev_pm_domain_detach(priv->pwr_devs.devs[j], true);
				}
			}
			return PTR_ERR(priv->pwr_devs.devs[i]);
		}
		ret = pm_runtime_get_sync(priv->pwr_devs.devs[i]);
		if (ret < 0) {
			dev_err(dev, "Failed to power on domain %d: %d\n", i, ret);
			dev_pm_domain_detach(priv->pwr_devs.devs[i], true);
			priv->pwr_devs.devs[i] = NULL;
			for (int j = 0; j < i; j++) {
				pm_runtime_put_sync(priv->pwr_devs.devs[j]);
				dev_pm_domain_detach(priv->pwr_devs.devs[j], true);
			}
			return ret;
		}
	}

	for (i = 0; i < NUM_OF_MPPHY_RST - 1; ++i) {
		ret = reset_control_assert(priv->resets[i]);
		if (ret) {
			dev_err(dev, "Failed to assert mpphy%d1", i);
			return ret;
		}
	}
	ret = reset_control_assert(priv->resets[NUM_OF_MPPHY_RST - 1]);
	if (ret) {
		dev_err(dev, "Failed to assert mpphy02");
		return ret;
	}

	for (i = 0; i < NUM_OF_MPPHY_RST - 1; ++i) {
		ret = reset_control_deassert(priv->resets[i]);
		if (ret) {
			dev_err(dev, "Failed to deassert mpphy%d1", i);
			return ret;
		}
	}
	ret = reset_control_deassert(priv->resets[NUM_OF_MPPHY_RST - 1]);
	if (ret) {
		dev_err(dev, "Failed to deassert mpphy02");
		return ret;
	}

	ret = clk_bulk_prepare_enable(priv->num_clks, priv->clks);
	if (ret) {
		dev_err(dev, "Failed to enable bulk clocks: %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, priv);

	provider = devm_of_phy_provider_register(dev, mp_phy_xlate);
	if (IS_ERR(provider)) {
		dev_err(dev, "Failed to register PHY provider\n");
		return PTR_ERR(provider);
	}

	ret = mp_phy_pre_init(priv);
	if (ret < 0)
		return 0;

	dev_info(dev, "Multi-Protocol PHY driver probed successfully\n");
	return 0;
}

static void mp_phy_remove(struct platform_device *pdev)
{
	struct mp_phy_priv *priv = dev_get_drvdata(&pdev->dev);
	int i;

	clk_bulk_disable_unprepare(priv->num_clks, priv->clks);

	if (priv->fw) {
		release_firmware(priv->fw);
		priv->fw = NULL;
	}

	for (i = priv->pwr_devs.no_pwr_devs - 1; i >= 0; i--) {
		if (priv->pwr_devs.devs[i]) {
			pm_runtime_put_sync(priv->pwr_devs.devs[i]);
			dev_pm_domain_detach(priv->pwr_devs.devs[i], true);
			priv->pwr_devs.devs[i] = NULL;
		}
	}

	pm_runtime_put(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	platform_set_drvdata(pdev, NULL);
}

static int mp_phy_suspend_noirq(struct device *dev)
{
	struct mp_phy_priv *priv = dev_get_drvdata(dev);
	int i;

	for (i = 0; i < MPPHY_NUM_CHANNELS; i++) {
		struct mp_phy_chan_priv *chan = &priv->chan[i];

		if (chan->initialized) {
			dev_dbg(dev, "Channel %d will need re-init (was protocol: %d)\n",
				i, chan->current_protocol);
			chan->initialized = false;
		}
	}

	if (priv->fw) {
		release_firmware(priv->fw);
		priv->fw = NULL;
		dev_dbg(dev, "Firmware released during suspend\n");
	}

	clk_bulk_disable_unprepare(priv->num_clks, priv->clks);

	for (int i = 0; i < priv->pwr_devs.no_pwr_devs; i++) {
		if (priv->pwr_devs.devs[i])
			pm_runtime_put_sync(priv->pwr_devs.devs[i]);
	}

	dev_info(dev, "Multi-Protocol PHY suspended\n");

	return 0;
}

static int mp_phy_resume_noirq(struct device *dev)
{
	struct mp_phy_priv *priv = dev_get_drvdata(dev);
	int ret;

	ret = request_firmware(&priv->fw, MPPHY_FW_NAME, dev);
	if (ret < 0) {
		dev_err(dev, "Failed to request firmware during resume: %d\n", ret);
		return ret;
	}
	dev_dbg(dev, "Firmware loaded during resume\n");

	for (int i = 0; i < NUM_OF_MPPHY_RST; ++i) {
		ret = reset_control_assert(priv->resets[i]);
		if (ret) {
			dev_err(dev, "Failed to assert mpphy %d", i);
			return ret;
		}
	}
	for (int i = 0; i < NUM_OF_MPPHY_RST; ++i) {
		ret = reset_control_deassert(priv->resets[i]);
		if (ret) {
			dev_err(dev, "Failed to deassert mpphy %d", i);
			return ret;
		}
	}

	ret = clk_bulk_prepare_enable(priv->num_clks, priv->clks);
	if (ret) {
		dev_err(dev, "Failed to enable bulk clocks: %d\n", ret);
		return ret;
	}

	for (int i = 0; i < priv->pwr_devs.no_pwr_devs; i++) {
		if (priv->pwr_devs.devs[i]) {
			ret = pm_runtime_get_sync(priv->pwr_devs.devs[i]);
			if (ret < 0) {
				dev_err(dev, "Failed to resume domain %d: %d\n", i, ret);
				return ret;
			}
		}
	}

	ret = mp_phy_pre_init(priv);
	if (ret < 0) {
		dev_err(dev, "Failed to pre-init during resume: %d\n", ret);
		return ret;
	}

	dev_info(dev, "Multi-Protocol PHY resumed\n");

	return 0;
}

static const struct dev_pm_ops mp_phy_pm_ops = {
	.suspend_noirq = mp_phy_suspend_noirq,
	.resume_noirq = mp_phy_resume_noirq,
};

static const struct of_device_id mp_phy_of_match[] = {
	{ .compatible = "renesas,multi-protocol-phy" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mp_phy_of_match);

static struct platform_driver mp_phy_driver = {
	.probe	= mp_phy_probe,
	.remove	= mp_phy_remove,
	.driver = {
		.name		= "renesas,multi-protocol-phy",
		.of_match_table	= mp_phy_of_match,
		.pm = &mp_phy_pm_ops,
	},
};

module_platform_driver(mp_phy_driver);

MODULE_AUTHOR("Thanh Quan");
MODULE_DESCRIPTION("Renesas Multi-Protocol PHY driver");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE(MPPHY_FW_NAME);
