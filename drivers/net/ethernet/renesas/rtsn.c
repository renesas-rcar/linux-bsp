// SPDX-License-Identifier: GPL-2.0
/* Renesas Ethernet-TSN device driver
 *
 * Copyright (C) 2022 Renesas Electronics Corporation
 */

#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/net_tstamp.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/clk.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/iopoll.h>
#include <linux/reset.h>

#include "rtsn.h"
#include "rtsn_ptp.h"

u32 rtsn_read(void __iomem *addr)
{
	return ioread32(addr);
}

void rtsn_write(u32 data, void __iomem *addr)
{
	iowrite32(data, addr);
}

void rtsn_modify(void __iomem *addr, u32 clear, u32 set)
{
	rtsn_write((rtsn_read(addr) & ~clear) | set, addr);
}

int rtsn_reg_wait(void __iomem *addr, u32 mask, u32 expected)
{
	u32 val;
	int ret;

	ret = readl_poll_timeout(addr, val, (val & mask) == expected,
				 RTSN_INTERVAL_US, RTSN_TIMEOUT_US);

	return ret;
}

static void rtsn_ctrl_data_irq(struct rtsn_private *priv, bool enable)
{
	if (enable) {
		rtsn_write(TDIE_TDID_TDX(TX_CHAIN_IDX), priv->addr + TDIE0);
		rtsn_write(RDIE_RDID_RDX(RX_CHAIN_IDX), priv->addr + RDIE0);
	} else {
		rtsn_write(TDIE_TDID_TDX(TX_CHAIN_IDX), priv->addr + TDID0);
		rtsn_write(RDIE_RDID_RDX(RX_CHAIN_IDX), priv->addr + RDID0);
	}
}

static void rtsn_get_timestamp(struct rtsn_private *priv, struct timespec64 *ts)
{
	struct rtsn_ptp_private *ptp_priv = priv->ptp_priv;

	ptp_priv->info.gettime64(&ptp_priv->info, ts);
}

static int rtsn_tx_free(struct net_device *ndev, bool free_txed_only)
{
	struct rtsn_private *priv = netdev_priv(ndev);
	struct rtsn_ext_desc *desc;
	struct sk_buff *skb;
	int free_num = 0;
	int entry, size;

	for (; priv->cur_tx - priv->dirty_tx > 0; priv->dirty_tx++) {
		entry = priv->dirty_tx % priv->num_tx_ring;
		desc = &priv->tx_ring[entry];
		if (free_txed_only && (desc->die_dt & DT_MASK) != DT_FEMPTY)
			break;

		dma_rmb();
		size = le16_to_cpu(desc->info_ds) & TX_DS;
		skb = priv->tx_skb[entry];
		if (skb) {
			if (skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) {
				struct skb_shared_hwtstamps shhwtstamps;
				struct timespec64 ts;

				rtsn_get_timestamp(priv, &ts);
				memset(&shhwtstamps, 0, sizeof(shhwtstamps));
				shhwtstamps.hwtstamp = timespec64_to_ktime(ts);
				skb_tstamp_tx(skb, &shhwtstamps);
			}
			dma_unmap_single(ndev->dev.parent, le32_to_cpu(desc->dptr),
					 size, DMA_TO_DEVICE);
			dev_kfree_skb_any(priv->tx_skb[entry]);
			free_num++;
		}
		desc->die_dt = DT_EEMPTY;
		ndev->stats.tx_packets++;
		ndev->stats.tx_bytes += size;
	}

	desc = &priv->tx_ring[priv->num_tx_ring];
	desc->die_dt = DT_LINK;

	return free_num;
}

static bool rtsn_rx(struct net_device *ndev, int *quota)
{
	struct rtsn_private *priv = netdev_priv(ndev);
	int entry = priv->cur_rx % priv->num_rx_ring;
	int boguscnt = priv->dirty_rx + priv->num_rx_ring - priv->cur_rx;
	struct rtsn_ext_ts_desc *desc = &priv->rx_ring[entry];
	struct sk_buff *skb;
	dma_addr_t dma_addr;
	u16 pkt_len;
	u32 get_ts;
	int limit;

	boguscnt = min(boguscnt, *quota);
	limit = boguscnt;

	while ((desc->die_dt & DT_MASK) != DT_FEMPTY) {
		dma_rmb();
		pkt_len = le16_to_cpu(desc->info_ds) & RX_DS;
		if (--boguscnt < 0)
			break;

		skb = priv->rx_skb[entry];
		priv->rx_skb[entry] = NULL;
		dma_addr = le32_to_cpu(desc->dptr);
		dma_unmap_single(ndev->dev.parent, dma_addr, PKT_BUF_SZ, DMA_FROM_DEVICE);
		get_ts = priv->ptp_priv->tstamp_rx_ctrl & RTSN_RXTSTAMP_TYPE_V2_L2_EVENT;
		if (get_ts) {
			struct skb_shared_hwtstamps *shhwtstamps;
			struct timespec64 ts;

			shhwtstamps = skb_hwtstamps(skb);
			memset(shhwtstamps, 0, sizeof(*shhwtstamps));
			ts.tv_sec = (u64)le32_to_cpu(desc->ts_sec);
			ts.tv_nsec = le32_to_cpu(desc->ts_nsec & cpu_to_le32(0x3FFFFFFF));
			shhwtstamps->hwtstamp = timespec64_to_ktime(ts);
		}

		skb_put(skb, pkt_len);
		skb->protocol = eth_type_trans(skb, ndev);
		netif_receive_skb(skb);
		ndev->stats.rx_packets++;
		ndev->stats.rx_bytes += pkt_len;

		entry = (++priv->cur_rx) % priv->num_rx_ring;
		desc = &priv->rx_ring[entry];
	}

	/* Refill the RX ring buffers */
	for (; priv->cur_rx - priv->dirty_rx > 0; priv->dirty_rx++) {
		entry = priv->dirty_rx % priv->num_rx_ring;
		desc = &priv->rx_ring[entry];
		desc->info_ds = cpu_to_le16(PKT_BUF_SZ);

		if (!priv->rx_skb[entry]) {
			skb = netdev_alloc_skb(ndev, PKT_BUF_SZ + RTSN_ALIGN - 1);
			if (!skb)
				break;
			skb_reserve(skb, NET_IP_ALIGN);
			dma_addr = dma_map_single(ndev->dev.parent, skb->data,
						  le16_to_cpu(desc->info_ds),
						  DMA_FROM_DEVICE);
			if (dma_mapping_error(ndev->dev.parent, dma_addr))
				desc->info_ds = cpu_to_le16(0);
			desc->dptr = cpu_to_le32(dma_addr);
			skb_checksum_none_assert(skb);
			priv->rx_skb[entry] = skb;
		}
		dma_wmb();
		desc->die_dt = DT_FEMPTY | D_DIE;
	}

	desc = &priv->rx_ring[priv->num_rx_ring];
	desc->die_dt = DT_LINK;

	*quota -= limit - (++boguscnt);

	return boguscnt <= 0;
}

static int rtsn_poll(struct napi_struct *napi, int budget)
{
	struct net_device *ndev = napi->dev;
	struct rtsn_private *priv = netdev_priv(ndev);
	int quota = budget;

	if (rtsn_rx(ndev, &quota))
		goto out;

	rtsn_tx_free(ndev, true);

	netif_wake_subqueue(ndev, 0);

	napi_complete(napi);

	/* Re-enable TX/RX interrupts */
	rtsn_ctrl_data_irq(priv, true);
	__iowmb();

out:
	return budget - quota;
}

static int rtsn_desc_alloc(struct rtsn_private *priv)
{
	struct device *dev = &priv->pdev->dev;
	int i;

	priv->tx_desc_bat_size = sizeof(struct rtsn_desc) * TX_NUM_CHAINS;
	priv->tx_desc_bat = dma_alloc_coherent(dev, priv->tx_desc_bat_size,
					       &priv->tx_desc_bat_dma, GFP_KERNEL);

	if (!priv->tx_desc_bat)
		return -ENOMEM;

	for (i = 0; i < TX_NUM_CHAINS; i++)
		priv->tx_desc_bat[i].die_dt = DT_EOS;

	priv->rx_desc_bat_size = sizeof(struct rtsn_desc) * RX_NUM_CHAINS;
	priv->rx_desc_bat = dma_alloc_coherent(dev, priv->rx_desc_bat_size,
					       &priv->rx_desc_bat_dma, GFP_KERNEL);

	if (!priv->rx_desc_bat)
		return -ENOMEM;

	for (i = 0; i < RX_NUM_CHAINS; i++)
		priv->rx_desc_bat[i].die_dt = DT_EOS;

	return 0;
}

static void rtsn_desc_free(struct rtsn_private *priv)
{
	if (priv->tx_desc_bat)
		dma_free_coherent(&priv->pdev->dev, priv->tx_desc_bat_size, priv->tx_desc_bat,
				  priv->tx_desc_bat_dma);
	priv->tx_desc_bat = NULL;

	if (priv->rx_desc_bat)
		dma_free_coherent(&priv->pdev->dev, priv->rx_desc_bat_size, priv->rx_desc_bat,
				  priv->rx_desc_bat_dma);
	priv->rx_desc_bat = NULL;
}

static void rtsn_chain_free(struct rtsn_private *priv)
{
	struct device *dev = &priv->pdev->dev;

	dma_free_coherent(dev, sizeof(struct rtsn_ext_desc) * (priv->num_tx_ring + 1),
			  priv->tx_ring, priv->tx_desc_dma);
	priv->tx_ring = NULL;

	dma_free_coherent(dev, sizeof(struct rtsn_ext_ts_desc) * (priv->num_rx_ring + 1),
			  priv->rx_ring, priv->rx_desc_dma);
	priv->rx_ring = NULL;

	kfree(priv->tx_skb);
	priv->tx_skb = NULL;

	kfree(priv->rx_skb);
	priv->rx_skb = NULL;
}

static int rtsn_chain_init(struct rtsn_private *priv, int tx_size, int rx_size)
{
	struct net_device *ndev = priv->ndev;
	struct sk_buff *skb;
	int i;

	priv->num_tx_ring = tx_size;
	priv->num_rx_ring = rx_size;

	priv->tx_skb = kcalloc(tx_size, sizeof(*priv->tx_skb), GFP_KERNEL);
	priv->rx_skb = kcalloc(rx_size, sizeof(*priv->rx_skb), GFP_KERNEL);

	if (!priv->rx_skb || !priv->tx_skb)
		goto error;

	for (i = 0; i < rx_size; i++) {
		skb = netdev_alloc_skb(ndev, PKT_BUF_SZ + RTSN_ALIGN - 1);
		if (!skb)
			goto error;
		skb_reserve(skb, NET_IP_ALIGN);
		priv->rx_skb[i] = skb;
	}

	/* Allocate TX, RX descriptors */
	priv->tx_ring = dma_alloc_coherent(ndev->dev.parent,
					   sizeof(struct rtsn_ext_desc) * (tx_size + 1),
					   &priv->tx_desc_dma, GFP_KERNEL);
	priv->rx_ring = dma_alloc_coherent(ndev->dev.parent,
					   sizeof(struct rtsn_ext_ts_desc) * (rx_size + 1),
					   &priv->rx_desc_dma, GFP_KERNEL);

	if (!priv->tx_ring || !priv->rx_ring)
		goto error;

	return 0;

error:
	rtsn_chain_free(priv);

	return -ENOMEM;
}

static void rtsn_chain_format(struct rtsn_private *priv)
{
	struct net_device *ndev = priv->ndev;
	struct rtsn_ext_ts_desc *rx_desc;
	struct rtsn_ext_desc *tx_desc;
	struct rtsn_desc *bat_desc;
	int tx_ring_size = sizeof(*tx_desc) * priv->num_tx_ring;
	int rx_ring_size = sizeof(*rx_desc) * priv->num_rx_ring;
	dma_addr_t dma_addr;
	int i;

	priv->cur_tx = 0;
	priv->cur_rx = 0;
	priv->dirty_rx = 0;
	priv->dirty_tx = 0;

	/* TX */
	memset(priv->tx_ring, 0, tx_ring_size);
	for (i = 0, tx_desc = priv->tx_ring; i < priv->num_tx_ring; i++, tx_desc++)
		tx_desc->die_dt = DT_EEMPTY | D_DIE;

	tx_desc->dptr = cpu_to_le32((u32)priv->tx_desc_dma);
	tx_desc->die_dt = DT_LINK;

	bat_desc = &priv->tx_desc_bat[TX_CHAIN_IDX];
	bat_desc->die_dt = DT_LINK;
	bat_desc->dptr = cpu_to_le32((u32)priv->tx_desc_dma);

	/* RX */
	memset(priv->rx_ring, 0, rx_ring_size);
	for (i = 0, rx_desc = priv->rx_ring; i < priv->num_rx_ring; i++, rx_desc++) {
		dma_addr = dma_map_single(ndev->dev.parent, priv->rx_skb[i]->data, PKT_BUF_SZ,
					  DMA_FROM_DEVICE);
		if (!dma_mapping_error(ndev->dev.parent, dma_addr))
			rx_desc->info_ds = cpu_to_le16(PKT_BUF_SZ);
		rx_desc->dptr = cpu_to_le32((u32)dma_addr);
		rx_desc->die_dt = DT_FEMPTY | D_DIE;
	}
	rx_desc->dptr = cpu_to_le32((u32)priv->rx_desc_dma);
	rx_desc->die_dt = DT_LINK;

	bat_desc = &priv->rx_desc_bat[RX_CHAIN_IDX];
	bat_desc->die_dt = DT_LINK;
	bat_desc->dptr = cpu_to_le32((u32)priv->rx_desc_dma);
}

static int rtsn_dmac_init(struct rtsn_private *priv)
{
	int err;

	err = rtsn_chain_init(priv, TX_CHAIN_SIZE, RX_CHAIN_SIZE);
	if (err)
		return err;

	rtsn_chain_format(priv);

	return 0;
}

static int rtsn_change_mode(struct rtsn_private *priv, enum rtsn_mode mode)
{
	int ret;

	rtsn_write(mode, priv->addr + OCR);

	ret = rtsn_reg_wait(priv->addr + OSR, OSR_OPS, 1 << mode);

	return ret;
}

static inline int rtsn_hook_irq(unsigned int irq, irq_handler_t handler, unsigned long flags,
				struct rtsn_private *priv, const char *ch)
{
	char *name;
	int err;

	name = devm_kasprintf(&priv->pdev->dev, GFP_KERNEL, "%s:%s", priv->ndev->name, ch);
	if (!name)
		return -ENOMEM;

	err = request_irq(irq, handler, flags, name, priv);
	if (err)
		goto error;

	return 0;

error:
	netdev_err(priv->ndev, "Cannot request IRQ %s\n", name);
	free_irq(irq, priv);
	return err;
}

static int rtsn_get_data_irq_status(struct rtsn_private *priv)
{
	u32 val;

	val = rtsn_read(priv->addr + TDIS0) | TDIS_TDS(TX_CHAIN_IDX);
	val |= rtsn_read(priv->addr + RDIS0) | RDIS_RDS(RX_CHAIN_IDX);

	return val;
}

static void rtsn_queue_interrupt(struct rtsn_private *priv)
{
	if (napi_schedule_prep(&priv->napi)) {
		rtsn_ctrl_data_irq(priv, false);
		__napi_schedule(&priv->napi);
	}
}

static irqreturn_t rtsn_data_irq(struct rtsn_private *priv)
{
	/* Clear TX/RX irq status */
	rtsn_write(TDIS_TDS(TX_CHAIN_IDX), priv->addr + TDIS0);
	rtsn_write(RDIS_RDS(RX_CHAIN_IDX), priv->addr + RDIS0);

	rtsn_queue_interrupt(priv);

	return IRQ_HANDLED;
}

static irqreturn_t rtsn_irq(int irq, void *dev_id)
{
	struct rtsn_private *priv = dev_id;
	irqreturn_t ret = IRQ_NONE;

	if (rtsn_get_data_irq_status(priv))
		ret = rtsn_data_irq(priv);

	return ret;
}

static int rtsn_request_irqs(struct rtsn_private *priv)
{
	int err;

	/* TODO: Support more irqs */

	priv->tx_data_irq = platform_get_irq_byname(priv->pdev, "tx_data");
	if (priv->tx_data_irq < 0)
		return priv->tx_data_irq;

	err = rtsn_hook_irq(priv->tx_data_irq, rtsn_irq, 0, priv, "tx_data");
	if (err)
		return err;

	priv->rx_data_irq = platform_get_irq_byname(priv->pdev, "rx_data");
	if (priv->rx_data_irq < 0)
		return priv->rx_data_irq;

	err = rtsn_hook_irq(priv->rx_data_irq, rtsn_irq, 0, priv, "rx_data");
	if (err)
		return err;

	return 0;
}

static void rtsn_free_irqs(struct rtsn_private *priv)
{
	free_irq(priv->tx_data_irq, priv);
	free_irq(priv->rx_data_irq, priv);
}

static int rtsn_axibmi_init(struct rtsn_private *priv)
{
	int ret;

	ret = rtsn_reg_wait(priv->addr + RR, RR_RST, RR_RST_COMPLETE);
	if (ret)
		return ret;

	/* Set AXIWC */
	rtsn_write(AXIWC_DEFAULT, priv->addr + AXIWC);

	/* Set AXIRC */
	rtsn_write(AXIRC_DEFAULT, priv->addr + AXIRC);

	/* TX Descritor chain setting */
	rtsn_write(TATLS0_TEDE | TATLS0_TATEN(TX_CHAIN_IDX), priv->addr + TATLS0);
	rtsn_write(priv->tx_desc_bat_dma + TX_CHAIN_ADDR_OFFSET, priv->addr + TATLS1);
	rtsn_write(TATLR_TATL, priv->addr + TATLR);

	ret = rtsn_reg_wait(priv->addr + TATLR, TATLR_TATL, 0);
	if (ret)
		return ret;

	/* RX Descriptor chain setting */
	rtsn_write(RATLS0_RETS | RATLS0_REDE | RATLS0_RATEN(RX_CHAIN_IDX), priv->addr + RATLS0);
	rtsn_write(priv->rx_desc_bat_dma + RX_CHAIN_ADDR_OFFSET, priv->addr + RATLS1);
	rtsn_write(RATLR_RATL, priv->addr + RATLR);

	ret = rtsn_reg_wait(priv->addr + RATLR, RATLR_RATL, 0);
	if (ret)
		return ret;

	/* Enable TX/RX interrupts */
	rtsn_ctrl_data_irq(priv, true);

	return 0;
}

static void rtsn_mhd_init(struct rtsn_private *priv)
{
	/* TX General setting */
	rtsn_write(TGC1_STTV_DEFAULT | TGC1_TQTM_SFM, priv->addr + TGC1);
	rtsn_write(TMS_MFS_MAX, priv->addr + TMS0);

	/* RX Filter IP */
	rtsn_write(CFCR_SDID(RX_CHAIN_IDX), priv->addr + CFCR0);
	rtsn_write(FMSCR_FMSIE(RX_CHAIN_IDX), priv->addr + FMSCR);
}

static int rtsn_get_phy_params(struct rtsn_private *priv)
{
	struct device_node *np = priv->ndev->dev.parent->of_node;

	of_get_phy_mode(np, &priv->iface);
	switch (priv->iface) {
	case PHY_INTERFACE_MODE_MII:
		priv->speed = 100;
		break;
	case PHY_INTERFACE_MODE_RGMII:
		priv->speed = 1000;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static void rtsn_set_phy_interface(struct rtsn_private *priv)
{
	u32 val;

	switch (priv->iface) {
	case PHY_INTERFACE_MODE_MII:
		val = MPIC_PIS_MII;
		break;
	/*  R-Car V4H v1.0 does not support RMAC/RGMII,
	 *  but it has internal GMII-RGMII converter.
	 */
	case PHY_INTERFACE_MODE_RGMII:
		val = MPIC_PIS_GMII;
		break;
	default:
		return;
	}

	rtsn_modify(priv->addr + MPIC, MPIC_PIS_MASK, val);
}

static void rtsn_set_rate(struct rtsn_private *priv)
{
	u32 val;

	switch (priv->speed) {
	case 10:
		val = MPIC_LSC_10M;
		break;
	case 100:
		val = MPIC_LSC_100M;
		break;
	case 1000:
		val = MPIC_LSC_1G;
		break;
	}

	/* FIXME: Should change to CONFIG mode? */
	rtsn_modify(priv->addr + MPIC, MPIC_LSC_MASK, val);
}

static int rtsn_rmac_init(struct rtsn_private *priv)
{
	const u8 *mac_addr = priv->ndev->dev_addr;
	int ret;

	ret = rtsn_get_phy_params(priv);
	if (ret)
		return ret;

	/* Set MAC address */
	rtsn_write((mac_addr[0] << 8) | mac_addr[1], priv->addr + MRMAC0);
	rtsn_write((mac_addr[2] << 24) | (mac_addr[3] << 16) | (mac_addr[4] << 8) | mac_addr[5],
		   priv->addr + MRMAC1);

	/* Set xMII type */
	rtsn_set_phy_interface(priv);
	rtsn_set_rate(priv);

	/* Enable MII */
	rtsn_modify(priv->addr + MPIC, MPIC_PSMCS_MASK | MPIC_PSMHT_MASK,
		    MPIC_PSMCS_DEFAULT | MPIC_PSMHT_DEFAULT);

	/* Link verification */
	rtsn_modify(priv->addr + MLVC, MLVC_PLV, MLVC_PLV);
	ret = rtsn_reg_wait(priv->addr + MLVC, MLVC_PLV, 0);
	if (ret)
		return ret;

	return ret;
}

static void rtsn_set_delay_mode(struct rtsn_private *priv)
{
	struct device_node *np = priv->ndev->dev.parent->of_node;
	bool delay;
	u32 val;

	delay = of_property_read_bool(np, "tx-internal-delay");
	if (delay)
		rtsn_write(GPOUT_TDM, priv->addr + GPOUT);

	delay = of_property_read_bool(np, "rx-internal-delay");
	if (delay) {
		val = rtsn_read(priv->addr + GPOUT);
		rtsn_write(val | GPOUT_RDM, priv->addr + GPOUT);
	}
}

static int rtsn_hw_init(struct rtsn_private *priv)
{
	int ret;

	/* Trigger reset */
	reset_control_reset(priv->rst);
	mdelay(1);

	/* Change to CONFIG mode */
	ret = rtsn_change_mode(priv, OCR_OPC_CONFIG);
	if (ret)
		return ret;

	ret = rtsn_axibmi_init(priv);
	if (ret)
		return ret;

	rtsn_mhd_init(priv);

	ret = rtsn_rmac_init(priv);
	if (ret)
		return ret;

	rtsn_set_delay_mode(priv);

	ret = rtsn_change_mode(priv, OCR_OPC_DISABLE);
	if (ret)
		return ret;

	/* Change to OPERATION mode */
	ret = rtsn_change_mode(priv, OCR_OPC_OPERATION);

	return ret;
}

static int rtsn_mii_access(struct mii_bus *bus, bool read, int devad, int regad, u16 data)
{
	struct rtsn_private *priv = bus->priv;
	u32 val;
	int ret;

	val = MPSM_PDA(devad) | MPSM_PRA(regad) | MPSM_PSME;

	if (!read)
		val |= MPSM_PSMAD | MPSM_PRD_SET(data);

	rtsn_write(val, priv->addr + MPSM);

	ret = rtsn_reg_wait(priv->addr + MPSM, MPSM_PSME, 0);
	if (ret)
		return ret;

	if (read)
		ret = MPSM_PRD_GET(rtsn_read(priv->addr + MPSM));

	return ret;
}

static int rtsn_mii_access_indirect(struct mii_bus *bus, bool read, int devad, int regad, u16 data)
{
	int ret;

	ret = rtsn_mii_access(bus, false, 0, MII_MMD_CTRL, devad);
	if (ret)
		return ret;

	ret = rtsn_mii_access(bus, false, 0, MII_MMD_DATA, regad);
	if (ret)
		return ret;

	ret = rtsn_mii_access(bus, false, 0, MII_MMD_CTRL, devad | MII_MMD_CTRL_NOINCR);
	if (ret)
		return ret;

	if (read)
		ret = rtsn_mii_access(bus, true, 0, MII_MMD_DATA, 0);
	else
		ret = rtsn_mii_access(bus, false, 0, MII_MMD_DATA, data);

	return ret;
}

static int rtsn_mii_read(struct mii_bus *bus, int addr, int regnum)
{
	int ret;

	if (regnum & MII_ADDR_C45) {
		int devad = (regnum >> MII_DEVADDR_C45_SHIFT) & 0x1f;
		int regad = regnum & MII_REGADDR_C45_MASK;

		ret = rtsn_mii_access_indirect(bus, true, devad, regad, 0);
	} else {
		ret = rtsn_mii_access(bus, true, addr, regnum, 0);
	}

	return ret;
}

static int rtsn_mii_write(struct mii_bus *bus, int addr, int regnum, u16 val)
{
	int ret;

	if (regnum & MII_ADDR_C45) {
		int devad = (regnum >> MII_DEVADDR_C45_SHIFT) & 0x1f;
		int regad = regnum & MII_REGADDR_C45_MASK;

		ret = rtsn_mii_access_indirect(bus, false, devad, regad, val);
	} else {
		ret = rtsn_mii_access(bus, false, addr, regnum, val);
	}

	return ret;
}

static int rtsn_mii_register(struct rtsn_private *priv)
{
	struct platform_device *pdev = priv->pdev;
	struct device *dev = &pdev->dev;
	struct mii_bus *mii;
	int err;

	mii = mdiobus_alloc();
	if (!mii)
		return -ENOMEM;

	mii->name = "rtsn_mii";
	sprintf(mii->id, "%s-%x", pdev->name, pdev->id);
	mii->priv = priv;
	mii->read = rtsn_mii_read;
	mii->write = rtsn_mii_write;
	mii->parent = dev;

	err = of_mdiobus_register(mii, dev->of_node);
	if (err)
		goto out_free_bus;

	priv->mii = mii;

	return 0;

out_free_bus:
	mdiobus_free(mii);
	return err;
}

static void rtsn_mii_unregister(struct rtsn_private *priv)
{
	if (priv->mii) {
		mdiobus_unregister(priv->mii);
		mdiobus_free(priv->mii);
		priv->mii = NULL;
	}
}

static void rtsn_adjust_link(struct net_device *ndev)
{
	struct rtsn_private *priv = netdev_priv(ndev);
	struct phy_device *phydev = ndev->phydev;
	/* FIXME: Support this feature */

	phy_print_status(phydev);
	priv->link = phydev->link;
}

static int rtsn_phy_init(struct rtsn_private *priv)
{
	struct device_node *np = priv->ndev->dev.parent->of_node;
	struct phy_device *phydev;
	struct device_node *phy;
	int err = 0;

	priv->link = 0;

	/* TODO: Support fixed-link? */
	phy = of_parse_phandle(np, "phy-handle", 0);
	if (!phy)
		return -ENOENT;

	phydev = of_phy_connect(priv->ndev, phy, rtsn_adjust_link, 0, priv->iface);
	if (!phydev) {
		err = -ENOENT;
		goto out;
	}

	/* Only support full-duplex mode */
	phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_10baseT_Half_BIT);
	phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_100baseT_Half_BIT);
	phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_1000baseT_Half_BIT);

	phy_attached_info(phydev);

out:
	of_node_put(phy);
	return err;
}

static void rtsn_phy_deinit(struct rtsn_private *priv)
{
	phy_stop(priv->ndev->phydev);
	phy_disconnect(priv->ndev->phydev);
	priv->ndev->phydev = NULL;
}

static int rtsn_init(struct rtsn_private *priv)
{
	int ret;

	ret = rtsn_desc_alloc(priv);
	if (ret)
		return ret;

	ret = rtsn_dmac_init(priv);
	if (ret)
		goto out_free_desc;

	/* HW initialization */
	ret = rtsn_hw_init(priv);
	if (ret)
		goto out_free_desc;

	ret = rtsn_mii_register(priv);
	if (ret)
		goto out_free_desc;

	ret = rtsn_phy_init(priv);
	if (ret)
		goto out_free_desc;

	ret = rtsn_request_irqs(priv);
	if (ret)
		goto out_free_desc;

	return 0;

out_free_desc:
	rtsn_desc_free(priv);
	return ret;
}

static void rtsn_deinit(struct rtsn_private *priv)
{
	rtsn_phy_deinit(priv);
	rtsn_mii_unregister(priv);
	rtsn_desc_free(priv);
	rtsn_chain_free(priv);
	rtsn_free_irqs(priv);
}

static void rtsn_read_mac_address(struct net_device *ndev, const u8 *mac)
{
	struct rtsn_private *priv = netdev_priv(ndev);

	if (!IS_ERR(mac)) {
		ether_addr_copy(ndev->dev_addr, mac);
	} else {
		u32 mrmac0 = rtsn_read(priv->addr + MRMAC0);
		u32 mrmac1 = rtsn_read(priv->addr + MRMAC1);

		ndev->dev_addr[0] = (mrmac0 >>  8) & 0xFF;
		ndev->dev_addr[1] = (mrmac0 >>  0) & 0xFF;
		ndev->dev_addr[2] = (mrmac1 >> 24) & 0xFF;
		ndev->dev_addr[3] = (mrmac1 >> 16) & 0xFF;
		ndev->dev_addr[4] = (mrmac1 >>  8) & 0xFF;
		ndev->dev_addr[5] = (mrmac1 >>  0) & 0xFF;
	}
}

static int rtsn_open(struct net_device *ndev)
{
	struct rtsn_private *priv = netdev_priv(ndev);
	int err;

	napi_enable(&priv->napi);

	err = rtsn_init(priv);
	if (err)
		goto out_napi_off;

	phy_start(ndev->phydev);

	netif_start_queue(ndev);

	rtsn_ptp_init(priv->ptp_priv, RTSN_PTP_REG_LAYOUT_V4H, RTSN_PTP_CLOCK_V4H);

	return 0;

out_napi_off:
	napi_disable(&priv->napi);
	return err;
}

static int rtsn_stop(struct net_device *ndev)
{
	struct rtsn_private *priv = netdev_priv(ndev);

	napi_disable(&priv->napi);
	rtsn_change_mode(priv, OCR_OPC_DISABLE);
	rtsn_deinit(priv);

	return 0;
}

static netdev_tx_t rtsn_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct rtsn_private *priv = netdev_priv(ndev);
	struct rtsn_ext_desc *desc;
	int ret = NETDEV_TX_OK;
	unsigned long flags;
	dma_addr_t dma_addr;
	int entry;

	spin_lock_irqsave(&priv->lock, flags);

	if (priv->cur_tx - priv->dirty_tx > priv->num_tx_ring) {
		netif_stop_subqueue(ndev, 0);
		ret = NETDEV_TX_BUSY;
		goto out;
	}

	if (skb_put_padto(skb, ETH_ZLEN))
		goto out;

	dma_addr = dma_map_single(ndev->dev.parent, skb->data, skb->len, DMA_TO_DEVICE);
	if (dma_mapping_error(ndev->dev.parent, dma_addr))
		goto drop;

	entry = priv->cur_tx % priv->num_tx_ring;
	priv->tx_skb[entry] = skb;
	desc = &priv->tx_ring[entry];
	desc->dptr = cpu_to_le32(dma_addr);
	desc->info_ds = cpu_to_le16(skb->len);
	desc->info1 = cpu_to_le64(skb->len);

	if (skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) {
		skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
		priv->ts_tag++;
		desc->info_ds |= cpu_to_le16(TXC);
		desc->info = priv->ts_tag;
	}

	skb_tx_timestamp(skb);
	dma_wmb();

	desc->die_dt = DT_FSINGLE | D_DIE;
	priv->cur_tx++;

	/* Start xmit */
	rtsn_write(BIT(TX_CHAIN_IDX), priv->addr + TRCR0);

out:
	spin_unlock_irqrestore(&priv->lock, flags);
	return ret;
drop:
	dev_kfree_skb_any(skb);
	goto out;
}

static struct net_device_stats *rtsn_get_stats(struct net_device *ndev)
{
	return &ndev->stats;
}

static int rtsn_hwstamp_get(struct net_device *ndev, struct ifreq *req)
{
	struct rtsn_private *priv = netdev_priv(ndev);
	struct rtsn_ptp_private *ptp_priv = priv->ptp_priv;
	struct hwtstamp_config config;

	config.flags = 0;
	config.tx_type = ptp_priv->tstamp_tx_ctrl ? HWTSTAMP_TX_ON : HWTSTAMP_TX_OFF;
	switch (ptp_priv->tstamp_rx_ctrl & RTSN_RXTSTAMP_TYPE) {
	case RTSN_RXTSTAMP_TYPE_V2_L2_EVENT:
		config.rx_filter = HWTSTAMP_FILTER_PTP_V2_L2_EVENT;
		break;
	case RTSN_RXTSTAMP_TYPE_ALL:
		config.rx_filter = HWTSTAMP_FILTER_ALL;
		break;
	default:
		config.rx_filter = HWTSTAMP_FILTER_NONE;
	}

	return copy_to_user(req->ifr_data, &config, sizeof(config)) ? -EFAULT : 0;
}

static int rtsn_hwstamp_set(struct net_device *ndev, struct ifreq *req)
{
	struct rtsn_private *priv = netdev_priv(ndev);
	struct rtsn_ptp_private *ptp_priv = priv->ptp_priv;
	struct hwtstamp_config config;
	u32 tstamp_rx_ctrl = RTSN_RXTSTAMP_ENABLED;
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
		tstamp_tx_ctrl = RTSN_TXTSTAMP_ENABLED;
		break;
	default:
		return -ERANGE;
	}

	switch (config.rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		tstamp_rx_ctrl = 0;
		break;
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
		tstamp_rx_ctrl |= RTSN_RXTSTAMP_TYPE_V2_L2_EVENT;
		break;
	default:
		config.rx_filter = HWTSTAMP_FILTER_ALL;
		tstamp_rx_ctrl |= RTSN_RXTSTAMP_TYPE_ALL;
	}

	ptp_priv->tstamp_tx_ctrl = tstamp_tx_ctrl;
	ptp_priv->tstamp_rx_ctrl = tstamp_rx_ctrl;

	return copy_to_user(req->ifr_data, &config, sizeof(config)) ? -EFAULT : 0;
}

static int rtsn_do_ioctl(struct net_device *ndev, struct ifreq *req, int cmd)
{
	if (!netif_running(ndev))
		return -EINVAL;

	switch (cmd) {
	case SIOCGHWTSTAMP:
		return rtsn_hwstamp_get(ndev, req);
	case SIOCSHWTSTAMP:
		return rtsn_hwstamp_set(ndev, req);
	default:
		break;
	}

	return 0;
}

static const struct net_device_ops rtsn_netdev_ops = {
	.ndo_open		= rtsn_open,
	.ndo_stop		= rtsn_stop,
	.ndo_start_xmit		= rtsn_start_xmit,
	.ndo_get_stats		= rtsn_get_stats,
	.ndo_do_ioctl		= rtsn_do_ioctl,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= eth_mac_addr,
};

static int rtsn_get_ts_info(struct net_device *ndev, struct ethtool_ts_info *info)
{
	struct rtsn_private *priv = netdev_priv(ndev);

	info->phc_index = ptp_clock_index(priv->ptp_priv->clock);
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

static const struct ethtool_ops rtsn_ethtool_ops = {
	.nway_reset		= phy_ethtool_nway_reset,
	.get_link		= ethtool_op_get_link,
	.get_ts_info		= rtsn_get_ts_info,
	.get_link_ksettings	= phy_ethtool_get_link_ksettings,
	.set_link_ksettings	= phy_ethtool_set_link_ksettings,
};

static const struct of_device_id rtsn_match_table[] = {
	{.compatible = "renesas,ethertsn-r8a779g0", },
	{ }
};

MODULE_DEVICE_TABLE(of, rtsn_match_table);

static int rtsn_probe(struct platform_device *pdev)
{
	struct resource *res, *res_ptp;
	struct rtsn_private *priv;
	struct net_device *ndev;
	const u8 *mac_addr;
	u32 chan_idx;
	int ret;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "tsnes");
	res_ptp = platform_get_resource_byname(pdev, IORESOURCE_MEM, "gptp");
	if (!res || !res_ptp) {
		dev_err(&pdev->dev, "invalid resource\n");
		return -EINVAL;
	}

	ndev = alloc_etherdev_mqs(sizeof(struct rtsn_private), TX_NUM_CHAINS, RX_NUM_CHAINS);
	if (!ndev)
		return -ENOMEM;

	SET_NETDEV_DEV(ndev, &pdev->dev);
	ether_setup(ndev);

	priv = netdev_priv(ndev);
	priv->ndev = ndev;
	priv->ptp_priv = rtsn_ptp_alloc(pdev);

	priv->addr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(priv->addr))
		return PTR_ERR(priv->addr);

	priv->ptp_priv->addr = devm_ioremap_resource(&pdev->dev, res_ptp);
	if (IS_ERR(priv->ptp_priv->addr))
		return PTR_ERR(priv->ptp_priv->addr);

	priv->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(priv->clk))
		return -PTR_ERR(priv->clk);

	priv->rst = devm_reset_control_get(&pdev->dev, NULL);
	if (IS_ERR(priv->rst))
		return -PTR_ERR(priv->rst);

	pm_runtime_enable(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);

	spin_lock_init(&priv->lock);
	ndev->features = NETIF_F_RXCSUM;
	ndev->hw_features = NETIF_F_RXCSUM;
	ndev->base_addr = res->start;
	ndev->netdev_ops = &rtsn_netdev_ops;
	ndev->ethtool_ops = &rtsn_ethtool_ops;
	if (!of_property_read_u32(pdev->dev.of_node, "channel-index", &chan_idx))
		snprintf(ndev->name, IFNAMSIZ, "tsn%d", chan_idx);

	netif_napi_add(ndev, &priv->napi, rtsn_poll, 64);

	mac_addr = of_get_mac_address(pdev->dev.of_node);
	rtsn_read_mac_address(ndev, mac_addr);
	if (!is_valid_ether_addr(ndev->dev_addr))
		eth_hw_addr_random(ndev);

	ret = register_netdev(ndev);
	if (ret)
		goto out_reg_ndev;

	platform_set_drvdata(pdev, priv);
	priv->pdev = pdev;

	dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));

	device_set_wakeup_capable(&pdev->dev, 1);

	netdev_info(ndev, "MAC address %pMn", ndev->dev_addr);

	return 0;

out_reg_ndev:
	unregister_netdev(ndev);
	netif_napi_del(&priv->napi);
	free_netdev(ndev);
	return ret;
}

static int rtsn_remove(struct platform_device *pdev)
{
	struct rtsn_private *priv = platform_get_drvdata(pdev);

	rtsn_change_mode(priv, OCR_OPC_DISABLE);
	netif_napi_del(&priv->napi);
	unregister_netdev(priv->ndev);
	free_netdev(priv->ndev);

	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_driver rtsn_driver = {
	.probe		= rtsn_probe,
	.remove		= rtsn_remove,
	.driver	= {
		.name	= "rtsn",
		.of_match_table	= rtsn_match_table,
	}
};
module_platform_driver(rtsn_driver);
MODULE_AUTHOR("Phong Hoang <phong.hoang.wz@renesas.com>");
MODULE_DESCRIPTION("Renesas Ethernet-TSN device driver");
MODULE_LICENSE("GPL v2");
