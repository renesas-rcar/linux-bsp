// SPDX-License-Identifier: GPL-2.0
/* Renesas Ethernet Switch device driver
 *
 * Copyright (C) 2026 Renesas Electronics Corporation
 */

#include <linux/err.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/kernel.h>
#include <linux/net_tstamp.h>

#include "rswitch.h"
#include "rswitch_ethtool.h"

static int rswitch_get_ts_info(struct net_device *ndev, struct kernel_ethtool_ts_info *info)
{
	struct rswitch_device *rdev = netdev_priv(ndev);

	info->phc_index = ptp_clock_index(rdev->priv->ptp_priv->clock);
	info->so_timestamping = SOF_TIMESTAMPING_TX_SOFTWARE |
				SOF_TIMESTAMPING_TX_HARDWARE |
				SOF_TIMESTAMPING_SOFTWARE |
				SOF_TIMESTAMPING_TX_HARDWARE |
				SOF_TIMESTAMPING_RX_HARDWARE |
				SOF_TIMESTAMPING_RAW_HARDWARE;
	info->tx_types = BIT(HWTSTAMP_TX_OFF) | BIT(HWTSTAMP_TX_ON);
	info->rx_filters = BIT(HWTSTAMP_FILTER_NONE) | BIT(HWTSTAMP_FILTER_ALL);

	return 0;
}

static const struct ethtool_ops rswitch_ethtool_ops = {
	.get_ts_info = rswitch_get_ts_info,
	.get_link_ksettings = phy_ethtool_get_link_ksettings,
	.set_link_ksettings = phy_ethtool_set_link_ksettings,
};

void rswitch_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &rswitch_ethtool_ops;
}
