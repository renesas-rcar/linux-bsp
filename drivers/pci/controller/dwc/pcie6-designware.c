// SPDX-License-Identifier: GPL-2.0
/*
 * Synopsys DesignWare PCIe host controller driver
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 *		https://www.samsung.com
 *
 * Author: Jingoo Han <jg1.han@samsung.com>
 */

#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/msi.h>
#include <linux/of_address.h>
#include <linux/of_pci.h>
#include <linux/pci_regs.h>
#include <linux/platform_device.h>
#include <linux/align.h>
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma/edma.h>
#include <linux/gpio/consumer.h>
#include <linux/ioport.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/sizes.h>
#include <linux/types.h>
#include <linux/iopoll.h>
#include <linux/bitfield.h>

#include "../../pci.h"
#include "pcie6-designware.h"

#include <linux/pci-epc.h>
#include <linux/pci-epf.h>

/* PCIe6 Designware */

static const char * const dw_pcie6_app_clks[DW_PCIE_NUM_APP_CLKS] = {
	[DW_PCIE_DBI_CLK] = "dbi",
	[DW_PCIE_MSTR_CLK] = "mstr",
	[DW_PCIE_SLV_CLK] = "slv",
};

static const char * const dw_pcie6_core_clks[DW_PCIE_NUM_CORE_CLKS] = {
	[DW_PCIE_PIPE_CLK] = "pipe",
	[DW_PCIE_CORE_CLK] = "core",
	[DW_PCIE_AUX_CLK] = "aux",
	[DW_PCIE_REF_CLK] = "ref",
};

static const char * const dw_pcie6_app_rsts[DW_PCIE_NUM_APP_RSTS] = {
	[DW_PCIE_DBI_RST] = "dbi",
	[DW_PCIE_MSTR_RST] = "mstr",
	[DW_PCIE_SLV_RST] = "slv",
};

static const char * const dw_pcie6_core_rsts[DW_PCIE_NUM_CORE_RSTS] = {
	[DW_PCIE_NON_STICKY_RST] = "non-sticky",
	[DW_PCIE_STICKY_RST] = "sticky",
	[DW_PCIE_CORE_RST] = "core",
	[DW_PCIE_PIPE_RST] = "pipe",
	[DW_PCIE_PHY_RST] = "phy",
	[DW_PCIE_HOT_RST] = "hot",
	[DW_PCIE_PWR_RST] = "pwr",
};

static int dw_pcie6_get_clocks(struct dw_pcie6 *pci)
{
	int i, ret;

	for (i = 0; i < DW_PCIE_NUM_APP_CLKS; i++)
		pci->app_clks[i].id = dw_pcie6_app_clks[i];

	for (i = 0; i < DW_PCIE_NUM_CORE_CLKS; i++)
		pci->core_clks[i].id = dw_pcie6_core_clks[i];

	ret = devm_clk_bulk_get_optional(pci->dev, DW_PCIE_NUM_APP_CLKS,
					 pci->app_clks);
	if (ret)
		return ret;

	return devm_clk_bulk_get_optional(pci->dev, DW_PCIE_NUM_CORE_CLKS,
					  pci->core_clks);
}

static int dw_pcie6_get_resets(struct dw_pcie6 *pci)
{
	int i, ret;

	for (i = 0; i < DW_PCIE_NUM_APP_RSTS; i++)
		pci->app_rsts[i].id = dw_pcie6_app_rsts[i];

	for (i = 0; i < DW_PCIE_NUM_CORE_RSTS; i++)
		pci->core_rsts[i].id = dw_pcie6_core_rsts[i];

	ret = devm_reset_control_bulk_get_optional_shared(pci->dev,
							  DW_PCIE_NUM_APP_RSTS,
							  pci->app_rsts);
	if (ret)
		return ret;

	ret = devm_reset_control_bulk_get_optional_exclusive(pci->dev,
							     DW_PCIE_NUM_CORE_RSTS,
							     pci->core_rsts);
	if (ret)
		return ret;

	pci->pe_rst = devm_gpiod_get_optional(pci->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(pci->pe_rst))
		return PTR_ERR(pci->pe_rst);

	return 0;
}

int dw_pcie6_get_resources(struct dw_pcie6 *pci)
{
	struct platform_device *pdev = to_platform_device(pci->dev);
	struct device_node *np = dev_of_node(pci->dev);
	struct resource *res;
	int ret;

	if (!pci->dbi_base) {
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dbi");
		pci->dbi_base = devm_pci_remap_cfg_resource(pci->dev, res);
		if (IS_ERR(pci->dbi_base))
			return PTR_ERR(pci->dbi_base);
		pci->dbi_phys_addr = res->start;
	}

	/* DBI2 is mainly useful for the endpoint controller */
	if (!pci->dbi_base2) {
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dbi2");
		if (res) {
			pci->dbi_base2 = devm_pci_remap_cfg_resource(pci->dev, res);
			if (IS_ERR(pci->dbi_base2))
				return PTR_ERR(pci->dbi_base2);
		} else {
			pci->dbi_base2 = pci->dbi_base + SZ_4K;
		}
	}

	/* For non-unrolled iATU/eDMA platforms this range will be ignored */
	if (!pci->atu_base) {
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "atu");
		if (res) {
			pci->atu_size = resource_size(res);
			pci->atu_base = devm_ioremap_resource(pci->dev, res);
			if (IS_ERR(pci->atu_base))
				return PTR_ERR(pci->atu_base);
			pci->atu_phys_addr = res->start;
		} else {
			pci->atu_base = pci->dbi_base + DEFAULT_DBI_ATU_OFFSET;
		}
	}

	/* Set a default value suitable for at most 8 in and 8 out windows */
	if (!pci->atu_size)
		pci->atu_size = SZ_4K;

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

	/* LLDD is supposed to manually switch the clocks and resets state */
	if (dw_pcie6_cap_is(pci, REQ_RES)) {
		ret = dw_pcie6_get_clocks(pci);
		if (ret)
			return ret;

		ret = dw_pcie6_get_resets(pci);
		if (ret)
			return ret;
	}

	if (pci->max_link_speed < 1)
		pci->max_link_speed = of_pci_get_max_link_speed(np);

	of_property_read_u32(np, "num-lanes", &pci->num_lanes);

	if (of_property_read_bool(np, "snps,enable-cdm-check"))
		dw_pcie6_cap_set(pci, CDM_CHECK);

	return 0;
}

void dw_pcie6_version_detect(struct dw_pcie6 *pci)
{
	u32 ver;

	/* The content of the CSR is zero on DWC PCIe older than v4.70a */
	ver = dw_pcie6_readl_dbi(pci, PCIE_VERSION_NUMBER);
	if (!ver)
		return;

	if (pci->version && pci->version != ver)
		dev_warn(pci->dev, "Versions don't match (%08x != %08x)\n",
			 pci->version, ver);
	else
		pci->version = ver;

	ver = dw_pcie6_readl_dbi(pci, PCIE_VERSION_TYPE);

	if (pci->type && pci->type != ver)
		dev_warn(pci->dev, "Types don't match (%08x != %08x)\n",
			 pci->type, ver);
	else
		pci->type = ver;
}

/*
 * These interfaces resemble the pci_find_*capability() interfaces, but these
 * are for configuring host controllers, which are bridges *to* PCI devices but
 * are not PCI devices themselves.
 */
static u8 __dw_pcie6_find_next_cap(struct dw_pcie6 *pci, u8 cap_ptr,
				  u8 cap)
{
	u8 cap_id, next_cap_ptr;
	u16 reg;

	if (!cap_ptr)
		return 0;

	reg = dw_pcie6_readw_dbi(pci, cap_ptr);
	cap_id = (reg & 0x00ff);

	if (cap_id > PCI_CAP_ID_MAX)
		return 0;

	if (cap_id == cap)
		return cap_ptr;

	next_cap_ptr = (reg & 0xff00) >> 8;
	return __dw_pcie6_find_next_cap(pci, next_cap_ptr, cap);
}

u8 dw_pcie6_find_capability(struct dw_pcie6 *pci, u8 cap)
{
	u8 next_cap_ptr;
	u16 reg;

	reg = dw_pcie6_readw_dbi(pci, PCI_CAPABILITY_LIST);
	next_cap_ptr = (reg & 0x00ff);

	return __dw_pcie6_find_next_cap(pci, next_cap_ptr, cap);
}
EXPORT_SYMBOL_GPL(dw_pcie6_find_capability);

static u16 dw_pcie6_find_next_ext_capability(struct dw_pcie6 *pci, u16 start,
					    u8 cap)
{
	u32 header;
	int ttl;
	int pos = PCI_CFG_SPACE_SIZE;

	/* minimum 8 bytes per capability */
	ttl = (PCI_CFG_SPACE_EXP_SIZE - PCI_CFG_SPACE_SIZE) / 8;

	if (start)
		pos = start;

	header = dw_pcie6_readl_dbi(pci, pos);
	/*
	 * If we have no capabilities, this is indicated by cap ID,
	 * cap version and next pointer all being 0.
	 */
	if (header == 0)
		return 0;

	while (ttl-- > 0) {
		if (PCI_EXT_CAP_ID(header) == cap && pos != start)
			return pos;

		pos = PCI_EXT_CAP_NEXT(header);
		if (pos < PCI_CFG_SPACE_SIZE)
			break;

		header = dw_pcie6_readl_dbi(pci, pos);
	}

	return 0;
}

u16 dw_pcie6_find_ext_capability(struct dw_pcie6 *pci, u8 cap)
{
	return dw_pcie6_find_next_ext_capability(pci, 0, cap);
}
EXPORT_SYMBOL_GPL(dw_pcie6_find_ext_capability);

int dw_pcie6_read(void __iomem *addr, int size, u32 *val)
{
	if (!IS_ALIGNED((uintptr_t)addr, size)) {
		*val = 0;
		return PCIBIOS_BAD_REGISTER_NUMBER;
	}

	if (size == 4) {
		*val = readl(addr);
	} else if (size == 2) {
		*val = readw(addr);
	} else if (size == 1) {
		*val = readb(addr);
	} else {
		*val = 0;
		return PCIBIOS_BAD_REGISTER_NUMBER;
	}

	return PCIBIOS_SUCCESSFUL;
}
EXPORT_SYMBOL_GPL(dw_pcie6_read);

int dw_pcie6_write(void __iomem *addr, int size, u32 val)
{
	if (!IS_ALIGNED((uintptr_t)addr, size))
		return PCIBIOS_BAD_REGISTER_NUMBER;

	if (size == 4)
		writel(val, addr);
	else if (size == 2)
		writew(val, addr);
	else if (size == 1)
		writeb(val, addr);
	else
		return PCIBIOS_BAD_REGISTER_NUMBER;

	return PCIBIOS_SUCCESSFUL;
}
EXPORT_SYMBOL_GPL(dw_pcie6_write);

u32 dw_pcie6_read_dbi(struct dw_pcie6 *pci, u32 reg, size_t size)
{
	int ret;
	u32 val;

	if (pci->ops && pci->ops->read_dbi)
		return pci->ops->read_dbi(pci, pci->dbi_base, reg, size);

	ret = dw_pcie6_read(pci->dbi_base + reg, size, &val);
	if (ret)
		dev_err(pci->dev, "Read DBI address failed\n");

	return val;
}
EXPORT_SYMBOL_GPL(dw_pcie6_read_dbi);

void dw_pcie6_write_dbi(struct dw_pcie6 *pci, u32 reg, size_t size, u32 val)
{
	int ret;

	if (pci->ops && pci->ops->write_dbi) {
		pci->ops->write_dbi(pci, pci->dbi_base, reg, size, val);
		return;
	}

	ret = dw_pcie6_write(pci->dbi_base + reg, size, val);
	if (ret)
		dev_err(pci->dev, "Write DBI address failed\n");
}
EXPORT_SYMBOL_GPL(dw_pcie6_write_dbi);

void dw_pcie6_write_dbi2(struct dw_pcie6 *pci, u32 reg, size_t size, u32 val)
{
	int ret;

	if (pci->ops && pci->ops->write_dbi2) {
		pci->ops->write_dbi2(pci, pci->dbi_base2, reg, size, val);
		return;
	}

	ret = dw_pcie6_write(pci->dbi_base2 + reg, size, val);
	if (ret)
		dev_err(pci->dev, "write DBI address failed\n");
}
EXPORT_SYMBOL_GPL(dw_pcie6_write_dbi2);

static inline void __iomem *dw_pcie6_select_atu(struct dw_pcie6 *pci, u32 dir,
					       u32 index)
{
	if (dw_pcie6_cap_is(pci, IATU_UNROLL))
		return pci->atu_base + PCIE_ATU_UNROLL_BASE(dir, index);

	dw_pcie6_writel_dbi(pci, PCIE_ATU_VIEWPORT, dir | index);
	return pci->atu_base;
}

static u32 dw_pcie6_readl_atu(struct dw_pcie6 *pci, u32 dir, u32 index, u32 reg)
{
	void __iomem *base;
	int ret;
	u32 val;

	base = dw_pcie6_select_atu(pci, dir, index);

	if (pci->ops && pci->ops->read_dbi)
		return pci->ops->read_dbi(pci, base, reg, 4);

	ret = dw_pcie6_read(base + reg, 4, &val);
	if (ret)
		dev_err(pci->dev, "Read ATU address failed\n");

	return val;
}

static void dw_pcie6_writel_atu(struct dw_pcie6 *pci, u32 dir, u32 index,
			       u32 reg, u32 val)
{
	void __iomem *base;
	int ret;

	base = dw_pcie6_select_atu(pci, dir, index);

	if (pci->ops && pci->ops->write_dbi) {
		pci->ops->write_dbi(pci, base, reg, 4, val);
		return;
	}

	ret = dw_pcie6_write(base + reg, 4, val);
	if (ret)
		dev_err(pci->dev, "Write ATU address failed\n");
}

static inline u32 dw_pcie6_readl_atu_ob(struct dw_pcie6 *pci, u32 index, u32 reg)
{
	return dw_pcie6_readl_atu(pci, PCIE_ATU_REGION_DIR_OB, index, reg);
}

static inline void dw_pcie6_writel_atu_ob(struct dw_pcie6 *pci, u32 index, u32 reg,
					 u32 val)
{
	dw_pcie6_writel_atu(pci, PCIE_ATU_REGION_DIR_OB, index, reg, val);
}

static inline u32 dw_pcie6_enable_ecrc(u32 val)
{
	/*
	 * DesignWare core version 4.90A has a design issue where the 'TD'
	 * bit in the Control register-1 of the ATU outbound region acts
	 * like an override for the ECRC setting, i.e., the presence of TLP
	 * Digest (ECRC) in the outgoing TLPs is solely determined by this
	 * bit. This is contrary to the PCIe spec which says that the
	 * enablement of the ECRC is solely determined by the AER
	 * registers.
	 *
	 * Because of this, even when the ECRC is enabled through AER
	 * registers, the transactions going through ATU won't have TLP
	 * Digest as there is no way the PCI core AER code could program
	 * the TD bit which is specific to the DesignWare core.
	 *
	 * The best way to handle this scenario is to program the TD bit
	 * always. It affects only the traffic from root port to downstream
	 * devices.
	 *
	 * At this point,
	 * When ECRC is enabled in AER registers, everything works normally
	 * When ECRC is NOT enabled in AER registers, then,
	 * on Root Port:- TLP Digest (DWord size) gets appended to each packet
	 *                even through it is not required. Since downstream
	 *                TLPs are mostly for configuration accesses and BAR
	 *                accesses, they are not in critical path and won't
	 *                have much negative effect on the performance.
	 * on End Point:- TLP Digest is received for some/all the packets coming
	 *                from the root port. TLP Digest is ignored because,
	 *                as per the PCIe Spec r5.0 v1.0 section 2.2.3
	 *                "TLP Digest Rules", when an endpoint receives TLP
	 *                Digest when its ECRC check functionality is disabled
	 *                in AER registers, received TLP Digest is just ignored.
	 * Since there is no issue or error reported either side, best way to
	 * handle the scenario is to program TD bit by default.
	 */

	return val | PCIE_ATU_TD;
}

int dw_pcie6_prog_outbound_atu(struct dw_pcie6 *pci,
			      const struct dw_pcie6_ob_atu_cfg *atu)
{
	u64 cpu_addr = atu->cpu_addr;
	u32 retries, val;
	u64 limit_addr;

	if (pci->ops && pci->ops->cpu_addr_fixup)
		cpu_addr = pci->ops->cpu_addr_fixup(pci, cpu_addr);

	limit_addr = cpu_addr + atu->size - 1;

	if ((limit_addr & ~pci->region_limit) != (cpu_addr & ~pci->region_limit) ||
	    !IS_ALIGNED(cpu_addr, pci->region_align) ||
	    !IS_ALIGNED(atu->pci_addr, pci->region_align) || !atu->size) {
		return -EINVAL;
	}

	dw_pcie6_writel_atu_ob(pci, atu->index, PCIE_ATU_LOWER_BASE,
			      lower_32_bits(cpu_addr));
	dw_pcie6_writel_atu_ob(pci, atu->index, PCIE_ATU_UPPER_BASE,
			      upper_32_bits(cpu_addr));

	dw_pcie6_writel_atu_ob(pci, atu->index, PCIE_ATU_LIMIT,
			      lower_32_bits(limit_addr));
	if (dw_pcie6_ver_is_ge(pci, 460A))
		dw_pcie6_writel_atu_ob(pci, atu->index, PCIE_ATU_UPPER_LIMIT,
				      upper_32_bits(limit_addr));

	dw_pcie6_writel_atu_ob(pci, atu->index, PCIE_ATU_LOWER_TARGET,
			      lower_32_bits(atu->pci_addr));
	dw_pcie6_writel_atu_ob(pci, atu->index, PCIE_ATU_UPPER_TARGET,
			      upper_32_bits(atu->pci_addr));

	val = atu->type | atu->routing | PCIE_ATU_FUNC_NUM(atu->func_no);
	if (upper_32_bits(limit_addr) > upper_32_bits(cpu_addr) &&
	    dw_pcie6_ver_is_ge(pci, 460A))
		val |= PCIE_ATU_INCREASE_REGION_SIZE;
	if (dw_pcie6_ver_is(pci, 490A))
		val = dw_pcie6_enable_ecrc(val);
	dw_pcie6_writel_atu_ob(pci, atu->index, PCIE_ATU_REGION_CTRL1, val);

	val = PCIE_ATU_ENABLE;
	if (atu->type == PCIE_ATU_TYPE_MSG) {
		/* The data-less messages only for now */
		val |= PCIE_ATU_INHIBIT_PAYLOAD | atu->code;
	}
	dw_pcie6_writel_atu_ob(pci, atu->index, PCIE_ATU_REGION_CTRL2, val);

	/*
	 * Make sure ATU enable takes effect before any subsequent config
	 * and I/O accesses.
	 */
	for (retries = 0; retries < LINK_WAIT_MAX_IATU_RETRIES; retries++) {
		val = dw_pcie6_readl_atu_ob(pci, atu->index, PCIE_ATU_REGION_CTRL2);
		if (val & PCIE_ATU_ENABLE)
			return 0;

		mdelay(LINK_WAIT_IATU);
	}

	dev_err(pci->dev, "Outbound iATU is not being enabled\n");

	return -ETIMEDOUT;
}

static inline u32 dw_pcie6_readl_atu_ib(struct dw_pcie6 *pci, u32 index, u32 reg)
{
	return dw_pcie6_readl_atu(pci, PCIE_ATU_REGION_DIR_IB, index, reg);
}

static inline void dw_pcie6_writel_atu_ib(struct dw_pcie6 *pci, u32 index, u32 reg,
					 u32 val)
{
	dw_pcie6_writel_atu(pci, PCIE_ATU_REGION_DIR_IB, index, reg, val);
}

int dw_pcie6_prog_inbound_atu(struct dw_pcie6 *pci, int index, int type,
			     u64 cpu_addr, u64 pci_addr, u64 size)
{
	u64 limit_addr = pci_addr + size - 1;
	u32 retries, val;

	if ((limit_addr & ~pci->region_limit) != (pci_addr & ~pci->region_limit) ||
	    !IS_ALIGNED(cpu_addr, pci->region_align) ||
	    !IS_ALIGNED(pci_addr, pci->region_align) || !size) {
		return -EINVAL;
	}

	dw_pcie6_writel_atu_ib(pci, index, PCIE_ATU_LOWER_BASE,
			      lower_32_bits(pci_addr));
	dw_pcie6_writel_atu_ib(pci, index, PCIE_ATU_UPPER_BASE,
			      upper_32_bits(pci_addr));

	dw_pcie6_writel_atu_ib(pci, index, PCIE_ATU_LIMIT,
			      lower_32_bits(limit_addr));
	if (dw_pcie6_ver_is_ge(pci, 460A))
		dw_pcie6_writel_atu_ib(pci, index, PCIE_ATU_UPPER_LIMIT,
				      upper_32_bits(limit_addr));

	dw_pcie6_writel_atu_ib(pci, index, PCIE_ATU_LOWER_TARGET,
			      lower_32_bits(cpu_addr));
	dw_pcie6_writel_atu_ib(pci, index, PCIE_ATU_UPPER_TARGET,
			      upper_32_bits(cpu_addr));

	val = type;
	if (upper_32_bits(limit_addr) > upper_32_bits(pci_addr) &&
	    dw_pcie6_ver_is_ge(pci, 460A))
		val |= PCIE_ATU_INCREASE_REGION_SIZE;
	dw_pcie6_writel_atu_ib(pci, index, PCIE_ATU_REGION_CTRL1, val);
	dw_pcie6_writel_atu_ib(pci, index, PCIE_ATU_REGION_CTRL2, PCIE_ATU_ENABLE);

	/*
	 * Make sure ATU enable takes effect before any subsequent config
	 * and I/O accesses.
	 */
	for (retries = 0; retries < LINK_WAIT_MAX_IATU_RETRIES; retries++) {
		val = dw_pcie6_readl_atu_ib(pci, index, PCIE_ATU_REGION_CTRL2);
		if (val & PCIE_ATU_ENABLE)
			return 0;

		mdelay(LINK_WAIT_IATU);
	}

	dev_err(pci->dev, "Inbound iATU is not being enabled\n");

	return -ETIMEDOUT;
}

int dw_pcie6_prog_ep_inbound_atu(struct dw_pcie6 *pci, u8 func_no, int index,
				int type, u64 cpu_addr, u8 bar)
{
	u32 retries, val;

	if (!IS_ALIGNED(cpu_addr, pci->region_align))
		return -EINVAL;

	dw_pcie6_writel_atu_ib(pci, index, PCIE_ATU_LOWER_TARGET,
			      lower_32_bits(cpu_addr));
	dw_pcie6_writel_atu_ib(pci, index, PCIE_ATU_UPPER_TARGET,
			      upper_32_bits(cpu_addr));

	dw_pcie6_writel_atu_ib(pci, index, PCIE_ATU_REGION_CTRL1, type |
			      PCIE_ATU_FUNC_NUM(func_no));
	dw_pcie6_writel_atu_ib(pci, index, PCIE_ATU_REGION_CTRL2,
			      PCIE_ATU_ENABLE | PCIE_ATU_FUNC_NUM_MATCH_EN |
			      PCIE_ATU_BAR_MODE_ENABLE | (bar << 8));

	/*
	 * Make sure ATU enable takes effect before any subsequent config
	 * and I/O accesses.
	 */
	for (retries = 0; retries < LINK_WAIT_MAX_IATU_RETRIES; retries++) {
		val = dw_pcie6_readl_atu_ib(pci, index, PCIE_ATU_REGION_CTRL2);
		if (val & PCIE_ATU_ENABLE)
			return 0;

		mdelay(LINK_WAIT_IATU);
	}

	dev_err(pci->dev, "Inbound iATU is not being enabled\n");

	return -ETIMEDOUT;
}

void dw_pcie6_disable_atu(struct dw_pcie6 *pci, u32 dir, int index)
{
	dw_pcie6_writel_atu(pci, dir, index, PCIE_ATU_REGION_CTRL2, 0);
}

int dw_pcie6_wait_for_link(struct dw_pcie6 *pci)
{
	u32 offset, val;
	int retries;

	/* Check if the link is up or not */
	for (retries = 0; retries < LINK_WAIT_MAX_RETRIES; retries++) {
		if (dw_pcie6_link_up(pci))
			break;

		msleep(LINK_WAIT_SLEEP_MS);
	}

	if (retries >= LINK_WAIT_MAX_RETRIES) {
		dev_info(pci->dev, "Phy link never came up\n");
		return -ETIMEDOUT;
	}

	/*
	 * As per PCIe r6.0, sec 6.6.1, a Downstream Port that supports Link
	 * speeds greater than 5.0 GT/s, software must wait a minimum of 100 ms
	 * after Link training completes before sending a Configuration Request.
	 */
	if (pci->max_link_speed > 2)
		msleep(PCIE_RESET_CONFIG_WAIT_MS);

	offset = dw_pcie6_find_capability(pci, PCI_CAP_ID_EXP);
	val = dw_pcie6_readw_dbi(pci, offset + PCI_EXP_LNKSTA);

	dev_info(pci->dev, "PCIe Gen.%u x%u link up\n",
		 FIELD_GET(PCI_EXP_LNKSTA_CLS, val),
		 FIELD_GET(PCI_EXP_LNKSTA_NLW, val));

	return 0;
}
EXPORT_SYMBOL_GPL(dw_pcie6_wait_for_link);

int dw_pcie6_link_up(struct dw_pcie6 *pci)
{
	u32 val;

	if (pci->ops && pci->ops->link_up)
		return pci->ops->link_up(pci);

	val = dw_pcie6_readl_dbi(pci, PCIE_PORT_DEBUG1);
	return ((val & PCIE_PORT_DEBUG1_LINK_UP) &&
		(!(val & PCIE_PORT_DEBUG1_LINK_IN_TRAINING)));
}
EXPORT_SYMBOL_GPL(dw_pcie6_link_up);

void dw_pcie6_upconfig_setup(struct dw_pcie6 *pci)
{
	u32 val;

	val = dw_pcie6_readl_dbi(pci, PCIE_PORT_MULTI_LANE_CTRL);
	val |= PORT_MLTI_UPCFG_SUPPORT;
	dw_pcie6_writel_dbi(pci, PCIE_PORT_MULTI_LANE_CTRL, val);
}
EXPORT_SYMBOL_GPL(dw_pcie6_upconfig_setup);

static void dw_pcie6_link_set_max_speed(struct dw_pcie6 *pci)
{
	u32 cap, ctrl2, link_speed;
	u8 offset = dw_pcie6_find_capability(pci, PCI_CAP_ID_EXP);

	cap = dw_pcie6_readl_dbi(pci, offset + PCI_EXP_LNKCAP);

	/*
	 * Even if the platform doesn't want to limit the maximum link speed,
	 * just cache the hardware default value so that the vendor drivers can
	 * use it to do any link specific configuration.
	 */
	if (pci->max_link_speed < 1) {
		pci->max_link_speed = FIELD_GET(PCI_EXP_LNKCAP_SLS, cap);
		return;
	}

	ctrl2 = dw_pcie6_readl_dbi(pci, offset + PCI_EXP_LNKCTL2);
	ctrl2 &= ~PCI_EXP_LNKCTL2_TLS;

	switch (pcie_link_speed[pci->max_link_speed]) {
	case PCIE_SPEED_2_5GT:
		link_speed = PCI_EXP_LNKCTL2_TLS_2_5GT;
		break;
	case PCIE_SPEED_5_0GT:
		link_speed = PCI_EXP_LNKCTL2_TLS_5_0GT;
		break;
	case PCIE_SPEED_8_0GT:
		link_speed = PCI_EXP_LNKCTL2_TLS_8_0GT;
		break;
	case PCIE_SPEED_16_0GT:
		link_speed = PCI_EXP_LNKCTL2_TLS_16_0GT;
		break;
	default:
		/* Use hardware capability */
		link_speed = FIELD_GET(PCI_EXP_LNKCAP_SLS, cap);
		ctrl2 &= ~PCI_EXP_LNKCTL2_HASD;
		break;
	}

	dw_pcie6_writel_dbi(pci, offset + PCI_EXP_LNKCTL2, ctrl2 | link_speed);

	cap &= ~((u32)PCI_EXP_LNKCAP_SLS);
	dw_pcie6_writel_dbi(pci, offset + PCI_EXP_LNKCAP, cap | link_speed);

}

static void dw_pcie6_link_set_max_link_width(struct dw_pcie6 *pci, u32 num_lanes)
{
	u32 lnkcap, lwsc, plc;
	u8 cap;

	if (!num_lanes)
		return;

	/* Set the number of lanes */
	plc = dw_pcie6_readl_dbi(pci, PCIE_PORT_LINK_CONTROL);
	plc &= ~PORT_LINK_FAST_LINK_MODE;
	plc &= ~PORT_LINK_MODE_MASK;

	/* Set link width speed control register */
	lwsc = dw_pcie6_readl_dbi(pci, PCIE_LINK_WIDTH_SPEED_CONTROL);
	lwsc &= ~PORT_LOGIC_LINK_WIDTH_MASK;
	lwsc |= PORT_LOGIC_LINK_WIDTH_1_LANES;
	switch (num_lanes) {
	case 1:
		plc |= PORT_LINK_MODE_1_LANES;
		break;
	case 2:
		plc |= PORT_LINK_MODE_2_LANES;
		break;
	case 4:
		plc |= PORT_LINK_MODE_4_LANES;
		break;
	case 8:
		plc |= PORT_LINK_MODE_8_LANES;
		break;
	default:
		dev_err(pci->dev, "num-lanes %u: invalid value\n", num_lanes);
		return;
	}
	dw_pcie6_writel_dbi(pci, PCIE_PORT_LINK_CONTROL, plc);
	dw_pcie6_writel_dbi(pci, PCIE_LINK_WIDTH_SPEED_CONTROL, lwsc);

	cap = dw_pcie6_find_capability(pci, PCI_CAP_ID_EXP);
	lnkcap = dw_pcie6_readl_dbi(pci, cap + PCI_EXP_LNKCAP);
	lnkcap &= ~PCI_EXP_LNKCAP_MLW;
	lnkcap |= FIELD_PREP(PCI_EXP_LNKCAP_MLW, num_lanes);
	dw_pcie6_writel_dbi(pci, cap + PCI_EXP_LNKCAP, lnkcap);
}

void dw_pcie6_iatu_detect(struct dw_pcie6 *pci)
{
	int max_region, ob, ib;
	u32 val, min, dir;
	u64 max;

	val = dw_pcie6_readl_dbi(pci, PCIE_ATU_VIEWPORT);
	if (val == 0xFFFFFFFF) {
		dw_pcie6_cap_set(pci, IATU_UNROLL);

		max_region = min((int)pci->atu_size / 512, 256);
	} else {
		pci->atu_base = pci->dbi_base + PCIE_ATU_VIEWPORT_BASE;
		pci->atu_size = PCIE_ATU_VIEWPORT_SIZE;

		dw_pcie6_writel_dbi(pci, PCIE_ATU_VIEWPORT, 0xFF);
		max_region = dw_pcie6_readl_dbi(pci, PCIE_ATU_VIEWPORT) + 1;
	}

	for (ob = 0; ob < max_region; ob++) {
		dw_pcie6_writel_atu_ob(pci, ob, PCIE_ATU_LOWER_TARGET, 0x11110000);
		val = dw_pcie6_readl_atu_ob(pci, ob, PCIE_ATU_LOWER_TARGET);
		if (val != 0x11110000)
			break;
	}

	for (ib = 0; ib < max_region; ib++) {
		dw_pcie6_writel_atu_ib(pci, ib, PCIE_ATU_LOWER_TARGET, 0x11110000);
		val = dw_pcie6_readl_atu_ib(pci, ib, PCIE_ATU_LOWER_TARGET);
		if (val != 0x11110000)
			break;
	}

	if (ob) {
		dir = PCIE_ATU_REGION_DIR_OB;
	} else if (ib) {
		dir = PCIE_ATU_REGION_DIR_IB;
	} else {
		dev_err(pci->dev, "No iATU regions found\n");
		return;
	}

	dw_pcie6_writel_atu(pci, dir, 0, PCIE_ATU_LIMIT, 0x0);
	min = dw_pcie6_readl_atu(pci, dir, 0, PCIE_ATU_LIMIT);

	if (dw_pcie6_ver_is_ge(pci, 460A)) {
		dw_pcie6_writel_atu(pci, dir, 0, PCIE_ATU_UPPER_LIMIT, 0xFFFFFFFF);
		max = dw_pcie6_readl_atu(pci, dir, 0, PCIE_ATU_UPPER_LIMIT);
	} else {
		max = 0;
	}

	pci->num_ob_windows = ob;
	pci->num_ib_windows = ib;
	pci->region_align = 1 << fls(min);
	pci->region_limit = (max << 32) | (SZ_4G - 1);

	dev_info(pci->dev, "iATU: unroll %s, %u ob, %u ib, align %uK, limit %lluG\n",
		 dw_pcie6_cap_is(pci, IATU_UNROLL) ? "T" : "F",
		 pci->num_ob_windows, pci->num_ib_windows,
		 pci->region_align / SZ_1K, (pci->region_limit + 1) / SZ_1G);
}

static u32 dw_pcie6_readl_dma(struct dw_pcie6 *pci, u32 reg)
{
	u32 val = 0;
	int ret;

	if (pci->ops && pci->ops->read_dbi)
		return pci->ops->read_dbi(pci, pci->edma.reg_base, reg, 4);

	ret = dw_pcie6_read(pci->edma.reg_base + reg, 4, &val);
	if (ret)
		dev_err(pci->dev, "Read DMA address failed\n");

	return val;
}

static int dw_pcie6_edma_irq_vector(struct device *dev, unsigned int nr)
{
	struct platform_device *pdev = to_platform_device(dev);
	char name[6];
	int ret;

	if (nr >= EDMA_MAX_WR_CH + EDMA_MAX_RD_CH)
		return -EINVAL;

	ret = platform_get_irq_byname_optional(pdev, "dma");
	if (ret > 0)
		return ret;

	snprintf(name, sizeof(name), "dma%u", nr);

	return platform_get_irq_byname_optional(pdev, name);
}

static struct dw_edma_plat_ops dw_pcie6_edma_ops = {
	.irq_vector = dw_pcie6_edma_irq_vector,
};

static void dw_pcie6_edma_init_data(struct dw_pcie6 *pci)
{
	pci->edma.dev = pci->dev;

	if (!pci->edma.ops)
		pci->edma.ops = &dw_pcie6_edma_ops;

	pci->edma.flags |= DW_EDMA_CHIP_LOCAL;
}

static int dw_pcie6_edma_find_mf(struct dw_pcie6 *pci)
{
	u32 val;

	/*
	 * Bail out finding the mapping format if it is already set by the glue
	 * driver. Also ensure that the edma.reg_base is pointing to a valid
	 * memory region.
	 */
	if (pci->edma.mf != EDMA_MF_EDMA_LEGACY)
		return pci->edma.reg_base ? 0 : -ENODEV;

	/*
	 * Indirect eDMA CSRs access has been completely removed since v5.40a
	 * thus no space is now reserved for the eDMA channels viewport and
	 * former DMA CTRL register is no longer fixed to FFs.
	 */
	if (dw_pcie6_ver_is_ge(pci, 540A))
		val = 0xFFFFFFFF;
	else
		val = dw_pcie6_readl_dbi(pci, PCIE_DMA_VIEWPORT_BASE + PCIE_DMA_CTRL);

	if (val == 0xFFFFFFFF && pci->edma.reg_base) {
		pci->edma.mf = EDMA_MF_EDMA_UNROLL;
	} else if (val != 0xFFFFFFFF) {
		pci->edma.mf = EDMA_MF_EDMA_LEGACY;

		pci->edma.reg_base = pci->dbi_base + PCIE_DMA_VIEWPORT_BASE;
	} else {
		return -ENODEV;
	}

	pci->edma.mf = EDMA_MF_HDMA_NATIVE;

	return 0;
}

static int dw_pcie6_edma_find_channels(struct dw_pcie6 *pci)
{
	u32 val;

	/*
	 * Autodetect the read/write channels count only for non-HDMA platforms.
	 * HDMA platforms with native CSR mapping doesn't support autodetect,
	 * so the glue drivers should've passed the valid count already. If not,
	 * the below sanity check will catch it.
	 */
	if (pci->edma.mf != EDMA_MF_HDMA_NATIVE) {
		val = dw_pcie6_readl_dma(pci, PCIE_DMA_CTRL);

		pci->edma.ll_wr_cnt = FIELD_GET(PCIE_DMA_NUM_WR_CHAN, val);
		pci->edma.ll_rd_cnt = FIELD_GET(PCIE_DMA_NUM_RD_CHAN, val);
	}

	/* Sanity check the channels count if the mapping was incorrect */
	if (!pci->edma.ll_wr_cnt || pci->edma.ll_wr_cnt > EDMA_MAX_WR_CH ||
	    !pci->edma.ll_rd_cnt || pci->edma.ll_rd_cnt > EDMA_MAX_RD_CH) {
		pci->edma.ll_wr_cnt = EDMA_MAX_WR_CH;
		pci->edma.ll_rd_cnt = EDMA_MAX_RD_CH;
	}

	return 0;
}

static int dw_pcie6_edma_find_chip(struct dw_pcie6 *pci)
{
	int ret;

	dw_pcie6_edma_init_data(pci);

	ret = dw_pcie6_edma_find_mf(pci);
	if (ret)
		return ret;

	return dw_pcie6_edma_find_channels(pci);
}

static int dw_pcie6_edma_irq_verify(struct dw_pcie6 *pci)
{
	struct platform_device *pdev = to_platform_device(pci->dev);
	u16 ch_cnt = pci->edma.ll_wr_cnt + pci->edma.ll_rd_cnt;
	char name[6];
	int ret;

	if (pci->edma.nr_irqs > 1)
		return pci->edma.nr_irqs != ch_cnt ? -EINVAL : 0;

	ret = platform_get_irq_byname_optional(pdev, "dma");
	if (ret > 0) {
		pci->edma.nr_irqs = 1;
		return 0;
	}

	for (; pci->edma.nr_irqs < ch_cnt; pci->edma.nr_irqs++) {
		snprintf(name, sizeof(name), "dma%d", pci->edma.nr_irqs);

		ret = platform_get_irq_byname_optional(pdev, name);
		if (ret <= 0)
			return -EINVAL;
	}

	return 0;
}

static int dw_pcie6_edma_ll_alloc(struct dw_pcie6 *pci)
{
	struct dw_edma_region *ll;
	dma_addr_t paddr;
	int i;

	for (i = 0; i < pci->edma.ll_wr_cnt; i++) {
		ll = &pci->edma.ll_region_wr[i];
		ll->sz = DMA_LLP_MEM_SIZE;
		ll->vaddr.mem = dmam_alloc_coherent(pci->dev, ll->sz,
						    &paddr, GFP_KERNEL);
		if (!ll->vaddr.mem)
			return -ENOMEM;

		ll->paddr = paddr;
	}

	for (i = 0; i < pci->edma.ll_rd_cnt; i++) {
		ll = &pci->edma.ll_region_rd[i];
		ll->sz = DMA_LLP_MEM_SIZE;
		ll->vaddr.mem = dmam_alloc_coherent(pci->dev, ll->sz,
						    &paddr, GFP_KERNEL);
		if (!ll->vaddr.mem)
			return -ENOMEM;

		ll->paddr = paddr;
	}

	return 0;
}

int dw_pcie6_edma_detect(struct dw_pcie6 *pci)
{
	int ret;

	/* Don't fail if no eDMA was found (for the backward compatibility) */
	ret = dw_pcie6_edma_find_chip(pci);
	if (ret)
		return 0;

	/* Don't fail on the IRQs verification (for the backward compatibility) */
	ret = dw_pcie6_edma_irq_verify(pci);
	if (ret) {
		dev_err(pci->dev, "Invalid eDMA IRQs found\n");
		return 0;
	}

	ret = dw_pcie6_edma_ll_alloc(pci);
	if (ret) {
		dev_err(pci->dev, "Couldn't allocate LLP memory\n");
		return ret;
	}

	/* Don't fail if the DW eDMA driver can't find the device */
	ret = dw_edma_probe(&pci->edma);
	if (ret && ret != -ENODEV) {
		dev_err(pci->dev, "Couldn't register eDMA device\n");
		return ret;
	}

	dev_info(pci->dev, "eDMA: unroll %s, %hu wr, %hu rd\n",
		 pci->edma.mf == EDMA_MF_EDMA_UNROLL ? "T" : "F",
		 pci->edma.ll_wr_cnt, pci->edma.ll_rd_cnt);

	return 0;
}

void dw_pcie6_edma_remove(struct dw_pcie6 *pci)
{
	dw_edma_remove(&pci->edma);
}

void dw_pcie6_setup(struct dw_pcie6 *pci)
{
	u32 val;

	dw_pcie6_link_set_max_speed(pci);

	/* Configure Gen1 N_FTS */
	if (pci->n_fts[0]) {
		val = dw_pcie6_readl_dbi(pci, PCIE_PORT_AFR);
		val &= ~(PORT_AFR_N_FTS_MASK | PORT_AFR_CC_N_FTS_MASK);
		val |= PORT_AFR_N_FTS(pci->n_fts[0]);
		val |= PORT_AFR_CC_N_FTS(pci->n_fts[0]);
		dw_pcie6_writel_dbi(pci, PCIE_PORT_AFR, val);
	}

	/* Configure Gen2+ N_FTS */
	if (pci->n_fts[1]) {
		val = dw_pcie6_readl_dbi(pci, PCIE_LINK_WIDTH_SPEED_CONTROL);
		val &= ~PORT_LOGIC_N_FTS_MASK;
		val |= pci->n_fts[1];
		dw_pcie6_writel_dbi(pci, PCIE_LINK_WIDTH_SPEED_CONTROL, val);
	}

	if (dw_pcie6_cap_is(pci, CDM_CHECK)) {
		val = dw_pcie6_readl_dbi(pci, PCIE_PL_CHK_REG_CONTROL_STATUS);
		val |= PCIE_PL_CHK_REG_CHK_REG_CONTINUOUS |
		       PCIE_PL_CHK_REG_CHK_REG_START;
		dw_pcie6_writel_dbi(pci, PCIE_PL_CHK_REG_CONTROL_STATUS, val);
	}

	val = dw_pcie6_readl_dbi(pci, PCIE_PORT_LINK_CONTROL);
	val &= ~PORT_LINK_FAST_LINK_MODE;
	val |= PORT_LINK_DLL_LINK_EN;
	dw_pcie6_writel_dbi(pci, PCIE_PORT_LINK_CONTROL, val);

	dw_pcie6_link_set_max_link_width(pci, pci->num_lanes);
}

/* PCIe6 Designware Host */

static struct pci_ops dw_pcie6_ops;
static struct pci_ops dw_child_pcie_ops;

static void dw_msi_ack_irq(struct irq_data *d)
{
	irq_chip_ack_parent(d);
}

static void dw_msi_mask_irq(struct irq_data *d)
{
	pci_msi_mask_irq(d);
	irq_chip_mask_parent(d);
}

static void dw_msi_unmask_irq(struct irq_data *d)
{
	pci_msi_unmask_irq(d);
	irq_chip_unmask_parent(d);
}

static struct irq_chip dw_pcie6_msi_irq_chip = {
	.name = "RCAR-GEN5-PCI-MSI",
	.irq_ack = dw_msi_ack_irq,
	.irq_mask = dw_msi_mask_irq,
	.irq_unmask = dw_msi_unmask_irq,
};

static struct msi_domain_info dw_pcie6_msi_domain_info = {
	.flags	= MSI_FLAG_USE_DEF_DOM_OPS | MSI_FLAG_USE_DEF_CHIP_OPS |
		  MSI_FLAG_NO_AFFINITY | MSI_FLAG_PCI_MSIX |
		  MSI_FLAG_MULTI_PCI_MSI,
	.chip	= &dw_pcie6_msi_irq_chip,
};

/* MSI int handler */
irqreturn_t dw_pcie6_handle_msi_irq(struct dw_pcie6_rp *pp)
{
	int i, pos;
	unsigned long val;
	u32 status, num_ctrls;
	irqreturn_t ret = IRQ_NONE;
	struct dw_pcie6 *pci = to_dw_pcie6_from_pp(pp);

	num_ctrls = pp->num_vectors / MAX_MSI_IRQS_PER_CTRL;

	for (i = 0; i < num_ctrls; i++) {
		status = dw_pcie6_readl_dbi(pci, PCIE_MSI_INTR0_STATUS +
					   (i * MSI_REG_CTRL_BLOCK_SIZE));
		if (!status)
			continue;

		ret = IRQ_HANDLED;
		val = status;
		pos = 0;
		while ((pos = find_next_bit(&val, MAX_MSI_IRQS_PER_CTRL,
					    pos)) != MAX_MSI_IRQS_PER_CTRL) {
			generic_handle_domain_irq(pp->irq_domain,
						  (i * MAX_MSI_IRQS_PER_CTRL) +
						  pos);
			pos++;
		}
	}

	return ret;
}

/* Chained MSI interrupt service routine */
static void dw_chained_msi_isr(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct dw_pcie6_rp *pp;

	chained_irq_enter(chip, desc);

	pp = irq_desc_get_handler_data(desc);
	dw_pcie6_handle_msi_irq(pp);

	chained_irq_exit(chip, desc);
}

static void dw_pcie6_setup_msi_msg(struct irq_data *d, struct msi_msg *msg)
{
	struct dw_pcie6_rp *pp = irq_data_get_irq_chip_data(d);
	struct dw_pcie6 *pci = to_dw_pcie6_from_pp(pp);
	u64 msi_target;

	msi_target = (u64)pp->msi_data;

	msg->address_lo = lower_32_bits(msi_target);
	msg->address_hi = upper_32_bits(msi_target);

	msg->data = d->hwirq;

	dev_dbg(pci->dev, "msi#%d address_hi %#x address_lo %#x\n",
		(int)d->hwirq, msg->address_hi, msg->address_lo);
}

static void dw_pcie6_bottom_mask(struct irq_data *d)
{
	struct dw_pcie6_rp *pp = irq_data_get_irq_chip_data(d);
	struct dw_pcie6 *pci = to_dw_pcie6_from_pp(pp);
	unsigned int res, bit, ctrl;
	unsigned long flags;

	raw_spin_lock_irqsave(&pp->lock, flags);

	ctrl = d->hwirq / MAX_MSI_IRQS_PER_CTRL;
	res = ctrl * MSI_REG_CTRL_BLOCK_SIZE;
	bit = d->hwirq % MAX_MSI_IRQS_PER_CTRL;

	pp->irq_mask[ctrl] |= BIT(bit);
	dw_pcie6_writel_dbi(pci, PCIE_MSI_INTR0_MASK + res, pp->irq_mask[ctrl]);

	raw_spin_unlock_irqrestore(&pp->lock, flags);
}

static void dw_pcie6_bottom_unmask(struct irq_data *d)
{
	struct dw_pcie6_rp *pp = irq_data_get_irq_chip_data(d);
	struct dw_pcie6 *pci = to_dw_pcie6_from_pp(pp);
	unsigned int res, bit, ctrl;
	unsigned long flags;

	raw_spin_lock_irqsave(&pp->lock, flags);

	ctrl = d->hwirq / MAX_MSI_IRQS_PER_CTRL;
	res = ctrl * MSI_REG_CTRL_BLOCK_SIZE;
	bit = d->hwirq % MAX_MSI_IRQS_PER_CTRL;

	pp->irq_mask[ctrl] &= ~BIT(bit);
	dw_pcie6_writel_dbi(pci, PCIE_MSI_INTR0_MASK + res, pp->irq_mask[ctrl]);

	raw_spin_unlock_irqrestore(&pp->lock, flags);
}

static void dw_pci_bottom_ack(struct irq_data *d)
{
	struct dw_pcie6_rp *pp  = irq_data_get_irq_chip_data(d);
	struct dw_pcie6 *pci = to_dw_pcie6_from_pp(pp);
	unsigned int res, bit, ctrl;

	ctrl = d->hwirq / MAX_MSI_IRQS_PER_CTRL;
	res = ctrl * MSI_REG_CTRL_BLOCK_SIZE;
	bit = d->hwirq % MAX_MSI_IRQS_PER_CTRL;

	dw_pcie6_writel_dbi(pci, PCIE_MSI_INTR0_STATUS + res, BIT(bit));
}

static struct irq_chip dw_pci_msi_bottom_irq_chip = {
	.name = "DWPCI-MSI",
	.irq_ack = dw_pci_bottom_ack,
	.irq_compose_msi_msg = dw_pcie6_setup_msi_msg,
	.irq_mask = dw_pcie6_bottom_mask,
	.irq_unmask = dw_pcie6_bottom_unmask,
};

static int dw_pcie6_irq_domain_alloc(struct irq_domain *domain,
				    unsigned int virq, unsigned int nr_irqs,
				    void *args)
{
	struct dw_pcie6_rp *pp = domain->host_data;
	unsigned long flags;
	u32 i;
	int bit;

	raw_spin_lock_irqsave(&pp->lock, flags);

	bit = bitmap_find_free_region(pp->msi_irq_in_use, pp->num_vectors,
				      order_base_2(nr_irqs));

	raw_spin_unlock_irqrestore(&pp->lock, flags);

	if (bit < 0)
		return -ENOSPC;

	for (i = 0; i < nr_irqs; i++)
		irq_domain_set_info(domain, virq + i, bit + i,
				    pp->msi_irq_chip,
				    pp, handle_edge_irq,
				    NULL, NULL);

	return 0;
}

static void dw_pcie6_irq_domain_free(struct irq_domain *domain,
				    unsigned int virq, unsigned int nr_irqs)
{
	struct irq_data *d = irq_domain_get_irq_data(domain, virq);
	struct dw_pcie6_rp *pp = domain->host_data;
	unsigned long flags;

	raw_spin_lock_irqsave(&pp->lock, flags);

	bitmap_release_region(pp->msi_irq_in_use, d->hwirq,
			      order_base_2(nr_irqs));

	raw_spin_unlock_irqrestore(&pp->lock, flags);
}

static const struct irq_domain_ops dw_pcie6_msi_domain_ops = {
	.alloc	= dw_pcie6_irq_domain_alloc,
	.free	= dw_pcie6_irq_domain_free,
};

int dw_pcie6_allocate_domains(struct dw_pcie6_rp *pp)
{
	struct dw_pcie6 *pci = to_dw_pcie6_from_pp(pp);
	struct fwnode_handle *fwnode = of_node_to_fwnode(pci->dev->of_node);

	pp->irq_domain = irq_domain_create_linear(fwnode, pp->num_vectors,
					       &dw_pcie6_msi_domain_ops, pp);
	if (!pp->irq_domain) {
		dev_err(pci->dev, "Failed to create IRQ domain\n");
		return -ENOMEM;
	}

	irq_domain_update_bus_token(pp->irq_domain, DOMAIN_BUS_NEXUS);

	pp->msi_domain = pci_msi_create_irq_domain(fwnode,
						   &dw_pcie6_msi_domain_info,
						   pp->irq_domain);
	if (!pp->msi_domain) {
		dev_err(pci->dev, "Failed to create MSI domain\n");
		irq_domain_remove(pp->irq_domain);
		return -ENOMEM;
	}

	return 0;
}

static void dw_pcie6_free_msi(struct dw_pcie6_rp *pp)
{
	u32 ctrl;

	for (ctrl = 0; ctrl < MAX_MSI_CTRLS; ctrl++) {
		if (pp->msi_irq[ctrl] > 0)
			irq_set_chained_handler_and_data(pp->msi_irq[ctrl],
							 NULL, NULL);
	}

	irq_domain_remove(pp->msi_domain);
	irq_domain_remove(pp->irq_domain);
}

static void dw_pcie6_msi_init(struct dw_pcie6_rp *pp)
{
	struct dw_pcie6 *pci = to_dw_pcie6_from_pp(pp);
	u64 msi_target = (u64)pp->msi_data;

	if (!pci_msi_enabled() || !pp->has_msi_ctrl)
		return;

	/* Program the msi_data */
	dw_pcie6_writel_dbi(pci, PCIE_MSI_ADDR_LO, lower_32_bits(msi_target));
	dw_pcie6_writel_dbi(pci, PCIE_MSI_ADDR_HI, upper_32_bits(msi_target));
}

static int dw_pcie6_parse_split_msi_irq(struct dw_pcie6_rp *pp)
{
	struct dw_pcie6 *pci = to_dw_pcie6_from_pp(pp);
	struct device *dev = pci->dev;
	struct platform_device *pdev = to_platform_device(dev);
	u32 ctrl, max_vectors;
	int irq;

	/* Parse any "msiX" IRQs described in the devicetree */
	for (ctrl = 0; ctrl < MAX_MSI_CTRLS; ctrl++) {
		char msi_name[] = "msiX";

		msi_name[3] = '0' + ctrl;
		irq = platform_get_irq_byname_optional(pdev, msi_name);
		if (irq == -ENXIO)
			break;
		if (irq < 0)
			return dev_err_probe(dev, irq,
					     "Failed to parse MSI IRQ '%s'\n",
					     msi_name);

		pp->msi_irq[ctrl] = irq;
	}

	/* If no "msiX" IRQs, caller should fallback to "msi" IRQ */
	if (ctrl == 0)
		return -ENXIO;

	max_vectors = ctrl * MAX_MSI_IRQS_PER_CTRL;
	if (pp->num_vectors > max_vectors) {
		dev_warn(dev, "Exceeding number of MSI vectors, limiting to %u\n",
			 max_vectors);
		pp->num_vectors = max_vectors;
	}
	if (!pp->num_vectors)
		pp->num_vectors = max_vectors;

	return 0;
}

static int dw_pcie6_msi_host_init(struct dw_pcie6_rp *pp)
{
	struct dw_pcie6 *pci = to_dw_pcie6_from_pp(pp);
	struct device *dev = pci->dev;
	struct platform_device *pdev = to_platform_device(dev);
	u64 *msi_vaddr = NULL;
	int ret;
	u32 ctrl, num_ctrls;

	for (ctrl = 0; ctrl < MAX_MSI_CTRLS; ctrl++)
		pp->irq_mask[ctrl] = ~0;

	if (!pp->msi_irq[0]) {
		ret = dw_pcie6_parse_split_msi_irq(pp);
		if (ret < 0 && ret != -ENXIO)
			return ret;
	}

	if (!pp->num_vectors)
		pp->num_vectors = MSI_DEF_NUM_VECTORS;
	num_ctrls = pp->num_vectors / MAX_MSI_IRQS_PER_CTRL;

	if (!pp->msi_irq[0]) {
		pp->msi_irq[0] = platform_get_irq_byname_optional(pdev, "msi");
		if (pp->msi_irq[0] < 0) {
			pp->msi_irq[0] = platform_get_irq(pdev, 0);
			if (pp->msi_irq[0] < 0)
				return pp->msi_irq[0];
		}
	}

	dev_dbg(dev, "Using %d MSI vectors\n", pp->num_vectors);

	pp->msi_irq_chip = &dw_pci_msi_bottom_irq_chip;

	ret = dw_pcie6_allocate_domains(pp);
	if (ret)
		return ret;

	for (ctrl = 0; ctrl < num_ctrls; ctrl++) {
		if (pp->msi_irq[ctrl] > 0)
			irq_set_chained_handler_and_data(pp->msi_irq[ctrl],
						    dw_chained_msi_isr, pp);
	}

	/*
	 * Even though the iMSI-RX Module supports 64-bit addresses some
	 * peripheral PCIe devices may lack 64-bit message support. In
	 * order not to miss MSI TLPs from those devices the MSI target
	 * address has to be within the lowest 4GB.
	 *
	 * Note until there is a better alternative found the reservation is
	 * done by allocating from the artificially limited DMA-coherent
	 * memory.
	 */
	ret = dma_set_coherent_mask(dev, DMA_BIT_MASK(32));
	if (!ret)
		msi_vaddr = dmam_alloc_coherent(dev, sizeof(u64), &pp->msi_data,
						GFP_KERNEL);

	if (!msi_vaddr) {
		dev_warn(dev, "Failed to allocate 32-bit MSI address\n");
		dma_set_coherent_mask(dev, DMA_BIT_MASK(64));
		msi_vaddr = dmam_alloc_coherent(dev, sizeof(u64), &pp->msi_data,
						GFP_KERNEL);
		if (!msi_vaddr) {
			dev_err(dev, "Failed to allocate MSI address\n");
			dw_pcie6_free_msi(pp);
			return -ENOMEM;
		}
	}

	return 0;
}

static void dw_pcie6_host_request_msg_tlp_res(struct dw_pcie6_rp *pp)
{
	struct dw_pcie6 *pci = to_dw_pcie6_from_pp(pp);
	struct resource_entry *win;
	struct resource *res;

	win = resource_list_first_type(&pp->bridge->windows, IORESOURCE_MEM);
	if (win) {
		res = devm_kzalloc(pci->dev, sizeof(*res), GFP_KERNEL);
		if (!res)
			return;

		/*
		 * Allocate MSG TLP region of size 'region_align' at the end of
		 * the host bridge window.
		 */
		res->start = win->res->end - pci->region_align + 1;
		res->end = win->res->end;
		res->name = "msg";
		res->flags = win->res->flags | IORESOURCE_BUSY;

		if (!devm_request_resource(pci->dev, win->res, res))
			pp->msg_res = res;
	}
}

int dw_pcie6_host_init(struct dw_pcie6_rp *pp)
{
	struct dw_pcie6 *pci = to_dw_pcie6_from_pp(pp);
	struct device *dev = pci->dev;
	struct device_node *np = dev->of_node;
	struct platform_device *pdev = to_platform_device(dev);
	struct resource_entry *win;
	struct pci_host_bridge *bridge;
	struct resource *res;
	int ret;

	raw_spin_lock_init(&pp->lock);

	ret = dw_pcie6_get_resources(pci);
	if (ret)
		return ret;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "config");
	if (res) {
		pp->cfg0_size = resource_size(res);
		pp->cfg0_base = res->start;

		pp->va_cfg0_base = devm_pci_remap_cfg_resource(dev, res);
		if (IS_ERR(pp->va_cfg0_base))
			return PTR_ERR(pp->va_cfg0_base);
	} else {
		dev_err(dev, "Missing *config* reg space\n");
		return -ENODEV;
	}

	bridge = devm_pci_alloc_host_bridge(dev, 0);
	if (!bridge)
		return -ENOMEM;

	pp->bridge = bridge;

	/* Get the I/O range from DT */
	win = resource_list_first_type(&bridge->windows, IORESOURCE_IO);
	if (win) {
		pp->io_size = resource_size(win->res);
		pp->io_bus_addr = win->res->start - win->offset;
		pp->io_base = pci_pio_to_address(win->res->start);
	}

	/* Set default bus ops */
	bridge->ops = &dw_pcie6_ops;
	bridge->child_ops = &dw_child_pcie_ops;

	if (pp->ops->init) {
		ret = pp->ops->init(pp);
		if (ret)
			return ret;
	}

	if (pci_msi_enabled()) {
		pp->has_msi_ctrl = !(pp->ops->msi_init ||
				     of_property_read_bool(np, "msi-parent") ||
				     of_property_read_bool(np, "msi-map"));

		/*
		 * For the has_msi_ctrl case the default assignment is handled
		 * in the dw_pcie6_msi_host_init().
		 */
		if (!pp->has_msi_ctrl && !pp->num_vectors) {
			pp->num_vectors = MSI_DEF_NUM_VECTORS;
		} else if (pp->num_vectors > MAX_MSI_IRQS) {
			dev_err(dev, "Invalid number of vectors\n");
			ret = -EINVAL;
			goto err_deinit_host;
		}

		if (pp->ops->msi_init) {
			ret = pp->ops->msi_init(pp);
			if (ret < 0)
				goto err_deinit_host;
		} else if (pp->has_msi_ctrl) {
			ret = dw_pcie6_msi_host_init(pp);
			if (ret < 0)
				goto err_deinit_host;
		}
	}

	dw_pcie6_version_detect(pci);

	dw_pcie6_iatu_detect(pci);

	/*
	 * Allocate the resource for MSG TLP before programming the iATU
	 * outbound window in dw_pcie6_setup_rc(). Since the allocation depends
	 * on the value of 'region_align', this has to be done after
	 * dw_pcie6_iatu_detect().
	 *
	 * Glue drivers need to set 'use_atu_msg' before dw_pcie6_host_init() to
	 * make use of the generic MSG TLP implementation.
	 */
	if (pp->use_atu_msg)
		dw_pcie6_host_request_msg_tlp_res(pp);

	ret = dw_pcie6_edma_detect(pci);
	if (ret)
		goto err_free_msi;

	ret = dw_pcie6_setup_rc(pp);
	if (ret)
		goto err_remove_edma;

	if (!dw_pcie6_link_up(pci)) {
		ret = dw_pcie6_start_link(pci);
		if (ret)
			goto err_remove_edma;
	}

	/* Ignore errors, the link may come up later */
	dw_pcie6_wait_for_link(pci);

	bridge->sysdata = pp;

	ret = pci_host_probe(bridge);
	if (ret)
		goto err_stop_link;

	if (pp->ops->post_init)
		pp->ops->post_init(pp);

	return 0;

err_stop_link:
	dw_pcie6_stop_link(pci);

err_remove_edma:
	dw_pcie6_edma_remove(pci);

err_free_msi:
	if (pp->has_msi_ctrl)
		dw_pcie6_free_msi(pp);

err_deinit_host:
	if (pp->ops->deinit)
		pp->ops->deinit(pp);

	return ret;
}
EXPORT_SYMBOL_GPL(dw_pcie6_host_init);

void dw_pcie6_host_deinit(struct dw_pcie6_rp *pp)
{
	struct dw_pcie6 *pci = to_dw_pcie6_from_pp(pp);

	pci_stop_root_bus(pp->bridge->bus);
	pci_remove_root_bus(pp->bridge->bus);

	dw_pcie6_stop_link(pci);

	dw_pcie6_edma_remove(pci);

	if (pp->has_msi_ctrl)
		dw_pcie6_free_msi(pp);

	if (pp->ops->deinit)
		pp->ops->deinit(pp);
}
EXPORT_SYMBOL_GPL(dw_pcie6_host_deinit);

static void __iomem *dw_pcie6_other_conf_map_bus(struct pci_bus *bus,
						unsigned int devfn, int where)
{
	struct dw_pcie6_rp *pp = bus->sysdata;
	struct dw_pcie6 *pci = to_dw_pcie6_from_pp(pp);
	struct dw_pcie6_ob_atu_cfg atu = { 0 };
	int type, ret;
	u32 busdev;

	/*
	 * Checking whether the link is up here is a last line of defense
	 * against platforms that forward errors on the system bus as
	 * SError upon PCI configuration transactions issued when the link
	 * is down. This check is racy by definition and does not stop
	 * the system from triggering an SError if the link goes down
	 * after this check is performed.
	 */
	if (!dw_pcie6_link_up(pci))
		return NULL;

	busdev = PCIE_ATU_BUS(bus->number) | PCIE_ATU_DEV(PCI_SLOT(devfn)) |
		 PCIE_ATU_FUNC(PCI_FUNC(devfn));

	if (pci_is_root_bus(bus->parent))
		type = PCIE_ATU_TYPE_CFG0;
	else
		type = PCIE_ATU_TYPE_CFG1;

	atu.type = type;
	atu.cpu_addr = pp->cfg0_base;
	atu.pci_addr = busdev;
	atu.size = pp->cfg0_size;

	ret = dw_pcie6_prog_outbound_atu(pci, &atu);
	if (ret)
		return NULL;

	return pp->va_cfg0_base + where;
}

static int dw_pcie6_rd_other_conf(struct pci_bus *bus, unsigned int devfn,
				 int where, int size, u32 *val)
{
	struct dw_pcie6_rp *pp = bus->sysdata;
	struct dw_pcie6 *pci = to_dw_pcie6_from_pp(pp);
	struct dw_pcie6_ob_atu_cfg atu = { 0 };
	int ret;

	ret = pci_generic_config_read(bus, devfn, where, size, val);
	if (ret != PCIBIOS_SUCCESSFUL)
		return ret;

	if (pp->cfg0_io_shared) {
		atu.type = PCIE_ATU_TYPE_IO;
		atu.cpu_addr = pp->io_base;
		atu.pci_addr = pp->io_bus_addr;
		atu.size = pp->io_size;

		ret = dw_pcie6_prog_outbound_atu(pci, &atu);
		if (ret)
			return PCIBIOS_SET_FAILED;
	}

	return PCIBIOS_SUCCESSFUL;
}

static int dw_pcie6_wr_other_conf(struct pci_bus *bus, unsigned int devfn,
				 int where, int size, u32 val)
{
	struct dw_pcie6_rp *pp = bus->sysdata;
	struct dw_pcie6 *pci = to_dw_pcie6_from_pp(pp);
	struct dw_pcie6_ob_atu_cfg atu = { 0 };
	int ret;

	ret = pci_generic_config_write(bus, devfn, where, size, val);
	if (ret != PCIBIOS_SUCCESSFUL)
		return ret;

	if (pp->cfg0_io_shared) {
		atu.type = PCIE_ATU_TYPE_IO;
		atu.cpu_addr = pp->io_base;
		atu.pci_addr = pp->io_bus_addr;
		atu.size = pp->io_size;

		ret = dw_pcie6_prog_outbound_atu(pci, &atu);
		if (ret)
			return PCIBIOS_SET_FAILED;
	}

	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops dw_child_pcie_ops = {
	.map_bus = dw_pcie6_other_conf_map_bus,
	.read = dw_pcie6_rd_other_conf,
	.write = dw_pcie6_wr_other_conf,
};

void __iomem *dw_pcie6_own_conf_map_bus(struct pci_bus *bus, unsigned int devfn, int where)
{
	struct dw_pcie6_rp *pp = bus->sysdata;
	struct dw_pcie6 *pci = to_dw_pcie6_from_pp(pp);

	if (PCI_SLOT(devfn) > 0)
		return NULL;

	return pci->dbi_base + where;
}
EXPORT_SYMBOL_GPL(dw_pcie6_own_conf_map_bus);

static struct pci_ops dw_pcie6_ops = {
	.map_bus = dw_pcie6_own_conf_map_bus,
	.read = pci_generic_config_read,
	.write = pci_generic_config_write,
};

static int dw_pcie6_iatu_setup(struct dw_pcie6_rp *pp)
{
	struct dw_pcie6 *pci = to_dw_pcie6_from_pp(pp);
	struct dw_pcie6_ob_atu_cfg atu = { 0 };
	struct resource_entry *entry;
	int i, ret;

	/* Note the very first outbound ATU is used for CFG IOs */
	if (!pci->num_ob_windows) {
		dev_err(pci->dev, "No outbound iATU found\n");
		return -EINVAL;
	}

	/*
	 * Ensure all out/inbound windows are disabled before proceeding with
	 * the MEM/IO (dma-)ranges setups.
	 */
	for (i = 0; i < pci->num_ob_windows; i++)
		dw_pcie6_disable_atu(pci, PCIE_ATU_REGION_DIR_OB, i);

	for (i = 0; i < pci->num_ib_windows; i++)
		dw_pcie6_disable_atu(pci, PCIE_ATU_REGION_DIR_IB, i);

	i = 0;
	resource_list_for_each_entry(entry, &pp->bridge->windows) {
		if (resource_type(entry->res) != IORESOURCE_MEM)
			continue;

		if (pci->num_ob_windows <= ++i)
			break;

		atu.index = i;
		atu.type = PCIE_ATU_TYPE_MEM;
		atu.cpu_addr = entry->res->start;
		atu.pci_addr = entry->res->start - entry->offset;

		/* Adjust iATU size if MSG TLP region was allocated before */
		if (pp->msg_res && pp->msg_res->parent == entry->res)
			atu.size = resource_size(entry->res) -
					resource_size(pp->msg_res);
		else
			atu.size = resource_size(entry->res);

		ret = dw_pcie6_prog_outbound_atu(pci, &atu);
		if (ret) {
			dev_err(pci->dev, "Failed to set MEM range %pr\n",
				entry->res);
			return ret;
		}
	}

	if (pp->io_size) {
		if (pci->num_ob_windows > ++i) {
			atu.index = i;
			atu.type = PCIE_ATU_TYPE_IO;
			atu.cpu_addr = pp->io_base;
			atu.pci_addr = pp->io_bus_addr;
			atu.size = pp->io_size;

			ret = dw_pcie6_prog_outbound_atu(pci, &atu);
			if (ret) {
				dev_err(pci->dev, "Failed to set IO range %pr\n",
					entry->res);
				return ret;
			}
		} else {
			pp->cfg0_io_shared = true;
		}
	}

	if (pci->num_ob_windows <= i)
		dev_warn(pci->dev, "Ranges exceed outbound iATU size (%d)\n",
			 pci->num_ob_windows);

	if (pp->use_atu_msg) {
		if (pci->num_ob_windows > ++i) {
			pp->msg_atu_index = i;
		} else {
			dev_err(pci->dev, "Cannot add outbound window for MSG TLP\n");
			return -ENOMEM;
		}
	}

	i = 0;
	resource_list_for_each_entry(entry, &pp->bridge->dma_ranges) {
		if (resource_type(entry->res) != IORESOURCE_MEM)
			continue;

		if (pci->num_ib_windows <= i)
			break;

		ret = dw_pcie6_prog_inbound_atu(pci, i++, PCIE_ATU_TYPE_MEM,
					       entry->res->start,
					       entry->res->start - entry->offset,
					       resource_size(entry->res));
		if (ret) {
			dev_err(pci->dev, "Failed to set DMA range %pr\n",
				entry->res);
			return ret;
		}
	}

	if (pci->num_ib_windows <= i)
		dev_warn(pci->dev, "Dma-ranges exceed inbound iATU size (%u)\n",
			 pci->num_ib_windows);

	return 0;
}

int dw_pcie6_setup_rc(struct dw_pcie6_rp *pp)
{
	struct dw_pcie6 *pci = to_dw_pcie6_from_pp(pp);
	u32 val, ctrl, num_ctrls;
	int ret;

	/*
	 * Enable DBI read-only registers for writing/updating configuration.
	 * Write permission gets disabled towards the end of this function.
	 */
	dw_pcie6_dbi_ro_wr_en(pci);

	dw_pcie6_setup(pci);

	if (pp->has_msi_ctrl) {
		num_ctrls = pp->num_vectors / MAX_MSI_IRQS_PER_CTRL;

		/* Initialize IRQ Status array */
		for (ctrl = 0; ctrl < num_ctrls; ctrl++) {
			dw_pcie6_writel_dbi(pci, PCIE_MSI_INTR0_MASK +
					    (ctrl * MSI_REG_CTRL_BLOCK_SIZE),
					    pp->irq_mask[ctrl]);
			dw_pcie6_writel_dbi(pci, PCIE_MSI_INTR0_ENABLE +
					    (ctrl * MSI_REG_CTRL_BLOCK_SIZE),
					    ~0);
		}
	}

	dw_pcie6_msi_init(pp);

	/* Setup RC BARs */
	dw_pcie6_writel_dbi(pci, PCI_BASE_ADDRESS_0, 0x00000004);
	dw_pcie6_writel_dbi(pci, PCI_BASE_ADDRESS_1, 0x00000000);

	/* Setup interrupt pins */
	val = dw_pcie6_readl_dbi(pci, PCI_INTERRUPT_LINE);
	val &= 0xffff00ff;
	val |= 0x00000100;
	dw_pcie6_writel_dbi(pci, PCI_INTERRUPT_LINE, val);

	/* Setup bus numbers */
	val = dw_pcie6_readl_dbi(pci, PCI_PRIMARY_BUS);
	val &= 0xff000000;
	val |= 0x00ff0100;
	dw_pcie6_writel_dbi(pci, PCI_PRIMARY_BUS, val);

	/* Setup command register */
	val = dw_pcie6_readl_dbi(pci, PCI_COMMAND);
	val &= 0xffff0000;
	val |= PCI_COMMAND_IO | PCI_COMMAND_MEMORY |
		PCI_COMMAND_MASTER | PCI_COMMAND_SERR;
	dw_pcie6_writel_dbi(pci, PCI_COMMAND, val);

	/*
	 * If the platform provides its own child bus config accesses, it means
	 * the platform uses its own address translation component rather than
	 * ATU, so we should not program the ATU here.
	 */
	if (pp->bridge->child_ops == &dw_child_pcie_ops) {
		ret = dw_pcie6_iatu_setup(pp);
		if (ret)
			return ret;
	}

	dw_pcie6_writel_dbi(pci, PCI_BASE_ADDRESS_0, 0);

	/* Program correct class for RC */
	dw_pcie6_writew_dbi(pci, PCI_CLASS_DEVICE, PCI_CLASS_BRIDGE_PCI);

	val = dw_pcie6_readl_dbi(pci, PCIE_LINK_WIDTH_SPEED_CONTROL);
	val |= PORT_LOGIC_SPEED_CHANGE;
	dw_pcie6_writel_dbi(pci, PCIE_LINK_WIDTH_SPEED_CONTROL, val);

	dw_pcie6_dbi_ro_wr_dis(pci);

	return 0;
}
EXPORT_SYMBOL_GPL(dw_pcie6_setup_rc);

static int dw_pcie6_pme_turn_off(struct dw_pcie6 *pci)
{
	struct dw_pcie6_ob_atu_cfg atu = { 0 };
	void __iomem *mem;
	int ret;

	if (pci->num_ob_windows <= pci->pp.msg_atu_index)
		return -ENOSPC;

	if (!pci->pp.msg_res)
		return -ENOSPC;

	atu.code = PCIE_MSG_CODE_PME_TURN_OFF;
	atu.routing = PCIE_MSG_TYPE_R_BC;
	atu.type = PCIE_ATU_TYPE_MSG;
	atu.size = resource_size(pci->pp.msg_res);
	atu.index = pci->pp.msg_atu_index;

	atu.cpu_addr = pci->pp.msg_res->start;

	ret = dw_pcie6_prog_outbound_atu(pci, &atu);
	if (ret)
		return ret;

	mem = ioremap(pci->pp.msg_res->start, pci->region_align);
	if (!mem)
		return -ENOMEM;

	/* A dummy write is converted to a Msg TLP */
	writel(0, mem);

	iounmap(mem);

	return 0;
}

int dw_pcie6_suspend_noirq(struct dw_pcie6 *pci)
{
	u8 offset = dw_pcie6_find_capability(pci, PCI_CAP_ID_EXP);
	u32 val;
	int ret = 0;

	/*
	 * If L1SS is supported, then do not put the link into L2 as some
	 * devices such as NVMe expect low resume latency.
	 */
	if (dw_pcie6_readw_dbi(pci, offset + PCI_EXP_LNKCTL) & PCI_EXP_LNKCTL_ASPM_L1)
		return 0;

	if (dw_pcie6_get_ltssm(pci) <= DW_PCIE_LTSSM_DETECT_ACT)
		return 0;

	if (pci->pp.ops->pme_turn_off)
		pci->pp.ops->pme_turn_off(&pci->pp);
	else
		ret = dw_pcie6_pme_turn_off(pci);

	if (ret)
		return ret;

	ret = read_poll_timeout(dw_pcie6_get_ltssm, val, val == DW_PCIE_LTSSM_L2_IDLE,
				PCIE_PME_TO_L2_TIMEOUT_US/10,
				PCIE_PME_TO_L2_TIMEOUT_US, false, pci);
	if (ret) {
		dev_err(pci->dev, "Timeout waiting for L2 entry! LTSSM: 0x%x\n", val);
		return ret;
	}

	dw_pcie6_stop_link(pci);
	if (pci->pp.ops->deinit)
		pci->pp.ops->deinit(&pci->pp);

	pci->suspended = true;

	return ret;
}
EXPORT_SYMBOL_GPL(dw_pcie6_suspend_noirq);

int dw_pcie6_resume_noirq(struct dw_pcie6 *pci)
{
	int ret;

	if (!pci->suspended)
		return 0;

	pci->suspended = false;

	if (pci->pp.ops->init) {
		ret = pci->pp.ops->init(&pci->pp);
		if (ret) {
			dev_err(pci->dev, "Host init failed: %d\n", ret);
			return ret;
		}
	}

	dw_pcie6_setup_rc(&pci->pp);

	ret = dw_pcie6_start_link(pci);
	if (ret)
		return ret;

	ret = dw_pcie6_wait_for_link(pci);
	if (ret)
		return ret;

	return ret;
}
EXPORT_SYMBOL_GPL(dw_pcie6_resume_noirq);

/* PCIe6 Designware Endpoint */

/**
 * dw_pcie6_ep_get_func_from_ep - Get the struct dw_pcie6_ep_func corresponding to
 *				 the endpoint function
 * @ep: DWC EP device
 * @func_no: Function number of the endpoint device
 *
 * Return: struct dw_pcie6_ep_func if success, NULL otherwise.
 */
struct dw_pcie6_ep_func *
dw_pcie6_ep_get_func_from_ep(struct dw_pcie6_ep *ep, u8 func_no)
{
	struct dw_pcie6_ep_func *ep_func;

	list_for_each_entry(ep_func, &ep->func_list, list) {
		if (ep_func->func_no == func_no)
			return ep_func;
	}

	return NULL;
}

static void __dw_pcie6_ep_reset_bar(struct dw_pcie6 *pci, u8 func_no,
				   enum pci_barno bar, int flags)
{
	struct dw_pcie6_ep *ep = &pci->ep;
	u32 reg;

	reg = PCI_BASE_ADDRESS_0 + (4 * bar);
	dw_pcie6_dbi_ro_wr_en(pci);
	dw_pcie6_ep_writel_dbi2(ep, func_no, reg, 0x0);
	dw_pcie6_ep_writel_dbi(ep, func_no, reg, 0x0);
	if (flags & PCI_BASE_ADDRESS_MEM_TYPE_64) {
		dw_pcie6_ep_writel_dbi2(ep, func_no, reg + 4, 0x0);
		dw_pcie6_ep_writel_dbi(ep, func_no, reg + 4, 0x0);
	}
	dw_pcie6_dbi_ro_wr_dis(pci);
}

/**
 * dw_pcie6_ep_reset_bar - Reset endpoint BAR
 * @pci: DWC PCI device
 * @bar: BAR number of the endpoint
 */
void dw_pcie6_ep_reset_bar(struct dw_pcie6 *pci, enum pci_barno bar)
{
	u8 func_no, funcs;

	funcs = pci->ep.epc->max_functions;

	for (func_no = 0; func_no < funcs; func_no++)
		__dw_pcie6_ep_reset_bar(pci, func_no, bar, 0);
}
EXPORT_SYMBOL_GPL(dw_pcie6_ep_reset_bar);

static u8 __dw_pcie6_ep_find_next_cap(struct dw_pcie6_ep *ep, u8 func_no,
				     u8 cap_ptr, u8 cap)
{
	u8 cap_id, next_cap_ptr;
	u16 reg;

	if (!cap_ptr)
		return 0;

	reg = dw_pcie6_ep_readw_dbi(ep, func_no, cap_ptr);
	cap_id = (reg & 0x00ff);

	if (cap_id > PCI_CAP_ID_MAX)
		return 0;

	if (cap_id == cap)
		return cap_ptr;

	next_cap_ptr = (reg & 0xff00) >> 8;
	return __dw_pcie6_ep_find_next_cap(ep, func_no, next_cap_ptr, cap);
}

static u8 dw_pcie6_ep_find_capability(struct dw_pcie6_ep *ep, u8 func_no, u8 cap)
{
	u8 next_cap_ptr;
	u16 reg;

	reg = dw_pcie6_ep_readw_dbi(ep, func_no, PCI_CAPABILITY_LIST);
	next_cap_ptr = (reg & 0x00ff);

	return __dw_pcie6_ep_find_next_cap(ep, func_no, next_cap_ptr, cap);
}

static int dw_pcie6_ep_write_header(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
				   struct pci_epf_header *hdr)
{
	struct dw_pcie6_ep *ep = epc_get_drvdata(epc);
	struct dw_pcie6 *pci = to_dw_pcie6_from_ep(ep);

	dw_pcie6_dbi_ro_wr_en(pci);
	dw_pcie6_ep_writew_dbi(ep, func_no, PCI_VENDOR_ID, hdr->vendorid);
	dw_pcie6_ep_writew_dbi(ep, func_no, PCI_DEVICE_ID, hdr->deviceid);
	dw_pcie6_ep_writeb_dbi(ep, func_no, PCI_REVISION_ID, hdr->revid);
	dw_pcie6_ep_writeb_dbi(ep, func_no, PCI_CLASS_PROG, hdr->progif_code);
	dw_pcie6_ep_writew_dbi(ep, func_no, PCI_CLASS_DEVICE,
			      hdr->subclass_code | hdr->baseclass_code << 8);
	dw_pcie6_ep_writeb_dbi(ep, func_no, PCI_CACHE_LINE_SIZE,
			      hdr->cache_line_size);
	dw_pcie6_ep_writew_dbi(ep, func_no, PCI_SUBSYSTEM_VENDOR_ID,
			      hdr->subsys_vendor_id);
	dw_pcie6_ep_writew_dbi(ep, func_no, PCI_SUBSYSTEM_ID, hdr->subsys_id);
	dw_pcie6_ep_writeb_dbi(ep, func_no, PCI_INTERRUPT_PIN,
			      hdr->interrupt_pin);
	dw_pcie6_dbi_ro_wr_dis(pci);

	return 0;
}

static int dw_pcie6_ep_inbound_atu(struct dw_pcie6_ep *ep, u8 func_no, int type,
				  dma_addr_t cpu_addr, enum pci_barno bar)
{
	int ret;
	u32 free_win;
	struct dw_pcie6 *pci = to_dw_pcie6_from_ep(ep);

	if (!ep->bar_to_atu[bar])
		free_win = find_first_zero_bit(ep->ib_window_map, pci->num_ib_windows);
	else
		free_win = ep->bar_to_atu[bar] - 1;

	if (free_win >= pci->num_ib_windows) {
		dev_err(pci->dev, "No free inbound window\n");
		return -EINVAL;
	}

	ret = dw_pcie6_prog_ep_inbound_atu(pci, func_no, free_win, type,
					  cpu_addr, bar);
	if (ret < 0) {
		dev_err(pci->dev, "Failed to program IB window\n");
		return ret;
	}

	/*
	 * Always increment free_win before assignment, since value 0 is used to identify
	 * unallocated mapping.
	 */
	ep->bar_to_atu[bar] = free_win + 1;
	set_bit(free_win, ep->ib_window_map);

	return 0;
}

static int dw_pcie6_ep_outbound_atu(struct dw_pcie6_ep *ep,
				   struct dw_pcie6_ob_atu_cfg *atu)
{
	struct dw_pcie6 *pci = to_dw_pcie6_from_ep(ep);
	u32 free_win;
	int ret;

	free_win = find_first_zero_bit(ep->ob_window_map, pci->num_ob_windows);
	if (free_win >= pci->num_ob_windows) {
		dev_err(pci->dev, "No free outbound window\n");
		return -EINVAL;
	}

	atu->index = free_win;
	ret = dw_pcie6_prog_outbound_atu(pci, atu);
	if (ret)
		return ret;

	set_bit(free_win, ep->ob_window_map);
	ep->outbound_addr[free_win] = atu->cpu_addr;

	return 0;
}

static void dw_pcie6_ep_clear_bar(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
				 struct pci_epf_bar *epf_bar)
{
	struct dw_pcie6_ep *ep = epc_get_drvdata(epc);
	struct dw_pcie6 *pci = to_dw_pcie6_from_ep(ep);
	enum pci_barno bar = epf_bar->barno;
	u32 atu_index = ep->bar_to_atu[bar] - 1;

	if (!ep->bar_to_atu[bar])
		return;

	__dw_pcie6_ep_reset_bar(pci, func_no, bar, epf_bar->flags);

	dw_pcie6_disable_atu(pci, PCIE_ATU_REGION_DIR_IB, atu_index);
	clear_bit(atu_index, ep->ib_window_map);
	ep->epf_bar[bar] = NULL;
	ep->bar_to_atu[bar] = 0;
}

static int dw_pcie6_ep_set_bar(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
			      struct pci_epf_bar *epf_bar)
{
	struct dw_pcie6_ep *ep = epc_get_drvdata(epc);
	struct dw_pcie6 *pci = to_dw_pcie6_from_ep(ep);
	enum pci_barno bar = epf_bar->barno;
	size_t size = epf_bar->size;
	int flags = epf_bar->flags;
	int ret, type;
	u32 reg;

	/*
	 * DWC does not allow BAR pairs to overlap, e.g. you cannot combine BARs
	 * 1 and 2 to form a 64-bit BAR.
	 */
	if ((flags & PCI_BASE_ADDRESS_MEM_TYPE_64) && (bar & 1))
		return -EINVAL;

	/*
	 * Certain EPF drivers dynamically change the physical address of a BAR
	 * (i.e. they call set_bar() twice, without ever calling clear_bar(), as
	 * calling clear_bar() would clear the BAR's PCI address assigned by the
	 * host).
	 */
	if (ep->epf_bar[bar]) {
		/*
		 * We can only dynamically change a BAR if the new BAR size and
		 * BAR flags do not differ from the existing configuration.
		 */
		if (ep->epf_bar[bar]->barno != bar ||
		    ep->epf_bar[bar]->size != size ||
		    ep->epf_bar[bar]->flags != flags)
			return -EINVAL;

		/*
		 * When dynamically changing a BAR, skip writing the BAR reg, as
		 * that would clear the BAR's PCI address assigned by the host.
		 */
		goto config_atu;
	}

	reg = PCI_BASE_ADDRESS_0 + (4 * bar);

	dw_pcie6_dbi_ro_wr_en(pci);

	dw_pcie6_ep_writel_dbi2(ep, func_no, reg, lower_32_bits(size - 1));
	dw_pcie6_ep_writel_dbi(ep, func_no, reg, flags);

	if (flags & PCI_BASE_ADDRESS_MEM_TYPE_64) {
		dw_pcie6_ep_writel_dbi2(ep, func_no, reg + 4, upper_32_bits(size - 1));
		dw_pcie6_ep_writel_dbi(ep, func_no, reg + 4, 0);
	}

	dw_pcie6_dbi_ro_wr_dis(pci);

config_atu:
	if (!(flags & PCI_BASE_ADDRESS_SPACE))
		type = PCIE_ATU_TYPE_MEM;
	else
		type = PCIE_ATU_TYPE_IO;

	ret = dw_pcie6_ep_inbound_atu(ep, func_no, type, epf_bar->phys_addr, bar);
	if (ret)
		return ret;

	ep->epf_bar[bar] = epf_bar;

	return 0;
}

static int dw_pcie6_find_index(struct dw_pcie6_ep *ep, phys_addr_t addr,
			      u32 *atu_index)
{
	u32 index;
	struct dw_pcie6 *pci = to_dw_pcie6_from_ep(ep);

	for_each_set_bit(index, ep->ob_window_map, pci->num_ob_windows) {
		if (ep->outbound_addr[index] != addr)
			continue;
		*atu_index = index;
		return 0;
	}

	return -EINVAL;
}

static u64 dw_pcie6_ep_align_addr(struct pci_epc *epc, u64 pci_addr,
				 size_t *pci_size, size_t *offset)
{
	struct dw_pcie6_ep *ep = epc_get_drvdata(epc);
	struct dw_pcie6 *pci = to_dw_pcie6_from_ep(ep);
	u64 mask = pci->region_align - 1;
	size_t ofst = pci_addr & mask;

	*pci_size = ALIGN(ofst + *pci_size, epc->mem->window.page_size);
	*offset = ofst;

	return pci_addr & ~mask;
}

static void dw_pcie6_ep_unmap_addr(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
				  phys_addr_t addr)
{
	int ret;
	u32 atu_index;
	struct dw_pcie6_ep *ep = epc_get_drvdata(epc);
	struct dw_pcie6 *pci = to_dw_pcie6_from_ep(ep);

	ret = dw_pcie6_find_index(ep, addr, &atu_index);
	if (ret < 0)
		return;

	dw_pcie6_disable_atu(pci, PCIE_ATU_REGION_DIR_OB, atu_index);
	clear_bit(atu_index, ep->ob_window_map);
}

static int dw_pcie6_ep_map_addr(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
			       phys_addr_t addr, u64 pci_addr, size_t size)
{
	int ret;
	struct dw_pcie6_ep *ep = epc_get_drvdata(epc);
	struct dw_pcie6 *pci = to_dw_pcie6_from_ep(ep);
	struct dw_pcie6_ob_atu_cfg atu = { 0 };

	atu.func_no = func_no;
	atu.type = PCIE_ATU_TYPE_MEM;
	atu.cpu_addr = addr;
	atu.pci_addr = pci_addr;
	atu.size = size;
	ret = dw_pcie6_ep_outbound_atu(ep, &atu);
	if (ret) {
		dev_err(pci->dev, "Failed to enable address\n");
		return ret;
	}

	return 0;
}

static int dw_pcie6_ep_get_msi(struct pci_epc *epc, u8 func_no, u8 vfunc_no)
{
	struct dw_pcie6_ep *ep = epc_get_drvdata(epc);
	struct dw_pcie6_ep_func *ep_func;
	u32 val, reg;

	ep_func = dw_pcie6_ep_get_func_from_ep(ep, func_no);
	if (!ep_func || !ep_func->msi_cap)
		return -EINVAL;

	reg = ep_func->msi_cap + PCI_MSI_FLAGS;
	val = dw_pcie6_ep_readw_dbi(ep, func_no, reg);
	if (!(val & PCI_MSI_FLAGS_ENABLE))
		return -EINVAL;

	val = FIELD_GET(PCI_MSI_FLAGS_QSIZE, val);

	return val;
}

static int dw_pcie6_ep_set_msi(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
			      u8 interrupts)
{
	struct dw_pcie6_ep *ep = epc_get_drvdata(epc);
	struct dw_pcie6 *pci = to_dw_pcie6_from_ep(ep);
	struct dw_pcie6_ep_func *ep_func;
	u32 val, reg;

	ep_func = dw_pcie6_ep_get_func_from_ep(ep, func_no);
	if (!ep_func || !ep_func->msi_cap)
		return -EINVAL;

	reg = ep_func->msi_cap + PCI_MSI_FLAGS;
	val = dw_pcie6_ep_readw_dbi(ep, func_no, reg);
	val &= ~PCI_MSI_FLAGS_QMASK;
	val |= FIELD_PREP(PCI_MSI_FLAGS_QMASK, interrupts);
	dw_pcie6_dbi_ro_wr_en(pci);
	dw_pcie6_ep_writew_dbi(ep, func_no, reg, val);
	dw_pcie6_dbi_ro_wr_dis(pci);

	return 0;
}

static int dw_pcie6_ep_get_msix(struct pci_epc *epc, u8 func_no, u8 vfunc_no)
{
	struct dw_pcie6_ep *ep = epc_get_drvdata(epc);
	struct dw_pcie6_ep_func *ep_func;
	u32 val, reg;

	ep_func = dw_pcie6_ep_get_func_from_ep(ep, func_no);
	if (!ep_func || !ep_func->msix_cap)
		return -EINVAL;

	reg = ep_func->msix_cap + PCI_MSIX_FLAGS;
	val = dw_pcie6_ep_readw_dbi(ep, func_no, reg);
	if (!(val & PCI_MSIX_FLAGS_ENABLE))
		return -EINVAL;

	val &= PCI_MSIX_FLAGS_QSIZE;

	return val;
}

static int dw_pcie6_ep_set_msix(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
			       u16 interrupts, enum pci_barno bir, u32 offset)
{
	struct dw_pcie6_ep *ep = epc_get_drvdata(epc);
	struct dw_pcie6 *pci = to_dw_pcie6_from_ep(ep);
	struct dw_pcie6_ep_func *ep_func;
	u32 val, reg;
	u16 actual_interrupts = interrupts + 1;

	ep_func = dw_pcie6_ep_get_func_from_ep(ep, func_no);
	if (!ep_func || !ep_func->msix_cap)
		return -EINVAL;

	dw_pcie6_dbi_ro_wr_en(pci);

	reg = ep_func->msix_cap + PCI_MSIX_FLAGS;
	val = dw_pcie6_ep_readw_dbi(ep, func_no, reg);
	val &= ~PCI_MSIX_FLAGS_QSIZE;
	val |= interrupts; /* 0's based value */
	dw_pcie6_writew_dbi(pci, reg, val);

	reg = ep_func->msix_cap + PCI_MSIX_TABLE;
	val = offset | bir;
	dw_pcie6_ep_writel_dbi(ep, func_no, reg, val);

	reg = ep_func->msix_cap + PCI_MSIX_PBA;
	val = (offset + (actual_interrupts * PCI_MSIX_ENTRY_SIZE)) | bir;
	dw_pcie6_ep_writel_dbi(ep, func_no, reg, val);

	dw_pcie6_dbi_ro_wr_dis(pci);

	return 0;
}

static int dw_pcie6_ep_raise_irq(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
				unsigned int type, u16 interrupt_num)
{
	struct dw_pcie6_ep *ep = epc_get_drvdata(epc);

	if (!ep->ops->raise_irq)
		return -EINVAL;

	return ep->ops->raise_irq(ep, func_no, type, interrupt_num);
}

static void dw_pcie6_ep_stop(struct pci_epc *epc)
{
	struct dw_pcie6_ep *ep = epc_get_drvdata(epc);
	struct dw_pcie6 *pci = to_dw_pcie6_from_ep(ep);

	dw_pcie6_stop_link(pci);
}

static int dw_pcie6_ep_start(struct pci_epc *epc)
{
	struct dw_pcie6_ep *ep = epc_get_drvdata(epc);
	struct dw_pcie6 *pci = to_dw_pcie6_from_ep(ep);

	return dw_pcie6_start_link(pci);
}

static const struct pci_epc_features*
dw_pcie6_ep_get_features(struct pci_epc *epc, u8 func_no, u8 vfunc_no)
{
	struct dw_pcie6_ep *ep = epc_get_drvdata(epc);

	if (!ep->ops->get_features)
		return NULL;

	return ep->ops->get_features(ep);
}

static const struct pci_epc_ops epc_ops = {
	.write_header		= dw_pcie6_ep_write_header,
	.set_bar		= dw_pcie6_ep_set_bar,
	.clear_bar		= dw_pcie6_ep_clear_bar,
	.align_addr		= dw_pcie6_ep_align_addr,
	.map_addr		= dw_pcie6_ep_map_addr,
	.unmap_addr		= dw_pcie6_ep_unmap_addr,
	.set_msi		= dw_pcie6_ep_set_msi,
	.get_msi		= dw_pcie6_ep_get_msi,
	.set_msix		= dw_pcie6_ep_set_msix,
	.get_msix		= dw_pcie6_ep_get_msix,
	.raise_irq		= dw_pcie6_ep_raise_irq,
	.start			= dw_pcie6_ep_start,
	.stop			= dw_pcie6_ep_stop,
	.get_features		= dw_pcie6_ep_get_features,
};

/**
 * dw_pcie6_ep_raise_intx_irq - Raise INTx IRQ to the host
 * @ep: DWC EP device
 * @func_no: Function number of the endpoint
 *
 * Return: 0 if success, errono otherwise.
 */
int dw_pcie6_ep_raise_intx_irq(struct dw_pcie6_ep *ep, u8 func_no)
{
	struct dw_pcie6 *pci = to_dw_pcie6_from_ep(ep);
	struct device *dev = pci->dev;

	dev_err(dev, "EP cannot raise INTX IRQs\n");

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(dw_pcie6_ep_raise_intx_irq);

/**
 * dw_pcie6_ep_raise_msi_irq - Raise MSI IRQ to the host
 * @ep: DWC EP device
 * @func_no: Function number of the endpoint
 * @interrupt_num: Interrupt number to be raised
 *
 * Return: 0 if success, errono otherwise.
 */
int dw_pcie6_ep_raise_msi_irq(struct dw_pcie6_ep *ep, u8 func_no,
			     u8 interrupt_num)
{
	u32 msg_addr_lower, msg_addr_upper, reg;
	struct dw_pcie6_ep_func *ep_func;
	struct pci_epc *epc = ep->epc;
	size_t map_size = sizeof(u32);
	size_t offset;
	u16 msg_ctrl, msg_data;
	bool has_upper;
	u64 msg_addr;
	int ret;

	ep_func = dw_pcie6_ep_get_func_from_ep(ep, func_no);
	if (!ep_func || !ep_func->msi_cap)
		return -EINVAL;

	/* Raise MSI per the PCI Local Bus Specification Revision 3.0, 6.8.1. */
	reg = ep_func->msi_cap + PCI_MSI_FLAGS;
	msg_ctrl = dw_pcie6_ep_readw_dbi(ep, func_no, reg);
	has_upper = !!(msg_ctrl & PCI_MSI_FLAGS_64BIT);
	reg = ep_func->msi_cap + PCI_MSI_ADDRESS_LO;
	msg_addr_lower = dw_pcie6_ep_readl_dbi(ep, func_no, reg);
	if (has_upper) {
		reg = ep_func->msi_cap + PCI_MSI_ADDRESS_HI;
		msg_addr_upper = dw_pcie6_ep_readl_dbi(ep, func_no, reg);
		reg = ep_func->msi_cap + PCI_MSI_DATA_64;
		msg_data = dw_pcie6_ep_readw_dbi(ep, func_no, reg);
	} else {
		msg_addr_upper = 0;
		reg = ep_func->msi_cap + PCI_MSI_DATA_32;
		msg_data = dw_pcie6_ep_readw_dbi(ep, func_no, reg);
	}
	msg_addr = ((u64)msg_addr_upper) << 32 | msg_addr_lower;

	msg_addr = dw_pcie6_ep_align_addr(epc, msg_addr, &map_size, &offset);
	ret = dw_pcie6_ep_map_addr(epc, func_no, 0, ep->msi_mem_phys, msg_addr,
				  map_size);
	if (ret)
		return ret;

	writel(msg_data | (interrupt_num - 1), ep->msi_mem + offset);

	dw_pcie6_ep_unmap_addr(epc, func_no, 0, ep->msi_mem_phys);

	return 0;
}
EXPORT_SYMBOL_GPL(dw_pcie6_ep_raise_msi_irq);

/**
 * dw_pcie6_ep_raise_msix_irq_doorbell - Raise MSI-X to the host using Doorbell
 *					method
 * @ep: DWC EP device
 * @func_no: Function number of the endpoint device
 * @interrupt_num: Interrupt number to be raised
 *
 * Return: 0 if success, errno otherwise.
 */
int dw_pcie6_ep_raise_msix_irq_doorbell(struct dw_pcie6_ep *ep, u8 func_no,
				       u16 interrupt_num)
{
	struct dw_pcie6 *pci = to_dw_pcie6_from_ep(ep);
	struct dw_pcie6_ep_func *ep_func;
	u32 msg_data;

	ep_func = dw_pcie6_ep_get_func_from_ep(ep, func_no);
	if (!ep_func || !ep_func->msix_cap)
		return -EINVAL;

	msg_data = (func_no << PCIE_MSIX_DOORBELL_PF_SHIFT) |
		   (interrupt_num - 1);

	dw_pcie6_writel_dbi(pci, PCIE_MSIX_DOORBELL, msg_data);

	return 0;
}

/**
 * dw_pcie6_ep_raise_msix_irq - Raise MSI-X to the host
 * @ep: DWC EP device
 * @func_no: Function number of the endpoint device
 * @interrupt_num: Interrupt number to be raised
 *
 * Return: 0 if success, errno otherwise.
 */
int dw_pcie6_ep_raise_msix_irq(struct dw_pcie6_ep *ep, u8 func_no,
			      u16 interrupt_num)
{
	struct dw_pcie6 *pci = to_dw_pcie6_from_ep(ep);
	struct pci_epf_msix_tbl *msix_tbl;
	struct dw_pcie6_ep_func *ep_func;
	struct pci_epc *epc = ep->epc;
	size_t map_size = sizeof(u32);
	size_t offset;
	u32 reg, msg_data, vec_ctrl;
	u32 tbl_offset;
	u64 msg_addr;
	int ret;
	u8 bir;

	ep_func = dw_pcie6_ep_get_func_from_ep(ep, func_no);
	if (!ep_func || !ep_func->msix_cap)
		return -EINVAL;

	reg = ep_func->msix_cap + PCI_MSIX_TABLE;
	tbl_offset = dw_pcie6_ep_readl_dbi(ep, func_no, reg);
	bir = FIELD_GET(PCI_MSIX_TABLE_BIR, tbl_offset);
	tbl_offset &= PCI_MSIX_TABLE_OFFSET;

	msix_tbl = ep->epf_bar[bir]->addr + tbl_offset;
	msg_addr = msix_tbl[(interrupt_num - 1)].msg_addr;
	msg_data = msix_tbl[(interrupt_num - 1)].msg_data;
	vec_ctrl = msix_tbl[(interrupt_num - 1)].vector_ctrl;

	if (vec_ctrl & PCI_MSIX_ENTRY_CTRL_MASKBIT) {
		dev_dbg(pci->dev, "MSI-X entry ctrl set\n");
		return -EPERM;
	}

	msg_addr = dw_pcie6_ep_align_addr(epc, msg_addr, &map_size, &offset);
	ret = dw_pcie6_ep_map_addr(epc, func_no, 0, ep->msi_mem_phys, msg_addr,
				  map_size);
	if (ret)
		return ret;

	writel(msg_data, ep->msi_mem + offset);

	/* flush posted write before unmap */
	readl(ep->msi_mem + offset);

	dw_pcie6_ep_unmap_addr(epc, func_no, 0, ep->msi_mem_phys);

	return 0;
}

/**
 * dw_pcie6_ep_cleanup - Cleanup DWC EP resources after fundamental reset
 * @ep: DWC EP device
 *
 * Cleans up the DWC EP specific resources like eDMA etc... after fundamental
 * reset like PERST#. Note that this API is only applicable for drivers
 * supporting PERST# or any other methods of fundamental reset.
 */
void dw_pcie6_ep_cleanup(struct dw_pcie6_ep *ep)
{
	struct dw_pcie6 *pci = to_dw_pcie6_from_ep(ep);

	dw_pcie6_edma_remove(pci);
}
EXPORT_SYMBOL_GPL(dw_pcie6_ep_cleanup);

/**
 * dw_pcie6_ep_deinit - Deinitialize the endpoint device
 * @ep: DWC EP device
 *
 * Deinitialize the endpoint device. EPC device is not destroyed since that will
 * be taken care by Devres.
 */
void dw_pcie6_ep_deinit(struct dw_pcie6_ep *ep)
{
	struct pci_epc *epc = ep->epc;

	dw_pcie6_ep_cleanup(ep);

	pci_epc_mem_free_addr(epc, ep->msi_mem_phys, ep->msi_mem,
			      epc->mem->window.page_size);

	pci_epc_mem_exit(epc);
}
EXPORT_SYMBOL_GPL(dw_pcie6_ep_deinit);

static unsigned int dw_pcie6_ep_find_ext_capability(struct dw_pcie6 *pci, int cap)
{
	u32 header;
	int pos = PCI_CFG_SPACE_SIZE;

	while (pos) {
		header = dw_pcie6_readl_dbi(pci, pos);
		if (PCI_EXT_CAP_ID(header) == cap)
			return pos;

		pos = PCI_EXT_CAP_NEXT(header);
		if (!pos)
			break;
	}

	return 0;
}

static void dw_pcie6_ep_init_non_sticky_registers(struct dw_pcie6 *pci)
{
	unsigned int offset;
	unsigned int nbars;
	u32 reg, i;

	offset = dw_pcie6_ep_find_ext_capability(pci, PCI_EXT_CAP_ID_REBAR);

	dw_pcie6_dbi_ro_wr_en(pci);

	if (offset) {
		reg = dw_pcie6_readl_dbi(pci, offset + PCI_REBAR_CTRL);
		nbars = (reg & PCI_REBAR_CTRL_NBAR_MASK) >>
			PCI_REBAR_CTRL_NBAR_SHIFT;

		/*
		 * PCIe r6.0, sec 7.8.6.2 require us to support at least one
		 * size in the range from 1 MB to 512 GB. Advertise support
		 * for 1 MB BAR size only.
		 */
		for (i = 0; i < nbars; i++, offset += PCI_REBAR_CTRL)
			dw_pcie6_writel_dbi(pci, offset + PCI_REBAR_CAP, BIT(4));
	}

	dw_pcie6_setup(pci);
	dw_pcie6_dbi_ro_wr_dis(pci);
}

/**
 * dw_pcie6_ep_init_registers - Initialize DWC EP specific registers
 * @ep: DWC EP device
 *
 * Initialize the registers (CSRs) specific to DWC EP. This API should be called
 * only when the endpoint receives an active refclk (either from host or
 * generated locally).
 */
int dw_pcie6_ep_init_registers(struct dw_pcie6_ep *ep)
{
	struct dw_pcie6 *pci = to_dw_pcie6_from_ep(ep);
	struct dw_pcie6_ep_func *ep_func;
	struct device *dev = pci->dev;
	struct pci_epc *epc = ep->epc;
	u32 ptm_cap_base, reg;
	u8 hdr_type;
	u8 func_no;
	void *addr;
	int ret;

	hdr_type = dw_pcie6_readb_dbi(pci, PCI_HEADER_TYPE) &
		   PCI_HEADER_TYPE_MASK;
	if (hdr_type != PCI_HEADER_TYPE_NORMAL) {
		dev_err(pci->dev,
			"PCIe controller is not set to EP mode (hdr_type:0x%x)!\n",
			hdr_type);
		return -EIO;
	}

	dw_pcie6_version_detect(pci);

	dw_pcie6_iatu_detect(pci);

	ret = dw_pcie6_edma_detect(pci);
	if (ret)
		return ret;

	ret = -ENOMEM;
	if (!ep->ib_window_map) {
		ep->ib_window_map = devm_bitmap_zalloc(dev, pci->num_ib_windows,
						       GFP_KERNEL);
		if (!ep->ib_window_map)
			goto err_remove_edma;
	}

	if (!ep->ob_window_map) {
		ep->ob_window_map = devm_bitmap_zalloc(dev, pci->num_ob_windows,
						       GFP_KERNEL);
		if (!ep->ob_window_map)
			goto err_remove_edma;
	}

	if (!ep->outbound_addr) {
		addr = devm_kcalloc(dev, pci->num_ob_windows, sizeof(phys_addr_t),
				    GFP_KERNEL);
		if (!addr)
			goto err_remove_edma;
		ep->outbound_addr = addr;
	}

	for (func_no = 0; func_no < epc->max_functions; func_no++) {

		ep_func = dw_pcie6_ep_get_func_from_ep(ep, func_no);
		if (ep_func)
			continue;

		ep_func = devm_kzalloc(dev, sizeof(*ep_func), GFP_KERNEL);
		if (!ep_func)
			goto err_remove_edma;

		ep_func->func_no = func_no;
		ep_func->msi_cap = dw_pcie6_ep_find_capability(ep, func_no,
							      PCI_CAP_ID_MSI);
		ep_func->msix_cap = dw_pcie6_ep_find_capability(ep, func_no,
							       PCI_CAP_ID_MSIX);

		list_add_tail(&ep_func->list, &ep->func_list);
	}

	if (ep->ops->init)
		ep->ops->init(ep);

	ptm_cap_base = dw_pcie6_ep_find_ext_capability(pci, PCI_EXT_CAP_ID_PTM);

	/*
	 * PTM responder capability can be disabled only after disabling
	 * PTM root capability.
	 */
	if (ptm_cap_base) {
		dw_pcie6_dbi_ro_wr_en(pci);
		reg = dw_pcie6_readl_dbi(pci, ptm_cap_base + PCI_PTM_CAP);
		reg &= ~PCI_PTM_CAP_ROOT;
		dw_pcie6_writel_dbi(pci, ptm_cap_base + PCI_PTM_CAP, reg);

		reg = dw_pcie6_readl_dbi(pci, ptm_cap_base + PCI_PTM_CAP);
		reg &= ~(PCI_PTM_CAP_RES | PCI_PTM_GRANULARITY_MASK);
		dw_pcie6_writel_dbi(pci, ptm_cap_base + PCI_PTM_CAP, reg);
		dw_pcie6_dbi_ro_wr_dis(pci);
	}

	dw_pcie6_ep_init_non_sticky_registers(pci);

	return 0;

err_remove_edma:
	dw_pcie6_edma_remove(pci);

	return ret;
}
EXPORT_SYMBOL_GPL(dw_pcie6_ep_init_registers);

/**
 * dw_pcie6_ep_linkup - Notify EPF drivers about Link Up event
 * @ep: DWC EP device
 */
void dw_pcie6_ep_linkup(struct dw_pcie6_ep *ep)
{
	struct pci_epc *epc = ep->epc;

	pci_epc_linkup(epc);
}
EXPORT_SYMBOL_GPL(dw_pcie6_ep_linkup);

/**
 * dw_pcie6_ep_linkdown - Notify EPF drivers about Link Down event
 * @ep: DWC EP device
 *
 * Non-sticky registers are also initialized before sending the notification to
 * the EPF drivers. This is needed since the registers need to be initialized
 * before the link comes back again.
 */
void dw_pcie6_ep_linkdown(struct dw_pcie6_ep *ep)
{
	struct dw_pcie6 *pci = to_dw_pcie6_from_ep(ep);
	struct pci_epc *epc = ep->epc;

	/*
	 * Initialize the non-sticky DWC registers as they would've reset post
	 * Link Down. This is specifically needed for drivers not supporting
	 * PERST# as they have no way to reinitialize the registers before the
	 * link comes back again.
	 */
	dw_pcie6_ep_init_non_sticky_registers(pci);

	pci_epc_linkdown(epc);
}
EXPORT_SYMBOL_GPL(dw_pcie6_ep_linkdown);

/**
 * dw_pcie6_ep_init - Initialize the endpoint device
 * @ep: DWC EP device
 *
 * Initialize the endpoint device. Allocate resources and create the EPC
 * device with the endpoint framework.
 *
 * Return: 0 if success, errno otherwise.
 */
int dw_pcie6_ep_init(struct dw_pcie6_ep *ep)
{
	int ret;
	struct resource *res;
	struct pci_epc *epc;
	struct dw_pcie6 *pci = to_dw_pcie6_from_ep(ep);
	struct device *dev = pci->dev;
	struct platform_device *pdev = to_platform_device(dev);
	struct device_node *np = dev->of_node;

	INIT_LIST_HEAD(&ep->func_list);

	ret = dw_pcie6_get_resources(pci);
	if (ret)
		return ret;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "addr_space");
	if (!res)
		return -EINVAL;

	ep->phys_base = res->start;
	ep->addr_size = resource_size(res);

	if (ep->ops->pre_init)
		ep->ops->pre_init(ep);

	epc = devm_pci_epc_create(dev, &epc_ops);
	if (IS_ERR(epc)) {
		dev_err(dev, "Failed to create epc device\n");
		return PTR_ERR(epc);
	}

	ep->epc = epc;
	epc_set_drvdata(epc, ep);

	ret = of_property_read_u8(np, "max-functions", &epc->max_functions);
	if (ret < 0)
		epc->max_functions = 1;

	ret = pci_epc_mem_init(epc, ep->phys_base, ep->addr_size,
			       ep->page_size);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize address space\n");
		return ret;
	}

	ep->msi_mem = pci_epc_mem_alloc_addr(epc, &ep->msi_mem_phys,
					     epc->mem->window.page_size);
	if (!ep->msi_mem) {
		ret = -ENOMEM;
		dev_err(dev, "Failed to reserve memory for MSI/MSI-X\n");
		goto err_exit_epc_mem;
	}

	return 0;

err_exit_epc_mem:
	pci_epc_mem_exit(epc);

	return ret;
}
EXPORT_SYMBOL_GPL(dw_pcie6_ep_init);
