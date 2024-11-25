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
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/sizes.h>
#include <linux/types.h>

#include "../../pci.h"
#include "pcie6-designware.h"

#include <linux/pci-epc.h>
#include <linux/pci-epf.h>

/*** PCIe Designware ***/

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

int pcie6_rcar_get_link_speed(struct device_node *node)
{
	u32 max_link_speed;

	if (of_property_read_u32(node, "max-link-speed", &max_link_speed) ||
	    max_link_speed == 0 || max_link_speed > 6)
		return -EINVAL;

	return max_link_speed;
}
EXPORT_SYMBOL_GPL(pcie6_rcar_get_link_speed);

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

static inline void __iomem *dw_pcie6_select_atu(struct dw_pcie6 *pci, u32 dir,
					       u32 index)
{
	if (pci->iatu_unroll_enabled)
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

static int __dw_pcie6_prog_outbound_atu(struct dw_pcie6 *pci, u8 func_no,
				       int index, int type, u64 cpu_addr,
				       u64 pci_addr, u64 size)
{
	u32 retries, val;
	u64 limit_addr;

	if (pci->ops && pci->ops->cpu_addr_fixup)
		cpu_addr = pci->ops->cpu_addr_fixup(pci, cpu_addr);

	limit_addr = cpu_addr + size - 1;

	if ((limit_addr & ~pci->region_limit) != (cpu_addr & ~pci->region_limit) ||
	    !IS_ALIGNED(cpu_addr, pci->region_align) ||
	    !IS_ALIGNED(pci_addr, pci->region_align) || !size) {
		return -EINVAL;
	}

	dw_pcie6_writel_atu_ob(pci, index, PCIE_ATU_LOWER_BASE,
			      lower_32_bits(cpu_addr));
	dw_pcie6_writel_atu_ob(pci, index, PCIE_ATU_UPPER_BASE,
			      upper_32_bits(cpu_addr));

	dw_pcie6_writel_atu_ob(pci, index, PCIE_ATU_LIMIT,
			      lower_32_bits(limit_addr));
	if (dw_pcie6_ver_is_ge(pci, 460A))
		dw_pcie6_writel_atu_ob(pci, index, PCIE_ATU_UPPER_LIMIT,
				      upper_32_bits(limit_addr));

	dw_pcie6_writel_atu_ob(pci, index, PCIE_ATU_LOWER_TARGET,
			      lower_32_bits(pci_addr));
	dw_pcie6_writel_atu_ob(pci, index, PCIE_ATU_UPPER_TARGET,
			      upper_32_bits(pci_addr));

	val = type | PCIE_ATU_FUNC_NUM(func_no);
	if (upper_32_bits(limit_addr) > upper_32_bits(cpu_addr) &&
	    dw_pcie6_ver_is_ge(pci, 460A))
		val |= PCIE_ATU_INCREASE_REGION_SIZE;
	if (dw_pcie6_ver_is(pci, 490A))
		val = dw_pcie6_enable_ecrc(val);
	dw_pcie6_writel_atu_ob(pci, index, PCIE_ATU_REGION_CTRL1, val);

	dw_pcie6_writel_atu_ob(pci, index, PCIE_ATU_REGION_CTRL2, PCIE_ATU_ENABLE);

	/*
	 * Make sure ATU enable takes effect before any subsequent config
	 * and I/O accesses.
	 */
	for (retries = 0; retries < LINK_WAIT_MAX_IATU_RETRIES; retries++) {
		val = dw_pcie6_readl_atu_ob(pci, index, PCIE_ATU_REGION_CTRL2);
		if (val & PCIE_ATU_ENABLE)
			return 0;

		mdelay(LINK_WAIT_IATU);
	}

	dev_err(pci->dev, "Outbound iATU is not being enabled\n");

	return -ETIMEDOUT;
}

int dw_pcie6_prog_outbound_atu(struct dw_pcie6 *pci, int index, int type,
			      u64 cpu_addr, u64 pci_addr, u64 size)
{
	return __dw_pcie6_prog_outbound_atu(pci, 0, index, type,
					   cpu_addr, pci_addr, size);
}

int dw_pcie6_prog_ep_outbound_atu(struct dw_pcie6 *pci, u8 func_no, int index,
				 int type, u64 cpu_addr, u64 pci_addr,
				 u64 size)
{
	return __dw_pcie6_prog_outbound_atu(pci, func_no, index, type,
					   cpu_addr, pci_addr, size);
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

int dw_pcie6_prog_inbound_atu(struct dw_pcie6 *pci, u8 func_no, int index,
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

		usleep_range(LINK_WAIT_USLEEP_MIN, LINK_WAIT_USLEEP_MAX);
	}

	if (retries >= LINK_WAIT_MAX_RETRIES) {
		dev_err(pci->dev, "Phy link never came up\n");
		return -ETIMEDOUT;
	}

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

static void dw_pcie6_link_set_max_speed(struct dw_pcie6 *pci, u32 link_gen)
{
	u32 cap, ctrl2, link_speed;
	u8 offset = dw_pcie6_find_capability(pci, PCI_CAP_ID_EXP);

	cap = dw_pcie6_readl_dbi(pci, offset + PCI_EXP_LNKCAP);
	ctrl2 = dw_pcie6_readl_dbi(pci, offset + PCI_EXP_LNKCTL2);
	ctrl2 &= ~PCI_EXP_LNKCTL2_TLS;

	switch (pcie_link_speed[link_gen]) {
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
	case PCIE_SPEED_32_0GT:
		link_speed = PCI_EXP_LNKCTL2_TLS_32_0GT;
		break;
	case PCIE_SPEED_64_0GT:
		link_speed = PCI_EXP_LNKCTL2_TLS_64_0GT;
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

static bool dw_pcie6_iatu_unroll_enabled(struct dw_pcie6 *pci)
{
	u32 val;

	val = dw_pcie6_readl_dbi(pci, PCIE_ATU_VIEWPORT);
	if (val == 0xffffffff)
		return true;

	return false;
}

static void dw_pcie6_iatu_detect_regions(struct dw_pcie6 *pci)
{
	int max_region, ob, ib;
	u32 val, min, dir;
	u64 max;

	if (pci->iatu_unroll_enabled) {
		max_region = min((int)pci->atu_size / 512, 256);
	} else {
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
}

void dw_pcie6_iatu_detect(struct dw_pcie6 *pci)
{
	struct platform_device *pdev = to_platform_device(pci->dev);

	pci->iatu_unroll_enabled = dw_pcie6_iatu_unroll_enabled(pci);
	if (pci->iatu_unroll_enabled) {
		if (!pci->atu_base) {
			struct resource *res =
				platform_get_resource_byname(pdev, IORESOURCE_MEM, "atu");
			if (res) {
				pci->atu_size = resource_size(res);
				pci->atu_base = devm_ioremap_resource(pci->dev, res);
			}
			if (!pci->atu_base || IS_ERR(pci->atu_base))
				pci->atu_base = pci->dbi_base + DEFAULT_DBI_ATU_OFFSET;
		}

		if (!pci->atu_size)
			/* Pick a minimal default, enough for 8 in and 8 out windows */
			pci->atu_size = SZ_4K;
	} else {
		pci->atu_base = pci->dbi_base + PCIE_ATU_VIEWPORT_BASE;
		pci->atu_size = PCIE_ATU_VIEWPORT_SIZE;
	}

	dw_pcie6_iatu_detect_regions(pci);

	dev_info(pci->dev, "iATU unroll: %s\n", pci->iatu_unroll_enabled ?
		"enabled" : "disabled");

	dev_info(pci->dev, "iATU regions: %u ob, %u ib, align %uK, limit %lluG\n",
		 pci->num_ob_windows, pci->num_ib_windows,
		 pci->region_align / SZ_1K, (pci->region_limit + 1) / SZ_1G);
}

void dw_pcie6_setup(struct dw_pcie6 *pci)
{
	struct device_node *np = pci->dev->of_node;
	u32 val;

	if (pci->link_gen > 0)
		dw_pcie6_link_set_max_speed(pci, pci->link_gen);

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

	if (of_property_read_bool(np, "snps,enable-cdm-check")) {
		val = dw_pcie6_readl_dbi(pci, PCIE_PL_CHK_REG_CONTROL_STATUS);
		val |= PCIE_PL_CHK_REG_CHK_REG_CONTINUOUS |
		       PCIE_PL_CHK_REG_CHK_REG_START;
		dw_pcie6_writel_dbi(pci, PCIE_PL_CHK_REG_CONTROL_STATUS, val);
	}

	val = dw_pcie6_readl_dbi(pci, PCIE_PORT_LINK_CONTROL);
	val &= ~PORT_LINK_FAST_LINK_MODE;
	val |= PORT_LINK_DLL_LINK_EN;
	dw_pcie6_writel_dbi(pci, PCIE_PORT_LINK_CONTROL, val);

	of_property_read_u32(np, "num-lanes", &pci->num_lanes);
	if (!pci->num_lanes) {
		dev_dbg(pci->dev, "Using h/w default number of lanes\n");
		return;
	}

	/* Set the number of lanes */
	val &= ~PORT_LINK_FAST_LINK_MODE;
	val &= ~PORT_LINK_MODE_MASK;
	switch (pci->num_lanes) {
	case 1:
		val |= PORT_LINK_MODE_1_LANES;
		break;
	case 2:
		val |= PORT_LINK_MODE_2_LANES;
		break;
	case 4:
		val |= PORT_LINK_MODE_4_LANES;
		break;
	case 8:
		val |= PORT_LINK_MODE_8_LANES;
		break;
	default:
		dev_err(pci->dev, "num-lanes %u: invalid value\n", pci->num_lanes);
		return;
	}
	dw_pcie6_writel_dbi(pci, PCIE_PORT_LINK_CONTROL, val);

	/* Set link width speed control register */
	val = dw_pcie6_readl_dbi(pci, PCIE_LINK_WIDTH_SPEED_CONTROL);
	val &= ~PORT_LOGIC_LINK_WIDTH_MASK;
	switch (pci->num_lanes) {
	case 1:
		val |= PORT_LOGIC_LINK_WIDTH_1_LANES;
		break;
	case 2:
		val |= PORT_LOGIC_LINK_WIDTH_2_LANES;
		break;
	case 4:
		val |= PORT_LOGIC_LINK_WIDTH_4_LANES;
		break;
	case 8:
		val |= PORT_LOGIC_LINK_WIDTH_8_LANES;
		break;
	}
	dw_pcie6_writel_dbi(pci, PCIE_LINK_WIDTH_SPEED_CONTROL, val);
}

/*** PCIe Designware Host ***/

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
	.name = "PCI-MSI",
	.irq_ack = dw_msi_ack_irq,
	.irq_mask = dw_msi_mask_irq,
	.irq_unmask = dw_msi_unmask_irq,
};

static struct msi_domain_info dw_pcie6_msi_domain_info = {
	.flags	= (MSI_FLAG_USE_DEF_DOM_OPS | MSI_FLAG_USE_DEF_CHIP_OPS |
		   MSI_FLAG_PCI_MSIX | MSI_FLAG_MULTI_PCI_MSI),
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

static void dw_pci_setup_msi_msg(struct irq_data *d, struct msi_msg *msg)
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

static int dw_pci_msi_set_affinity(struct irq_data *d,
				   const struct cpumask *mask, bool force)
{
	return -EINVAL;
}

static void dw_pci_bottom_mask(struct irq_data *d)
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

static void dw_pci_bottom_unmask(struct irq_data *d)
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
	.irq_compose_msi_msg = dw_pci_setup_msi_msg,
	.irq_set_affinity = dw_pci_msi_set_affinity,
	.irq_mask = dw_pci_bottom_mask,
	.irq_unmask = dw_pci_bottom_unmask,
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
	u64 *msi_vaddr;
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

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (ret)
		dev_warn(dev, "Failed to set DMA mask to 32-bit. Devices with only 32-bit MSI support may not work properly\n");

	msi_vaddr = dmam_alloc_coherent(dev, sizeof(u64), &pp->msi_data,
					GFP_KERNEL);
	if (!msi_vaddr) {
		dev_err(dev, "Failed to alloc and map MSI data\n");
		dw_pcie6_free_msi(pp);
		return -ENOMEM;
	}

	return 0;
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

	if (!pci->dbi_base) {
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dbi");
		pci->dbi_base = devm_pci_remap_cfg_resource(dev, res);
		if (IS_ERR(pci->dbi_base))
			return PTR_ERR(pci->dbi_base);
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

	if (pci->link_gen < 1)
		pci->link_gen = of_pci_get_max_link_speed(np);

	/* Set default bus ops */
	bridge->ops = &dw_pcie6_ops;
	bridge->child_ops = &dw_child_pcie_ops;

	if (pp->ops->host_init) {
		ret = pp->ops->host_init(pp);
		if (ret)
			return ret;
	}

	if (pci_msi_enabled()) {
		pp->has_msi_ctrl = !(pp->ops->msi_host_init ||
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

		if (pp->ops->msi_host_init) {
			ret = pp->ops->msi_host_init(pp);
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

	ret = dw_pcie6_setup_rc(pp);
	if (ret)
		goto err_free_msi;

	if (!dw_pcie6_link_up(pci)) {
		ret = dw_pcie6_start_link(pci);
		if (ret)
			goto err_free_msi;
	}

	/* Ignore errors, the link may come up later */
	dw_pcie6_wait_for_link(pci);

	bridge->sysdata = pp;

	ret = pci_host_probe(bridge);
	if (ret)
		goto err_stop_link;

	return 0;

err_stop_link:
	dw_pcie6_stop_link(pci);

err_free_msi:
	if (pp->has_msi_ctrl)
		dw_pcie6_free_msi(pp);

err_deinit_host:
	if (pp->ops->host_deinit)
		pp->ops->host_deinit(pp);

	return ret;
}
EXPORT_SYMBOL_GPL(dw_pcie6_host_init);

void dw_pcie6_host_deinit(struct dw_pcie6_rp *pp)
{
	struct dw_pcie6 *pci = to_dw_pcie6_from_pp(pp);

	pci_stop_root_bus(pp->bridge->bus);
	pci_remove_root_bus(pp->bridge->bus);

	dw_pcie6_stop_link(pci);

	if (pp->has_msi_ctrl)
		dw_pcie6_free_msi(pp);

	if (pp->ops->host_deinit)
		pp->ops->host_deinit(pp);
}
EXPORT_SYMBOL_GPL(dw_pcie6_host_deinit);

static void __iomem *dw_pcie6_other_conf_map_bus(struct pci_bus *bus,
						unsigned int devfn, int where)
{
	struct dw_pcie6_rp *pp = bus->sysdata;
	struct dw_pcie6 *pci = to_dw_pcie6_from_pp(pp);
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

	ret = dw_pcie6_prog_outbound_atu(pci, 0, type, pp->cfg0_base, busdev,
					pp->cfg0_size);
	if (ret)
		return NULL;

	return pp->va_cfg0_base + where;
}

static int dw_pcie6_rd_other_conf(struct pci_bus *bus, unsigned int devfn,
				 int where, int size, u32 *val)
{
	struct dw_pcie6_rp *pp = bus->sysdata;
	struct dw_pcie6 *pci = to_dw_pcie6_from_pp(pp);
	int ret;

	ret = pci_generic_config_read(bus, devfn, where, size, val);
	if (ret != PCIBIOS_SUCCESSFUL)
		return ret;

	if (pp->cfg0_io_shared) {
		ret = dw_pcie6_prog_outbound_atu(pci, 0, PCIE_ATU_TYPE_IO,
						pp->io_base, pp->io_bus_addr,
						pp->io_size);
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
	int ret;

	ret = pci_generic_config_write(bus, devfn, where, size, val);
	if (ret != PCIBIOS_SUCCESSFUL)
		return ret;

	if (pp->cfg0_io_shared) {
		ret = dw_pcie6_prog_outbound_atu(pci, 0, PCIE_ATU_TYPE_IO,
						pp->io_base, pp->io_bus_addr,
						pp->io_size);
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
	struct resource_entry *entry;
	int i, ret;

	/* Note the very first outbound ATU is used for CFG IOs */
	if (!pci->num_ob_windows) {
		dev_err(pci->dev, "No outbound iATU found\n");
		return -EINVAL;
	}

	/*
	 * Ensure all outbound windows are disabled before proceeding with
	 * the MEM/IO ranges setups.
	 */
	for (i = 0; i < pci->num_ob_windows; i++)
		dw_pcie6_disable_atu(pci, PCIE_ATU_REGION_DIR_OB, i);

	i = 0;
	resource_list_for_each_entry(entry, &pp->bridge->windows) {
		if (resource_type(entry->res) != IORESOURCE_MEM)
			continue;

		if (pci->num_ob_windows <= ++i)
			break;

		ret = dw_pcie6_prog_outbound_atu(pci, i, PCIE_ATU_TYPE_MEM,
						entry->res->start,
						entry->res->start - entry->offset,
						resource_size(entry->res));
		if (ret) {
			dev_err(pci->dev, "Failed to set MEM range %pr\n",
				entry->res);
			return ret;
		}
	}

	if (pp->io_size) {
		if (pci->num_ob_windows > ++i) {
			ret = dw_pcie6_prog_outbound_atu(pci, i, PCIE_ATU_TYPE_IO,
							pp->io_base,
							pp->io_bus_addr,
							pp->io_size);
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
		dev_warn(pci->dev, "Resources exceed number of ATU entries (%d)\n",
			 pci->num_ob_windows);

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

/*** PCIe Designware Endpoint ***/

void dw_pcie6_ep_linkup(struct dw_pcie6_ep *ep)
{
	struct pci_epc *epc = ep->epc;

	pci_epc_linkup(epc);
}
EXPORT_SYMBOL_GPL(dw_pcie6_ep_linkup);

void dw_pcie6_ep_init_notify(struct dw_pcie6_ep *ep)
{
	struct pci_epc *epc = ep->epc;

	pci_epc_init_notify(epc);
}
EXPORT_SYMBOL_GPL(dw_pcie6_ep_init_notify);

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

static unsigned int dw_pcie6_ep_func_select(struct dw_pcie6_ep *ep, u8 func_no)
{
	unsigned int func_offset = 0;

	if (ep->ops->func_conf_select)
		func_offset = ep->ops->func_conf_select(ep, func_no);

	return func_offset;
}

static void __dw_pcie6_ep_reset_bar(struct dw_pcie6 *pci, u8 func_no,
				   enum pci_barno bar, int flags)
{
	u32 reg;
	unsigned int func_offset = 0;
	struct dw_pcie6_ep *ep = &pci->ep;

	func_offset = dw_pcie6_ep_func_select(ep, func_no);

	reg = func_offset + PCI_BASE_ADDRESS_0 + (4 * bar);
	dw_pcie6_dbi_ro_wr_en(pci);
	dw_pcie6_writel_dbi2(pci, reg, 0x0);
	dw_pcie6_writel_dbi(pci, reg, 0x0);
	if (flags & PCI_BASE_ADDRESS_MEM_TYPE_64) {
		dw_pcie6_writel_dbi2(pci, reg + 4, 0x0);
		dw_pcie6_writel_dbi(pci, reg + 4, 0x0);
	}
	dw_pcie6_dbi_ro_wr_dis(pci);
}

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
	struct dw_pcie6 *pci = to_dw_pcie6_from_ep(ep);
	unsigned int func_offset = 0;
	u8 cap_id, next_cap_ptr;
	u16 reg;

	if (!cap_ptr)
		return 0;

	func_offset = dw_pcie6_ep_func_select(ep, func_no);

	reg = dw_pcie6_readw_dbi(pci, func_offset + cap_ptr);
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
	struct dw_pcie6 *pci = to_dw_pcie6_from_ep(ep);
	unsigned int func_offset = 0;
	u8 next_cap_ptr;
	u16 reg;

	func_offset = dw_pcie6_ep_func_select(ep, func_no);

	reg = dw_pcie6_readw_dbi(pci, func_offset + PCI_CAPABILITY_LIST);
	next_cap_ptr = (reg & 0x00ff);

	return __dw_pcie6_ep_find_next_cap(ep, func_no, next_cap_ptr, cap);
}

static int dw_pcie6_ep_write_header(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
				   struct pci_epf_header *hdr)
{
	struct dw_pcie6_ep *ep = epc_get_drvdata(epc);
	struct dw_pcie6 *pci = to_dw_pcie6_from_ep(ep);
	unsigned int func_offset = 0;

	func_offset = dw_pcie6_ep_func_select(ep, func_no);

	dw_pcie6_dbi_ro_wr_en(pci);
	dw_pcie6_writew_dbi(pci, func_offset + PCI_VENDOR_ID, hdr->vendorid);
	dw_pcie6_writew_dbi(pci, func_offset + PCI_DEVICE_ID, hdr->deviceid);
	dw_pcie6_writeb_dbi(pci, func_offset + PCI_REVISION_ID, hdr->revid);
	dw_pcie6_writeb_dbi(pci, func_offset + PCI_CLASS_PROG, hdr->progif_code);
	dw_pcie6_writew_dbi(pci, func_offset + PCI_CLASS_DEVICE,
			   hdr->subclass_code | hdr->baseclass_code << 8);
	dw_pcie6_writeb_dbi(pci, func_offset + PCI_CACHE_LINE_SIZE,
			   hdr->cache_line_size);
	dw_pcie6_writew_dbi(pci, func_offset + PCI_SUBSYSTEM_VENDOR_ID,
			   hdr->subsys_vendor_id);
	dw_pcie6_writew_dbi(pci, func_offset + PCI_SUBSYSTEM_ID, hdr->subsys_id);
	dw_pcie6_writeb_dbi(pci, func_offset + PCI_INTERRUPT_PIN,
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
		free_win = ep->bar_to_atu[bar];

	if (free_win >= pci->num_ib_windows) {
		dev_err(pci->dev, "No free inbound window\n");
		return -EINVAL;
	}

	ret = dw_pcie6_prog_inbound_atu(pci, func_no, free_win, type,
				       cpu_addr, bar);
	if (ret < 0) {
		dev_err(pci->dev, "Failed to program IB window\n");
		return ret;
	}

	ep->bar_to_atu[bar] = free_win;
	set_bit(free_win, ep->ib_window_map);

	return 0;
}

static int dw_pcie6_ep_outbound_atu(struct dw_pcie6_ep *ep, u8 func_no,
				   phys_addr_t phys_addr,
				   u64 pci_addr, size_t size)
{
	struct dw_pcie6 *pci = to_dw_pcie6_from_ep(ep);
	u32 free_win;
	int ret;

	free_win = find_first_zero_bit(ep->ob_window_map, pci->num_ob_windows);
	if (free_win >= pci->num_ob_windows) {
		dev_err(pci->dev, "No free outbound window\n");
		return -EINVAL;
	}

	ret = dw_pcie6_prog_ep_outbound_atu(pci, func_no, free_win, PCIE_ATU_TYPE_MEM,
					   phys_addr, pci_addr, size);
	if (ret)
		return ret;

	set_bit(free_win, ep->ob_window_map);
	ep->outbound_addr[free_win] = phys_addr;

	return 0;
}

static void dw_pcie6_ep_clear_bar(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
				 struct pci_epf_bar *epf_bar)
{
	struct dw_pcie6_ep *ep = epc_get_drvdata(epc);
	struct dw_pcie6 *pci = to_dw_pcie6_from_ep(ep);
	enum pci_barno bar = epf_bar->barno;
	u32 atu_index = ep->bar_to_atu[bar];

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
	unsigned int func_offset = 0;
	int ret, type;
	u32 reg;

	func_offset = dw_pcie6_ep_func_select(ep, func_no);

	reg = PCI_BASE_ADDRESS_0 + (4 * bar) + func_offset;

	if (!(flags & PCI_BASE_ADDRESS_SPACE))
		type = PCIE_ATU_TYPE_MEM;
	else
		type = PCIE_ATU_TYPE_IO;

	ret = dw_pcie6_ep_inbound_atu(ep, func_no, type, epf_bar->phys_addr, bar);
	if (ret)
		return ret;

	if (ep->epf_bar[bar])
		return 0;

	dw_pcie6_dbi_ro_wr_en(pci);

	dw_pcie6_writel_dbi2(pci, reg, lower_32_bits(size - 1));
	dw_pcie6_writel_dbi(pci, reg, flags);

	if (flags & PCI_BASE_ADDRESS_MEM_TYPE_64) {
		dw_pcie6_writel_dbi2(pci, reg + 4, upper_32_bits(size - 1));
		dw_pcie6_writel_dbi(pci, reg + 4, 0);
	}

	ep->epf_bar[bar] = epf_bar;
	dw_pcie6_dbi_ro_wr_dis(pci);

	return 0;
}

static int dw_pcie6_find_index(struct dw_pcie6_ep *ep, phys_addr_t addr,
			      u32 *atu_index)
{
	u32 index;
	struct dw_pcie6 *pci = to_dw_pcie6_from_ep(ep);

	for (index = 0; index < pci->num_ob_windows; index++) {
		if (ep->outbound_addr[index] != addr)
			continue;
		*atu_index = index;
		return 0;
	}

	return -EINVAL;
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

	ret = dw_pcie6_ep_outbound_atu(ep, func_no, addr, pci_addr, size);
	if (ret) {
		dev_err(pci->dev, "Failed to enable address\n");
		return ret;
	}

	return 0;
}

static int dw_pcie6_ep_get_msi(struct pci_epc *epc, u8 func_no, u8 vfunc_no)
{
	struct dw_pcie6_ep *ep = epc_get_drvdata(epc);
	struct dw_pcie6 *pci = to_dw_pcie6_from_ep(ep);
	u32 val, reg;
	unsigned int func_offset = 0;
	struct dw_pcie6_ep_func *ep_func;

	ep_func = dw_pcie6_ep_get_func_from_ep(ep, func_no);
	if (!ep_func || !ep_func->msi_cap)
		return -EINVAL;

	func_offset = dw_pcie6_ep_func_select(ep, func_no);

	reg = ep_func->msi_cap + func_offset + PCI_MSI_FLAGS;
	val = dw_pcie6_readw_dbi(pci, reg);
	if (!(val & PCI_MSI_FLAGS_ENABLE))
		return -EINVAL;

	val = (val & PCI_MSI_FLAGS_QSIZE) >> 4;

	return val;
}

static int dw_pcie6_ep_set_msi(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
			      u8 interrupts)
{
	struct dw_pcie6_ep *ep = epc_get_drvdata(epc);
	struct dw_pcie6 *pci = to_dw_pcie6_from_ep(ep);
	u32 val, reg;
	unsigned int func_offset = 0;
	struct dw_pcie6_ep_func *ep_func;

	ep_func = dw_pcie6_ep_get_func_from_ep(ep, func_no);
	if (!ep_func || !ep_func->msi_cap)
		return -EINVAL;

	func_offset = dw_pcie6_ep_func_select(ep, func_no);

	reg = ep_func->msi_cap + func_offset + PCI_MSI_FLAGS;
	val = dw_pcie6_readw_dbi(pci, reg);
	val &= ~PCI_MSI_FLAGS_QMASK;
	val |= (interrupts << 1) & PCI_MSI_FLAGS_QMASK;
	dw_pcie6_dbi_ro_wr_en(pci);
	dw_pcie6_writew_dbi(pci, reg, val);
	dw_pcie6_dbi_ro_wr_dis(pci);

	return 0;
}

static int dw_pcie6_ep_get_msix(struct pci_epc *epc, u8 func_no, u8 vfunc_no)
{
	struct dw_pcie6_ep *ep = epc_get_drvdata(epc);
	struct dw_pcie6 *pci = to_dw_pcie6_from_ep(ep);
	u32 val, reg;
	unsigned int func_offset = 0;
	struct dw_pcie6_ep_func *ep_func;

	ep_func = dw_pcie6_ep_get_func_from_ep(ep, func_no);
	if (!ep_func || !ep_func->msix_cap)
		return -EINVAL;

	func_offset = dw_pcie6_ep_func_select(ep, func_no);

	reg = ep_func->msix_cap + func_offset + PCI_MSIX_FLAGS;
	val = dw_pcie6_readw_dbi(pci, reg);
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
	u32 val, reg;
	unsigned int func_offset = 0;
	struct dw_pcie6_ep_func *ep_func;

	ep_func = dw_pcie6_ep_get_func_from_ep(ep, func_no);
	if (!ep_func || !ep_func->msix_cap)
		return -EINVAL;

	dw_pcie6_dbi_ro_wr_en(pci);

	func_offset = dw_pcie6_ep_func_select(ep, func_no);

	reg = ep_func->msix_cap + func_offset + PCI_MSIX_FLAGS;
	val = dw_pcie6_readw_dbi(pci, reg);
	val &= ~PCI_MSIX_FLAGS_QSIZE;
	val |= interrupts;
	dw_pcie6_writew_dbi(pci, reg, val);

	reg = ep_func->msix_cap + func_offset + PCI_MSIX_TABLE;
	val = offset | bir;
	dw_pcie6_writel_dbi(pci, reg, val);

	reg = ep_func->msix_cap + func_offset + PCI_MSIX_PBA;
	val = (offset + (interrupts * PCI_MSIX_ENTRY_SIZE)) | bir;
	dw_pcie6_writel_dbi(pci, reg, val);

	dw_pcie6_dbi_ro_wr_dis(pci);

	return 0;
}

static int dw_pcie6_ep_raise_irq(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
				enum pci_epc_irq_type type, u16 interrupt_num)
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

int dw_pcie6_ep_raise_legacy_irq(struct dw_pcie6_ep *ep, u8 func_no)
{
	struct dw_pcie6 *pci = to_dw_pcie6_from_ep(ep);
	struct device *dev = pci->dev;

	dev_err(dev, "EP cannot trigger legacy IRQs\n");

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(dw_pcie6_ep_raise_legacy_irq);

int dw_pcie6_ep_raise_msi_irq(struct dw_pcie6_ep *ep, u8 func_no,
			     u8 interrupt_num)
{
	struct dw_pcie6 *pci = to_dw_pcie6_from_ep(ep);
	struct dw_pcie6_ep_func *ep_func;
	struct pci_epc *epc = ep->epc;
	unsigned int aligned_offset;
	unsigned int func_offset = 0;
	u16 msg_ctrl, msg_data;
	u32 msg_addr_lower, msg_addr_upper, reg;
	u64 msg_addr;
	bool has_upper;
	int ret;

	ep_func = dw_pcie6_ep_get_func_from_ep(ep, func_no);
	if (!ep_func || !ep_func->msi_cap)
		return -EINVAL;

	func_offset = dw_pcie6_ep_func_select(ep, func_no);

	/* Raise MSI per the PCI Local Bus Specification Revision 3.0, 6.8.1. */
	reg = ep_func->msi_cap + func_offset + PCI_MSI_FLAGS;
	msg_ctrl = dw_pcie6_readw_dbi(pci, reg);
	has_upper = !!(msg_ctrl & PCI_MSI_FLAGS_64BIT);
	reg = ep_func->msi_cap + func_offset + PCI_MSI_ADDRESS_LO;
	msg_addr_lower = dw_pcie6_readl_dbi(pci, reg);
	if (has_upper) {
		reg = ep_func->msi_cap + func_offset + PCI_MSI_ADDRESS_HI;
		msg_addr_upper = dw_pcie6_readl_dbi(pci, reg);
		reg = ep_func->msi_cap + func_offset + PCI_MSI_DATA_64;
		msg_data = dw_pcie6_readw_dbi(pci, reg);
	} else {
		msg_addr_upper = 0;
		reg = ep_func->msi_cap + func_offset + PCI_MSI_DATA_32;
		msg_data = dw_pcie6_readw_dbi(pci, reg);
	}
	aligned_offset = msg_addr_lower & (epc->mem->window.page_size - 1);
	msg_addr = ((u64)msg_addr_upper) << 32 |
			(msg_addr_lower & ~aligned_offset);
	ret = dw_pcie6_ep_map_addr(epc, func_no, 0, ep->msi_mem_phys, msg_addr,
				  epc->mem->window.page_size);
	if (ret)
		return ret;

	writel(msg_data | (interrupt_num - 1), ep->msi_mem + aligned_offset);

	dw_pcie6_ep_unmap_addr(epc, func_no, 0, ep->msi_mem_phys);

	return 0;
}
EXPORT_SYMBOL_GPL(dw_pcie6_ep_raise_msi_irq);

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
EXPORT_SYMBOL_GPL(dw_pcie6_ep_raise_msix_irq);

int dw_pcie6_ep_raise_msix_irq(struct dw_pcie6_ep *ep, u8 func_no,
			      u16 interrupt_num)
{
	struct dw_pcie6 *pci = to_dw_pcie6_from_ep(ep);
	struct dw_pcie6_ep_func *ep_func;
	struct pci_epf_msix_tbl *msix_tbl;
	struct pci_epc *epc = ep->epc;
	unsigned int func_offset = 0;
	u32 reg, msg_data, vec_ctrl;
	unsigned int aligned_offset;
	u32 tbl_offset;
	u64 msg_addr;
	int ret;
	u8 bir;

	ep_func = dw_pcie6_ep_get_func_from_ep(ep, func_no);
	if (!ep_func || !ep_func->msix_cap)
		return -EINVAL;

	func_offset = dw_pcie6_ep_func_select(ep, func_no);

	reg = ep_func->msix_cap + func_offset + PCI_MSIX_TABLE;
	tbl_offset = dw_pcie6_readl_dbi(pci, reg);
	bir = (tbl_offset & PCI_MSIX_TABLE_BIR);
	tbl_offset &= PCI_MSIX_TABLE_OFFSET;

	msix_tbl = ep->epf_bar[bir]->addr + tbl_offset;
	msg_addr = msix_tbl[(interrupt_num - 1)].msg_addr;
	msg_data = msix_tbl[(interrupt_num - 1)].msg_data;
	vec_ctrl = msix_tbl[(interrupt_num - 1)].vector_ctrl;

	if (vec_ctrl & PCI_MSIX_ENTRY_CTRL_MASKBIT) {
		dev_dbg(pci->dev, "MSI-X entry ctrl set\n");
		return -EPERM;
	}

	aligned_offset = msg_addr & (epc->mem->window.page_size - 1);
	msg_addr = ALIGN_DOWN(msg_addr, epc->mem->window.page_size);
	ret = dw_pcie6_ep_map_addr(epc, func_no, 0, ep->msi_mem_phys, msg_addr,
				  epc->mem->window.page_size);
	if (ret)
		return ret;

	writel(msg_data, ep->msi_mem + aligned_offset);

	dw_pcie6_ep_unmap_addr(epc, func_no, 0, ep->msi_mem_phys);

	return 0;
}

void dw_pcie6_ep_exit(struct dw_pcie6_ep *ep)
{
	struct pci_epc *epc = ep->epc;

	pci_epc_mem_free_addr(epc, ep->msi_mem_phys, ep->msi_mem,
			      epc->mem->window.page_size);

	pci_epc_mem_exit(epc);
}

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

int dw_pcie6_ep_init_complete(struct dw_pcie6_ep *ep)
{
	struct dw_pcie6 *pci = to_dw_pcie6_from_ep(ep);
	unsigned int offset;
	unsigned int nbars;
	u8 hdr_type;
	u32 reg;
	int i;

	hdr_type = dw_pcie6_readb_dbi(pci, PCI_HEADER_TYPE) &
		   PCI_HEADER_TYPE_MASK;
	if (hdr_type != PCI_HEADER_TYPE_NORMAL) {
		dev_err(pci->dev,
			"PCIe controller is not set to EP mode (hdr_type:0x%x)!\n",
			hdr_type);
		return -EIO;
	}

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

	return 0;
}
EXPORT_SYMBOL_GPL(dw_pcie6_ep_init_complete);

int dw_pcie6_ep_init(struct dw_pcie6_ep *ep)
{
	int ret;
	void *addr;
	u8 func_no;
	struct resource *res;
	struct pci_epc *epc;
	struct dw_pcie6 *pci = to_dw_pcie6_from_ep(ep);
	struct device *dev = pci->dev;
	struct platform_device *pdev = to_platform_device(dev);
	struct device_node *np = dev->of_node;
	const struct pci_epc_features *epc_features;
	struct dw_pcie6_ep_func *ep_func;

	INIT_LIST_HEAD(&ep->func_list);

	if (!pci->dbi_base) {
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dbi");
		pci->dbi_base = devm_pci_remap_cfg_resource(dev, res);
		if (IS_ERR(pci->dbi_base))
			return PTR_ERR(pci->dbi_base);
	}

	if (!pci->dbi_base2) {
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dbi2");
		if (!res) {
			pci->dbi_base2 = pci->dbi_base + SZ_4K;
		} else {
			pci->dbi_base2 = devm_pci_remap_cfg_resource(dev, res);
			if (IS_ERR(pci->dbi_base2))
				return PTR_ERR(pci->dbi_base2);
		}
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "addr_space");
	if (!res)
		return -EINVAL;

	ep->phys_base = res->start;
	ep->addr_size = resource_size(res);

	dw_pcie6_version_detect(pci);

	dw_pcie6_iatu_detect(pci);

	ep->ib_window_map = devm_bitmap_zalloc(dev, pci->num_ib_windows,
					       GFP_KERNEL);
	if (!ep->ib_window_map)
		return -ENOMEM;

	ep->ob_window_map = devm_bitmap_zalloc(dev, pci->num_ob_windows,
					       GFP_KERNEL);
	if (!ep->ob_window_map)
		return -ENOMEM;

	addr = devm_kcalloc(dev, pci->num_ob_windows, sizeof(phys_addr_t),
			    GFP_KERNEL);
	if (!addr)
		return -ENOMEM;
	ep->outbound_addr = addr;

	if (pci->link_gen < 1)
		pci->link_gen = of_pci_get_max_link_speed(np);

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

	for (func_no = 0; func_no < epc->max_functions; func_no++) {
		ep_func = devm_kzalloc(dev, sizeof(*ep_func), GFP_KERNEL);
		if (!ep_func)
			return -ENOMEM;

		ep_func->func_no = func_no;
		ep_func->msi_cap = dw_pcie6_ep_find_capability(ep, func_no,
							      PCI_CAP_ID_MSI);
		ep_func->msix_cap = dw_pcie6_ep_find_capability(ep, func_no,
							       PCI_CAP_ID_MSIX);

		list_add_tail(&ep_func->list, &ep->func_list);
	}

	if (ep->ops->ep_init)
		ep->ops->ep_init(ep);

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

	if (ep->ops->get_features) {
		epc_features = ep->ops->get_features(ep);
		if (epc_features->core_init_notifier)
			return 0;
	}

	ret = dw_pcie6_ep_init_complete(ep);
	if (ret)
		goto err_free_epc_mem;

	return 0;

err_free_epc_mem:
	pci_epc_mem_free_addr(epc, ep->msi_mem_phys, ep->msi_mem,
			      epc->mem->window.page_size);

err_exit_epc_mem:
	pci_epc_mem_exit(epc);

	return ret;
}
EXPORT_SYMBOL_GPL(dw_pcie6_ep_init);
