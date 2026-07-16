// SPDX-License-Identifier: GPL-2.0-only
/*
 * PCIe 6.0 Controller driver for Renesas R-Car Gen5 Series SoCs
 */

#include <linux/io.h>
#include <linux/of_device.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/delay.h>
#include <linux/sys_soc.h>
#include <linux/gpio/consumer.h>
#include <linux/iopoll.h>
#include <linux/platform_device.h>
#include <linux/of.h>

#include "pcie6-rcar-gen5.h"
#include "pcie6-designware.h"


static void rcar_gen5_pcie6_fwupdate(struct rcar_pcie6 *rcar_pcie6, int num_lanes, u32 channel)
{
	u32 i, val;
	void __iomem *sram_addr;

	if (channel == 0) {
		/* Write ICCM firmware */
		sram_addr = rcar_pcie6->phy_base + ICCM_OFFSET;
		for (i = 0; i < rcar_pcie6->fw_iccm->size; i += 4) {
			val = get_unaligned_le32(rcar_pcie6->fw_iccm->data + i);
			writel(val, sram_addr);
			sram_addr += 4;
		}

		/* Write DCCM firmware */
		sram_addr = rcar_pcie6->phy_base + DCCM_OFFSET;
		for (i = 0; i < rcar_pcie6->fw_dccm->size; i += 4) {
			val = get_unaligned_le32(rcar_pcie6->fw_dccm->data + i);
			writel(val, sram_addr);
			sram_addr += 4;
		}

		/* 8 lanes is only for ch0 */
		if (num_lanes == 8) {
			// Write ICCM firmware for 8 lanes
			sram_addr = rcar_pcie6->phy_shared + ICCM_OFFSET;
			for (i = 0; i < rcar_pcie6->fw_iccm->size; i += 4) {
				val = get_unaligned_le32(rcar_pcie6->fw_iccm->data + i);
				writel(val, sram_addr);
				sram_addr += 4;
			}

			/* Write DCCM firmware for 8 lanes */
			sram_addr = rcar_pcie6->phy_shared + DCCM_OFFSET;
			for (i = 0; i < rcar_pcie6->fw_dccm->size; i += 4) {
				val = get_unaligned_le32(rcar_pcie6->fw_dccm->data + i);
				writel(val, sram_addr);
				sram_addr += 4;
			}
		}
	} else {
		/* Write ICCM firmware */
		sram_addr = rcar_pcie6->phy_base + ICCM_OFFSET;
		for (i = 0; i < rcar_pcie6->fw_iccm->size; i += 4) {
			val = get_unaligned_le32(rcar_pcie6->fw_iccm->data + i);
			writel(val, sram_addr);
			sram_addr += 4;
		}
		/* Write DCCM firmware */
		sram_addr = rcar_pcie6->phy_base + DCCM_OFFSET;
		for (i = 0; i < rcar_pcie6->fw_dccm->size; i += 4) {
			val = get_unaligned_le32(rcar_pcie6->fw_dccm->data + i);
			writel(val, sram_addr);
			sram_addr += 4;
		}
	}

}

void rcar_gen5_pcie6_txpreset_coef_mapping(struct dw_pcie6 *pci)
{
	u32 val, k;

	/* Full swing with preset = '0' */
	for (k = 0; k <= 2; k++) {
		val = dw_pcie6_readl_dbi(pci, PCIEG6_PF0_GEN3_RELATED_OFF);
		val &= ~RATE_SHADOW_SELECT;
		if (k != 0)
			val |= (k << 24);
		val |= BIT(15);
		dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_RELATED_OFF, val);

		dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_PSET_INDEX_OFF, 0x0);
		dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_PSET_COEF_MAP_0, 0x0000C8C0);
		dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_PSET_INDEX_OFF, 0x1);
		dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_PSET_COEF_MAP_0, 0x00007A00);
		dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_PSET_INDEX_OFF, 0x2);
		dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_PSET_COEF_MAP_0, 0x000009C8);
		dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_PSET_INDEX_OFF, 0x3);
		dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_PSET_COEF_MAP_0, 0x00005A80);
		dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_PSET_INDEX_OFF, 0x4);
		dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_PSET_COEF_MAP_0, 0x00000BC0);
		dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_PSET_INDEX_OFF, 0x5);
		dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_PSET_COEF_MAP_0, 0x00000AC4);
		dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_PSET_INDEX_OFF, 0x6);
		dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_PSET_COEF_MAP_0, 0x00000A46);
		dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_PSET_INDEX_OFF, 0x7);
		dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_PSET_COEF_MAP_0, 0x0000A844);
		dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_PSET_INDEX_OFF, 0x8);
		dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_PSET_COEF_MAP_0, 0x000068C6);
		dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_PSET_INDEX_OFF, 0x9);
		dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_PSET_COEF_MAP_0, 0x000009C8);
		dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_PSET_INDEX_OFF, 0xA);
		dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_PSET_COEF_MAP_0, 0x000107C0);
		dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_LOCAL_FS_LF_OFF, 0x00000BCF);

		val = dw_pcie6_readl_dbi(pci, PCIEG6_PF0_GEN3_EQ_CONTROL_OFF);
		val |= GENMASK(5, 4);
		dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_CONTROL_OFF, val);

		val = dw_pcie6_readl_dbi(pci, PCIEG6_PF0_GEN3_RELATED_OFF);
		val |= (BIT(13) | BIT(10));
		dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_RELATED_OFF, val);

		val = dw_pcie6_readl_dbi(pci, PCIEG6_PF0_GEN3_EQ_CONTROL_OFF);
		val &= ~(GENMASK(23, 8));
		dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_CONTROL_OFF, val);
	}

	k = GENMASK(1, 0);

	val = dw_pcie6_readl_dbi(pci, PCIEG6_PF0_GEN3_RELATED_OFF);
	val |= BIT(15) | (k << 24);
	dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_RELATED_OFF, val);

	dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_PSET_INDEX_OFF, 0x0);
	dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_PSET_COEF_MAP_0, 0x00000BC0);
	dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_PSET_INDEX_OFF, 0x1);
	dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_PSET_COEF_MAP_0, 0x00000AC4);
	dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_PSET_INDEX_OFF, 0x2);
	dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_PSET_COEF_MAP_0, 0x000009CB);
	dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_PSET_INDEX_OFF, 0x3);
	dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_PSET_COEF_MAP_0, 0x00003B00);
	dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_PSET_INDEX_OFF, 0x4);
	dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_PSET_COEF_MAP_0, 0x000089C0);
	dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_PSET_INDEX_OFF, 0x5);
	dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_PSET_COEF_MAP_0, 0x000C08C9);
	dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_PSET_INDEX_OFF, 0x6);
	dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_PSET_COEF_MAP_0, 0x00087845);
	dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_PSET_INDEX_OFF, 0x7);
	dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_PSET_COEF_MAP_0, 0x0010084A);
	dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_PSET_INDEX_OFF, 0x8);
	dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_PSET_COEF_MAP_0, 0x001407CB);
	dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_PSET_INDEX_OFF, 0x9);
	dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_PSET_COEF_MAP_0, 0x0014274B);
	dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_PSET_INDEX_OFF, 0xA);
	dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_PSET_COEF_MAP_0, 0x0000F800);

	dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_LOCAL_FS_LF_OFF, 0x00000BCF);

	val = dw_pcie6_readl_dbi(pci, PCIEG6_PF0_GEN3_EQ_CONTROL_OFF);
	val |= GENMASK(5, 4);
	dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_CONTROL_OFF, val);

	val = dw_pcie6_readl_dbi(pci, PCIEG6_PF0_GEN3_RELATED_OFF);
	val |= (BIT(13) | BIT(10));
	dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_RELATED_OFF, val);

	val = dw_pcie6_readl_dbi(pci, PCIEG6_PF0_GEN3_EQ_CONTROL_OFF);
	val &= ~GENMASK(23, 8);
	val |= (0x2 << 8);
	dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_CONTROL_OFF, val);
}

int rcar_gen5_pcie6_monitor_pmd(struct rcar_pcie6 *rcar_pcie6, int num_lanes)
{
	struct dw_pcie6 *pci = rcar_pcie6->pci;
	u32 val;
	int ret;

	for (int X = 0; X < 3; X++) {
		ret = readl_poll_timeout(rcar_pcie6->phy_base + PCIEG6_PMD_RX_OVRDVAL_3(X),
					val, !(val & RX_ACK_OVRDVAL), 1, 999999);

		if (ret) {
			dev_err(pci->dev, "PMD RX_ACK timeout at %d with x%dlanes\n", X, num_lanes);
			return ret;
		}

		ret = readl_poll_timeout(rcar_pcie6->phy_base + PCIEG6_PMD_TX_OVRDVAL_0(X),
					val, !(val & TX_ACK_OVRDVAL), 1, 999999);

		if (ret) {
			dev_err(pci->dev, "PMD TX_ACK timeout at %d with x%dlanes\n", X, num_lanes);
			return ret;
		}
	}

	if (num_lanes == 8) {
		for (int X = 0; X < 3; X++) {
			ret = readl_poll_timeout(rcar_pcie6->phy_shared +
						PCIEG6_PMD_RX_OVRDVAL_3(X),
						val, !(val & RX_ACK_OVRDVAL),
						1, 999999);

			if (ret) {
				dev_err(pci->dev,
					"PMD RX_ACK timeout at %d with x%d lanes\n",
					X, num_lanes);
				return ret;
			}

			ret = readl_poll_timeout(rcar_pcie6->phy_shared +
						PCIEG6_PMD_TX_OVRDVAL_0(X),
						val, !(val & TX_ACK_OVRDVAL),
						1, 999999);

			if (ret) {
				dev_err(pci->dev,
					"PMD TX_ACK timeout at %d with x%d lanes\n",
					X, num_lanes);
				return ret;
			}
		}
	}

	return 0;
}

void rcar_gen5_pcie6_bootload(struct rcar_pcie6 *rcar_pcie6, int num_lanes, u32 channel)
{
	struct dw_pcie6 *pci = rcar_pcie6->pci;
	u32 val;
	int ret;
	bool boot_done = false;

	if (channel == 0) {
		if (num_lanes == 8) {
			/* 13..16. Store Firmware to SRAM */
			rcar_gen5_pcie6_fwupdate(rcar_pcie6, num_lanes, channel);

			writel(0x00181FE6, rcar_pcie6->phy_base + 0x20174);
			writel(0x00181FE6, rcar_pcie6->phy_base + 0x201B4);
			writel(0x00181FE6, rcar_pcie6->phy_base + 0x201F4);
			writel(0x00181FE6, rcar_pcie6->phy_base + 0x20234);

			/* 17. Deassert PHY reset */
			val = readl(rcar_pcie6->base + PCI6RESETC);
			val |= PIPE_LANE_RESET_0_3 | PIPE_LANE_RESET_4_7;
			writel(val, rcar_pcie6->base + PCI6RESETC);

			for (int i = 0; i < 1000; i++) {
				val = readl(rcar_pcie6->base + PCIE6BOOTLC);
				if ((val & BIT(1)) == 0) {
					boot_done = true;
					break;
				}
			}

			if (!boot_done)
				dev_info(pci->dev,
					"Timeout: BootLoader failed to complete on PCIe6_ch%d\n",
					channel);
			else
				dev_info(pci->dev,
					"BootLoader load complete on PCIe6_ch%d\n",
					channel);

			dw_pcie6_writel_dbi(pci, PCIEG6_PF0_PHY_CONTROL_OFF,
						BIT(15) | GENMASK(13, 12) | BIT(6));
			dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_RELATED_OFF, BIT(9));

			/* 18. Execute CPU request cpu to run*/
			val = readl(rcar_pcie6->base + PCI6CPUCTLSTS);
			val |= (PHY0_CPU_RUN_REQ_INT | PHY1_CPU_RUN_REQ_INT);
			writel(val, rcar_pcie6->base + PCI6CPUCTLSTS);

			ret = readl_poll_timeout(rcar_pcie6->base + PCI6CPUCTLSTS,
						val,
						(val & PHY_CPU_RUN_ACK_MASK) ==
						PHY_CPU_RUN_ACK_MASK,
						1, 1000000);
			if (ret)
				dev_info(pci->dev,
					"Timeout: CPU failed to start programmed sequence on PCIe6_ch%d\n",
					channel);

			val = readl(rcar_pcie6->base + PCI6CPUCTLSTS);
			val &= ~(PHY0_CPU_RUN_REQ_INT | PHY1_CPU_RUN_REQ_INT);
			writel(val, rcar_pcie6->base + PCI6CPUCTLSTS);

			ret = readl_poll_timeout(rcar_pcie6->base + PCI6CPUCTLSTS,
						val,
						(val & PHY_CPU_RUN_ACK_MASK) == 0x00,
						1, 1000000);
			if (ret)
				dev_info(pci->dev,
					"Timeout: CPU failed to start programmed sequence on PCIe6_ch%d\n",
					channel);

			dev_info(pci->dev, "CPU start programming sequence on PCIe6_ch%d\n",
				channel);
		} else {
			/* 13..16. Store Firmware to SRAM */
			rcar_gen5_pcie6_fwupdate(rcar_pcie6, num_lanes, channel);

			writel(0x00181FE6, rcar_pcie6->phy_base + 0x20174);
			writel(0x00181FE6, rcar_pcie6->phy_base + 0x201B4);
			writel(0x00181FE6, rcar_pcie6->phy_base + 0x201F4);
			writel(0x00181FE6, rcar_pcie6->phy_base + 0x20234);

			/* 17. Deassert PHY reset */
			val = readl(rcar_pcie6->base + PCI6RESETC);
			val |= PIPE_LANE_RESET_0_3;
			writel(val, rcar_pcie6->base + PCI6RESETC);

			for (int i = 0; i < 1000; i++) {
				val = readl(rcar_pcie6->base + PCIE6BOOTLC);
				if ((val & BIT(1)) == 0) {
					boot_done = true;
					break;
				}
			}

			if (!boot_done)
				dev_info(pci->dev,
					"Timeout: BootLoader failed to complete on PCIe6_ch%d\n",
					channel);
			else
				dev_info(pci->dev,
					"BootLoader load complete on PCIe6_ch%d\n",
					channel);

			dw_pcie6_writel_dbi(pci, PCIEG6_PF0_PHY_CONTROL_OFF,
						BIT(15) | GENMASK(13, 12) | BIT(6));
			dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_RELATED_OFF, BIT(9));

			/* 18. Execute CPU request cpu to run*/
			val = readl(rcar_pcie6->base + PCI6CPUCTLSTS);
			val |= PHY0_CPU_RUN_REQ_INT;
			writel(val, rcar_pcie6->base + PCI6CPUCTLSTS);

			ret = readl_poll_timeout(rcar_pcie6->base + PCI6CPUCTLSTS,
						val,
						(val & PHY0_CPU_RUN_ACK_OUT) ==
						PHY0_CPU_RUN_ACK_OUT,
						1, 1000000);
			if (ret)
				dev_info(pci->dev,
					"Timeout: CPU failed to start programmed sequence on PCIe6_ch%d\n",
					channel);

			val = readl(rcar_pcie6->base + PCI6CPUCTLSTS);
			val &= ~PHY0_CPU_RUN_REQ_INT;
			writel(val, rcar_pcie6->base + PCI6CPUCTLSTS);

			ret = readl_poll_timeout(rcar_pcie6->base + PCI6CPUCTLSTS,
						val,
						(val & PHY0_CPU_RUN_ACK_OUT) == 0x00,
						1, 1000000);
			if (ret)
				dev_info(pci->dev,
					"Timeout: CPU failed to start programmed sequence on PCIe6_ch%d\n",
					channel);

			dev_info(pci->dev, "CPU start programming sequence on PCIe6_ch%d\n",
				channel);
		}
	} else {
		/* 13..16. Store Firmware to SRAM */
		rcar_gen5_pcie6_fwupdate(rcar_pcie6, num_lanes, channel);

		writel(0x00181FE6, rcar_pcie6->phy_base + 0x20174);
		writel(0x00181FE6, rcar_pcie6->phy_base + 0x201B4);
		writel(0x00181FE6, rcar_pcie6->phy_base + 0x201F4);
		writel(0x00181FE6, rcar_pcie6->phy_base + 0x20234);

		/* 17. Deassert PHY reset */
		val = readl(rcar_pcie6->base_shared + PCI6RESETC);
		val |= PIPE_LANE_RESET_4_7;
		writel(val, rcar_pcie6->base_shared + PCI6RESETC);

		for (int i = 0; i < 1000; i++) {
			val = readl(rcar_pcie6->base_shared + PCIE6BOOTLC);
			if ((val & BIT(1)) == 0) {
				boot_done = true;
				break;
			}
		}

		if (!boot_done)
			dev_info(pci->dev,
				"Timeout: BootLoader failed to complete on PCIe6_ch%d\n",
				channel);
		else
			dev_info(pci->dev,
				"BootLoader load complete on PCIe6_ch%d\n",
				channel);

		dw_pcie6_writel_dbi(pci, PCIEG6_PF0_PHY_CONTROL_OFF,
					BIT(15) | GENMASK(13, 12) | BIT(6));
		dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_RELATED_OFF, BIT(9));

		/* 18. Execute CPU request cpu to run*/
		val = readl(rcar_pcie6->base_shared + PCI6CPUCTLSTS);
		val |= PHY1_CPU_RUN_REQ_INT;
		writel(val, rcar_pcie6->base_shared + PCI6CPUCTLSTS);

		ret = readl_poll_timeout(rcar_pcie6->base_shared + PCI6CPUCTLSTS,
					val,
					(val & PHY1_CPU_RUN_ACK_OUT) ==
					PHY1_CPU_RUN_ACK_OUT,
					1, 1000000);
		if (ret)
			dev_info(pci->dev,
				"Timeout: CPU failed to start programmed sequence on PCIe6_ch%d\n",
				channel);

		val = readl(rcar_pcie6->base_shared + PCI6CPUCTLSTS);
		val &= ~PHY1_CPU_RUN_REQ_INT;
		writel(val, rcar_pcie6->base_shared + PCI6CPUCTLSTS);

		ret = readl_poll_timeout(rcar_pcie6->base_shared + PCI6CPUCTLSTS,
					val,
					(val & PHY1_CPU_RUN_ACK_OUT) == 0x00,
					1, 1000000);
		if (ret) {
			dev_info(pci->dev,
				"Timeout: CPU failed to start programmed sequence on PCIe6_ch%d\n",
				channel);

		dev_info(pci->dev, "CPU start programming sequence on PCIe6_ch%d\n",
			channel);
		}
	}
}

static void rcar_gen5_pcie6_ltssm_enable(struct rcar_pcie6 *rcar_pcie6,
					 bool enable)
{
	u32 val;

	val = readl(rcar_pcie6->base + PCIERSTCTRL1);
	if (enable) {
		val |= APP_LTSSM_ENABLE;
		val &= ~BIT(16);
	} else {
		val &= ~APP_LTSSM_ENABLE;
		val |= BIT(16);
	}
	writel(val, rcar_pcie6->base + PCIERSTCTRL1);
}

static void rcar_gen5_pcie6_retrain_link(struct dw_pcie6 *pci)
{
	u32 val, lnksta, retries;

	val = dw_pcie6_readl_dbi(pci, EXPCAP(PCI_EXP_LNKCTL));
	val |= PCI_EXP_LNKCTL_RL;
	dw_pcie6_writel_dbi(pci, EXPCAP(PCI_EXP_LNKCTL), val);

	/* Wait for link retrain */
	for (retries = 0; retries <= 10; retries++) {
		lnksta = dw_pcie6_readw_dbi(pci, EXPCAP(PCI_EXP_LNKSTA));

		/* Check retrain flag */
		if (!(lnksta & PCI_EXP_LNKSTA_LT))
			break;
		mdelay(1);
	}	
}

static void rcar_gen5_pcie6_check_speed(struct dw_pcie6 *pci)
{
	u32 lnkcap, lnksta;

	lnkcap = dw_pcie6_readl_dbi(pci, EXPCAP(PCI_EXP_LNKCAP));
	lnksta = dw_pcie6_readw_dbi(pci, EXPCAP(PCI_EXP_LNKSTA));

	if ((lnksta & PCI_EXP_LNKSTA_CLS) != (lnkcap & PCI_EXP_LNKCAP_SLS))
		rcar_gen5_pcie6_retrain_link(pci);
}

static int rcar_gen5_pcie6_link_up(struct dw_pcie6 *pci)
{
	struct rcar_pcie6 *rcar_pcie6 = to_rcar_gen5_pcie6(pci);
	u32 val, mask;

	val = readl(rcar_pcie6->base + PCIEINTSTS0);
	mask = SMLH_RDLH_LINK_UP;

	rcar_gen5_pcie6_check_speed(pci);

	return (val & mask) == mask;
}

static int rcar_gen5_pcie6_start_link(struct dw_pcie6 *pci)
{
	struct rcar_pcie6 *rcar_pcie6 = to_rcar_gen5_pcie6(pci);

	rcar_gen5_pcie6_ltssm_enable(rcar_pcie6, true);

	return 0;
}

static const struct dw_pcie6_ops dw_pcie6_ops = {
	.start_link = rcar_gen5_pcie6_start_link,
	.link_up = rcar_gen5_pcie6_link_up,
};

int rcar_gen5_pcie6_get_link_speed(struct device_node *node)
{
	u32 max_link_speed;

	if (of_property_read_u32(node, "max-link-speed", &max_link_speed) ||
		max_link_speed == 0 || max_link_speed > 6)
			return -EINVAL;

	return max_link_speed;
}

void rcar_gen5_pcie6_refres_phy1(struct rcar_pcie6 *rcar_pcie6, int channel)
{
	u32	val;

	if (channel == 0) {
		val = readl(rcar_pcie6->base + PCI6RESCODE1);
		val &= ~PHY1_CM0_RESCAL_MODE_INT;
		writel(val, rcar_pcie6->base + PCI6RESCODE1);
	} else {
		val = readl(rcar_pcie6->base_shared + PCI6RESCODE1);
		val &= ~PHY1_CM0_RESCAL_MODE_INT;
		writel(val, rcar_pcie6->base_shared + PCI6RESCODE1);
	}
}

void rcar_gen5_pcie6_refclk_phy1(struct rcar_pcie6 *rcar_pcie6, int channel)
{
	u32 val;

	if (channel == 0) {
		val = readl(rcar_pcie6->base + PCI6PY0REFCLK);
		val &= ~PHY0_REF0_REPEAT_CLK_EN_INT;
		writel(val, rcar_pcie6->base + PCI6PY0REFCLK);
		val &= ~PHY1_REF0_REPEAT_CLK_EN_INT;
		writel(val, rcar_pcie6->base + PCI6PY0REFCLK);
		val |= BIT(28);
		writel(val, rcar_pcie6->base + PCI6PY0REFCLK);
	} else {
		val = readl(rcar_pcie6->base_shared + PCI6PY0REFCLK);
		val &= ~PHY0_REF0_REPEAT_CLK_EN_INT;
		writel(val, rcar_pcie6->base_shared + PCI6PY0REFCLK);
		val &= ~PHY1_REF0_REPEAT_CLK_EN_INT;
		writel(val, rcar_pcie6->base_shared + PCI6PY0REFCLK);
		val |= BIT(28);
		writel(val, rcar_pcie6->base_shared + PCI6PY0REFCLK);
	}
	/* clk_det_en for debug */
	val = readl(rcar_pcie6->base + PCI6PY0REFCLK);
	val |= CLK_DET_EN_INT;
	writel(val, rcar_pcie6->base + PCI6PY0REFCLK);
}

void rcar_gen5_pcie6_channel_aggregation(struct rcar_pcie6 *rcar_pcie6, int num_lanes, int channel)
{
	u32 val;

	/* Set lane bifurcation */
	if (num_lanes != 8) {
		if (channel == 0) {
			val = readl(rcar_pcie6->base + PCIEMSR0);
			val |= BFRCTNST;
			writel(val, rcar_pcie6->base + PCIEMSR0);

			val = readl(rcar_pcie6->base + PCIE6BOOTLC);
			val |= IB_SEL_OFFSET;
			writel(val, rcar_pcie6->base + PCIE6BOOTLC);
		} else {
			val = readl(rcar_pcie6->base_shared + PCIEMSR0);
			val |= BFRCTNST;
			writel(val, rcar_pcie6->base_shared + PCIEMSR0);

			val = readl(rcar_pcie6->base_shared + PCIE6BOOTLC);
			val |= IB_SEL_OFFSET;
			writel(val, rcar_pcie6->base_shared + PCIE6BOOTLC);
		}
	}
}

void rcar_gen5_pcie6_set_device_type(struct rcar_pcie6 *rcar_pcie6, bool rc)
{
	u32 val;

	/* Set device type to Root or Endpoint */
	val = readl(rcar_pcie6->base + PCIEMSR0);
	if (rc)
		val |= DEVICE_TYPE_RC;
	else
		val |= DEVICE_TYPE_EP;
	writel(val, rcar_pcie6->base + PCIEMSR0);
}

int rcar_gen5_pcie6_get_resources(struct rcar_pcie6 *rcar_pcie6,
					struct platform_device *pdev)
{
	int ret;
	struct dw_pcie6 *pci = rcar_pcie6->pci;
	struct device_node *np = dev_of_node(&pdev->dev);
	struct resource *res;

	pci->ops = &dw_pcie6_ops;

	ret = of_property_read_u32(np, "num-lanes", &pci->num_lanes);
	if (ret) {
		dev_err(&pdev->dev, "Missing num-lanes\n");
		return ret;
	}

	ret = of_property_read_u32(np, "channel-id", &rcar_pcie6->ch);
	if (ret) {
		dev_err(&pdev->dev, "Missing channel-id\n");
		return ret;
	}

	if (rcar_pcie6->ch == 1 && pci->num_lanes > MAX_LANES_CH1) {
		dev_err(&pdev->dev, "PCIe6 ch%d only supports up to %d lanes (got %d)", rcar_pcie6->ch, MAX_LANES_CH1, pci->num_lanes);
		return -EINVAL;
	}

	pci->max_link_speed = rcar_gen5_pcie6_get_link_speed(np);

	/* eDMA region can be mapped to a custom base address */
	if (!pci->edma.reg_base) {
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dma");
		if (res) {
			pci->edma.reg_base = devm_ioremap_resource(pci->dev, res);
			if (IS_ERR(pci->edma.reg_base))
				return PTR_ERR(pci->edma.reg_base);
		} else if (pci->atu_size >= 2 * DEFAULT_DBI_DMA_OFFSET) {
			pci->edma.reg_base = pci->atu_base + DEFAULT_DBI_DMA_OFFSET;
		}
	}

	for (int i = 0; i < PCIE6_RCAR_NUM_RSTS; i++)
		rcar_pcie6->rsts[i].id = rcar_pcie6_reset_ids[i];

	ret = devm_reset_control_bulk_get_exclusive(&pdev->dev,
							PCIE6_RCAR_NUM_RSTS,
							rcar_pcie6->rsts);
	if (ret) {
		dev_err(&pdev->dev, "Failed to get PCIe6 RESETS: %d\n", ret);
		return ret;
	}

	/* Renesas-specific registers */
	rcar_pcie6->base = devm_platform_ioremap_resource_byname(pdev, "apb");
	if (IS_ERR(rcar_pcie6->base))
		return PTR_ERR(rcar_pcie6->base);

	rcar_pcie6->phy_base = devm_platform_ioremap_resource_byname(pdev, "phy");
	if (IS_ERR(rcar_pcie6->phy_base))
		return PTR_ERR(rcar_pcie6->phy_base);

	if (pci->num_lanes == 8) {
		rcar_pcie6->phy_shared = devm_platform_ioremap_resource_byname(pdev, "phy_shared");
		if (IS_ERR(rcar_pcie6->phy_shared))
			return PTR_ERR(rcar_pcie6->phy_shared);

		rcar_pcie6->dbi_shared = devm_platform_ioremap_resource_byname(pdev, "dbi_shared");
		if (IS_ERR(rcar_pcie6->dbi_shared))
			return PTR_ERR(rcar_pcie6->dbi_shared);
	}

	if (rcar_pcie6->ch == 1) {
		rcar_pcie6->base_shared = devm_platform_ioremap_resource_byname(pdev, "apb_shared");
		if (IS_ERR(rcar_pcie6->base_shared))
			return PTR_ERR(rcar_pcie6->base_shared);
	}

	for (int i = 0; i < PCIE6_RCAR_NUM_CLKS; i++)
		rcar_pcie6->clks[i].id = rcar_pcie6_clk_ids[i];

	ret = devm_clk_bulk_get(&pdev->dev, PCIE6_RCAR_NUM_CLKS,
				rcar_pcie6->clks);
	if (ret) {
		dev_err(&pdev->dev, "Failed to get clocks: %d\n", ret);
		return ret;
	}

	ret = request_firmware(&rcar_pcie6->fw_dccm, PCIE6_FW_DATA_DCCM_NAME, &pdev->dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to request firmware dccm: %d\n", ret);
		return ret;
	}

	ret = request_firmware(&rcar_pcie6->fw_iccm, PCIE6_FW_DATA_ICCM_NAME, &pdev->dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to request firmware iccm: %d\n", ret);
		return ret;
	}

	return 0;
}
