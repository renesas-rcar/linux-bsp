/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Synopsys DesignWare PCIe host controller driver
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 *		https://www.samsung.com
 *
 * Author: Jingoo Han <jg1.han@samsung.com>
 */

#ifndef _PCIE_DESIGNWARE_H
#define _PCIE_DESIGNWARE_H

#include <linux/bitfield.h>
#include <linux/dma-mapping.h>
#include <linux/irq.h>
#include <linux/msi.h>
#include <linux/pci.h>

#include <linux/pci-epc.h>
#include <linux/pci-epf.h>

/* DWC PCIe IP-core versions (native support since v4.70a) */
#define DW_PCIE_VER_365A		0x3336352a
#define DW_PCIE_VER_460A		0x3436302a
#define DW_PCIE_VER_470A		0x3437302a
#define DW_PCIE_VER_480A		0x3438302a
#define DW_PCIE_VER_490A		0x3439302a
#define DW_PCIE_VER_520A		0x3532302a

#define __dw_pcie6_ver_cmp(_pci, _ver, _op) \
	((_pci)->version _op DW_PCIE_VER_ ## _ver)

#define dw_pcie6_ver_is(_pci, _ver) __dw_pcie6_ver_cmp(_pci, _ver, ==)

#define dw_pcie6_ver_is_ge(_pci, _ver) __dw_pcie6_ver_cmp(_pci, _ver, >=)

#define dw_pcie6_ver_type_is(_pci, _ver, _type) \
	(__dw_pcie6_ver_cmp(_pci, _ver, ==) && \
	 __dw_pcie6_ver_cmp(_pci, TYPE_ ## _type, ==))

#define dw_pcie6_ver_type_is_ge(_pci, _ver, _type) \
	(__dw_pcie6_ver_cmp(_pci, _ver, ==) && \
	 __dw_pcie6_ver_cmp(_pci, TYPE_ ## _type, >=))

/* Parameters for the waiting for link up routine */
#define LINK_WAIT_MAX_RETRIES		10
#define LINK_WAIT_USLEEP_MIN		90000
#define LINK_WAIT_USLEEP_MAX		100000

/* Parameters for the waiting for iATU enabled routine */
#define LINK_WAIT_MAX_IATU_RETRIES	5
#define LINK_WAIT_IATU			9

/* Synopsys-specific PCIe configuration registers */
#define PCIE_PORT_AFR			0x70C
#define PORT_AFR_N_FTS_MASK		GENMASK(15, 8)
#define PORT_AFR_N_FTS(n)		FIELD_PREP(PORT_AFR_N_FTS_MASK, n)
#define PORT_AFR_CC_N_FTS_MASK		GENMASK(23, 16)
#define PORT_AFR_CC_N_FTS(n)		FIELD_PREP(PORT_AFR_CC_N_FTS_MASK, n)
#define PORT_AFR_ENTER_ASPM		BIT(30)
#define PORT_AFR_L0S_ENTRANCE_LAT_SHIFT	24
#define PORT_AFR_L0S_ENTRANCE_LAT_MASK	GENMASK(26, 24)
#define PORT_AFR_L1_ENTRANCE_LAT_SHIFT	27
#define PORT_AFR_L1_ENTRANCE_LAT_MASK	GENMASK(29, 27)

#define PCIE_PORT_LINK_CONTROL		0x710
#define PORT_LINK_DLL_LINK_EN		BIT(5)
#define PORT_LINK_FAST_LINK_MODE	BIT(7)
#define PORT_LINK_MODE_MASK		GENMASK(21, 16)
#define PORT_LINK_MODE(n)		FIELD_PREP(PORT_LINK_MODE_MASK, n)
#define PORT_LINK_MODE_1_LANES		PORT_LINK_MODE(0x1)
#define PORT_LINK_MODE_2_LANES		PORT_LINK_MODE(0x3)
#define PORT_LINK_MODE_4_LANES		PORT_LINK_MODE(0x7)
#define PORT_LINK_MODE_8_LANES		PORT_LINK_MODE(0xf)

#define PCIE_PORT_DEBUG0		0x728
#define PORT_LOGIC_LTSSM_STATE_MASK	0x1f
#define PORT_LOGIC_LTSSM_STATE_L0	0x11
#define PCIE_PORT_DEBUG1		0x72C
#define PCIE_PORT_DEBUG1_LINK_UP		BIT(4)
#define PCIE_PORT_DEBUG1_LINK_IN_TRAINING	BIT(29)

#define PCIE_LINK_WIDTH_SPEED_CONTROL	0x80C
#define PORT_LOGIC_N_FTS_MASK		GENMASK(7, 0)
#define PORT_LOGIC_SPEED_CHANGE		BIT(17)
#define PORT_LOGIC_LINK_WIDTH_MASK	GENMASK(12, 8)
#define PORT_LOGIC_LINK_WIDTH(n)	FIELD_PREP(PORT_LOGIC_LINK_WIDTH_MASK, n)
#define PORT_LOGIC_LINK_WIDTH_1_LANES	PORT_LOGIC_LINK_WIDTH(0x1)
#define PORT_LOGIC_LINK_WIDTH_2_LANES	PORT_LOGIC_LINK_WIDTH(0x2)
#define PORT_LOGIC_LINK_WIDTH_4_LANES	PORT_LOGIC_LINK_WIDTH(0x4)
#define PORT_LOGIC_LINK_WIDTH_8_LANES	PORT_LOGIC_LINK_WIDTH(0x8)

#define PCIE_MSI_ADDR_LO		0x820
#define PCIE_MSI_ADDR_HI		0x824
#define PCIE_MSI_INTR0_ENABLE		0x828
#define PCIE_MSI_INTR0_MASK		0x82C
#define PCIE_MSI_INTR0_STATUS		0x830

#define GEN3_RELATED_OFF			0x890
#define GEN3_RELATED_OFF_GEN3_ZRXDC_NONCOMPL	BIT(0)
#define GEN3_RELATED_OFF_RXEQ_RGRDLESS_RXTS	BIT(13)
#define GEN3_RELATED_OFF_GEN3_EQ_DISABLE	BIT(16)
#define GEN3_RELATED_OFF_RATE_SHADOW_SEL_SHIFT	24
#define GEN3_RELATED_OFF_RATE_SHADOW_SEL_MASK	GENMASK(25, 24)

#define PCIE_PORT_MULTI_LANE_CTRL	0x8C0
#define PORT_MLTI_UPCFG_SUPPORT		BIT(7)

#define PCIE_VERSION_NUMBER		0x8F8
#define PCIE_VERSION_TYPE		0x8FC

/*
 * iATU inbound and outbound windows CSRs. Before the IP-core v4.80a each
 * iATU region CSRs had been indirectly accessible by means of the dedicated
 * viewport selector. The iATU/eDMA CSRs space was re-designed in DWC PCIe
 * v4.80a in a way so the viewport was unrolled into the directly accessible
 * iATU/eDMA CSRs space.
 */
#define PCIE_ATU_VIEWPORT		0x900
#define PCIE_ATU_REGION_DIR_IB		BIT(31)
#define PCIE_ATU_REGION_DIR_OB		0
#define PCIE_ATU_VIEWPORT_BASE		0x904
#define PCIE_ATU_UNROLL_BASE(dir, index) \
	(((index) << 9) | ((dir == PCIE_ATU_REGION_DIR_IB) ? BIT(8) : 0))
#define PCIE_ATU_VIEWPORT_SIZE		0x2C
#define PCIE_ATU_REGION_CTRL1		0x000
#define PCIE_ATU_INCREASE_REGION_SIZE	BIT(13)
#define PCIE_ATU_TYPE_MEM		0x0
#define PCIE_ATU_TYPE_IO		0x2
#define PCIE_ATU_TYPE_CFG0		0x4
#define PCIE_ATU_TYPE_CFG1		0x5
#define PCIE_ATU_TD			BIT(8)
#define PCIE_ATU_FUNC_NUM(pf)           ((pf) << 20)
#define PCIE_ATU_REGION_CTRL2		0x004
#define PCIE_ATU_ENABLE			BIT(31)
#define PCIE_ATU_BAR_MODE_ENABLE	BIT(30)
#define PCIE_ATU_FUNC_NUM_MATCH_EN      BIT(19)
#define PCIE_ATU_LOWER_BASE		0x008
#define PCIE_ATU_UPPER_BASE		0x00C
#define PCIE_ATU_LIMIT			0x010
#define PCIE_ATU_LOWER_TARGET		0x014
#define PCIE_ATU_BUS(x)			FIELD_PREP(GENMASK(31, 24), x)
#define PCIE_ATU_DEV(x)			FIELD_PREP(GENMASK(23, 19), x)
#define PCIE_ATU_FUNC(x)		FIELD_PREP(GENMASK(18, 16), x)
#define PCIE_ATU_UPPER_TARGET		0x018
#define PCIE_ATU_UPPER_LIMIT		0x020

#define PCIE_MISC_CONTROL_1_OFF		0x8BC
#define PCIE_DBI_RO_WR_EN		BIT(0)

#define PCIE_MSIX_DOORBELL		0x948
#define PCIE_MSIX_DOORBELL_PF_SHIFT	24

#define PCIE_PL_CHK_REG_CONTROL_STATUS			0xB20
#define PCIE_PL_CHK_REG_CHK_REG_START			BIT(0)
#define PCIE_PL_CHK_REG_CHK_REG_CONTINUOUS		BIT(1)
#define PCIE_PL_CHK_REG_CHK_REG_COMPARISON_ERROR	BIT(16)
#define PCIE_PL_CHK_REG_CHK_REG_LOGIC_ERROR		BIT(17)
#define PCIE_PL_CHK_REG_CHK_REG_COMPLETE		BIT(18)

#define PCIE_PL_CHK_REG_ERR_ADDR			0xB28

/*
 * iATU Unroll-specific register definitions
 * From 4.80 core version the address translation will be made by unroll
 */
#define PCIE_ATU_UNR_REGION_CTRL1	0x00
#define PCIE_ATU_UNR_REGION_CTRL2	0x04
#define PCIE_ATU_UNR_LOWER_BASE		0x08
#define PCIE_ATU_UNR_UPPER_BASE		0x0C
#define PCIE_ATU_UNR_LOWER_LIMIT	0x10
#define PCIE_ATU_UNR_LOWER_TARGET	0x14
#define PCIE_ATU_UNR_UPPER_TARGET	0x18
#define PCIE_ATU_UNR_UPPER_LIMIT	0x20

/*
 * RAS-DES register definitions
 */
#define PCIE_RAS_DES_EVENT_COUNTER_CONTROL	0x8
#define EVENT_COUNTER_ALL_CLEAR		0x3
#define EVENT_COUNTER_ENABLE_ALL	0x7
#define EVENT_COUNTER_ENABLE_SHIFT	2
#define EVENT_COUNTER_EVENT_SEL_MASK	GENMASK(7, 0)
#define EVENT_COUNTER_EVENT_SEL_SHIFT	16
#define EVENT_COUNTER_EVENT_Tx_L0S	0x2
#define EVENT_COUNTER_EVENT_Rx_L0S	0x3
#define EVENT_COUNTER_EVENT_L1		0x5
#define EVENT_COUNTER_EVENT_L1_1	0x7
#define EVENT_COUNTER_EVENT_L1_2	0x8
#define EVENT_COUNTER_GROUP_SEL_SHIFT	24
#define EVENT_COUNTER_GROUP_5		0x5

#define PCIE_RAS_DES_EVENT_COUNTER_DATA		0xc

/*
 * The default address offset between dbi_base and atu_base. Root controller
 * drivers are not required to initialize atu_base if the offset matches this
 * default; the driver core automatically derives atu_base from dbi_base using
 * this offset, if atu_base not set.
 */
#define DEFAULT_DBI_ATU_OFFSET (0x3 << 20)

#define MAX_MSI_IRQS			256
#define MAX_MSI_IRQS_PER_CTRL		32
#define MAX_MSI_CTRLS			(MAX_MSI_IRQS / MAX_MSI_IRQS_PER_CTRL)
#define MSI_REG_CTRL_BLOCK_SIZE		12
#define MSI_DEF_NUM_VECTORS		32

/* Maximum number of inbound/outbound iATUs */
#define MAX_IATU_IN			256
#define MAX_IATU_OUT			256

struct dw_pcie6;
struct dw_pcie6_rp;
struct dw_pcie6_ep;

enum dw_pcie6_device_mode {
	DW_PCIE_UNKNOWN_TYPE,
	DW_PCIE_EP_TYPE,
	DW_PCIE_LEG_EP_TYPE,
	DW_PCIE_RC_TYPE,
};

struct dw_pcie6_host_ops {
	int (*host_init)(struct dw_pcie6_rp *pp);
	void (*host_deinit)(struct dw_pcie6_rp *pp);
	int (*msi_host_init)(struct dw_pcie6_rp *pp);
};

struct dw_pcie6_rp {
	bool			has_msi_ctrl:1;
	bool			cfg0_io_shared:1;
	u64			cfg0_base;
	void __iomem		*va_cfg0_base;
	u32			cfg0_size;
	resource_size_t		io_base;
	phys_addr_t		io_bus_addr;
	u32			io_size;
	int			irq;
	const struct dw_pcie6_host_ops *ops;
	int			msi_irq[MAX_MSI_CTRLS];
	struct irq_domain	*irq_domain;
	struct irq_domain	*msi_domain;
	dma_addr_t		msi_data;
	struct irq_chip		*msi_irq_chip;
	u32			num_vectors;
	u32			irq_mask[MAX_MSI_CTRLS];
	struct pci_host_bridge  *bridge;
	raw_spinlock_t		lock;
	DECLARE_BITMAP(msi_irq_in_use, MAX_MSI_IRQS);
};

struct dw_pcie6_ep_ops {
	void	(*ep_init)(struct dw_pcie6_ep *ep);
	int	(*raise_irq)(struct dw_pcie6_ep *ep, u8 func_no,
			     enum pci_epc_irq_type type, u16 interrupt_num);
	const struct pci_epc_features* (*get_features)(struct dw_pcie6_ep *ep);
	/*
	 * Provide a method to implement the different func config space
	 * access for different platform, if different func have different
	 * offset, return the offset of func. if use write a register way
	 * return a 0, and implement code in callback function of platform
	 * driver.
	 */
	unsigned int (*func_conf_select)(struct dw_pcie6_ep *ep, u8 func_no);
};

struct dw_pcie6_ep_func {
	struct list_head	list;
	u8			func_no;
	u8			msi_cap;	/* MSI capability offset */
	u8			msix_cap;	/* MSI-X capability offset */
};

struct dw_pcie6_ep {
	struct pci_epc		*epc;
	struct list_head	func_list;
	const struct dw_pcie6_ep_ops *ops;
	phys_addr_t		phys_base;
	size_t			addr_size;
	size_t			page_size;
	u8			bar_to_atu[PCI_STD_NUM_BARS];
	phys_addr_t		*outbound_addr;
	unsigned long		*ib_window_map;
	unsigned long		*ob_window_map;
	void __iomem		*msi_mem;
	phys_addr_t		msi_mem_phys;
	struct pci_epf_bar	*epf_bar[PCI_STD_NUM_BARS];
};

struct dw_pcie6_ops {
	u64	(*cpu_addr_fixup)(struct dw_pcie6 *pcie, u64 cpu_addr);
	u32	(*read_dbi)(struct dw_pcie6 *pcie, void __iomem *base, u32 reg,
			    size_t size);
	void	(*write_dbi)(struct dw_pcie6 *pcie, void __iomem *base, u32 reg,
			     size_t size, u32 val);
	void    (*write_dbi2)(struct dw_pcie6 *pcie, void __iomem *base, u32 reg,
			      size_t size, u32 val);
	int	(*link_up)(struct dw_pcie6 *pcie);
	int	(*start_link)(struct dw_pcie6 *pcie);
	void	(*stop_link)(struct dw_pcie6 *pcie);
};

struct dw_pcie6 {
	struct device		*dev;
	void __iomem		*dbi_base;
	void __iomem		*dbi_base2;
	void __iomem		*atu_base;
	size_t			atu_size;
	u32			num_ib_windows;
	u32			num_ob_windows;
	u32			region_align;
	u64			region_limit;
	struct dw_pcie6_rp	pp;
	struct dw_pcie6_ep	ep;
	const struct dw_pcie6_ops *ops;
	u32			version;
	u32			type;
	int			num_lanes;
	int			link_gen;
	u8			n_fts[2];
	bool			iatu_unroll_enabled: 1;
};

struct dw_plat_pcie6 {
	struct dw_pcie6			*pci;
	enum dw_pcie6_device_mode	mode;
};


#define to_dw_pcie6_from_pp(port) container_of((port), struct dw_pcie6, pp)

#define to_dw_pcie6_from_ep(endpoint)   \
		container_of((endpoint), struct dw_pcie6, ep)

void dw_pcie6_version_detect(struct dw_pcie6 *pci);

u8 dw_pcie6_find_capability(struct dw_pcie6 *pci, u8 cap);
u16 dw_pcie6_find_ext_capability(struct dw_pcie6 *pci, u8 cap);

int dw_pcie6_read(void __iomem *addr, int size, u32 *val);
int dw_pcie6_write(void __iomem *addr, int size, u32 val);

u32 dw_pcie6_read_dbi(struct dw_pcie6 *pci, u32 reg, size_t size);
void dw_pcie6_write_dbi(struct dw_pcie6 *pci, u32 reg, size_t size, u32 val);
void dw_pcie6_write_dbi2(struct dw_pcie6 *pci, u32 reg, size_t size, u32 val);
int dw_pcie6_link_up(struct dw_pcie6 *pci);
void dw_pcie6_upconfig_setup(struct dw_pcie6 *pci);
int dw_pcie6_wait_for_link(struct dw_pcie6 *pci);
int dw_pcie6_prog_outbound_atu(struct dw_pcie6 *pci, int index, int type,
			      u64 cpu_addr, u64 pci_addr, u64 size);
int dw_pcie6_prog_ep_outbound_atu(struct dw_pcie6 *pci, u8 func_no, int index,
				 int type, u64 cpu_addr, u64 pci_addr, u64 size);
int dw_pcie6_prog_inbound_atu(struct dw_pcie6 *pci, u8 func_no, int index,
			     int type, u64 cpu_addr, u8 bar);
void dw_pcie6_disable_atu(struct dw_pcie6 *pci, u32 dir, int index);
void dw_pcie6_setup(struct dw_pcie6 *pci);
void dw_pcie6_iatu_detect(struct dw_pcie6 *pci);

static inline void dw_pcie6_writel_dbi(struct dw_pcie6 *pci, u32 reg, u32 val)
{
	dw_pcie6_write_dbi(pci, reg, 0x4, val);
}

static inline u32 dw_pcie6_readl_dbi(struct dw_pcie6 *pci, u32 reg)
{
	return dw_pcie6_read_dbi(pci, reg, 0x4);
}

static inline void dw_pcie6_writew_dbi(struct dw_pcie6 *pci, u32 reg, u16 val)
{
	dw_pcie6_write_dbi(pci, reg, 0x2, val);
}

static inline u16 dw_pcie6_readw_dbi(struct dw_pcie6 *pci, u32 reg)
{
	return dw_pcie6_read_dbi(pci, reg, 0x2);
}

static inline void dw_pcie6_writeb_dbi(struct dw_pcie6 *pci, u32 reg, u8 val)
{
	dw_pcie6_write_dbi(pci, reg, 0x1, val);
}

static inline u8 dw_pcie6_readb_dbi(struct dw_pcie6 *pci, u32 reg)
{
	return dw_pcie6_read_dbi(pci, reg, 0x1);
}

static inline void dw_pcie6_writel_dbi2(struct dw_pcie6 *pci, u32 reg, u32 val)
{
	dw_pcie6_write_dbi2(pci, reg, 0x4, val);
}

static inline void dw_pcie6_dbi_ro_wr_en(struct dw_pcie6 *pci)
{
	u32 reg;
	u32 val;

	reg = PCIE_MISC_CONTROL_1_OFF;
	val = dw_pcie6_readl_dbi(pci, reg);
	val |= PCIE_DBI_RO_WR_EN;
	dw_pcie6_writel_dbi(pci, reg, val);
}

static inline void dw_pcie6_dbi_ro_wr_dis(struct dw_pcie6 *pci)
{
	u32 reg;
	u32 val;

	reg = PCIE_MISC_CONTROL_1_OFF;
	val = dw_pcie6_readl_dbi(pci, reg);
	val &= ~PCIE_DBI_RO_WR_EN;
	dw_pcie6_writel_dbi(pci, reg, val);
}

static inline int dw_pcie6_start_link(struct dw_pcie6 *pci)
{
	if (pci->ops && pci->ops->start_link)
		return pci->ops->start_link(pci);

	return 0;
}

static inline void dw_pcie6_stop_link(struct dw_pcie6 *pci)
{
	if (pci->ops && pci->ops->stop_link)
		pci->ops->stop_link(pci);
}

#ifdef CONFIG_PCIE6_DW
irqreturn_t dw_pcie6_handle_msi_irq(struct dw_pcie6_rp *pp);
int dw_pcie6_setup_rc(struct dw_pcie6_rp *pp);
int dw_pcie6_host_init(struct dw_pcie6_rp *pp);
void dw_pcie6_host_deinit(struct dw_pcie6_rp *pp);
int dw_pcie6_allocate_domains(struct dw_pcie6_rp *pp);
void __iomem *dw_pcie6_own_conf_map_bus(struct pci_bus *bus, unsigned int devfn,
				       int where);
void dw_pcie6_ep_linkup(struct dw_pcie6_ep *ep);
int dw_pcie6_ep_init(struct dw_pcie6_ep *ep);
int dw_pcie6_ep_init_complete(struct dw_pcie6_ep *ep);
void dw_pcie6_ep_init_notify(struct dw_pcie6_ep *ep);
void dw_pcie6_ep_exit(struct dw_pcie6_ep *ep);
int dw_pcie6_ep_raise_legacy_irq(struct dw_pcie6_ep *ep, u8 func_no);
int dw_pcie6_ep_raise_msi_irq(struct dw_pcie6_ep *ep, u8 func_no,
			     u8 interrupt_num);
int dw_pcie6_ep_raise_msix_irq(struct dw_pcie6_ep *ep, u8 func_no,
			     u16 interrupt_num);
int dw_pcie6_ep_raise_msix_irq_doorbell(struct dw_pcie6_ep *ep, u8 func_no,
				       u16 interrupt_num);
void dw_pcie6_ep_reset_bar(struct dw_pcie6 *pci, enum pci_barno bar);
struct dw_pcie6_ep_func *
dw_pcie6_ep_get_func_from_ep(struct dw_pcie6_ep *ep, u8 func_no);

#endif
#endif /* _PCIE_DESIGNWARE_H */
