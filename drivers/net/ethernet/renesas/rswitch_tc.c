// SPDX-License-Identifier: GPL-2.0
/* Renesas Ethernet Switch device driver
 *
 * Copyright (C) 2026 Renesas Electronics Corporation
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/etherdevice.h>
#include <linux/if_bridge.h>
#include <linux/if_vlan.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <net/pkt_cls.h>
#include <net/tc_act/tc_gact.h>

#include "rswitch.h"
#include "rswitch_tc.h"

static int tc_setup_cbs(struct rswitch_device *rdev,
			struct tc_cbs_qopt_offload *qopt)
{
	/* The following equations were used to calculate the register values:
	 * hicredit = max_interference_size * idle_slope / port_transmit_rate
	 * locredit = max_frame_size * send_slope / port_transmit_rate

	 * (1) CIV[byte/cycle] = ((portTransmitRate[bps] / 8) *
	 *     (bandwidthFraction[%] / 100)) / clk_f[Hz] * (CycleTime / GateOpenTime)
	 * For example, when portTransmitRate is 1Gbps, bandwidthFraction is 25%
	 * and clk_f is 200MHz,
	 * CIV is 0.15625. CIV[19:16] = 0 CIV[15:0] = 0.15625 * 2^16 = 10,240 = 2800H
	 * (2) CUL[byte] = (maxInterferenceSize[bit] * clk_f[Hz] / portTransmitRate[bps] +
	 *     requestDelay[cyc]) * CIV[byte/cycle]
	 */

	s64 port_transmit_rate_kbps, port_transmit_rate_mbps, value, tmp_val;
	u32 clk_rate = 0, civ, cul, x, bandwidth_fraction;
	struct rswitch_private *priv = rdev->priv;
	struct net_device *ndev = rdev->ndev;
	u32 queue = qopt->queue;
	bool relaxed;

	relaxed = of_property_read_bool(priv->pdev->dev.of_node, "renesas,mdc-relaxed");

	if (priv->clk)
		clk_rate = clk_get_rate(priv->clk);

	if (!clk_rate) {
		clk_rate = relaxed ? RSWITCH3_FALLBACK_CLK_RELAX : RSWITCH3_FALLBACK_CLK_SPEC;
		netdev_warn(ndev,
			    "clk_get_rate() returned 0, using fallback %u.%06u MHz%s\n",
			    clk_rate / 1000000, clk_rate % 1000000,
			    relaxed ? " (relaxed)" : " (specific)");
	}

	port_transmit_rate_kbps = qopt->idleslope - qopt->sendslope;

	if (!qopt->enable) {
		x = FIELD_GET(CE, ioread32(rdev->etha->addr + EACOEM));
		x = x & ~BIT(queue);
		iowrite32(FIELD_PREP(CE, x), rdev->etha->addr + EACAEC);
		iowrite32(FIELD_PREP(CC, BIT(queue)), rdev->etha->addr + EACC);
		return 0;
	}

	/* Port Transmit Rate and Speed Divider */
	switch (div_s64(port_transmit_rate_kbps, 1000)) {
	case SPEED_10000:
	case SPEED_5000:
	case SPEED_2500:
	case SPEED_1000:
	case SPEED_100:
		break;
	default:
		netdev_err(ndev,
			   "Invalid portTransmitRate %lld (idleSlope - sendSlope)\n",
			   port_transmit_rate_kbps);
		return -EINVAL;
	}

	bandwidth_fraction = div_s64(qopt->idleslope * 100, port_transmit_rate_kbps);

	port_transmit_rate_mbps = (port_transmit_rate_kbps * 1000) >> 3;
	value = (port_transmit_rate_mbps * bandwidth_fraction / 100) << 16;
	civ = div_s64(value, clk_rate);

	if (civ > FIELD_MAX(CIV)) {
		netdev_err(ndev,
			   "CBS: hardware clocked at %u Hz can't support %llu bytes-per-second\n",
			   clk_rate, port_transmit_rate_kbps * 1000);
		return -EINVAL;
	}

	if (!civ) {
		netdev_err(ndev, "CBS: values out of bounds\n");

		return -EINVAL;
	}

	value = (s64)qopt->hicredit * clk_rate;
	tmp_val = (s64)qopt->idleslope * 1000;
	x = div_s64(value, tmp_val);
	x = (x + 50) * civ;

	cul = min_t(u32, x, FIELD_MAX(CUL));
	if (cul < x) {
		netdev_warn(ndev,
			    "CBS: truncated max_size from %u to %u bytes due to hw limitations\n",
			    x, (u32)FIELD_MAX(CUL));
	}

	x = FIELD_GET(CE, ioread32(rdev->etha->addr + EACOEM));
	x = x & ~BIT(queue);
	iowrite32(FIELD_PREP(CE, x), rdev->etha->addr + EACAEC);
	iowrite32(FIELD_PREP(CC, BIT(queue)), rdev->etha->addr + EACC);

	iowrite32(FIELD_PREP(CIV, civ), rdev->etha->addr + EACAIVC(queue));
	iowrite32(FIELD_PREP(CUL, cul), rdev->etha->addr + EACAULC(queue));
	iowrite32(FIELD_PREP(CE, BIT(queue)), rdev->etha->addr + EACAEC);
	iowrite32(FIELD_PREP(CC, BIT(queue)), rdev->etha->addr + EACC);

	netdev_info(ndev, "CBS queue %d: send %d, idle %d, hi %d, lo %d\n",
		    queue, qopt->sendslope, qopt->idleslope,
		    qopt->hicredit, qopt->locredit);
	return 0;
}

static int rswitch_tc_mqprio_add(struct net_device *ndev, u8 num_tc)
{
	int i;

	if (num_tc != ndev->real_num_tx_queues) {
		netdev_err(ndev, "Only %d traffic classes supported\n",
			   ndev->real_num_tx_queues);
		return -EINVAL;
	}

	netdev_set_num_tc(ndev, num_tc);

	for (i = 0; i < num_tc; i++)
		netdev_set_tc_queue(ndev, i, 1, i);

	netdev_dbg(ndev, "dev->num_tc %u dev->real_num_tx_queues %u\n",
		   ndev->num_tc, ndev->real_num_tx_queues);

	return 0;
}

static int rswitch_tc_mqprio_del(struct net_device *ndev)
{
	netdev_reset_tc(ndev);

	netdev_dbg(ndev, "dev->num_tc %u dev->real_num_tx_queues %u\n",
		   ndev->num_tc, ndev->real_num_tx_queues);

	return 0;
}

static int rswitch_tc_setup_qdisc_mqprio(struct net_device *ndev,
					 struct tc_mqprio_qopt_offload *m)
{
	struct rswitch_device *rdev = netdev_priv(ndev);
	struct rswitch_etha *etha = rdev->etha;
	u8 num_tc = m->qopt.num_tc;
	int err;

	m->qopt.hw = TC_MQPRIO_HW_OFFLOAD_TCS;

	if (num_tc == 0)
		err = rswitch_tc_mqprio_del(ndev);
	else
		err = rswitch_tc_mqprio_add(ndev, num_tc);
	if (err)
		return err;

	etha->mm_tc_offload = num_tc > 0;
	etha->mm_preemptible_tcs = 0;
	if (num_tc)
		etha->mm_preemptible_tcs = m->preemptible_tcs &
					   GENMASK(num_tc - 1, 0);
	rswitch_mm_update(rdev);

	return 0;
}

int rswitch_setup_tc(struct net_device *ndev, enum tc_setup_type type,
		     void *type_data)
{
	struct rswitch_device *rdev = netdev_priv(ndev);

	switch (type) {
	case TC_SETUP_QDISC_MQPRIO:
		return rswitch_tc_setup_qdisc_mqprio(ndev, type_data);
	case TC_SETUP_QDISC_CBS:
		return tc_setup_cbs(rdev, type_data);
	default:
		return -EOPNOTSUPP;
	}
}
