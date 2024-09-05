// SPDX-License-Identifier: GPL-2.0
/* Renesas Ethernet Switch3 device driver
 * The drivers is based on Ethernet Switch2 driver
 *
 * Copyright (C) 2024 Renesas Electronics Corporation
 */

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/if_vlan.h>
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
#include <linux/reset.h>

#include "rswitch3.h"

static void *debug_addr;
static inline u32 rs_read32(void *addr)
{
	return ioread32(addr);
}

static inline void rs_write32(u32 data, void *addr)
{
	iowrite32(data, addr);
}

static int rswitch_reg_wait(void __iomem *addr, u32 offs, u32 mask, u32 expected)
{
	int i;

	for (i = 0; i < RSWITCH_TIMEOUT_MS; i++) {
		if ((rs_read32(addr + offs) & mask) == expected)
			return 0;

		mdelay(1);
	}

	return -ETIMEDOUT;
}

static u32 rswitch_etha_offs(int index)
{
	return RSWITCH_ETHA_OFFSET + index * RSWITCH_ETHA_SIZE;
}

static u32 rswitch_etha_read(struct rswitch_etha *etha, enum rswitch_reg reg)
{
	return rs_read32(etha->addr + reg);
}

static void rswitch_etha_write(struct rswitch_etha *etha, u32 data, enum rswitch_reg reg)
{
	rs_write32(data, etha->addr + reg);
}

static void rswitch_etha_modify(struct rswitch_etha *etha, enum rswitch_reg reg,
				u32 clear, u32 set)
{
	rswitch_etha_write(etha, (rswitch_etha_read(etha, reg) & ~clear) | set, reg);
}

static void rswitch_modify(void __iomem *addr, enum rswitch_reg reg, u32 clear, u32 set)
{
	rs_write32((rs_read32(addr + reg) & ~clear) | set, addr + reg);
}

static bool __maybe_unused rswitch_is_any_data_irq(struct rswitch_private *priv, u32 *dis, bool tx)
{
	int i;
	u32 *mask = tx ? priv->gwca.tx_irq_bits : priv->gwca.rx_irq_bits;

	for (i = 0; i < RSWITCH_NUM_IRQ_REGS; i++) {
		if (dis[i] & mask[i])
			return true;
	}

	return false;
}

static void rswitch_get_data_irq_status(struct rswitch_private *priv, u32 *dis)
{
	int i;

	for (i = 0; i < RSWITCH_NUM_IRQ_REGS; i++)
		dis[i] = rs_read32(priv->addr + GWDIS0 + i * 0x10);
}

static void rswitch_enadis_data_irq(struct rswitch_private *priv, int index, bool enable)
{
	u32 offs = (enable ? GWDIE0 : GWDID0) + (index / 32) * 0x10;
	u32 tmp = 0;

	/* For VPF? */
	if (enable)
		tmp = rs_read32(priv->addr + offs);

	rs_write32(BIT(index % 32) | tmp, priv->addr + offs);
}

static void rswitch_ack_data_irq(struct rswitch_private *priv, int index)
{
	u32 offs = GWDIS0 + (index / 32) * 0x10;

	rs_write32(BIT(index % 32), priv->addr + offs);
}

static bool rswitch_is_chain_rxed(struct rswitch_gwca_chain *c, u8 unexpected)
{
	int entry;
	struct rswitch_ext_ts_desc *desc;

	entry = c->dirty % c->num_ring;
	desc = &c->rx_ring[entry];

	if ((desc->die_dt & DT_MASK) != unexpected)
		return true;

	return false;
}


static bool rswitch_rx(struct net_device *ndev, int *quota)
{
	struct rswitch_device *rdev = netdev_priv(ndev);
	struct rswitch_gwca_chain *c = rdev->rx_chain;
	int boguscnt = c->dirty + c->num_ring - c->cur;
	int entry = c->cur % c->num_ring;
	struct rswitch_ext_ts_desc *desc = &c->rx_ring[entry];
	int limit;
	u16 pkt_len;
	struct sk_buff *skb;
	dma_addr_t dma_addr;
	u32 get_ts;

	boguscnt = min(boguscnt, *quota);
	limit = boguscnt;

	while ((desc->die_dt & DT_MASK) != DT_FEMPTY) {
		dma_rmb();
		pkt_len = le16_to_cpu(desc->info_ds) & RX_DS;
		if (--boguscnt < 0)
			break;
		dma_addr = le32_to_cpu(desc->dptrl) | ((__le64)le32_to_cpu(desc->dptrh) << 32);
		dma_unmap_single(ndev->dev.parent, dma_addr,
				 RSWITCH_RX_BUF_SIZE - NET_SKB_PAD - NET_IP_ALIGN,
				 DMA_FROM_DEVICE);

		if ((desc->die_dt & DT_MASK) == DT_FSTART) {
			if (c->multi_desc) {
				/* Got the error so freed the multi-descriptor skb */
				dev_kfree_skb_any(c->skb_multi);
			}
			c->skb_multi = build_skb(c->rx_bufs[entry], RSWITCH_RX_BUF_SIZE);
			if (!c->skb_multi) {
				c->multi_desc = false;
				goto next;
			}
			skb_checksum_none_assert(c->skb_multi);
			skb_reserve(c->skb_multi, NET_SKB_PAD + NET_IP_ALIGN);
			skb_put(c->skb_multi, pkt_len);
			c->multi_desc = true;
			c->total_len = pkt_len;

			goto next;
		} else if ((desc->die_dt & DT_MASK) == DT_FMID) {
			if (!c->multi_desc)
				goto next;

			skb_add_rx_frag(c->skb_multi,
					skb_shinfo(c->skb_multi)->nr_frags,
					virt_to_page(c->rx_bufs[entry]),
					offset_in_page(c->rx_bufs[entry]) + NET_SKB_PAD + NET_IP_ALIGN,
					pkt_len, RSWITCH_RX_BUF_SIZE);
			c->total_len += pkt_len;
			goto next;
		} else if ((desc->die_dt & DT_MASK) == DT_FEND) {
			if (!c->multi_desc)
				goto next;

			skb_add_rx_frag(c->skb_multi,
					skb_shinfo(c->skb_multi)->nr_frags,
					virt_to_page(c->rx_bufs[entry]),
					offset_in_page(c->rx_bufs[entry]) + NET_SKB_PAD + NET_IP_ALIGN,
					pkt_len, RSWITCH_RX_BUF_SIZE);

			skb = c->skb_multi;
			pkt_len += c->total_len;
			c->skb_multi = NULL;
			c->multi_desc = false;
		}  else {
			/* F_SINGLE */
			if (c->multi_desc) {
				/* Got the error so freed the multi-descriptor skb */
				dev_kfree_skb_any(c->skb_multi);
				c->skb_multi = NULL;
				c->multi_desc = false;
			}
			skb = build_skb(c->rx_bufs[entry], RSWITCH_RX_BUF_SIZE);
			if (!skb)
				goto next;
			skb_checksum_none_assert(skb);
			skb_reserve(skb, NET_SKB_PAD + NET_IP_ALIGN);
			skb_put(skb, pkt_len);
		}

		get_ts = rdev->priv->ptp_priv->tstamp_rx_ctrl & RCAR_GEN4_RXTSTAMP_TYPE_V2_L2_EVENT;
		if (get_ts) {
			struct skb_shared_hwtstamps *shhwtstamps;
			struct timespec64 ts;

			shhwtstamps = skb_hwtstamps(skb);
			memset(shhwtstamps, 0, sizeof(*shhwtstamps));
			ts.tv_sec = (u64)le32_to_cpu(desc->ts_sec);
			ts.tv_nsec = le32_to_cpu(desc->ts_nsec & 0x3FFFFFFF);
			shhwtstamps->hwtstamp = timespec64_to_ktime(ts);
		}
		skb->protocol = eth_type_trans(skb, ndev);
		napi_gro_receive(&rdev->napi, skb);
		rdev->ndev->stats.rx_packets++;
		rdev->ndev->stats.rx_bytes += pkt_len;

next:
		c->rx_bufs[entry] = NULL;
		entry = (++c->cur) % c->num_ring;
		desc = &c->rx_ring[entry];
	}

	/* Refill the RX ring buffers */
	for (; c->cur - c->dirty > 0; c->dirty++) {
		entry = c->dirty % c->num_ring;
		desc = &c->rx_ring[entry];
		desc->info_ds = cpu_to_le16(MAX_DESC_SZ);

		if (!c->rx_bufs[entry]) {
			c->rx_bufs[entry] = netdev_alloc_frag(RSWITCH_RX_BUF_SIZE);
			if (!c->rx_bufs[entry])
				break;	/* Better luch next round */

			dma_addr = dma_map_single(ndev->dev.parent,
					c->rx_bufs[entry] + NET_SKB_PAD + NET_IP_ALIGN,
					RSWITCH_RX_BUF_SIZE - NET_SKB_PAD - NET_IP_ALIGN,
					DMA_FROM_DEVICE);

			if (dma_mapping_error(ndev->dev.parent, dma_addr))
				desc->info_ds = cpu_to_le16(0);
			desc->dptrl = cpu_to_le32(lower_32_bits(dma_addr));
			desc->dptrh = cpu_to_le32(upper_32_bits(dma_addr));
		}
		dma_wmb();
		desc->die_dt = DT_FEMPTY | DIE;
	}

	*quota -= limit - (++boguscnt);

	return boguscnt <= 0;
}

static void rswitch_get_timestamp(struct rswitch_private *priv,
				  struct timespec64 *ts)
{
	struct rcar_gen4_ptp_private *ptp_priv = priv->ptp_priv;

	ptp_priv->info.gettime64(&ptp_priv->info, ts);
}

static int rswitch_tx_free(struct net_device *ndev, bool free_txed_only)
{
	struct rswitch_device *rdev = netdev_priv(ndev);
	struct rswitch_ext_desc *desc;
	int free_num = 0;
	int entry, size;
	dma_addr_t dma_addr;
	struct rswitch_gwca_chain *c = rdev->tx_chain;
	struct sk_buff *skb;

	for (; c->cur - c->dirty > 0; c->dirty++) {
		entry = c->dirty % c->num_ring;
		desc = &c->tx_ring[entry];
		if (free_txed_only && (desc->die_dt & DT_MASK) != DT_FEMPTY)
			break;

		dma_rmb();
		size = le16_to_cpu(desc->info_ds) & TX_DS;
		skb = c->skb[entry];
		if (skb) {
			if (skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) {
				struct skb_shared_hwtstamps shhwtstamps;
				struct timespec64 ts;

				rswitch_get_timestamp(rdev->priv, &ts);
				memset(&shhwtstamps, 0, sizeof(shhwtstamps));
				shhwtstamps.hwtstamp = timespec64_to_ktime(ts);
				skb_tstamp_tx(skb, &shhwtstamps);
			}
			dma_addr = le32_to_cpu(desc->dptrl) |
				   ((__le64)le32_to_cpu(desc->dptrh) << 32);
			dma_unmap_single(ndev->dev.parent, dma_addr,
					size, DMA_TO_DEVICE);
			dev_kfree_skb_any(c->skb[entry]);
			c->skb[entry] = NULL;
			rdev->ndev->stats.tx_packets++;
			free_num++;
		}
		desc->die_dt = DT_EEMPTY;
		rdev->ndev->stats.tx_bytes += size;
	}

	return free_num;
}

static int rswitch_poll(struct napi_struct *napi, int budget)
{
	struct net_device *ndev = napi->dev;
	struct rswitch_device *rdev = netdev_priv(ndev);
	struct rswitch_private *priv = rdev->priv;
	unsigned long flags;
	int quota = budget;

retry:
	rswitch_tx_free(ndev, true);

	if (rswitch_rx(ndev, &quota))
		goto out;
	else if (rswitch_is_chain_rxed(rdev->rx_chain, DT_FEMPTY))
		goto retry;

	netif_wake_subqueue(ndev, 0);

	if (napi_complete_done(napi, budget - quota)) {
		spin_lock_irqsave(&priv->lock, flags);
		/* Re-enable RX/TX interrupts */
		rswitch_enadis_data_irq(priv, rdev->tx_chain->index, true);
		rswitch_enadis_data_irq(priv, rdev->rx_chain->index, true);
		spin_unlock_irqrestore(&priv->lock, flags);
	}

	__iowmb();

out:
	return budget - quota;
}

static bool rswitch_agent_clock_is_enabled(void __iomem *base_addr, int port)
{
	u32 val = rs_read32(base_addr + RCEC);

	/* fixed: hardcoded GWCA1 settings BIT(14) for now */
	if (val & RCEC_RCE)
		return (val & BIT(14)) ? true : false;
	else
		return false;
}

static void rswitch_agent_clock_ctrl(void __iomem *base_addr, int port, int enable)
{
	u32 val;

	/* fixed: hardcoded GWCA1 settings BIT(14) for now */
	if (enable) {
		val = rs_read32(base_addr + RCEC);
		rs_write32(val | RCEC_RCE | BIT(14), base_addr + RCEC);
	} else {
		val = rs_read32(base_addr + RCDC);
		rs_write32(val | BIT(14), base_addr + RCDC);
	}
}

static int rswitch_etha_change_mode(struct rswitch_etha *etha,
				    enum rswitch_etha_mode mode)
{
	void __iomem *base_addr;
	int ret;

	base_addr = etha->addr - rswitch_etha_offs(etha->index);

	/* Enable clock */
	if (!rswitch_agent_clock_is_enabled(base_addr, etha->index))
		rswitch_agent_clock_ctrl(base_addr, etha->index, 1);

	rs_write32(mode, etha->addr + EAMC);

	ret = rswitch_reg_wait(etha->addr, EAMS, EAMS_OPS_MASK, mode);

	/* Disable clock */
	if (mode == EAMC_OPC_DISABLE)
		rswitch_agent_clock_ctrl(base_addr, etha->index, 0);

	return ret;
}

static void rswitch_etha_read_mac_address(struct rswitch_etha *etha)
{
	u8 *mac = &etha->mac_addr[0];
	u32 mrmac0 = rswitch_etha_read(etha, MRMAC0);
	u32 mrmac1 = rswitch_etha_read(etha, MRMAC1);

	mac[0] = (mrmac0 >>  8) & 0xFF;
	mac[1] = (mrmac0 >>  0) & 0xFF;
	mac[2] = (mrmac1 >> 24) & 0xFF;
	mac[3] = (mrmac1 >> 16) & 0xFF;
	mac[4] = (mrmac1 >>  8) & 0xFF;
	mac[5] = (mrmac1 >>  0) & 0xFF;
}

static bool __maybe_unused rswitch_etha_wait_link_verification(struct rswitch_etha *etha)
{
	/* Request Link Verification */
	rswitch_etha_write(etha, MLVC_PLV, MLVC);
	return rswitch_reg_wait(etha->addr, MLVC, MLVC_PLV, 0);
}

static void rswitch_rmac_setting(struct rswitch_etha *etha, const u8 *mac)
{
	u32 val;

	/* FIXME */
	/* Set xMII type */
	switch (etha->speed) {
	case 10:
		val = MPIC_LSC_10M;
		rswitch_etha_write(etha, MPIC_PIS_GMII | val, MPIC);
		break;
	case 100:
		val = MPIC_LSC_100M;
		rswitch_etha_write(etha, MPIC_PIS_GMII | val, MPIC);
		break;
	case 1000:
		val = MPIC_LSC_1G;
		rswitch_etha_write(etha, MPIC_PIS_GMII | val, MPIC);
		break;
	case 2500:
		val = MPIC_LSC_2_5G;
		rswitch_etha_write(etha, MPIC_PIS_XGMII | val, MPIC);
		break;
	case 5000:
		val = MPIC_LSC_5G;
		rswitch_etha_write(etha, MPIC_PIS_XGMII | val, MPIC);
		break;
	case 10000:
		val = MPIC_LSC_10G;
		rswitch_etha_write(etha, MPIC_PIS_XGMII | val, MPIC);
		break;
	default:
		return;
	}

#if 0
	/* Set Interrupt enable */
	rswitch_etha_write(etha, 0, MEIE);
	rswitch_etha_write(etha, 0, MMIE0);
	rswitch_etha_write(etha, 0, MMIE1);
	rswitch_etha_write(etha, 0, MMIE2);
	rswitch_etha_write(etha, 0, MMIE2);
	/* Set Tx function */
	rswitch_etha_write(etha, 0, MTFFC);
	rswitch_etha_write(etha, 0, MTPFC);
	rswitch_etha_write(etha, 0, MTPFC2);
	rswitch_etha_write(etha, 0, MTPFC30);
	rswitch_etha_write(etha, 0, MTATC0);
	/* Set Rx function */
	rswitch_etha_write(etha, 0, MRGC);
	rswitch_etha_write(etha, 0x00070007, MRAFC);
	rswitch_etha_write(etha, 0, MRFSCE);
	rswitch_etha_write(etha, 0, MRFSCP);
	rswitch_etha_write(etha, 0, MTRC);

	/* Set Address Filtering function */
	/* Set XGMII function */
	/* Set Half Duplex function */
	/* Set PLCA function */
#endif
}

static void rswitch_etha_enable_mii(struct rswitch_etha *etha)
{
	rswitch_etha_modify(etha, MPIC, MPIC_PSMCS_MASK | MPIC_PSMHT_MASK,
			    MPIC_PSMCS(etha->psmcs) | MPIC_PSMHT(0x06));
	rswitch_etha_modify(etha, MPSM, 0, MPSM_MFF_C45);
}

static int rswitch_etha_hw_init(struct rswitch_etha *etha, const u8 *mac)
{
	int err;

	/* Change to CONFIG Mode */
	err = rswitch_etha_change_mode(etha, EAMC_OPC_DISABLE);
	if (err < 0)
		return err;
	err = rswitch_etha_change_mode(etha, EAMC_OPC_CONFIG);
	if (err < 0)
		return err;

	rs_write32(EAVCC_VEM_SC_TAG, etha->addr + EAVCC);

	rswitch_rmac_setting(etha, mac);
	rswitch_etha_enable_mii(etha);

	/* Change to OPERATION Mode */
	err = rswitch_etha_change_mode(etha, EAMC_OPC_OPERATION);
	if (err < 0)
		return err;

	return 0;

	/* Link Verification
	 * return rswitch_etha_wait_link_verification(etha);
	 *
	 */
}

void rswitch_serdes_write32(void __iomem *addr, u32 offs,  u32 bank, u32 data)
{
	iowrite32(bank, addr + RSWITCH_SERDES_BANK_SELECT);
	iowrite32(data, addr + offs);
}

u32 rswitch_serdes_read32(void __iomem *addr, u32 offs,  u32 bank)
{
	iowrite32(bank, addr + RSWITCH_SERDES_BANK_SELECT);
	return ioread32(addr + offs);
}

static int rswitch_serdes_reg_wait(void __iomem *addr, u32 offs, u32 bank, u32 mask, u32 expected)
{
	int i;

	iowrite32(bank, addr + RSWITCH_SERDES_BANK_SELECT);
	mdelay(1);

	for (i = 0; i < RSWITCH_TIMEOUT_MS; i++) {
		if ((ioread32(addr + offs) & mask) == expected)
			return 0;
		mdelay(1);
	}

	return -ETIMEDOUT;
}

static int rswitch_serdes_common_init_ram(struct rswitch_etha *etha)
{
	void __iomem *common_addr = etha->serdes_addr - etha->index * RSWITCH_SERDES_OFFSET;
	int ret, i;

	for (i = 0; i < RSWITCH_SERDES_NUM; i++) {
		ret = rswitch_serdes_reg_wait(common_addr + i * RSWITCH_SERDES_OFFSET,
					      VR_XS_PMA_MP_12G_16G_25G_SRAM, BANK_180,
					      BIT(0), 0x01);
		if (ret)
			return ret;
	}

	rswitch_serdes_write32(common_addr, VR_XS_PMA_MP_12G_16G_25G_SRAM, BANK_180, 0x03);

	return 0;
}

static void rswitch_serdes_common_setting(struct rswitch_etha *etha)
{
	void __iomem *addr = etha->serdes_addr - etha->index * RSWITCH_SERDES_OFFSET;

	/* Set combination mode */
	rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_16G_25G_REF_CLK_CTRL, BANK_180, 0xd7);
	rswitch_serdes_write32(addr, VR_XS_PMA_MP_10G_MPLLA_CTRL2, BANK_180, 0xc200);
	rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_16G_MPLLA_CTRL0, BANK_180, 0x42);
	rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_MPLLA_CTRL1, BANK_180, 0);
	rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_MPLLA_CTRL3, BANK_180, 0x2f);
	rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_16G_MPLLB_CTRL0, BANK_180, 0x60);
	rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_16G_MPLLB_CTRL2, BANK_180, 0x2200);
	rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_MPLLB_CTRL1, BANK_180, 0);
	rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_MPLLB_CTRL3, BANK_180, 0x3d);
}

static int rswitch_serdes_chan_setting(struct rswitch_etha *etha)
{
	void __iomem *addr = etha->serdes_addr;
	int ret;

	/* TODO: Support 10Gbps, SerDes not supported in VPF */
	switch (etha->phy_interface) {
	case PHY_INTERFACE_MODE_SGMII:
		rswitch_serdes_write32(addr, VR_XS_PCS_DIG_CTRL1, BANK_380, 0x2000);
		rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_16G_25G_MPLL_CMN_CTRL,
				       BANK_180, 0x11);
		rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_16G_25G_VCO_CAL_LD0, BANK_180, 0x540);
		rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_VCO_CAL_REF0, BANK_180, 0x15);
		rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_16G_25G_RX_GENCTRL1, BANK_180, 0x100);
		rswitch_serdes_write32(addr, VR_XS_PMA_CONSUMER_10G_RX_GENCTRL4, BANK_180, 0);
		rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_16G_25G_TX_RATE_CTRL, BANK_180, 0x02);
		rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_16G_25G_RX_RATE_CTRL, BANK_180, 0x03);
		rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_16G_TX_GENCTRL2, BANK_180, 0x100);
		rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_16G_RX_GENCTRL2, BANK_180, 0x100);
		rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_AFE_DFE_EN_CTRL, BANK_180, 0);
		rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_RX_EQ_CTRL0, BANK_180, 0x07);
		rswitch_serdes_write32(addr, VR_XS_PMA_MP_10G_RX_IQ_CTRL0, BANK_180, 0);
		rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_16G_25G_TX_GENCTRL1, BANK_180, 0x310);
		rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_16G_TX_GENCTRL2, BANK_180, 0x101);
		ret = rswitch_serdes_reg_wait(addr, VR_XS_PMA_MP_12G_16G_TX_GENCTRL2,
					      BANK_180, BIT(0), 0);
		if (ret)
			return ret;

		rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_16G_RX_GENCTRL2, BANK_180, 0x101);
		ret = rswitch_serdes_reg_wait(addr, VR_XS_PMA_MP_12G_16G_RX_GENCTRL2,
					      BANK_180, BIT(0), 0);
		if (ret)
			return ret;

		rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_16G_25G_TX_GENCTRL1,
				       BANK_180, 0x1310);
		rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_16G_25G_TX_EQ_CTRL0,
				       BANK_180, 0x1800);
		rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_16G_25G_TX_EQ_CTRL1, BANK_180, 0);
		rswitch_serdes_write32(addr, SR_XS_PCS_CTRL2, BANK_300, 0x01);
		rswitch_serdes_write32(addr, VR_XS_PCS_DIG_CTRL1, BANK_380, 0x2100);
		ret = rswitch_serdes_reg_wait(addr, VR_XS_PCS_DIG_CTRL1, BANK_380, BIT(8), 0);
		if (ret)
			return ret;

		break;
	case PHY_INTERFACE_MODE_USXGMII:
	case PHY_INTERFACE_MODE_5GBASER:
		rswitch_serdes_write32(addr, SR_XS_PCS_CTRL2, BANK_300, 0x0);
		rswitch_serdes_write32(addr, VR_XS_PCS_DEBUG_CTRL, BANK_380, 0x50);
		rswitch_serdes_write32(addr, VR_XS_PCS_DIG_CTRL1, BANK_380, 0x2200);
		rswitch_serdes_write32(addr, VR_XS_PCS_KR_CTRL, BANK_380, 0x400);
		rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_16G_25G_MPLL_CMN_CTRL, BANK_180, 0x1);
		rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_16G_25G_VCO_CAL_LD0, BANK_180, 0x56a);
		rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_VCO_CAL_REF0, BANK_180, 0x15);
		rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_16G_25G_RX_GENCTRL1,
				       BANK_180, 0x1100);
		rswitch_serdes_write32(addr, VR_XS_PMA_CONSUMER_10G_RX_GENCTRL4, BANK_180, 1);
		rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_16G_25G_TX_RATE_CTRL, BANK_180, 0x01);
		rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_16G_25G_RX_RATE_CTRL, BANK_180, 0x01);
		rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_16G_TX_GENCTRL2, BANK_180, 0x300);
		rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_16G_RX_GENCTRL2, BANK_180, 0x300);
		rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_AFE_DFE_EN_CTRL, BANK_180, 0);
		rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_RX_EQ_CTRL0, BANK_180, 0x4);
		rswitch_serdes_write32(addr, VR_XS_PMA_MP_10G_RX_IQ_CTRL0, BANK_180, 0);
		rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_16G_25G_TX_GENCTRL1, BANK_180, 0x310);
		rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_16G_TX_GENCTRL2, BANK_180, 0x0301);
		ret = rswitch_serdes_reg_wait(addr, VR_XS_PMA_MP_12G_16G_TX_GENCTRL2,
					      BANK_180, BIT(0), 0);
		if (ret)
			return ret;
		rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_16G_RX_GENCTRL2, BANK_180, 0x301);
		ret = rswitch_serdes_reg_wait(addr, VR_XS_PMA_MP_12G_16G_RX_GENCTRL2,
					      BANK_180, BIT(0), 0);
		if (ret)
			return ret;
		rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_16G_25G_TX_GENCTRL1,
				       BANK_180, 0x1310);
		rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_16G_25G_TX_EQ_CTRL0,
				       BANK_180, 0x1800);
		rswitch_serdes_write32(addr, VR_XS_PMA_MP_12G_16G_25G_TX_EQ_CTRL1, BANK_180, 0);
		rswitch_serdes_write32(addr, VR_XS_PCS_DIG_CTRL1, BANK_380, 0x2300);
		ret = rswitch_serdes_reg_wait(addr, VR_XS_PCS_DIG_CTRL1, BANK_380, BIT(8), 0);
		if (ret)
			return ret;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int rswitch_serdes_set_chan_speed(struct rswitch_etha *etha)
{
	void __iomem *addr = etha->serdes_addr;

	/* TODO: Support 10Gbps, SerDes not suported in VPF */
	switch (etha->phy_interface) {
	case PHY_INTERFACE_MODE_SGMII:
		if (etha->speed == 1000)
			rswitch_serdes_write32(addr, SR_MII_CTRL, BANK_1F00, 0x140);
		else if (etha->speed == 100)
			rswitch_serdes_write32(addr, SR_MII_CTRL, BANK_1F00, 0x2100);

		break;
	case PHY_INTERFACE_MODE_USXGMII:
		/* USXGMII - 2.5Gbps */
		rswitch_serdes_write32(addr, SR_MII_CTRL, BANK_1F00, 0x120);

		break;
	case PHY_INTERFACE_MODE_5GBASER:
		rswitch_serdes_write32(addr, SR_MII_CTRL, BANK_1F00, 0x2120);

		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int rswitch_serdes_monitor_linkup(struct rswitch_etha *etha)
{
	int ret, retry = 5;
	u32 val;

retry:
	ret = rswitch_serdes_reg_wait(etha->serdes_addr,
				      SR_XS_PCS_STS1, BANK_300, BIT(2), BIT(2));
	if (ret) {
		pr_debug("\n%s: SerDes Link up failed, restart linkup\n", __func__);

		if (retry < 0)
			return -ETIMEDOUT;

		retry--;

		val = rswitch_serdes_read32(etha->serdes_addr,
					    VR_XS_PMA_MP_12G_16G_25G_RX_GENCTRL1, BANK_180);
		rswitch_serdes_write32(etha->serdes_addr, VR_XS_PMA_MP_12G_16G_25G_RX_GENCTRL1,
				       BANK_180, val | BIT(4));
		udelay(20);
		rswitch_serdes_write32(etha->serdes_addr, VR_XS_PMA_MP_12G_16G_25G_RX_GENCTRL1,
				       BANK_180, val & (~BIT(4)));

		goto retry;
	}

	return 0;
}

static int rswitch_serdes_common_init(struct rswitch_etha *etha)
{
	void __iomem *common_addr = etha->serdes_addr - etha->index * RSWITCH_SERDES_OFFSET;
	int ret, i;

	/* Initialize SRAM */
	ret = rswitch_serdes_common_init_ram(etha);
	if (ret)
		return ret;

	for (i = 0; i < RSWITCH_SERDES_NUM; i++) {
		ret = rswitch_serdes_reg_wait(common_addr + i * RSWITCH_SERDES_OFFSET,
					      SR_XS_PCS_CTRL1, BANK_300, BIT(15), 0);
		if (ret)
			return ret;
	}

	for (i = 0; i < RSWITCH_SERDES_NUM; i++)
		rswitch_serdes_write32(common_addr + i * RSWITCH_SERDES_OFFSET,
				       0x03d4, BANK_380, 0x443);

	/* Set common setting */
	rswitch_serdes_common_setting(etha);

	for (i = 0; i < RSWITCH_SERDES_NUM; i++)
		rswitch_serdes_write32(common_addr + i * RSWITCH_SERDES_OFFSET,
				       VR_XS_PCS_SFTY_DISABLE, BANK_380, 0x01);

	/* Assert softreset for PHY */
	rswitch_serdes_write32(common_addr, VR_XS_PCS_DIG_CTRL1, BANK_380, 0x8000);

	/* Initialize SRAM */
	ret = rswitch_serdes_common_init_ram(etha);
	if (ret)
		return ret;

	return rswitch_serdes_reg_wait(common_addr, VR_XS_PCS_DIG_CTRL1, BANK_380, BIT(15), 0);
}

static int rswitch_serdes_chan_init(struct rswitch_etha *etha)
{
	int ret;
	u32 val;

	/* Set channel settings*/
	ret = rswitch_serdes_chan_setting(etha);
	if (ret)
		return ret;

	/* Set speed (bps) */
	ret = rswitch_serdes_set_chan_speed(etha);
	if (ret)
		return ret;

	rswitch_serdes_write32(etha->serdes_addr, VR_XS_PCS_SFTY_UE_INTRO, BANK_380, 0);
	rswitch_serdes_write32(etha->serdes_addr, VR_XS_PCS_SFTY_DISABLE, BANK_380, 0);

	val = rswitch_serdes_read32(etha->serdes_addr, VR_XS_PMA_MP_12G_16G_25G_TX_GENCTRL0,
				    BANK_180);
	rswitch_serdes_write32(etha->serdes_addr, VR_XS_PMA_MP_12G_16G_25G_TX_GENCTRL0,
			       BANK_180, val | BIT(8));

	ret = rswitch_serdes_reg_wait(etha->serdes_addr, VR_XS_PMA_MP_12G_16G_25G_TX_STS,
				      BANK_180, BIT(0), 1);
	if (ret)
		return ret;

	rswitch_serdes_write32(etha->serdes_addr, VR_XS_PMA_MP_12G_16G_25G_TX_GENCTRL0,
			       BANK_180, val &= ~BIT(8));

	ret = rswitch_serdes_reg_wait(etha->serdes_addr, VR_XS_PMA_MP_12G_16G_25G_TX_STS,
				      BANK_180, BIT(0), 0);
	if (ret)
		return ret;

	val = rswitch_serdes_read32(etha->serdes_addr, VR_XS_PMA_MP_12G_16G_25G_RX_GENCTRL1,
				    BANK_180);
	rswitch_serdes_write32(etha->serdes_addr, VR_XS_PMA_MP_12G_16G_25G_RX_GENCTRL1,
			       BANK_180, val | BIT(4));

	ret = rswitch_serdes_reg_wait(etha->serdes_addr, VR_XS_PMA_MP_12G_16G_25G_RX_STS,
				      BANK_180, BIT(0), 1);
	if (ret)
		return ret;

	rswitch_serdes_write32(etha->serdes_addr, VR_XS_PMA_MP_12G_16G_25G_RX_GENCTRL1,
			       BANK_180, val &= ~BIT(4));

	ret = rswitch_serdes_reg_wait(etha->serdes_addr, VR_XS_PMA_MP_12G_16G_25G_RX_STS,
				      BANK_180, BIT(0), 0);
	if (ret)
		return ret;

	/* Check Link up restart */
	return rswitch_serdes_monitor_linkup(etha);
}

static int rswitch_etha_set_access_c45(struct rswitch_etha *etha, bool read,
				      int phyad, int devad, int regad, int data)
{
	int pop = read ? MDIO_READ_C45 : MDIO_WRITE_C45;
	u32 val;
	int ret;

	/* No match device */
	if (devad == 0xffffffff)
		return 0;

	/* Clear completion flags */
	writel(MMIS1_CLEAR_FLAGS, etha->addr + MMIS1);

	/* Submit address to PHY (MDIO_ADDR_C45 << 13) */
	val = MPSM_PSME | MPSM_MFF_C45;
	rs_write32((regad << 16) | (devad << 8) | (phyad << 3) | val, etha->addr + MPSM);

	ret = rswitch_reg_wait(etha->addr, MMIS1, MMIS1_PAACS, MMIS1_PAACS);
	if (ret)
		return ret;

	/* Clear address completion flag */
	rswitch_etha_modify(etha, MMIS1, MMIS1_PAACS, MMIS1_PAACS);

	/* Read/Write PHY register */
	if (read) {
		writel((pop << 13) | (devad << 8) | (phyad << 3) | val, etha->addr + MPSM);

		ret = rswitch_reg_wait(etha->addr, MMIS1, MMIS1_PRACS, MMIS1_PRACS);
		if (ret)
			return ret;

		/* Read data */
		ret = (rs_read32(etha->addr + MPSM) & MPSM_PRD_MASK) >> 16;

		/* Clear read completion flag */
		rswitch_etha_modify(etha, MMIS1, MMIS1_PRACS, MMIS1_PRACS);
	} else {
		rs_write32((data << 16) | (pop << 13) | (devad << 8) | (phyad << 3) | val,
			   etha->addr + MPSM);

		ret = rswitch_reg_wait(etha->addr, MMIS1, MMIS1_PWACS, MMIS1_PWACS);
	}

	return ret;
}

static int __maybe_unused rswitch_etha_set_access_c22(struct rswitch_etha *etha, bool read,
				       int phyad, int regad, int data)
{
	int pop = read ? MDIO_READ_C22 : MDIO_WRITE_C22;
	int ret;
	u32 val;

	val = MPSM_POP(pop) | MPSM_PDA(phyad) | MPSM_PRA(regad) | MPSM_PSME;

	if (!read)
		val |= MPSM_PRD_WRITE(data);

	rs_write32(val, etha->addr + MPSM);

	ret = rswitch_reg_wait(etha->addr, MPSM, MPSM_PSME, 0);
	if (ret)
		return ret;

	return read ? MPSM_PRD_READ(rswitch_etha_read(etha, MPSM)) : 0;
}

static int rswitch_etha_set_access_c22_vpf(struct rswitch_etha *etha, bool read,
					int phyad, int regad, int data)
{
	int pop = read ? MDIO_READ_C22 : MDIO_WRITE_C22;
	int ret;

	rswitch_etha_modify(etha, MPSM, MPSM_POP_MASK, MPSM_POP(pop));
	rswitch_etha_modify(etha, MPSM, MPSM_PDA_MASK, MPSM_PDA(phyad));
	rswitch_etha_modify(etha, MPSM, MPSM_PRA_MASK, MPSM_PRA(regad));

	if (!read)
		rswitch_etha_modify(etha, MPSM, MPSM_PRD_MASK, MPSM_PRD_WRITE(data));

	ret = rswitch_reg_wait(etha->addr, MPSM, MPSM_PSME, 0);
	if (ret)
		return ret;

	return read ? MPSM_PRD_READ(rswitch_etha_read(etha, MPSM)) : 0;
}

static int rswitch_etha_mii_read(struct mii_bus *bus, int addr, int regnum)
{
	struct rswitch_etha *etha = bus->priv;
	struct rswitch_private *priv = container_of(etha, struct rswitch_private, etha[etha->index]);
	int mode, devad, regad;

	mode = regnum & MII_ADDR_C45;
	devad = (regnum >> MII_DEVADDR_C45_SHIFT) & 0x1f;
	regad = regnum & MII_REGADDR_C45_MASK;

	/* Clause 22 */
	if (!mode) {
		if (!priv->vpf_mode)
			return -EOPNOTSUPP;
		else
			return rswitch_etha_set_access_c22_vpf(etha, true, addr, regnum, 0);
	}

	return rswitch_etha_set_access_c45(etha, true, addr, devad, regad, 0);
}

static int rswitch_etha_mii_write(struct mii_bus *bus, int addr, int regnum, u16 val)
{
	struct rswitch_etha *etha = bus->priv;
	struct rswitch_private *priv = container_of(etha, struct rswitch_private, etha[etha->index]);
	int mode, devad, regad;

	mode = regnum & MII_ADDR_C45;
	devad = (regnum >> MII_DEVADDR_C45_SHIFT) & 0x1f;
	regad = regnum & MII_REGADDR_C45_MASK;

	/* Clause 22 */
	if (!mode) {
		if (!priv->vpf_mode)
			return -EOPNOTSUPP;
		else
			return rswitch_etha_set_access_c22_vpf(etha, false, addr, regnum, val);
	}

	return rswitch_etha_set_access_c45(etha, false, addr, devad, regad, val);
}

static int rswitch_etha_mii_reset(struct mii_bus *bus)
{
	/* TODO */
	return 0;
}

/* Use of_node_put() on it when done */
static struct device_node *rswitch_get_phy_node(struct rswitch_device *rdev)
{
	struct device_node *ports, *port, *phy = NULL;
	int err = 0;
	u32 index;

	ports = of_get_child_by_name(rdev->ndev->dev.parent->of_node, "ports");
	if (!ports)
		return NULL;

	for_each_child_of_node(ports, port) {
		err = of_property_read_u32(port, "reg", &index);
		if (err < 0)
			return NULL;
		if (index != rdev->etha->index)
			continue;

		/* The default is SGMII interface */
		err = of_get_phy_mode(port, &rdev->etha->phy_interface);
		if (err < 0)
			rdev->etha->phy_interface = PHY_INTERFACE_MODE_SGMII;

		pr_info("%s PHY interface = %s", __func__, phy_modes(rdev->etha->phy_interface));

		phy = of_parse_phandle(port, "phy-handle", 0);
		if (phy) {
			switch (rdev->etha->phy_interface) {
			case PHY_INTERFACE_MODE_SGMII:
				rdev->etha->speed = 1000;
				break;
			case PHY_INTERFACE_MODE_5GBASER:
				rdev->etha->speed = 2500;
				break;
			case PHY_INTERFACE_MODE_USXGMII:
				rdev->etha->speed = 10000;
				break;
			default:
				break;
			}
		} else {
			if (of_phy_is_fixed_link(port)) {
				struct device_node *fixed_link;

				fixed_link = of_get_child_by_name(port, "fixed-link");
				err = of_property_read_u32(fixed_link, "speed", &rdev->etha->speed);
				if (err)
					break;

				err = of_phy_register_fixed_link(port);
				if (err)
					break;

				phy = of_node_get(port);
			}
		}
	}

	of_node_put(ports);

	return phy;
}

static struct device_node *rswitch_get_port_node(struct rswitch_device *rdev)
{
	struct device_node *ports, *port;
	int err = 0;
	u32 index;

	ports = of_get_child_by_name(rdev->ndev->dev.parent->of_node, "ports");
	if (!ports)
		return NULL;

	for_each_child_of_node(ports, port) {
		err = of_property_read_u32(port, "reg", &index);
		if (err < 0)
			return NULL;
		if (index == rdev->etha->index)
			break;
	}

	of_node_put(ports);

	return port;
}

static int rswitch_mii_register(struct rswitch_device *rdev)
{
	struct mii_bus *mii_bus;
	struct device_node *port;
	int err;

	mii_bus = mdiobus_alloc();
	if (!mii_bus)
		return -ENOMEM;

	mii_bus->name = "rswitch_mii";
	sprintf(mii_bus->id, "etha%d", rdev->etha->index);
	mii_bus->priv = rdev->etha;
	mii_bus->read = rswitch_etha_mii_read;
	mii_bus->write = rswitch_etha_mii_write;
	mii_bus->reset = rswitch_etha_mii_reset;
	mii_bus->parent = &rdev->ndev->dev;

	port = rswitch_get_port_node(rdev);
	of_node_get(port);
	err = of_mdiobus_register(mii_bus, port);
	if (err < 0) {
		mdiobus_free(mii_bus);
		goto out;
	}

	rdev->etha->mii = mii_bus;

out:
	of_node_put(port);

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

	if (phydev->link != rdev->etha->link) {
		phy_print_status(phydev);
		rdev->etha->link = phydev->link;
	}
}

static int rswitch_phy_init(struct rswitch_device *rdev, struct device_node *phy)
{
	struct phy_device *phydev;
	int err = 0;

	phydev = of_phy_connect(rdev->ndev, phy, rswitch_adjust_link, 0,
				rdev->etha->phy_interface);
	if (!phydev) {
		err = -ENOENT;
		goto out;
	}

	phy_attached_info(phydev);

out:
	return err;
}

static void rswitch_phy_deinit(struct rswitch_device *rdev)
{
	if (rdev->ndev->phydev) {
		struct device_node *ports, *port;
		u32 index;

		phy_disconnect(rdev->ndev->phydev);
		rdev->ndev->phydev = NULL;

		ports = of_get_child_by_name(rdev->ndev->dev.parent->of_node, "ports");
		for_each_child_of_node(ports, port) {
			of_property_read_u32(port, "reg", &index);
			if (index == rdev->etha->index)
				break;
		}

		if (of_phy_is_fixed_link(port))
			of_phy_deregister_fixed_link(port);

		of_node_put(ports);
	}
}

static int rswitch_open(struct net_device *ndev)
{
	struct rswitch_device *rdev = netdev_priv(ndev);
	struct device_node *phy;
	int err = 0;
	bool phy_started = false;
	unsigned long flags;

	napi_enable(&rdev->napi);

	if (!parallel_mode && rdev->etha) {
		if (!rdev->etha->operated) {
			if (!rdev->etha->mii) {
				phy = rswitch_get_phy_node(rdev);
				if (!phy)
					goto error;
			}

			err = rswitch_etha_hw_init(rdev->etha, ndev->dev_addr);
			if (err < 0)
				goto error;

			if (!rdev->etha->mii) {
				err = rswitch_mii_register(rdev);
				if (err < 0)
					goto error;
				if (!rdev->priv->vpf_mode) {
					err = rswitch_phy_init(rdev, phy);
					if (err < 0)
						goto error;
				}

				of_node_put(phy);
			}
		}

		if (!rdev->priv->vpf_mode) {
			ndev->phydev->speed = rdev->etha->speed;
			phy_set_max_speed(ndev->phydev, rdev->etha->speed);

			phy_start(ndev->phydev);
		}

		phy_started = true;

		if (!rdev->priv->serdes_common_init && !rdev->priv->vpf_mode) {
			err = rswitch_serdes_common_init(rdev->etha);
			if (err < 0)
				goto error;
			rdev->priv->serdes_common_init = true;
		}

		if (!rdev->etha->operated && !rdev->priv->vpf_mode) {
			err = rswitch_serdes_chan_init(rdev->etha);
			if (err < 0)
				goto error;
		}

		rdev->etha->operated = true;
	}

	ndev->max_mtu = MAX_MTU_SZ;
	ndev->min_mtu = ETH_MIN_MTU;

	netif_start_queue(ndev);

	/* Enable RX */
	rswitch_modify(rdev->addr, GWTRC0, 0, BIT(rdev->rx_chain->index));

	/* Enable interrupt */
	pr_debug("%s: tx = %d, rx = %d\n", __func__, rdev->tx_chain->index, rdev->rx_chain->index);

	spin_lock_irqsave(&rdev->priv->lock, flags);
	rswitch_enadis_data_irq(rdev->priv, rdev->tx_chain->index, true);
	rswitch_enadis_data_irq(rdev->priv, rdev->rx_chain->index, true);
	spin_unlock_irqrestore(&rdev->priv->lock, flags);

	rdev->priv->chan_running |= BIT(rdev->port);
out:
	return err;

error:
	if (phy_started)
		phy_stop(ndev->phydev);
	rswitch_phy_deinit(rdev);
	rswitch_mii_unregister(rdev);
	napi_disable(&rdev->napi);
	goto out;
};

static int rswitch_stop(struct net_device *ndev)
{
	struct rswitch_device *rdev = netdev_priv(ndev);

	if (rdev->etha && ndev->phydev)
		phy_stop(ndev->phydev);

	napi_disable(&rdev->napi);

	return 0;
};

static int rswitch_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct rswitch_device *rdev = netdev_priv(ndev);
	int ret = NETDEV_TX_OK;
	int entry;
	dma_addr_t dma_addr;
	struct rswitch_ext_desc *desc;
	unsigned long flags;
	struct rswitch_gwca_chain *c = rdev->tx_chain;
	int i, num_desc, pkt_len, size;

	spin_lock_irqsave(&rdev->lock, flags);

	num_desc = skb->len % MAX_DESC_SZ ? skb->len / MAX_DESC_SZ + 1 : skb->len / MAX_DESC_SZ;

	if (c->cur - c->dirty > c->num_ring - num_desc) {
		netif_stop_subqueue(ndev, 0);
		ret = NETDEV_TX_BUSY;
		goto out;
	}

	if (skb_put_padto(skb, ETH_ZLEN))
		goto out;

	dma_addr = dma_map_single(ndev->dev.parent, skb->data, skb->len, DMA_TO_DEVICE);
	if (dma_mapping_error(ndev->dev.parent, dma_addr))
		goto drop;

	entry = c->cur % c->num_ring;

	/* Stored the skb at the last descriptor so that can free the previous descriptors ASAP */
	c->skb[(entry + num_desc - 1) % c->num_ring] = skb;
	desc = &c->tx_ring[entry];
	desc->dptrl = cpu_to_le32(lower_32_bits(dma_addr));
	desc->dptrh = cpu_to_le32(upper_32_bits(dma_addr));

	if (num_desc > 1) {
		size = skb->len / num_desc;

		pkt_len = skb->len - (num_desc - 1) * size;
		desc->info_ds = cpu_to_le16(pkt_len);
		for (i = 1; i < num_desc; i++) {
			desc = &c->tx_ring[(entry + i) % c->num_ring];
			desc->dptrl = cpu_to_le32(lower_32_bits(dma_addr + pkt_len));
			desc->dptrh = cpu_to_le32(upper_32_bits(dma_addr + pkt_len));
			desc->info_ds = cpu_to_le16(size);

			pkt_len += size;
		}
	} else {
		desc->info_ds = cpu_to_le16(skb->len);
	}

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
	dma_wmb();

	if (num_desc > 1) {
		for (i = num_desc - 1; i >= 0; i--) {
			desc = &c->tx_ring[(entry + i) % c->num_ring];
			if (!i)
				desc->die_dt = DT_FSTART;
			else if (i == num_desc - 1)
				desc->die_dt = DT_FEND | DIE;
			else
				desc->die_dt = DT_FMID;
		}
	} else {
		desc = &c->tx_ring[entry];
		desc->die_dt = DT_FSINGLE | DIE;
	}

	c->cur += num_desc;
	rswitch_modify(rdev->addr, GWTRC0, 0, BIT(c->index));

out:
	spin_unlock_irqrestore(&rdev->lock, flags);

	return ret;

drop:
	dev_kfree_skb_any(skb);
	goto out;

}

static struct net_device_stats *rswitch_get_stats(struct net_device *ndev)
{
	return &ndev->stats;
}

static int rswitch_hwstamp_get(struct net_device *ndev, struct ifreq *req)
{
	struct rswitch_device *rdev = netdev_priv(ndev);
	struct rswitch_private *priv = rdev->priv;
	struct rcar_gen4_ptp_private *ptp_priv = priv->ptp_priv;
	struct hwtstamp_config config;

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

static int rswitch_hwstamp_set(struct net_device *ndev, struct ifreq *req)
{
	struct rswitch_device *rdev = netdev_priv(ndev);
	struct rswitch_private *priv = rdev->priv;
	struct rcar_gen4_ptp_private *ptp_priv = priv->ptp_priv;
	struct hwtstamp_config config;
	u32 tstamp_rx_ctrl = RCAR_GEN4_RXTSTAMP_ENABLED;
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

	ptp_priv->tstamp_tx_ctrl = tstamp_tx_ctrl;
	ptp_priv->tstamp_rx_ctrl = tstamp_rx_ctrl;

	return copy_to_user(req->ifr_data, &config, sizeof(config)) ? -EFAULT : 0;
}

static int rswitch_do_ioctl(struct net_device *ndev, struct ifreq *req, int cmd)
{
	if (!netif_running(ndev))
		return -EINVAL;

	switch (cmd) {
	case SIOCGHWTSTAMP:
		return rswitch_hwstamp_get(ndev, req);
	case SIOCSHWTSTAMP:
		return rswitch_hwstamp_set(ndev, req);
	default:
		break;
	}

	return 0;
}

static const struct net_device_ops rswitch_netdev_ops = {
	.ndo_open = rswitch_open,
	.ndo_stop = rswitch_stop,
	.ndo_start_xmit = rswitch_start_xmit,
	.ndo_get_stats = rswitch_get_stats,
	.ndo_do_ioctl = rswitch_do_ioctl,
	.ndo_validate_addr = eth_validate_addr,
	.ndo_set_mac_address = eth_mac_addr,
};

static int rswitch_get_ts_info(struct net_device *ndev, struct ethtool_ts_info *info)
{
	struct rswitch_device *rdev = netdev_priv(ndev);

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

static const struct ethtool_ops rswitch_ethtool_ops = {
	.get_ts_info = rswitch_get_ts_info,
};

static const struct of_device_id renesas_eth_sw_of_table[] = {
	{ .compatible = "renesas,etherswitch", },
	{ }
};
MODULE_DEVICE_TABLE(of, renesas_eth_sw_of_table);

static void rswitch_clock_enable(struct rswitch_private *priv)
{
	rs_write32(GENMASK(RSWITCH_NUM_HW - 1, 0) | RCEC_RCE, priv->addr + RCEC);
}

static void rswitch_reset(struct rswitch_private *priv)
{
	if (!parallel_mode) {
		rs_write32(RRC_RR, priv->addr + RRC);
		rs_write32(RRC_RR_CLR, priv->addr + RRC);

		if (!priv->vpf_mode) {
			reset_control_assert(priv->sd_rst);
			mdelay(1);
			reset_control_deassert(priv->sd_rst);
		}

		if (!priv->vpf_mode) {
			/* There is a slight difference in SerDes hardware behavior between
			 * each version after resetting. This step is to ensure the stable
			 * condition of initialization, especially for R-Car S4 v1.1.
			 */
			mdelay(1);
			rs_write32(0, priv->serdes_addr + RSWITCH_SERDES_LOCAL_OFFSET);
		}
	} else {
		int gwca_idx;
		u32 gwro_offset;
		int mode;
		int count;

		if (priv->gwca.index == RSWITCH_GWCA_IDX_TO_HW_NUM(0)) {
			gwca_idx = 14;
			gwro_offset = RSWITCH_GWCA1_OFFSET;
		} else {
			gwca_idx = 13;
			gwro_offset = RSWITCH_GWCA0_OFFSET;
		}

		count = 0;
		do {
			mode = rs_read32(priv->addr + gwro_offset + 0x0004) & GWMS_OPS_MASK;
			if (mode == GWMC_OPC_OPERATION)
				break;

			count++;
			if (!(count % 100))
				pr_info(" rswitch wait for GWMS%d %d==%d\n", gwca_idx, mode,
					GWMC_OPC_OPERATION);

			mdelay(10);
		} while (1);
	}
}

static void rswitch_etha_init(struct rswitch_private *priv, int index)
{
	struct rswitch_etha *etha = &priv->etha[index];

	memset(etha, 0, sizeof(*etha));
	etha->index = index;
	etha->addr = priv->addr + rswitch_etha_offs(index);
	etha->serdes_addr = priv->serdes_addr + index * RSWITCH_SERDES_OFFSET;

	/* MPIC.PSMCS = (clk [MHz] / (MDC frequency [MHz] * 2) - 1.
	 * Calculating PSMCS value as MDC frequency = 2.5MHz. So, multiply
	 * both the numerator and the denominator by 10.
	 */
	etha->psmcs = clk_get_rate(priv->clk) / 100000 / (25 * 2) - 1;
}

static int rswitch_gwca_change_mode(struct rswitch_private *priv,
				    enum rswitch_gwca_mode mode)
{
	int ret;

	/* Enable clock */
	if (!rswitch_agent_clock_is_enabled(priv->addr, priv->gwca.index))
		rswitch_agent_clock_ctrl(priv->addr, priv->gwca.index, 1);

	rs_write32(mode, priv->addr + GWMC);

	ret = rswitch_reg_wait(priv->addr, GWMS, GWMS_OPS_MASK, mode);

	/* Disable clock */
	if (mode == GWMC_OPC_DISABLE)
		rswitch_agent_clock_ctrl(priv->addr, priv->gwca.index, 0);

	return ret;
}

static int rswitch_gwca_mcast_table_reset(struct rswitch_private *priv)
{
	rs_write32(GWMTIRM_MTIOG, priv->addr + GWMTIRM);
	return rswitch_reg_wait(priv->addr, GWMTIRM, GWMTIRM_MTR, GWMTIRM_MTR);
}

static int rswitch_gwca_axi_ram_reset(struct rswitch_private *priv)
{
	rs_write32(GWARIRM_ARIOG, priv->addr + GWARIRM);
	return rswitch_reg_wait(priv->addr, GWARIRM, GWARIRM_ARR, GWARIRM_ARR);
}

static int rswitch_gwca_hw_init(struct rswitch_private *priv)
{
	int err;

	err = rswitch_gwca_change_mode(priv, GWMC_OPC_DISABLE);
	if (err < 0)
		return err;
	err = rswitch_gwca_change_mode(priv, GWMC_OPC_CONFIG);
	if (err < 0)
		return err;
	err = rswitch_gwca_mcast_table_reset(priv);
	if (err < 0)
		return err;
	err = rswitch_gwca_axi_ram_reset(priv);
	if (err < 0)
		return err;

	/* Full setting flow */
	rs_write32(GWVCC_VEM_SC_TAG, priv->addr + GWVCC);
	rs_write32(0, priv->addr + GWTTFC);
	rs_write32(lower_32_bits(priv->desc_bat_dma), priv->addr + GWDCBAC1);
	rs_write32(upper_32_bits(priv->desc_bat_dma), priv->addr + GWDCBAC0);

	iowrite32((0xff << 8) | 0xff, priv->addr + GWMDNC);
	iowrite32(GWTPC_PPPL(GWCA_IPV_NUM), priv->addr + GWTPC0);

	err = rswitch_gwca_change_mode(priv, GWMC_OPC_DISABLE);
	if (err < 0)
		return err;
	err = rswitch_gwca_change_mode(priv, GWMC_OPC_OPERATION);
	if (err < 0)
		return err;

	return 0;
}

static void rswitch_gwca_chain_free(struct net_device *ndev,
				    struct rswitch_private *priv,
				    struct rswitch_gwca_chain *c)
{
	int i;

	if (!c->dir_tx) {
		dma_free_coherent(ndev->dev.parent,
				  sizeof(struct rswitch_ext_ts_desc) *
				  (c->num_ring + 1), c->rx_ring, c->ring_dma);
		c->rx_ring = NULL;

		for (i = 0; i < c->num_ring; i++)
			skb_free_frag(c->rx_bufs[i]);

		kfree(c->rx_bufs);
		c->rx_bufs = NULL;
	} else {
		dma_free_coherent(ndev->dev.parent,
				  sizeof(struct rswitch_desc) *
				  (c->num_ring + 1), c->tx_ring, c->ring_dma);
		c->tx_ring = NULL;
		kfree(c->skb);
		c->skb = NULL;
	}
}

static int rswitch_gwca_chain_init(struct net_device *ndev,
				   struct rswitch_private *priv,
				   struct rswitch_gwca_chain *c,
				   bool dir_tx, int num_ring)
{
	int i, bit;
	int index = c->index;	/* Keep the index before memset() */
	void *rx_buf;

	memset(c, 0, sizeof(*c));
	c->index = index;
	c->dir_tx = dir_tx;
	c->num_ring = num_ring;
	c->ndev = ndev;

	if (!dir_tx) {
		c->rx_bufs = kcalloc(c->num_ring, sizeof(*c->rx_bufs), GFP_KERNEL);
		if (!c->rx_bufs)
			goto out;

		for (i = 0; i < c->num_ring; i++) {
			rx_buf = netdev_alloc_frag(RSWITCH_RX_BUF_SIZE);
			if (!rx_buf)
				goto out;
			c->rx_bufs[i] = rx_buf;
		}
		c->rx_ring = dma_alloc_coherent(ndev->dev.parent,
				sizeof(struct rswitch_ext_ts_desc) *
				(c->num_ring + 1), &c->ring_dma, GFP_KERNEL);
	} else {
		c->skb = kcalloc(c->num_ring, sizeof(*c->skb), GFP_KERNEL);
		if (!c->skb)
			return -ENOMEM;

		c->tx_ring = dma_alloc_coherent(ndev->dev.parent,
				sizeof(struct rswitch_ext_desc) *
				(c->num_ring + 1), &c->ring_dma, GFP_KERNEL);
	}
	if (!c->rx_ring && !c->tx_ring)
		goto out;

	index = c->index / 32;
	bit = BIT(c->index % 32);
	if (dir_tx)
		priv->gwca.tx_irq_bits[index] |= bit;
	else
		priv->gwca.rx_irq_bits[index] |= bit;

	return 0;

out:
	rswitch_gwca_chain_free(ndev, priv, c);

	return -ENOMEM;
}

static int rswitch_gwca_chain_format(struct net_device *ndev,
				struct rswitch_private *priv,
				struct rswitch_gwca_chain *c)
{
	struct rswitch_ext_desc *ring;
	struct rswitch_desc *desc;
	int tx_ring_size = sizeof(*ring) * c->num_ring;
	int i;
	dma_addr_t dma_addr;

	memset(c->tx_ring, 0, tx_ring_size);
	for (i = 0, ring = c->tx_ring; i < c->num_ring; i++, ring++) {
		if (!c->dir_tx) {
			dma_addr = dma_map_single(ndev->dev.parent,
					c->rx_bufs[i] + NET_SKB_PAD + NET_IP_ALIGN,
					RSWITCH_RX_BUF_SIZE - NET_SKB_PAD - NET_IP_ALIGN,
					DMA_FROM_DEVICE);
			if (!dma_mapping_error(ndev->dev.parent, dma_addr))
				ring->info_ds = cpu_to_le16(MAX_DESC_SZ);
			ring->dptrl = cpu_to_le32(lower_32_bits(dma_addr));
			ring->dptrh = cpu_to_le32(upper_32_bits(dma_addr));
			ring->die_dt = DT_FEMPTY | DIE;
		} else {
			ring->die_dt = DT_EEMPTY | DIE;
		}
	}
	ring->dptrl = cpu_to_le32(lower_32_bits(c->ring_dma));
	ring->dptrh = cpu_to_le32(upper_32_bits(c->ring_dma));
	ring->die_dt = DT_LINKFIX;

	desc = &priv->desc_bat[c->index];
	desc->die_dt = DT_LINKFIX;
	desc->dptrl = cpu_to_le32(lower_32_bits(c->ring_dma));
	desc->dptrh = cpu_to_le32(upper_32_bits(c->ring_dma));

	iowrite32(GWDCC_BALR | (c->dir_tx ? GWDCC_DCP(GWCA_IPV_NUM) | GWDCC_DQT : 0) | GWDCC_EDE,
		  priv->addr + GWDCC_OFFS(c->index));

	return 0;
}

static int rswitch_gwca_chain_ext_ts_format(struct net_device *ndev,
					    struct rswitch_private *priv,
					    struct rswitch_gwca_chain *c)
{
	struct rswitch_ext_ts_desc *ring;
	struct rswitch_desc *desc;
	int ring_size = sizeof(*ring) * c->num_ring;
	int i;
	dma_addr_t dma_addr;

	memset(c->rx_ring, 0, ring_size);
	for (i = 0, ring = c->rx_ring; i < c->num_ring; i++, ring++) {
		if (!c->dir_tx) {
			dma_addr = dma_map_single(ndev->dev.parent,
					c->rx_bufs[i] + NET_SKB_PAD + NET_IP_ALIGN,
					RSWITCH_RX_BUF_SIZE - NET_SKB_PAD - NET_IP_ALIGN,
					DMA_FROM_DEVICE);
			if (!dma_mapping_error(ndev->dev.parent, dma_addr))
				ring->info_ds = cpu_to_le16(MAX_DESC_SZ);
			ring->dptrl = cpu_to_le32(lower_32_bits(dma_addr));
			ring->dptrh = cpu_to_le32(upper_32_bits(dma_addr));
			ring->die_dt = DT_FEMPTY | DIE;
		} else {
			ring->die_dt = DT_EEMPTY | DIE;
		}
	}
	ring->dptrl = cpu_to_le32(lower_32_bits(c->ring_dma));
	ring->dptrh = cpu_to_le32(upper_32_bits(c->ring_dma));
	ring->die_dt = DT_LINKFIX;

	desc = &priv->desc_bat[c->index];
	desc->die_dt = DT_LINKFIX;
	desc->dptrl = cpu_to_le32(lower_32_bits(c->ring_dma));
	desc->dptrh = cpu_to_le32(upper_32_bits(c->ring_dma));

	iowrite32(GWDCC_BALR | (c->dir_tx ? GWDCC_DCP(GWCA_IPV_NUM) | GWDCC_DQT : 0) |
		  GWDCC_ETS | GWDCC_EDE,
		  priv->addr + GWDCC_OFFS(c->index));

	return 0;
}

static int rswitch_desc_alloc(struct rswitch_private *priv)
{
	struct device *dev = &priv->pdev->dev;
	int i, num_chains = priv->gwca.num_chains;

	priv->desc_bat_size = sizeof(struct rswitch_desc) * num_chains;
	priv->desc_bat = dma_alloc_coherent(dev, priv->desc_bat_size,
					    &priv->desc_bat_dma, GFP_KERNEL);
	if (!priv->desc_bat)
		return -ENOMEM;
	for (i = 0; i < num_chains; i++)
		priv->desc_bat[i].die_dt = DT_EOS;

	return 0;
}

static void rswitch_desc_free(struct rswitch_private *priv)
{
	if (priv->desc_bat)
		dma_free_coherent(&priv->pdev->dev, priv->desc_bat_size,
				  priv->desc_bat, priv->desc_bat_dma);
	priv->desc_bat = NULL;
}

static struct rswitch_gwca_chain *rswitch_gwca_get(struct rswitch_private *priv)
{
	int index;

	index = find_first_zero_bit(priv->gwca.used, priv->gwca.num_chains);
	if (index >= priv->gwca.num_chains)
		return NULL;
	set_bit(index, priv->gwca.used);
	priv->gwca.chains[index].index = index;

	return &priv->gwca.chains[index];
}

static void rswitch_gwca_put(struct rswitch_private *priv,
			     struct rswitch_gwca_chain *c)
{
	clear_bit(c->index, priv->gwca.used);
}

static int rswitch_txdmac_init(struct net_device *ndev,
			       struct rswitch_private *priv)
{
	struct rswitch_device *rdev = netdev_priv(ndev);
	int err;

	rdev->tx_chain = rswitch_gwca_get(priv);
	if (!rdev->tx_chain)
		return -EBUSY;

	err = rswitch_gwca_chain_init(ndev, priv, rdev->tx_chain, true, TX_RING_SIZE);
	if (err < 0)
		goto out_init;

	err = rswitch_gwca_chain_format(ndev, priv, rdev->tx_chain);
	if (err < 0)
		goto out_format;

	return 0;

out_format:
	rswitch_gwca_chain_free(ndev, priv, rdev->tx_chain);

out_init:
	rswitch_gwca_put(priv, rdev->tx_chain);

	return err;
}

static void rswitch_txdmac_free(struct net_device *ndev,
				struct rswitch_private *priv)
{
	struct rswitch_device *rdev = netdev_priv(ndev);

	rswitch_gwca_chain_free(ndev, priv, rdev->tx_chain);
	rswitch_gwca_put(priv, rdev->tx_chain);
}

static int rswitch_rxdmac_init(struct net_device *ndev,
			       struct rswitch_private *priv)
{
	struct rswitch_device *rdev = netdev_priv(ndev);
	int err;

	rdev->rx_chain = rswitch_gwca_get(priv);
	if (!rdev->rx_chain)
		return -EBUSY;

	err = rswitch_gwca_chain_init(ndev, priv, rdev->rx_chain, false, RX_RING_SIZE);
	if (err < 0)
		goto out_init;
	err = rswitch_gwca_chain_ext_ts_format(ndev, priv, rdev->rx_chain);
	if (err < 0)
		goto out_format;

	return 0;

out_format:
	rswitch_gwca_chain_free(ndev, priv, rdev->rx_chain);

out_init:
	rswitch_gwca_put(priv, rdev->rx_chain);

	return err;
}

static void rswitch_rxdmac_free(struct net_device *ndev,
				struct rswitch_private *priv)
{
	struct rswitch_device *rdev = netdev_priv(ndev);

	rswitch_gwca_chain_free(ndev, priv, rdev->rx_chain);
	rswitch_gwca_put(priv, rdev->rx_chain);
}

static void rswitch_set_mac_address(struct rswitch_device *rdev)
{
	struct net_device *ndev = rdev->ndev;
	struct device_node *ports, *port;
	u32 index;
	const u8 *mac;

	ports = of_get_child_by_name(ndev->dev.parent->of_node, "ports");

	for_each_child_of_node(ports, port) {
		of_property_read_u32(port, "reg", &index);
		if (index == rdev->etha->index)
			break;
	}

	mac = of_get_mac_address(port);
	if (!IS_ERR(mac))
		ether_addr_copy(ndev->dev_addr, mac);

	if (!is_valid_ether_addr(ndev->dev_addr))
		ether_addr_copy(ndev->dev_addr, rdev->etha->mac_addr);

	if (!is_valid_ether_addr(ndev->dev_addr))
		eth_hw_addr_random(ndev);

	of_node_put(ports);
}

static int rswitch_ndev_create(struct rswitch_private *priv, int index)
{
	struct platform_device *pdev = priv->pdev;
	struct net_device *ndev;
	struct rswitch_device *rdev;
	int err;

	ndev = alloc_etherdev_mqs(sizeof(struct rswitch_device), 1, 1);
	if (!ndev)
		return -ENOMEM;

	SET_NETDEV_DEV(ndev, &pdev->dev);
	ether_setup(ndev);

	rdev = netdev_priv(ndev);
	rdev->ndev = ndev;
	rdev->priv = priv;
	priv->rdev[index] = rdev;
	/* TODO: netdev instance : ETHA port is 1:1 mapping */
	if (index < RSWITCH_MAX_NUM_ETHA) {
		rdev->port = index;
		rdev->etha = &priv->etha[index];
	} else {
		rdev->port = -1;
		rdev->etha = NULL;
	}
	rdev->addr = priv->addr;

	spin_lock_init(&rdev->lock);

	ndev->features = NETIF_F_RXCSUM;
	ndev->hw_features = NETIF_F_RXCSUM;
	ndev->base_addr = (unsigned long)rdev->addr;
	snprintf(ndev->name, IFNAMSIZ, "tsn%d", index);
	ndev->netdev_ops = &rswitch_netdev_ops;
	ndev->ethtool_ops = &rswitch_ethtool_ops;

	netif_napi_add(ndev, &rdev->napi, rswitch_poll, 64);

	rswitch_set_mac_address(rdev);

	/* FIXME: it seems S4 VPF has FWPBFCSDC0/1 only so that we cannot set
	 * CSD = 1 (rx_chain->index = 1) for FWPBFCS03. So, use index = 0
	 * for the RX.
	 */
	err = rswitch_rxdmac_init(ndev, priv);
	if (err < 0)
		goto out_rxdmac;

	err = rswitch_txdmac_init(ndev, priv);
	if (err < 0)
		goto out_txdmac;

	/* Print device information */
	netdev_info(ndev, "MAC address %pMn", ndev->dev_addr);

	return 0;

out_txdmac:
	rswitch_rxdmac_free(ndev, priv);

out_rxdmac:
	netif_napi_del(&rdev->napi);
	free_netdev(ndev);

	return err;
}

static void rswitch_ndev_unregister(struct rswitch_private *priv, int index)
{
	struct rswitch_device *rdev = priv->rdev[index];
	struct net_device *ndev = rdev->ndev;

	rswitch_txdmac_free(ndev, priv);
	rswitch_rxdmac_free(ndev, priv);
	unregister_netdev(ndev);
	netif_napi_del(&rdev->napi);
	free_netdev(ndev);
}

static int rswitch_bpool_config(struct rswitch_private *priv)
{
	u32 val;

	val = rs_read32(priv->addr + CABPIRM);
	if (val & CABPIRM_BPR)
		return 0;

	rs_write32(CABPIRM_BPIOG, priv->addr + CABPIRM);
	return rswitch_reg_wait(priv->addr, CABPIRM, CABPIRM_BPR, CABPIRM_BPR);
}

static void rswitch_coma_init(struct rswitch_private *priv)
{
	iowrite32(CABPPFLC_INIT_VALUE, priv->addr + CABPPFLC0);
}

static void rswitch_queue_interrupt(struct net_device *ndev)
{
	struct rswitch_device *rdev = netdev_priv(ndev);

	if (napi_schedule_prep(&rdev->napi)) {
		spin_lock(&rdev->priv->lock);
		rswitch_enadis_data_irq(rdev->priv, rdev->tx_chain->index, false);
		rswitch_enadis_data_irq(rdev->priv, rdev->rx_chain->index, false);
		spin_unlock(&rdev->priv->lock);
		__napi_schedule(&rdev->napi);
	}
}

static irqreturn_t __maybe_unused rswitch_data_irq(struct rswitch_private *priv, u32 *dis)
{
	struct rswitch_gwca_chain *c;
	int i;
	int index, bit;

	for (i = 0; i < priv->gwca.num_chains; i++) {
		c = &priv->gwca.chains[i];
		index = c->index / 32;
		bit = BIT(c->index % 32);
		if (!(dis[index] & bit))
			continue;

		rswitch_ack_data_irq(priv, c->index);
		rswitch_queue_interrupt(c->ndev);
	}

	return IRQ_HANDLED;
}

static irqreturn_t rswitch_irq(int irq, void *dev_id)
{
	struct rswitch_private *priv = dev_id;
	irqreturn_t ret = IRQ_NONE;
	u32 dis[RSWITCH_NUM_IRQ_REGS];

	rswitch_get_data_irq_status(priv, dis);

	if (rswitch_is_any_data_irq(priv, dis, true) ||
	    rswitch_is_any_data_irq(priv, dis, false))
		ret = rswitch_data_irq(priv, dis);

	return ret;
}

static int rswitch_request_irqs(struct rswitch_private *priv)
{
	int irq, err;

	/* FIXME: other queues */
	irq = platform_get_irq_byname(priv->pdev, "gwca1_gwdis");
	if (irq < 0)
		goto out;

	err = request_irq(irq, rswitch_irq, 0, "rswitch: gwca1_gwdis", priv);
	if (err < 0)
		goto out;

out:
	return err;
}

static int rswitch_free_irqs(struct rswitch_private *priv)
{
	int irq;

	irq = platform_get_irq_byname(priv->pdev, "gwca1_gwdis");
	if (irq < 0)
		return irq;

	free_irq(irq, priv);

	return 0;
}

static void rswitch_fwd_init(struct rswitch_private *priv)
{
	int i;
	int gwca_hw_idx = RSWITCH_HW_NUM_TO_GWCA_IDX(priv->gwca.index);

	for (i = 0; i < RSWITCH_NUM_HW; i++) {
		rs_write32(FWPC0_DEFAULT, priv->addr + FWPC00 + (i * 0x10));
		rs_write32(0, priv->addr + FWPBFC(i));
	}
	/*
	 * FIXME: hardcoded setting. Make a macro about port vector calc.
	 * ETHA0 = forward to GWCA0, GWCA0 = forward to ETHA0,...
	 * Currently, always forward to GWCA1.
	 */
	for (i = 0; i < num_etha_ports; i++) {
		rs_write32(priv->rdev[i]->rx_chain->index, priv->addr + FWPBFCSDC(gwca_hw_idx, i));
		rs_write32(BIT(priv->gwca.index), priv->addr + FWPBFC(i));
	}

	/* For GWCA */
	rs_write32(FWPC0_DEFAULT, priv->addr + FWPC0(priv->gwca.index));
	rs_write32(FWPC1_DDE, priv->addr + FWPC1(priv->gwca.index));
	rs_write32(0, priv->addr + FWPBFC(priv->gwca.index));
	rs_write32(GENMASK(num_etha_ports - 1, 0), priv->addr + FWPBFC(priv->gwca.index));

	/* TODO: add chrdev for fwd */
	/* TODO: add proc for fwd */
}

static int rswitch_init(struct rswitch_private *priv)
{
	int i;
	int err;

	/* Non hardware initializations */
	for (i = 0; i < num_etha_ports; i++)
		rswitch_etha_init(priv, i);

	err = rswitch_desc_alloc(priv);
	if (err < 0)
		return -ENOMEM;

	/* Hardware initializations */
	if (!parallel_mode)
		rswitch_clock_enable(priv);
	for (i = 0; i < num_ndev; i++)
		rswitch_etha_read_mac_address(&priv->etha[i]);
	rswitch_reset(priv);
	err = rswitch_gwca_hw_init(priv);
	if (err < 0)
		goto out;

	for (i = 0; i < num_ndev; i++) {
		err = rswitch_ndev_create(priv, i);
		if (err < 0)
			goto out;
	}

	/* TODO: chrdev register */

	if (!parallel_mode) {
		err = rswitch_bpool_config(priv);
		if (err < 0)
			goto out;

		rswitch_coma_init(priv);
		rswitch_fwd_init(priv);
	}

	err = rcar_gen4_ptp_init(priv->ptp_priv, RCAR_GEN4_PTP_REG_LAYOUT, RCAR_GEN4_PTP_CLOCK_X5H);
	if (err < 0)
		goto out;

	err = rswitch_request_irqs(priv);
	if (err < 0)
		goto out;
	/* Register devices so Linux network stack can access them now */

	for (i = 0; i < num_ndev; i++) {
		err = register_netdev(priv->rdev[i]->ndev);
		if (err)
			goto out;
	}

	return 0;

out:
	for (i--; i >= 0; i--)
		rswitch_ndev_unregister(priv, i);

	rswitch_desc_free(priv);

	return err;
}

static void rswitch_deinit_rdev(struct rswitch_private *priv, int index)
{
	struct rswitch_device *rdev = priv->rdev[index];

	if (rdev->etha && rdev->etha->operated) {
		rswitch_phy_deinit(rdev);
		rswitch_mii_unregister(rdev);
	}
}

static void rswitch_deinit(struct rswitch_private *priv)
{
	int i;

	for (i = 0; i < num_ndev; i++) {
		rswitch_deinit_rdev(priv, i);
		rswitch_ndev_unregister(priv, i);
	}

	rswitch_free_irqs(priv);
	rswitch_desc_free(priv);
}

static int renesas_eth_sw_probe(struct platform_device *pdev)
{
	struct rswitch_private *priv;
	struct resource *res, *res_serdes, *res_ptp;
	int ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	res_serdes = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	res_ptp = platform_get_resource_byname(pdev, IORESOURCE_MEM, "gptp");
	if (!res || !res_serdes || !res_ptp) {
		dev_err(&pdev->dev, "invalid resource\n");
		return -EINVAL;
	}

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	spin_lock_init(&priv->lock);

	priv->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(priv->clk))
		return PTR_ERR(priv->clk);

	if (of_find_property(pdev->dev.of_node, "vpf_mode", NULL))
		priv->vpf_mode = true;
	else
		priv->vpf_mode = false;

	priv->ptp_priv = rcar_gen4_ptp_alloc(pdev);
	if (!priv->ptp_priv)
		return -ENOMEM;

	if (!parallel_mode)
		parallel_mode = of_property_read_bool(pdev->dev.of_node, "parallel_mode");

	if (parallel_mode) {
		num_ndev = 1;
		num_etha_ports = 1;
	}

	priv->ptp_priv->parallel_mode = parallel_mode;

	if (!parallel_mode) {
		priv->rsw_clk = devm_clk_get(&pdev->dev, "rsw2");
		if (IS_ERR(priv->rsw_clk)) {
			dev_err(&pdev->dev, "Failed to get rsw2 clock: %ld\n",
				PTR_ERR(priv->rsw_clk));
			return -PTR_ERR(priv->rsw_clk);
		}

		priv->phy_clk = devm_clk_get(&pdev->dev, "eth-phy");
		if (IS_ERR(priv->phy_clk)) {
			dev_err(&pdev->dev, "Failed to get eth-phy clock: %ld\n",
				PTR_ERR(priv->phy_clk));
			return -PTR_ERR(priv->phy_clk);
		}
	}

	priv->sd_rst = devm_reset_control_get(&pdev->dev, "eth-phy");

	platform_set_drvdata(pdev, priv);
	priv->pdev = pdev;
	priv->addr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(priv->addr))
		return PTR_ERR(priv->addr);

	priv->ptp_priv->addr = devm_ioremap_resource(&pdev->dev, res_ptp);
	if (IS_ERR(priv->ptp_priv->addr))
		return PTR_ERR(priv->ptp_priv->addr);

	priv->serdes_addr = devm_ioremap_resource(&pdev->dev, res_serdes);
	if (IS_ERR(priv->serdes_addr))
		return PTR_ERR(priv->serdes_addr);

	debug_addr = priv->addr;
	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(40));
	if (ret < 0) {
		ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
		if (ret < 0)
			return ret;
	}

	/* Fixed to use GWCA1 */
	priv->gwca.index = 14;
	priv->gwca.num_chains = num_ndev * NUM_CHAINS_PER_NDEV;
	priv->gwca.chains = devm_kcalloc(&pdev->dev, priv->gwca.num_chains,
					 sizeof(*priv->gwca.chains), GFP_KERNEL);
	if (!priv->gwca.chains)
		return -ENOMEM;

	if (!parallel_mode) {
		pm_runtime_enable(&pdev->dev);
		pm_runtime_get_sync(&pdev->dev);
		clk_prepare(priv->phy_clk);
		clk_enable(priv->phy_clk);
	}

	rswitch_init(priv);

	device_set_wakeup_capable(&pdev->dev, 1);

	return 0;
}

static int renesas_eth_sw_remove(struct platform_device *pdev)
{
	struct rswitch_private *priv = platform_get_drvdata(pdev);

	if (!parallel_mode) {
		/* Disable R-Switch clock */
		rs_write32(RCDC_RCD, priv->addr + RCDC);
		rswitch_deinit(priv);

		pm_runtime_put(&pdev->dev);
		pm_runtime_disable(&pdev->dev);
		clk_disable(priv->phy_clk);
	}

	rcar_gen4_ptp_unregister(priv->ptp_priv);
	rswitch_desc_free(priv);

	platform_set_drvdata(pdev, NULL);

	return 0;
}

static int __maybe_unused rswitch_suspend(struct device *dev)
{
	struct rswitch_private *priv = dev_get_drvdata(dev);
	int i;

	for (i = 0; i < num_ndev; i++) {
		struct net_device *ndev = priv->rdev[i]->ndev;

		if (priv->rdev[i]->tx_chain->index < 0)
			continue;

		if (netif_running(ndev)) {
			netif_stop_subqueue(ndev, 0);
			rswitch_stop(ndev);
		}

		rswitch_txdmac_free(ndev, priv);
		rswitch_rxdmac_free(ndev, priv);
		priv->rdev[i]->etha->operated = false;
	}

	priv->serdes_common_init = false;
	rcar_gen4_ptp_unregister(priv->ptp_priv);
	rswitch_desc_free(priv);

	return 0;
}

static int rswitch_resume_chan(struct net_device *ndev)
{
	struct rswitch_device *rdev = netdev_priv(ndev);
	int ret;

	ret = rswitch_rxdmac_init(ndev, rdev->priv);
	if (ret)
		goto out_dmac;

	ret = rswitch_txdmac_init(ndev, rdev->priv);
	if (ret) {
		rswitch_rxdmac_free(ndev, rdev->priv);
		goto out_dmac;
	}

	if (netif_running(ndev)) {
		ret = rswitch_open(ndev);
		if (ret)
			goto error;
	}

	return 0;

error:
	rswitch_txdmac_free(ndev, rdev->priv);
	rswitch_rxdmac_free(ndev, rdev->priv);
out_dmac:
	/* Workround that still gets two chains (rx, tx)
	 * to allow the next channel, if any, to restore
	 * the correct index of chains.
	 */
	rswitch_gwca_get(rdev->priv);
	rswitch_gwca_get(rdev->priv);
	rdev->tx_chain->index = -1;

	return ret;
}

static int __maybe_unused rswitch_resume(struct device *dev)
{
	struct rswitch_private *priv = dev_get_drvdata(dev);
	int i, ret, err = 0;

	ret = rswitch_desc_alloc(priv);
	if (ret)
		return ret;

	if (ret) {
		rswitch_desc_free(priv);
		return ret;
	}

	if (!parallel_mode)
		rswitch_clock_enable(priv);

	ret = rswitch_gwca_hw_init(priv);
	if (ret)
		return ret;

	if (!parallel_mode) {
		ret = rswitch_bpool_config(priv);
		if (ret)
			return ret;

		rswitch_fwd_init(priv);

	}

	ret = rcar_gen4_ptp_init(priv->ptp_priv, RCAR_GEN4_PTP_REG_LAYOUT, RCAR_GEN4_PTP_CLOCK_X5H);
	if (ret)
		return ret;

	for (i = 0; i < num_ndev; i++) {
		struct net_device *ndev = priv->rdev[i]->ndev;

		if (priv->rdev[i]->tx_chain->index >= 0) {
			ret = rswitch_resume_chan(ndev);
			if (ret) {
				pr_info("Failed to resume %s", ndev->name);
				err++;
			}
		} else {
			err++;
		}
	}

	if (err == num_ndev) {
		rswitch_desc_free(priv);

		return -ENXIO;
	}

	return 0;
}

static int __maybe_unused rswitch_runtime_nop(struct device *dev)
{
	return 0;
}

static const struct dev_pm_ops rswitch_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(rswitch_suspend, rswitch_resume)
	SET_RUNTIME_PM_OPS(rswitch_runtime_nop, rswitch_runtime_nop, NULL)
};

static struct platform_driver renesas_eth_sw_driver_platform = {
	.probe = renesas_eth_sw_probe,
	.remove = renesas_eth_sw_remove,
	.driver = {
		.name	= "renesas_eth_sw",
		.pm	= &rswitch_dev_pm_ops,
		.of_match_table = renesas_eth_sw_of_table,
	}
};
module_platform_driver(renesas_eth_sw_driver_platform);
MODULE_AUTHOR("Yoshihiro Shimoda");
MODULE_DESCRIPTION("Renesas Ethernet Switch device driver");
MODULE_LICENSE("GPL v2");
