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
	RSWITCH_STAT("rx_over_errors", r_stats.mrovfc),
	RSWITCH_STAT("rx_header_crc_errors", r_stats.mrhcrcec),
	RSWITCH_STAT("rx_broadcast", r_stats.mrbfc),
	RSWITCH_STAT("rx_multicast", r_stats.mrmfc),
	RSWITCH_STAT("rx_unicast", r_stats.mrufc),
	RSWITCH_STAT("rx_phy_errors", r_stats.mrpefc),
	RSWITCH_STAT("rx_nibble_err_frm_cnt", r_stats.mrnefc),
	RSWITCH_STAT("rx_fcs_errors", r_stats.mrfmefc),
	RSWITCH_STAT("rx_ff_missing_errors", r_stats.mrffmefc),
	RSWITCH_STAT("rx_c-frag_cnt_errors", r_stats.mrcfcefc),
	RSWITCH_STAT("rx_frag_cnt_errors", r_stats.mrfcefc),
	RSWITCH_STAT("rx_RMAC_filter_errors", r_stats.mrrcfefc),
	RSWITCH_STAT("rx_frames", r_stats.mrfc),
	RSWITCH_STAT("rx_good_undersize_err", r_stats.mrguefc),
	RSWITCH_STAT("rx_bad_undersize_err", r_stats.mrbuefc),
	RSWITCH_STAT("rx_good_oversize_err", r_stats.mrgoefc),
	RSWITCH_STAT("rx_bad_oversize_err", r_stats.mrboefc),
	RSWITCH_STAT("rx_byte_e-frm_cnt", r_stats.mrxbceu),
	RSWITCH_STAT("rx_byte_p-frm_cnt", r_stats.mrxbcpu),

	RSWITCH_STAT("tx_bytes_e_frame", r_stats.mtxbceu),
	RSWITCH_STAT("tx_bytes_p_frame", r_stats.mtxbcpu),
	RSWITCH_STAT("tx_broadcast", r_stats.mtbfc),
	RSWITCH_STAT("tx_multicast", r_stats.mtmfc),
	RSWITCH_STAT("tx_unicast", r_stats.mtufc),
	RSWITCH_STAT("tx_errors", r_stats.mtefc),
	RSWITCH_STAT("tx_byte_e-frm_cnt", r_stats.mtxbceu),
	RSWITCH_STAT("tx_byte_p-frm_cnt", r_stats.mtxbcpu),

	RSWITCH_NETDEV_STAT("tx_dropped", stats.tx_dropped),
	RSWITCH_NETDEV_STAT("collisions", stats.__collisions),
	RSWITCH_NETDEV_STAT("rx_frame_errors", stats.rx_frame_errors),
	RSWITCH_NETDEV_STAT("tx_fifo_errors", stats.tx_fifo_errors),
	RSWITCH_NETDEV_STAT("tx_heartbeat_errors", stats.tx_heartbeat_errors),
	RSWITCH_STAT("eth_agent_minimum_frm_size_err", e_stats.eausmfsecn),
	RSWITCH_STAT("eth_agent_TAG_flt_err", e_stats.eatfecn),
	RSWITCH_STAT("eth_agent_frm_size_err", e_stats.eafsecn),
	RSWITCH_STAT("eth_agent_dsq_overflow_err", e_stats.eadqoecn),
	RSWITCH_STAT("eth_agent_dsq_security_err", e_stats.eadqsecn),
	RSWITCH_STAT("eth_agent_chk_sum_err", e_stats.eacksecn),
	RSWITCH_STAT("eth_agent_lost_desc", e_stats.ealdcn),
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
