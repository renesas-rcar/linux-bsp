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
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

/* Hardcoded for enable module clock */
#define MDLC_BASE		0xc9c90000
#define MPPHY_PDID		(0)
#define MPPHY_CLK_MASK(n)	GENMASK((n) + 1, n)
#define MPPHY_CLK_SHIFT(n)	(n)

#define MDLC_PKCPROT0		(MDLC_BASE + 0x0cf0)
#define MDLC_PKCPROT1		(MDLC_BASE + 0x0cf4)

#define _MDLC_MPDG(k)		(MDLC_BASE + 0x0200 + (k) * 4)
#define _MDLC_MPDGS(k)		(MDLC_BASE + 0x0300 + (k) * 4)
#define MDLC_MPIER0		(MDLC_BASE + 0x0110)
#define MDLC_MPIMR0		(MDLC_BASE + 0x0120)

#define MDLC_MPDG		_MDLC_MPDG(MPPHY_PDID)
#define MDLC_MPDGS		_MDLC_MPDGS(MPPHY_PDID)

#define MDLC_MSRES(i)		(MDLC_BASE + 0x0900 + (i) * 4)
#define MDLC_MSRESS(i)	(	MDLC_BASE + 0x0960 + (i) * 4)

static void mp_phy_module_power_gating_set(u8 pdid, u8 mode)
{
	void __iomem *unlock = ioremap(MDLC_PKCPROT0, 4);
	void __iomem *mpdg = ioremap(_MDLC_MPDG(pdid), 4);
	void __iomem *mpdgs = ioremap(_MDLC_MPDGS(pdid), 4);
	void __iomem *mpier0 = ioremap(MDLC_MPIER0, 4);
	void __iomem *mpimr0 = ioremap(MDLC_MPIMR0, 4);

	writel(0xA5A5A501, unlock);

	if ((readl(mpdgs) & 0x3) == mode)
			goto unmap;

	while (readl(mpdgs) != readl(mpdg))
			udelay(1000);

	writel(0, mpier0);
	writel(0x1, mpimr0);

	writel(0x1, mpdg);

	while (readl(mpdgs) != readl(mpdg))
			udelay(1000);

	writel(mode, mpdg);

	while (readl(mpdgs) != readl(mpdg))
			udelay(1000);

unmap:
	iounmap(unlock);
	iounmap(mpdg);
	iounmap(mpdgs);
	iounmap(mpier0);
	iounmap(mpimr0);
}

static void mp_phy_module_standy_set(u8 clk_reg_no, u8 pos, u8 mode)
{
	void __iomem *unlock = ioremap(MDLC_PKCPROT1, 4);
	void __iomem *msress = ioremap(MDLC_MSRESS(clk_reg_no), 4);
	void __iomem *msres = ioremap(MDLC_MSRES(clk_reg_no), 4);
	u32 val;

	writel(0xA5A5A501, unlock);

	if ((readl(msress) & MPPHY_CLK_MASK(pos)) == (mode << MPPHY_CLK_SHIFT(pos)))
			goto unmap;

	while ((readl(msress) & MPPHY_CLK_MASK(pos)) != (readl(msres) & MPPHY_CLK_MASK(pos)))
			udelay(1000);

	val = readl(msres);
	val &= ~MPPHY_CLK_MASK(pos);
	val |= mode << MPPHY_CLK_SHIFT(pos);
	writel(val, msres);

	while ((readl(msress) & MPPHY_CLK_MASK(pos)) != (readl(msres) & MPPHY_CLK_MASK(pos)))
			udelay(1000);

unmap:
	iounmap(unlock);
	iounmap(msress);
	iounmap(msres);
}

static void mp_phy_module_power_reset(void)
{
	mp_phy_module_power_gating_set(3, 0x03);
	mp_phy_module_power_gating_set(4, 0x03);
	mp_phy_module_power_gating_set(5, 0x03);
	mp_phy_module_power_gating_set(6, 0x03);

	mp_phy_module_standy_set(6, 8, 0x01);
	mp_phy_module_standy_set(6, 10, 0x01);
	mp_phy_module_standy_set(6, 12, 0x01);
	mp_phy_module_standy_set(6, 14, 0x01);
	mp_phy_module_standy_set(6, 16, 0x01);
}

static void mp_phy_module_power_run(void)
{
	mp_phy_module_power_gating_set(3, 0x03);
	mp_phy_module_power_gating_set(4, 0x03);
	mp_phy_module_power_gating_set(5, 0x03);
	mp_phy_module_power_gating_set(6, 0x03);

	mp_phy_module_standy_set(6, 8, 0x03);
	mp_phy_module_standy_set(6, 10, 0x03);
	mp_phy_module_standy_set(6, 12, 0x03);
	mp_phy_module_standy_set(6, 14, 0x03);
	mp_phy_module_standy_set(6, 16, 0x03);
}
//--------------------------------------------------

#define MPPHY_NUM_CHANNELS	3

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
#define MPPHY_CMNCNT1_CH_EN(ch)      ((ch) == 0 ? BIT(1) : BIT((ch) * 8))

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

/* CMNCNT1/2 clock settings */
#define MPPHY_CMNCNT2_CLK_CH(ch)     (0x30003 << ((ch) * 4))

/* PXREFCLK register value */
#define MPPHY_PXREFCLK_VAL          0x35

/* PXTXREQ register value */
#define MPPHY_PXTXREQ_VAL           0x8

/* Context settings */
#define MPPHY_CNTXT1_VALUE	0x02010002
#define MPPHY_CNTXT2_VALUE	0x02020202  /* For channels 1-3 */
#define MPPHY_CNTXT2_CH0_VALUE	0x02020201  /* Special for channel 0 */
#define MPPHY_TXREQ_VALUE	0x8


struct mp_phy_chan_priv {
	unsigned int channel_id;
	unsigned int lane_id;
};

struct mp_phy_priv {
	void __iomem *base;
	struct device *dev;
	struct reset_control *reset;
	struct clk *clk;
	struct mp_phy_chan_priv chan[MPPHY_NUM_CHANNELS];
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

static int mp_phy_init_ethernet(struct mp_phy_priv *priv, u32 channel_id)
{

	u32 cntxt2_val;

	cntxt2_val = (channel_id == 0) ? MPPHY_CNTXT2_CH0_VALUE : MPPHY_CNTXT2_VALUE;

	mp_phy_update_bits(priv->base, MPPHY_CMNCNT1, MPPHY_CMNCNT1_CH_MASK(channel_id),
			   MPPHY_CMNCNT1_CH_EN(channel_id));
	mp_phy_update_bits(priv->base, MPPHY_PCS0REG5, MPPHY_PCS0REG5_CH(channel_id),
			   MPPHY_PCS0REG5_CH(channel_id));
	mp_phy_update_bits(priv->base, MPPHY_PCS0REG1, MPPHY_PCS0REG1_VAL, MPPHY_PCS0REG1_VAL);
	mp_phy_update_bits(priv->base, MPPHY_PXTEST(channel_id), MPPHY_PXTEST_BIT, MPPHY_PXTEST_BIT);
	mp_phy_update_bits(priv->base, MPPHY_PCS0REG5, MPPHY_PCS0REG5_CH(channel_id), 0x0);
	mp_phy_update_bits(priv->base, MPPHY_PCS0REG1, MPPHY_PCS0REG1_VAL, 0x0);
	mp_phy_update_bits(priv->base, MPPHY_PXTEST(channel_id), MPPHY_PXTEST_BIT, 0x0);

	/* Set PHY rx/tx reset and sram bypass mode */
	mp_phy_write(priv->base, MPPHY_PXRXCNT(channel_id), MPPHY_PXRXCNT_RESET_VAL);
	mp_phy_write(priv->base, MPPHY_PXSRAMCNT(channel_id), MPPHY_PXSRAMCNT_BYPASS);
	mp_phy_update_bits(priv->base, MPPHY_PXSRAMCNT(channel_id),
			   MPPHY_PXSRAMCNT_BIT3, MPPHY_PXSRAMCNT_BIT3);

	/* Clock supply settings */
	mp_phy_update_bits(priv->base, MPPHY_CMNCNT2,
			   MPPHY_CMNCNT2_CLK_CH(channel_id),
			   MPPHY_CMNCNT2_CLK_CH(channel_id));

	mp_phy_update_bits(priv->base, MPPHY_PXREFCLK(channel_id),
			   MPPHY_PXREFCLK_VAL, MPPHY_PXREFCLK_VAL);

	/* Release PHY rx/tx reset */
	mp_phy_write(priv->base, MPPHY_PXRXCNT(channel_id), 0x0);

	/* Setting Context Restore Registers and select PHY2/PHY3 protocol */
	mp_phy_write(priv->base, MPPHY_PXCNTXT1(channel_id), MPPHY_CNTXT1_VALUE);
	mp_phy_write(priv->base, MPPHY_PXCNTXT2(channel_id), cntxt2_val);
	mp_phy_write(priv->base, MPPHY_PXTXREQ(channel_id), MPPHY_PXTXREQ_VAL);

	return 0;

}

static int mp_phy_init_pcie4(struct mp_phy_priv *priv, u32 channel_id)
{
	struct mp_phy_chan_priv *chan = &priv->chan[channel_id];
	u32 lane_count = chan->lane_id ? chan->lane_id : 4; /* Default to 4 lanes */

	dev_info(priv->dev, "PCIe4 PHY initialization on channel %d, lanes: %d\n",
		 channel_id, lane_count);

	mp_phy_module_standy_set(6, 16, 0x03);

	switch(lane_count) {
	case 1:
	case 2:
		if(channel_id == 0) {
			printk("%s %d: Before: PCIEG4_MPPHY_P0CNTXT1: 0x%08x \n", __func__, __LINE__,
			       readl(priv->base + MPPHY_PXCNTXT1(0)));
			mp_phy_update_bits(priv->base, MPPHY_PXCNTXT1(0), 0x2010002, 0x2010002);
			printk("%s %d: After: PCIEG4_MPPHY_P0CNTXT1: 0x%08x \n", __func__, __LINE__,
			       readl(priv->base + MPPHY_PXCNTXT1(0)));
			printk("%s %d: Before: PCIEG4_MPPHY_P0CNTXT2: 0x%08x \n", __func__, __LINE__,
			       readl(priv->base + MPPHY_PXCNTXT2(0)));
			mp_phy_update_bits(priv->base, MPPHY_PXCNTXT2(0), 0x2020201, 0x2020201);
			printk("%s %d: After: PCIEG4_MPPHY_P0CNTXT2: 0x%08x \n", __func__, __LINE__,
			       readl(priv->base + MPPHY_PXCNTXT2(0)));
			printk("%s %d: Before: PCIEG4_MPPHY_P0TXREQ: 0x%08x \n", __func__, __LINE__,
			       readl(priv->base + MPPHY_PXTXREQ(0)));
			mp_phy_update_bits(priv->base, MPPHY_PXTXREQ(0), 0x80004, 0x80004);
			printk("%s %d: After: PCIEG4_MPPHY_P0TXREQ: 0x%08x \n", __func__, __LINE__,
			       readl(priv->base + MPPHY_PXTXREQ(0)));

			printk("%s %d: Before: PCIEG4_MPPHY_CMNCNT1: 0x%08x \n", __func__, __LINE__,
			       readl(priv->base + MPPHY_CMNCNT1));
			mp_phy_write(priv->base, MPPHY_CMNCNT1, 0x02020200);
			printk("%s %d: After: PCIEG4_MPPHY_CMNCNT1: 0x%08x \n", __func__, __LINE__,
			       readl(priv->base + MPPHY_CMNCNT1));

			printk("%s %d: Before: PCIEG4_MPPHY_CMNCNT2: 0x%08x \n", __func__, __LINE__,
			       readl(priv->base + MPPHY_CMNCNT2));
			mp_phy_update_bits(priv->base, MPPHY_CMNCNT2, 0x30000, 0x30000);
			printk("%s %d: After: PCIEG4_MPPHY_CMNCNT2: 0x%08x \n", __func__, __LINE__,
			       readl(priv->base + MPPHY_CMNCNT2));

			mp_phy_update_bits(priv->base, MPPHY_CMNCNT2, 0x1, 0x1);
			printk("%s %d: After: PCIEG4_MPPHY_CMNCNT2: 0x%08x \n", __func__, __LINE__,
			       readl(priv->base + MPPHY_CMNCNT2));

			printk("%s %d: Before: PCIEG4_MPPHY_P0REFCLK: 0x%08x \n", __func__, __LINE__,
			       readl(priv->base + MPPHY_PXREFCLK(0)));
			mp_phy_update_bits(priv->base, MPPHY_PXREFCLK(0), 0x30, 0x30);
			mp_phy_update_bits(priv->base, MPPHY_PXREFCLK(0), 0x4, 0x4);
			mp_phy_update_bits(priv->base, MPPHY_PXREFCLK(0), 0x1, 0x1);
			printk("%s %d: Before: PCIEG4_MPPHY_P0REFCLK: 0x%08x \n", __func__, __LINE__,
			       readl(priv->base + MPPHY_PXREFCLK(0)));

			printk("%s %d: Before: PCIEG4_MPPHY_P0TEST: 0x%08x \n", __func__, __LINE__,
			       readl(priv->base + MPPHY_PXTEST(0)));
			mp_phy_update_bits(priv->base, MPPHY_PXTEST(0), 0x1, 0x1);
			printk("%s %d: After: PCIEG4_MPPHY_P0TEST: 0x%08x \n", __func__, __LINE__,
			       readl(priv->base + MPPHY_PXTEST(0)));

			mp_phy_module_standy_set(6, 8, 0x03);
			mp_phy_module_standy_set(6, 10, 0x03);

			mp_phy_update_bits(priv->base, MPPHY_PXRXCNT(0), 0x202, 0x202);
			printk("%s %d: Before: PCIEG4_MPPHY_P0SRAMCNT: 0x%08x \n", __func__, __LINE__,
			       readl(priv->base + MPPHY_PXSRAMCNT(0)));
			mp_phy_update_bits(priv->base, MPPHY_PXSRAMCNT(0), 0xF, 0xF);
			printk("%s %d: After: PCIEG4_MPPHY_P0SRAMCNT: 0x%08x \n", __func__, __LINE__,
			       readl(priv->base + MPPHY_PXSRAMCNT(0)));

			mp_phy_update_bits(priv->base, MPPHY_PXRXCNT(0), 0x202, 0x0);

			printk("%s %d: Before: PCIEG4_MPPHY_P0TEST: 0x%08x \n", __func__, __LINE__,
			       readl(priv->base + MPPHY_PXTEST(0)));
			mp_phy_update_bits(priv->base, MPPHY_PXTEST(0), 0x1, 0x0);
			printk("%s %d: After: PCIEG4_MPPHY_P0TEST: 0x%08x \n", __func__, __LINE__,
			       readl(priv->base + MPPHY_PXTEST(0)));
			printk("%s %d: Before: PCIEG4_MPPHY_PCS0REG1: 0x%08x \n", __func__, __LINE__,
			       readl(priv->base + MPPHY_PCS0REG1));
			mp_phy_update_bits(priv->base, MPPHY_PCS0REG1, 0x10000, 0x0);
			printk("%s %d: After: PCIEG4_MPPHY_PCS0REG1: 0x%08x \n", __func__, __LINE__,
			       readl(priv->base + MPPHY_PCS0REG1));
			printk("%s %d: Before: PCIEG4_MPPHY_PCS0REG5: 0x%08x \n", __func__, __LINE__,
			       readl(priv->base + MPPHY_PCS0REG5));
			mp_phy_update_bits(priv->base, MPPHY_PCS0REG5, 0x3000000, 0x0);
			printk("%s %d: After: PCIEG4_MPPHY_PCS0REG5: 0x%08x \n", __func__, __LINE__,
			       readl(priv->base + MPPHY_PCS0REG5));
		}
		if(channel_id == 1) {
			mp_phy_update_bits(priv->base, MPPHY_PXCNTXT1(2), 0x2010002, 0x2010002);
			mp_phy_update_bits(priv->base, MPPHY_PXCNTXT2(2), 0x2020202, 0x2020202);
			mp_phy_update_bits(priv->base, MPPHY_PXTXREQ(2), 0x8, 0x8);

			mp_phy_write(priv->base, MPPHY_CMNCNT1, 0x01020000);
			mp_phy_update_bits(priv->base, MPPHY_CMNCNT2, 0x3000000, 0x3000000);

			mp_phy_update_bits(priv->base, MPPHY_CMNCNT2, 0x100, 0x100);

			mp_phy_update_bits(priv->base, MPPHY_PXREFCLK(2), 0x30, 0x30);
			mp_phy_update_bits(priv->base, MPPHY_PXREFCLK(2), 0x4, 0x4);
			mp_phy_update_bits(priv->base, MPPHY_PXREFCLK(2), 0x1, 0x1);
			mp_phy_update_bits(priv->base, MPPHY_PXTEST(2), 0x1, 0x1);

			mp_phy_module_standy_set(6, 12, 0x03);
			mp_phy_module_standy_set(6, 14, 0x03);
			mp_phy_update_bits(priv->base, MPPHY_PXRXCNT(2), 0x202, 0x202);
			mp_phy_update_bits(priv->base, MPPHY_PXSRAMCNT(2), 0xF, 0xF);
			mp_phy_update_bits(priv->base, MPPHY_PXRXCNT(2), 0x202, 0x0);
			mp_phy_update_bits(priv->base, MPPHY_PXTEST(2), 0x1, 0x0);
			mp_phy_update_bits(priv->base, MPPHY_PCS0REG1, 0x10000, 0x0);
			mp_phy_update_bits(priv->base, MPPHY_PCS0REG5, 0x3000000, 0x0);

		}
		break;
	case 4:
		if(channel_id == 0) {
			mp_phy_update_bits(priv->base, MPPHY_PXCNTXT1(0), 0x2010002, 0x2010002);
			mp_phy_update_bits(priv->base, MPPHY_PXCNTXT2(0), 0x2020201, 0x2020201);
			mp_phy_update_bits(priv->base, MPPHY_PXTXREQ(0), 0x8, 0x8);
			mp_phy_update_bits(priv->base, MPPHY_PXCNTXT1(1), 0x2010002, 0x2010002);
			mp_phy_update_bits(priv->base, MPPHY_PXCNTXT2(1), 0x2020201, 0x2020202);
			mp_phy_update_bits(priv->base, MPPHY_PXTXREQ(1), 0x8, 0x8);

			printk("%s %d: Before: PCIEG4_MPPHY_CMNCNT1: 0x%08x \n", __func__, __LINE__,
			       readl(priv->base + MPPHY_CMNCNT1));
			mp_phy_write(priv->base, MPPHY_CMNCNT1, 0x02020000);
			printk("%s %d: After: PCIEG4_MPPHY_CMNCNT1: 0x%08x \n", __func__, __LINE__,
			       readl(priv->base + MPPHY_CMNCNT1));

			mp_phy_update_bits(priv->base, MPPHY_CMNCNT2, 0x30000, 0x30000);
			mp_phy_update_bits(priv->base, MPPHY_CMNCNT2, 0x300000, 0x300000);

			mp_phy_update_bits(priv->base, MPPHY_CMNCNT2, 0x11, 0x11);

			mp_phy_update_bits(priv->base, MPPHY_PXREFCLK(0), 0x30, 0x30);
			mp_phy_update_bits(priv->base, MPPHY_PXREFCLK(0), 0x4, 0x4);
			mp_phy_update_bits(priv->base, MPPHY_PXREFCLK(0), 0x1, 0x1);
			mp_phy_update_bits(priv->base, MPPHY_PXREFCLK(1), 0x30, 0x30);
			mp_phy_update_bits(priv->base, MPPHY_PXREFCLK(1), 0x4, 0x4);
			mp_phy_update_bits(priv->base, MPPHY_PXREFCLK(1), 0x1, 0x1);

			printk("%s %d: Before: PCIEG4_MPPHY_P0TEST: 0x%08x \n", __func__, __LINE__,
			       readl(priv->base + MPPHY_PXTEST(0)));
			mp_phy_update_bits(priv->base, MPPHY_PXTEST(0), 0x1, 0x1);
			printk("%s %d: After: PCIEG4_MPPHY_P0TEST: 0x%08x \n", __func__, __LINE__,
			       readl(priv->base + MPPHY_PXTEST(0)));
			printk("%s %d: Before: PCIEG4_MPPHY_P1TEST: 0x%08x \n", __func__, __LINE__,
			       readl(priv->base + MPPHY_PXTEST(1)));
			mp_phy_update_bits(priv->base, MPPHY_PXTEST(1), 0x1, 0x1);
			printk("%s %d: After: PCIEG4_MPPHY_P1TEST: 0x%08x \n", __func__, __LINE__,
			       readl(priv->base + MPPHY_PXTEST(1)));

			mp_phy_module_standy_set(6, 8, 0x03);
			mp_phy_module_standy_set(6, 10, 0x03);

			mp_phy_update_bits(priv->base, MPPHY_PXRXCNT(0), 0x202, 0x202);
			mp_phy_update_bits(priv->base, MPPHY_PXRXCNT(1), 0x202, 0x202);

			printk("%s %d: Before: PCIEG4_MPPHY_P0SRAMCNT: 0x%08x \n", __func__, __LINE__,
			       readl(priv->base + MPPHY_PXSRAMCNT(0)));
			mp_phy_update_bits(priv->base, MPPHY_PXSRAMCNT(0), 0xF, 0xF);
			printk("%s %d: After: PCIEG4_MPPHY_P0SRAMCNT: 0x%08x \n", __func__, __LINE__,
			       readl(priv->base + MPPHY_PXSRAMCNT(0)));
			printk("%s %d: Before: PCIEG4_MPPHY_P1SRAMCNT: 0x%08x \n", __func__, __LINE__,
			       readl(priv->base + MPPHY_PXSRAMCNT(1)));
			mp_phy_update_bits(priv->base, MPPHY_PXSRAMCNT(1), 0xF, 0xF);
			printk("%s %d: After: PCIEG4_MPPHY_P0SRAMCNT: 0x%08x \n", __func__, __LINE__,
			       readl(priv->base + MPPHY_PXSRAMCNT(1)));

			mp_phy_update_bits(priv->base, MPPHY_PXRXCNT(0), 0x202, 0x0);
			mp_phy_update_bits(priv->base, MPPHY_PXRXCNT(1), 0x202, 0x0);

			printk("%s %d: Before: PCIEG4_MPPHY_P0TEST: 0x%08x \n", __func__, __LINE__,
			       readl(priv->base + MPPHY_PXTEST(0)));
			mp_phy_update_bits(priv->base, MPPHY_PXTEST(0), 0x1, 0x0);
			printk("%s %d: After: PCIEG4_MPPHY_P0TEST: 0x%08x \n", __func__, __LINE__,
			       readl(priv->base + MPPHY_PXTEST(0)));
			printk("%s %d: Before: PCIEG4_MPPHY_P1TEST: 0x%08x \n", __func__, __LINE__,
			       readl(priv->base + MPPHY_PXTEST(1)));
			mp_phy_update_bits(priv->base, MPPHY_PXTEST(1), 0x1, 0x0);
			printk("%s %d: After: PCIEG4_MPPHY_P1TEST: 0x%08x \n", __func__, __LINE__,
			       readl(priv->base + MPPHY_PXTEST(1)));
			printk("%s %d: Before: PCIEG4_MPPHY_PCS0REG1: 0x%08x \n", __func__, __LINE__,
			       readl(priv->base + MPPHY_PCS0REG1));
			mp_phy_update_bits(priv->base, MPPHY_PCS0REG1, 0x10000, 0x0);
			printk("%s %d: After: PCIEG4_MPPHY_PCS0REG1: 0x%08x \n", __func__, __LINE__,
			       readl(priv->base + MPPHY_PCS0REG1));
			printk("%s %d: Before: PCIEG4_MPPHY_PCS0REG5: 0x%08x \n", __func__, __LINE__,
			       readl(priv->base + MPPHY_PCS0REG5));
			mp_phy_update_bits(priv->base, MPPHY_PCS0REG5, 0xF000000, 0x0);
			printk("%s %d: After: PCIEG4_MPPHY_PCS0REG5: 0x%08x \n", __func__, __LINE__,
			       readl(priv->base + MPPHY_PCS0REG5));

		}
		if(channel_id == 1) {
			mp_phy_update_bits(priv->base, MPPHY_PXCNTXT1(2), 0x2010002, 0x2010002);
			mp_phy_update_bits(priv->base, MPPHY_PXCNTXT2(2), 0x2020202, 0x2020202);
			mp_phy_update_bits(priv->base, MPPHY_PXTXREQ(2), 0x8, 0x8);
			mp_phy_update_bits(priv->base, MPPHY_PXCNTXT1(3), 0x2010002, 0x2010002);
			mp_phy_update_bits(priv->base, MPPHY_PXCNTXT2(3), 0x2020202, 0x2020202);
			mp_phy_update_bits(priv->base, MPPHY_PXTXREQ(3), 0x8, 0x8);

			mp_phy_write(priv->base, MPPHY_CMNCNT1, 0x02020000);
			mp_phy_update_bits(priv->base, MPPHY_CMNCNT2, 0x3000000, 0x3000000);
			mp_phy_update_bits(priv->base, MPPHY_CMNCNT2, 0x30000000, 0x30000000);

			mp_phy_update_bits(priv->base, MPPHY_CMNCNT2, 0x100, 0x100);

			mp_phy_update_bits(priv->base, MPPHY_PXREFCLK(2), 0x30, 0x30);
			mp_phy_update_bits(priv->base, MPPHY_PXREFCLK(2), 0x4, 0x4);
			mp_phy_update_bits(priv->base, MPPHY_PXREFCLK(2), 0x1, 0x1);
			mp_phy_update_bits(priv->base, MPPHY_PXREFCLK(3), 0x30, 0x30);
			mp_phy_update_bits(priv->base, MPPHY_PXREFCLK(3), 0x4, 0x4);
			mp_phy_update_bits(priv->base, MPPHY_PXREFCLK(3), 0x1, 0x1);

			mp_phy_update_bits(priv->base, MPPHY_PXTEST(2), 0x1, 0x1);
			mp_phy_update_bits(priv->base, MPPHY_PXTEST(3), 0x1, 0x1);
			mp_phy_module_standy_set(6, 12, 0x03);
			mp_phy_module_standy_set(6, 14, 0x03);
			mp_phy_update_bits(priv->base, MPPHY_PXRXCNT(2), 0x202, 0x202);
			mp_phy_update_bits(priv->base, MPPHY_PXRXCNT(3), 0x202, 0x202);
			mp_phy_update_bits(priv->base, MPPHY_PXSRAMCNT(2), 0xF, 0xF);
			mp_phy_update_bits(priv->base, MPPHY_PXSRAMCNT(3), 0xF, 0xF);
			mp_phy_update_bits(priv->base, MPPHY_PXRXCNT(2), 0x202, 0x0);
			mp_phy_update_bits(priv->base, MPPHY_PXRXCNT(3), 0x202, 0x0);

			mp_phy_update_bits(priv->base, MPPHY_PXTEST(2), 0x1, 0x0);
			mp_phy_update_bits(priv->base, MPPHY_PXTEST(3), 0x1, 0x0);
			mp_phy_update_bits(priv->base, MPPHY_PCS0REG1, 0x10000, 0x0);
			mp_phy_update_bits(priv->base, MPPHY_PCS0REG5, 0xF000000, 0x0);
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

		mp_phy_write(priv->base, MPPHY_CMNCNT1, 0x00000000);
		mp_phy_update_bits(priv->base, MPPHY_CMNCNT2, 0x30000, 0x30000);
		mp_phy_update_bits(priv->base, MPPHY_CMNCNT2, 0x300000, 0x300000);
		mp_phy_update_bits(priv->base, MPPHY_CMNCNT2, 0x3000000, 0x3000000);
		mp_phy_update_bits(priv->base, MPPHY_CMNCNT2, 0x30000000, 0x30000000);

		mp_phy_update_bits(priv->base, MPPHY_CMNCNT2, 0x111, 0x111);

		mp_phy_update_bits(priv->base, MPPHY_PXRXCNT(0), 0x202, 0x202);
		mp_phy_update_bits(priv->base, MPPHY_PXRXCNT(1), 0x202, 0x202);
		mp_phy_update_bits(priv->base, MPPHY_PXRXCNT(2), 0x202, 0x202);
		mp_phy_update_bits(priv->base, MPPHY_PXRXCNT(3), 0x202, 0x202);

		mp_phy_module_standy_set(6, 8, 0x03);
		mp_phy_module_standy_set(6, 10, 0x03);
		mp_phy_module_standy_set(6, 12, 0x03);
		mp_phy_module_standy_set(6, 14, 0x03);

		mp_phy_update_bits(priv->base, MPPHY_PXRXCNT(0), 0x202, 0x202);
		mp_phy_update_bits(priv->base, MPPHY_PXRXCNT(1), 0x202, 0x202);
		mp_phy_update_bits(priv->base, MPPHY_PXRXCNT(2), 0x202, 0x202);
		mp_phy_update_bits(priv->base, MPPHY_PXRXCNT(3), 0x202, 0x202);

		mp_phy_update_bits(priv->base, MPPHY_PXSRAMCNT(0), 0xF, 0xF);
		mp_phy_update_bits(priv->base, MPPHY_PXSRAMCNT(1), 0xF, 0xF);
		mp_phy_update_bits(priv->base, MPPHY_PXSRAMCNT(2), 0xF, 0xF);
		mp_phy_update_bits(priv->base, MPPHY_PXSRAMCNT(3), 0xF, 0xF);

		mp_phy_update_bits(priv->base, MPPHY_PXRXCNT(0), 0x202, 0x0);
		mp_phy_update_bits(priv->base, MPPHY_PXRXCNT(1), 0x202, 0x0);
		mp_phy_update_bits(priv->base, MPPHY_PXRXCNT(2), 0x202, 0x0);
		mp_phy_update_bits(priv->base, MPPHY_PXRXCNT(3), 0x202, 0x0);

		mp_phy_update_bits(priv->base, MPPHY_PXTEST(0), 0x1, 0x0);
		mp_phy_update_bits(priv->base, MPPHY_PXTEST(1), 0x1, 0x0);
		mp_phy_update_bits(priv->base, MPPHY_PXTEST(2), 0x1, 0x0);
		mp_phy_update_bits(priv->base, MPPHY_PXTEST(3), 0x1, 0x0);
		mp_phy_update_bits(priv->base, MPPHY_PCS0REG1, 0x10000, 0x0);
		mp_phy_update_bits(priv->base, MPPHY_PCS0REG5, 0xFF000000, 0x0);

		break;
	}
	return 0;
}

static int mp_phy_init_usb(struct mp_phy_priv *priv, u32 channel_id)
{
	dev_info(priv->dev, "USB PHY initialization requested on channel %d\n", channel_id);
	dev_info(priv->dev, "USB PHY settings not implemented yet - will be configured later\n");

	/* TODO: Implement USB specific initialization here */

	return 0;
}

static int mp_phy_init(struct phy *phy)
{
	struct mp_phy_priv *priv = phy_get_drvdata(phy);
	u32 channel_id = phy->id;

	/*
	 * Note: Current source code support for Ethernet, PCIe
	 * initialization is based on the bare metal code shared by the board team.
	 * USB initialization is not implemented yet.
	 */

	if (channel_id > 3) {
		dev_err(priv->dev, "Invalid channel ID: %d\n", channel_id);
		return -EINVAL;
	}

	switch(channel_id) {
	case 0:
	case 1:
		mp_phy_init_pcie4(priv, channel_id);
		break;
	case 2:
		mp_phy_init_ethernet(priv, channel_id);
		break;
	case 3:
		mp_phy_init_usb(priv, channel_id);
		break;
	}

	return 0;
}


static int mp_phy_late_init(struct phy *phy)
{
	struct mp_phy_priv *priv = phy_get_drvdata(phy);
	struct mp_phy_chan_priv *chan = &priv->chan[phy->id];
	u32 data;
	u32 channel_id = phy->id;
	u32 lane_count;

	/*
	 * The datasheet describes initialization procedure without full
	 * information about the registers. Therefore, the source code is based
	 * on the bare metal code shared by the board team.
	 */
	if (channel_id == 0 || channel_id == 1) {
		lane_count = chan->lane_id ? chan->lane_id : 4; /* Default to 4 lanes */

		switch(lane_count) {
		case 1:
		case 2:
			if(channel_id == 0) {
				while (1) {
					data = readl(priv->base + MPPHY_PXSRAMCNT(0));
					if (data & BIT(5))
						break;
				}
				printk("%s %d: MPPHY_P0SRAMCNT: 0x%x\n", __func__, __LINE__,readl(priv->base + MPPHY_PXSRAMCNT(0)));
				while (1) {
					data = readl(priv->base + MPPHY_PXRXREQ1(0));
					if (!(data & BIT(1)))
						break;
				}
				printk("%s %d: MPPHY_P0RXREQ1: 0x%x\n", __func__, __LINE__,readl(priv->base + MPPHY_PXRXREQ1(0)));
			}
			if(channel_id == 1) {
				while (1) {
					data = readl(priv->base + MPPHY_PXSRAMCNT(2));
					if (data & BIT(5))
						break;
				}
				printk("%s %d: MPPHY_P2SRAMCNT: 0x%x\n", __func__, __LINE__,readl(priv->base + MPPHY_PXSRAMCNT(2)));
				while (1) {
					data = readl(priv->base + MPPHY_PXRXREQ1(2));
					if (!(data & BIT(1)))
						break;
				}
				printk("%s %d: MPPHY_P2RXREQ1: 0x%x\n", __func__, __LINE__,readl(priv->base + MPPHY_PXRXREQ1(2)));
			}
			break;
		case 4:
			if(channel_id == 0) {
				while (1) {
					data = readl(priv->base + MPPHY_PXSRAMCNT(0));
					if (data & BIT(5))
						break;
				}
				printk("%s %d: MPPHY_P0SRAMCNT: 0x%x\n", __func__, __LINE__,readl(priv->base + MPPHY_PXSRAMCNT(0)));
				while (1) {
					data = readl(priv->base + MPPHY_PXSRAMCNT(1));
					if (data & BIT(5))
						break;
				}
				printk("%s %d: MPPHY_P1SRAMCNT: 0x%x\n", __func__, __LINE__,readl(priv->base + MPPHY_PXSRAMCNT(1)));
				while (1) {
					data = readl(priv->base + MPPHY_PXRXREQ1(0));
					if (!(data & BIT(1)))
						break;
				}
				printk("%s %d: MPPHY_P0RXREQ1: 0x%x\n", __func__, __LINE__,readl(priv->base + MPPHY_PXRXREQ1(0)));
				while (1) {
					data = readl(priv->base + MPPHY_PXRXREQ1(1));
					if (!(data & BIT(1)))
						break;
				}
				printk("%s %d: MPPHY_P1RXREQ1: 0x%x\n", __func__, __LINE__,readl(priv->base + MPPHY_PXRXREQ1(1)));
			}
			if(channel_id == 1) {
				while (1) {
					data = readl(priv->base + MPPHY_PXSRAMCNT(2));
					if (data & BIT(5))
						break;
				}
				printk("%s %d: MPPHY_P2SRAMCNT: 0x%x\n", __func__, __LINE__,readl(priv->base + MPPHY_PXSRAMCNT(2)));
				while (1) {
					data = readl(priv->base + MPPHY_PXSRAMCNT(3));
					if (data & BIT(5))
						break;
				}
				printk("%s %d: MPPHY_P3SRAMCNT: 0x%x\n", __func__, __LINE__,readl(priv->base + MPPHY_PXSRAMCNT(3)));
				while (1) {
					data = readl(priv->base + MPPHY_PXRXREQ1(2));
					if (!(data & BIT(1)))
						break;
				}
				printk("%s %d: MPPHY_P2RXREQ1: 0x%x\n", __func__, __LINE__,readl(priv->base + MPPHY_PXRXREQ1(2)));
				while (1) {
					data = readl(priv->base + MPPHY_PXRXREQ1(3));
					if (!(data & BIT(1)))
						break;
				}
				printk("%s %d: MPPHY_P3RXREQ1: 0x%x\n", __func__, __LINE__,readl(priv->base + MPPHY_PXRXREQ1(3)));
			}
			break;
		case 8:
			while (1) {
				data = readl(priv->base + MPPHY_PXSRAMCNT(0));
				if (data & BIT(5))
					break;
			}
			printk("%s %d: MPPHY_P0SRAMCNT: 0x%x\n", __func__, __LINE__,readl(priv->base + MPPHY_PXSRAMCNT(0)));
			while (1) {
				data = readl(priv->base + MPPHY_PXSRAMCNT(1));
				if (data & BIT(5))
					break;
			}
			printk("%s %d: MPPHY_P1SRAMCNT: 0x%x\n", __func__, __LINE__,readl(priv->base + MPPHY_PXSRAMCNT(1)));
			while (1) {
				data = readl(priv->base + MPPHY_PXSRAMCNT(2));
				if (data & BIT(5))
					break;
			}
			printk("%s %d: MPPHY_P2SRAMCNT: 0x%x\n", __func__, __LINE__,readl(priv->base + MPPHY_PXSRAMCNT(2)));
			while (1) {
				data = readl(priv->base + MPPHY_PXSRAMCNT(3));
				if (data & BIT(5))
					break;
			}
			printk("%s %d: MPPHY_P3SRAMCNT: 0x%x\n", __func__, __LINE__,readl(priv->base + MPPHY_PXSRAMCNT(3)));
			while (1) {
				data = readl(priv->base + MPPHY_PXRXREQ1(0));
				if (!(data & BIT(1)))
					break;
			}
			printk("%s %d: MPPHY_P0RXREQ1: 0x%x\n", __func__, __LINE__,readl(priv->base + MPPHY_PXRXREQ1(0)));
			while (1) {
				data = readl(priv->base + MPPHY_PXRXREQ1(1));
				if (!(data & BIT(1)))
					break;
			}
			printk("%s %d: MPPHY_P1RXREQ1: 0x%x\n", __func__, __LINE__,readl(priv->base + MPPHY_PXRXREQ1(1)));
			while (1) {
				data = readl(priv->base + MPPHY_PXRXREQ1(2));
				if (!(data & BIT(1)))
					break;
			}
			printk("%s %d: MPPHY_P2RXREQ1: 0x%x\n", __func__, __LINE__,readl(priv->base + MPPHY_PXRXREQ1(2)));
			while (1) {
				data = readl(priv->base + MPPHY_PXRXREQ1(3));
				if (!(data & BIT(1)))
					break;
			}
			printk("%s %d: MPPHY_P3RXREQ1: 0x%x\n", __func__, __LINE__,readl(priv->base + MPPHY_PXRXREQ1(3)));
			break;
		}
	} else if (channel_id == 2) {
		mp_phy_write(priv->base, MPPHY_PXSRAMCNT(channel_id), SRAM_CONTROL_SET_BIT);
	}

	return 0;
}

static int mp_phy_set_mode(struct phy *phy, enum phy_mode mode, int submode)
{
	if (mode == PHY_MODE_PCIE) {
		dev_info(&phy->dev, "Supported mode: %d\n", mode);
		return 0;
	} else if (mode == PHY_MODE_ETHERNET) {
		dev_info(&phy->dev, "Supported mode: %d\n", mode);
		return 0;
	} else if (mode == PHY_MODE_USB_HOST || mode == PHY_MODE_USB_DEVICE || mode == PHY_MODE_USB_OTG) {
		dev_info(&phy->dev, "Supported mode: %d\n", mode);
		return 0;
	} else {
		dev_info(&phy->dev, "Unknown protocol for set_mode\n");
		return -EOPNOTSUPP;
	}
}

static const struct phy_ops mp_phy_ops = {
	.init		= mp_phy_init,
	.power_on	= mp_phy_late_init,
	.set_mode	= mp_phy_set_mode,
	.owner		= THIS_MODULE,
};

static struct phy *mp_phy_xlate(struct device *dev, struct of_phandle_args *args)
{
	struct mp_phy_priv *priv = dev_get_drvdata(dev);
	struct mp_phy_chan_priv *chan;
	struct phy *phy;

	if (args->args_count > 2) {
		dev_err(dev, "Invalid args_count: %d\n", args->args_count);
		return ERR_PTR(-EINVAL);
	}

	phy = devm_phy_create(dev, NULL, &mp_phy_ops);
	if (IS_ERR(phy)) {
		dev_err(dev, "Failed to create PHY\n");
		return phy;
	}

	/* Set channel ID from first argument if available */
	if (args->args_count >= 1)
		phy->id = args->args[0];
	else
		phy->id = 0;

	chan = &priv->chan[phy->id];
	chan->channel_id = phy->id;

	/* Set lane ID from second argument if available */
	if (args->args_count >= 2)
		chan->lane_id = args->args[1];
	else
		chan->lane_id = 0;

	phy_set_drvdata(phy, priv);

	return phy;
}

static int mp_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mp_phy_priv *priv;
	struct phy_provider *provider;
	struct resource *res;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;

	/* Get base address from device tree */
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

	/* TODO: Enable reset control when DTS binding is ready */
	/* Get reset control */
	//priv->reset = devm_reset_control_get(dev, NULL);
	//if (IS_ERR(priv->reset)) {
	//	dev_err(dev, "Failed to get reset control\n");
	//	return PTR_ERR(priv->reset);
	//}

	/* TODO: Enable clock control when clock is available */
	/* Get clocks */
	//priv->clk = devm_clk_get(dev, NULL);
	//if (IS_ERR(priv->clk)) {
	//	dev_err(dev, "Failed to get mp_phy clock\n");
	//	return PTR_ERR(priv->clk);
	//}

	/* Enable clock if available */
	//if (priv->clk) {
	//	int ret = clk_prepare_enable(priv->clk);
	//	if (ret) {
	//		dev_err(dev, "Failed to enable clock: %d\n", ret);
	//		return ret;
	//	}
	//}

	mp_phy_module_power_reset();
	udelay(1000);
	mp_phy_module_power_run();

	platform_set_drvdata(pdev, priv);

	provider = devm_of_phy_provider_register(dev, mp_phy_xlate);
	if (IS_ERR(provider)) {
		dev_err(dev, "Failed to register PHY provider\n");
		return PTR_ERR(provider);
	}

	dev_info(dev, "Multi-Protocol PHY driver probed successfully\n");
	return 0;
}

static int mp_phy_remove(struct platform_device *pdev)
{
	pm_runtime_put(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	platform_set_drvdata(pdev, NULL);

	return 0;
}

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
	},
};

module_platform_driver(mp_phy_driver);

MODULE_AUTHOR("Thanh Quan");
MODULE_DESCRIPTION("Renesas Multi-Protocol PHY driver");
MODULE_LICENSE("GPL v2");
