// SPDX-License-Identifier: GPL-2.0-only
/*
 * Renesas Ethernet PCS Device Driver
 *
 * Based on the Renesas Ethernet SERDES driver and updated for PCS support.
 *
 * Copyright (C) 2025 Renesas Electronics Corporation
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/phy.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/clk.h>

#define R8A78000_ETH_PCS_NUM				8
#define R8A78000_ETH_PCS_OFFSET				0x0400
#define R8A78000_ETH_PCS_BANK_SELECT		0x03fc
#define R8A78000_ETH_PCS_TIMEOUT_US			100000000
#define R8A78000_ETH_PCS_NUM_RETRY_LINKUP	8

struct r8a78000_eth_pcs_drv_data;
struct r8a78000_eth_pcs_channel {
	struct r8a78000_eth_pcs_drv_data *dd;
	struct phy *phy;
	void __iomem *addr;
	struct phy *mpphy;
	phy_interface_t phy_interface;
	bool aneg_on;
	int speed;
	int index;
	bool powered_on;
};

struct r8a78000_eth_pcs_drv_data {
	void __iomem *addr;
	struct platform_device *pdev;
	struct reset_control *resets[R8A78000_ETH_PCS_NUM];
	struct clk_bulk_data *clks;
	int num_clks;
	struct r8a78000_eth_pcs_channel channel[R8A78000_ETH_PCS_NUM];
};

static void r8a78000_eth_pcs_write32(void __iomem *addr, u32 offs, u32 bank, u32 data)
{
	iowrite32(bank, addr + R8A78000_ETH_PCS_BANK_SELECT);
	iowrite32(data, addr + offs);
}

static int
r8a78000_eth_pcs_reg_wait(struct r8a78000_eth_pcs_channel *channel,
			  u32 offs, u32 bank, u32 mask, u32 expected)
{
	u32 val = 0;
	int ret;

	iowrite32(bank, channel->addr + R8A78000_ETH_PCS_BANK_SELECT);

	ret = readl_poll_timeout_atomic(channel->addr + offs, val,
					(val & mask) == expected,
					1, R8A78000_ETH_PCS_TIMEOUT_US);
	if (ret)
		dev_dbg(&channel->phy->dev,
			"%s: index %d, offs %x, bank %x, mask %x, expected %x\n",
			 __func__, channel->index, offs, bank, mask, expected);

	return ret;
}

static int
r8a78000_eth_pcs_init_ram(struct r8a78000_eth_pcs_channel *channel, struct phy *mpphy)
{
	int ret;

	ret = r8a78000_eth_pcs_reg_wait(channel, 0x026c, 0x180, BIT(0), 0x01);
	if (ret)
		return ret;

	r8a78000_eth_pcs_write32(channel->addr, 0x026c, 0x180, 0x03);

	ret = phy_power_on(mpphy);
	usleep_range(1000, 1100);

	if (ret)
		return ret;

	ret = r8a78000_eth_pcs_reg_wait(channel, 0x0000, 0x300, BIT(15), 0);
	if (ret)
		return ret;

	return 0;
}

static int r8a78000_eth_pcs_common_setting(struct r8a78000_eth_pcs_channel *channel)
{
	int ret;

	switch (channel->phy_interface) {
	case PHY_INTERFACE_MODE_SGMII:
		r8a78000_eth_pcs_write32(channel->addr, 0x001c, 0x300, 0x0001);
		r8a78000_eth_pcs_write32(channel->addr, 0x0000, 0x380, 0x2000);
		r8a78000_eth_pcs_write32(channel->addr, 0x0000, 0x1f00, 0x0140);
		r8a78000_eth_pcs_write32(channel->addr, 0x00f8, 0x180, 0x0019);
		r8a78000_eth_pcs_write32(channel->addr, 0x0248, 0x180, 0x001e);

		r8a78000_eth_pcs_write32(channel->addr, 0x0000, 0x300, 0x0c40);

		ret = r8a78000_eth_pcs_reg_wait(channel, 0x0040, 0x380, GENMASK(4, 2), 0x06 << 2);
		if (ret)
			return ret;

		r8a78000_eth_pcs_write32(channel->addr, 0x0000, 0x300, 0x0440);

		ret = r8a78000_eth_pcs_reg_wait(channel, 0x0040, 0x380, GENMASK(4, 2), 0x04 << 2);
		if (ret)
			return ret;

		r8a78000_eth_pcs_write32(channel->addr, 0x0014, 0x380, 0x0050);
		r8a78000_eth_pcs_write32(channel->addr, 0x00d8, 0x180, 0x3000);
		r8a78000_eth_pcs_write32(channel->addr, 0x00dc, 0x180, 0x0000);

		return 0;

	case PHY_INTERFACE_MODE_USXGMII:
		r8a78000_eth_pcs_write32(channel->addr, 0x001c, 0x300, 0x0000);
		r8a78000_eth_pcs_write32(channel->addr, 0x001c, 0x380, 0x0000);
		r8a78000_eth_pcs_write32(channel->addr, 0x0000, 0x380, 0x2200);
		r8a78000_eth_pcs_write32(channel->addr, 0x00f8, 0x180, 0x0022);
		r8a78000_eth_pcs_write32(channel->addr, 0x0248, 0x180, 0x0028);
		r8a78000_eth_pcs_write32(channel->addr, 0x0000, 0x300, 0x0c40);

		ret = r8a78000_eth_pcs_reg_wait(channel, 0x0040, 0x380, GENMASK(4, 2), 0x06 << 2);
		if (ret)
			return ret;

		r8a78000_eth_pcs_write32(channel->addr, 0x0000, 0x300, 0x0440);

		ret = r8a78000_eth_pcs_reg_wait(channel, 0x0040, 0x380, GENMASK(4, 2), 0x04 << 2);
		if (ret)
			return ret;

		r8a78000_eth_pcs_write32(channel->addr, 0x0014, 0x380, 0x0050);
		r8a78000_eth_pcs_write32(channel->addr, 0x00d8, 0x180, 0x1800);
		r8a78000_eth_pcs_write32(channel->addr, 0x00dc, 0x180, 0x0012);

		ret = r8a78000_eth_pcs_reg_wait(channel, 0x0080, 0x180, BIT(12), BIT(12));
		if (ret)
			return ret;

		r8a78000_eth_pcs_write32(channel->addr, 0x0170, 0x180, 0x1000);

		ret = r8a78000_eth_pcs_reg_wait(channel, 0x0260, 0x180, BIT(12), BIT(12));
		if (ret)
			return ret;

		r8a78000_eth_pcs_write32(channel->addr, 0x0170, 0x180, 0x0000);

		return 0;

	default:
		return -EOPNOTSUPP;
	}
}

static int
r8a78000_eth_pcs_chan_setting(struct r8a78000_eth_pcs_channel *channel)
{
	int ret;

	switch (channel->phy_interface) {
	case PHY_INTERFACE_MODE_SGMII:
		if (!channel->aneg_on)
			return 0;

		/* For AN_ON */
		r8a78000_eth_pcs_write32(channel->addr, 0x0004, 0x1f80, 0x0005);
		r8a78000_eth_pcs_write32(channel->addr, 0x0000, 0x1f80, 0x2200);
		r8a78000_eth_pcs_write32(channel->addr, 0x0000, 0x1f00, 0x3140);

		ret = r8a78000_eth_pcs_reg_wait(channel, 0x0008, 0x1f80, BIT(0), BIT(0));
		if (ret)
			return ret;

		break;
	case PHY_INTERFACE_MODE_USXGMII:
		if (!channel->aneg_on)
			return 0;
		/* For AN_ON */
		r8a78000_eth_pcs_write32(channel->addr, 0x0004, 0x1f80, 0x0001);
		r8a78000_eth_pcs_write32(channel->addr, 0x0028, 0x1f80, 0x0001);
		r8a78000_eth_pcs_write32(channel->addr, 0x0000, 0x1f80, 0x2008);
		r8a78000_eth_pcs_write32(channel->addr, 0x0000, 0x1f00, 0x3140);

		ret = r8a78000_eth_pcs_reg_wait(channel, 0x0008, 0x1f80, BIT(0), BIT(0));
		if (ret)
			return ret;

		r8a78000_eth_pcs_write32(channel->addr, 0x0008, 0x1f80, 0x0000);

		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int
r8a78000_eth_pcs_chan_speed(struct r8a78000_eth_pcs_channel *channel)
{
	int ret;

	switch (channel->phy_interface) {
	case PHY_INTERFACE_MODE_SGMII:
		/* Do nothing */
		break;
	case PHY_INTERFACE_MODE_USXGMII:
		if (channel->speed == 10000)
			r8a78000_eth_pcs_write32(channel->addr, 0x0000, 0x1f00, 0x2140);
		else if (channel->speed == 2500)
			r8a78000_eth_pcs_write32(channel->addr, 0x0000, 0x1f00, 0x0120);
		else
			return -EOPNOTSUPP;

		r8a78000_eth_pcs_write32(channel->addr, 0x0000, 0x380, 0x2600);

		ret = r8a78000_eth_pcs_reg_wait(channel, 0x0000, 0x380, BIT(10), 0);
		if (ret)
			return ret;

		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int r8a78000_eth_pcs_monitor_linkup(struct r8a78000_eth_pcs_channel *channel)
{
	int i, ret;

	for (i = 0; i < R8A78000_ETH_PCS_NUM_RETRY_LINKUP; i++) {
		ret = r8a78000_eth_pcs_reg_wait(channel, 0x0004, 0x300,
						BIT(2), BIT(2));
		if (!ret)
			break;

		/* restart */
		r8a78000_eth_pcs_write32(channel->addr, 0x0144, 0x180, 0x0010);
		udelay(1);
		r8a78000_eth_pcs_write32(channel->addr, 0x0144, 0x180, 0x0000);
	}

	return ret;
}

static int r8a78000_eth_pcs_init(struct phy *p)
{
	struct r8a78000_eth_pcs_channel *channel = phy_get_drvdata(p);
	struct device *dev = &channel->dd->pdev->dev;
	int ret;

	if (!channel->mpphy) {
		dev_err(dev, "Failed to get mmphy\n");
		return -EINVAL;
	}

	ret = phy_set_mode_ext(channel->mpphy, PHY_MODE_ETHERNET,
			       channel->phy_interface);
	if (ret) {
		dev_err(dev, "Failed to set mode to mphy\n");
		return ret;
	}

	ret = phy_init(channel->mpphy);
	if (ret) {
		dev_err(dev, "Failed to init mp-phy\n");
		return ret;
	}

	ret = r8a78000_eth_pcs_init_ram(channel, channel->mpphy);
	if (ret)
		return ret;

	ret = r8a78000_eth_pcs_common_setting(channel);

	return ret;
}

static int r8a78000_eth_pcs_exit(struct phy *p)
{
	return 0;
}

static int r8a78000_eth_pcs_hw_init_late(struct r8a78000_eth_pcs_channel *channel)
{
	int ret;

	ret = r8a78000_eth_pcs_chan_setting(channel);
	if (ret)
		return ret;

	ret = r8a78000_eth_pcs_chan_speed(channel);
	if (ret)
		return ret;

	return r8a78000_eth_pcs_monitor_linkup(channel);
}

static int r8a78000_eth_pcs_power_on(struct phy *p)
{
	struct r8a78000_eth_pcs_channel *channel = phy_get_drvdata(p);

	if (channel->powered_on)
		return 0;

	int ret = r8a78000_eth_pcs_hw_init_late(channel);

	if (ret)
		return ret;

	channel->powered_on = true;

	return 0;
}

static int r8a78000_eth_pcs_power_off(struct phy *p)
{
	struct r8a78000_eth_pcs_channel *channel = phy_get_drvdata(p);

	channel->powered_on = false;
	return 0;
}

static int r8a78000_eth_pcs_set_mode(struct phy *p, enum phy_mode mode,
				     int submode)
{
	struct r8a78000_eth_pcs_channel *channel = phy_get_drvdata(p);

	if (mode != PHY_MODE_ETHERNET)
		return -EOPNOTSUPP;

	switch (submode) {
	case PHY_INTERFACE_MODE_GMII:
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_USXGMII:
		channel->phy_interface = submode;
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int r8a78000_eth_pcs_set_speed(struct phy *p, int speed)
{
	struct r8a78000_eth_pcs_channel *channel = phy_get_drvdata(p);

	channel->speed = speed;

	return 0;
}

static const struct phy_ops r8a78000_eth_pcs_ops = {
	.init		= r8a78000_eth_pcs_init,
	.exit		= r8a78000_eth_pcs_exit,
	.power_on	= r8a78000_eth_pcs_power_on,
	.power_off	= r8a78000_eth_pcs_power_off,
	.set_mode	= r8a78000_eth_pcs_set_mode,
	.set_speed	= r8a78000_eth_pcs_set_speed,
};

static int rsw3_get_mpphy_nodes(struct r8a78000_eth_pcs_drv_data *dd)
{
	struct device *dev = &dd->pdev->dev;
	struct device_node *ports, *port;
	u32 index;
	int err;

	ports = of_get_child_by_name(dev->of_node, "ports");

	if (!ports)
		return -EINVAL;

	for_each_child_of_node(ports, port) {
		struct phy *mpphy;

		err = of_property_read_u32(port, "reg", &index);
		if (err < 0)
			continue;

		mpphy = devm_of_phy_get_by_index(dev, port, 0);
		if (IS_ERR(mpphy)) {
			err = PTR_ERR(mpphy);
			if (err == -EPROBE_DEFER) {
				dev_dbg(dev, "PCS: mmphy not ready, defer probe\n");
				goto out;
			}
		} else {
			dd->channel[index].mpphy = mpphy;
		}
	}

	err = 0;
out:
	of_node_put(ports);

	return err;
}

static struct phy *r8a78000_eth_pcs_xlate(struct device *dev,
					  const struct of_phandle_args *args)
{
	struct r8a78000_eth_pcs_drv_data *dd = dev_get_drvdata(dev);

	if (args->args[0] >= R8A78000_ETH_PCS_NUM)
		return ERR_PTR(-ENODEV);

	return dd->channel[args->args[0]].phy;
}

static const struct of_device_id r8a78000_eth_pcs_of_table[] = {
	{ .compatible = "renesas,r8a78000-ether-pcs", },
	{ }
};
MODULE_DEVICE_TABLE(of, r8a78000_eth_pcs_of_table);

static int r8a78000_eth_pcs_probe(struct platform_device *pdev)
{
	struct r8a78000_eth_pcs_drv_data *dd;
	struct resource *res;
	struct phy_provider *provider;
	int i, ret;
	static u8 rst_control_get_retries = 5;

	dd = devm_kzalloc(&pdev->dev, sizeof(*dd), GFP_KERNEL);
	if (!dd)
		return -ENOMEM;

	platform_set_drvdata(pdev, dd);
	dd->pdev = pdev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;

	dd->addr = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (!dd->addr)
		return -ENOMEM;

	/* Create PHY channels */
	for (i = 0; i < R8A78000_ETH_PCS_NUM; i++) {
		struct r8a78000_eth_pcs_channel *channel = &dd->channel[i];

		channel->phy = devm_phy_create(&pdev->dev, NULL, &r8a78000_eth_pcs_ops);
		if (IS_ERR(channel->phy))
			return PTR_ERR(channel->phy);

		channel->addr = dd->addr + R8A78000_ETH_PCS_OFFSET * i;
		channel->dd = dd;
		channel->index = i;
		channel->aneg_on = true;
		phy_set_drvdata(channel->phy, channel);
	}

	ret = rsw3_get_mpphy_nodes(dd);
	if (ret == -EPROBE_DEFER)
		return ret;

	provider = devm_of_phy_provider_register(&pdev->dev, r8a78000_eth_pcs_xlate);
	if (IS_ERR(provider))
		return PTR_ERR(provider);

	pm_runtime_enable(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);

	/* Get reset control */
	for (int i = 0; i < R8A78000_ETH_PCS_NUM - 1; ++i) {
		char rst_name[16] = {0};

		sprintf(rst_name, "pcs%d", i);
		dd->resets[i] = devm_reset_control_get(&pdev->dev, rst_name);
		if (IS_ERR(dd->resets[i])) {
			dev_dbg(&pdev->dev, "Failed to get reset control pcs%d, retries: %d\n",
				i, rst_control_get_retries);
			if (rst_control_get_retries) {
				--rst_control_get_retries;
				return -EPROBE_DEFER;
			}
			rst_control_get_retries = 0;
			return PTR_ERR(dd->resets[0]);
		}
	}

	/* Get clocks */
	dd->num_clks = devm_clk_bulk_get_all(&pdev->dev, &dd->clks);
	if (dd->num_clks < 1) {
		dev_err(&pdev->dev, "Failed to get pcs clocks\n");
		return -ENODEV;
	}

	ret = clk_bulk_prepare_enable(dd->num_clks, dd->clks);
	if (ret) {
		dev_err(&pdev->dev, "Failed to enable bulk clocks: %d\n", ret);
		return ret;
	}

	return 0;
}

static void r8a78000_eth_pcs_remove(struct platform_device *pdev)
{
	pm_runtime_put(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	platform_set_drvdata(pdev, NULL);
}

static int pcs_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct r8a78000_eth_pcs_drv_data *dd = platform_get_drvdata(pdev);

	for (int i = 0; i < R8A78000_ETH_PCS_NUM; i++) {
		struct r8a78000_eth_pcs_channel *channel = &dd->channel[i];

		if (channel->mpphy) {
			phy_power_off(channel->mpphy);
			phy_exit(channel->mpphy);
		}
	}

	clk_bulk_disable_unprepare(dd->num_clks, dd->clks);

	return 0;
}

static int pcs_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct r8a78000_eth_pcs_drv_data *dd = platform_get_drvdata(pdev);
	int ret;

	ret = clk_bulk_prepare_enable(dd->num_clks, dd->clks);
	if (ret) {
		dev_err(&pdev->dev, "Failed to enable bulk clocks: %d\n", ret);
		return ret;
	}

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(pcs_pm_ops, pcs_suspend, pcs_resume);

static struct platform_driver r8a78000_eth_pcs_driver_platform = {
	.probe = r8a78000_eth_pcs_probe,
	.remove_new = r8a78000_eth_pcs_remove,
	.driver = {
		.name = "r8a78000_eth_pcs",
		.of_match_table = r8a78000_eth_pcs_of_table,
		.pm = &pcs_pm_ops,
	}
};
module_platform_driver(r8a78000_eth_pcs_driver_platform);
MODULE_LICENSE("GPL");
