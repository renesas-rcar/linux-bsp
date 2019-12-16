// SPDX-License-Identifier: GPL-2.0+
/*
 * R-Car Gen3 HDMI PHY
 *
 * Copyright (C) 2016 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */

#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <drm/bridge/dw_hdmi.h>
#include <drm/drm_modes.h>

#define RCAR_HDMI_PHY_OPMODE_PLLCFG	0x06	/* Mode of operation and PLL dividers */
#define RCAR_HDMI_PHY_CKSYMTXCTRL	0x09	/* Clock Symbol and Transmitter Control Register */
#define RCAR_HDMI_PHY_VLEVCTRL		0x0e	/* Voltage Level Control Register */
#define RCAR_HDMI_PHY_PLLCURRGMPCTRL	0x10	/* PLL current and Gmp (conductance) */
#define RCAR_HDMI_PHY_PLLDIVCTRL	0x11	/* PLL dividers */
#define RCAR_HDMI_PHY_TXTERM		0x19	/* Transmission Termination Register */

struct rcar_hdmi_phy_params {
	unsigned long mpixelclock;
	u16 opmode_div;	/* Mode of operation and PLL dividers */
	u16 curr_gmp;	/* PLL current and Gmp (conductance) */
	u16 div;	/* PLL dividers */
};

struct rcar_hdmi_phy_params_2 {
	unsigned long mpixelclock;
	u16 clk;	/* Clock Symbol and Transmitter Control Register */
	u16 vol_level;	/* Voltage Level */
	u16 trans;	/* Transmission Termination Register */
};

static const struct rcar_hdmi_phy_params rcar_hdmi_phy_params[] = {
	{ 35500000,  0x0003, 0x0283, 0x0628 },
	{ 44900000,  0x0003, 0x0285, 0x0228 },
	{ 71000000,  0x0002, 0x1183, 0x0614 },
	{ 90000000,  0x0002, 0x1142, 0x0214 },
	{ 140250000, 0x0001, 0x20c0, 0x060a },
	{ 182750000, 0x0001, 0x2080, 0x020a },
	{ 281250000, 0x0000, 0x3040, 0x0605 },
	{ 297000000, 0x0000, 0x3041, 0x0205 },
	{ ~0UL,      0x0000, 0x0000, 0x0000 },
};

static const struct rcar_hdmi_phy_params_2 rcar_hdmi_phy_params_2[] = {
	{ 165000000,  0x8c88, 0x0180, 0x0007},
	{ 297000000,  0x83c8, 0x0180, 0x0004},
	{ ~0UL,       0x0000, 0x0000, 0x0000},
};

static enum drm_mode_status
rcar_hdmi_mode_valid(struct drm_connector *connector,
		     const struct drm_display_mode *mode)
{
	/*
	 * The maximum supported clock frequency is 297 MHz, as shown in the PHY
	 * parameters table.
	 */
	if (mode->clock > 297000)
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

static int rcar_hdmi_phy_configure(struct dw_hdmi *hdmi,
				   const struct dw_hdmi_plat_data *pdata,
				   unsigned long mpixelclock)
{
	const struct rcar_hdmi_phy_params *params = rcar_hdmi_phy_params;
	const struct rcar_hdmi_phy_params_2 *params_2 = rcar_hdmi_phy_params_2;

	for (; params->mpixelclock != ~0UL; ++params) {
		if (mpixelclock <= params->mpixelclock)
			break;
	}

	if (params->mpixelclock == ~0UL)
		return -EINVAL;

	dw_hdmi_phy_i2c_write(hdmi, params->opmode_div,
			      RCAR_HDMI_PHY_OPMODE_PLLCFG);
	dw_hdmi_phy_i2c_write(hdmi, params->curr_gmp,
			      RCAR_HDMI_PHY_PLLCURRGMPCTRL);
	dw_hdmi_phy_i2c_write(hdmi, params->div, RCAR_HDMI_PHY_PLLDIVCTRL);

	for (; params_2->mpixelclock != ~0UL; ++params_2) {
		if (mpixelclock <= params_2->mpixelclock)
			break;
	}

	if (params_2->mpixelclock == ~0UL)
		return -EINVAL;

	dw_hdmi_phy_i2c_write(hdmi, params_2->clk, RCAR_HDMI_PHY_CKSYMTXCTRL);
	dw_hdmi_phy_i2c_write(hdmi, params_2->vol_level,
			      RCAR_HDMI_PHY_VLEVCTRL);
	dw_hdmi_phy_i2c_write(hdmi, params_2->trans, RCAR_HDMI_PHY_TXTERM);

	return 0;
}

static const struct dw_hdmi_plat_data rcar_dw_hdmi_plat_data = {
	.mode_valid = rcar_hdmi_mode_valid,
	.configure_phy	= rcar_hdmi_phy_configure,
	.dev_type	= RCAR_HDMI,
};

static int rcar_dw_hdmi_probe(struct platform_device *pdev)
{
	struct dw_hdmi *hdmi;

	hdmi = dw_hdmi_probe(pdev, &rcar_dw_hdmi_plat_data);
	if (IS_ERR(hdmi))
		return PTR_ERR(hdmi);

	platform_set_drvdata(pdev, hdmi);

	return 0;
}

static int rcar_dw_hdmi_remove(struct platform_device *pdev)
{
	struct dw_hdmi *hdmi = platform_get_drvdata(pdev);

	dw_hdmi_remove(hdmi);

	return 0;
}

static const struct of_device_id rcar_dw_hdmi_of_table[] = {
	{ .compatible = "renesas,rcar-gen3-hdmi" },
	{ /* Sentinel */ },
};
MODULE_DEVICE_TABLE(of, rcar_dw_hdmi_of_table);

static struct platform_driver rcar_dw_hdmi_platform_driver = {
	.probe		= rcar_dw_hdmi_probe,
	.remove		= rcar_dw_hdmi_remove,
	.driver		= {
		.name	= "rcar-dw-hdmi",
		.of_match_table = rcar_dw_hdmi_of_table,
	},
};

module_platform_driver(rcar_dw_hdmi_platform_driver);

MODULE_AUTHOR("Laurent Pinchart <laurent.pinchart@ideasonboard.com>");
MODULE_DESCRIPTION("Renesas R-Car Gen3 HDMI Encoder Driver");
MODULE_LICENSE("GPL");
