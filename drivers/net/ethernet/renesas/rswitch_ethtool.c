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

enum {NETDEV_STATS, RSWITCH_STATS};

struct rswitch_stats {
	char stat_string[ETH_GSTRING_LEN];
	int type;
	int sizeof_stat;
	int stat_offset;
};

#define RSWITCH_STAT(str, m) {						\
	.stat_string = str,						\
	.type = RSWITCH_STATS,						\
	.sizeof_stat = sizeof_field(struct rswitch_device, m),		\
	.stat_offset = offsetof(struct rswitch_device, m),		\
}

#define RSWITCH_NETDEV_STAT(str, m) {					\
	.stat_string = str,						\
	.type = NETDEV_STATS,						\
	.sizeof_stat = sizeof_field(struct net_device, m),		\
	.stat_offset = offsetof(struct net_device, m),			\
}

static const struct rswitch_stats rswitch_gstrings_stats[] = {
	RSWITCH_STAT("rx_packets", stats.mrufc),
	RSWITCH_STAT("tx_packets", stats.mtufc),
	RSWITCH_STAT("rx_bytes_e_frame", stats.mrxbceu),
	RSWITCH_STAT("tx_bytes_e_frame", stats.mtxbceu),
	RSWITCH_STAT("rx_bytes_p_frame", stats.mrxbcpu),
	RSWITCH_STAT("tx_bytes_p_frame", stats.mtxbcpu),
	RSWITCH_STAT("rx_broadcast", stats.mrbfc),
	RSWITCH_STAT("tx_broadcast", stats.mtbfc),
	RSWITCH_STAT("rx_multicast", stats.mrmfc),
	RSWITCH_STAT("tx_multicast", stats.mtmfc),
	RSWITCH_STAT("rx_errors", stats.mrpefc),
	RSWITCH_STAT("tx_errors", stats.mtefc),
	RSWITCH_NETDEV_STAT("tx_dropped", stats.tx_dropped),
	/* { "multicast", RSWITCH_STATS, (stats.mprc) }, */
	/* { "collisions", RSWITCH_STATS, (stats.colc) }, */
	/* { "rx_length_errors", RSWITCH_STATS, (stats.rlerrc) }, */
	RSWITCH_NETDEV_STAT("rx_over_errors", stats.rx_over_errors),
	RSWITCH_STAT("rx_crc_errors", stats.mrfmefc),
	RSWITCH_NETDEV_STAT("rx_frame_errors", stats.rx_frame_errors),
	/* { "rx_no_buffer_count", RSWITCH_STATS, (stats.rnbc) }, */
	/* { "rx_missed_errors", RSWITCH_STATS, (stats.mpc) }, */
	/* { "tx_aborted_errors", RSWITCH_STATS, (stats.ecol) }, */
	/* { "tx_carrier_errors", RSWITCH_STATS, (stats.tncrs) }, */
	RSWITCH_NETDEV_STAT("tx_fifo_errors", stats.tx_fifo_errors),
	RSWITCH_NETDEV_STAT("tx_heartbeat_errors", stats.tx_heartbeat_errors),
	/* { "tx_window_errors", RSWITCH_STATS, (stats.latecol) }, */
	/* { "tx_abort_late_coll", RSWITCH_STATS, (stats.latecol) }, */
	/* { "tx_deferred_ok", RSWITCH_STATS, (stats.dc) }, */
	/* { "tx_single_coll_ok", RSWITCH_STATS, (stats.scc) }, */
	/* { "tx_multi_coll_ok", RSWITCH_STATS, (stats.mcc) }, */
	/* { "tx_timeout_count", RSWITCH_STATS, (tx_timeout_count) }, */
	/* { "tx_restart_queue", RSWITCH_STATS, (restart_queue) }, */
	/* { "rx_long_length_errors", RSWITCH_STATS, (stats.roc) }, */
	/* { "rx_short_length_errors", RSWITCH_STATS, (stats.ruc) }, */
	/* { "rx_align_errors", RSWITCH_STATS, (stats.algnerrc) }, */
	/* { "tx_tcp_seg_good", RSWITCH_STATS, (stats.tsctc) }, */
	/* { "tx_tcp_seg_failed", RSWITCH_STATS, (stats.tsctfc) }, */
	/* { "rx_flow_control_xon", RSWITCH_STATS, (stats.xonrxc) }, */
	/* { "rx_flow_control_xoff", RSWITCH_STATS, (stats.xoffrxc) }, */
	/* { "tx_flow_control_xon", RSWITCH_STATS, (stats.xontxc) }, */
	/* { "tx_flow_control_xoff", RSWITCH_STATS, (stats.xofftxc) }, */
	/* { "rx_long_byte_count", RSWITCH_STATS, (stats.gorcl) }, */
	/* { "rx_csum_offload_good", RSWITCH_STATS, (hw_csum_good) }, */
	/* { "rx_csum_offload_errors", RSWITCH_STATS, (hw_csum_err) }, */
	/* { "alloc_rx_buff_failed", RSWITCH_STATS, (alloc_rx_buff_failed) }, */
	/* { "tx_smbus", RSWITCH_STATS, (stats.mgptc) }, */
	/* { "rx_smbus", RSWITCH_STATS, (stats.mgprc) }, */
	/* { "dropped_smbus", RSWITCH_STATS, (stats.mgpdc) }, */
};

#define RSWITCH_QUEUE_STATS_LEN 0
#define RSWITCH_GLOBAL_STATS_LEN ARRAY_SIZE(rswitch_gstrings_stats)
#define RSWITCH_STATS_LEN (RSWITCH_GLOBAL_STATS_LEN + RSWITCH_QUEUE_STATS_LEN)

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

static void rswitch_get_ethtool_stats(struct net_device *ndev,
				      struct ethtool_stats *stats, u64 *data)
{
	const struct rswitch_stats *stat = rswitch_gstrings_stats;
	struct rswitch_device *rdev = netdev_priv(ndev);
	int i;

	rswitch_update_stats(rdev);
	for (i = 0; i < RSWITCH_GLOBAL_STATS_LEN; i++, stat++) {
		char *p;

		switch (stat->type) {
		case NETDEV_STATS:
			p = (char *)ndev + stat->stat_offset;
			break;
		case RSWITCH_STATS:
			p = (char *)rdev + stat->stat_offset;
			break;
		default:
			netdev_WARN_ONCE(ndev, "Invalid Rswitch stat type: %u index %d\n",
					 stat->type, i);
			continue;
		}

		if (stat->sizeof_stat == sizeof(u64))
			data[i] += *(u64 *)p;
		else
			data[i] += *(u32 *)p;
	}
/* BUG_ON(i != E1000_STATS_LEN); */
}

static void rswitch_get_strings(struct net_device *ndev, u32 stringset,
				u8 *data)
{
	u8 *p = data;
	int i;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < RSWITCH_GLOBAL_STATS_LEN; i++) {
			memcpy(p, rswitch_gstrings_stats[i].stat_string,
			       ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}
		/* BUG_ON(p - data != E1000_STATS_LEN * ETH_GSTRING_LEN); */
		break;
	}
}

static int rswitch_get_sset_count(struct net_device *netdev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return RSWITCH_STATS_LEN;
	default:
		return -EOPNOTSUPP;
	}
}

static const struct ethtool_ops rswitch_ethtool_ops = {
	.get_ts_info		= rswitch_get_ts_info,
	.get_channels		= rswitch_get_channels,
	.set_channels		= rswitch_set_channels,
	.get_strings		= rswitch_get_strings,
	.get_ethtool_stats	= rswitch_get_ethtool_stats,
	.get_sset_count		= rswitch_get_sset_count,
	.get_link_ksettings	= phy_ethtool_get_link_ksettings,
	.set_link_ksettings	= phy_ethtool_set_link_ksettings,
};

void rswitch_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &rswitch_ethtool_ops;
}
