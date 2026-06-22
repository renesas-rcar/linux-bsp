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

static void rswitch_get_channels(struct net_device *dev,
				 struct ethtool_channels *chan)
{
	struct rswitch_device *rdev = netdev_priv(dev);

	chan->rx_count = rdev->etha->num_rx_prios_used;
	chan->tx_count = rdev->etha->num_tx_prios_used;
	chan->combined_count = rdev->etha->num_tx_prios_used + rdev->etha->num_tx_prios_used;
	chan->max_rx = RSWITCH_PRIOS;
	chan->max_tx = RSWITCH_PRIOS;
	chan->max_combined = RSWITCH_PRIOS * 2;
}

static int rswitch_set_channels(struct net_device *dev,
				struct ethtool_channels *chan)
{
	struct rswitch_device *rdev = netdev_priv(dev);

	if (chan->rx_count > RSWITCH_PRIOS ||
	    chan->tx_count > RSWITCH_PRIOS ||
	    !chan->rx_count || !chan->tx_count)
		return -EINVAL;

	rdev->etha->num_rx_prios_used = chan->rx_count;
	rdev->etha->num_tx_prios_used = chan->tx_count;

	return 0;
}

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
	.get_channels = rswitch_get_channels,
	.set_channels = rswitch_set_channels,
	.get_link_ksettings = phy_ethtool_get_link_ksettings,
	.set_link_ksettings = phy_ethtool_set_link_ksettings,
};

void rswitch_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &rswitch_ethtool_ops;
}
