// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for Renesas Ethernet Switch3.
 * The implementation is based on the existing Ethernet
 * Switch2 driver for R-Car S4-8
 *
 * Copyright (C) 2025 Renesas Electronics Corporation
 */

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/etherdevice.h>
#include <linux/iopoll.h>
#include <linux/if_vlan.h>
#include <linux/ethtool.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/net_tstamp.h>
#include <linux/of.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/clk.h>
#include <linux/rtnetlink.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/sys_soc.h>
#include <linux/reset.h>

#include "rswitch3.h"

static u8 cached_mac_addresses[RSWITCH3_NUM_PORTS][ETH_ALEN];
static bool mac_cache_valid[RSWITCH3_NUM_PORTS];
static int rsw3_rx_poll(struct napi_struct *napi, int budget);
static int rsw3_tx_poll(struct napi_struct *napi, int budget);

static int rsw3_reg_wait(void __iomem *addr, u32 offs, u32 mask, u32 expected)
{
	u32 val;

	if (!addr) {
		pr_err("%s: Invalid address\n", __func__);
		return -EINVAL;
	}

	int ret = readl_poll_timeout_atomic(addr + offs, val, (val & mask) == expected,
					    1, RSWITCH3_TIMEOUT_US);

	if (ret) {
		pr_err("%s: Timeout waiting addr: 0x%p, offset: 0x%x, mask: 0x%x, expected: 0x%x\n",
		       __func__, addr, offs, mask, expected);
	} else {
		pr_debug("%s: Success addr: 0x%p, offset: 0x%x, val: 0x%x\n", __func__,
			 addr, offs, val);
	}

	return ret;
}

static void rsw3_modify(void __iomem *addr, enum rsw3_reg reg, u32 clear, u32 set)
{
	iowrite32((ioread32(addr + reg) & ~clear) | set, addr + reg);
}

/* Common Agent block (COMA) */
static void rsw3_reset(struct rsw3_private *priv)
{
	if (!parallel_mode) {
		iowrite32(RRC_RR, priv->addr + RRC);
		iowrite32(RRC_RR_CLR, priv->addr + RRC);
	} else {
		int gwca_idx;
		u32 gwro_offset;
		int mode;
		int count;

		if (priv->gwca.index == RSWITCH3_GWCA_IDX_TO_HW_NUM(0)) {
			gwca_idx = 14;
			gwro_offset = RSWITCH3_GWCA1_OFFSET;
		} else {
			gwca_idx = 13;
			gwro_offset = RSWITCH3_GWCA0_OFFSET;
		}

		count = 0;
		do {
			mode = ioread32(priv->addr + gwro_offset + 0x4) & GWMS_OPS_MASK;
			if (mode == GWMC_OPC_OPERATION)
				break;

			count++;
			if (!(count % 100))
				pr_info("rswitch wait for GWMS%d %d==%d\n", gwca_idx, mode,
					GWMC_OPC_OPERATION);
			mdelay(10);
		} while (1);
	}
}

static void rsw3_clock_enable(struct rsw3_private *priv)
{
	iowrite32(RCEC_ACE_DEFAULT | RCEC_RCE, priv->addr + RCEC);
}

static void rsw3_clock_disable(struct rsw3_private *priv)
{
	iowrite32(RCDC_RCD, priv->addr + RCDC);
}

static bool rsw3_agent_clock_is_enabled(void __iomem *coma_addr,
					unsigned int port)
{
	u32 val = ioread32(coma_addr + RCEC);

	if (!(val & RCEC_RCE)) {
		pr_debug("rswitch: Agent clock is not enabled\n");
		return false;
	}

	return !!(val & BIT(port));
}

static void rsw3_agent_clock_ctrl(void __iomem *coma_addr, unsigned int port,
				  int enable)
{
	u32 val;

	if (enable) {
		val = ioread32(coma_addr + RCEC);
		iowrite32(val | RCEC_RCE | BIT(port), coma_addr + RCEC);
	} else {
		val = ioread32(coma_addr + RCDC);
		iowrite32(val | BIT(port), coma_addr + RCDC);
	}
}

static int rsw3_bpool_config(struct rsw3_private *priv)
{
	u32 val;
	int ret;

	val = ioread32(priv->addr + CABPIRM);
	if (val & CABPIRM_BPR)
		return 0;

	iowrite32(CABPIRM_BPIOG, priv->addr + CABPIRM);

	ret = rsw3_reg_wait(priv->addr, CABPIRM, CABPIRM_BPR, CABPIRM_BPR);
	if (ret) {
		pr_err("%s: Timeout waiting for buffer pool configuration\n", __func__);
		return ret;
	}

	return 0;
}

static void rsw3_coma_init(struct rsw3_private *priv)
{
	iowrite32(CABPPFLC_INIT_VALUE, priv->addr + CABPPFLC0);
}

/* R-Switch-3 block (TOP) */
static void rsw3_top_init(struct rsw3_private *priv)
{
	unsigned int i, j;

	rsw3_for_each_enabled_port(priv, i) {
		struct rsw3_device *rdev = priv->rdev[i];
		unsigned int reg, bit;

		if (rdev->disabled)
			continue;

		for (j = 0; j < NUM_QUEUES_TX_PER_NDEV; j++) {
			struct rsw3_gwca_queue *gq = rdev->tx_queue[j];
			unsigned int irq_idx = j % GWCA_NUM_IRQS;

			iowrite32(TPDEMIMC_GDICM(GWCA_INDEX, irq_idx),
				  priv->addr + TPDEMIMC(gq->index));

			reg = gq->index / 32;
			bit = BIT(gq->index % 32);
			priv->irq[irq_idx].irq_bits[reg] |= bit;
		}

		for (j = 0; j < NUM_QUEUES_RX_PER_NDEV; j++) {
			struct rsw3_gwca_queue *gq = rdev->rx_queue[j];
			unsigned int irq_idx = (j + NUM_QUEUES_TX_PER_NDEV) % GWCA_NUM_IRQS;

			iowrite32(TPDEMIMC_GDICM(GWCA_INDEX, irq_idx),
				  priv->addr + TPDEMIMC(gq->index));

			reg = gq->index / 32;
			bit = BIT(gq->index % 32);
			priv->irq[irq_idx].irq_bits[reg] |= bit;
		}
	}
}

static void rsw3_fwd_allow_ports(struct rsw3_private *priv,
					unsigned int src_port, u32 dst_mask)
{
	if (!dst_mask || src_port >= RSWITCH3_NUM_AGENTS)
		return;

	dst_mask &= GENMASK(RSWITCH3_NUM_AGENTS - 1, 0);

	rsw3_modify(priv->addr, FWPBFC(src_port), 0,
			FIELD_PREP(FWPBFC_PBDV, dst_mask));
	rsw3_modify(priv->addr, FWPBFC1(src_port), FWPBFC1_PBRP,
			FIELD_PREP(FWPBFC1_PBRP, FWPBFC1_PBRP_TSN));
	rsw3_modify(priv->addr, FWPC2(src_port),
			FWPC2_LTWFM_TO_PORT(dst_mask), 0);
	rsw3_modify(priv->addr, FWPC0(src_port), FWPC0_MACSDA,
			FWPC0_MACSDA);
}

static void rsw3_enable_tsn_forwarding(struct rsw3_private *priv,
						u32 tsnes_id, u32 rsw_port,
						u32 requested_mask)
{
	struct device *dev = &priv->pdev->dev;
	u32 enabled_ports;
	u32 external_ports;
	u32 missing_ports;
	unsigned int i;

	enabled_ports = 0;
	for (i = 0; i < RSWITCH3_NUM_PORTS; i++) {
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

	rsw3_fwd_allow_ports(priv, rsw_port, external_ports);

	for (i = 0; i < RSWITCH3_NUM_PORTS; i++) {
		if (!(external_ports & BIT(i)))
			continue;

		rsw3_fwd_allow_ports(priv, i, BIT(rsw_port));
	}
}

static void rsw3_attached_forwarding(struct rsw3_private *priv)
{
	u32 tsnes_id;

	for (tsnes_id = 0; tsnes_id < RSWITCH3_NUM_TSNES; tsnes_id++) {
		u32 rsw_port = RSWITCH3_TSNES_PORT_BASE + tsnes_id;

		if (!(priv->tsnes_attached & BIT(tsnes_id)))
			continue;

		rsw3_enable_tsn_forwarding(priv, tsnes_id, rsw_port,
					priv->tsnes_fwd_mask[tsnes_id]);
	}
}

/* Forwarding engine block (MFWD) */
static int rsw3_fwd_init(struct rsw3_private *priv)
{
	u32 all_ports_mask = GENMASK(RSWITCH3_NUM_AGENTS - 1, 0);
	unsigned int i;
	int ret;

	/* Start with empty configuration */
	for (i = 0; i < RSWITCH3_NUM_AGENTS; i++) {
		/* Disable all port features */
		iowrite32(0, priv->addr + FWPC0(i));
		/* Disallow L3 forwarding and direct descriptor forwarding */
		iowrite32(FIELD_PREP(FWCP1_LTHFW, all_ports_mask),
			  priv->addr + FWPC1(i));
		/* Disallow L2 forwarding */
		iowrite32(FIELD_PREP(FWPC2_LTWFM, all_ports_mask),
			  priv->addr + FWPC2(i));
		/* Disallow port based forwarding */
		iowrite32(0, priv->addr + FWPBFC(i));
		iowrite32(0, priv->addr + FWPBFC1(i));
	}

	/* For enabled ETHA ports, setup port based forwarding */
	rsw3_for_each_enabled_port(priv, i) {
		rsw3_modify(priv->addr, FWPBFC(i), FWPBFC_PBDV,
			    FIELD_PREP(FWPBFC_PBDV, BIT(priv->gwca.index)));

		if (priv->rdev[i]->disabled)
			continue;

		iowrite32(priv->rdev[i]->rx_queue[0]->index,
			  priv->addr + FWPBFCSDC(GWCA_INDEX, i));

		/* Allow L2 forwarding */
		iowrite32(FWPC0_MACSDA, priv->addr + FWPC0(i));
		rsw3_modify(priv->addr, FWPC2(i), FWPC2_LTWFM_TO_PORT(BIT(priv->gwca.index)), 0);
	}

	/* For GWCA port, allow direct descriptor forwarding */
	rsw3_modify(priv->addr, FWPC1(priv->gwca.index), FWPC1_DDE, FWPC1_DDE);

	rsw3_attached_forwarding(priv);

	/* Initialize MAC table */
	iowrite32(FWMACTIM_MACTIOG, priv->addr + FWMACTIM);
	ret = rsw3_reg_wait(priv->addr, FWMACTIM, FWMACTIM_MACTR, FWMACTIM_MACTR);

	return ret;

}

/* gPTP timer (gPTP) */
static void rsw3_get_timestamp(struct rsw3_private *priv,
			       struct timespec64 *ts)
{
	priv->ptp_priv->info.gettime64(&priv->ptp_priv->info, ts);
}

/* Gateway CPU agent block (GWCA) */
static int rsw3_gwca_change_mode(struct rsw3_private *priv,
				 enum rsw3_gwca_mode mode)
{
	int ret;

	if (!rsw3_agent_clock_is_enabled(priv->addr, priv->gwca.index))
		rsw3_agent_clock_ctrl(priv->addr, priv->gwca.index, 1);

	iowrite32(mode, priv->addr + GWMC);

	ret = rsw3_reg_wait(priv->addr, GWMS, GWMS_OPS_MASK, mode);

	if (mode == GWMC_OPC_DISABLE)
		rsw3_agent_clock_ctrl(priv->addr, priv->gwca.index, 0);

	return ret;
}

static int rsw3_gwca_mcast_table_reset(struct rsw3_private *priv)
{
	iowrite32(GWMTIRM_MTIOG, priv->addr + GWMTIRM);

	return rsw3_reg_wait(priv->addr, GWMTIRM, GWMTIRM_MTR, GWMTIRM_MTR);
}

static int rsw3_gwca_axi_ram_reset(struct rsw3_private *priv)
{
	iowrite32(GWARIRM_ARIOG, priv->addr + GWARIRM);

	return rsw3_reg_wait(priv->addr, GWARIRM, GWARIRM_ARR, GWARIRM_ARR);
}

static bool rsw3_is_any_data_irq(struct rsw3_private *priv, u32 *dis, u32 *mask)
{
	unsigned int i;

	for (i = 0; i < RSWITCH3_NUM_IRQ_REGS; i++) {
		if (dis[i] & mask[i])
			return true;
	}

	return false;
}

static void rsw3_get_data_irq_status(struct rsw3_private *priv, u32 *dis)
{
	int i;

	for (i = 0; i < RSWITCH3_NUM_IRQ_REGS; i++)
		dis[i] = ioread32(priv->addr + GWDIS0 + i * 0x10);
}

static void rsw3_enadis_data_irq(struct rsw3_private *priv, unsigned int index, bool enable)
{
	u32 offs = (enable ? GWDIE0 : GWDID0) + (index / 32) * 0x10;
	u32 tmp = 0;

	/* For VPF? */
	if (enable)
		tmp = ioread32(priv->addr + offs);

	iowrite32(BIT(index % 32) | tmp, priv->addr + offs);
}

static void rsw3_ack_data_irq(struct rsw3_private *priv, int index)
{
	u32 offs = GWDIS0 + (index / 32) * 0x10;

	spin_lock(&priv->lock);
	iowrite32(BIT(index % 32), priv->addr + offs);
	spin_unlock(&priv->lock);
}

static unsigned int rsw3_next_queue_index(struct rsw3_gwca_queue *gq,
					  bool cur, unsigned int num)
{
	unsigned int index = cur ? gq->cur : gq->dirty;

	if (index + num >= gq->ring_size)
		index = (index + num) % gq->ring_size;
	else
		index += num;

	return index;
}

static unsigned int rsw3_get_num_cur_queues(struct rsw3_gwca_queue *gq)
{
	if (gq->cur >= gq->dirty)
		return gq->cur - gq->dirty;
	else
		return gq->ring_size - gq->dirty + gq->cur;
}

static bool rsw3_is_queue_rxed(struct rsw3_gwca_queue *gq)
{
	struct rsw3_ext_ts_desc *desc = &gq->rx_ring[gq->dirty];

	if ((desc->desc.die_dt & DT_MASK) != DT_FEMPTY)
		return true;

	return false;
}

static int rsw3_gwca_queue_alloc_rx_buf(struct rsw3_gwca_queue *gq,
					unsigned int start_index, unsigned int num)
{
	unsigned int i, index;

	for (i = 0; i < num; i++) {
		index = (i + start_index) % gq->ring_size;
		if (gq->rx_bufs[index])
			continue;
		gq->rx_bufs[index] = netdev_alloc_frag(RSWITCH3_BUF_SIZE);
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

static void rsw3_gwca_queue_free(struct net_device *ndev,
				 struct rsw3_gwca_queue *gq)
{
	unsigned int i;

	if (!gq->dir_tx) {
		dma_free_coherent(ndev->dev.parent,
				  sizeof(struct rsw3_ext_ts_desc) *
				  (gq->ring_size + 1), gq->rx_ring, gq->ring_dma);
		gq->rx_ring = NULL;

		for (i = 0; i < gq->ring_size; i++)
			skb_free_frag(gq->rx_bufs[i]);
		kfree(gq->rx_bufs);
		gq->rx_bufs = NULL;
	} else {
		dma_free_coherent(ndev->dev.parent,
				  sizeof(struct rsw3_ext_desc) *
				  (gq->ring_size + 1), gq->tx_ring, gq->ring_dma);
		gq->tx_ring = NULL;
		kfree(gq->skbs);
		gq->skbs = NULL;
		kfree(gq->unmap_addrs);
		gq->unmap_addrs = NULL;
	}

	gq->cur = 0;
	gq->dirty = 0;
}

static int rsw3_gwca_queue_alloc(struct net_device *ndev,
				 struct rsw3_private *priv,
				 struct rsw3_gwca_queue *gq,
				 bool dir_tx, unsigned int ring_size)
{
	gq->dir_tx = dir_tx;
	gq->ring_size = ring_size;

	if (!dir_tx) {
		gq->rx_bufs = kcalloc(gq->ring_size, sizeof(*gq->rx_bufs), GFP_KERNEL);
		if (!gq->rx_bufs)
			return -ENOMEM;
		if (rsw3_gwca_queue_alloc_rx_buf(gq, 0, gq->ring_size) < 0)
			goto out;

		gq->rx_ring = dma_alloc_coherent(ndev->dev.parent,
						 sizeof(struct rsw3_ext_ts_desc) *
						 (gq->ring_size + 1), &gq->ring_dma, GFP_KERNEL);
	} else {
		gq->skbs = kcalloc(gq->ring_size, sizeof(*gq->skbs), GFP_KERNEL);
		if (!gq->skbs)
			return -ENOMEM;
		gq->unmap_addrs = kcalloc(gq->ring_size, sizeof(*gq->unmap_addrs), GFP_KERNEL);
		if (!gq->unmap_addrs)
			goto out;
		gq->tx_ring = dma_alloc_coherent(ndev->dev.parent,
						 sizeof(struct rsw3_ext_desc) *
						 (gq->ring_size + 1), &gq->ring_dma, GFP_KERNEL);
	}

	if (!gq->rx_ring && !gq->tx_ring)
		goto out;

	return 0;

out:
	rsw3_gwca_queue_free(ndev, gq);

	return -ENOMEM;
}

static void rsw3_desc_set_dptr(struct rsw3_desc *desc, dma_addr_t addr)
{
	desc->dptrl = cpu_to_le32(lower_32_bits(addr));
	desc->dptrh = upper_32_bits(addr) & 0xff;
}

static dma_addr_t rsw3_desc_get_dptr(const struct rsw3_desc *desc)
{
	return __le32_to_cpu(desc->dptrl) | (u64)(desc->dptrh) << 32;
}

static int rsw3_gwca_queue_format(struct net_device *ndev,
				  struct rsw3_private *priv,
				  struct rsw3_gwca_queue *gq)
{
	unsigned int ring_size = sizeof(struct rsw3_ext_desc) * gq->ring_size;
	struct rsw3_ext_desc *desc;
	struct rsw3_desc *linkfix;
	dma_addr_t dma_addr;
	unsigned int i;

	memset(gq->tx_ring, 0, ring_size);
	for (i = 0, desc = gq->tx_ring; i < gq->ring_size; i++, desc++) {
		if (!gq->dir_tx) {
			dma_addr = dma_map_single(ndev->dev.parent,
						  gq->rx_bufs[i] + RSWITCH3_HEADROOM,
						  RSWITCH3_MAP_BUF_SIZE,
						  DMA_FROM_DEVICE);
			if (dma_mapping_error(ndev->dev.parent, dma_addr))
				goto err;

			desc->desc.info_ds = cpu_to_le16(RSWITCH3_DESC_BUF_SIZE);
			rsw3_desc_set_dptr(&desc->desc, dma_addr);
			desc->desc.die_dt = DT_FEMPTY | DIE;
		} else {
			desc->desc.die_dt = DT_EEMPTY | DIE;
		}
	}
	rsw3_desc_set_dptr(&desc->desc, gq->ring_dma);
	desc->desc.die_dt = DT_LINKFIX;

	linkfix = &priv->gwca.linkfix_table[gq->index];
	linkfix->die_dt = DT_LINKFIX;
	rsw3_desc_set_dptr(linkfix, gq->ring_dma);

	iowrite32(GWDCC_BALR | (gq->dir_tx ? GWDCC_DCP(GWCA_IPV_NUM) | GWDCC_DQT : 0) | GWDCC_EDE,
		  priv->addr + GWDCC_OFFS(gq->index));

	/* Enable Under Switch Minimum Frame Size Padding */
	iowrite32(GWCKSC_USMFSPE, priv->addr + GWCKSC);

	return 0;

err:
	if (!gq->dir_tx) {
		for (desc = gq->tx_ring; i-- > 0; desc++) {
			dma_addr = rsw3_desc_get_dptr(&desc->desc);
			dma_unmap_single(ndev->dev.parent, dma_addr,
					 RSWITCH3_MAP_BUF_SIZE, DMA_FROM_DEVICE);
		}
	}

	return -ENOMEM;
}

static int rsw3_gwca_queue_ext_ts_fill(struct net_device *ndev,
				       struct rsw3_gwca_queue *gq,
				       unsigned int start_index,
				       unsigned int num)
{
	struct rsw3_device *rdev = netdev_priv(ndev);
	struct rsw3_ext_ts_desc *desc;
	unsigned int i, index;
	dma_addr_t dma_addr;

	for (i = 0; i < num; i++) {
		index = (i + start_index) % gq->ring_size;
		desc = &gq->rx_ring[index];
		if (!gq->dir_tx) {
			dma_addr = dma_map_single(ndev->dev.parent,
						  gq->rx_bufs[index] + RSWITCH3_HEADROOM,
						  RSWITCH3_MAP_BUF_SIZE,
						  DMA_FROM_DEVICE);
			if (dma_mapping_error(ndev->dev.parent, dma_addr))
				goto err;

			desc->desc.info_ds = cpu_to_le16(RSWITCH3_DESC_BUF_SIZE);
			rsw3_desc_set_dptr(&desc->desc, dma_addr);
			dma_wmb();
			desc->desc.die_dt = DT_FEMPTY | DIE;
			desc->info1 = cpu_to_le64(INFO1_SPN(rdev->etha->index));
		} else {
			desc->desc.die_dt = DT_EEMPTY | DIE;
		}
	}

	desc = &gq->rx_ring[gq->ring_size];
	desc->desc.die_dt = DT_LINKFIX;
	desc->desc.dptrl = cpu_to_le32(lower_32_bits(gq->ring_dma));
	desc->desc.dptrh = cpu_to_le32(upper_32_bits(gq->ring_dma));

	return 0;

err:
	if (!gq->dir_tx) {
		for (; i-- > 0; ) {
			index = (i + start_index) % gq->ring_size;
			desc = &gq->rx_ring[index];
			dma_addr = rsw3_desc_get_dptr(&desc->desc);
			dma_unmap_single(ndev->dev.parent, dma_addr,
					 RSWITCH3_MAP_BUF_SIZE, DMA_FROM_DEVICE);
		}
	}

	return -ENOMEM;
}

static int rsw3_gwca_queue_ext_ts_format(struct net_device *ndev,
					 struct rsw3_private *priv,
					 struct rsw3_gwca_queue *gq)
{
	unsigned int ring_size = sizeof(struct rsw3_ext_ts_desc) * gq->ring_size;
	struct rsw3_ext_ts_desc *desc;
	struct rsw3_desc *linkfix;
	int err;

	memset(gq->rx_ring, 0, ring_size);
	err = rsw3_gwca_queue_ext_ts_fill(ndev, gq, 0, gq->ring_size);
	if (err < 0)
		return err;

	desc = &gq->rx_ring[gq->ring_size];	/* Last */
	rsw3_desc_set_dptr(&desc->desc, gq->ring_dma);
	desc->desc.die_dt = DT_LINKFIX;

	linkfix = &priv->gwca.linkfix_table[gq->index];
	linkfix->die_dt = DT_LINKFIX;
	rsw3_desc_set_dptr(linkfix, gq->ring_dma);

	iowrite32(GWDCC_BALR | (gq->dir_tx ? GWDCC_DCP(GWCA_IPV_NUM) | GWDCC_DQT : 0) |
		  GWDCC_ETS | GWDCC_EDE,
		  priv->addr + GWDCC_OFFS(gq->index));

	/* Enable Under Switch Minimum Frame Size Padding */
	iowrite32(GWCKSC_USMFSPE, priv->addr + GWCKSC);

	return 0;
}

static int rsw3_gwca_linkfix_alloc(struct rsw3_private *priv)
{
	unsigned int i, num_queues = priv->gwca.num_queues;
	struct rsw3_gwca *gwca = &priv->gwca;
	struct device *dev = &priv->pdev->dev;

	gwca->linkfix_table_size = sizeof(struct rsw3_desc) * num_queues;
	gwca->linkfix_table = dma_alloc_coherent(dev, gwca->linkfix_table_size,
						 &gwca->linkfix_table_dma, GFP_KERNEL);
	if (!gwca->linkfix_table)
		return -ENOMEM;
	for (i = 0; i < num_queues; i++)
		gwca->linkfix_table[i].die_dt = DT_EOS;

	return 0;
}

static void rsw3_gwca_linkfix_free(struct rsw3_private *priv)
{
	struct rsw3_gwca *gwca = &priv->gwca;

	if (gwca->linkfix_table)
		dma_free_coherent(&priv->pdev->dev, gwca->linkfix_table_size,
				  gwca->linkfix_table, gwca->linkfix_table_dma);
	gwca->linkfix_table = NULL;
}

static struct rsw3_gwca_queue *rsw3_gwca_get(struct rsw3_private *priv)
{
	struct rsw3_gwca_queue *gq;
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

static void rsw3_gwca_put(struct rsw3_private *priv,
			  struct rsw3_gwca_queue *gq)
{
	clear_bit(gq->index, priv->gwca.used);
}

static int rsw3_txdmac_alloc(struct net_device *ndev)
{
	struct rsw3_device *rdev = netdev_priv(ndev);
	struct rsw3_private *priv = rdev->priv;
	int i, err;

	for (i = 0; i < NUM_QUEUES_TX_PER_NDEV; i++) {
		rdev->tx_queue[i] = rsw3_gwca_get(priv);
		if (!rdev->tx_queue[i]) {
			for (; i-- > 0; )
				rsw3_gwca_put(priv, rdev->tx_queue[i]);

			return -EBUSY;
		}

		err = rsw3_gwca_queue_alloc(ndev, priv, rdev->tx_queue[i], true, TX_RING_SIZE);
		if (err < 0) {
			for (; i >= 0; i--)
				rsw3_gwca_put(priv, rdev->tx_queue[i]);

			return err;
		}

		netif_napi_add_tx(ndev, &rdev->tx_queue[i]->napi, rsw3_tx_poll);
	}

	return 0;
}

static void rsw3_txdmac_free(struct net_device *ndev)
{
	struct rsw3_device *rdev = netdev_priv(ndev);
	int i;

	for (i = 0; i < NUM_QUEUES_TX_PER_NDEV; i++) {
		rsw3_gwca_queue_free(ndev, rdev->tx_queue[i]);
		rsw3_gwca_put(rdev->priv, rdev->tx_queue[i]);
		netif_napi_del(&rdev->tx_queue[i]->napi);
	}
}

static int rsw3_txdmac_init(struct rsw3_private *priv, unsigned int index)
{
	struct rsw3_device *rdev = priv->rdev[index];
	int i, err;

	for (i = 0; i < NUM_QUEUES_TX_PER_NDEV; i++) {
		err = rsw3_gwca_queue_format(rdev->ndev, priv, rdev->tx_queue[i]);
		if (err)
			return err;
	}

	return 0;
}

static int rsw3_rxdmac_alloc(struct net_device *ndev)
{
	struct rsw3_device *rdev = netdev_priv(ndev);
	struct rsw3_private *priv = rdev->priv;
	int i, err;

	for (i = 0; i < NUM_QUEUES_RX_PER_NDEV; i++) {
		rdev->rx_queue[i] = rsw3_gwca_get(priv);
		if (!rdev->rx_queue[i]) {
			for (; i-- > 0; )
				rsw3_gwca_put(priv, rdev->rx_queue[i]);

			return -EBUSY;
		}

		err = rsw3_gwca_queue_alloc(ndev, priv, rdev->rx_queue[i], false, RX_RING_SIZE);
		if (err < 0) {
			for (; i >= 0; i--)
				rsw3_gwca_put(priv, rdev->rx_queue[i]);

			return err;
		}

		netif_napi_add(ndev, &rdev->rx_queue[i]->napi, rsw3_rx_poll);
		rdev->rx_queue[i]->napi_idx = i;
	}

	return 0;
}

static void rsw3_rxdmac_free(struct net_device *ndev)
{
	struct rsw3_device *rdev = netdev_priv(ndev);
	int i;

	for (i = 0; i < NUM_QUEUES_RX_PER_NDEV; i++) {
		rsw3_gwca_queue_free(ndev, rdev->rx_queue[i]);
		rsw3_gwca_put(rdev->priv, rdev->rx_queue[i]);
		netif_napi_del(&rdev->rx_queue[i]->napi);
	}
}

static int rsw3_rxdmac_init(struct rsw3_private *priv, unsigned int index)
{
	struct rsw3_device *rdev = priv->rdev[index];
	struct net_device *ndev = rdev->ndev;
	int i, err;

	for (i = 0; i < NUM_QUEUES_RX_PER_NDEV; i++) {
		err = rsw3_gwca_queue_ext_ts_format(ndev, priv, rdev->rx_queue[i]);
		if (err)
			return err;
	}

	return 0;
}

static int rsw3_gwca_hw_init(struct rsw3_private *priv)
{
	unsigned int i;
	int err;

	err = rsw3_gwca_change_mode(priv, GWMC_OPC_DISABLE);
	if (err < 0)
		return err;
	err = rsw3_gwca_change_mode(priv, GWMC_OPC_CONFIG);
	if (err < 0)
		return err;

	err = rsw3_gwca_mcast_table_reset(priv);
	if (err < 0)
		return err;
	err = rsw3_gwca_axi_ram_reset(priv);
	if (err < 0)
		return err;

	iowrite32(GWVCC_VEM_SC_TAG, priv->addr + GWVCC);
	iowrite32(0, priv->addr + GWTTFC);
	iowrite32(lower_32_bits(priv->gwca.linkfix_table_dma), priv->addr + GWDCBAC1);
	iowrite32(upper_32_bits(priv->gwca.linkfix_table_dma), priv->addr + GWDCBAC0);
	iowrite32(GWMDNC_TSDMN(1) | GWMDNC_TXDMN(0x1e) | GWMDNC_RXDMN(0x1f),
		  priv->addr + GWMDNC);

	iowrite32(GWTPC_PPPL(GWCA_IPV_NUM), priv->addr + GWTPC0);

	for (i = 0; i < rswitch3_num_ports; i++) {
		if (priv->rdev[i]->disabled)
			continue;
		err = rsw3_rxdmac_init(priv, i);
		if (err < 0)
			return err;
		err = rsw3_txdmac_init(priv, i);
		if (err < 0)
			return err;
	}

	err = rsw3_gwca_change_mode(priv, GWMC_OPC_DISABLE);
	if (err < 0)
		return err;
	return rsw3_gwca_change_mode(priv, GWMC_OPC_OPERATION);
}

static int rsw3_gwca_hw_deinit(struct rsw3_private *priv)
{
	int err;

	err = rsw3_gwca_change_mode(priv, GWMC_OPC_DISABLE);
	if (err < 0)
		return err;
	err = rsw3_gwca_change_mode(priv, GWMC_OPC_RESET);
	if (err < 0)
		return err;

	return rsw3_gwca_change_mode(priv, GWMC_OPC_DISABLE);
}

static int rsw3_gwca_halt(struct rsw3_private *priv)
{
	int err;

	priv->gwca_halt = true;
	err = rsw3_gwca_hw_deinit(priv);
	dev_err(&priv->pdev->dev, "halted (%d)\n", err);

	return err;
}

static struct sk_buff *rsw3_rx_handle_desc(struct net_device *ndev,
					   struct rsw3_gwca_queue *gq,
					   struct rsw3_ext_ts_desc *desc)
{
	dma_addr_t dma_addr = rsw3_desc_get_dptr(&desc->desc);
	u16 pkt_len = le16_to_cpu(desc->desc.info_ds) & RX_DS;
	u8 die_dt = desc->desc.die_dt & DT_MASK;
	struct sk_buff *skb = NULL;

	dma_unmap_single(ndev->dev.parent, dma_addr, RSWITCH3_MAP_BUF_SIZE,
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
		skb = build_skb(gq->rx_bufs[gq->cur], RSWITCH3_BUF_SIZE);
		if (skb) {
			skb_reserve(skb, RSWITCH3_HEADROOM);
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
				offset_in_page(gq->rx_bufs[gq->cur]) + RSWITCH3_HEADROOM,
				pkt_len, RSWITCH3_BUF_SIZE);
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

static bool rsw3_rx(struct net_device *ndev, struct rsw3_gwca_queue *gq, int *quota)
{
	struct rsw3_device *rdev = netdev_priv(ndev);
	struct rsw3_ext_ts_desc *desc;
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
		skb = rsw3_rx_handle_desc(ndev, gq, desc);
		if (!skb)
			goto out;

		get_ts = rdev->priv->ptp_priv->tstamp_rx_ctrl & RCAR_GEN4_RXTSTAMP_TYPE_V2_L2_EVENT;
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
		gq->cur = rsw3_next_queue_index(gq, true, 1);
		desc = &gq->rx_ring[gq->cur];

		if (--boguscnt <= 0)
			break;
	}

	num = rsw3_get_num_cur_queues(gq);
	ret = rsw3_gwca_queue_alloc_rx_buf(gq, gq->dirty, num);
	if (ret < 0)
		goto err;
	ret = rsw3_gwca_queue_ext_ts_fill(ndev, gq, gq->dirty, num);
	if (ret < 0)
		goto err;
	gq->dirty = rsw3_next_queue_index(gq, false, num);

	*quota -= limit - boguscnt;

	return boguscnt <= 0;

err:
	rsw3_gwca_halt(rdev->priv);

	return 0;
}

static int rsw3_tx_free(struct rsw3_gwca_queue *gq, int budget)
{
	struct napi_struct *napi = &gq->napi;
	struct net_device *ndev = napi->dev;
	struct rsw3_ext_desc *desc;
	struct rsw3_device *rdev;
	struct sk_buff *skb;
	int done = 0;

	rdev = netdev_priv(ndev);

	desc = &gq->tx_ring[gq->dirty];
	while (done < budget && (desc->desc.die_dt & DT_MASK) == DT_FEMPTY) {
		dma_rmb();
		skb = gq->skbs[gq->dirty];
		if (skb) {
			if (skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) {
				struct skb_shared_hwtstamps shhwtstamps;
				struct timespec64 ts;

				rsw3_get_timestamp(rdev->priv, &ts);
				memset(&shhwtstamps, 0, sizeof(shhwtstamps));
				shhwtstamps.hwtstamp = timespec64_to_ktime(ts);
				skb_tstamp_tx(skb, &shhwtstamps);
			}

			rdev->ndev->stats.tx_packets++;
			rdev->ndev->stats.tx_bytes += skb->len;
			dma_unmap_single(ndev->dev.parent,
					 gq->unmap_addrs[gq->dirty],
					 skb->len, DMA_TO_DEVICE);
			dev_kfree_skb_any(gq->skbs[gq->dirty]);
			gq->skbs[gq->dirty] = NULL;
		}
		desc->desc.die_dt = DT_EEMPTY;
		gq->dirty = rsw3_next_queue_index(gq, false, 1);
		desc = &gq->tx_ring[gq->dirty];
		done++;
	}

	return done;
}

static int rsw3_tx_poll(struct napi_struct *napi, int budget)
{
	struct rsw3_gwca_queue *gq = napi_to_gwca_queue(napi);
	struct net_device *ndev = napi->dev;
	struct rsw3_private *priv;
	struct rsw3_device *rdev;
	int qidx = gq->napi_idx;
	int done;

	rdev = netdev_priv(ndev);
	priv = rdev->priv;

	done = rsw3_tx_free(gq, budget);

	netif_wake_subqueue(ndev, qidx);

	if (done < budget && napi_complete_done(napi, done)) {
		unsigned long flags;

		spin_lock_irqsave(&priv->lock, flags);
		if (test_bit(rdev->port, priv->opened_ports))
			rsw3_enadis_data_irq(priv, rdev->tx_queue[qidx]->index, true);
		spin_unlock_irqrestore(&priv->lock, flags);
	}

	return done;
}

static int rsw3_rx_poll(struct napi_struct *napi, int budget)
{
	struct rsw3_gwca_queue *gq = napi_to_gwca_queue(napi);
	struct net_device *ndev = napi->dev;
	struct rsw3_private *priv;
	struct rsw3_device *rdev;
	unsigned long flags;
	int quota = budget;
	int qidx;

	rdev = netdev_priv(ndev);
	priv = rdev->priv;

	qidx = gq->napi_idx;

retry:
	if (rsw3_rx(ndev, gq, &quota))
		goto out;
	else if (rdev->priv->gwca_halt)
		goto err;
	else if (rsw3_is_queue_rxed(rdev->rx_queue[qidx]))
		goto retry;

	if (napi_complete_done(napi, budget - quota)) {
		spin_lock_irqsave(&priv->lock, flags);
		if (test_bit(rdev->port, priv->opened_ports))
			rsw3_enadis_data_irq(priv, rdev->rx_queue[qidx]->index, true);
		spin_unlock_irqrestore(&priv->lock, flags);
	}

out:
	return budget - quota;

err:
	napi_complete(napi);

	return 0;
}

static void rsw3_queue_interrupt(struct napi_struct *napi, bool dir_tx)
{
	struct rsw3_gwca_queue *gq = napi_to_gwca_queue(napi);
	struct net_device *ndev = napi->dev;
	struct rsw3_device *rdev = netdev_priv(ndev);
	int qidx = gq->napi_idx;

	if (napi_schedule_prep(napi)) {
		spin_lock(&rdev->priv->lock);
		if (dir_tx)
			rsw3_enadis_data_irq(rdev->priv, rdev->tx_queue[qidx]->index, false);
		else
			rsw3_enadis_data_irq(rdev->priv, rdev->rx_queue[qidx]->index, false);
		spin_unlock(&rdev->priv->lock);
		__napi_schedule(napi);
	}
}

static irqreturn_t rsw3_data_irq(struct rsw3_private *priv, u32 *dis, u32 *mask)
{
	struct rsw3_gwca_queue *gq;
	unsigned int i, index, bit;

	for (i = 0; i < priv->gwca.num_queues; i++) {
		gq = &priv->gwca.queues[i];
		index = gq->index / 32;
		bit = BIT(gq->index % 32);
		if (!(dis[index] & bit) || !(mask[index] & bit))
			continue;

		rsw3_ack_data_irq(priv, gq->index);

		if (gq->dir_tx)
			rsw3_queue_interrupt(&gq->napi, true);
		else
			rsw3_queue_interrupt(&gq->napi, false);
	}

	return IRQ_HANDLED;
}

static irqreturn_t rsw3_gwca_irq(int irq, void *dev_id)
{
	struct rsw3_private *priv = dev_id;
	irqreturn_t ret = IRQ_NONE;
	u32 dis[RSWITCH3_NUM_IRQ_REGS];
	unsigned int i;
	u32 *mask;

	rsw3_get_data_irq_status(priv, dis);

	for (i = 0; i < GWCA_NUM_IRQS; i++) {
		if (irq == priv->irq[i].irq_id) {
			mask = priv->irq[i].irq_bits;
			break;
		}
	}

	if (!mask)
		return ret;

	if (rsw3_is_any_data_irq(priv, dis, mask))
		ret = rsw3_data_irq(priv, dis, mask);

	return ret;
}

static int rsw3_gwca_request_irqs(struct rsw3_private *priv)
{
	char *resource_name, *irq_name;
	int i, ret, irq;

	for (i = 0; i < GWCA_NUM_IRQS; i++) {
		resource_name = kasprintf(GFP_KERNEL, GWCA_IRQ_RESOURCE_NAME, i);
		if (!resource_name)
			return -ENOMEM;

		irq = platform_get_irq_byname(priv->pdev, resource_name);
		kfree(resource_name);
		if (irq < 0)
			return irq;

		irq_name = devm_kasprintf(&priv->pdev->dev, GFP_KERNEL,
					  GWCA_IRQ_NAME, i);
		if (!irq_name)
			return -ENOMEM;

		ret = devm_request_irq(&priv->pdev->dev, irq, rsw3_gwca_irq,
				       0, irq_name, priv);
		if (ret < 0)
			return ret;

		priv->irq[i].irq_id = irq;
	}

	return 0;
}

/* Ethernet TSN Agent block (ETHA) and Ethernet MAC IP block (RMAC) */
static int rsw3_etha_change_mode(struct rsw3_etha *etha,
				 enum rsw3_etha_mode mode)
{
	int ret;

	if (!rsw3_agent_clock_is_enabled(etha->coma_addr, etha->index))
		rsw3_agent_clock_ctrl(etha->coma_addr, etha->index, 1);

	iowrite32(mode, etha->addr + EAMC);

	ret = rsw3_reg_wait(etha->addr, EAMS, EAMS_OPS_MASK, mode);

	if (mode == EAMC_OPC_DISABLE)
		rsw3_agent_clock_ctrl(etha->coma_addr, etha->index, 0);

	return ret;
}

static void rsw3_etha_read_mac_address(struct rsw3_etha *etha)
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
	if (is_valid_ether_addr(mac) && etha->index < RSWITCH3_NUM_PORTS) {
		memcpy(cached_mac_addresses[etha->index], mac, ETH_ALEN);
		mac_cache_valid[etha->index] = true;
	}
	/* Restore from cache if current read is invalid but cache has valid MAC */
	else if (!is_valid_ether_addr(mac) && etha->index < RSWITCH3_NUM_PORTS &&
		 mac_cache_valid[etha->index]) {
		memcpy(mac, cached_mac_addresses[etha->index], ETH_ALEN);
	}
}

static void rsw3_etha_write_mac_address(struct rsw3_etha *etha, const u8 *mac)
{
	iowrite32((mac[0] << 8) | mac[1], etha->addr + MRMAC0);
	iowrite32((mac[2] << 24) | (mac[3] << 16) | (mac[4] << 8) | mac[5],
		  etha->addr + MRMAC1);
}

static int __maybe_unused rsw3_etha_wait_link_verification(struct rsw3_etha *etha)
{
	iowrite32(MLVC_PLV, etha->addr + MLVC);

	return rsw3_reg_wait(etha->addr, MLVC, MLVC_PLV, 0);
}

static void rsw3_rmac_setting(struct rsw3_etha *etha, const u8 *mac)
{
	u32 pis, lsc;

	rsw3_etha_write_mac_address(etha, mac);

	switch (etha->phy_interface) {
	case PHY_INTERFACE_MODE_SGMII:
		pis = MPIC_PIS_GMII;
		break;
	case PHY_INTERFACE_MODE_USXGMII:
	case PHY_INTERFACE_MODE_5GBASER:
	case PHY_INTERFACE_MODE_10GBASER:
		pis = MPIC_PIS_XGMII;
		break;
	default:
		pis = FIELD_GET(MPIC_PIS, ioread32(etha->addr + MPIC));
		break;
	}

	switch (etha->speed) {
	case 100:
		lsc = MPIC_LSC_100M;
		break;
	case 1000:
		lsc = MPIC_LSC_1G;
		break;
	case 2500:
		lsc = MPIC_LSC_2_5G;
		break;
	case 5000:
		lsc = MPIC_LSC_5G;
		break;
	case 10000:
		lsc = MPIC_LSC_10G;
		break;
	default:
		lsc = FIELD_GET(MPIC_LSC, ioread32(etha->addr + MPIC));
		break;
	}

	rsw3_modify(etha->addr, MPIC, MPIC_PIS | MPIC_LSC,
		    FIELD_PREP(MPIC_PIS, pis) | FIELD_PREP(MPIC_LSC, lsc));
	/* Set MIOC Bit(3)*/
	if (etha->connect_to_xpcs) {
		if (etha->index >= 5 && etha->index <= 7)
			iowrite32(MIOC_BIT3_SET, etha->addr + MIOC);
		else
			pr_err("%s: Invalid port %d for XPCS connection\n", __func__, etha->index);
	}
}

static void rsw3_etha_enable_mii(struct rsw3_etha *etha)
{
	u32 psmcs10 = etha->psmcs & 0x3ff;

	rsw3_modify(etha->addr, MPIC, MPIC_PSMCS_LO | MPIC_PSMCS_HI | MPIC_PSMHT,
		    MPIC_PSMCS_ENC(psmcs10) | FIELD_PREP(MPIC_PSMHT, 0x06));
}

static int rsw3_etha_mii_hw_init(struct rsw3_etha *etha)
{
	int err;

	if (etha->operated)
		return 0;

	err = rsw3_etha_change_mode(etha, EAMC_OPC_DISABLE);
	if (err < 0)
		return err;

	err = rsw3_etha_change_mode(etha, EAMC_OPC_CONFIG);
	if (err < 0)
		return err;

	rsw3_etha_enable_mii(etha);

	err = rsw3_etha_change_mode(etha, EAMC_OPC_DISABLE);
	if (err < 0)
		return err;

	etha->operated = true;

	return 0;
}

static int rsw3_etha_mii_hw_start(struct rsw3_etha *etha)
{
	u32 val;
	int err;

	val = ioread32(etha->addr + EAMS);

	if ((val & EAMS_OPS_MASK) != EAMC_OPC_OPERATION) {
		err = rsw3_etha_change_mode(etha, EAMC_OPC_DISABLE);
		if (err < 0)
			return err;

		err = rsw3_etha_change_mode(etha, EAMC_OPC_OPERATION);
		if (err < 0)
			return err;
	}

	return 0;
}

static void rsw3_etha_init_tsn_egress_path(struct rsw3_etha *etha)
{
	unsigned int q;

	iowrite32(0, etha->addr + EATDRC);
	iowrite32(0, etha->addr + EAIRC);
	iowrite32(0, etha->addr + EATDQSC);
	iowrite32(0, etha->addr + EATDQAC);
	iowrite32(0, etha->addr + EATPEC);

	for (q = 0; q < RSWITCH3_NUM_PRIOS; q++) {
		iowrite32(RSWITCH3_TSNA_QUEUE_DEPTH, etha->addr + EATDQDC(q));
		iowrite32(EATMFSC_MAX, etha->addr + EATMFSC(q));
	}

	iowrite32(0, etha->addr + EACTDQDC);
	iowrite32(0, etha->addr + EATTFC);
}

static int rsw3_etha_hw_init_tsn_internal(struct rsw3_private *priv,
						u32 rsw_port, const u8 *tsnes_mac)
{
	struct rsw3_etha *etha = &priv->etha[rsw_port];
	struct rsw3_device *rdev;
	struct net_device *ndev;
	const u8 *mac = tsnes_mac;
	int err;

	if (rsw_port >= RSWITCH3_NUM_PORTS || !priv->rdev[rsw_port])
		return -ENODEV;

	rdev = priv->rdev[rsw_port];
	ndev = rdev->ndev;

	if ((!mac || !is_valid_ether_addr(mac)) && ndev)
		mac = ndev->dev_addr;
	if (!mac || !is_valid_ether_addr(mac))
		return -EINVAL;

	err = rsw3_etha_change_mode(etha, EAMC_OPC_DISABLE);
	if (err < 0)
		return err;

	err = rsw3_etha_change_mode(etha, EAMC_OPC_CONFIG);
	if (err < 0)
		return err;

	rsw3_etha_init_tsn_egress_path(etha);
	iowrite32(EAVCC_VEM_NO_TAG, etha->addr + EAVCC);
	rsw3_etha_write_mac_address(etha, mac);
	iowrite32(RSW3_MRAFC_RX_PROMISC, etha->addr + MRAFC);
	rsw3_modify(etha->addr, MIOC, MIOC_FORCE_PHY_LINK,
			MIOC_FORCE_PHY_LINK);
	rsw3_modify(etha->addr, MPIC, MPIC_PIS | MPIC_LSC | MPIC_PLSPP,
			FIELD_PREP(MPIC_PIS, MPIC_PIS_GMII) |
			FIELD_PREP(MPIC_LSC, MPIC_LSC_2_5G) |
			MPIC_PLSPP);

	/*
	 * Internal TSN-ES interfaces 8..12 have no external PHY/XPCS.
	 * The TSN-ES side owns link state
	 */
	err = rsw3_etha_change_mode(etha, EAMC_OPC_DISABLE);
	if (err < 0)
		return err;

	err = rsw3_etha_change_mode(etha, EAMC_OPC_OPERATION);
	if (err < 0)
		return err;

	iowrite32(EATDQC_DISABLE_CUT_THROUGH, etha->addr + EATDQC);
	etha->operated = true;

	return 0;
}

static int rsw3_etha_hw_init(struct rsw3_etha *etha, const u8 *mac)
{
	int err;

	err = rsw3_etha_change_mode(etha, EAMC_OPC_DISABLE);
	if (err < 0)
		return err;

	err = rsw3_etha_change_mode(etha, EAMC_OPC_CONFIG);
	if (err < 0)
		return err;

	rsw3_rmac_setting(etha, mac);
	rsw3_etha_init_tsn_egress_path(etha);
	iowrite32(EAVCC_VEM_SC_TAG, etha->addr + EAVCC);

	err = rsw3_etha_wait_link_verification(etha);
	if (err < 0)
		return err;

	err = rsw3_etha_change_mode(etha, EAMC_OPC_DISABLE);
	if (err < 0)
		return err;

	err = rsw3_etha_change_mode(etha, EAMC_OPC_OPERATION);
	if (err < 0)
		return err;

	iowrite32(EATDQC_DISABLE_CUT_THROUGH, etha->addr + EATDQC);
	etha->operated = true;

	return 0;
}

static int rsw3_etha_mpsm_op(struct rsw3_etha *etha, bool read,
			     unsigned int mmf, unsigned int pda,
			     unsigned int pra, unsigned int pop,
			     unsigned int prd)
{
	u32 val;
	int ret;

	val = MPSM_PSME |
	      FIELD_PREP(MPSM_MFF, mmf) |
	      FIELD_PREP(MPSM_PDA, pda) |
	      FIELD_PREP(MPSM_PRA, pra) |
	      FIELD_PREP(MPSM_POP, pop) |
	      FIELD_PREP(MPSM_PRD, prd);
	iowrite32(val, etha->addr + MPSM);

	ret = rsw3_reg_wait(etha->addr, MPSM, MPSM_PSME, 0);
	if (ret)
		return ret;

	if (read) {
		val = ioread32(etha->addr + MPSM);
		ret = FIELD_GET(MPSM_PRD, val);
	}

	return ret;
}

static int rsw3_etha_mii_read_c45(struct mii_bus *bus, int addr, int devad,
				  int regad)
{
	struct rsw3_etha *etha = bus->priv;
	int ret;

	ret = rsw3_etha_mpsm_op(etha, false, MPSM_MMF_C45, addr, devad,
				MPSM_POP_ADDRESS, regad);
	if (ret)
		return ret;

	return rsw3_etha_mpsm_op(etha, true, MPSM_MMF_C45, addr, devad,
				 MPSM_POP_READ_C45, 0);
}

static int rsw3_etha_mii_write_c45(struct mii_bus *bus, int addr, int devad,
				   int regad, u16 val)
{
	struct rsw3_etha *etha = bus->priv;
	int ret;

	ret = rsw3_etha_mpsm_op(etha, false, MPSM_MMF_C45, addr, devad,
				MPSM_POP_ADDRESS, regad);
	if (ret)
		return ret;

	return rsw3_etha_mpsm_op(etha, false, MPSM_MMF_C45, addr, devad,
				 MPSM_POP_WRITE, val);
}

static int rsw3_etha_mii_read_c22(struct mii_bus *bus, int phyad, int regad)
{
	struct rsw3_etha *etha = bus->priv;

	return rsw3_etha_mpsm_op(etha, true, MPSM_MMF_C22, phyad, regad,
				 MPSM_POP_READ_C22, 0);
}

static int rsw3_etha_mii_write_c22(struct mii_bus *bus, int phyad,
				   int regad, u16 val)
{
	struct rsw3_etha *etha = bus->priv;

	return rsw3_etha_mpsm_op(etha, false, MPSM_MMF_C22, phyad, regad,
				 MPSM_POP_WRITE, val);
}

/* Call of_node_put(port) after done */
static struct device_node *rsw3_get_port_node(struct rsw3_device *rdev)
{
	struct device_node *ports, *port;
	int err = 0;
	u32 index, etha_mii_idx;

	ports = of_get_child_by_name(rdev->ndev->dev.parent->of_node,
				     "ethernet-ports");
	if (!ports)
		return NULL;

	for_each_child_of_node(ports, port) {
		err = of_property_read_u32(port, "reg", &index);
		if (err < 0) {
			port = NULL;
			goto out;
		}
		if (index == rdev->etha->index) {
			if (!of_device_is_available(port))
				port = NULL;

			err = of_property_read_u32(port, "renesas,etha-mii-chan", &etha_mii_idx);
			if (err < 0)
				etha_mii_idx = index;

			rdev->etha_mii_idx = etha_mii_idx;

			break;
		}
	}

out:
	of_node_put(ports);

	return port;
}

static int rsw3_etha_get_params(struct rsw3_device *rdev)
{
	u32 max_speed;
	int err;

	if (!rdev->np_port)
		return 0;	/* ignored */

	err = of_get_phy_mode(rdev->np_port, &rdev->etha->phy_interface);
	if (err)
		return err;

	rdev->etha->connect_to_xpcs =
		of_property_read_bool(rdev->np_port, "renesas,connect_to_xpcs");

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

static int rsw3_mii_register(struct rsw3_device *rdev)
{
	struct device_node *mdio_np;
	struct mii_bus *mii_bus;
	int err;

	if (rdev->etha->mii)
		return 0;

	mii_bus = mdiobus_alloc();
	if (!mii_bus)
		return -ENOMEM;

	mii_bus->name = "rsw3_mii";
	sprintf(mii_bus->id, "etha%d", rdev->etha->index);
	mii_bus->priv = rdev->etha_mii;
	mii_bus->read_c45 = rsw3_etha_mii_read_c45;
	mii_bus->write_c45 = rsw3_etha_mii_write_c45;
	mii_bus->read = rsw3_etha_mii_read_c22;
	mii_bus->write = rsw3_etha_mii_write_c22;
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

static void rsw3_mii_unregister(struct rsw3_device *rdev)
{
	if (rdev->etha->mii) {
		mdiobus_unregister(rdev->etha->mii);
		mdiobus_free(rdev->etha->mii);
		rdev->etha->mii = NULL;
	}
}

static void rsw3_adjust_link(struct net_device *ndev)
{
	struct rsw3_device *rdev = netdev_priv(ndev);
	struct phy_device *phydev = ndev->phydev;

	if (phydev->link != rdev->etha->link) {
		phy_print_status(phydev);
		if (phydev->link)
			phy_power_on(rdev->pcs);
		else if (rdev->pcs->power_count)
			phy_power_off(rdev->pcs);

		rdev->etha->link = phydev->link;

		if (phydev->link && !parallel_mode && phydev->speed != rdev->etha->speed) {
			rdev->etha->speed = phydev->speed;

			rsw3_etha_hw_init(rdev->etha, rdev->ndev->dev_addr);
			phy_set_speed(rdev->pcs, rdev->etha->speed);
		}
	}
}

static void rsw3_phy_remove_link_mode(struct rsw3_device *rdev,
					struct phy_device *phydev)
{
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

static int rsw3_phy_device_init(struct rsw3_device *rdev)
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
	} else {
		phy = of_parse_phandle(rdev->np_port, "phy-handle", 0);
		if (!phy)
			return -ENODEV;
	}

	/* Set phydev->host_interfaces before calling of_phy_connect() to
	 * configure the PHY with the information of host_interfaces.
	 */
	phydev = of_phy_find_device(phy);
	if (!phydev)
		goto out;
	__set_bit(rdev->etha->phy_interface, phydev->host_interfaces);
	phydev->mac_managed_pm = true;

	phydev = of_phy_connect(rdev->ndev, phy, rsw3_adjust_link, 0,
				rdev->etha->phy_interface);
	if (!phydev)
		goto out;

	phy_set_max_speed(phydev, SPEED_10000);
	phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_10baseT_Half_BIT);
	phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_10baseT_Full_BIT);
	phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_100baseT_Half_BIT);
	phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_1000baseT_Half_BIT);
	rsw3_phy_remove_link_mode(rdev, phydev);

	phy_attached_info(phydev);

	err = 0;
out:
	if (phy != rdev->np_port)
		of_node_put(phy);

	return err;
}

static void rsw3_phy_device_deinit(struct rsw3_device *rdev)
{
	if (rdev->ndev->phydev)
		phy_disconnect(rdev->ndev->phydev);
}

static int rsw3_pcs_set_params(struct rsw3_device *rdev)
{
	int err;

	err = phy_set_mode_ext(rdev->pcs, PHY_MODE_ETHERNET,
			       rdev->etha->phy_interface);
	if (err < 0)
		return err;

	return phy_set_speed(rdev->pcs, rdev->etha->speed);
}

static int rsw3_ether_port_init_one(struct rsw3_device *rdev)
{
	int err;

	if (!parallel_mode) {
		rdev->pcs = devm_of_phy_get(&rdev->priv->pdev->dev, rdev->np_port, NULL);
		if (IS_ERR(rdev->pcs)) {
			err = -EPROBE_DEFER;
			goto err_pcs_phy_get;
		}

		err = rsw3_etha_mii_hw_init(rdev->etha_mii);
		if (err < 0)
			return err;

		err = rsw3_etha_hw_init(rdev->etha, rdev->ndev->dev_addr);
		if (err < 0)
			return err;

		err = rsw3_mii_register(rdev);
		if (err < 0)
			return err;

		err = rsw3_pcs_set_params(rdev);
		if (err < 0)
			goto err_pcs_set_params;
	}

	return 0;

err_pcs_set_params:
err_pcs_phy_get:
	rsw3_phy_device_deinit(rdev);

	return err;
}

static void rsw3_ether_port_deinit_one(struct rsw3_device *rdev)
{
	rsw3_phy_device_deinit(rdev);
	rsw3_mii_unregister(rdev);
}

static int rsw3_ether_port_init_all(struct rsw3_private *priv)
{
	unsigned int i;
	int err;

	rsw3_for_each_enabled_port(priv, i) {
		if (priv->rdev[i]->disabled)
			continue;
		err = rsw3_ether_port_init_one(priv->rdev[i]);
		if (err)
			goto err_init_one;
	}

	/* The HWUM states that if no PHY TX/RX clock is provided to RMAC, the transition from
	 * OPERATION to DISABLED of ETHA may not be possible. Therefore, change the etha_mii
	 * channels to OPERATION mode only after initializing all channels. This helps avoid
	 * timeouts when changing modes for other ETHA settings when the same port is used for
	 * both MDIO and data transfer.
	 */
	rsw3_for_each_enabled_port(priv, i) {
		if (priv->rdev[i]->disabled)
			continue;

		err = rsw3_etha_mii_hw_start(priv->rdev[i]->etha_mii);
		if (err)
			goto err_pcs;

		err = phy_init(priv->rdev[i]->pcs);
		if (err)
			goto err_pcs;
	}

	return 0;

err_pcs:
	rsw3_for_each_enabled_port_continue_reverse(priv, i)
		phy_exit(priv->rdev[i]->pcs);
	i = rswitch3_num_ports;

err_init_one:
	rsw3_for_each_enabled_port_continue_reverse(priv, i)
		rsw3_ether_port_deinit_one(priv->rdev[i]);

	return err;
}

static void rsw3_ether_port_deinit_all(struct rsw3_private *priv)
{
	unsigned int i;

	rsw3_for_each_enabled_port(priv, i) {
		phy_exit(priv->rdev[i]->pcs);

		rsw3_ether_port_deinit_one(priv->rdev[i]);
	}
}

static int rsw3_open(struct net_device *ndev)
{
	struct rsw3_device *rdev = netdev_priv(ndev);
	unsigned long flags;
	int i, err;

	/* Init PHY if not already connected */
	if (!ndev->phydev) {
		err = rsw3_phy_device_init(rdev);
		if (err)
			return err;

		err = phy_init(rdev->pcs);
		if (err) {
			rsw3_phy_device_deinit(rdev);
			return err;
		}

		err = rsw3_pcs_set_params(rdev);
		if (err) {
			phy_exit(rdev->pcs);
			rsw3_phy_device_deinit(rdev);
			return err;
		}
	}

	for (i = 0; i < NUM_QUEUES_TX_PER_NDEV; i++)
		napi_enable(&rdev->tx_queue[i]->napi);

	for (i = 0; i < NUM_QUEUES_RX_PER_NDEV; i++)
		napi_enable(&rdev->rx_queue[i]->napi);

	spin_lock_irqsave(&rdev->priv->lock, flags);
	bitmap_set(rdev->priv->opened_ports, rdev->port, 1);
	for (i = 0; i < NUM_QUEUES_TX_PER_NDEV; i++)
		rsw3_enadis_data_irq(rdev->priv, rdev->tx_queue[i]->index, true);
	for (i = 0; i < NUM_QUEUES_RX_PER_NDEV; i++)
		rsw3_enadis_data_irq(rdev->priv, rdev->rx_queue[i]->index, true);
	spin_unlock_irqrestore(&rdev->priv->lock, flags);

	phy_start(ndev->phydev);
	netif_start_queue(ndev);

	return 0;
}

static int rsw3_stop(struct net_device *ndev)
{
	struct rsw3_device *rdev = netdev_priv(ndev);
	unsigned long flags;
	int i;

	netif_tx_stop_all_queues(ndev);
	phy_stop(ndev->phydev);

	spin_lock_irqsave(&rdev->priv->lock, flags);
	for (i = 0; i < NUM_QUEUES_TX_PER_NDEV; i++)
		rsw3_enadis_data_irq(rdev->priv, rdev->tx_queue[i]->index, false);
	for (i = 0; i < NUM_QUEUES_RX_PER_NDEV; i++)
		rsw3_enadis_data_irq(rdev->priv, rdev->rx_queue[i]->index, false);
	bitmap_clear(rdev->priv->opened_ports, rdev->port, 1);
	spin_unlock_irqrestore(&rdev->priv->lock, flags);

	for (i = 0; i < NUM_QUEUES_TX_PER_NDEV; i++)
		napi_disable(&rdev->tx_queue[i]->napi);
	for (i = 0; i < NUM_QUEUES_RX_PER_NDEV; i++)
		napi_disable(&rdev->rx_queue[i]->napi);

	return 0;
}

static bool rsw3_ext_desc_set_info1(struct rsw3_device *rdev,
				    struct sk_buff *skb,
				    struct rsw3_ext_desc *desc)
{
	if (!parallel_mode)
		desc->info1 = cpu_to_le64(INFO1_DV(BIT(rdev->etha->index)) |
					  INFO1_IPV(GWCA_IPV_NUM) | INFO1_FMT);
	else
		desc->info1 = cpu_to_le64(INFO1_IPV(GWCA_IPV_NUM));


	if (skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) {
		skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
		rdev->ts_tag++;
		desc->info1 |= cpu_to_le64(INFO1_TSUN(rdev->ts_tag) | INFO1_TXC);
	}

	skb_tx_timestamp(skb);

	return true;
}

static bool rsw3_ext_desc_set(struct rsw3_device *rdev,
			      struct sk_buff *skb,
			      struct rsw3_ext_desc *desc,
			      dma_addr_t dma_addr, u16 len, u8 die_dt)
{
	rsw3_desc_set_dptr(&desc->desc, dma_addr);
	desc->desc.info_ds = cpu_to_le16(len);
	if (!rsw3_ext_desc_set_info1(rdev, skb, desc))
		return false;

	dma_wmb();

	desc->desc.die_dt = die_dt;

	return true;
}

static u8 rsw3_ext_desc_get_die_dt(unsigned int nr_desc, unsigned int index)
{
	if (nr_desc == 1)
		return DT_FSINGLE | DIE;
	if (index == 0)
		return DT_FSTART;
	if (nr_desc - 1 == index)
		return DT_FEND | DIE;
	return DT_FMID;
}

static u16 rsw3_ext_desc_get_len(u8 die_dt, unsigned int orig_len)
{
	switch (die_dt & DT_MASK) {
	case DT_FSINGLE:
	case DT_FEND:
		return (orig_len % RSWITCH3_DESC_BUF_SIZE) ?: RSWITCH3_DESC_BUF_SIZE;
	case DT_FSTART:
	case DT_FMID:
		return RSWITCH3_DESC_BUF_SIZE;
	default:
		return 0;
	}
}

static netdev_tx_t rsw3_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct rsw3_device *rdev = netdev_priv(ndev);
	dma_addr_t dma_addr, dma_addr_orig;
	netdev_tx_t ret = NETDEV_TX_OK;
	struct rsw3_ext_desc *desc;
	struct rsw3_gwca_queue *gq;
	unsigned int i, nr_desc;
	u16 qidx, len;
	u8 die_dt;

	qidx = skb_get_queue_mapping(skb);
	gq = rdev->tx_queue[qidx];

	nr_desc = (skb->len - 1) / RSWITCH3_DESC_BUF_SIZE + 1;
	if (rsw3_get_num_cur_queues(gq) >= gq->ring_size - nr_desc) {
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
		desc = &gq->tx_ring[rsw3_next_queue_index(gq, true, i)];
		die_dt = rsw3_ext_desc_get_die_dt(nr_desc, i);
		dma_addr = dma_addr_orig + i * RSWITCH3_DESC_BUF_SIZE;
		len = rsw3_ext_desc_get_len(die_dt, skb->len);
		if (!rsw3_ext_desc_set(rdev, skb, desc, dma_addr, len, die_dt))
			goto err_unmap;
	}

	gq->cur = rsw3_next_queue_index(gq, true, nr_desc);
	rsw3_modify(rdev->addr, GWTRC(gq->index), 0, BIT(gq->index % 32));

	return ret;

err_unmap:
	gq->skbs[(gq->cur + nr_desc - 1) % gq->ring_size] = NULL;
	dma_unmap_single(ndev->dev.parent, dma_addr_orig, skb->len, DMA_TO_DEVICE);

err_kfree:
	dev_kfree_skb_any(skb);

	return ret;
}

static struct net_device_stats *rsw3_get_stats(struct net_device *ndev)
{
	return &ndev->stats;
}

static int rsw3_hwstamp_get(struct net_device *ndev, struct ifreq *req)
{
	struct rsw3_device *rdev = netdev_priv(ndev);
	struct rcar_gen4_ptp_private *ptp_priv;
	struct hwtstamp_config config;

	ptp_priv = rdev->priv->ptp_priv;

	config.flags = 0;
	config.tx_type = ptp_priv->tstamp_tx_ctrl ? HWTSTAMP_TX_ON :
							  HWTSTAMP_TX_OFF;
	switch (ptp_priv->tstamp_rx_ctrl & RCAR_GEN4_RXTSTAMP_TYPE) {
	case RCAR_GEN4_RXTSTAMP_TYPE_V2_L2_EVENT:
		config.rx_filter = HWTSTAMP_FILTER_PTP_V2_L2_EVENT;
		break;
	case RCAR_GEN4_RXTSTAMP_TYPE_ALL:
		config.rx_filter = HWTSTAMP_FILTER_ALL;
		break;
	default:
		config.rx_filter = HWTSTAMP_FILTER_NONE;
		break;
	}

	return copy_to_user(req->ifr_data, &config, sizeof(config)) ? -EFAULT : 0;
}

static int rsw3_hwstamp_set(struct net_device *ndev, struct ifreq *req)
{
	struct rsw3_device *rdev = netdev_priv(ndev);
	u32 tstamp_rx_ctrl = RCAR_GEN4_RXTSTAMP_ENABLED;
	struct hwtstamp_config config;
	u32 tstamp_tx_ctrl;

	if (copy_from_user(&config, req->ifr_data, sizeof(config)))
		return -EFAULT;

	if (config.flags)
		return -EINVAL;

	switch (config.tx_type) {
	case HWTSTAMP_TX_OFF:
		tstamp_tx_ctrl = 0;
		break;
	case HWTSTAMP_TX_ON:
		tstamp_tx_ctrl = RCAR_GEN4_TXTSTAMP_ENABLED;
		break;
	default:
		return -ERANGE;
	}

	switch (config.rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		tstamp_rx_ctrl = 0;
		break;
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
		tstamp_rx_ctrl |= RCAR_GEN4_RXTSTAMP_TYPE_V2_L2_EVENT;
		break;
	default:
		config.rx_filter = HWTSTAMP_FILTER_ALL;
		tstamp_rx_ctrl |= RCAR_GEN4_RXTSTAMP_TYPE_ALL;
		break;
	}

	rdev->priv->ptp_priv->tstamp_tx_ctrl = tstamp_tx_ctrl;
	rdev->priv->ptp_priv->tstamp_rx_ctrl = tstamp_rx_ctrl;

	return copy_to_user(req->ifr_data, &config, sizeof(config)) ? -EFAULT : 0;
}

static int rsw3_eth_ioctl(struct net_device *ndev, struct ifreq *req, int cmd)
{
	if (!netif_running(ndev))
		return -EINVAL;

	switch (cmd) {
	case SIOCGHWTSTAMP:
		return rsw3_hwstamp_get(ndev, req);
	case SIOCSHWTSTAMP:
		return rsw3_hwstamp_set(ndev, req);
	default:
		return phy_mii_ioctl(ndev->phydev, req, cmd);
	}
}

static int rsw3_mac_addr_search(struct rsw3_private *priv, const u8 *mac,
				struct rsw3_dst_mac_search_result *result)
{
	u32 val;
	int ret;

	iowrite32((mac[0] << 8) | mac[1], priv->addr + FWMACTS0);
	iowrite32((mac[2] << 24) | (mac[3] << 16) | (mac[4] << 8) | mac[5],
		  priv->addr + FWMACTS1);
	iowrite32(FWMACTS2_MACTSEA(RSWITCH3_MAX_MAC_ENTRY), priv->addr + FWMACTS2);
	iowrite32(FWMACTS3_MAC_SEARCH, priv->addr + FWMACTS3);

	ret = rsw3_reg_wait(priv->addr, FWMACTSR0, FWMACTSR0_MACTS, 0);
	if (ret)
		return ret;

	val = ioread32(priv->addr + FWMACTSR0);
	if ((val & FWMACTSR0_MACSNF) == FWMACTSR0_MACSNF_FOUND) {
		result->found = true;
		result->qidx = ioread32(priv->addr + FWMACTSR2(GWCA_INDEX));
		result->entry = ioread32(priv->addr + FWMACTSR6);
	} else {
		result->found = false;
	}

	return 0;
}

static void rsw3_del_dst_mac_addr(struct rsw3_private *priv, const u8 *mac)
{
	iowrite32((mac[0] << 8) | mac[1], priv->addr + FWMACTL5);
	iowrite32((mac[2] << 24) | (mac[3] << 16) | (mac[4] << 8) | mac[5],
		  priv->addr + FWMACTL6);

	iowrite32(FWMACTL1_MACED, priv->addr + FWMACTL1);

	iowrite32(FWMACTL8_MACDVL(priv->gwca.index), priv->addr + FWMACTL8);

	rsw3_reg_wait(priv->addr, FWMACTLR, FWMACTLR_MACTL, 0);
}

static void rsw3_set_dst_mac_addr(struct net_device *ndev, const u8 *mac, unsigned int index)
{
	struct rsw3_device *rdev = netdev_priv(ndev);
	struct rsw3_private *priv = rdev->priv;
	struct rsw3_dst_mac_search_result mac_found;
	unsigned int qidx;
	u16 entry;
	u32 val;

	mutex_lock(&priv->m_lock);

	qidx = ((index - 1) % (NUM_QUEUES_RX_PER_NDEV - 1)) + 1;

	rsw3_mac_addr_search(priv, mac, &mac_found);
	if (mac_found.found) {
		if (mac_found.qidx == rdev->rx_queue[qidx]->index)
			goto out;

		rsw3_del_dst_mac_addr(priv, mac);
		entry = mac_found.entry;
	} else {
		val = ioread32(priv->addr + FWMACTEM);
		entry = FWMACTEM_MACTUEN(val) + FWMACTEM_MACTEN(val);
	}

	iowrite32(entry, priv->addr + FWMACTL0);
	iowrite32(FWMACTL4_MACDSLVL(rdev->port), priv->addr + FWMACTL4);
	iowrite32((mac[0] << 8) | mac[1], priv->addr + FWMACTL5);
	iowrite32((mac[2] << 24) | (mac[3] << 16) | (mac[4] << 8) | mac[5],
		  priv->addr + FWMACTL6);

	iowrite32(rdev->rx_queue[qidx]->index,
		  priv->addr + FWMACTL7(GWCA_INDEX));

	iowrite32(FWMACTL8_MACDVL(priv->gwca.index), priv->addr + FWMACTL8);

	rsw3_reg_wait(priv->addr, FWMACTLR, FWMACTLR_MACTL, 0);

out:
	mutex_unlock(&priv->m_lock);
}

static void rsw3_set_rx_mode(struct net_device *ndev)
{
	struct netdev_hw_addr *ha;
	unsigned int idx = 1;

	/* Set Unicast MAC address */
	netdev_for_each_uc_addr(ha, ndev) {
		rsw3_set_dst_mac_addr(ndev, ha->addr, idx);
		idx++;
	}
}

static const struct net_device_ops rsw3_netdev_ops = {
	.ndo_open = rsw3_open,
	.ndo_stop = rsw3_stop,
	.ndo_start_xmit = rsw3_start_xmit,
	.ndo_get_stats = rsw3_get_stats,
	.ndo_eth_ioctl = rsw3_eth_ioctl,
	.ndo_validate_addr = eth_validate_addr,
	.ndo_set_mac_address = eth_mac_addr,
	.ndo_set_rx_mode = rsw3_set_rx_mode,
};

static int rsw3_get_ts_info(struct net_device *ndev, struct kernel_ethtool_ts_info *info)
{
	struct rsw3_device *rdev = netdev_priv(ndev);

	info->phc_index = ptp_clock_index(rdev->priv->ptp_priv->clock);
	info->so_timestamping = SOF_TIMESTAMPING_TX_SOFTWARE |
				SOF_TIMESTAMPING_RX_SOFTWARE |
				SOF_TIMESTAMPING_SOFTWARE |
				SOF_TIMESTAMPING_TX_HARDWARE |
				SOF_TIMESTAMPING_RX_HARDWARE |
				SOF_TIMESTAMPING_RAW_HARDWARE;
	info->tx_types = BIT(HWTSTAMP_TX_OFF) | BIT(HWTSTAMP_TX_ON);
	info->rx_filters = BIT(HWTSTAMP_FILTER_NONE) | BIT(HWTSTAMP_FILTER_ALL);

	return 0;
}

static const struct ethtool_ops rsw3_ethtool_ops = {
	.get_ts_info = rsw3_get_ts_info,
	.get_link_ksettings = phy_ethtool_get_link_ksettings,
	.set_link_ksettings = phy_ethtool_set_link_ksettings,
};

static const struct of_device_id renesas_eth_sw_of_table[] = {
	{ .compatible = "renesas,r8a78000-ether-switch", },
	{ }
};
MODULE_DEVICE_TABLE(of, renesas_eth_sw_of_table);

static u32 rsw3_calc_psmcs(struct rsw3_private *priv)
{
	unsigned long rate = 0;
	bool relaxed;
	u32 psmcs;

	relaxed = of_property_read_bool(priv->pdev->dev.of_node, "renesas,mdc-relaxed");

	if (priv->clk)
		rate = clk_get_rate(priv->clk);

	if (!rate) {
		rate = relaxed ? RSW3_FALLBACK_CLK_RELAX : RSW3_FALLBACK_CLK_SPEC;
		dev_warn(&priv->pdev->dev,
			 "clk_get_rate() returned 0, using fallback %lu.%06lu MHz%s\n",
			 rate / 1000000, rate % 1000000,
			 relaxed ? " (relaxed)" : " (specific)");
	}

	/*
	 * PSMCS = round( clk / (MDC * 2) ) - 1
	 * Use rounding to minimize MDC error; clamp to field width if needed.
	 */
	psmcs = DIV_ROUND_CLOSEST(rate, (RSW3_MDC_HZ * 2));
	if (psmcs)
		psmcs -= 1;

	/* If MPIC_PSMCS field has a max width (e.g. 10 bits), clamp here.
	 * Example: psmcs &= MPIC_PSMCS_MASK;
	 * (left as-is if macro not available)
	 */
	return psmcs;
}

int rswitch3_attach_tsnes(struct device *dev, u32 tsnes_id, u32 rsw_port,
			  u32 fwd_mask, const u8 *mac)
{
	struct rsw3_private *priv = dev_get_drvdata(dev);
	struct rsw3_etha *etha;
	u32 val;
	int ret = 0;

	if (!priv)
		return -EPROBE_DEFER;

	if (tsnes_id >= RSWITCH3_NUM_TSNES)
		return -EINVAL;

	if (rsw_port != RSWITCH3_TSNES_PORT_BASE + tsnes_id)
		return -EINVAL;

	mutex_lock(&priv->tsnes_lock);

	if (priv->tsnes_attached & BIT(tsnes_id)) {
		ret = -EBUSY;
		goto out_unlock;
	}

	if (!rsw3_agent_clock_is_enabled(priv->addr, rsw_port))
		rsw3_agent_clock_ctrl(priv->addr, rsw_port, 1);

	ret = rsw3_etha_hw_init_tsn_internal(priv, rsw_port, mac);
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

		val = ioread32(etha->addr + MIOC);
		val &= ~MIOC_BIT3_SET;
		iowrite32(val, etha->addr + MIOC);
	}

	priv->tsnes_attached |= BIT(tsnes_id);
	priv->tsnes_fwd_mask[tsnes_id] = fwd_mask;
	rsw3_enable_tsn_forwarding(priv, tsnes_id, rsw_port, fwd_mask);

out_unlock:
	mutex_unlock(&priv->tsnes_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(rswitch3_attach_tsnes);

void rswitch3_detach_tsnes(struct device *dev, u32 tsnes_id, u32 rsw_port)
{
	struct rsw3_private *priv = dev_get_drvdata(dev);

	if (!priv || tsnes_id >= RSWITCH3_NUM_TSNES)
		return;

	mutex_lock(&priv->tsnes_lock);

	priv->tsnes_attached &= ~BIT(tsnes_id);
	priv->tsnes_fwd_mask[tsnes_id] = 0;
	rsw3_fwd_init(priv);

	mutex_unlock(&priv->tsnes_lock);
}
EXPORT_SYMBOL_GPL(rswitch3_detach_tsnes);

static void rsw3_etha_init(struct rsw3_private *priv, unsigned int index)
{
	struct rsw3_etha *etha = &priv->etha[index];
	struct rsw3_etha *etha_mii = &priv->etha_mii[index];

	memset(etha, 0, sizeof(*etha));
	etha->index = index;
	etha->addr = priv->addr + RSWITCH3_ETHA_OFFSET + index * RSWITCH3_ETHA_SIZE;
	etha->coma_addr = priv->addr;

	memset(etha_mii, 0, sizeof(*etha));
	etha_mii->index = index;
	etha_mii->addr = priv->addr + RSWITCH3_ETHA_OFFSET + index * RSWITCH3_ETHA_SIZE;
	etha_mii->coma_addr = priv->addr;

	/*
	 * MPIC.PSMCS = clk / (MDC * 2) - 1
	 * where MDC = 2.5 MHz. Compute using Hz and round safely.
	 * If clk rate is unavailable (0), fall back to a known-good SoC rate.
	 */
	etha_mii->psmcs = rsw3_calc_psmcs(priv);
}

static int rsw3_device_alloc(struct rsw3_private *priv, unsigned int index)
{
	struct platform_device *pdev = priv->pdev;
	struct rsw3_device *rdev;
	struct net_device *ndev;
	int err;

	if (index >= rswitch3_num_ports)
		return -EINVAL;

	ndev = alloc_etherdev_mqs(sizeof(struct rsw3_device),
				  NUM_QUEUES_TX_PER_NDEV,
				  NUM_QUEUES_RX_PER_NDEV);
	if (!ndev)
		return -ENOMEM;

	netif_set_real_num_tx_queues(ndev, NUM_QUEUES_TX_PER_NDEV);
	netif_set_real_num_rx_queues(ndev, NUM_QUEUES_RX_PER_NDEV);

	SET_NETDEV_DEV(ndev, &pdev->dev);
	ether_setup(ndev);

	rdev = netdev_priv(ndev);
	rdev->ndev = ndev;
	rdev->priv = priv;
	priv->rdev[index] = rdev;
	rdev->port = index;
	rdev->etha = &priv->etha[index];
	rdev->etha_mii = &priv->etha_mii[index];
	rdev->addr = priv->addr;

	ndev->base_addr = (unsigned long)rdev->addr;
	snprintf(ndev->name, IFNAMSIZ, "tsn%d", index);
	ndev->netdev_ops = &rsw3_netdev_ops;
	ndev->ethtool_ops = &rsw3_ethtool_ops;
	ndev->max_mtu = RSWITCH3_MAX_MTU;
	ndev->min_mtu = ETH_MIN_MTU;

	rdev->np_port = rsw3_get_port_node(rdev);
	rdev->disabled = !rdev->np_port;
	err = of_get_ethdev_address(rdev->np_port, ndev);

	if (err) {
		if (is_valid_ether_addr(rdev->etha->mac_addr))
			eth_hw_addr_set(ndev, rdev->etha->mac_addr);
		else
			eth_hw_addr_random(ndev);
	}

	rdev->etha_mii = &priv->etha_mii[rdev->etha_mii_idx];

	err = rsw3_etha_get_params(rdev);
	if (err < 0)
		goto out_get_params;

	err = rsw3_rxdmac_alloc(ndev);
	if (err < 0)
		goto out_rxdmac;

	err = rsw3_txdmac_alloc(ndev);
	if (err < 0)
		goto out_txdmac;

	return 0;

out_txdmac:
	rsw3_rxdmac_free(ndev);

out_rxdmac:
out_get_params:
	of_node_put(rdev->np_port);
	free_netdev(ndev);

	return err;
}

static void rsw3_device_free(struct rsw3_private *priv, unsigned int index)
{
	struct rsw3_device *rdev = priv->rdev[index];
	struct net_device *ndev = rdev->ndev;

	if (!priv->rdev[index]->disabled) {
		rsw3_txdmac_free(ndev);
		rsw3_rxdmac_free(ndev);
	}

	of_node_put(rdev->np_port);
	free_netdev(ndev);
}

static int rsw3_init(struct rsw3_private *priv)
{
	unsigned int i;
	int err;

	for (i = 0; i < rswitch3_num_ports; i++)
		rsw3_etha_init(priv, i);

	if (!parallel_mode)
		rsw3_clock_enable(priv);
	for (i = 0; i < rswitch3_num_ports; i++)
		rsw3_etha_read_mac_address(&priv->etha[i]);

	rsw3_reset(priv);

	if (!parallel_mode)
		rsw3_clock_enable(priv);

	if (!parallel_mode) {
		err = rsw3_bpool_config(priv);
		if (err < 0)
			return err;

		rsw3_coma_init(priv);

		err = rsw3_gwca_linkfix_alloc(priv);
		if (err < 0)
			return -ENOMEM;
	}

	for (i = 0; i < rswitch3_num_ports; i++) {
		err = rsw3_device_alloc(priv, i);
		if (err < 0) {
			for (; i-- > 0; )
				rsw3_device_free(priv, i);
		}
	}

	rsw3_top_init(priv);

	if (!parallel_mode) {
		err = rsw3_fwd_init(priv);
		if (err < 0)
			goto err_fwd_init;
	}

	 err = rcar_gen4_ptp_register(priv->ptp_priv, RCAR_GEN4_PTP_REG_LAYOUT,
				      RCAR_GEN5_PTP_CLOCK_X5H);
	if (err < 0)
		goto err_ptp_register;

	err = rsw3_gwca_request_irqs(priv);
	if (err < 0)
		goto err_gwca_request_irq;

	err = rsw3_gwca_hw_init(priv);
	if (err < 0)
		goto err_gwca_hw_init;

	err = rsw3_ether_port_init_all(priv);
	if (err)
		goto err_ether_port_init_all;

	rsw3_for_each_enabled_port(priv, i) {
		err = register_netdev(priv->rdev[i]->ndev);
		if (err) {
			rsw3_for_each_enabled_port_continue_reverse(priv, i)
				unregister_netdev(priv->rdev[i]->ndev);
			goto err_register_netdev;
		}
	}

	rsw3_for_each_enabled_port(priv, i)
		netdev_info(priv->rdev[i]->ndev, "MAC address %pM\n",
			    priv->rdev[i]->ndev->dev_addr);

	return 0;

err_register_netdev:
	rsw3_ether_port_deinit_all(priv);

err_ether_port_init_all:
	rsw3_gwca_hw_deinit(priv);

err_gwca_hw_init:
err_gwca_request_irq:
	rcar_gen4_ptp_unregister(priv->ptp_priv);

err_fwd_init:
err_ptp_register:
	for (i = 0; i < rswitch3_num_ports; i++)
		rsw3_device_free(priv, i);
	return err;
}

static int renesas_eth_sw_probe(struct platform_device *pdev)
{
	struct rsw3_private *priv;
	struct resource *res, *res_ptp;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	mutex_init(&priv->tsnes_lock);
	priv->tsnes_attached = 0;

	priv->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(priv->clk)) {
		dev_warn(&pdev->dev, "no input clock; will use fallback frequency for PSMCS (%pe)\n",
			 priv->clk);
		priv->clk = NULL;
	} else {
		ret = clk_prepare_enable(priv->clk);
		if (ret) {
			dev_warn(&pdev->dev, "Clock enable failed (%d); using fallback\n", ret);
			priv->clk = NULL;
		}
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "secure_base");
	if (!res) {
		ret = -EINVAL;
		goto err_sw_probe;
	}

	res_ptp = platform_get_resource_byname(pdev, IORESOURCE_MEM, "gptp_base");
	if (!res_ptp) {
		ret = -EINVAL;
		goto err_sw_probe;
	}

	if (!parallel_mode)
		parallel_mode = of_property_read_bool(pdev->dev.of_node, "parallel_mode");

	if (parallel_mode)
		rswitch3_num_ports = 1;

	platform_set_drvdata(pdev, priv);
	priv->pdev = pdev;

	priv->addr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(priv->addr)) {
		ret = PTR_ERR(priv->addr);
		goto err_sw_probe;
	}

	priv->ptp_priv = rcar_gen4_ptp_alloc(pdev);
	if (!priv->ptp_priv) {
		ret = -ENOMEM;
		goto err_sw_probe;
	}

	priv->ptp_priv->addr = devm_ioremap_resource(&pdev->dev, res_ptp);
	if (IS_ERR(priv->ptp_priv->addr)) {
		ret = PTR_ERR(priv->ptp_priv->addr);
		goto err_sw_probe;
	}

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(40));
	if (ret < 0) {
		ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
		if (ret < 0)
			goto err_sw_probe;
	}

	mutex_init(&priv->m_lock);

	priv->gwca.index = AGENT_INDEX_GWCA;
	priv->gwca.num_queues = min(rswitch3_num_ports * NUM_QUEUES_PER_NDEV,
				    RSWITCH3_MAX_NUM_QUEUES);
	priv->gwca.queues = devm_kcalloc(&pdev->dev, priv->gwca.num_queues,
					 sizeof(*priv->gwca.queues), GFP_KERNEL);
	if (!priv->gwca.queues) {
		ret = -ENOMEM;
		goto err_sw_probe;
	}

	if (!parallel_mode) {
		pm_runtime_enable(&pdev->dev);
		pm_runtime_get_sync(&pdev->dev);
	}

	ret = rsw3_init(priv);
	if (ret < 0) {
		if (!parallel_mode) {
			pm_runtime_put(&pdev->dev);
			pm_runtime_disable(&pdev->dev);
		}
		goto err_sw_probe;
	}

	device_set_wakeup_capable(&pdev->dev, 1);

	return 0;

err_sw_probe:
	clk_disable_unprepare(priv->clk);

	return ret;
}

static void rsw3_deinit(struct rsw3_private *priv)
{
	unsigned int i;

	rsw3_gwca_hw_deinit(priv);

	rcar_gen4_ptp_unregister(priv->ptp_priv);

	rsw3_for_each_enabled_port(priv, i) {
		struct rsw3_device *rdev = priv->rdev[i];

		unregister_netdev(rdev->ndev);
		rsw3_ether_port_deinit_one(rdev);
		phy_exit(priv->rdev[i]->pcs);
	}

	for (i = 0; i < rswitch3_num_ports; i++)
		rsw3_device_free(priv, i);

	rsw3_gwca_linkfix_free(priv);

	rsw3_clock_disable(priv);
}

static void renesas_eth_sw_remove(struct platform_device *pdev)
{
	struct rsw3_private *priv = platform_get_drvdata(pdev);

	if (priv->clk)
		clk_disable_unprepare(priv->clk);

	rsw3_deinit(priv);

	pm_runtime_put(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	platform_set_drvdata(pdev, NULL);
}

static int renesas_eth_sw_suspend(struct device *dev)
{
	struct rsw3_private *priv = dev_get_drvdata(dev);
	struct net_device *ndev;
	unsigned int i;

	rsw3_for_each_enabled_port(priv, i) {
		ndev = priv->rdev[i]->ndev;
		if (netif_running(ndev)) {
			netif_device_detach(ndev);
			rsw3_stop(ndev);
		}

		if (priv->rdev[i]->pcs->init_count) {
			phy_exit(priv->rdev[i]->pcs);
			priv->rdev[i]->pcs->init_count = 0;
		}

		rsw3_txdmac_free(ndev);
		rsw3_rxdmac_free(ndev);
	}

	for (i = 0; i < rswitch3_num_ports; i++) {
		struct rsw3_etha *etha_mii = &priv->etha_mii[i];

		etha_mii->operated = false;
	}

	clk_disable_unprepare(priv->clk);

	return 0;
}

static int renesas_eth_sw_resume(struct device *dev)
{
	struct rsw3_private *priv = dev_get_drvdata(dev);
	struct net_device *ndev;
	unsigned int i;

	if (clk_prepare_enable(priv->clk)) {
		dev_warn(dev, "Failed to enable clock, will use fallback frequency\n");
		priv->clk = NULL;
	}

	rsw3_clock_enable(priv);

	rsw3_bpool_config(priv);

	rsw3_coma_init(priv);

	rsw3_fwd_init(priv);

	rsw3_for_each_enabled_port(priv, i) {
		ndev = priv->rdev[i]->ndev;

		rsw3_rxdmac_alloc(ndev);
		rsw3_txdmac_alloc(ndev);
	}

	rsw3_top_init(priv);

	rsw3_gwca_hw_init(priv);

	rsw3_ether_port_init_all(priv);

	rcar_gen4_ptp_reinit_hw(priv->ptp_priv);

	rsw3_for_each_enabled_port(priv, i) {
		ndev = priv->rdev[i]->ndev;
		if (netif_running(ndev)) {
			rsw3_open(ndev);
			netif_device_attach(ndev);
		}
	}

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(renesas_eth_sw_pm_ops, renesas_eth_sw_suspend,
				renesas_eth_sw_resume);

static struct platform_driver renesas_eth_sw_driver_platform = {
	.probe = renesas_eth_sw_probe,
	.remove_new = renesas_eth_sw_remove,
	.driver = {
		.name = "renesas_eth_sw",
		.pm = pm_sleep_ptr(&renesas_eth_sw_pm_ops),
		.of_match_table = renesas_eth_sw_of_table,
	}
};
module_platform_driver(renesas_eth_sw_driver_platform);
MODULE_DESCRIPTION("Renesas Ethernet Switch3 device driver");
MODULE_LICENSE("GPL");
