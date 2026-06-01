// SPDX-License-Identifier: GPL-2.0-only
/*
 * Renesas UFS host controller driver for R-Car Gen5
 *
 * Copyright (C) 2025 Renesas Electronics Corporation
 */

#include <linux/clk.h>
#include <linux/reset.h>
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
#include <linux/platform_device.h>

#include "ufshcd-pltfrm.h"

struct ufs_rcar_gen5_priv {
	void __iomem *phy_base;
	struct clk *clk;
	struct reset_control *rstc;
	bool initialized;	/* The hardware needs initialization once */
};

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
	val = ioread16(priv->phy_base + 0x20000);
	iowrite16(val & ~BIT(0), priv->phy_base + 0x20000);	/* 19 */
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

	val32 = ufshcd_readl(hba, 0x000000C0);

	val32 &= ~0xE000;

	ufshcd_writel(hba, val32, 0x000000C0);

	val32 = ufshcd_readl(hba, 0x000000C0);

	val32 |=  0x5000;

	ufshcd_writel(hba, val32, 0x000000C0);

	val32 = ufshcd_readl(hba, REG_INTERRUPT_ENABLE);

	ufshcd_writel(hba, val32 | BIT(10),  REG_INTERRUPT_ENABLE);

	/* 27 */
	ufshcd_dme_set(hba, 0x81010000, 0x00000005);
	/* 28 */
	ufshcd_dme_set(hba, 0x81150000, 0x00000001);
	/* 29 */
	ufshcd_dme_set(hba, 0x81180000, 0x00000001);
	/* 30 */
	ufshcd_dme_set(hba, 0x80090000, 0x0000000C);
	/* 31 */
	ufshcd_dme_set(hba, 0x800a0000, 0x00000080);
	/* 32 */
	ufshcd_dme_set(hba, 0x80090001, 0x0000000C);
	/* 33 */
	ufshcd_dme_set(hba, 0x800a0001, 0x00000080);
	/* 34 */
	ufshcd_dme_set(hba, 0x800a0004, 0x00000003);
	/* 35 */
	ufshcd_dme_set(hba, 0x800b0004, 0x000000EA);
	/* 36 */
	ufshcd_dme_set(hba, 0x800a0005, 0x00000003);
	/* 37 */
	ufshcd_dme_set(hba, 0x800b0005, 0x000000EA);
	/* 38 */
	ufshcd_dme_set(hba, 0xd0850000, 0x00000001);

	val = ioread16(priv->phy_base + 0x20000);
	iowrite16(val | BIT(0), priv->phy_base + 0x20000);	/* 39 */

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
	val = ioread16(priv->phy_base + 0x20000);
	iowrite16(val & ~BIT(0), priv->phy_base + 0x20000);

	ufshcd_dme_set(hba, 0xd0890000, 0x00000001);

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
	struct ufs_host_params dev_param;
	int ret;

	ufshcd_init_host_params(&dev_param);
	dev_param.hs_rx_gear = UFS_HS_G5;
	dev_param.hs_tx_gear = UFS_HS_G5;
	dev_param.rx_pwr_hs = FAST_MODE;
	dev_param.tx_pwr_hs = FAST_MODE;
	dev_param.hs_rate = PA_HS_MODE_B;
	dev_param.tx_lanes = 2;
	dev_param.rx_lanes = 2;

	ret = ufshcd_negotiate_pwr_params(&dev_param, dev_max_params, dev_req_params);

	if (ret)
		dev_err(hba->dev, "%s: failed to determine capabilities\n",
			__func__);

	ufshcd_dme_configure_adapt(hba,
				dev_req_params->gear_tx,
				PA_INITIAL_ADAPT);

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

	priv->clk = devm_clk_get(hba->dev, "fck");
	if (IS_ERR(priv->clk))
		return PTR_ERR(priv->clk);

	priv->rstc = devm_reset_control_get_optional_exclusive(hba->dev, NULL);
	if (IS_ERR(priv->rstc))
		return PTR_ERR(priv->rstc);

	priv->phy_base = devm_platform_ioremap_resource_byname(pdev, "phy");
	if (IS_ERR(priv->phy_base))
		return PTR_ERR(priv->phy_base);

	reset_control_deassert(priv->rstc);

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

static void ufs_rcar_gen5_remove(struct platform_device *pdev)
{
	ufshcd_pltfrm_remove(pdev);
}

static int ufs_system_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ufs_hba *hba = platform_get_drvdata(pdev);
	struct ufs_rcar_gen5_priv *priv = ufshcd_get_variant(hba);
	int ret = 0;

	ret = reset_control_assert(priv->rstc);
	if (ret)
		dev_warn(dev, "Failed to release the UFS system from reset");

	return ufshcd_system_suspend(dev);
}

static int ufs_system_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ufs_hba *hba = platform_get_drvdata(pdev);
	struct ufs_rcar_gen5_priv *priv = ufshcd_get_variant(hba);
	int ret = 0;

	ret = ufshcd_system_resume(dev);
	if (ret) {
		dev_err(dev, "Failed to resume the UFS system");
		return ret;
	}

	ret = reset_control_deassert(priv->rstc);
	if (ret)
		dev_warn(dev, "Failed to release the UFS system from reset");

	return 0;
}

static const struct dev_pm_ops ufs_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(ufs_system_suspend, ufs_system_resume)
	.prepare         = ufshcd_suspend_prepare,
	.complete        = ufshcd_resume_complete,
};

static struct platform_driver ufs_rcar_gen5_platform = {
	.probe	= ufs_rcar_gen5_probe,
	.remove_new	= ufs_rcar_gen5_remove,
	.driver	= {
		.name	= "ufshcd-renesas-rcar-gen5",
		.pm = pm_sleep_ptr(&ufs_pm_ops),
		.of_match_table	= of_match_ptr(ufs_rcar_gen5_of_match),
	},
};
module_platform_driver(ufs_rcar_gen5_platform);

MODULE_AUTHOR("Yoshihiro Shimoda <yoshihiro.shimoda.uh@renesas.com>");
MODULE_DESCRIPTION("Renesas UFS host controller driver for R-Car Gen5");
MODULE_LICENSE("Dual MIT/GPL");
