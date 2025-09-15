// SPDX-License-Identifier: GPL-2.0-only
/*
 * Renesas UFS host controller driver for R-Car Gen5
 *
 * Copyright (C) 2025 Renesas Electronics Corporation
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>
#include <linux/sys_soc.h>
#include <ufs/ufshcd.h>

#include "ufshcd-pltfrm.h"

struct ufs_rcar_gen5_priv {
	void __iomem *phy_base;
	bool initialized;	/* The hardware needs initialization once */
};

/* Hardcoded for enable module clock */
#define MDLC_BASE		0xC08F0000
#define UFS_PDID		(0)
#define UFS_CLK_MASK(n)	GENMASK((n) + 1, n)
#define UFS_CLK_SHIFT(n)	(n)

#define MDLC_PKCPROT0		(MDLC_BASE + 0x0cf0)
#define MDLC_PKCPROT1		(MDLC_BASE + 0x0cf4)

#define _MDLC_MPDG(k)		(MDLC_BASE + 0x0200 + (k) * 4)
#define _MDLC_MPDGS(k)		(MDLC_BASE + 0x0300 + (k) * 4)
#define MDLC_MPIER0		(MDLC_BASE + 0x0110)
#define MDLC_MPIMR0		(MDLC_BASE + 0x0120)

#define MDLC_MPDG		_MDLC_MPDG(UFS_PDID)
#define MDLC_MPDGS		_MDLC_MPDGS(UFS_PDID)

#define MDLC_MSRES(i)		(MDLC_BASE + 0x0900 + (i) * 4)
#define MDLC_MSRESS(i)	(	MDLC_BASE + 0x0960 + (i) * 4)

static void ufs_module_power_gating_set(u8 pdid, u8 mode)
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

static void ufs_module_standy_set(u8 clk_reg_no, u8 pos, u8 mode)
{
	void __iomem *unlock = ioremap(MDLC_PKCPROT1, 4);
	void __iomem *msress = ioremap(MDLC_MSRESS(clk_reg_no), 4);
	void __iomem *msres = ioremap(MDLC_MSRES(clk_reg_no), 4);
	u32 val;

	writel(0xA5A5A501, unlock);

	if ((readl(msress) & UFS_CLK_MASK(pos)) == (mode << UFS_CLK_SHIFT(pos)))
			goto unmap;

	while ((readl(msress) & UFS_CLK_MASK(pos)) != (readl(msres) & UFS_CLK_MASK(pos)))
			udelay(1000);

	val = readl(msres);
	val &= ~UFS_CLK_MASK(pos);
	val |= mode << UFS_CLK_SHIFT(pos);
	writel(val, msres);

	while ((readl(msress) & UFS_CLK_MASK(pos)) != (readl(msres) & UFS_CLK_MASK(pos)))
			udelay(1000);

unmap:
	iounmap(unlock);
	iounmap(msress);
	iounmap(msres);
}

static void __maybe_unused ufs0_module_power_reset(void)
{
	ufs_module_power_gating_set(0, 0x03);
	ufs_module_standy_set(6, 0, 0x01);
}

static void __maybe_unused ufs0_module_power_run(void)
{
	ufs_module_power_gating_set(0, 0x03);
	ufs_module_standy_set(6, 0, 0x03);
}

static void __maybe_unused ufs1_module_power_reset(void)
{
	ufs_module_power_gating_set(1, 0x03);
	ufs_module_standy_set(6, 2, 0x01);
}

static void __maybe_unused ufs1_module_power_run(void)
{
	ufs_module_power_gating_set(1, 0x03);
	ufs_module_standy_set(6, 2, 0x03);
}

static void ufs_rcar_gen5_send_dme_command(struct ufs_hba *hba, u32 cmd,
					   u32 arg1, u32 arg2, u32 arg3)
{
	ufshcd_writel(hba, arg1, REG_UIC_COMMAND_ARG_1);
	ufshcd_writel(hba, arg2, REG_UIC_COMMAND_ARG_2);
	ufshcd_writel(hba, arg3, REG_UIC_COMMAND_ARG_3);
	ufshcd_writel(hba, cmd, REG_UIC_COMMAND);
}

static void ufs_rcar_gen5_pre_init(struct ufs_hba *hba)
{
	struct ufs_rcar_gen5_priv *priv = ufshcd_get_variant(hba);
	int ret, timeout;
	u32 val32;
	u16 val;

	if (priv->initialized)
		return;

	/* FIXME:
	 * Since we don't know whether register names can be explosed,
	 * this driver uses magic numbers for now.
	 */
							/* # from sample code */
	iowrite16(0x0001, priv->phy_base + 0x20000);	/* 1 */
	iowrite16(0x005c, priv->phy_base + 0x20212);	/* 2 */
	iowrite16(0x005c, priv->phy_base + 0x20214);	/* 3 */
	iowrite16(0x005c, priv->phy_base + 0x20216);	/* 4 */
	iowrite16(0x005c, priv->phy_base + 0x20218);	/* 5 */
	iowrite16(0x036a, priv->phy_base + 0x201d0);	/* 6 */
	iowrite16(0x0102, priv->phy_base + 0x201d2);	/* 7 */
	iowrite16(0x001f, priv->phy_base + 0x20082);	/* 8 */
	iowrite16(0x000b, priv->phy_base + 0x20084);	/* 9 */
	iowrite16(0x0126, priv->phy_base + 0x201d2);	/* 10 */
	iowrite16(0x01dc, priv->phy_base + 0x20214);	/* 12 */
	iowrite16(0x01dc, priv->phy_base + 0x20218);	/* 13 */
	iowrite16(0x0000, priv->phy_base + 0x201cc);	/* 15 */
	iowrite16(0x0200, priv->phy_base + 0x201ce);	/* 16 */
	iowrite16(0x0000, priv->phy_base + 0x20212);	/* 17 */
	iowrite16(0x0000, priv->phy_base + 0x20216);	/* 18 */
	ret = readw_poll_timeout_atomic(priv->phy_base + 0x201ec, val,
					(val & BIT(12)) == 0, 1, 100000);
	if (ret)
		return;
	ret = readw_poll_timeout_atomic(priv->phy_base + 0x201e4, val,
					(val & BIT(12)) == 0, 1, 100000);
	if (ret)
		return;
	ret = readw_poll_timeout_atomic(priv->phy_base + 0x201f0, val,
					(val & BIT(12)) == 0, 1, 100000);
	if (ret)
		return;
	ret = readw_poll_timeout_atomic(priv->phy_base + 0x201e8, val,
					(val & BIT(12)) == 0, 1, 100000);
	if (ret)
		return;
	iowrite16(0x0000, priv->phy_base + 0x20000);		/* 19 */
	ufshcd_writel(hba, BIT(0), REG_CONTROLLER_ENABLE);	/* 20 */
	timeout = 100000;
	do {
		val32 = ufshcd_readl(hba, REG_CONTROLLER_ENABLE);
		if (val32 & BIT(0))
			break;
		udelay(1);
	} while (timeout--);
	/* 25 */
	timeout = 100000;
	do {
		val32 = ufshcd_readl(hba, REG_CONTROLLER_STATUS);
		if (val32 & BIT(3))
			break;
		udelay(1);
	} while (timeout--);
	/* 26: Skip IE because we cannot handle interrupts here */
	/* 27 */
	ufs_rcar_gen5_send_dme_command(hba, 0x00000002, 0x81010000, 0x00000000, 0x00000005);
	/* 28 */
	ufs_rcar_gen5_send_dme_command(hba, 0x00000002, 0x81150000, 0x00000000, 0x00000001);
	/* 29 */
	ufs_rcar_gen5_send_dme_command(hba, 0x00000002, 0x81180000, 0x00000000, 0x00000001);
	/* 30 */
	ufs_rcar_gen5_send_dme_command(hba, 0x00000002, 0x80090000, 0x00000000, 0x00000000);
	/* 31 */
	ufs_rcar_gen5_send_dme_command(hba, 0x00000002, 0x800a0000, 0x00000000, 0x000000c8);
	/* 32 */
	ufs_rcar_gen5_send_dme_command(hba, 0x00000002, 0x80090001, 0x00000000, 0x00000000);
	/* 33 */
	ufs_rcar_gen5_send_dme_command(hba, 0x00000002, 0x800a0001, 0x00000000, 0x000000c8);
	/* 34 */
	ufs_rcar_gen5_send_dme_command(hba, 0x00000002, 0x800a0004, 0x00000000, 0x00000000);
	/* 35 */
	ufs_rcar_gen5_send_dme_command(hba, 0x00000002, 0x800b0004, 0x00000000, 0x00000064);
	/* 36 */
	ufs_rcar_gen5_send_dme_command(hba, 0x00000002, 0x800a0005, 0x00000000, 0x00000000);
	/* 37 */
	ufs_rcar_gen5_send_dme_command(hba, 0x00000002, 0x800b0005, 0x00000000, 0x00000064);
	/* 38 */
	ufs_rcar_gen5_send_dme_command(hba, 0x00000002, 0xd0850000, 0x00000000, 0x00000001);
	iowrite16(0x0001, priv->phy_base + 0x20000);	/* 39 */
	/* 40 */
	val = ioread16(priv->phy_base + 0x20022);
	iowrite16(val & ~BIT(0), priv->phy_base + 0x20022);
	/* 41 */
	ret = readw_poll_timeout_atomic(priv->phy_base + (0x00198 << 1), val,
					(val & BIT(0)) == BIT(0), 1, 100000);
	if (ret)
		return;
	iowrite16(0x0368, priv->phy_base + 0x201d0);	/* 44 */
	/* 45-48 */
	ret = readw_poll_timeout_atomic(priv->phy_base + 0x201e4, val,
					(val & BIT(11)) == 0, 1, 100000);
	if (ret)
		return;
	ret = readw_poll_timeout_atomic(priv->phy_base + 0x201e8, val,
					(val & BIT(11)) == 0, 1, 100000);
	if (ret)
		return;
	ret = readw_poll_timeout_atomic(priv->phy_base + 0x201ec, val,
					(val & BIT(11)) == 0, 1, 100000);
	if (ret)
		return;
	ret = readw_poll_timeout_atomic(priv->phy_base + 0x201f0, val,
					(val & BIT(11)) == 0, 1, 100000);
	if (ret)
		return;

	priv->initialized = true;
}

static int ufs_rcar_gen5_hce_enable_notify(struct ufs_hba *hba,
					   enum ufs_notify_change_status status)
{
	if (status == PRE_CHANGE)
		ufs_rcar_gen5_pre_init(hba);

	return 0;
}

static int
ufs_rcar_gen5_pre_pwr_change(struct ufs_hba *hba,
			     struct ufs_pa_layer_attr *dev_max_params,
			     struct ufs_pa_layer_attr *dev_req_params)
{
	struct ufs_dev_params dev_param;
	int ret;

	ufshcd_init_pwr_dev_param(&dev_param);
	dev_param.hs_rx_gear = UFS_HS_G5;
	dev_param.hs_tx_gear = UFS_HS_G5;
	dev_param.rx_pwr_hs = FASTAUTO_MODE;
	dev_param.tx_pwr_hs = FASTAUTO_MODE;
	dev_param.hs_rate = PA_HS_MODE_A;

	ret = ufshcd_get_pwr_dev_param(&dev_param, dev_max_params, dev_req_params);

	return 0;
}

static int
ufs_rcar_gen5_pwr_change_notify(struct ufs_hba *hba,
				enum ufs_notify_change_status stage,
				struct ufs_pa_layer_attr *dev_max_params,
				struct ufs_pa_layer_attr *dev_req_params)
{
	if (stage == PRE_CHANGE)
		return ufs_rcar_gen5_pre_pwr_change(hba, dev_max_params,
						    dev_req_params);

	return 0;
}

static int ufs_rcar_gen5_init(struct ufs_hba *hba)
{
	struct platform_device *pdev = to_platform_device(hba->dev);
	struct ufs_rcar_gen5_priv *priv;

	priv = devm_kzalloc(hba->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	ufshcd_set_variant(hba, priv);

	priv->phy_base = devm_platform_ioremap_resource_byname(pdev, "phy");
	if (IS_ERR(priv->phy_base))
		return PTR_ERR(priv->phy_base);

	/* FIXME */
	hba->quirks |= UFSHCD_QUIRK_BROKEN_64BIT_ADDRESS | UFSHCD_QUIRK_HIBERN_FASTAUTO;

	return 0;
}

static int ufs_rcar_gen5_suspend(struct ufs_hba *hba, enum ufs_pm_op op,
				 enum ufs_notify_change_status status)
{
	struct ufs_rcar_gen5_priv *priv = ufshcd_get_variant(hba);

	if (op == UFS_SYSTEM_PM)
		ufshcd_set_link_off(hba);

	if (status == PRE_CHANGE)
		return 0;

	/* it shuold be re-initialized again */
	priv->initialized = false;

	return 0;
}

static int ufs_rcar_gen5_resume(struct ufs_hba *hba, enum ufs_pm_op op)
{

	struct platform_device *pdev = to_platform_device(hba->dev);

	struct resource *res;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "phy");

	if (res->start == 0xc0a00000) {
		ufs0_module_power_reset();
		udelay(1000);
		ufs0_module_power_run();
		udelay(1000);
	}
	else {
		ufs1_module_power_reset();
		udelay(1000);
		ufs1_module_power_run();
		udelay(1000);
	}

	/* re-initialized again */
	ufs_rcar_gen5_pre_init(hba);

	return 0;
}

static const struct ufs_hba_variant_ops ufs_rcar_gen5_vops = {
	.name		= "renesas",
	.init		= ufs_rcar_gen5_init,
	.hce_enable_notify = ufs_rcar_gen5_hce_enable_notify,
	.pwr_change_notify = ufs_rcar_gen5_pwr_change_notify,
	.suspend	= ufs_rcar_gen5_suspend,
	.resume		= ufs_rcar_gen5_resume,
};

static const struct of_device_id __maybe_unused ufs_rcar_gen5_of_match[] = {
	{ .compatible = "renesas,rcar-gen5-ufs" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ufs_rcar_gen5_of_match);

static int ufs_rcar_gen5_probe(struct platform_device *pdev)
{
	return ufshcd_pltfrm_init(pdev, &ufs_rcar_gen5_vops);
}

static int ufs_rcar_gen5_remove(struct platform_device *pdev)
{
	struct ufs_hba *hba = platform_get_drvdata(pdev);

	ufshcd_remove(hba);

	return 0;
}

static struct platform_driver ufs_rcar_gen5_platform = {
	.probe	= ufs_rcar_gen5_probe,
	.remove	= ufs_rcar_gen5_remove,
	.driver	= {
		.name	= "ufshcd-renesas-rcar-gen5",
		.of_match_table	= of_match_ptr(ufs_rcar_gen5_of_match),
	},
};
module_platform_driver(ufs_rcar_gen5_platform);

MODULE_AUTHOR("Yoshihiro Shimoda <yoshihiro.shimoda.uh@renesas.com>");
MODULE_DESCRIPTION("Renesas UFS host controller driver for R-Car Gen5");
MODULE_LICENSE("Dual MIT/GPL");
