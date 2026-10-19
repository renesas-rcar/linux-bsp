// SPDX-License-Identifier: GPL-2.0
/* Renesas Ethernet Switch device driver
 *
 * Copyright (C) 2022-2026 Renesas Electronics Corporation
 */

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/etherdevice.h>
#include <linux/ip.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/net_tstamp.h>
#include <linux/of.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/rtnetlink.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/sys_soc.h>

#include "rswitch.h"
#include "rswitch_l2.h"
#include "rswitch_tc.h"
#include "rswitch_ethtool.h"

static bool parallel_mode;

static u8 cached_mac_addresses[RSWITCH_MAX_NUM_PORTS][ETH_ALEN];
static bool mac_cache_valid[RSWITCH_MAX_NUM_PORTS];
static int rswitch_rx_poll(struct napi_struct *napi, int budget);
static int rswitch_tx_poll(struct napi_struct *napi, int budget);

int rswitch_reg_wait(void __iomem *addr, u32 offs, u32 mask, u32 expected)
{
	u32 val;

	return readl_poll_timeout_atomic(addr + offs, val, (val & mask) == expected,
					 1, RSWITCH_TIMEOUT_US);
}

void rswitch_modify(void __iomem *addr, u32 reg, u32 clear, u32 set)
{
	iowrite32((ioread32(addr + reg) & ~clear) | set, addr + reg);
}

/* Common Agent block (COMA) */
static void rswitch_reset(struct rswitch_private *priv)
{
	if (!parallel_mode) {
		iowrite32(RR, priv->addr_coma + RRC);
		iowrite32(!RR, priv->addr_coma + RRC);
	} else {
		int count = 0;
		int mode;

		do {
			mode = FIELD_GET(OPC, ioread32(priv->addr_gwca + GWMS));
			if (mode == OPC_OPERATION)
				break;

			count++;
			if (!(count % 100))
				pr_info("rswitch wait for GWMS%d %d==%d\n",
					priv->info->gwca_index, mode, OPC_OPERATION);
			mdelay(10);
		} while (1);
	}
}

static void rswitch_clock_enable(struct rswitch_private *priv)
{
	iowrite32(RCEC_ACE_DEFAULT(priv->info->agent_index_gwca) | RCE,
		  priv->addr_coma + RCEC);
}

static void rswitch_clock_disable(struct rswitch_private *priv)
{
	iowrite32(RCD, priv->addr_coma + RCDC);
}

static bool rswitch_agent_clock_is_enabled(void __iomem *coma_addr,
					   unsigned int port)
{
	u32 val = ioread32(coma_addr + RCEC);

	if (val & RCE)
		return (val & BIT(port)) ? true : false;
	else
		return false;
}

static void rswitch_agent_clock_ctrl(void __iomem *coma_addr, unsigned int port,
				     int enable)
{
	u32 val;

	if (enable) {
		val = ioread32(coma_addr + RCEC);
		iowrite32(val | RCE | BIT(port), coma_addr + RCEC);
	} else {
		val = ioread32(coma_addr + RCDC);
		iowrite32(val | BIT(port), coma_addr + RCDC);
	}
}

static int rswitch2_bpool_config(struct rswitch_private *priv)
{
	u32 val;

	val = ioread32(priv->addr_coma + CABPIRM);
	if (val & BPR)
		return 0;

	iowrite32(BPIOG, priv->addr_coma + CABPIRM);

	return rswitch_reg_wait(priv->addr_coma, CABPIRM, BPR, BPR);
}

static int rswitch3_bpool_config(struct rswitch_private *priv)
{
	u32 val;

	val = ioread32(priv->addr_coma + CABPIRM_RSW3);
	if (val & BPR)
		return 0;

	iowrite32(BPIOG, priv->addr_coma + CABPIRM_RSW3);

	return rswitch_reg_wait(priv->addr_coma, CABPIRM_RSW3, BPR, BPR);
}

static void rswitch_coma_init(struct rswitch_private *priv)
{
	iowrite32(CABPPFLC_INIT_VALUE, priv->addr_coma + CABPPFLC0);
}

static void rswitch_top_init(struct rswitch_private *priv)
{
	u8 gwca_index = priv->info->gwca_index;
	unsigned int i, j;

	if (priv->info->rswitch_version == RSW20) {
		for (i = 0; i < RSWITCH_MAX_NUM_QUEUES; i++)
			iowrite32((i / 16) << (gwca_index * 8), priv->addr_mfab + TPEMIMC7(i));
		return;
	}

	for (i = 0; i < GWCA_NUM_IRQS; i++)
		memset(priv->irq[i].irq_bits, 0, sizeof(priv->irq[i].irq_bits));

	rswitch_for_each_enabled_port(priv, i) {
		struct rswitch_device *rdev = priv->rdev[i];
		unsigned int reg, bit;

		if (rdev->disabled)
			continue;

		for (j = 0; j < priv->info->num_tx_queues; j++) {
			struct rswitch_gwca_queue *gq = rdev->tx_queue[j];
			unsigned int irq_idx = j % GWCA_NUM_IRQS;

			iowrite32(TPDEMIMC_GDICM(gwca_index, irq_idx),
				  priv->addr_mfab + RSW3_TPDEMIMC(gq->index));

			reg = gq->index / 32;
			bit = BIT(gq->index % 32);
			priv->irq[irq_idx].irq_bits[reg] |= bit;
		}

		for (j = 0; j < priv->info->num_rx_queues; j++) {
			struct rswitch_gwca_queue *gq = rdev->rx_queue[j];
			unsigned int irq_idx = (j + priv->info->num_tx_queues) % GWCA_NUM_IRQS;

			iowrite32(TPDEMIMC_GDICM(gwca_index, irq_idx),
				  priv->addr_mfab + RSW3_TPDEMIMC(gq->index));

			reg = gq->index / 32;
			bit = BIT(gq->index % 32);
			priv->irq[irq_idx].irq_bits[reg] |= bit;
		}
	}
}

static void rswitch_fwd_allow_ports(struct rswitch_private *priv,
				    unsigned int src_port, u32 dst_mask)
{
	u32 all_ports_val = GENMASK(RSWITCH_NUM_AGENTS_RSW3 - 1, 0);

	if (!dst_mask || src_port >= RSWITCH_NUM_AGENTS_RSW3)
		return;

	dst_mask &= GENMASK(RSWITCH_NUM_AGENTS_RSW3 - 1, 0);

	rswitch_modify(priv->addr_mfwd, RSW3_FWPBFC0(src_port), all_ports_val,
		       field_prep(PBDV(RSWITCH_NUM_AGENTS_RSW3), dst_mask));
	rswitch_modify(priv->addr_mfwd, RSW3_FWPBFC1(src_port), PBRP,
		       FIELD_PREP(PBRP, PBR_PRIO_TSNA));
	rswitch_modify(priv->addr_mfwd, RSW3_FWPC2(src_port), 0,
		       field_prep(LTWFM(RSWITCH_NUM_AGENTS_RSW3), dst_mask));
	rswitch_modify(priv->addr_mfwd, RSW3_FWPC0(src_port), MACDSA, MACDSA);
}

void rswitch_enable_tsn_forwarding(struct rswitch_private *priv,
				   u32 tsnes_id, u32 rsw_port,
				   u32 requested_mask)
{
	struct device *dev = &priv->pdev->dev;
	u32 enabled_ports;
	u32 external_ports;
	u32 missing_ports;
	unsigned int i;

	enabled_ports = 0;
	for (i = 0; i < RSWITCH_NUM_PORTS_RSW3; i++) {
		if (!priv->rdev[i] || priv->rdev[i]->disabled)
			continue;

		enabled_ports |= BIT(i);
	}

	enabled_ports &= GENMASK(7, 0);
	requested_mask &= GENMASK(7, 0);

	if (requested_mask) {
		missing_ports = requested_mask & ~enabled_ports;
		if (missing_ports)
			dev_err(dev,
				"rswitch3 TSN-ES%u has no enabled in DTS\n", tsnes_id);

		external_ports = requested_mask & enabled_ports;
	} else {
		external_ports = enabled_ports;
	}

	external_ports &= ~BIT(rsw_port);

	if (!external_ports) {
		dev_err(dev,
			"rswitch3 TSN-ES%u has no enabled external port to forward from interface %u\n",
			tsnes_id, rsw_port);
		return;
	}

	if (!priv->rdev[rsw_port]->brdev) {
		rswitch_fwd_allow_ports(priv, rsw_port, external_ports);

		for (i = 0; i < RSWITCH_NUM_PORTS_RSW3; i++) {
			if (!(external_ports & BIT(i)))
				continue;

			rswitch_fwd_allow_ports(priv, i, BIT(rsw_port));
		}
	}
}

static void rswitch_attached_forwarding(struct rswitch_private *priv)
{
	u32 tsnes_id;

	for (tsnes_id = 0; tsnes_id < RSWITCH3_NUM_TSNES; tsnes_id++) {
		u32 rsw_port = RSWITCH3_TSNES_PORT_BASE + tsnes_id;

		if (!test_bit(tsnes_id, priv->tsnes_attached))
			continue;

		rswitch_enable_tsn_forwarding(priv, tsnes_id, rsw_port,
					      priv->tsnes_fwd_mask[tsnes_id]);
	}
}

/* Forwarding engine block (MFWD) */
static int rswitch2_fwd_init(struct rswitch_private *priv)
{
	u8 num_agents = priv->info->rswitch_num_agents;
	u32 all_ports_val = GENMASK(num_agents - 1, 0);
	u8 gwca_index = priv->info->gwca_index;
	u32 reg_val, ret, i;

	/* Start with empty configuration */
	for (i = 0; i < num_agents; i++) {
		/* Disable all port features */
		iowrite32(0, priv->addr_mfwd + FWPC0(i));
		/* Disallow L3 forwarding and direct descriptor forwarding */
		iowrite32(field_prep(LTHFM(num_agents), all_ports_val),
			  priv->addr_mfwd + FWPC1(i));
		/* Disallow L2 forwarding */
		iowrite32(field_prep(LTWFM(num_agents), all_ports_val),
			  priv->addr_mfwd + FWPC2(i));
		/* Disallow port based forwarding */
		iowrite32(0, priv->addr_mfwd + FWPBFC(i));
	}

	/* Configure MAC table aging */
	rswitch_modify(priv->addr_mfwd, FWMACAGUSPC, MACAGUSP,
		       FIELD_PREP(MACAGUSP, RSW_AGEING_CLK_PER_US));

	reg_val = FIELD_PREP(MACAGT, RSW_AGEING_TIME);
	reg_val |= MACAGE | MACAGSL;
	iowrite32(reg_val, priv->addr_mfwd + FWMACAGC);

	/* For enabled ETHA ports, setup port based forwarding */
	rswitch_for_each_enabled_port(priv, i) {
		/* Port based forwarding from port i to GWCA port */
		rswitch_modify(priv->addr_mfwd, FWPBFC(i), PBDV(num_agents),
			       field_prep(PBDV(num_agents), BIT(priv->gwca.agent_index)));
		/* Within GWCA port, forward to Rx queue for port i */
		iowrite32(priv->rdev[i]->rx_queue[0]->index,
			  priv->addr_mfwd + FWPBFCSDC(gwca_index, i));
	}

	/* For GWCA port, allow direct descriptor forwarding */
	rswitch_modify(priv->addr_mfwd, FWPC1(priv->gwca.agent_index), 0, DDE);

	/* Initialize hardware L2 forwarding table */

	/* Allow entire table to be used for "un-secure" entries */
	rswitch_modify(priv->addr_mfwd, FWMACHEC, 0, MACHMUE);

	/* Initialize MAC hash table */
	iowrite32(MACTIOG, priv->addr_mfwd + FWMACTIM);

	ret = rswitch_reg_wait(priv->addr_mfwd, FWMACTIM, MACTIOG, 0);
	if (ret)
		return ret;

	/* Allow entire VLAN table to be used for "un-secure" entries */
	iowrite32(VLANTMUE, priv->addr_mfwd + FWVLANTEC);

	/* Initialize VLAN table */
	iowrite32(VLANTIOG, priv->addr_mfwd + FWVLANTIM);

	return rswitch_reg_wait(priv->addr_mfwd, FWVLANTIM, VLANTIOG, 0);
}

static int rswitch3_fwd_init(struct rswitch_private *priv)
{
	u8 num_agents = priv->info->rswitch_num_agents;
	u32 all_ports_val = GENMASK(num_agents - 1, 0);
	u8 gwca_index = priv->info->gwca_index;
	unsigned int i;
	u32 reg_val;
	int ret;

	/* Start with empty configuration */
	for (i = 0; i < priv->info->rswitch_num_agents; i++) {
		/* Disable all port features */
		iowrite32(0, priv->addr_mfwd + RSW3_FWPC0(i));
		/* Disallow L3 forwarding and direct descriptor forwarding */
		iowrite32(field_prep(LTHFM(num_agents), all_ports_val),
			  priv->addr_mfwd + RSW3_FWPC1(i));
		/* Disallow L2 forwarding */
		iowrite32(field_prep(LTWFM(num_agents), all_ports_val),
			  priv->addr_mfwd + RSW3_FWPC2(i));
		/* Disable L3 forwarding mask all port */
		iowrite32(0, priv->addr_mfwd + RSW3_FWPC3(i));
		/* Disallow port based forwarding */
		iowrite32(0, priv->addr_mfwd + RSW3_FWPBFC0(i));
		iowrite32(0, priv->addr_mfwd + RSW3_FWPBFC1(i));
	}

	//	iowrite32(RSW3_FWSCR22_L2SCRSL + all_ports_val, priv->addr_mfwd + RSW3_FWSCR22);
	iowrite32(0xffffffff, priv->addr_mfwd + RSW3_FWSCR27);
	iowrite32(0xffffffff, priv->addr_mfwd + RSW3_FWSCR28);
	iowrite32(0xffffffff, priv->addr_mfwd + RSW3_FWSCR29);
	iowrite32(0xffffffff, priv->addr_mfwd + RSW3_FWSCR30);
	iowrite32(0xffffffff, priv->addr_mfwd + RSW3_FWSCR31);
	iowrite32(0xffffffff, priv->addr_mfwd + RSW3_FWSCR32);
	iowrite32(0xffffffff, priv->addr_mfwd + RSW3_FWSCR33);
	iowrite32(0xffffffff, priv->addr_mfwd + RSW3_FWSCR34);

	/* Start MAC table init */
	/* Set all possible MAC table entries to insecure */
	iowrite32(0x7ff, priv->addr_mfwd + RSW3_FWMACTEC0);

	/* reserve some of the MAC table for HW learning */
	rswitch_modify(priv->addr_mfwd, RSW3_FWMACHWLC0, 0,
		       FIELD_PREP(RSW3_MACTHWLEA, 0x7f) |
		       FIELD_PREP(RSW3_MACTHWLSA, 0x40));

	/* MAC table reset flow */
	iowrite32(MACTIOG, priv->addr_mfwd + RSW3_FWMACTIM);
	ret = rswitch_reg_wait(priv->addr_mfwd, RSW3_FWMACTIM, MACTR, MACTR);
	if (ret)
		return ret;

	/* For GWCA port, allow direct descriptor forwarding */
	rswitch_modify(priv->addr_mfwd, RSW3_FWPC1(priv->gwca.agent_index), 0, DDE);

	/* Configure MAC table aging */
	rswitch_modify(priv->addr_mfwd, RSW3_FWMACAGUSPC, RSW3_MACAGUSP,
		       field_prep(RSW3_MACAGUSP, RSW3_AGEING_CLK_PER_US));

	reg_val = field_prep(RSW3_MACAGT, RSW_AGEING_TIME);
	reg_val |= RSW3_MACAGE | RSW3_MACAGSL;
	iowrite32(reg_val, priv->addr_mfwd + RSW3_FWMACAGC);

	/* For enabled ETHA ports, setup port based forwarding */
	rswitch_for_each_enabled_port(priv, i) {
		if (priv->rdev[i]->disabled)
			continue;

		/* Within GWCA port, forward to Rx queue for port i */
		iowrite32(priv->rdev[i]->rx_queue[0]->index,
			  priv->addr_mfwd + RSW3_FWPBFCSDC(gwca_index, i));

		/* Port based forwarding from port i to GWCA port */
		rswitch_modify(priv->addr_mfwd, RSW3_FWPBFC0(i), PBDV(num_agents),
			       field_prep(PBDV(num_agents), BIT(priv->gwca.agent_index)));

		/* Allow L2 forwarding */
		iowrite32(RSW3_MACDSA, priv->addr_mfwd + RSW3_FWPC0(i));
		rswitch_modify(priv->addr_mfwd, RSW3_FWPC2(i),
			       field_prep(LTWFM(num_agents), BIT(priv->gwca.agent_index)), 0);
	}

	/* L3 forwarding/filtering/routing L3/L2 stream hash ID functions */
	iowrite32(0, priv->addr_mfwd + RSW3_FWIP4SC);

	/* L3/L2 stream filter insecure entry number */
	iowrite32(0x3ff, priv->addr_mfwd + RSW3_FWLTHTEC1);

	/* Init L3 table */
	iowrite32(RSW3_LTHTIOG, priv->addr_mfwd + RSW3_FWLTHTIM);

	/* Wait for completion */
	ret = rswitch_reg_wait(priv->addr_mfwd, RSW3_FWLTHTIM, LTHTR, LTHTR);
	if (ret)
		return ret;

	/* Allow access to frame update rules from "un-secure" APB */
	iowrite32(0xffffffff, priv->addr_mfwd + RSW3_FWSCR34);
	iowrite32(0xffffffff, priv->addr_mfwd + RSW3_FWSCR33);
	iowrite32(0xffffffff, priv->addr_mfwd + RSW3_FWSCR32);
	iowrite32(0xffffffff, priv->addr_mfwd + RSW3_FWSCR31);
	iowrite32(0xffffffff, priv->addr_mfwd + RSW3_FWSCR30);
	iowrite32(0xffffffff, priv->addr_mfwd + RSW3_FWSCR29);
	iowrite32(0xffffffff, priv->addr_mfwd + RSW3_FWSCR28);
	iowrite32(0xffffffff, priv->addr_mfwd + RSW3_FWSCR27);

	rswitch_attached_forwarding(priv);

	/* Initialize frame update rules table */
	iowrite32(RSW3_L23UTIOG, priv->addr_mfwd + RSW3_FWL23UTIM);
	ret = rswitch_reg_wait(priv->addr_mfwd, RSW3_FWL23UTIM, RSW3_L23UTR, RSW3_L23UTR);

	/* Allow entire VLAN table to be used for "un-secure" entries */
	iowrite32(RSW3_VLANTMUE, priv->addr_mfwd + RSW3_FWVLANTEC);

	/* Initialize VLAN table */
	iowrite32(RSW3_VLANTIOG, priv->addr_mfwd + RSW3_FWVLANTIM);

	return rswitch_reg_wait(priv->addr_mfwd, RSW3_FWVLANTIM, RSW3_VLANTIOG, 0);
}

/* Gateway CPU agent block (GWCA) */
int rswitch_gwca_change_mode(struct rswitch_private *priv,
			     enum rswitch_port_mode mode)
{
	int ret;

	if (!rswitch_agent_clock_is_enabled(priv->addr_gwca, priv->gwca.agent_index)) {
		spin_lock(&priv->agent_lock);
		rswitch_agent_clock_ctrl(priv->addr_gwca, priv->gwca.agent_index, 1);
		spin_unlock(&priv->agent_lock);
	}

	iowrite32(OPC_DISABLE, priv->addr_gwca + GWMC);

	ret = rswitch_reg_wait(priv->addr_gwca, GWMS, OPC, OPC_DISABLE);
	if (ret < 0)
		return ret;

	if (mode == OPC_DISABLE) {
		spin_lock(&priv->agent_lock);
		rswitch_agent_clock_ctrl(priv->addr_gwca, priv->gwca.agent_index, 0);
		spin_unlock(&priv->agent_lock);

		return ret;
	}

	iowrite32(mode, priv->addr_gwca + GWMC);

	return rswitch_reg_wait(priv->addr_gwca, GWMS, OPC, mode);
}

static int rswitch_gwca_mcast_table_reset(struct rswitch_private *priv)
{
	iowrite32(MTIOG, priv->addr_gwca + GWMTIRM);

	return rswitch_reg_wait(priv->addr_gwca, GWMTIRM, MTR, MTR);
}

static int rswitch_gwca_axi_ram_reset(struct rswitch_private *priv)
{
	iowrite32(ARIOG, priv->addr_gwca + GWARIRM);

	return rswitch_reg_wait(priv->addr_gwca, GWARIRM, ARR, ARR);
}

static bool rswitch_is_any_data_irq(struct rswitch_private *priv, u32 *dis, u32 *mask)
{
	unsigned int i;

	for (i = 0; i < RSWITCH_NUM_IRQ_REGS; i++) {
		if (dis[i] & mask[i])
			return true;
	}

	return false;
}

static void rswitch_get_data_irq_status(struct rswitch_private *priv, u32 *dis)
{
	unsigned int i;

	for (i = 0; i < RSWITCH_NUM_IRQ_REGS; i++) {
		dis[i] = ioread32(priv->addr_gwca + GWDIS(i));
		dis[i] &= ioread32(priv->addr_gwca + GWDIE(i));
	}
}

static void rswitch_enadis_data_irq(struct rswitch_private *priv,
				    unsigned int index, bool enable)
{
	u32 offs = enable ? GWDIE(index / 32) : GWDID(index / 32);

	iowrite32(BIT(index % 32), priv->addr_gwca + offs);
}

static void rswitch_ack_data_irq(struct rswitch_private *priv,
				 unsigned int index)
{
	u32 offs = GWDIS(index / 32);

	spin_lock(&priv->lock);
	iowrite32(BIT(index % 32), priv->addr_gwca + offs);
	spin_unlock(&priv->lock);
}

static unsigned int rswitch_next_queue_index(struct rswitch_gwca_queue *gq,
					     bool cur, unsigned int num)
{
	unsigned int index = cur ? gq->cur : gq->dirty;

	if (index + num >= gq->ring_size)
		index = (index + num) % gq->ring_size;
	else
		index += num;

	return index;
}

static unsigned int rswitch_get_num_cur_queues(struct rswitch_gwca_queue *gq)
{
	if (gq->cur >= gq->dirty)
		return gq->cur - gq->dirty;
	else
		return gq->ring_size - gq->dirty + gq->cur;
}

static bool rswitch_is_queue_rxed(struct rswitch_gwca_queue *gq)
{
	struct rswitch_ext_ts_desc *desc = &gq->rx_ring[gq->dirty];

	if ((desc->desc.die_dt & DT_MASK) != DT_FEMPTY)
		return true;

	return false;
}

static int rswitch_gwca_queue_alloc_rx_buf(struct rswitch_gwca_queue *gq,
					   unsigned int start_index,
					   unsigned int num)
{
	unsigned int i, index;

	for (i = 0; i < num; i++) {
		index = (i + start_index) % gq->ring_size;
		if (gq->rx_bufs[index])
			continue;
		gq->rx_bufs[index] = netdev_alloc_frag(RSWITCH_BUF_SIZE);
		if (!gq->rx_bufs[index])
			goto err;
	}

	return 0;

err:
	for (; i-- > 0; ) {
		index = (i + start_index) % gq->ring_size;
		skb_free_frag(gq->rx_bufs[index]);
		gq->rx_bufs[index] = NULL;
	}

	return -ENOMEM;
}

static void rswitch_gwca_queue_free(struct device *dev,
				    struct rswitch_gwca_queue *gq)
{
	unsigned int i;

	if (!gq->dir_tx) {
		if (gq->rx_ring) {
			dma_free_coherent(dev,
					  sizeof(struct rswitch_ext_ts_desc) *
					  (gq->ring_size + 1), gq->rx_ring, gq->ring_dma);
			gq->rx_ring = NULL;
		}
		for (i = 0; i < gq->ring_size; i++)
			if (gq->rx_bufs[i])
				skb_free_frag(gq->rx_bufs[i]);
		kfree(gq->rx_bufs);
		gq->rx_bufs = NULL;
	} else {
		if (gq->tx_ring) {
			dma_free_coherent(dev,
					  sizeof(struct rswitch_ext_desc) *
					  (gq->ring_size + 1), gq->tx_ring, gq->ring_dma);
			gq->tx_ring = NULL;
		}
		kfree(gq->skbs);
		gq->skbs = NULL;
		kfree(gq->unmap_addrs);
		gq->unmap_addrs = NULL;
	}
}

static void rswitch_gwca_ts_queue_free(struct rswitch_private *priv)
{
	struct rswitch_gwca_queue *gq = &priv->gwca.ts_queue;

	dma_free_coherent(&priv->pdev->dev,
			  sizeof(struct rswitch_ts_desc) * (gq->ring_size + 1),
			  gq->ts_ring, gq->ring_dma);
	gq->ts_ring = NULL;
}

static int rswitch_gwca_queue_alloc(struct net_device *ndev,
				    struct rswitch_private *priv,
				    struct rswitch_gwca_queue *gq,
				    bool dir_tx, unsigned int ring_size)
{
	unsigned int i, bit;

	gq->dir_tx = dir_tx;
	gq->ring_size = ring_size;
	gq->ndev = ndev;

	if (!dir_tx) {
		gq->rx_bufs = kcalloc(gq->ring_size, sizeof(*gq->rx_bufs), GFP_KERNEL);
		if (!gq->rx_bufs)
			return -ENOMEM;
		if (rswitch_gwca_queue_alloc_rx_buf(gq, 0, gq->ring_size) < 0)
			goto out;

		gq->rx_ring = dma_alloc_coherent(&priv->pdev->dev,
						 sizeof(struct rswitch_ext_ts_desc) *
						 (gq->ring_size + 1), &gq->ring_dma, GFP_KERNEL);
	} else {
		gq->skbs = kcalloc(gq->ring_size, sizeof(*gq->skbs), GFP_KERNEL);
		if (!gq->skbs)
			return -ENOMEM;
		gq->unmap_addrs = kcalloc(gq->ring_size, sizeof(*gq->unmap_addrs), GFP_KERNEL);
		if (!gq->unmap_addrs)
			goto out;
		gq->tx_ring = dma_alloc_coherent(&priv->pdev->dev,
						 sizeof(struct rswitch_ext_desc) *
						 (gq->ring_size + 1), &gq->ring_dma, GFP_KERNEL);
	}

	if (!gq->rx_ring && !gq->tx_ring)
		goto out;

	if (priv->info->rswitch_version == RSW20) {
		i = gq->index / 32;
		bit = BIT(gq->index % 32);
		if (dir_tx)
			priv->gwca.tx_irq_bits[i] |= bit;
		else
			priv->gwca.rx_irq_bits[i] |= bit;
	}

	return 0;

out:
	rswitch_gwca_queue_free(&priv->pdev->dev, gq);

	return -ENOMEM;
}

static void rswitch_desc_set_dptr(struct rswitch_desc *desc, dma_addr_t addr)
{
	desc->dptrl = cpu_to_le32(lower_32_bits(addr));
	desc->dptrh = upper_32_bits(addr) & 0xff;
}

static dma_addr_t rswitch_desc_get_dptr(const struct rswitch_desc *desc)
{
	return __le32_to_cpu(desc->dptrl) | (u64)(desc->dptrh) << 32;
}

static int rswitch_gwca_queue_format(struct net_device *ndev,
				     struct rswitch_private *priv,
				     struct rswitch_gwca_queue *gq)
{
	unsigned int ring_size = sizeof(struct rswitch_ext_desc) * gq->ring_size;
	struct rswitch_ext_desc *desc;
	struct rswitch_desc *linkfix;
	dma_addr_t dma_addr;
	unsigned int i;

	memset(gq->tx_ring, 0, ring_size);
	for (i = 0, desc = gq->tx_ring; i < gq->ring_size; i++, desc++) {
		if (!gq->dir_tx) {
			dma_addr = dma_map_single(ndev->dev.parent,
						  gq->rx_bufs[i] + RSWITCH_HEADROOM,
						  RSWITCH_MAP_BUF_SIZE,
						  DMA_FROM_DEVICE);
			if (dma_mapping_error(ndev->dev.parent, dma_addr))
				goto err;

			desc->desc.info_ds = cpu_to_le16(RSWITCH_DESC_BUF_SIZE);
			rswitch_desc_set_dptr(&desc->desc, dma_addr);
			desc->desc.die_dt = DT_FEMPTY | DIE;
		} else {
			desc->desc.die_dt = DT_EEMPTY | DIE;
		}
	}
	rswitch_desc_set_dptr(&desc->desc, gq->ring_dma);
	desc->desc.die_dt = DT_LINKFIX;

	linkfix = &priv->gwca.linkfix_table[gq->index];
	linkfix->die_dt = DT_LINKFIX;
	rswitch_desc_set_dptr(linkfix, gq->ring_dma);

	iowrite32(BALR | (gq->dir_tx ? DCP(GWCA_IPV_NUM) | DQT : 0) | EDE,
		  priv->addr_gwca + GWDCC(gq->index));

	return 0;

err:
	if (!gq->dir_tx) {
		for (desc = gq->tx_ring; i-- > 0; desc++) {
			dma_addr = rswitch_desc_get_dptr(&desc->desc);
			dma_unmap_single(ndev->dev.parent, dma_addr,
					 RSWITCH_MAP_BUF_SIZE, DMA_FROM_DEVICE);
		}
	}

	return -ENOMEM;
}

static void rswitch_gwca_ts_queue_fill(struct rswitch_private *priv,
				       unsigned int start_index,
				       unsigned int num)
{
	struct rswitch_gwca_queue *gq = &priv->gwca.ts_queue;
	struct rswitch_ts_desc *desc;
	unsigned int i, index;

	for (i = 0; i < num; i++) {
		index = (i + start_index) % gq->ring_size;
		desc = &gq->ts_ring[index];
		desc->desc.die_dt = DT_FEMPTY_ND | DIE;
	}
}

static int rswitch_gwca_queue_ext_ts_fill(struct device *dev,
					  struct rswitch_gwca_queue *gq,
					  unsigned int start_index,
					  unsigned int num)
{
	struct rswitch_ext_ts_desc *desc;
	unsigned int i, index;
	dma_addr_t dma_addr;

	for (i = 0; i < num; i++) {
		index = (i + start_index) % gq->ring_size;
		desc = &gq->rx_ring[index];
		if (!gq->dir_tx) {
			dma_addr = dma_map_single(dev,
						  gq->rx_bufs[index] + RSWITCH_HEADROOM,
						  RSWITCH_MAP_BUF_SIZE,
						  DMA_FROM_DEVICE);
			if (dma_mapping_error(dev, dma_addr))
				goto err;

			desc->desc.info_ds = cpu_to_le16(RSWITCH_DESC_BUF_SIZE);
			rswitch_desc_set_dptr(&desc->desc, dma_addr);
			dma_wmb();
			desc->desc.die_dt = DT_FEMPTY | DIE;
		} else {
			desc->desc.die_dt = DT_EEMPTY | DIE;
		}
	}

	return 0;

err:
	if (!gq->dir_tx) {
		for (; i-- > 0; ) {
			index = (i + start_index) % gq->ring_size;
			desc = &gq->rx_ring[index];
			desc->desc.die_dt = 0x00;
			dma_wmb();
			dma_addr = rswitch_desc_get_dptr(&desc->desc);
			dma_unmap_single(dev, dma_addr,
					 RSWITCH_MAP_BUF_SIZE, DMA_FROM_DEVICE);
		}
	}

	return -ENOMEM;
}

static int rswitch_gwca_queue_ext_ts_format(struct device *dev,
					    struct rswitch_private *priv,
					    struct rswitch_gwca_queue *gq)
{
	unsigned int ring_size = sizeof(struct rswitch_ext_ts_desc) * gq->ring_size;
	struct rswitch_ext_ts_desc *desc;
	struct rswitch_desc *linkfix;
	int err;

	memset(gq->rx_ring, 0, ring_size);
	err = rswitch_gwca_queue_ext_ts_fill(dev, gq, 0, gq->ring_size);
	if (err < 0)
		return err;

	desc = &gq->rx_ring[gq->ring_size];	/* Last */
	rswitch_desc_set_dptr(&desc->desc, gq->ring_dma);
	desc->desc.die_dt = DT_LINKFIX;

	linkfix = &priv->gwca.linkfix_table[gq->index];
	linkfix->die_dt = DT_LINKFIX;
	rswitch_desc_set_dptr(linkfix, gq->ring_dma);

	iowrite32(BALR | (gq->dir_tx ? DCP(GWCA_IPV_NUM) | DQT : 0) | ETS | EDE,
		  priv->addr_gwca + GWDCC(gq->index));

	return 0;
}

static int rswitch_gwca_linkfix_alloc(struct rswitch_private *priv)
{
	unsigned int i, num_queues = priv->gwca.num_queues;
	struct rswitch_gwca *gwca = &priv->gwca;
	struct device *dev = &priv->pdev->dev;

	gwca->linkfix_table_size = sizeof(struct rswitch_desc) * num_queues;
	gwca->linkfix_table = dma_alloc_coherent(dev, gwca->linkfix_table_size,
						 &gwca->linkfix_table_dma, GFP_KERNEL);
	if (!gwca->linkfix_table)
		return -ENOMEM;
	for (i = 0; i < num_queues; i++)
		gwca->linkfix_table[i].die_dt = DT_EOS;

	return 0;
}

static void rswitch_gwca_linkfix_free(struct rswitch_private *priv)
{
	struct rswitch_gwca *gwca = &priv->gwca;

	if (gwca->linkfix_table)
		dma_free_coherent(&priv->pdev->dev, gwca->linkfix_table_size,
				  gwca->linkfix_table, gwca->linkfix_table_dma);
	gwca->linkfix_table = NULL;
}

static int rswitch_gwca_ts_queue_alloc(struct rswitch_private *priv)
{
	struct rswitch_gwca_queue *gq = &priv->gwca.ts_queue;
	struct rswitch_ts_desc *desc;

	gq->ring_size = TS_RING_SIZE(priv);
	gq->ts_ring = dma_alloc_coherent(&priv->pdev->dev,
					 sizeof(struct rswitch_ts_desc) *
					 (gq->ring_size + 1), &gq->ring_dma, GFP_KERNEL);

	if (!gq->ts_ring)
		return -ENOMEM;

	rswitch_gwca_ts_queue_fill(priv, 0, TS_RING_SIZE(priv));
	desc = &gq->ts_ring[gq->ring_size];
	desc->desc.die_dt = DT_LINKFIX;
	rswitch_desc_set_dptr(&desc->desc, gq->ring_dma);

	return 0;
}

static struct rswitch_gwca_queue *rswitch_gwca_get(struct rswitch_private *priv)
{
	struct rswitch_gwca_queue *gq;
	unsigned int index;

	index = find_first_zero_bit(priv->gwca.used, priv->gwca.num_queues);
	if (index >= priv->gwca.num_queues)
		return NULL;
	set_bit(index, priv->gwca.used);
	gq = &priv->gwca.queues[index];
	memset(gq, 0, sizeof(*gq));
	gq->index = index;

	return gq;
}

static void rswitch_gwca_put(struct rswitch_private *priv,
			     struct rswitch_gwca_queue *gq)
{
	clear_bit(gq->index, priv->gwca.used);
}

static int rswitch_txdmac_alloc(struct net_device *ndev)
{
	struct rswitch_device *rdev = netdev_priv(ndev);
	struct rswitch_private *priv = rdev->priv;
	int i, err;

	for (i = 0; i < priv->info->num_tx_queues; i++) {
		rdev->tx_queue[i] = rswitch_gwca_get(priv);
		if (!rdev->tx_queue[i]) {
			for (; i-- > 0; )
				rswitch_gwca_put(priv, rdev->tx_queue[i]);
			return -EBUSY;
		}

		err = rswitch_gwca_queue_alloc(ndev, priv, rdev->tx_queue[i], true, TX_RING_SIZE);
		if (err < 0) {
			for (; i >= 0; i--)
				rswitch_gwca_put(priv, rdev->tx_queue[i]);
			return err;
		}

		netif_napi_add_tx(ndev, &rdev->tx_queue[i]->napi, rswitch_tx_poll);
		rdev->tx_queue[i]->napi_idx = i;
	}

	return 0;
}

static void rswitch_txdmac_free(struct net_device *ndev)
{
	struct rswitch_device *rdev = netdev_priv(ndev);
	int i;

	for (i = 0; i < rdev->priv->info->num_tx_queues; i++) {
		rswitch_gwca_queue_free(ndev->dev.parent, rdev->tx_queue[i]);
		rswitch_gwca_put(rdev->priv, rdev->tx_queue[i]);
		netif_napi_del(&rdev->tx_queue[i]->napi);
	}
}

static int rswitch_txdmac_init(struct rswitch_private *priv, unsigned int index)
{
	struct rswitch_device *rdev = priv->rdev[index];
	int i, err;

	for (i = 0; i < priv->info->num_tx_queues; i++) {
		err = rswitch_gwca_queue_format(rdev->ndev, priv, rdev->tx_queue[i]);
		if (err)
			return err;
	}

	return 0;
}

static int rswitch_rxdmac_alloc(struct net_device *ndev)
{
	struct rswitch_device *rdev = netdev_priv(ndev);
	struct rswitch_private *priv = rdev->priv;
	int i, err;

	for (i = 0; i < priv->info->num_rx_queues; i++) {
		rdev->rx_queue[i] = rswitch_gwca_get(priv);
		if (!rdev->rx_queue[i]) {
			for (; i-- > 0; )
				rswitch_gwca_put(priv, rdev->rx_queue[i]);
			return -EBUSY;
		}

		err = rswitch_gwca_queue_alloc(ndev, priv, rdev->rx_queue[i], false, RX_RING_SIZE);
		if (err < 0) {
			for (; i >= 0; i--)
				rswitch_gwca_put(priv, rdev->rx_queue[i]);
			return err;
		}

		netif_napi_add(ndev, &rdev->rx_queue[i]->napi, rswitch_rx_poll);
		rdev->rx_queue[i]->napi_idx = i;
	}

	return 0;
}

static void rswitch_rxdmac_free(struct net_device *ndev)
{
	struct rswitch_device *rdev = netdev_priv(ndev);
	int i;

	for (i = 0; i < rdev->priv->info->num_rx_queues; i++) {
		rswitch_gwca_queue_free(ndev->dev.parent, rdev->rx_queue[i]);
		rswitch_gwca_put(rdev->priv, rdev->rx_queue[i]);
		netif_napi_del(&rdev->rx_queue[i]->napi);
	}
}

static int rswitch_rxdmac_init(struct rswitch_private *priv, unsigned int index)
{
	struct rswitch_device *rdev = priv->rdev[index];
	struct net_device *ndev = rdev->ndev;
	int i, err;

	for (i = 0; i < priv->info->num_rx_queues; i++) {
		err = rswitch_gwca_queue_ext_ts_format(ndev->dev.parent, priv, rdev->rx_queue[i]);
		if (err)
			return err;
	}

	return 0;
}

static void rswitch_shared_rx_queue_free(struct rswitch_private *priv)
{
	struct rswitch_gwca *gwca = &priv->gwca;

	rswitch_gwca_put(priv, gwca->l2_shared_rx_queue);
	rswitch_gwca_queue_free(&priv->pdev->dev, gwca->l2_shared_rx_queue);
}

static int rswitch_shared_rx_queue_alloc(struct rswitch_private *priv)
{
	struct rswitch_gwca *gwca = &priv->gwca;
	struct device *dev = &priv->pdev->dev;

	int err;

	gwca->l2_shared_rx_queue = rswitch_gwca_get(priv);
	if (!gwca->l2_shared_rx_queue)
		return -EBUSY;

	err = rswitch_gwca_queue_alloc(NULL, priv, gwca->l2_shared_rx_queue, false, RX_RING_SIZE);
	if (err < 0) {
		rswitch_gwca_put(priv, gwca->l2_shared_rx_queue);
		return err;
	}

	err = rswitch_gwca_queue_ext_ts_format(dev, priv, gwca->l2_shared_rx_queue);

	if (err < 0)
		rswitch_shared_rx_queue_free(priv);
	return err;
}

static int rswitch_gwca_hw_init(struct rswitch_private *priv)
{
	unsigned int i;
	int err;

	err = rswitch_gwca_change_mode(priv, OPC_CONFIG);
	if (err < 0)
		return err;

	err = rswitch_gwca_mcast_table_reset(priv);
	if (err < 0)
		return err;

	err = rswitch_gwca_axi_ram_reset(priv);
	if (err < 0)
		return err;

	iowrite32(FIELD_PREP(VEM, SC_TAG_VLAN), priv->addr_gwca + GWVCC);

	/* GWCA uses only one queue */
	iowrite32(0, priv->addr_gwca + GWIRC);
	iowrite32(FIELD_PREP(DQD, DES_RAM_DP), priv->addr_gwca + GWRDQDC0);

	/* Drop frames with unknown tags */
	iowrite32(UT, priv->addr_gwca + GWTTFC);

	if (priv->info->rswitch_version == RSW20) {
		iowrite32(lower_32_bits(priv->gwca.linkfix_table_dma),
			  priv->addr_gwca + GWDCBAC1);
		iowrite32(upper_32_bits(priv->gwca.linkfix_table_dma),
			  priv->addr_gwca + GWDCBAC0);
		iowrite32(lower_32_bits(priv->gwca.ts_queue.ring_dma), priv->addr_gwca + GWTDCAC10);
		iowrite32(upper_32_bits(priv->gwca.ts_queue.ring_dma), priv->addr_gwca + GWTDCAC00);
		iowrite32(FIELD_PREP(TSDMN, 1) | FIELD_PREP(TXDMN, 0x1e) | FIELD_PREP(RXDMN, 0x1f),
			  priv->addr_gwca + GWMDNC);
	} else {
		iowrite32(lower_32_bits(priv->gwca.linkfix_table_dma),
			  priv->addr_gwca + GWDCBAC1_RSW3);
		iowrite32(upper_32_bits(priv->gwca.linkfix_table_dma),
			  priv->addr_gwca + GWDCBAC0_RSW3);
		iowrite32(lower_32_bits(priv->gwca.ts_queue.ring_dma), priv->addr_gwca + GWTDCAC10);
		iowrite32(upper_32_bits(priv->gwca.ts_queue.ring_dma), priv->addr_gwca + GWTDCAC00);
		iowrite32(FIELD_PREP(TSDMN, 1) | FIELD_PREP(TXDMN, 0x1e) | FIELD_PREP(RXDMN, 0x1f),
			  priv->addr_gwca + GWMDNC_RSW3);
	}

	iowrite32(TE, priv->addr_gwca + GWTSDCC0);

	iowrite32(PPPL(GWCA_IPV_NUM), priv->addr_gwca + GWTPC0);

	for (i = 0; i < priv->num_ports; i++) {
		err = rswitch_rxdmac_init(priv, i);
		if (err < 0)
			return err;

		err = rswitch_txdmac_init(priv, i);
		if (err < 0)
			return err;
	}

	err = rswitch_shared_rx_queue_alloc(priv);
	if (err < 0)
		return err;

	return rswitch_gwca_change_mode(priv, OPC_OPERATION);
}

static int rswitch_gwca_hw_deinit(struct rswitch_private *priv)
{
	int err;

	err = rswitch_gwca_change_mode(priv, OPC_RESET);
	if (err < 0)
		return err;

	rswitch_shared_rx_queue_free(priv);

	return rswitch_gwca_change_mode(priv, OPC_DISABLE);
}

static int rswitch_gwca_halt(struct rswitch_private *priv)
{
	int err;

	priv->gwca_halt = true;
	err = rswitch_gwca_hw_deinit(priv);
	dev_err(&priv->pdev->dev, "halted (%d)\n", err);

	return err;
}

static struct sk_buff *rswitch_rx_handle_desc(struct net_device *ndev,
					      struct rswitch_gwca_queue *gq,
					      struct rswitch_ext_ts_desc *desc)
{
	dma_addr_t dma_addr = rswitch_desc_get_dptr(&desc->desc);
	u16 pkt_len = le16_to_cpu(desc->desc.info_ds) & RX_DS;
	u8 die_dt = desc->desc.die_dt & DT_MASK;
	struct sk_buff *skb = NULL;

	dma_unmap_single(ndev->dev.parent, dma_addr, RSWITCH_MAP_BUF_SIZE,
			 DMA_FROM_DEVICE);

	/* The RX descriptor order will be one of the following:
	 * - FSINGLE
	 * - FSTART -> FEND
	 * - FSTART -> FMID -> FEND
	 */

	/* Check whether the descriptor is unexpected order */
	switch (die_dt) {
	case DT_FSTART:
	case DT_FSINGLE:
		if (gq->skb_fstart) {
			dev_kfree_skb_any(gq->skb_fstart);
			gq->skb_fstart = NULL;
			ndev->stats.rx_dropped++;
		}
		break;
	case DT_FMID:
	case DT_FEND:
		if (!gq->skb_fstart) {
			ndev->stats.rx_dropped++;
			return NULL;
		}
		break;
	default:
		break;
	}

	/* Handle the descriptor */
	switch (die_dt) {
	case DT_FSTART:
	case DT_FSINGLE:
		skb = build_skb(gq->rx_bufs[gq->cur], RSWITCH_BUF_SIZE);
		if (skb) {
			skb_reserve(skb, RSWITCH_HEADROOM);
			skb_put(skb, pkt_len);
			gq->pkt_len = pkt_len;
			if (die_dt == DT_FSTART) {
				gq->skb_fstart = skb;
				skb = NULL;
			}
		}
		break;
	case DT_FMID:
	case DT_FEND:
		skb_add_rx_frag(gq->skb_fstart, skb_shinfo(gq->skb_fstart)->nr_frags,
				virt_to_page(gq->rx_bufs[gq->cur]),
				offset_in_page(gq->rx_bufs[gq->cur]) + RSWITCH_HEADROOM,
				pkt_len, RSWITCH_BUF_SIZE);
		if (die_dt == DT_FEND) {
			skb = gq->skb_fstart;
			gq->skb_fstart = NULL;
		}
		gq->pkt_len += pkt_len;
		break;
	default:
		netdev_err(ndev, "%s: unexpected value (%x)\n", __func__, die_dt);
		break;
	}

	return skb;
}

static bool rswitch_rx(struct net_device *ndev, struct rswitch_gwca_queue *gq, int *quota)
{
	struct rswitch_device *rdev = netdev_priv(ndev);
	struct rswitch_ext_ts_desc *desc;
	int limit, boguscnt, ret;
	struct sk_buff *skb;
	unsigned int num;
	u32 get_ts;

	if (*quota <= 0)
		return true;

	boguscnt = min_t(int, gq->ring_size, *quota);
	limit = boguscnt;

	desc = &gq->rx_ring[gq->cur];

	while ((desc->desc.die_dt & DT_MASK) != DT_FEMPTY) {
		dma_rmb();
		skb = rswitch_rx_handle_desc(ndev, gq, desc);
		if (!skb)
			goto out;

		get_ts = rdev->priv->tstamp_rx_ctrl != HWTSTAMP_FILTER_NONE;
		if (get_ts) {
			struct skb_shared_hwtstamps *shhwtstamps;
			struct timespec64 ts;

			shhwtstamps = skb_hwtstamps(skb);
			memset(shhwtstamps, 0, sizeof(*shhwtstamps));
			ts.tv_sec = __le32_to_cpu(desc->ts_sec);
			ts.tv_nsec = __le32_to_cpu(desc->ts_nsec & cpu_to_le32(0x3fffffff));
			shhwtstamps->hwtstamp = timespec64_to_ktime(ts);
		}
		skb->protocol = eth_type_trans(skb, ndev);
		skb_record_rx_queue(skb, gq->napi_idx);
		napi_gro_receive(&rdev->rx_queue[gq->napi_idx]->napi, skb);
		rdev->ndev->stats.rx_packets++;
		rdev->ndev->stats.rx_bytes += gq->pkt_len;

out:
		gq->rx_bufs[gq->cur] = NULL;
		gq->cur = rswitch_next_queue_index(gq, true, 1);
		desc = &gq->rx_ring[gq->cur];

		if (--boguscnt <= 0)
			break;
	}

	num = rswitch_get_num_cur_queues(gq);
	ret = rswitch_gwca_queue_alloc_rx_buf(gq, gq->dirty, num);
	if (ret < 0)
		goto err;
	ret = rswitch_gwca_queue_ext_ts_fill(ndev->dev.parent, gq, gq->dirty, num);
	if (ret < 0)
		goto err;
	gq->dirty = rswitch_next_queue_index(gq, false, num);

	*quota -= limit - boguscnt;

	return boguscnt <= 0;

err:
	rswitch_gwca_halt(rdev->priv);

	return 0;
}

static int rswitch_tx_free(struct net_device *ndev, struct rswitch_gwca_queue *gq, int budget)
{
	struct rswitch_device *rdev = netdev_priv(ndev);
	struct rswitch_ext_desc *desc;
	struct sk_buff *skb;
	int done = 0;

	desc = &gq->tx_ring[gq->dirty];
	while (done < budget && (desc->desc.die_dt & DT_MASK) == DT_FEMPTY) {
		dma_rmb();

		skb = gq->skbs[gq->dirty];
		if (skb) {
			rdev->ndev->stats.tx_packets++;
			rdev->ndev->stats.tx_bytes += skb->len;
			dma_unmap_single(ndev->dev.parent,
					 gq->unmap_addrs[gq->dirty],
					 skb->len, DMA_TO_DEVICE);
			dev_kfree_skb_any(gq->skbs[gq->dirty]);
			gq->skbs[gq->dirty] = NULL;
		}

		desc->desc.die_dt = DT_EEMPTY;
		gq->dirty = rswitch_next_queue_index(gq, false, 1);
		desc = &gq->tx_ring[gq->dirty];
		done++;
	}

	return done;
}

static int rswitch_tx_poll(struct napi_struct *napi, int budget)
{
	struct rswitch_gwca_queue *gq = napi_to_gwca_queue(napi);
	struct net_device *ndev = napi->dev;
	struct rswitch_private *priv;
	struct rswitch_device *rdev;
	int qidx = gq->napi_idx;
	int done;

	rdev = netdev_priv(ndev);
	priv = rdev->priv;

	done = rswitch_tx_free(ndev, gq, budget);

	netif_wake_subqueue(ndev, qidx);

	if (done < budget && napi_complete_done(napi, done)) {
		unsigned long flags;

		spin_lock_irqsave(&priv->lock, flags);
		if (test_bit(rdev->port, priv->opened_ports))
			rswitch_enadis_data_irq(priv, rdev->tx_queue[qidx]->index, true);
		spin_unlock_irqrestore(&priv->lock, flags);
	}

	return done;
}

static int rswitch_rx_poll(struct napi_struct *napi, int budget)
{
	struct rswitch_gwca_queue *gq = napi_to_gwca_queue(napi);
	struct net_device *ndev = napi->dev;
	struct rswitch_private *priv;
	struct rswitch_device *rdev;
	unsigned long flags;
	int quota = budget;
	int qidx = gq->napi_idx;

	rdev = netdev_priv(ndev);
	priv = rdev->priv;

retry:
	if (rswitch_rx(ndev, gq, &quota))
		goto out;
	else if (rdev->priv->gwca_halt)
		goto err;
	else if (rswitch_is_queue_rxed(rdev->rx_queue[qidx]))
		goto retry;

	if (napi_complete_done(napi, budget - quota)) {
		spin_lock_irqsave(&priv->lock, flags);
		if (rdev->rx_old_queue) {
			rdev->rx_queue[0] = rdev->rx_old_queue;
			rdev->rx_old_queue = NULL;
		}
		if (test_bit(rdev->port, priv->opened_ports))
			rswitch_enadis_data_irq(priv, rdev->rx_queue[qidx]->index, true);
		rswitch_enadis_data_irq(priv, priv->gwca.l2_shared_rx_queue->index, true);
		spin_unlock_irqrestore(&priv->lock, flags);
	}

out:
	return budget - quota;

err:
	napi_complete(napi);

	return 0;
}

static void rswitch_queue_interrupt(struct rswitch_private *priv,
				    struct rswitch_gwca_queue *gq,
				    struct napi_struct *napi, bool dir_tx)
{
	struct rswitch_ext_ts_desc *desc;
	struct rswitch_device *rdev;
	struct net_device *ndev;
	unsigned long flags;
	int qidx = gq->napi_idx;
	u32 spn;

	/* If we receive a shared queue through the exception path, it will be missing the ndev
	 * pointer. This needs to be added to be able to determine from which port the packet was
	 * received. Then we temporarily exchange the rx_queue pointer in rdev. This will be
	 * restored after the packet has been processed.
	 */

	if (gq->index == priv->gwca.l2_shared_rx_queue->index) {
		desc = &gq->rx_ring[gq->cur];
		if (priv->info->rswitch_version == RSW30)
			spn = FIELD_GET(SPN_X5H, le64_to_cpu(desc->info1));
		else
			spn = FIELD_GET(SPN, le64_to_cpu(desc->info1));
		ndev = priv->rdev[spn]->ndev;
		rdev = netdev_priv(ndev);
		gq->ndev = ndev;
		/* store original rx_queue */
		spin_lock_irqsave(&priv->lock, flags);
		rdev->rx_old_queue = rdev->rx_queue[0];
		rdev->rx_queue[0] = gq;
		spin_unlock_irqrestore(&priv->lock, flags);
		napi = &rdev->rx_queue[0]->napi;
		qidx = 0;
	} else {
		rdev = netdev_priv(gq->ndev);
	}

	if (napi_schedule_prep(napi)) {
		spin_lock(&priv->lock);
		if (dir_tx) {
			rswitch_enadis_data_irq(priv, rdev->tx_queue[qidx]->index, false);
		} else {
			rswitch_enadis_data_irq(priv, rdev->rx_queue[qidx]->index, false);
			rswitch_enadis_data_irq(priv, priv->gwca.l2_shared_rx_queue->index, false);
		}
		spin_unlock(&priv->lock);
		__napi_schedule(napi);
	}
}

static irqreturn_t rswitch_data_irq(struct rswitch_private *priv, u32 *dis, u32 *mask)
{
	struct rswitch_gwca_queue *gq;
	unsigned int i, index, bit;

	for (i = 0; i < priv->gwca.num_queues; i++) {
		gq = &priv->gwca.queues[i];
		index = gq->index / 32;
		bit = BIT(gq->index % 32);
		if (!(dis[index] & bit) || (mask && !(mask[index] & bit)))
			continue;

		rswitch_ack_data_irq(priv, gq->index);

		if (gq->dir_tx)
			rswitch_queue_interrupt(priv, gq, &gq->napi, true);
		else
			rswitch_queue_interrupt(priv, gq, &gq->napi, false);
	}

	return IRQ_HANDLED;
}

static irqreturn_t rswitch_gwca_irq(int irq, void *dev_id)
{
	struct rswitch_private *priv = dev_id;
	u32 dis[RSWITCH_NUM_IRQ_REGS];
	irqreturn_t ret = IRQ_NONE;
	u32 *mask = NULL;
	int i;

	rswitch_get_data_irq_status(priv, dis);

	if (priv->info->rswitch_version == RSW20) {
		if (rswitch_is_any_data_irq(priv, dis, priv->gwca.tx_irq_bits) ||
		    rswitch_is_any_data_irq(priv, dis, priv->gwca.rx_irq_bits))
			ret = rswitch_data_irq(priv, dis, NULL);
		return ret;
	}

	for (i = 0; i < GWCA_NUM_IRQS; i++) {
		if (irq == priv->irq[i].irq_id) {
			mask = priv->irq[i].irq_bits;
			break;
		}
	}

	if (!mask)
		return ret;

	if (rswitch_is_any_data_irq(priv, dis, mask))
		ret = rswitch_data_irq(priv, dis, mask);

	return ret;
}

static int rswitch_gwca_request_irqs(struct rswitch_private *priv)
{
	u8 gwca_index = priv->info->gwca_index;
	char *resource_name, *irq_name;
	int i, ret, irq;

	for (i = 0; i < GWCA_NUM_IRQS; i++) {
		if (priv->info->rswitch_version == RSW20)
			resource_name = kasprintf(GFP_KERNEL,
						  GWCA_IRQ_RESOURCE_NAME, gwca_index, i);
		else
			resource_name = kasprintf(GFP_KERNEL,
						  GWCA_IRQ_RESOURCE_NAME_RSW3, gwca_index, i);
		if (!resource_name)
			return -ENOMEM;

		irq = platform_get_irq_byname(priv->pdev, resource_name);
		kfree(resource_name);
		if (irq < 0)
			return irq;

		if (priv->info->rswitch_version == RSW20)
			irq_name = devm_kasprintf(&priv->pdev->dev, GFP_KERNEL,
						  GWCA_IRQ_NAME, priv->info->gwca_index, i);
		else
			irq_name = devm_kasprintf(&priv->pdev->dev, GFP_KERNEL,
						  GWCA_IRQ_NAME_RSW3, priv->info->gwca_index, i);

		if (!irq_name)
			return -ENOMEM;

		priv->irq[i].irq_id = irq;

		ret = devm_request_irq(&priv->pdev->dev, irq, rswitch_gwca_irq,
				       0, irq_name, priv);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static void rswitch_ts(struct rswitch_private *priv)
{
	struct rswitch_gwca_queue *gq = &priv->gwca.ts_queue;
	struct skb_shared_hwtstamps shhwtstamps;
	struct rswitch_ts_desc *desc;
	struct rswitch_device *rdev;
	struct sk_buff *ts_skb;
	struct timespec64 ts;
	unsigned int num;
	u32 tag, port;

	desc = &gq->ts_ring[gq->cur];
	while ((desc->desc.die_dt & DT_MASK) != DT_FEMPTY_ND) {
		dma_rmb();

		port = TS_DESC_DPN(__le32_to_cpu(desc->desc.dptrl));
		if (unlikely(port >= priv->num_ports))
			goto next;
		rdev = priv->rdev[port];

		tag = TS_DESC_TSUN(__le32_to_cpu(desc->desc.dptrl));
		if (unlikely(tag >= TS_TAGS_PER_PORT))
			goto next;
		ts_skb = xchg(&rdev->ts_skb[tag], NULL);
		smp_mb(); /* order rdev->ts_skb[] read before bitmap update */
		clear_bit(tag, rdev->ts_skb_used);

		if (unlikely(!ts_skb))
			goto next;

		memset(&shhwtstamps, 0, sizeof(shhwtstamps));
		ts.tv_sec = __le32_to_cpu(desc->ts_sec);
		ts.tv_nsec = __le32_to_cpu(desc->ts_nsec & cpu_to_le32(0x3fffffff));
		shhwtstamps.hwtstamp = timespec64_to_ktime(ts);
		skb_tstamp_tx(ts_skb, &shhwtstamps);
		dev_consume_skb_irq(ts_skb);

next:
		gq->cur = rswitch_next_queue_index(gq, true, 1);
		desc = &gq->ts_ring[gq->cur];
	}

	num = rswitch_get_num_cur_queues(gq);
	rswitch_gwca_ts_queue_fill(priv, gq->dirty, num);
	gq->dirty = rswitch_next_queue_index(gq, false, num);
}

static irqreturn_t rswitch_gwca_ts_irq(int irq, void *dev_id)
{
	struct rswitch_private *priv = dev_id;

	if (ioread32(priv->addr_gwca + GWTSDIS) & TS) {
		iowrite32(TS, priv->addr_gwca + GWTSDIS);
		rswitch_ts(priv);

		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static int rswitch_gwca_ts_request_irqs(struct rswitch_private *priv)
{
	int irq;

	irq = platform_get_irq_byname(priv->pdev, GWCA_TS_IRQ_RESOURCE_NAME);
	if (irq < 0)
		return irq;

	return devm_request_irq(&priv->pdev->dev, irq, rswitch_gwca_ts_irq,
				0, GWCA_TS_IRQ_NAME, priv);
}

/* Ethernet TSN Agent block (ETHA) and Ethernet MAC IP block (RMAC) */
int rswitch_etha_change_mode(struct rswitch_etha *etha,
			     enum rswitch_port_mode mode)
{
	int ret;

	if (!rswitch_agent_clock_is_enabled(etha->coma_addr, etha->index)) {
		spin_lock(&etha->priv->agent_lock);
		rswitch_agent_clock_ctrl(etha->coma_addr, etha->index, 1);
		spin_unlock(&etha->priv->agent_lock);
	}

	iowrite32(OPC_DISABLE, etha->addr + EAMC);
	ret = rswitch_reg_wait(etha->addr, EAMS, OPC, OPC_DISABLE);
	if (ret < 0) {
		/* workaround for deadlock in RMAC due to a HW fault in X5H*/
		iowrite32(FIELD_PREP(RXBFM, 0x4), etha->addr + MXGMIIC);
		iowrite32(OPC_DISABLE, etha->addr + EAMC);
		ret = rswitch_reg_wait(etha->addr, EAMS, OPC, OPC_DISABLE);
		iowrite32(FIELD_PREP(RXBFM, 0), etha->addr + MXGMIIC);
	}
	if (ret < 0)
		return ret;

	if (mode == OPC_DISABLE) {
		spin_lock(&etha->priv->agent_lock);
		rswitch_agent_clock_ctrl(etha->coma_addr, etha->index, 0);
		spin_unlock(&etha->priv->agent_lock);

		return ret;
	}

	iowrite32(mode, etha->addr + EAMC);

	return rswitch_reg_wait(etha->addr, EAMS, OPC, mode);
}

static void rswitch_etha_read_mac_address(struct rswitch_etha *etha)
{
	u32 mrmac0 = ioread32(etha->addr + MRMAC0);
	u32 mrmac1 = ioread32(etha->addr + MRMAC1);
	u8 *mac = &etha->mac_addr[0];

	mac[0] = (mrmac0 >>  8) & 0xFF;
	mac[1] = (mrmac0 >>  0) & 0xFF;
	mac[2] = (mrmac1 >> 24) & 0xFF;
	mac[3] = (mrmac1 >> 16) & 0xFF;
	mac[4] = (mrmac1 >>  8) & 0xFF;
	mac[5] = (mrmac1 >>  0) & 0xFF;

	/* Cache valid MAC addresses for deferred probe scenarios */
	if (is_valid_ether_addr(mac) && etha->index < RSWITCH_MAX_NUM_PORTS) {
		memcpy(cached_mac_addresses[etha->index], mac, ETH_ALEN);
		mac_cache_valid[etha->index] = true;
	} else if (!is_valid_ether_addr(mac) && etha->index < RSWITCH_MAX_NUM_PORTS &&
		   mac_cache_valid[etha->index]) {
		memcpy(mac, cached_mac_addresses[etha->index], ETH_ALEN);
	}
}

static void rswitch_etha_write_mac_address(struct rswitch_etha *etha, const u8 *mac)
{
	iowrite32((mac[0] << 8) | mac[1], etha->addr + MRMAC0);
	iowrite32((mac[2] << 24) | (mac[3] << 16) | (mac[4] << 8) | mac[5],
		  etha->addr + MRMAC1);
}

static int rswitch_etha_wait_link_verification(struct rswitch_etha *etha)
{
	iowrite32(PLV, etha->addr + MLVC);

	return rswitch_reg_wait(etha->addr, MLVC, PLV, 0);
}

static void rswitch2_rmac_setting(struct rswitch_etha *etha, const u8 *mac)
{
	u32 pis, lsc;

	rswitch_etha_write_mac_address(etha, mac);

	switch (etha->phy_interface) {
	case PHY_INTERFACE_MODE_SGMII:
		pis = PIS_GMII;
		break;
	case PHY_INTERFACE_MODE_USXGMII:
	case PHY_INTERFACE_MODE_5GBASER:
		pis = PIS_XGMII;
		break;
	default:
		pis = FIELD_GET(PIS, ioread32(etha->addr + MPIC));
		break;
	}

	switch (etha->speed) {
	case 100:
		lsc = LSC_100M;
		break;
	case 1000:
		lsc = LSC_1G;
		break;
	case 2500:
		lsc = LSC_2_5G;
		break;
	default:
		lsc = FIELD_GET(LSC, ioread32(etha->addr + MPIC));
		break;
	}

	rswitch_modify(etha->addr, MPIC, PIS | LSC,
		       FIELD_PREP(PIS, pis) | FIELD_PREP(LSC, lsc));
}

static void rswitch3_rmac_setting(struct rswitch_etha *etha, const u8 *mac)
{
	u32 pis, lsc;

	rswitch_etha_write_mac_address(etha, mac);

	switch (etha->phy_interface) {
	case PHY_INTERFACE_MODE_SGMII:
		pis = PIS_GMII;
		break;
	case PHY_INTERFACE_MODE_USXGMII:
	case PHY_INTERFACE_MODE_5GBASER:
	case PHY_INTERFACE_MODE_10GBASER:
		pis = PIS_XGMII;
		break;
	default:
		pis = FIELD_GET(PIS, ioread32(etha->addr + MPIC));
		break;
	}

	switch (etha->speed) {
	case 100:
		lsc = LSC_100M;
		break;
	case 1000:
		lsc = LSC_1G;
		break;
	case 2500:
		lsc = LSC_2_5G;
		break;
	case 5000:
		lsc = LSC_5G;
		break;
	case 10000:
		lsc = LSC_10G;
		break;
	default:
		lsc = FIELD_GET(LSC, ioread32(etha->addr + MPIC));
		break;
	}

	rswitch_modify(etha->addr, MPIC, PIS | LSC,
		       FIELD_PREP(PIS, pis) | FIELD_PREP(LSC, lsc));

/*	Bit [3] : Connection change PHY or TSN-ES
 *		This function is enabled only TSNA/RMAC[5-7]
 *			0 : Connected TSN-ES[0-2]
 *			1 : Connected PHY
 */
	if (etha->connect_to_xpcs) {
		if (etha->index >= 5 && etha->index <= 7) {
			iowrite32(BIT(3), etha->addr + MIOC);
		} else {
			pr_err("%s: Invalid port %d for XPCS connection\n",
			       __func__, etha->index);
		}
	}
}

static void rswitch2_etha_enable_mii(struct rswitch_etha *etha)
{
	rswitch_modify(etha->addr, MPIC, PSMCS | PSMHT,
		       FIELD_PREP(PSMCS, etha->psmcs) |
		       FIELD_PREP(PSMHT, 0x06));
}

static void rswitch3_etha_enable_mii(struct rswitch_etha *etha)
{
	rswitch_modify(etha->addr, MPIC, PSMCS | PSMCS_HI | RSW3_PSMHT,
		       PSMCS_ENC(etha->psmcs) | FIELD_PREP(RSW3_PSMHT, 0x06));
}

static void rswitch_etha_init_tsn_egress_path(struct rswitch_etha *etha)
{
	unsigned int q;

	iowrite32(0, etha->addr + EATDRC);
	iowrite32(0, etha->addr + EAIRC);
	iowrite32(0, etha->addr + EATDQSC);
	iowrite32(0, etha->addr + EATDQAC);
	iowrite32(0, etha->addr + EATPEC);

	for (q = 0; q < RSWITCH3_NUM_PRIOS; q++) {
		iowrite32(RSWITCH3_TSNA_QUEUE_DEPTH, etha->addr + EATDQDC(q));
		iowrite32(MFS, etha->addr + EATMFSC(q));
	}

	iowrite32(0, etha->addr + EACTDQDC);
	iowrite32(0, etha->addr + EARTFC);
}

static int rswitch_etha_hw_init_tsn_internal(struct rswitch_private *priv,
					     u32 rsw_port, const u8 *tsnes_mac)
{
	struct rswitch_etha *etha = &priv->etha[rsw_port];
	struct rswitch_device *rdev;
	const u8 *mac = tsnes_mac;
	struct net_device *ndev;
	int err;

	if (rsw_port >= RSWITCH_NUM_PORTS_RSW3 || !priv->rdev[rsw_port])
		return -ENODEV;

	rdev = priv->rdev[rsw_port];
	ndev = rdev->ndev;

	if ((!mac || !is_valid_ether_addr(mac)) && ndev)
		mac = ndev->dev_addr;
	if (!mac || !is_valid_ether_addr(mac))
		return -EINVAL;

	err = rswitch_etha_change_mode(etha, OPC_CONFIG);
	if (err < 0)
		return err;

	rswitch_etha_init_tsn_egress_path(etha);
	iowrite32(FIELD_PREP(DQD, DES_RAM_DP), etha->addr + EATDQDC0);
	iowrite32(FIELD_PREP(VEM, SC_TAG_VLAN), etha->addr + EAVCC);
	/* Drop frames with unknown tags */
	iowrite32(UT, etha->addr + EARTFC);
	rswitch_etha_write_mac_address(etha, mac);
	iowrite32(RX_PROMISC, etha->addr + MRAFC);
	rswitch_modify(etha->addr, MIOC, FORCE_PHY_LINK, FORCE_PHY_LINK);
	rswitch_modify(etha->addr, MPIC, PIS | LSC | PLSPP,
		       FIELD_PREP(PIS, PIS_GMII) |
		       FIELD_PREP(LSC, LSC_2_5G) |
		       PLSPP);

	/*
	 * Internal TSN-ES interfaces 8..12 have no external PHY/XPCS.
	 * The TSN-ES side owns link state
	 */
	err = rswitch_etha_change_mode(etha, OPC_OPERATION);
	if (err < 0)
		return err;

	iowrite32(TCTDQD, etha->addr + EATDQC);
	etha->operated = true;

	return 0;
}

static int rswitch_etha_hw_init(struct rswitch_etha *etha, const u8 *mac,
				struct rswitch_private *priv)
{
	int err;

	err = rswitch_etha_change_mode(etha, OPC_CONFIG);
	if (err < 0)
		return err;

	iowrite32(FIELD_PREP(DQD, DES_RAM_DP), etha->addr + EATDQDC0);

	iowrite32(FIELD_PREP(VEM, SC_TAG_VLAN), etha->addr + EAVCC);

	/* Drop frames with unknown tags */
	iowrite32(UT, etha->addr + EARTFC);

	priv->info->rswitch_rmac_setting(etha, mac);
	priv->info->rswitch_etha_enable_mii(etha);

	err = rswitch_etha_wait_link_verification(etha);
	if (err < 0)
		return err;

	return rswitch_etha_change_mode(etha, OPC_OPERATION);
}

static int rswitch_etha_fixed_link(struct rswitch_etha *etha)
{
	int err;

	err = rswitch_etha_change_mode(etha, OPC_CONFIG);
	if (err < 0)
		return err;

	rswitch_modify(etha->addr, MPIC, PLSPP, etha->fixed_link ? PLSPP : 0);
	rswitch_modify(etha->addr, MIOC, BIT(5), etha->fixed_link ? BIT(5) : 0);

	return rswitch_etha_change_mode(etha, OPC_OPERATION);
}

static int rswitch_etha_mii_hw_start(struct rswitch_etha *etha)
{
	u32 val;
	int err;

	val = ioread32(etha->addr + EAMS);

	if ((val & OPC) != OPC_OPERATION) {
		err = rswitch_etha_change_mode(etha, OPC_OPERATION);
		if (err < 0)
			return err;
	}

	return 0;
}

static int rswitch_etha_mpsm_op(struct rswitch_etha *etha, bool read,
				unsigned int mmf, unsigned int pda,
				unsigned int pra, unsigned int pop,
				unsigned int prd)
{
	u32 val;
	int ret;

	val = PSME |
	      FIELD_PREP(MFF, mmf) |
	      FIELD_PREP(PDA, pda) |
	      FIELD_PREP(PRA, pra) |
	      FIELD_PREP(POP, pop) |
	      FIELD_PREP(PRD, prd);
	iowrite32(val, etha->addr + MPSM);

	ret = rswitch_reg_wait(etha->addr, MPSM, PSME, 0);
	if (ret)
		return ret;

	if (read) {
		val = ioread32(etha->addr + MPSM);
		ret = FIELD_GET(PRD, val);
	}

	return ret;
}

static int rswitch_etha_mii_read_c45(struct mii_bus *bus, int addr, int devad,
				     int regad)
{
	struct rswitch_etha *etha = bus->priv;
	int ret;

	ret = rswitch_etha_mpsm_op(etha, false, MMF_C45, addr, devad,
				   POP_ADDRESS, regad);
	if (ret)
		return ret;

	return rswitch_etha_mpsm_op(etha, true, MMF_C45, addr, devad,
				    POP_READ_C45, 0);
}

static int rswitch_etha_mii_write_c45(struct mii_bus *bus, int addr, int devad,
				      int regad, u16 val)
{
	struct rswitch_etha *etha = bus->priv;
	int ret;

	ret = rswitch_etha_mpsm_op(etha, false, MMF_C45, addr, devad,
				   POP_ADDRESS, regad);
	if (ret)
		return ret;

	return rswitch_etha_mpsm_op(etha, false, MMF_C45, addr, devad,
				    POP_WRITE, val);
}

static int rswitch_etha_mii_read_c22(struct mii_bus *bus, int phyad, int regad)
{
	struct rswitch_etha *etha = bus->priv;

	return rswitch_etha_mpsm_op(etha, true, MMF_C22, phyad, regad,
				    POP_READ_C22, 0);
}

static int rswitch_etha_mii_write_c22(struct mii_bus *bus, int phyad,
				      int regad, u16 val)
{
	struct rswitch_etha *etha = bus->priv;

	return rswitch_etha_mpsm_op(etha, false, MMF_C22, phyad, regad,
				    POP_WRITE, val);
}

/* Call of_node_put(port) after done */
static struct device_node *rswitch_get_port_node(struct rswitch_device *rdev)
{
	struct device_node *port = NULL;
	struct device_node *ports;
	int err = 0;
	u32 index, etha_mii_idx;

	ports = of_get_child_by_name(rdev->ndev->dev.parent->of_node,
				     "ethernet-ports");
	if (!ports)
		return NULL;

	for_each_available_child_of_node_scoped(ports, child) {
		if (of_property_read_u32(child, "reg", &index))
			break;

		if (index == rdev->etha->index) {
			port = of_node_get(child);

			if (rdev->priv->info->rswitch_version == RSW20)
				break;

			if (!of_device_is_available(port))
				port = NULL;

			err = of_property_read_u32(port, "renesas,etha-mii-chan", &etha_mii_idx);
			if (err < 0)
				etha_mii_idx = index;

			rdev->etha_mii_idx = etha_mii_idx;

			break;
		}
	}

	of_node_put(ports);

	return port;
}

static int rswitch_etha_get_params(struct rswitch_device *rdev)
{
	u32 max_speed;
	int err;

	if (!rdev->np_port)
		return 0;	/* ignored */

	rdev->phy_less = of_property_read_bool(rdev->np_port, "renesas,phy_less");
	if (!rdev->phy_less) {
		err = of_get_phy_mode(rdev->np_port, &rdev->etha->phy_interface);
		if (err)
			return err;
	}

	if (rdev->priv->info->rswitch_version != RSW20)
		rdev->etha->connect_to_xpcs = of_property_read_bool(rdev->np_port,
								    "renesas,connect_to_xpcs");
	else
		rdev->etha->connect_to_xpcs = false;

	err = of_property_read_u32(rdev->np_port, "max-speed", &max_speed);
	if (!err) {
		rdev->etha->speed = max_speed;
		return 0;
	}

	/* if no "max-speed" property, let's use default speed */
	switch (rdev->etha->phy_interface) {
	case PHY_INTERFACE_MODE_MII:
		rdev->etha->speed = SPEED_100;
		break;
	case PHY_INTERFACE_MODE_SGMII:
		rdev->etha->speed = SPEED_1000;
		break;
	case PHY_INTERFACE_MODE_USXGMII:
		rdev->etha->speed = SPEED_2500;
		break;
	case PHY_INTERFACE_MODE_5GBASER:
		rdev->etha->speed = SPEED_5000;
		break;
	case PHY_INTERFACE_MODE_10GBASER:
		rdev->etha->speed = SPEED_10000;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int rswitch_mii_register(struct rswitch_device *rdev)
{
	u8 version = rdev->priv->info->rswitch_version;
	struct device_node *mdio_np;
	struct mii_bus *mii_bus;
	int err;

	if (rdev->etha->mii)
		return 0;

	mii_bus = mdiobus_alloc();
	if (!mii_bus)
		return -ENOMEM;

	mii_bus->name = "rswitch_mii";
	sprintf(mii_bus->id, "etha%d", rdev->etha->index);
	mii_bus->priv = version == RSW20 ? rdev->etha : rdev->etha_mii;
	mii_bus->read_c45 = rswitch_etha_mii_read_c45;
	mii_bus->write_c45 = rswitch_etha_mii_write_c45;
	mii_bus->read = rswitch_etha_mii_read_c22;
	mii_bus->write = rswitch_etha_mii_write_c22;
	mii_bus->parent = &rdev->priv->pdev->dev;

	mdio_np = of_get_child_by_name(rdev->np_port, "mdio");
	err = of_mdiobus_register(mii_bus, mdio_np);
	if (err < 0) {
		mdiobus_free(mii_bus);
		goto out;
	}

	rdev->etha->mii = mii_bus;

out:
	of_node_put(mdio_np);

	return err;
}

static void rswitch_mii_unregister(struct rswitch_device *rdev)
{
	if (rdev->etha->mii) {
		mdiobus_unregister(rdev->etha->mii);
		mdiobus_free(rdev->etha->mii);
		rdev->etha->mii = NULL;
	}
}

static void rswitch_adjust_link(struct net_device *ndev)
{
	struct rswitch_device *rdev = netdev_priv(ndev);
	struct phy_device *phydev = ndev->phydev;
	int err;

	if (phydev->link != rdev->etha->link) {
		phy_print_status(phydev);
		if (phydev->link) {
			if (!rdev->serdes->power_count) {
				phy_power_on(rdev->serdes);
			} else {
				phy_power_off(rdev->serdes);
				phy_power_on(rdev->serdes);
			}
			if (phydev->link && !parallel_mode &&
			    phydev->speed != rdev->etha->speed) {
				rdev->etha->speed = phydev->speed;
				err = rswitch_etha_hw_init(rdev->etha,
							   rdev->ndev->dev_addr,
							   rdev->priv);
				if (err < 0) {
					netdev_err(ndev, "ETHA hw_init failed: %d\n", err);
					netif_carrier_off(ndev);
					rdev->etha->link = phydev->link;

					return;
				}
				phy_set_speed(rdev->serdes, rdev->etha->speed);
			}
		}
	}
}

static void rswitch_phy_remove_link_mode(struct rswitch_device *rdev,
					 struct phy_device *phydev)
{
	if (!rdev->priv->etha_no_runtime_change)
		return;

	switch (rdev->etha->speed) {
	case SPEED_100:
		phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_1000baseT_Full_BIT);
		phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_2500baseX_Full_BIT);
		phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_5000baseT_Full_BIT);
		phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_10000baseT_Full_BIT);
		break;
	case SPEED_1000:
		phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_100baseT_Full_BIT);
		phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_2500baseX_Full_BIT);
		phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_5000baseT_Full_BIT);
		phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_10000baseT_Full_BIT);
		break;
	case SPEED_2500:
		phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_100baseT_Full_BIT);
		phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_1000baseT_Full_BIT);
		phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_5000baseT_Full_BIT);
		phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_10000baseT_Full_BIT);
		break;
	case SPEED_5000:
		phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_100baseT_Full_BIT);
		phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_1000baseT_Full_BIT);
		phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_2500baseX_Full_BIT);
		phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_10000baseT_Full_BIT);
		break;
	case SPEED_10000:
		phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_100baseT_Full_BIT);
		phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_1000baseT_Full_BIT);
		phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_2500baseX_Full_BIT);
		phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_5000baseT_Full_BIT);
		break;
	default:
		break;
	}

	phy_set_max_speed(phydev, rdev->etha->speed);
}

static int rswitch_phy_device_init(struct rswitch_device *rdev)
{
	struct phy_device *phydev;
	struct device_node *phy;
	int err = -ENOENT;

	if (!rdev->np_port)
		return -ENODEV;

	if (of_phy_is_fixed_link(rdev->np_port)) {
		err = of_phy_register_fixed_link(rdev->np_port);
		if (err && err != -EEXIST)
			return err;
		phy = rdev->np_port;
		rdev->etha->fixed_link = true;
	} else {
		phy = of_parse_phandle(rdev->np_port, "phy-handle", 0);
		rdev->etha->fixed_link = false;
		if (!phy)
			return -ENODEV;
	}

	err = rswitch_etha_fixed_link(rdev->etha);
	if (err < 0)
		return err;

	/* Set phydev->host_interfaces before calling of_phy_connect() to
	 * configure the PHY with the information of host_interfaces.
	 */
	phydev = of_phy_find_device(phy);
	if (!phydev)
		goto out;
	__set_bit(rdev->etha->phy_interface, phydev->host_interfaces);
	phydev->mac_managed_pm = true;

	phydev = of_phy_connect(rdev->ndev, phy, rswitch_adjust_link, 0,
				rdev->etha->phy_interface);
	if (!phydev)
		goto out;

	phy_set_max_speed(phydev, SPEED_2500);
	phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_10baseT_Half_BIT);
	phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_10baseT_Full_BIT);
	phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_100baseT_Half_BIT);
	phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_1000baseT_Half_BIT);
	rswitch_phy_remove_link_mode(rdev, phydev);

	phy_attached_info(phydev);

	err = 0;
out:
	if (phy != rdev->np_port)
		of_node_put(phy);

	return err;
}

static void rswitch_phy_device_deinit(struct rswitch_device *rdev)
{
	if (rdev->ndev->phydev)
		phy_disconnect(rdev->ndev->phydev);
}

static int rswitch_serdes_set_params(struct rswitch_device *rdev)
{
	int err;

	err = phy_set_mode_ext(rdev->serdes, PHY_MODE_ETHERNET,
			       rdev->etha->phy_interface);
	if (err < 0)
		return err;

	return phy_set_speed(rdev->serdes, rdev->etha->speed);
}

static void rswitch_etha_set_exception_path(struct rswitch_private *priv)
{
	iowrite32(FDMACUFEF, priv->addr_mfwd + FWCEPRC2);
	/* RSW3 has more AXI chains, if needed EPCSD can be extended for this device */
	iowrite32(FIELD_PREP(EPCS, priv->info->gwca_index) |
		  FIELD_PREP(EPCSD, priv->gwca.l2_shared_rx_queue->index),
		  priv->addr_mfwd + FWCEPTC);
}

static int rswitch_ether_port_init_one(struct rswitch_device *rdev)
{
	int err;

	if (!parallel_mode) {
		rdev->serdes = devm_of_phy_get(&rdev->priv->pdev->dev,
					       rdev->np_port, NULL);
		if (IS_ERR(rdev->serdes)) {
			err = -EPROBE_DEFER;
			return err;
		}

		if (!rdev->etha->operated) {
			err = rswitch_etha_hw_init(rdev->etha,
						   rdev->ndev->dev_addr,
						   rdev->priv);
			if (err < 0)
				return err;

			if (rdev->priv->etha_no_runtime_change)
				rdev->etha->operated = true;
		}

		err = rswitch_mii_register(rdev);
		if (err < 0)
			return err;

		if (rdev->priv->info->rswitch_version == RSW20) {
			err = rswitch_phy_device_init(rdev);
			if (err < 0)
				goto err_phy_device_init;
		}

		err = rswitch_serdes_set_params(rdev);
		if (err < 0)
			goto err_serdes_set_params;
	}

	return 0;

err_phy_device_init:
	rswitch_mii_unregister(rdev);

err_serdes_set_params:
	rswitch_phy_device_deinit(rdev);

	return err;
}

static void rswitch_ether_port_deinit_one(struct rswitch_device *rdev)
{
	rswitch_phy_device_deinit(rdev);
	rswitch_mii_unregister(rdev);
}

static int rswitch_ether_port_init_all(struct rswitch_private *priv)
{
	struct rswitch_etha *etha;
	unsigned int i;
	int err;

	rswitch_etha_set_exception_path(priv);

	/* The HWUM states that if no PHY TX/RX clock is provided to RMAC, the transition from
	 * OPERATION to DISABLED of ETHA may not be possible. Therefore, change the etha_mii
	 * channels to OPERATION mode only after initializing all channels. This helps avoid
	 * timeouts when changing modes for other ETHA settings when the same port is used for
	 * both MDIO and data transfer.
	 */
	rswitch_for_each_enabled_port(priv, i) {
		if (priv->rdev[i]->disabled)
			continue;

		if (priv->rdev[i]->phy_less) {
			if (!rswitch_agent_clock_is_enabled(priv->addr_gwca, i))
				rswitch_agent_clock_ctrl(priv->addr_gwca, i, 1);

			err = rswitch_etha_hw_init_tsn_internal(priv, i, priv->etha[i].mac_addr);
			if (err)
				return err;

			/*
			 * Only TSN-ES0..2 need selector.
			 * MIOC[3] = 0: connect RSwitch3 port 5..7 to TSN-ES0..2.
			 * MIOC[3] = 1: connect RSwitch3 port 5..7 to external PHY/XPCS.
			 */
			if (i >= 5 && i <= 7) {
				etha = &priv->etha[i];

				if (!etha->addr) {
					err = -ENODEV;
					return err;
				}

				rswitch_modify(etha->addr, MIOC, BIT(3), 0);
			}
			continue;
		}

		err = rswitch_ether_port_init_one(priv->rdev[i]);
		if (err)
			return err;
	}

	rswitch_for_each_enabled_port(priv, i) {
		if (priv->rdev[i]->disabled)
			continue;

		if (priv->rdev[i]->phy_less)
			continue;

		if (priv->info->rswitch_version == RSW30) {
			err = rswitch_etha_mii_hw_start(priv->rdev[i]->etha_mii);
			if (err)
				goto err_serdes;
		}

		err = phy_init(priv->rdev[i]->serdes);
		if (err)
			goto err_serdes;
	}

	return 0;

err_serdes:
	rswitch_for_each_enabled_port_continue_reverse(priv, i)
		phy_exit(priv->rdev[i]->serdes);
	i = priv->num_ports;

	return err;
}

static void rswitch_ether_port_deinit_all(struct rswitch_private *priv)
{
	unsigned int i;

	rswitch_for_each_enabled_port(priv, i) {
		phy_exit(priv->rdev[i]->serdes);
		rswitch_ether_port_deinit_one(priv->rdev[i]);
	}
}

static int rswitch_open(struct net_device *ndev)
{
	struct rswitch_device *rdev = netdev_priv(ndev);
	unsigned long flags;
	int i, err;

	/* Init PHY if not already connected */
	if (!ndev->phydev && !rdev->phy_less) {
		err = rswitch_phy_device_init(rdev);
		if (err)
			return err;

		err = phy_init(rdev->serdes);
		if (err) {
			rswitch_phy_device_deinit(rdev);
			return err;
		}

		err = rswitch_serdes_set_params(rdev);
		if (err) {
			phy_exit(rdev->serdes);
			rswitch_phy_device_deinit(rdev);
			return err;
		}
	}

	if (bitmap_empty(rdev->priv->opened_ports, rdev->priv->num_ports))
		iowrite32(TS, rdev->priv->addr_gwca + GWTSDIE);

	for (i = 0; i < rdev->priv->info->num_tx_queues; i++)
		napi_enable(&rdev->tx_queue[i]->napi);
	for (i = 0; i < rdev->priv->info->num_rx_queues; i++)
		napi_enable(&rdev->rx_queue[i]->napi);

	spin_lock_irqsave(&rdev->priv->lock, flags);
	set_bit(rdev->port, rdev->priv->opened_ports);
	for (i = 0; i < rdev->priv->info->num_tx_queues; i++)
		rswitch_enadis_data_irq(rdev->priv, rdev->tx_queue[i]->index, true);
	for (i = 0; i < rdev->priv->info->num_rx_queues; i++)
		rswitch_enadis_data_irq(rdev->priv, rdev->rx_queue[i]->index, true);
	rswitch_enadis_data_irq(rdev->priv, rdev->priv->gwca.l2_shared_rx_queue->index, true);
	spin_unlock_irqrestore(&rdev->priv->lock, flags);

	if (!rdev->phy_less)
		phy_start(ndev->phydev);

	netif_start_queue(ndev);

	if (rdev->brdev)
		rswitch_update_l2_offload(rdev->priv);

	return 0;
}

static int rswitch_stop(struct net_device *ndev)
{
	struct rswitch_device *rdev = netdev_priv(ndev);
	struct sk_buff *ts_skb;
	unsigned long flags;
	unsigned int tag;
	int i;

	netif_tx_stop_all_queues(ndev);

	if (!rdev->phy_less)
		phy_stop(ndev->phydev);

	spin_lock_irqsave(&rdev->priv->lock, flags);
	for (i = 0; i < rdev->priv->info->num_tx_queues; i++)
		rswitch_enadis_data_irq(rdev->priv, rdev->tx_queue[i]->index, false);
	for (i = 0; i < rdev->priv->info->num_rx_queues; i++)
		rswitch_enadis_data_irq(rdev->priv, rdev->rx_queue[i]->index, false);
	clear_bit(rdev->port, rdev->priv->opened_ports);
	if (bitmap_empty(rdev->priv->opened_ports, 1))
		rswitch_enadis_data_irq(rdev->priv,
					rdev->priv->gwca.l2_shared_rx_queue->index,
					false);

	spin_unlock_irqrestore(&rdev->priv->lock, flags);

	for (i = 0; i < rdev->priv->info->num_tx_queues; i++)
		napi_disable(&rdev->tx_queue[i]->napi);
	for (i = 0; i < rdev->priv->info->num_rx_queues; i++)
		napi_disable(&rdev->rx_queue[i]->napi);

	if (rdev->brdev)
		rswitch_update_l2_offload(rdev->priv);

	if (bitmap_empty(rdev->priv->opened_ports, RSWITCH_MAX_NUM_PORTS))
		iowrite32(TS, rdev->priv->addr_gwca + GWTSDID);

	for_each_set_bit(tag, rdev->ts_skb_used, TS_TAGS_PER_PORT) {
		ts_skb = xchg(&rdev->ts_skb[tag], NULL);
		clear_bit(tag, rdev->ts_skb_used);
		if (ts_skb)
			dev_kfree_skb(ts_skb);
	}

	return 0;
}

static bool rswitch_ext_desc_set_info1(struct rswitch_device *rdev,
				       struct sk_buff *skb,
				       struct rswitch_ext_desc *desc)
{
	if (!parallel_mode)
		desc->info1 = cpu_to_le64(INFO1_DV(BIT(rdev->etha->index)) |
					  INFO1_IPV(GWCA_IPV_NUM) | INFO1_FMT);
	else
		desc->info1 = cpu_to_le64(INFO1_IPV(GWCA_IPV_NUM));

	if (skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) {
		unsigned int tag;

		tag = find_first_zero_bit(rdev->ts_skb_used, TS_TAGS_PER_PORT);
		if (tag == TS_TAGS_PER_PORT)
			return false;
		smp_mb(); /* order bitmap read before rdev->ts_skb[] write */
		rdev->ts_skb[tag] = skb_get(skb);
		set_bit(tag, rdev->ts_skb_used);

		skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
		desc->info1 |= cpu_to_le64(INFO1_TSUN(tag) | INFO1_TXC);

		skb_tx_timestamp(skb);
	}

	return true;
}

static bool rswitch_ext_desc_set(struct rswitch_device *rdev,
				 struct sk_buff *skb,
				 struct rswitch_ext_desc *desc,
				 dma_addr_t dma_addr, u16 len, u8 die_dt)
{
	rswitch_desc_set_dptr(&desc->desc, dma_addr);
	desc->desc.info_ds = cpu_to_le16(len);
	if (!rswitch_ext_desc_set_info1(rdev, skb, desc))
		return false;

	dma_wmb();

	desc->desc.die_dt = die_dt;

	return true;
}

static u8 rswitch_ext_desc_get_die_dt(unsigned int nr_desc, unsigned int index)
{
	if (nr_desc == 1)
		return DT_FSINGLE | DIE;
	if (index == 0)
		return DT_FSTART;
	if (nr_desc - 1 == index)
		return DT_FEND | DIE;
	return DT_FMID;
}

static u16 rswitch_ext_desc_get_len(u8 die_dt, unsigned int orig_len)
{
	switch (die_dt & DT_MASK) {
	case DT_FSINGLE:
	case DT_FEND:
		return (orig_len % RSWITCH_DESC_BUF_SIZE) ?: RSWITCH_DESC_BUF_SIZE;
	case DT_FSTART:
	case DT_FMID:
		return RSWITCH_DESC_BUF_SIZE;
	default:
		return 0;
	}
}

static netdev_tx_t rswitch_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct rswitch_device *rdev = netdev_priv(ndev);
	dma_addr_t dma_addr, dma_addr_orig;
	netdev_tx_t ret = NETDEV_TX_OK;
	struct rswitch_ext_desc *desc;
	struct rswitch_gwca_queue *gq;
	unsigned int i, nr_desc;
	u16 qidx, len;
	u8 die_dt;

	qidx = skb_get_queue_mapping(skb);
	if (qidx >= rdev->priv->info->num_tx_queues)
		qidx = 0;
	gq = rdev->tx_queue[qidx];

	nr_desc = (skb->len - 1) / RSWITCH_DESC_BUF_SIZE + 1;
	if (rswitch_get_num_cur_queues(gq) >= gq->ring_size - nr_desc) {
		netif_stop_subqueue(ndev, qidx);
		return NETDEV_TX_BUSY;
	}

	if (skb_put_padto(skb, ETH_ZLEN))
		return ret;

	dma_addr_orig = dma_map_single(ndev->dev.parent, skb->data, skb->len, DMA_TO_DEVICE);
	if (dma_mapping_error(ndev->dev.parent, dma_addr_orig))
		goto err_kfree;

	/* Stored the skb at the last descriptor to avoid skb free before hardware completes send */
	gq->skbs[(gq->cur + nr_desc - 1) % gq->ring_size] = skb;
	gq->unmap_addrs[(gq->cur + nr_desc - 1) % gq->ring_size] = dma_addr_orig;

	dma_wmb();

	/* DT_FSTART should be set at last. So, this is reverse order. */
	for (i = nr_desc; i-- > 0; ) {
		desc = &gq->tx_ring[rswitch_next_queue_index(gq, true, i)];
		die_dt = rswitch_ext_desc_get_die_dt(nr_desc, i);
		dma_addr = dma_addr_orig + i * RSWITCH_DESC_BUF_SIZE;
		len = rswitch_ext_desc_get_len(die_dt, skb->len);
		if (!rswitch_ext_desc_set(rdev, skb, desc, dma_addr, len, die_dt))
			goto err_unmap;
	}

	gq->cur = rswitch_next_queue_index(gq, true, nr_desc);
	rswitch_modify(rdev->addr, GWTRC(gq->index), 0, BIT(gq->index % 32));
	return ret;

err_unmap:
	gq->skbs[(gq->cur + nr_desc - 1) % gq->ring_size] = NULL;
	dma_unmap_single(ndev->dev.parent, dma_addr_orig, skb->len, DMA_TO_DEVICE);

err_kfree:
	dev_kfree_skb_any(skb);

	return ret;
}

void rswitch_update_stats(struct rswitch_device *rdev)
{
	void __iomem *addr_etha = rdev->etha->addr;
	u64 temp;

	/* Receive overflow Counter */
	rdev->r_stats.mrovfc += ioread32(addr_etha + MROVFC);
	/* Receive Header-CRC (PCH-CRC) error Counter */
	rdev->r_stats.mrhcrcec += ioread32(addr_etha + MRHCRCEC);
	/* Received good frame counter E-frames */
	rdev->r_stats.mrgfce += ioread32(addr_etha + MRGFCE);
	/* Received good frame counter P-frames */
	rdev->r_stats.mrgfcp += ioread32(addr_etha + MRGFCP);
	/* Received good broadcast frame counter */
	rdev->r_stats.mrbfc += ioread32(addr_etha + MRBFC);
	/* Received good multicast frame counter */
	rdev->r_stats.mrmfc += ioread32(addr_etha + MRMFC);
	/* Received good unicast frame counter */
	rdev->r_stats.mrufc += ioread32(addr_etha + MRUFC);
	/* Received PHY error frame counter */
	rdev->r_stats.mrpefc += ioread32(addr_etha + MRPEFC);
	/* Received nibble error frame counter */
	rdev->r_stats.mrnefc += ioread32(addr_etha + MRNEFC);
	/* Received FCS/mCRC error frame counter */
	rdev->r_stats.mrfmefc += ioread32(addr_etha + MRFMEFC);
	/* Received final fragment missing error frame count */
	rdev->r_stats.mrffmefc += ioread32(addr_etha + MRFFMEFC);
	/* Received C-fragment count error frame counter */
	rdev->r_stats.mrcfcefc += ioread32(addr_etha + MRCFCEFC);
	/* Received fragment count error frame counter */
	rdev->r_stats.mrfcefc += ioread32(addr_etha + MRFCEFC);
	/* Received RMAC filter error frame counter */
	rdev->r_stats.mrrcfefc += ioread32(addr_etha + MRRCFEFC);
	/* Received frame counter */
	rdev->r_stats.mrfc += ioread32(addr_etha + MRFC);
	/* Received good undersize error frame counter */
	rdev->r_stats.mrguefc += ioread32(addr_etha + MRGUEFC);
	/* Received bad undersize error frame counter */
	rdev->r_stats.mrbuefc += ioread32(addr_etha + MRBUEFC);
	/* Received good oversize error frame counter */
	rdev->r_stats.mrgoefc += ioread32(addr_etha + MRGOEFC);
	/* Received bad oversize error frame counter */
	rdev->r_stats.mrboefc += ioread32(addr_etha + MRBOEFC);
	/* Received byte counter E-frames */
	temp = (u64)ioread32(addr_etha + MRXBCEU);
	rdev->r_stats.mrxbceu += (temp << 32) + ioread32(addr_etha + MRXBCEL);
	/* Received byte counter P-frames */
	temp = (u64)ioread32(addr_etha + MRXBCPU);
	rdev->r_stats.mrxbcpu += (temp << 32) + ioread32(addr_etha + MRXBCPL);

	/* Transmitted good frame counter E-frames */
	rdev->r_stats.mtgfce += ioread32(addr_etha + MTGFCE);
	/* Transmitted good frame counter P-frames */
	rdev->r_stats.mtgfcp += ioread32(addr_etha + MTGFCP);
	/* Transmitted broadcast frame counter */
	rdev->r_stats.mtbfc += ioread32(addr_etha + MTBFC);
	/* Transmitted multicast frame counter */
	rdev->r_stats.mtmfc += ioread32(addr_etha + MTMFC);
	/* Transmitted unicast frame counter */
	rdev->r_stats.mtufc += ioread32(addr_etha + MTUFC);
	/* Transmitted error frame counter */
	rdev->r_stats.mtefc += ioread32(addr_etha + MTEFC);
	/* Transmitted byte counter E-frames Upper */
	temp = (u64)ioread32(addr_etha + MTXBCEU);
	rdev->r_stats.mtxbceu += (temp << 32) + ioread32(addr_etha + MTXBCEL);
	/* Transmitted byte counter P-frames Upper */
	temp = (u64)ioread32(addr_etha + MTXBCPU);
	rdev->r_stats.mtxbcpu += (temp << 32) + ioread32(addr_etha + MTXBCPL);

	/* Ethernet Agent Switch Minimum Frame Size Error CouNter */
	rdev->e_stats.eausmfsecn += ioread32(addr_etha + EAUSMFSECN);
	/* Ethernet Agent TAG Filtering Error CouNter */
	rdev->e_stats.eatfecn += ioread32(addr_etha + EATFECN);
	/* Ethernet Agent Frame Size Error CouNter */
	rdev->e_stats.eafsecn += ioread32(addr_etha + EAFSECN);
	/* Ethernet Agent Descriptor Queue Overflow Error CouNter */
	rdev->e_stats.eadqoecn += ioread32(addr_etha + EADQOECN);
	/* Ethernet Agent Descriptor Queue Security Error CouNter */
	rdev->e_stats.eadqsecn += ioread32(addr_etha + EADQSECN);
	/* Ethernet Agent ChecKSum Error CouNter */
	rdev->e_stats.eacksecn += ioread32(addr_etha + EACKSECN);
	/* Ethernet Agent Lost Descriptor CouNter */
	rdev->e_stats.ealdcn += ioread32(addr_etha + EALDCN);
	/* Ethernet Agent Received Frame CouNter E-frame per Octets 0 */
	rdev->e_stats.earfcneo0 += ioread32(addr_etha + EARFCNEO0);
	/* Ethernet Agent Received Frame CouNter E-frame per Octets 1 */
	rdev->e_stats.earfcneo1 += ioread32(addr_etha + EARFCNEO1);
	/* Ethernet Agent Received Frame CouNter E-frame per Octets 2 */
	rdev->e_stats.earfcneo2 += ioread32(addr_etha + EARFCNEO2);
	/* Ethernet Agent Received Frame CouNter E-frame per Octets 3 */
	rdev->e_stats.earfcneo3 += ioread32(addr_etha + EARFCNEO3);
	/* Ethernet Agent Received Frame CouNter E-frame per Octets 4 */
	rdev->e_stats.earfcneo4 += ioread32(addr_etha + EARFCNEO4);
	/* Ethernet Agent Received Frame CouNter E-frame per Octets 5 */
	rdev->e_stats.earfcneo5 += ioread32(addr_etha + EARFCNEO5);
	/* Ethernet Agent Received Frame CouNter E-frame per Octets 6 */
	rdev->e_stats.earfcneo6 += ioread32(addr_etha + EARFCNEO6);
	/* Ethernet Agent Received Frame CouNter P-frame per Octets 0 */
	rdev->e_stats.earfcnpo0 += ioread32(addr_etha + EARFCNPO0);
	/* Ethernet Agent Received Frame CouNter P-frame per Octets 1 */
	rdev->e_stats.earfcnpo1 += ioread32(addr_etha + EARFCNPO1);
	/* Ethernet Agent Received Frame CouNter P-frame per Octets 2 */
	rdev->e_stats.earfcnpo2 += ioread32(addr_etha + EARFCNPO2);
	/* Ethernet Agent Received Frame CouNter P-frame per Octets 3 */
	rdev->e_stats.earfcnpo3 += ioread32(addr_etha + EARFCNPO3);
	/* Ethernet Agent Received Frame CouNter P-frame per Octets 4 */
	rdev->e_stats.earfcnpo4 += ioread32(addr_etha + EARFCNPO4);
	/* Ethernet Agent Received Frame CouNter P-frame per Octets 5 */
	rdev->e_stats.earfcnpo5 += ioread32(addr_etha + EARFCNPO5);
	/* Ethernet Agent Received Frame CouNter P-frame per Octets 6 */
	rdev->e_stats.earfcnpo6 += ioread32(addr_etha + EARFCNPO6);
	/* Ethernet Agent Descriptor Queue Overflow Error CouNter Priority q (q=0..FRM_PRIO_N-1) */
	rdev->e_stats.eadqoecnp0 += ioread32(addr_etha + EADQOECNP(0));
	rdev->e_stats.eadqoecnp1 += ioread32(addr_etha + EADQOECNP(1));
	rdev->e_stats.eadqoecnp2 += ioread32(addr_etha + EADQOECNP(2));
	rdev->e_stats.eadqoecnp3 += ioread32(addr_etha + EADQOECNP(3));
	rdev->e_stats.eadqoecnp4 += ioread32(addr_etha + EADQOECNP(4));
	rdev->e_stats.eadqoecnp5 += ioread32(addr_etha + EADQOECNP(5));
	rdev->e_stats.eadqoecnp6 += ioread32(addr_etha + EADQOECNP(6));
	rdev->e_stats.eadqoecnp7 += ioread32(addr_etha + EADQOECNP(7));
	/* Ethernet Agent Descriptor Queue Overflow Error CouNter Cut Through */
	rdev->e_stats.eadqoecnct += ioread32(addr_etha + EADQOECNCT);
}

static void rswitch_get_stats64(struct net_device *ndev,
				struct rtnl_link_stats64 *storage)
{
	struct rswitch_device *rdev = netdev_priv(ndev);

	rswitch_update_stats(rdev);

	storage->rx_packets = rdev->r_stats.mrfc;
	storage->tx_packets = rdev->r_stats.mtufc + rdev->r_stats.mtmfc +
			      rdev->r_stats.mtbfc;
	storage->rx_bytes = rdev->r_stats.mrxbceu + rdev->r_stats.mtxbcpu;
	storage->tx_bytes = rdev->r_stats.mtxbceu + rdev->r_stats.mtxbcpu;
	storage->rx_errors = rdev->r_stats.mrpefc;
	storage->tx_errors = rdev->r_stats.mtefc;
	storage->rx_dropped = ndev->stats.rx_dropped;
	storage->tx_dropped = ndev->stats.tx_dropped;

	storage->multicast = rdev->r_stats.mrmfc + rdev->r_stats.mtmfc;

	/* detailed rx_errors: */
	storage->rx_length_errors = rdev->r_stats.mrguefc + rdev->r_stats.mrbuefc +
				    rdev->r_stats.mrgoefc + rdev->r_stats.mrboefc;
	storage->rx_over_errors = rdev->r_stats.mrovfc; //receive buffer overflow

	storage->rx_crc_errors = rdev->r_stats.mrfmefc + rdev->r_stats.mrhcrcec;
	storage->rx_frame_errors = rdev->r_stats.mrnefc +
				   rdev->r_stats.mrguefc + rdev->r_stats.mrbuefc +
				   rdev->r_stats.mrgoefc + rdev->r_stats.mrboefc;

	/* detailed tx_errors */
	storage->tx_aborted_errors = ndev->stats.tx_aborted_errors;
	storage->tx_carrier_errors = ndev->stats.tx_carrier_errors;
	storage->tx_fifo_errors = ndev->stats.tx_fifo_errors;
	storage->tx_heartbeat_errors = ndev->stats.tx_heartbeat_errors;
	storage->tx_window_errors = ndev->stats.tx_window_errors;
}

static int rswitch_hwstamp_get(struct net_device *ndev,
			       struct kernel_hwtstamp_config *config)
{
	struct rswitch_device *rdev = netdev_priv(ndev);
	struct rswitch_private *priv = rdev->priv;

	config->flags = 0;
	config->tx_type = priv->tstamp_tx_ctrl;
	config->rx_filter = priv->tstamp_rx_ctrl;

	return 0;
}

static int rswitch_hwstamp_set(struct net_device *ndev,
			       struct kernel_hwtstamp_config *config,
			       struct netlink_ext_ack *extack)
{
	struct rswitch_device *rdev = netdev_priv(ndev);
	enum hwtstamp_rx_filters tstamp_rx_ctrl;
	enum hwtstamp_tx_types tstamp_tx_ctrl;

	if (config->flags)
		return -EINVAL;

	switch (config->tx_type) {
	case HWTSTAMP_TX_OFF:
	case HWTSTAMP_TX_ON:
		tstamp_tx_ctrl = config->tx_type;
		break;
	default:
		return -ERANGE;
	}

	switch (config->rx_filter) {
	case HWTSTAMP_FILTER_NONE:
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
		tstamp_rx_ctrl = config->rx_filter;
		break;
	default:
		config->rx_filter = HWTSTAMP_FILTER_ALL;
		tstamp_rx_ctrl = HWTSTAMP_FILTER_ALL;
		break;
	}

	rdev->priv->tstamp_tx_ctrl = tstamp_tx_ctrl;
	rdev->priv->tstamp_rx_ctrl = tstamp_rx_ctrl;

	return 0;
}

static int rswitch_get_port_parent_id(struct net_device *ndev,
				      struct netdev_phys_item_id *ppid)
{
	struct rswitch_device *rdev = netdev_priv(ndev);
	const char *name;

	name = dev_name(&rdev->priv->pdev->dev);
	ppid->id_len = min_t(size_t, strlen(name), sizeof(ppid->id));
	memcpy(ppid->id, name, ppid->id_len);

	return 0;
}

static int rswitch_get_phys_port_name(struct net_device *ndev,
				      char *name, size_t len)
{
	struct rswitch_device *rdev = netdev_priv(ndev);

	snprintf(name, len, "tsn%d", rdev->port);

	return 0;
}

static int rsw3_mac_addr_search(struct rswitch_private *priv, const u8 *mac,
				struct rsw3_dst_mac_search_result *result)
{
	u32 val;
	int ret;

	iowrite32((mac[0] << 8) | mac[1], priv->addr_mfwd + RSW3_FWMACTS0);
	iowrite32((mac[2] << 24) | (mac[3] << 16) | (mac[4] << 8) | mac[5],
		  priv->addr_mfwd + RSW3_FWMACTS1);
	iowrite32(RSW3_FWMACTS2_MACTSEA(RSW3_MAX_MAC_ENTRY), priv->addr_mfwd + RSW3_FWMACTS2);
	iowrite32(RSW3_FWMACTS3_MAC_SEARCH, priv->addr_mfwd + RSW3_FWMACTS3);

	ret = rswitch_reg_wait(priv->addr_mfwd, RSW3_FWMACTSR0, RSW3_FWMACTSR0_MACTS, 0);
	if (ret)
		return ret;

	val = ioread32(priv->addr_mfwd + RSW3_FWMACTSR0);
	if ((val & RSW3_FWMACTSR0_MACSNF) == RSW3_FWMACTSR0_MACSNF_FOUND) {
		result->found = true;
		result->qidx = ioread32(priv->addr_mfwd + RSW3_FWMACTSR2(priv->info->gwca_index));
		result->entry = ioread32(priv->addr_mfwd + RSW3_FWMACTSR6);
	} else {
		result->found = false;
	}

	return 0;
}

static void rsw3_del_dst_mac_addr(struct rswitch_private *priv, const u8 *mac)
{
	iowrite32((mac[0] << 8) | mac[1], priv->addr_mfwd + RSW3_FWMACTL5);
	iowrite32((mac[2] << 24) | (mac[3] << 16) | (mac[4] << 8) | mac[5],
		  priv->addr_mfwd + RSW3_FWMACTL6);

	iowrite32(RSW3_FWMACTL1_MACED, priv->addr_mfwd + RSW3_FWMACTL1);

	iowrite32(RSW3_FWMACTL8_MACDVL(priv->gwca.agent_index), priv->addr_mfwd + RSW3_FWMACTL8);

	rswitch_reg_wait(priv->addr_mfwd, RSW3_FWMACTLR, RSW3_FWMACTLR_MACTL, 0);
}

static void rsw3_set_dst_mac_addr(struct net_device *ndev, const u8 *mac, unsigned int index)
{
	struct rswitch_device *rdev = netdev_priv(ndev);
	struct rswitch_private *priv = rdev->priv;
	struct rsw3_dst_mac_search_result mac_found;
	unsigned int qidx;
	u16 entry;
	u32 val;

	mutex_lock(&priv->m_lock);

	qidx = ((index - 1) % (priv->info->num_rx_queues - 1)) + 1;

	rsw3_mac_addr_search(priv, mac, &mac_found);
	if (mac_found.found) {
		if (mac_found.qidx == rdev->rx_queue[qidx]->index)
			goto out;

		rsw3_del_dst_mac_addr(priv, mac);
		entry = mac_found.entry;
	} else {
		val = ioread32(priv->addr_mfwd + RSW3_FWMACTEM);
		entry = RSW3_FWMACTEM_MACTUEN(val) + RSW3_FWMACTEM_MACTEN(val);
	}

	iowrite32(entry, priv->addr_mfwd + RSW3_FWMACTL0);
	iowrite32(RSW3_FWMACTL4_MACDSLVL(rdev->port), priv->addr_mfwd + RSW3_FWMACTL4);
	iowrite32((mac[0] << 8) | mac[1], priv->addr_mfwd + RSW3_FWMACTL5);
	iowrite32((mac[2] << 24) | (mac[3] << 16) | (mac[4] << 8) | mac[5],
		  priv->addr_mfwd + RSW3_FWMACTL6);

	iowrite32(rdev->rx_queue[qidx]->index,
		  priv->addr_mfwd + RSW3_FWMACTL7(priv->info->gwca_index));

	iowrite32(RSW3_FWMACTL8_MACDVL(priv->gwca.agent_index), priv->addr_mfwd + RSW3_FWMACTL8);

	rswitch_reg_wait(priv->addr_mfwd, RSW3_FWMACTLR, RSW3_FWMACTLR_MACTL, 0);

out:
	mutex_unlock(&priv->m_lock);
}

static void rswitch_set_rx_mode(struct net_device *ndev)
{
	struct rswitch_device *rdev = netdev_priv(ndev);
	struct netdev_hw_addr *ha;
	unsigned int idx = 1;

	if (rdev->priv->info->rswitch_version != RSW30)
		return;

	netdev_for_each_uc_addr(ha, ndev) {
		rsw3_set_dst_mac_addr(ndev, ha->addr, idx);
		idx++;
	}
}

static const struct net_device_ops rswitch_netdev_ops = {
	.ndo_open = rswitch_open,
	.ndo_stop = rswitch_stop,
	.ndo_start_xmit = rswitch_start_xmit,
	.ndo_get_stats64 = rswitch_get_stats64,
	.ndo_eth_ioctl = phy_do_ioctl_running,
	.ndo_setup_tc = rswitch_setup_tc,
	.ndo_get_port_parent_id = rswitch_get_port_parent_id,
	.ndo_get_phys_port_name = rswitch_get_phys_port_name,
	.ndo_validate_addr = eth_validate_addr,
	.ndo_set_mac_address = eth_mac_addr,
	.ndo_set_rx_mode = rswitch_set_rx_mode,
	.ndo_hwtstamp_get = rswitch_hwstamp_get,
	.ndo_hwtstamp_set = rswitch_hwstamp_set,
};

bool is_rdev(const struct net_device *ndev)
{
	return ndev && (ndev->netdev_ops == &rswitch_netdev_ops);
}

static int rswitch2_map_addr(struct platform_device *pdev, struct rswitch_private *priv)
{
	struct resource *res_mfwd;

	res_mfwd = platform_get_resource_byname(pdev, IORESOURCE_MEM, "secure_base");
	if (!res_mfwd) {
		dev_err(&pdev->dev, "invalid resource\n");
		return -EINVAL;
	}

	priv->ptp_priv = rcar_gen4_ptp_alloc(pdev);
	if (!priv->ptp_priv)
		return -ENOMEM;

	priv->addr_mfwd = devm_ioremap_resource(&pdev->dev, res_mfwd);
	if (IS_ERR(priv->addr_mfwd))
		return PTR_ERR(priv->addr_mfwd);

	priv->addr_mfab = priv->addr_mfwd + RSWITCH2_TOP_OFFSET;
	priv->addr_coma = priv->addr_mfwd + RSWITCH2_COMA_OFFSET;
	priv->addr_etha = priv->addr_mfwd + RSWITCH2_ETHA_OFFSET;
	priv->addr_gwca = priv->addr_mfwd + RSWITCH2_GWCA0_OFFSET +
			  RSWITCH_GWCA_SIZE * priv->info->gwca_index;
	priv->ptp_priv->addr = priv->addr_mfwd + RSWITCH_GPTP_OFFSET_S4;

	return 0;
}

static int rswitch3_map_addr(struct platform_device *pdev, struct rswitch_private *priv)
{
	struct resource *res_mfwd, *res_ptp;

	res_mfwd = platform_get_resource_byname(pdev, IORESOURCE_MEM, "secure_base");
	if (!res_mfwd) {
		dev_err(&pdev->dev, "invalid resource\n");
		return -EINVAL;
	}

	res_ptp = platform_get_resource_byname(pdev, IORESOURCE_MEM, "secure_ptp");
	if (!res_ptp) {
		dev_err(&pdev->dev, "invalid resource\n");
		return -EINVAL;
	}

	priv->ptp_priv = rcar_gen4_ptp_alloc(pdev);
	if (!priv->ptp_priv)
		return -ENOMEM;

	priv->addr_mfwd = devm_ioremap_resource(&pdev->dev, res_mfwd);
	if (IS_ERR(priv->addr_mfwd))
		return PTR_ERR(priv->addr_mfwd);

	priv->addr_mfab = priv->addr_mfwd + RSWITCH3_TOP_OFFSET;
	priv->addr_coma = priv->addr_mfwd + RSWITCH3_COMA_OFFSET;
	priv->addr_etha = priv->addr_mfwd + RSWITCH3_ETHA_OFFSET;
	priv->addr_gwca = priv->addr_mfwd + RSWITCH3_GWCA0_OFFSET +
			  RSWITCH_GWCA_SIZE * priv->info->gwca_index;

	priv->ptp_priv->addr = devm_ioremap_resource(&pdev->dev, res_ptp);
	if (IS_ERR(priv->ptp_priv->addr))
		return PTR_ERR(priv->ptp_priv->addr);

	return 0;
}

static const struct rswitch_hw_info rswitch_rsw2_hw_info = {
	.rswitch_map_addr = rswitch2_map_addr,
	.bpool_config = rswitch2_bpool_config,
	.rswitch_fwd_init = rswitch2_fwd_init,
	.rswitch_etha_enable_mii = rswitch2_etha_enable_mii,
	.rswitch_rmac_setting = rswitch2_rmac_setting,
	.rswitch_num_ports = RSWITCH_NUM_PORTS_RSW2,
	.rswitch_num_agents = RSWITCH_NUM_AGENTS_RSW2,
	.agent_index_gwca = AGENT_INDEX_GWCA_RSW2,
	.gwca_index = GWCA_INDEX_RSW2,
	.num_tx_queues = 1,
	.num_rx_queues = 1,
	.rswitch_version = RSW20,
};

static const struct rswitch_hw_info rswitch_rsw30_hw_info = {
	.rswitch_map_addr = rswitch3_map_addr,
	.bpool_config = rswitch3_bpool_config,
	.rswitch_fwd_init = rswitch3_fwd_init,
	.rswitch_etha_enable_mii = rswitch3_etha_enable_mii,
	.rswitch_rmac_setting = rswitch3_rmac_setting,
	.rswitch_num_ports = RSWITCH_NUM_PORTS_RSW3,
	.rswitch_num_agents = RSWITCH_NUM_AGENTS_RSW3,
	.agent_index_gwca = AGENT_INDEX_GWCA_RSW3,
	.gwca_index = GWCA_INDEX_RSW3,
	.num_tx_queues = 4,
	.num_rx_queues = 4,
	.rswitch_version = RSW30,
};

static const struct of_device_id renesas_eth_sw_of_table[] = {
	{ .compatible = "renesas,r8a779f0-ether-switch", .data = &rswitch_rsw2_hw_info },
	{ .compatible = "renesas,r8a78000-ether-switch", .data = &rswitch_rsw30_hw_info },
	{ }
};
MODULE_DEVICE_TABLE(of, renesas_eth_sw_of_table);

static u32 rswitch_calc_psmcs(struct rswitch_private *priv)
{
	unsigned long rate = 0;
	bool relaxed;
	u32 psmcs;

	relaxed = of_property_read_bool(priv->pdev->dev.of_node, "renesas,mdc-relaxed");

	if (priv->clk)
		rate = clk_get_rate(priv->clk);

	if (!rate) {
		rate = relaxed ? RSWITCH3_FALLBACK_CLK_RELAX : RSWITCH3_FALLBACK_CLK_SPEC;
		dev_warn(&priv->pdev->dev,
			 "clk_get_rate() returned 0, using fallback %lu.%06lu MHz%s\n",
			 rate / 1000000, rate % 1000000,
			 relaxed ? " (relaxed)" : " (specific)");
	}

	/*
	 * PSMCS = round( clk / (MDC * 2) ) - 1
	 * Use rounding to minimize MDC error; clamp to field width if needed.
	 */
	psmcs = DIV_ROUND_CLOSEST(rate, (RSWITCH3_MDC_HZ * 2));
	if (psmcs)
		psmcs -= 1;

	/* If MPIC_PSMCS field has a max width (e.g. 10 bits), clamp here.
	 * Example: psmcs &= MPIC_PSMCS_MASK;
	 * (left as-is if macro not available)
	 */
	return psmcs;
}

static void rswitch_etha_init(struct rswitch_private *priv, unsigned int index)
{
	struct rswitch_etha *etha = &priv->etha[index];
	struct rswitch_etha *etha_mii = &priv->etha_mii[index];

	memset(etha, 0, sizeof(*etha));
	etha->index = index;
	etha->addr = priv->addr_etha  + index * RSWITCH_ETHA_SIZE;
	etha->coma_addr = priv->addr_coma;
	etha->priv = priv;

	/* MPIC.PSMCS = (clk [MHz] / (MDC frequency [MHz] * 2) - 1.
	 * Calculating PSMCS value as MDC frequency = 2.5MHz. So, multiply
	 * both the numerator and the denominator by 10.
	 */
	etha->psmcs = clk_get_rate(priv->clk) / 100000 / (25 * 2) - 1;

	if (priv->info->rswitch_version != RSW20) {
		memset(etha_mii, 0, sizeof(*etha));
		etha_mii->index = index;
		etha_mii->addr = priv->addr_etha + index * RSWITCH_ETHA_SIZE;
		etha_mii->coma_addr = priv->addr_coma;
		etha_mii->psmcs = rswitch_calc_psmcs(priv);
	}
}

int rswitch_attach_tsnes(struct device *dev, u32 tsnes_id, u32 rsw_port,
			 u32 fwd_mask, const u8 *mac)
{
	struct rswitch_private *priv = dev_get_drvdata(dev);
	struct rswitch_etha *etha;
	int ret = 0;

	if (priv->info->rswitch_version != RSW30)
		return 0;

	if (!priv)
		return -EPROBE_DEFER;

	if (tsnes_id >= RSWITCH3_NUM_TSNES)
		return -EINVAL;

	if (rsw_port != RSWITCH3_TSNES_PORT_BASE + tsnes_id)
		return -EINVAL;

	mutex_lock(&priv->tsnes_lock);

	if (test_bit(tsnes_id, priv->tsnes_attached)) {
		ret = -EBUSY;
		goto out_unlock;
	}

	/* This is skipped if the TSN[rsw_port] is configured in the DTB. */
	if (!priv->rdev[rsw_port]->phy_less) {
		if (!rswitch_agent_clock_is_enabled(priv->addr_gwca, rsw_port))
			rswitch_agent_clock_ctrl(priv->addr_gwca, rsw_port, 1);

		ret = rswitch_etha_hw_init_tsn_internal(priv, rsw_port, mac);
		if (ret)
			goto out_unlock;

		/*
		 * Only TSN-ES0..2 need selector.
		 * MIOC[3] = 0: connect RSwitch3 port 5..7 to TSN-ES0..2.
		 * MIOC[3] = 1: connect RSwitch3 port 5..7 to external PHY/XPCS.
		 */
		if (tsnes_id <= 2) {
			etha = &priv->etha[rsw_port];

			if (!etha->addr) {
				ret = -ENODEV;
				goto out_unlock;
			}

			rswitch_modify(etha->addr, MIOC, BIT(3), 0);
		}
	}

	set_bit(tsnes_id, priv->tsnes_attached);
	priv->tsnes_fwd_mask[tsnes_id] = fwd_mask;
	rswitch_enable_tsn_forwarding(priv, tsnes_id, rsw_port, fwd_mask);

out_unlock:
	mutex_unlock(&priv->tsnes_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(rswitch_attach_tsnes);

void rswitch_detach_tsnes(struct device *dev, u32 tsnes_id, u32 rsw_port)
{
	struct rswitch_private *priv = dev_get_drvdata(dev);

	if (priv->info->rswitch_version != RSW30)
		return;

	if (!priv || tsnes_id >= RSWITCH3_NUM_TSNES)
		return;

	mutex_lock(&priv->tsnes_lock);

	clear_bit(tsnes_id, priv->tsnes_attached);
	priv->tsnes_fwd_mask[tsnes_id] = 0;
	rswitch3_fwd_init(priv);

	mutex_unlock(&priv->tsnes_lock);
}
EXPORT_SYMBOL_GPL(rswitch_detach_tsnes);

static int rswitch_device_alloc(struct rswitch_private *priv, unsigned int index)
{
	struct platform_device *pdev = priv->pdev;
	struct rswitch_device *rdev;
	struct net_device *ndev;
	int err;

	if (index >= priv->num_ports)
		return -EINVAL;

	ndev = devm_alloc_etherdev_mqs(&pdev->dev, sizeof(struct rswitch_device),
				       priv->info->num_tx_queues,
				       priv->info->num_rx_queues);
	if (!ndev)
		return -ENOMEM;

	netif_set_real_num_tx_queues(ndev, priv->info->num_tx_queues);
	netif_set_real_num_rx_queues(ndev, priv->info->num_rx_queues);

	SET_NETDEV_DEV(ndev, &pdev->dev);
	ether_setup(ndev);

	rdev = netdev_priv(ndev);
	rdev->ndev = ndev;
	rdev->priv = priv;
	priv->rdev[index] = rdev;
	rdev->port = index;
	rdev->etha = &priv->etha[index];
	if (priv->info->rswitch_version != RSW20)
		rdev->etha_mii = &priv->etha_mii[index];
	rdev->addr = priv->addr_gwca;
	rdev->rx_old_queue = NULL;

	ndev->base_addr = (unsigned long)priv->addr_mfwd;
	snprintf(ndev->name, IFNAMSIZ, "tsn%d", index);
	ndev->netdev_ops = &rswitch_netdev_ops;
	rswitch_set_ethtool_ops(ndev);
	ndev->max_mtu = RSWITCH_MAX_MTU;
	ndev->min_mtu = ETH_MIN_MTU;

	rdev->np_port = rswitch_get_port_node(rdev);
	rdev->disabled = !rdev->np_port;
	err = of_get_ethdev_address(rdev->np_port, ndev);
	if (err) {
		if (is_valid_ether_addr(rdev->etha->mac_addr))
			eth_hw_addr_set(ndev, rdev->etha->mac_addr);
		else
			eth_hw_addr_random(ndev);
	}

	if (priv->info->rswitch_version != RSW20)
		rdev->etha_mii = &priv->etha_mii[rdev->etha_mii_idx];

	err = rswitch_etha_get_params(rdev);
	if (err < 0)
		goto out_get_params;

	err = rswitch_rxdmac_alloc(ndev);
	if (err < 0)
		goto out_rxdmac;

	err = rswitch_txdmac_alloc(ndev);
	if (err < 0)
		goto out_txdmac;

	list_add_tail(&rdev->list, &priv->port_list);

	return 0;

out_txdmac:
	rswitch_rxdmac_free(ndev);

out_rxdmac:
out_get_params:
	of_node_put(rdev->np_port);

	return err;
}

static void rswitch_device_free(struct rswitch_private *priv, unsigned int index)
{
	struct rswitch_device *rdev = priv->rdev[index];
	struct net_device *ndev = rdev->ndev;

	list_del(&rdev->list);
	if (!priv->rdev[index]->disabled) {
		rswitch_txdmac_free(ndev);
		rswitch_rxdmac_free(ndev);
	}
	of_node_put(rdev->np_port);
}

static int rswitch_init(struct rswitch_private *priv)
{
	struct platform_device *pdev = priv->pdev;
	unsigned int i;
	int err;

	for (i = 0; i < priv->num_ports; i++)
		rswitch_etha_init(priv, i);

	if (!parallel_mode)
		rswitch_clock_enable(priv);

	for (i = 0; i < priv->num_ports; i++)
		rswitch_etha_read_mac_address(&priv->etha[i]);

	rswitch_reset(priv);

	if (!parallel_mode)
		rswitch_clock_enable(priv);

	if (!parallel_mode) {
		err = priv->info->bpool_config(priv);
		if (err < 0)
			return err;

		rswitch_coma_init(priv);

		err = rswitch_gwca_linkfix_alloc(priv);
		if (err < 0)
			return -ENOMEM;

		err = rswitch_gwca_ts_queue_alloc(priv);
		if (err < 0)
			goto err_ts_queue_alloc;
	}

	for (i = 0; i < priv->num_ports; i++) {
		err = rswitch_device_alloc(priv, i);
		if (err < 0) {
			for (; i-- > 0; )
				rswitch_device_free(priv, i);
			goto err_device_alloc;
		}
	}

	rswitch_top_init(priv);

	if (!parallel_mode) {
		err = priv->info->rswitch_fwd_init(priv);
		if (err < 0)
			goto err_fwd_init;
	}

	err = rcar_gen4_ptp_register(priv->ptp_priv,
				     RCAR_GEN4_PTP_REG_LAYOUT, clk_get_rate(priv->clk));
	if (err < 0)
		goto err_ptp_register;

	err = rswitch_gwca_request_irqs(priv);
	if (err < 0)
		goto err_gwca_request_irq;

	if (priv->info->rswitch_version == RSW20) {
		err = rswitch_gwca_ts_request_irqs(priv);
		if (err < 0)
			goto err_gwca_ts_request_irq;
	}

	err = rswitch_gwca_hw_init(priv);
	if (err < 0)
		goto err_gwca_hw_init;

	err = rswitch_ether_port_init_all(priv);
	if (err)
		goto err_ether_port_init_all;

	rswitch_for_each_enabled_port(priv, i) {
		err = devm_register_netdev(&pdev->dev, priv->rdev[i]->ndev);
		if (err)
			goto err_register_netdev;
	}

	rswitch_for_each_enabled_port(priv, i)
		netdev_info(priv->rdev[i]->ndev, "MAC address %pM\n",
			    priv->rdev[i]->ndev->dev_addr);

	return 0;

err_register_netdev:
	rswitch_ether_port_deinit_all(priv);

err_ether_port_init_all:
	rswitch_gwca_hw_deinit(priv);

err_gwca_hw_init:
err_gwca_ts_request_irq:
err_gwca_request_irq:
	rcar_gen4_ptp_unregister(priv->ptp_priv);

err_fwd_init:
err_ptp_register:
	for (i = 0; i < priv->num_ports; i++)
		rswitch_device_free(priv, i);

err_device_alloc:
	rswitch_gwca_ts_queue_free(priv);

err_ts_queue_alloc:
	rswitch_gwca_linkfix_free(priv);

	return err;
}

static void rswitch_deinit(struct rswitch_private *priv)
{
	unsigned int i;

	rswitch_gwca_hw_deinit(priv);
	rcar_gen4_ptp_unregister(priv->ptp_priv);

	rswitch_for_each_enabled_port(priv, i) {
		struct rswitch_device *rdev = priv->rdev[i];
		rswitch_ether_port_deinit_one(rdev);
		phy_exit(priv->rdev[i]->serdes);
	}

	for (i = 0; i < priv->num_ports; i++)
		rswitch_device_free(priv, i);

	rswitch_gwca_ts_queue_free(priv);
	rswitch_gwca_linkfix_free(priv);

	cancel_work_sync(&priv->switchdev_work->work);

	rswitch_clock_disable(priv);
}

static const struct soc_device_attribute rswitch_soc_no_speed_change[]  = {
	{ .soc_id = "r8a779f0", .revision = "ES1.0" },
	{ /* Sentinel */ }
};

static int renesas_eth_sw_probe(struct platform_device *pdev)
{
	const struct soc_device_attribute *attr;
	const struct rswitch_hw_info *info;
	struct rswitch_private *priv;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	spin_lock_init(&priv->lock);

	info = of_device_get_match_data(&pdev->dev);
	if (info)
		priv->info = info;
	else
		return PTR_ERR(info);

	priv->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(priv->clk)) {
		dev_warn(&pdev->dev, "no input clock; will use fallback frequency for PSMCS (%pe)\n",
			 priv->clk);
		priv->clk = NULL;
	} else {
		ret = clk_prepare_enable(priv->clk);
		if (ret) {
			dev_warn(&pdev->dev,
				 "failed to enable clock (%d); will use fallback frequency\n", ret);
			priv->clk = NULL;
		}
	}

	attr = soc_device_match(rswitch_soc_no_speed_change);
	if (attr)
		priv->etha_no_runtime_change = true;

	if (!parallel_mode)
		parallel_mode = of_property_read_bool(pdev->dev.of_node, "parallel_mode");

	if (parallel_mode)
		priv->num_ports = 1;
	else
		priv->num_ports = priv->info->rswitch_num_ports;

	platform_set_drvdata(pdev, priv);
	priv->pdev = pdev;
	ret = info->rswitch_map_addr(pdev, priv);
	if (ret < 0)
		goto err_sw_probe;

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(40));
	if (ret < 0) {
		ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
		if (ret < 0)
			goto err_sw_probe;
	}

	mutex_init(&priv->m_lock);

	priv->gwca.agent_index = priv->info->agent_index_gwca;
	priv->gwca.num_queues = min(priv->num_ports *
				    (priv->info->num_tx_queues + priv->info->num_rx_queues),
				    RSWITCH_MAX_NUM_QUEUES);
	/* One extra queue for L2 switch reception */
	priv->gwca.num_queues = min(priv->gwca.num_queues + 1,
				    RSWITCH_MAX_NUM_QUEUES);
	priv->gwca.queues = devm_kcalloc(&pdev->dev, priv->gwca.num_queues,
					 sizeof(*priv->gwca.queues), GFP_KERNEL);
	if (!priv->gwca.queues) {
		ret = -ENOMEM;
		goto err_sw_probe;
	}

	INIT_LIST_HEAD(&priv->port_list);

	if (!parallel_mode) {
		pm_runtime_enable(&pdev->dev);
		pm_runtime_get_sync(&pdev->dev);
	}

	ret = rswitch_init(priv);
	if (ret < 0)
		goto err_pm_runtime;

	if (list_empty(&priv->port_list))
		dev_warn(&pdev->dev, "could not initialize any ports\n");

	ret = rswitch_register_notifiers(priv);
	if (ret) {
		dev_err(&pdev->dev, "could not register notifiers\n");
		goto err_register_notifiers;
	}

	device_set_wakeup_capable(&pdev->dev, 1);

	return 0;
err_register_notifiers:
	rswitch_deinit(priv);

err_pm_runtime:
	if (!parallel_mode) {
		pm_runtime_put(&pdev->dev);
		pm_runtime_disable(&pdev->dev);
	}

err_sw_probe:
	clk_disable_unprepare(priv->clk);

	return ret;
}

static void renesas_eth_sw_remove(struct platform_device *pdev)
{
	struct rswitch_private *priv = platform_get_drvdata(pdev);

	rswitch_unregister_notifiers(priv);

	if (priv->clk)
		clk_disable_unprepare(priv->clk);

	rswitch_deinit(priv);

	pm_runtime_put(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	platform_set_drvdata(pdev, NULL);
}

static int renesas_eth_sw_suspend(struct device *dev)
{
	struct rswitch_private *priv = dev_get_drvdata(dev);
	struct net_device *ndev;
	unsigned int i;

	rswitch_for_each_enabled_port(priv, i) {
		ndev = priv->rdev[i]->ndev;
		if (netif_running(ndev)) {
			netif_device_detach(ndev);
			rswitch_stop(ndev);
		}
		if (priv->rdev[i]->serdes->init_count) {
			phy_exit(priv->rdev[i]->serdes);
			priv->rdev[i]->serdes->init_count = 0;
		}

		rswitch_txdmac_free(ndev);
		rswitch_rxdmac_free(ndev);
	}

	if (priv->info->rswitch_version != RSW20) {
		for (i = 0; i < priv->num_ports; i++) {
			struct rswitch_etha *etha_mii = &priv->etha_mii[i];

			etha_mii->operated = false;
		}
	}

	clk_disable_unprepare(priv->clk);

	return 0;
}

static int renesas_eth_sw_resume(struct device *dev)
{
	struct rswitch_private *priv = dev_get_drvdata(dev);
	struct net_device *ndev;
	unsigned int i;

	if (clk_prepare_enable(priv->clk)) {
		dev_warn(dev, "Failed to enable clock, will use fallback frequency\n");
		priv->clk = NULL;
	}

	rswitch_clock_enable(priv);
	priv->info->bpool_config(priv);
	rswitch_coma_init(priv);
	priv->info->rswitch_fwd_init(priv);

	rswitch_for_each_enabled_port(priv, i) {
		ndev = priv->rdev[i]->ndev;
		rswitch_rxdmac_alloc(ndev);
		rswitch_txdmac_alloc(ndev);
	}

	rswitch_top_init(priv);
	rswitch_gwca_hw_init(priv);
	rswitch_ether_port_init_all(priv);
	rcar_gen4_ptp_reinit_hw(priv->ptp_priv);

	rswitch_for_each_enabled_port(priv, i) {
		ndev = priv->rdev[i]->ndev;
		if (netif_running(ndev)) {
			rswitch_open(ndev);
			netif_device_attach(ndev);
		}
	}

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(renesas_eth_sw_pm_ops, renesas_eth_sw_suspend,
				renesas_eth_sw_resume);

static struct platform_driver renesas_eth_sw_driver_platform = {
	.probe = renesas_eth_sw_probe,
	.remove = renesas_eth_sw_remove,
	.driver = {
		.name = "renesas_eth_sw",
		.pm = pm_sleep_ptr(&renesas_eth_sw_pm_ops),
		.of_match_table = renesas_eth_sw_of_table,
	}
};
module_platform_driver(renesas_eth_sw_driver_platform);
module_param(parallel_mode, bool, 0644);
MODULE_PARM_DESC(parallel_mode, "Operate simultaneously with Realtime core");
MODULE_AUTHOR("Yoshihiro Shimoda, Michael Dege");
MODULE_DESCRIPTION("Renesas Ethernet Switch device driver");
MODULE_LICENSE("GPL");
