// SPDX-License-Identifier: GPL-2.0+
/* PHY driver for Qxxxx
 *
 * Copyright (C) 2025 Renesas Electronics Corporation
 */

#include <linux/bitfield.h>
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/hwmon.h>
#include <linux/marvell_phy.h>
#include <linux/phy.h>
#include <linux/sfp.h>
#include <linux/netdevice.h>

#define Q3XXX_PHY_ID                    0x002b0b20

static bool loopback_enable = false;
module_param(loopback_enable, bool, 0644);
MODULE_PARM_DESC(loopback_enable, "Enable internal PCS loopback for testing");

static int qxxxx_soft_reset(struct phy_device *phydev)
{
	int val = phy_read_mmd(phydev, 0x03, 0x0000);
	if (val < 0)
		return val;
	val |= BIT(15);
	phy_write_mmd(phydev, 0x03, 0x0000, val);
	return read_poll_timeout(phy_read_mmd, val, !(val & BIT(15)),
				10, 100000, false, phydev, 0x03, 0x0000);
}

static int qxxxx_set_loopback(struct phy_device *phydev, bool enable)
{
	int val = phy_read_mmd(phydev, 0x03, 0x0912);
	if (val < 0)
		return val;

	val &= ~BIT(14);
	if (enable)
		val |= BIT(14);

	return phy_write_mmd(phydev, 0x03, 0x0912, val);
}

static int qxxxx_set_usx_aneg_enable(struct phy_device *phydev, bool enable)
{
	int val = phy_read_mmd(phydev, 0x1e, 0x0964);
	if (val < 0)
		return val;

	val &= ~BIT(14);
	if (enable)
		val |= BIT(14);

	return phy_write_mmd(phydev, 0x1e, 0x0964, val);
}

static int qxxxx_enable_restart_aneg(struct phy_device *phydev)
{
	int val = phy_read_mmd(phydev, 0x07, 0x0000);
	if (val < 0)
		return val;

	val |= BIT(9);
	return phy_write_mmd(phydev, 0x07, 0x0000, val);
}

static int qxxxx_config_an_adv2(struct phy_device *phydev)
{
	int val = 0;
	val |= BIT(8);
	val |= BIT(9);
	val |= BIT(10);
	val |= BIT(4);
	return phy_write_mmd(phydev, 0x1e, 0x080C, val);
}

static int qxxxx_set_master_mode(struct phy_device *phydev, bool is_master)
{
	if (phydev->autoneg == AUTONEG_ENABLE)
		return 0;

	int val = phy_read_mmd(phydev, 0x01, 0x0834);
	if (val < 0)
		return val;
	val = is_master ? (val | BIT(14)) : (val & ~BIT(14));
	return phy_write_mmd(phydev, 0x01, 0x0834, val);
}

static int qxxxx_set_speed(struct phy_device *phydev, u8 speed_code)
{
	int val = phy_read_mmd(phydev, 0x03, 0x0000);
	if (val < 0)
		return val;

	val |= BIT(13);
	val |= BIT(6);
	val &= ~GENMASK(5, 2);
	val |= (speed_code << 2);
	return phy_write_mmd(phydev, 0x03, 0x0000, val);
}

static int qxxxx_config_aneg(struct phy_device *phydev)
{
	int ret;

	if (!phydev->autoneg)
		return 0;

	ret = qxxxx_set_usx_aneg_enable(phydev, true);
	if (ret)
		return ret;

	return 0;
}

static int qxxxx_config_init(struct phy_device *phydev)
{
	int ret, val;

	ret = qxxxx_soft_reset(phydev);
	if (ret)
		return ret;

	val = phy_read_mmd(phydev, 0x03, 0x0000);
	if (val >= 0 && (val & BIT(11))) {
		val &= ~BIT(11);
		phy_write_mmd(phydev, 0x03, 0x0000, val);
		qxxxx_soft_reset(phydev);
	}

	val = BIT(15) |
	      BIT(14) |
	      BIT(10) |
	      BIT(9) |
	      BIT(8) |
	      (1 << 7) |
	      (3 << 4);
	phy_write_mmd(phydev, 0x1E, 0x0964, val);

	ret = qxxxx_set_master_mode(phydev, true);
	if (ret)
		return ret;

	if (phydev->autoneg == AUTONEG_ENABLE) {
		ret = qxxxx_config_an_adv2(phydev);
		if (ret)
			return ret;

		ret = qxxxx_enable_restart_aneg(phydev);
		if (ret)
			return ret;
	} else {
		u8 speed_code;

		switch (phydev->speed) {
		case SPEED_10000:
			speed_code = 0x00;
			break;
		case SPEED_5000:
			speed_code = 0x08;
			break;
		case SPEED_2500:
			speed_code = 0x07;
			break;
		default:
			speed_code = 0x07;
		}

		ret = qxxxx_set_speed(phydev, speed_code);
		if (ret)
			return ret;
	}

	if (loopback_enable)
		qxxxx_set_loopback(phydev, true);

	val = phy_read_mmd(phydev, 0x03, 0x0000);
	if (val >= 0)
		dev_info(&phydev->mdio.dev, "[QXXXX] PCS_CTRL1 = 0x%04x\n", val);

	return 0;
}

static int qxxxx_read_status(struct phy_device *phydev)
{
	int val;

	phydev->link = 0;
	phydev->speed = SPEED_UNKNOWN;
	phydev->duplex = DUPLEX_UNKNOWN;

	val = phy_read_mmd(phydev, 0x03, 0x0913);
	if (val < 0)
		return val;

	if (val & BIT(2))
		phydev->link = 1;
	else
		return 0;

	val = phy_read_mmd(phydev, 0x03, 0x0000);
	if (((val >> 13) & 1) && ((val >> 6) & 1)) {
		switch ((val >> 2) & 0xF) {
		case 0x0:
			phydev->speed = SPEED_10000;
			break;
		case 0x8:
			phydev->speed = SPEED_5000;
			break;
		case 0x7:
			phydev->speed = SPEED_2500;
			break;
		}
	}

	phydev->duplex = DUPLEX_FULL;

	return 0;
}

static struct phy_driver QXXXX_driver[] = {
	{
		PHY_ID_MATCH_EXACT(Q3XXX_PHY_ID),
		.name		= "PHY QXXXX",
		.config_init	= qxxxx_config_init,
		.config_aneg	= qxxxx_config_aneg,
		.read_status	= qxxxx_read_status,
		.soft_reset	= qxxxx_soft_reset,
		.features	= PHY_10GBIT_FEATURES,
	},
};

module_phy_driver(QXXXX_driver);

MODULE_DESCRIPTION("Marvell QXXXX PHY driver");
MODULE_LICENSE("GPL");
