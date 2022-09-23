// SPDX-License-Identifier: GPL-2.0
/* PHY Marvell 88Q2110 device driver
 *
 * Copyright (C) 2022 Renesas Electronics Corporation
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/errno.h>
#include <linux/unistd.h>
#include <linux/hwmon.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/phy.h>
#include <linux/marvell_phy.h>
#include <linux/bitfield.h>
#include <linux/of.h>

#include <linux/io.h>
#include <asm/irq.h>
#include <linux/uaccess.h>

#define MV_88Q2110_AN_DISABLE	0
#define MV_88Q2110_AN_RESET	BIT(15)
#define MV_88Q2110_AN_ENABLE	BIT(12)
#define MV_88Q2110_AN_RESTART	BIT(9)

static void mv88q2110_ge_setting(struct phy_device *phydev)
{
	u16 val;

	/* FIXME: Remove registers access that are not available on 88Q2110 */
	phy_write_mmd(phydev, 1, 0x0900, 0x4000);

	val = phy_read_mmd(phydev, 1, 0x0834);
	val = (val & 0xfff0) | 0x01;
	phy_write_mmd(phydev, 1, 0x0834, val);

	phy_write_mmd(phydev, 3, 0xffe4, 0x07b5);
	phy_write_mmd(phydev, 3, 0xffe4, 0x06b6);
	mdelay(5);

	phy_write_mmd(phydev, 3, 0xffde, 0x402f);
	phy_write_mmd(phydev, 3, 0xfe2a, 0x3c3d);
	phy_write_mmd(phydev, 3, 0xfe34, 0x4040);
	phy_write_mmd(phydev, 3, 0xfe4b, 0x9337);
	phy_write_mmd(phydev, 3, 0xfe2a, 0x3c1d);
	phy_write_mmd(phydev, 3, 0xfe34, 0x0040);
	phy_write_mmd(phydev, 7, 0x8032, 0x0064);
	phy_write_mmd(phydev, 7, 0x8031, 0x0a01);
	phy_write_mmd(phydev, 7, 0x8031, 0x0c01);
	phy_write_mmd(phydev, 3, 0xfe0f, 0x0000);
	phy_write_mmd(phydev, 3, 0x800c, 0x0000);
	phy_write_mmd(phydev, 3, 0x801d, 0x0800);
	phy_write_mmd(phydev, 3, 0xfc00, 0x01c0);
	phy_write_mmd(phydev, 3, 0xfc17, 0x0425);
	phy_write_mmd(phydev, 3, 0xfc94, 0x5470);
	phy_write_mmd(phydev, 3, 0xfc95, 0x0055);
	phy_write_mmd(phydev, 3, 0xfc19, 0x08d8);
	phy_write_mmd(phydev, 3, 0xfc1a, 0x0110);
	phy_write_mmd(phydev, 3, 0xfc1b, 0x0a10);
	phy_write_mmd(phydev, 3, 0xfc3a, 0x2725);
	phy_write_mmd(phydev, 3, 0xfc61, 0x2627);
	phy_write_mmd(phydev, 3, 0xfc3b, 0x1612);
	phy_write_mmd(phydev, 3, 0xfc62, 0x1c12);
	phy_write_mmd(phydev, 3, 0xfc9d, 0x6367);
	phy_write_mmd(phydev, 3, 0xfc9e, 0x8060);
	phy_write_mmd(phydev, 3, 0xfc00, 0x01c8);
	phy_write_mmd(phydev, 3, 0x8000, 0x0000);
	phy_write_mmd(phydev, 3, 0x8016, 0x0011);

	phy_write_mmd(phydev, 3, 0xfda3, 0x1800);

	phy_write_mmd(phydev, 3, 0xfe02, 0x00c0);
	phy_write_mmd(phydev, 3, 0xffdb, 0x0010);
	phy_write_mmd(phydev, 3, 0xfff3, 0x0020);
	phy_write_mmd(phydev, 3, 0xfe40, 0x00a6);

	phy_write_mmd(phydev, 3, 0xfe60, 0x0000);
	phy_write_mmd(phydev, 3, 0xfe04, 0x0008);
	phy_write_mmd(phydev, 3, 0xfe2a, 0x3c3d);
	phy_write_mmd(phydev, 3, 0xfe4b, 0x9334);

	phy_write_mmd(phydev, 3, 0xfc10, 0xf600);
	phy_write_mmd(phydev, 3, 0xfc11, 0x073d);
	phy_write_mmd(phydev, 3, 0xfc12, 0x000d);
	phy_write_mmd(phydev, 3, 0xfc13, 0x0010);
}

static void mv88q2110_ge_soft_reset(struct phy_device *phydev)
{
	u16 val;

	/* FIXME: Remove registers access that are not available on 88Q2110 */
	phy_write_mmd(phydev, 3, 0xFFF3, 0x0024);

	val = phy_read_mmd(phydev, 1, 0);
	phy_write_mmd(phydev, 1, 0, val | 0x0800);

	phy_write_mmd(phydev, 3, 0xfff3, 0x020);
	phy_write_mmd(phydev, 3, 0xffe4, 0x0c);
	mdelay(1);

	phy_write_mmd(phydev, 3, 0xffe4, 0x06b6);

	phy_write_mmd(phydev, 1, 0, val & 0xf7ff);
	mdelay(1);

	phy_write_mmd(phydev, 3, 0xfc47, 0x0030);
	phy_write_mmd(phydev, 3, 0xfc47, 0x0031);
	phy_write_mmd(phydev, 3, 0xfc47, 0x0030);
	phy_write_mmd(phydev, 3, 0xfc47, 0x0000);
	phy_write_mmd(phydev, 3, 0xfc47, 0x0001);
	phy_write_mmd(phydev, 3, 0xfc47, 0x0000);

	phy_write_mmd(phydev, 3, 0x0900, 0x8000);

	phy_write_mmd(phydev, 1, 0x0900, 0x0000);
	phy_write_mmd(phydev, 3, 0xffe4, 0x000c);
}

static int mv88q2110_probe(struct phy_device *phydev)
{
	return 0;
}

static int mv88q2110_config_init(struct phy_device *phydev)
{
	u16 val;

	/* Set as Master */
	val = phy_read_mmd(phydev, 1, 0x0834);
	phy_write_mmd(phydev, 1, 0x0834, val | BIT(14));

	mv88q2110_ge_setting(phydev);

	/* Set to default compliant mode setting */
	phy_write_mmd(phydev, 3, 0xfdb8, 0);
	phy_write_mmd(phydev, 3, 0xfd3d, 0);
	phy_write_mmd(phydev, 1, 0x0902, 0x02);

	mv88q2110_ge_soft_reset(phydev);

	return 0;
}

static int mv88q2110_config_aneg(struct phy_device *phydev)
{
	if (phydev->autoneg == AUTONEG_ENABLE)
		phy_write_mmd(phydev, 7, 0x0200, MV_88Q2110_AN_ENABLE | MV_88Q2110_AN_RESTART);
	else
		phy_write_mmd(phydev, 7, 0x0200, MV_88Q2110_AN_DISABLE);

	return 0;
}

static int mv88q2110_read_status(struct phy_device *phydev)
{
	u16 val;

	phydev->link = 0;
	phydev->speed = 0;
	phydev->duplex = DUPLEX_UNKNOWN;

	val = phy_read_mmd(phydev, 3, 0x0901);

	if (val & BIT(2)) {
		phydev->link = 1;
		phydev->speed = 1000;
		phydev->duplex = DUPLEX_FULL;
	}

	return 0;
}

static struct phy_driver mv88q2110_drivers[] = {
	{
		.phy_id = MARVELL_PHY_ID_88Q2110,
		.phy_id_mask = MARVELL_PHY_ID_MASK,
		.name = "Marvell 88Q2110",
		.probe = mv88q2110_probe,
		.config_init = mv88q2110_config_init,
		.config_aneg = mv88q2110_config_aneg,
		.read_status = mv88q2110_read_status,
	},
};

module_phy_driver(mv88q2110_drivers);

static struct mdio_device_id __maybe_unused mv88q211x_tbl[] = {
	{ MARVELL_PHY_ID_88Q2110, MARVELL_PHY_ID_MASK },
	{ },
};

MODULE_DEVICE_TABLE(mdio, mv88q211x_tbl);
