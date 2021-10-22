// SPDX-License-Identifier: GPL-2.0
/*
 * IOMMU API for Renesas VMSA-compatible IPMMU
 *
 * Copyright (C) 2020 Renesas Electronics Corporation
 */

#include <linux/bitmap.h>
#include <linux/delay.h>
#include <linux/dma-iommu.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/io-pgtable.h>
#include <linux/iommu.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_iommu.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/sys_soc.h>
#include <linux/pci.h>

#include "ipmmu-vmsa.h"

#define IPMMU_CTX_MAX		16U
#define IPMMU_CTX_INVALID	-1
#define IPMMU_UTLB_MAX		63U

#define TLB_LOOP_TIMEOUT		100	/* 100us */

/* -----------------------------------------------------------------------------
 * Registers Definition
 */

#define IM_NS_ALIAS_OFFSET		0x800

#define IM_CTX_SIZE(n)			((n) < 8 ? \
					IM_CTX_SIZE0(n) : IM_CTX_SIZE8(n))
#define IM_CTX_SIZE0(n)			(((n) * 64) + ((n) * 4096))
#define IM_CTX_SIZE8(n)			((((n) - 8) * 64) + ((n) * 4096))

#define IMCTR(n)			((n) < 8 ? IMCTR0 : IMCTR8)
#define IMCTR0				0x10000
#define IMCTR8				0x10800
#define IMCTR_TRE			(1 << 17)
#define IMCTR_AFE			(1 << 16)
#define IMCTR_RTSEL_MASK		(7 << 4)
#define IMCTR_RTSEL_SHIFT		4
#define IMCTR_TREN			(1 << 3)
#define IMCTR_INTEN			(1 << 2)
#define IMCTR_FLUSH			(1 << 1)
#define IMCTR_MMUEN			(1 << 0)

#define IMCAAR				0x0004

#define IMTTBCR(n)			((n) < 8 ? IMTTBCR0 : IMTTBCR8)
#define IMTTBCR0			0x10008
#define IMTTBCR8			0x10808
#define IMTTBCR_EAE			(1 << 31)
#define IMTTBCR_PMB			(1 << 30)
#define IMTTBCR_TSZ1_MASK		(7 << 16)
#define IMTTBCR_TSZ1_SHIFT		16
#define IMTTBCR_SH0_INNER_SHAREABLE	(3 << 12)	/* R-Car Gen2 only */
#define IMTTBCR_ORGN0_WB_WA		(1 << 10)	/* R-Car Gen2 only */
#define IMTTBCR_IRGN0_WB_WA		(1 << 8)	/* R-Car Gen2 only */
#define IMTTBCR_SL0_TWOBIT_LVL_3	(0 << 6)	/* R-Car Gen3 only */
#define IMTTBCR_SL0_TWOBIT_LVL_2	(1 << 6)	/* R-Car Gen3 only */
#define IMTTBCR_SL0_TWOBIT_LVL_1	(2 << 6)	/* R-Car Gen3 only */
#define IMTTBCR_SL0_LVL_2		(0 << 4)
#define IMTTBCR_SL0_LVL_1		(1 << 4)
#define IMTTBCR_TSZ0_MASK		(7 << 0)
#define IMTTBCR_TSZ0_SHIFT		O

#define IMBUSCR				0x000c
#define IMBUSCR_DVM			(1 << 2)
#define IMBUSCR_BUSSEL_SYS		(0 << 0)
#define IMBUSCR_BUSSEL_CCI		(1 << 0)
#define IMBUSCR_BUSSEL_IMCAAR		(2 << 0)
#define IMBUSCR_BUSSEL_CCI_IMCAAR	(3 << 0)
#define IMBUSCR_BUSSEL_MASK		(3 << 0)

#define IMTTLBR0(n)			((n) < 8 ? IMTTLBR0_0 : IMTTLBR0_8)
#define IMTTLBR0_0			0x10010
#define IMTTLBR0_8			0x10810
#define IMTTUBR0(n)			((n) < 8 ? IMTTUBR0_0 : IMTTUBR0_8)
#define IMTTUBR0_0			0x10014
#define IMTTUBR0_8			0x10814
#define IMTTLBR1			0x0018
#define IMTTUBR1			0x001c

#define IMSTR(n)			((n) < 8 ? IMSTR0 : IMSTR8)
#define IMSTR0				0x10020
#define IMSTR8				0x10820
#define IMSTR_ERRLVL_MASK		(3 << 12)
#define IMSTR_ERRLVL_SHIFT		12
#define IMSTR_ERRCODE_TLB_FORMAT	(1 << 8)
#define IMSTR_ERRCODE_ACCESS_PERM	(4 << 8)
#define IMSTR_ERRCODE_SECURE_ACCESS	(5 << 8)
#define IMSTR_ERRCODE_MASK		(7 << 8)
#define IMSTR_MHIT			(1 << 4)
#define IMSTR_ABORT			(1 << 2)
#define IMSTR_PF			(1 << 1)
#define IMSTR_TF			(1 << 0)

#define IMMAIR0(n)			((n) < 8 ? IMMAIR0_0 : IMMAIR0_8)
#define IMMAIR0_0			0x10028
#define IMMAIR0_8			0x10828
#define IMMAIR1				0x002c
#define IMMAIR_ATTR_MASK		0xff
#define IMMAIR_ATTR_DEVICE		0x04
#define IMMAIR_ATTR_NC			0x44
#define IMMAIR_ATTR_WBRWA		0xff
#define IMMAIR_ATTR_SHIFT(n)		((n) << 3)
#define IMMAIR_ATTR_IDX_NC		0
#define IMMAIR_ATTR_IDX_WBRWA		1
#define IMMAIR_ATTR_IDX_DEV		2

					/* IMEAR on R-Car Gen2 */
#define IMELAR(n)			((n) < 8 ? IMELAR0 : IMELAR8)
#define IMELAR0				0x10030
#define IMELAR8				0x10830
					/* R-Car Gen3 only */
#define IMEUAR(n)			((n) < 8 ? IMEUAR0 : IMEUAR8)
#define IMEUAR0				0x10034
#define IMEUAR8				0x10834

#define IMPCTR				0x0200
#define IMPSTR				0x0208
#define IMPEAR				0x020c
#define IMPMBA(n)			(0x0280 + ((n) * 4))
#define IMPMBD(n)			(0x02c0 + ((n) * 4))

#define IMUCTR(n)			((n) < 32 ? IMUCTR0(n) : IMUCTR32(n))
#define IMUCTR0(n)			(0x03300 + ((n) * 16))
#define IMUCTR32(n)			(0x03600 + (((n) - 32) * 16))
#define IMUCTR_FIXADDEN			(1 << 31)
#define IMUCTR_FIXADD_MASK		(0xff << 16)
#define IMUCTR_FIXADD_SHIFT		16
#define IMUCTR_TTSEL_MMU(n)		((n) << 4)
#define IMUCTR_TTSEL_PMB		(8 << 4)
#define IMUCTR_TTSEL_MASK		(15 << 4)
#define IMUCTR_FLUSH			(1 << 1)
#define IMUCTR_MMUEN			(1 << 0)

#define IMUASID(n)			((n) < 32 ? IMUASID0(n) : IMUASID32(n))
#define IMUASID0(n)			(0x03308 + ((n) * 16))
#define IMUASID32(n)			(0x03608 + (((n) - 32) * 16))
#define IMUASID_ASID8_MASK		(0xff << 8)
#define IMUASID_ASID8_SHIFT		8
#define IMUASID_ASID0_MASK		(0xff << 0)
#define IMUASID_ASID0_SHIFT		0

/* -----------------------------------------------------------------------------
 * Root device handling
 */

static struct platform_driver ipmmu_driver;

static bool ipmmu_is_root(struct ipmmu_vmsa_device *mmu)
{
	return mmu->root == mmu;
}

static int __ipmmu_check_device(struct device *dev, void *data)
{
	struct ipmmu_vmsa_device *mmu = dev_get_drvdata(dev);
	struct ipmmu_vmsa_device **rootp = data;

	if (ipmmu_is_root(mmu))
		*rootp = mmu;

	return 0;
}

static struct ipmmu_vmsa_device *ipmmu_find_root(void)
{
	struct ipmmu_vmsa_device *root = NULL;

	return driver_for_each_device(&ipmmu_driver.driver, NULL, &root,
				      __ipmmu_check_device) == 0 ? root : NULL;
}

/* -----------------------------------------------------------------------------
 * Read/Write Access
 */

static u32 ipmmu_ctx_read_root(struct ipmmu_vmsa_domain *domain,
			       unsigned int reg)
{
	return ipmmu_read(domain->mmu->root,
			  IM_CTX_SIZE(domain->context_id) + reg);
}

static void ipmmu_ctx_write_root(struct ipmmu_vmsa_domain *domain,
				 unsigned int reg, u32 data)
{
	ipmmu_write(domain->mmu->root,
		    IM_CTX_SIZE(domain->context_id) + reg, data);
}

static void ipmmu_ctx_write_all(struct ipmmu_vmsa_domain *domain,
				unsigned int reg, u32 data)
{
	if (domain->mmu != domain->mmu->root)
		ipmmu_write(domain->mmu,
			    IM_CTX_SIZE(domain->context_id) + reg, data);

	ipmmu_write(domain->mmu->root,
		    IM_CTX_SIZE(domain->context_id) + reg, data);
}

/* -----------------------------------------------------------------------------
 * TLB and microTLB Management
 */

/* Wait for any pending TLB invalidations to complete */
static void ipmmu_tlb_sync(struct ipmmu_vmsa_domain *domain)
{
	unsigned int count = 0;

	while (ipmmu_ctx_read_root(domain, IMCTR(domain->context_id))
					 & IMCTR_FLUSH) {
		cpu_relax();
		if (++count == TLB_LOOP_TIMEOUT) {
			dev_err_ratelimited(domain->mmu->dev,
			"TLB sync timed out -- MMU may be deadlocked\n");
			return;
		}
		udelay(1);
	}
}

static void ipmmu_tlb_invalidate(struct ipmmu_vmsa_domain *domain)
{
	u32 reg;

	reg = ipmmu_ctx_read_root(domain, IMCTR(domain->context_id));
	reg |= IMCTR_FLUSH;
	ipmmu_ctx_write_all(domain, IMCTR(domain->context_id), reg);

	ipmmu_tlb_sync(domain);
}

/*
 * Enable MMU translation for the microTLB.
 */
static void ipmmu_utlb_enable(struct ipmmu_vmsa_domain *domain,
			      unsigned int utlb)
{
	struct ipmmu_vmsa_device *mmu = domain->mmu;

	/*
	 * TODO: Reference-count the microTLB as several bus masters can be
	 * connected to the same microTLB.
	 */

	/* TODO: What should we set the ASID to ? */
	ipmmu_write(mmu, IMUASID(utlb), 0);
	/* TODO: Do we need to flush the microTLB ? */
	ipmmu_write(mmu, IMUCTR(utlb),
		    IMUCTR_TTSEL_MMU(domain->context_id) |
		    IMUCTR_MMUEN);
	mmu->utlb_ctx[utlb] = domain->context_id;
}

/*
 * Disable MMU translation for the microTLB.
 */
static void ipmmu_utlb_disable(struct ipmmu_vmsa_domain *domain,
			       unsigned int utlb)
{
	struct ipmmu_vmsa_device *mmu = domain->mmu;

	ipmmu_write(mmu, IMUCTR(utlb), 0);
	mmu->utlb_ctx[utlb] = IPMMU_CTX_INVALID;
}

static void ipmmu_tlb_flush_all(void *cookie)
{
	struct ipmmu_vmsa_domain *domain = cookie;

	ipmmu_tlb_invalidate(domain);
}

static void ipmmu_tlb_flush(unsigned long iova, size_t size,
				size_t granule, void *cookie)
{
	ipmmu_tlb_flush_all(cookie);
}

static const struct iommu_flush_ops ipmmu_flush_ops = {
	.tlb_flush_all = ipmmu_tlb_flush_all,
	.tlb_flush_walk = ipmmu_tlb_flush,
	.tlb_flush_leaf = ipmmu_tlb_flush,
};

/* -----------------------------------------------------------------------------
 * Domain/Context Management
 */

static void ipmmu_domain_setup_context(struct ipmmu_vmsa_domain *domain)
{
	u64 ttbr;
	u32 tmp;

	/* TTBR0 */
	ttbr = domain->cfg.arm_lpae_s1_cfg.ttbr[0];
	ipmmu_ctx_write_root(domain, IMTTLBR0(domain->context_id), ttbr);
	ipmmu_ctx_write_root(domain, IMTTUBR0(domain->context_id), ttbr >> 32);

	/*
	 * TTBCR
	 * We use long descriptors and allocate the whole 32-bit VA space to
	 * TTBR0.
	 */
	if (domain->mmu->features->twobit_imttbcr_sl0)
		tmp = IMTTBCR_SL0_TWOBIT_LVL_1;
	else
		tmp = IMTTBCR_SL0_LVL_1;

	if (domain->mmu->features->cache_snoop)
		tmp |= IMTTBCR_SH0_INNER_SHAREABLE | IMTTBCR_ORGN0_WB_WA |
		       IMTTBCR_IRGN0_WB_WA;

	ipmmu_ctx_write_root(domain, IMTTBCR(domain->context_id),
				     IMTTBCR_EAE | tmp);

	/* MAIR0 */
	ipmmu_ctx_write_root(domain, IMMAIR0(domain->context_id),
			     domain->cfg.arm_lpae_s1_cfg.mair[0]);

	/* IMBUSCR */
	if (domain->mmu->features->setup_imbuscr)
		ipmmu_ctx_write_root(domain, IMBUSCR,
				     ipmmu_ctx_read_root(domain, IMBUSCR) &
				     ~(IMBUSCR_DVM | IMBUSCR_BUSSEL_MASK));

	/*
	 * IMSTR
	 * Clear all interrupt flags.
	 */
	ipmmu_ctx_write_root(domain, IMSTR(domain->context_id),
			     ipmmu_ctx_read_root(domain,
						 IMSTR(domain->context_id)));

	/*
	 * IMCTR
	 * Enable the MMU and interrupt generation. The long-descriptor
	 * translation table format doesn't use TEX remapping. Don't enable AF
	 * software management as we have no use for it. Flush the TLB as
	 * required when modifying the context registers.
	 */
	ipmmu_ctx_write_all(domain, IMCTR(domain->context_id),
			    IMCTR_INTEN | IMCTR_FLUSH | IMCTR_MMUEN);
}

static int ipmmu_domain_init_context(struct ipmmu_vmsa_domain *domain)
{
	int ret;

	/*
	 * Allocate the page table operations.
	 *
	 * VMSA states in section B3.6.3 "Control of Secure or Non-secure memory
	 * access, Long-descriptor format" that the NStable bit being set in a
	 * table descriptor will result in the NStable and NS bits of all child
	 * entries being ignored and considered as being set. The IPMMU seems
	 * not to comply with this, as it generates a secure access page fault
	 * if any of the NStable and NS bits isn't set when running in
	 * non-secure mode.
	 */
	domain->cfg.quirks = IO_PGTABLE_QUIRK_ARM_NS;
	domain->cfg.pgsize_bitmap = SZ_1G | SZ_2M | SZ_4K;
	domain->cfg.ias = 32;
	domain->cfg.oas = 40;
	domain->cfg.tlb = &ipmmu_flush_ops;
	domain->io_domain.geometry.aperture_end = DMA_BIT_MASK(32);
	domain->io_domain.geometry.force_aperture = true;
	/*
	 * TODO: Add support for coherent walk through CCI with DVM and remove
	 * cache handling. For now, delegate it to the io-pgtable code.
	 */
	domain->cfg.coherent_walk = false;
	domain->cfg.iommu_dev = domain->mmu->root->dev;

	/*
	 * Find an unused context.
	 */
	ret = ipmmu_domain_allocate_context(domain->mmu->root, domain);
	if (ret < 0)
		return ret;

	domain->context_id = ret;

	domain->iop = alloc_io_pgtable_ops(ARM_32_LPAE_S1, &domain->cfg,
					   domain);
	if (!domain->iop) {
		ipmmu_domain_free_context(domain->mmu->root,
					  domain->context_id);
		return -EINVAL;
	}

	ipmmu_domain_setup_context(domain);
	return 0;
}

static void ipmmu_domain_destroy_context(struct ipmmu_vmsa_domain *domain)
{
	if (!domain->mmu)
		return;

	/*
	 * Disable the context. Flush the TLB as required when modifying the
	 * context registers.
	 *
	 * TODO: Is TLB flush really needed ?
	 */
	ipmmu_ctx_write_all(domain, IMCTR(domain->context_id), IMCTR_FLUSH);
	ipmmu_tlb_sync(domain);
	ipmmu_domain_free_context(domain->mmu->root, domain->context_id);
}

/* -----------------------------------------------------------------------------
 * Fault Handling
 */

static irqreturn_t ipmmu_domain_irq(struct ipmmu_vmsa_domain *domain)
{
	const u32 err_mask = IMSTR_MHIT | IMSTR_ABORT | IMSTR_PF | IMSTR_TF;
	struct ipmmu_vmsa_device *mmu = domain->mmu;
	unsigned long iova;
	u32 status;

	status = ipmmu_ctx_read_root(domain, IMSTR(domain->context_id));
	if (!(status & err_mask))
		return IRQ_NONE;

	iova = ipmmu_ctx_read_root(domain, IMELAR(domain->context_id));
	if (IS_ENABLED(CONFIG_64BIT))
		iova |= (u64)ipmmu_ctx_read_root(domain,
					  IMEUAR(domain->context_id)) << 32;

	/*
	 * Clear the error status flags. Unlike traditional interrupt flag
	 * registers that must be cleared by writing 1, this status register
	 * seems to require 0. The error address register must be read before,
	 * otherwise its value will be 0.
	 */
	ipmmu_ctx_write_root(domain, IMSTR(domain->context_id), 0);

	/* Log fatal errors. */
	if (status & IMSTR_MHIT)
		dev_err_ratelimited(mmu->dev, "Multiple TLB hits @0x%lx\n",
				    iova);
	if (status & IMSTR_ABORT)
		dev_err_ratelimited(mmu->dev, "Page Table Walk Abort @0x%lx\n",
				    iova);

	if (!(status & (IMSTR_PF | IMSTR_TF)))
		return IRQ_NONE;

	/* Flush the TLB as required when IPMMU translation error occurred. */
	ipmmu_tlb_invalidate(domain);

	/*
	 * Try to handle page faults and translation faults.
	 *
	 * TODO: We need to look up the faulty device based on the I/O VA. Use
	 * the IOMMU device for now.
	 */
	if (!report_iommu_fault(&domain->io_domain, mmu->dev, iova, 0))
		return IRQ_HANDLED;

	dev_err_ratelimited(mmu->dev,
			    "Unhandled fault: status 0x%08x iova 0x%lx\n",
			    status, iova);

	return IRQ_HANDLED;
}

static irqreturn_t ipmmu_irq(int irq, void *dev)
{
	struct ipmmu_vmsa_device *mmu = dev;
	irqreturn_t status = IRQ_NONE;
	unsigned int i;
	unsigned long flags;

	spin_lock_irqsave(&mmu->lock, flags);

	/*
	 * Check interrupts for all active contexts.
	 */
	for (i = 0; i < mmu->num_ctx; i++) {
		if (!mmu->domains[i])
			continue;
		if (ipmmu_domain_irq(mmu->domains[i]) == IRQ_HANDLED)
			status = IRQ_HANDLED;
	}

	spin_unlock_irqrestore(&mmu->lock, flags);

	return status;
}

/* -----------------------------------------------------------------------------
 * IOMMU Operations
 */

static void ipmmu_domain_free(struct iommu_domain *io_domain)
{
	struct ipmmu_vmsa_domain *domain = to_vmsa_domain(io_domain);

	/*
	 * Free the domain resources. We assume that all devices have already
	 * been detached.
	 */
	iommu_put_dma_cookie(io_domain);
	ipmmu_domain_destroy_context(domain);
	free_io_pgtable_ops(domain->iop);
	kfree(domain);
}

static int ipmmu_attach_device(struct iommu_domain *io_domain,
			       struct device *dev)
{
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
	struct ipmmu_vmsa_device *mmu = to_ipmmu(dev);
	struct ipmmu_vmsa_domain *domain = to_vmsa_domain(io_domain);
	unsigned int i;
	int ret = 0;

	if (!mmu) {
		dev_err(dev, "Cannot attach to IPMMU\n");
		return -ENXIO;
	}

	mutex_lock(&domain->mutex);

	if (!domain->mmu) {
		/* The domain hasn't been used yet, initialize it. */
		domain->mmu = mmu;
		ret = ipmmu_domain_init_context(domain);
		if (ret < 0) {
			dev_err(dev, "Unable to initialize IPMMU context\n");
			domain->mmu = NULL;
		} else {
			dev_info(dev, "Using IPMMU context %u\n",
				 domain->context_id);
		}
	} else if (domain->mmu != mmu) {
		/*
		 * Something is wrong, we can't attach two devices using
		 * different IOMMUs to the same domain.
		 */
		dev_err(dev, "Can't attach IPMMU %s to domain on IPMMU %s\n",
			dev_name(mmu->dev), dev_name(domain->mmu->dev));
		ret = -EINVAL;
	} else
		dev_info(dev, "Reusing IPMMU context %u\n", domain->context_id);

	mutex_unlock(&domain->mutex);

	if (ret < 0)
		return ret;

	for (i = 0; i < fwspec->num_ids; ++i)
		ipmmu_utlb_enable(domain, fwspec->ids[i]);

	return 0;
}

static void ipmmu_detach_device(struct iommu_domain *io_domain,
				struct device *dev)
{
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
	struct ipmmu_vmsa_domain *domain = to_vmsa_domain(io_domain);
	unsigned int i;

	for (i = 0; i < fwspec->num_ids; ++i)
		ipmmu_utlb_disable(domain, fwspec->ids[i]);

	/*
	 * TODO: Optimize by disabling the context when no device is attached.
	 */
}

static void ipmmu_flush_iotlb_all(struct iommu_domain *io_domain)
{
	struct ipmmu_vmsa_domain *domain = to_vmsa_domain(io_domain);

	if (domain->mmu)
		ipmmu_tlb_flush_all(domain);
}

static void ipmmu_iotlb_sync(struct iommu_domain *io_domain,
			     struct iommu_iotlb_gather *gather)
{
	ipmmu_flush_iotlb_all(io_domain);
}

static const char * const rcar_v3u_slave_whitelist[] = {
	"ffd60000.dma-controller",
	"ffd61000.dma-controller",
	"ffd62000.dma-controller",
	"ffd63000.dma-controller",
	"fea10000.fcp",
	"fea11000.fcp",
	"e7350000.dma-controller",
	"e7351000.dma-controller",
	"ee140000.mmc",
	"e6ef0000.video",
	"e6ef1000.video",
	"e6ef2000.video",
	"e6ef3000.video",
	"e6ef4000.video",
	"e6ef5000.video",
	"e6ef6000.video",
	"e6ef7000.video",
	"e6ef8000.video",
	"e6ef9000.video",
	"e6efa000.video",
	"e6efb000.video",
	"e6efc000.video",
	"e6efd000.video",
	"e6efe000.video",
	"e6eff000.video",
	"e6ed0000.video",
	"e6ed1000.video",
	"e6ed2000.video",
	"e6ed3000.video",
	"e6ed4000.video",
	"e6ed5000.video",
	"e6ed6000.video",
	"e6ed7000.video",
	"e6ed8000.video",
	"e6ed9000.video",
	"e6eda000.video",
	"e6edb000.video",
	"e6edc000.video",
	"e6edd000.video",
	"e6ede000.video",
	"e6edf000.video",
	"e6800000.ethernet",
	"e6810000.ethernet",
	"e6820000.ethernet",
	"e6830000.ethernet",
	"e6840000.ethernet",
	"e6850000.ethernet",
	"e65d0000.pcie",
	"e65d8000.pcie",
	"e65f0000.pcie",
	"e65f8000.pcie",
	"ffa00000.imp-core",
	"ffa20000.imp-core",
	"ffa40000.imp-cve",
	"ffa50000.imp-cve",
	"fec00000.cisp",
	"fee00000.cisp",
	"fef00000.cisp",
	"fed80000.dsi-encoder",
	"e6ef0000.video",
	"e6ef1000.video",
	"e6ef2000.video",
	"e6ef3000.video",
	"e7a00000.stv_sts_00",
	"e7ba0000.stv_sts_01",
	"e7a10000.dof_sts_00",
	"e7bb0000.dof_sts_01",
	"e7a50000.acf_sts_00",
	"e7a60000.acf_sts_01",
	"e7a70000.acf_sts_02",
	"e7a80000.acf_sts_03",
	"fe860000.ims",
	"fe870000.ims",
	"fe880000.imr",
	"fe890000.imr",
	"fe8a0000.imr",
	"fe8b0000.imr",
	"fea00000.ivcp1e_00",
	"fec00000.cisp",
	"fed00000.tisp",
	"fee00000.cisp",
	"fed20000.tisp",
	"fef00000.cisp",
	"fed30000.tisp",
	"fe400000.cisp",
	"fed40000.tisp",
	"f1f00000.dma-controller",
	"f1f10000.dma-controller",
	"ff900000.imp-distributer0",
	"ff900000.imp-distributer1",
	"ff900000.imp-distributer2",
	"ff900000.imp-distributer3",
	"ff900000.imp-distributer4",
	"ff900000.imp-distributer5",
	"ff900000.imp-distributer6",
	"ffa00000.imp-core",
	"ffa20000.imp-core",
	"ffb00000.imp-core",
	"ffb20000.imp-core",
	"ffa40000.imp-cve",
	"ffa50000.imp-cve",
	"ffb40000.imp-cve",
	"ffb50000.imp-cve",
	"ffa60000.imp-cve",
	"ffb60000.imp-cve",
	"ffa70000.imp-cve",
	"ffb70000.imp-cve",
	"ffa80000.dma-controller",
	"ffb80000.dma-controller",
	"ffa84000.imp-psc",
	"ffb84000.imp-psc",
	"ffaa0000.imp-cnn",
	"ffbc0000.imp-cnn",
	"ffac0000.imp-cnn",
	"ed300000.imp-ram",
	"ffa8c000.imp-ram",
	"ffb8c000.imp-ram",
	"eda00000.imp-ram",
	"ffab0000.cnn-ram",
	"ed600000.cnn-ram",
	"ffbd0000.cnn-ram",
	"ed800000.cnn-ram",
	"ffad0000.cnn-ram",
	"ed400000.cnn-ram",
	"fedd0000.vspx",
	"fedd8000.vspx",
	"fede0000.vspx",
	"fede8000.vspx",
	"fe601000.fba",
	"fe602000.fba",
	"fe603000.fba",
	"fe604000.fba",
	"fe605000.fba",
	"fe606000.fba",
	"e7b81000.fba",
	"e7b87000.fba",
	"e7b61000.fba",
	"e7b80000.fba",
	"e7b82000.fba",
	"e7b86000.fba",
};

static bool ipmmu_slave_whitelist(struct device *dev)
{
	unsigned int i;

	/*
	 * For R-Car Gen3 use a white list to opt-in slave devices.
	 * For Other SoCs, this returns true anyway.
	 */
	if (!soc_device_match(soc_rcar_gen3))
		return true;

	/* Check whether this R-Car Gen3 can use the IPMMU correctly or not */
	if (!soc_device_match(soc_rcar_gen3_whitelist))
		return false;

#if defined(CONFIG_PCI)
	if (dev_is_pci(dev))
		return true;
#endif
	/* Check whether this slave device can work with the IPMMU */
	for (i = 0; i < ARRAY_SIZE(rcar_v3u_slave_whitelist); i++) {
		if (!strcmp(dev_name(dev), rcar_v3u_slave_whitelist[i]))
			return true;
	}

	/* Otherwise, do not allow use of IPMMU */
	return false;
}

static int ipmmu_of_xlate(struct device *dev,
			  struct of_phandle_args *spec)
{
	if (!ipmmu_slave_whitelist(dev))
		return -ENODEV;

	iommu_fwspec_add_ids(dev, spec->args, 1);

	/* Initialize once - xlate() will call multiple times */
	if (to_ipmmu(dev))
		return 0;

	return ipmmu_init_platform_device(dev, spec);
}

static const struct iommu_ops ipmmu_ops = {
	.domain_alloc = ipmmu_domain_alloc,
	.domain_free = ipmmu_domain_free,
	.attach_dev = ipmmu_attach_device,
	.detach_dev = ipmmu_detach_device,
	.map = ipmmu_map,
	.unmap = ipmmu_unmap,
	.flush_iotlb_all = ipmmu_flush_iotlb_all,
	.iotlb_sync = ipmmu_iotlb_sync,
	.iova_to_phys = ipmmu_iova_to_phys,
	.add_device = ipmmu_add_device,
	.remove_device = ipmmu_remove_device,
	.device_group = ipmmu_find_group,
	.pgsize_bitmap = SZ_1G | SZ_2M | SZ_4K,
	.of_xlate = ipmmu_of_xlate,
};

/* -----------------------------------------------------------------------------
 * Probe/remove and init
 */

static void ipmmu_device_reset(struct ipmmu_vmsa_device *mmu)
{
	unsigned int i;

	/* Disable all contexts. */
	for (i = 0; i < mmu->num_ctx; ++i)
		ipmmu_write(mmu, IM_CTX_SIZE(i) + IMCTR(i), 0);
}

static const struct ipmmu_features ipmmu_features_rcar_v3u = {
	.use_ns_alias_offset = false,
	.has_cache_leaf_nodes = true,
	.number_of_contexts = 16,
	.num_utlbs = 63,
	.setup_imbuscr = false,
	.twobit_imttbcr_sl0 = true,
	.reserved_context = true,
	.cache_snoop = true,
};

static const struct of_device_id ipmmu_of_ids[] = {
	{
		.compatible = "renesas,ipmmu-r8a779a0",
		.data = &ipmmu_features_rcar_v3u,
	}, {
		/* Terminator */
	},
};

static int ipmmu_probe(struct platform_device *pdev)
{
	struct ipmmu_vmsa_device *mmu;
	struct resource *res;
	int irq;
	int ret;

	mmu = devm_kzalloc(&pdev->dev, sizeof(*mmu), GFP_KERNEL);
	if (!mmu) {
		dev_err(&pdev->dev, "cannot allocate device data\n");
		return -ENOMEM;
	}

	mmu->dev = &pdev->dev;
	spin_lock_init(&mmu->lock);
	bitmap_zero(mmu->ctx, IPMMU_CTX_MAX);
	mmu->features = of_device_get_match_data(&pdev->dev);
	memset(mmu->utlb_ctx, IPMMU_CTX_INVALID, mmu->features->num_utlbs);
	dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(40));

	/* Map I/O memory and request IRQ. */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	mmu->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(mmu->base))
		return PTR_ERR(mmu->base);

	/*
	 * The IPMMU has two register banks, for secure and non-secure modes.
	 * The bank mapped at the beginning of the IPMMU address space
	 * corresponds to the running mode of the CPU. When running in secure
	 * mode the non-secure register bank is also available at an offset.
	 *
	 * Secure mode operation isn't clearly documented and is thus currently
	 * not implemented in the driver. Furthermore, preliminary tests of
	 * non-secure operation with the main register bank were not successful.
	 * Offset the registers base unconditionally to point to the non-secure
	 * alias space for now.
	 */
	if (mmu->features->use_ns_alias_offset)
		mmu->base += IM_NS_ALIAS_OFFSET;

	mmu->num_ctx = min(IPMMU_CTX_MAX, mmu->features->number_of_contexts);

	/*
	 * Determine if this IPMMU instance is a root device by checking for
	 * the lack of has_cache_leaf_nodes flag or renesas,ipmmu-main property.
	 */
	if (!mmu->features->has_cache_leaf_nodes ||
	    !of_find_property(pdev->dev.of_node, "renesas,ipmmu-main", NULL))
		mmu->root = mmu;
	else
		mmu->root = ipmmu_find_root();

	/*
	 * Wait until the root device has been registered for sure.
	 */
	if (!mmu->root)
		return -EPROBE_DEFER;

	/* Root devices have mandatory IRQs */
	if (ipmmu_is_root(mmu)) {
		irq = platform_get_irq(pdev, 0);
		if (irq < 0)
			return irq;

		ret = devm_request_irq(&pdev->dev, irq, ipmmu_irq, 0,
				       dev_name(&pdev->dev), mmu);
		if (ret < 0) {
			dev_err(&pdev->dev, "failed to request IRQ %d\n", irq);
			return ret;
		}

		ipmmu_device_reset(mmu);

		if (mmu->features->reserved_context) {
			dev_info(&pdev->dev, "IPMMU context 0 is reserved\n");
			set_bit(0, mmu->ctx);
		}
	}

	/*
	 * Register the IPMMU to the IOMMU subsystem in the following cases:
	 * - R-Car Gen2 IPMMU (all devices registered)
	 * - R-Car Gen3 IPMMU (leaf devices only - skip root IPMMU-MM device)
	 */
	if (!mmu->features->has_cache_leaf_nodes || !ipmmu_is_root(mmu)) {
		ret = iommu_device_sysfs_add(&mmu->iommu, &pdev->dev, NULL,
					     dev_name(&pdev->dev));
		if (ret)
			return ret;

		iommu_device_set_ops(&mmu->iommu, &ipmmu_ops);
		iommu_device_set_fwnode(&mmu->iommu,
					&pdev->dev.of_node->fwnode);

		ret = iommu_device_register(&mmu->iommu);
		if (ret)
			return ret;

#if defined(CONFIG_IOMMU_DMA)
#if defined(CONFIG_PCI)
		if (!iommu_present(&pci_bus_type))
			bus_set_iommu(&pci_bus_type, &ipmmu_ops);
#endif

		if (!iommu_present(&platform_bus_type))
			bus_set_iommu(&platform_bus_type, &ipmmu_ops);
#endif
	}

	/*
	 * We can't create the ARM mapping here as it requires the bus to have
	 * an IOMMU, which only happens when bus_set_iommu() is called in
	 * ipmmu_init() after the probe function returns.
	 */

	platform_set_drvdata(pdev, mmu);

	return 0;
}

static int ipmmu_remove(struct platform_device *pdev)
{
	struct ipmmu_vmsa_device *mmu = platform_get_drvdata(pdev);

	iommu_device_sysfs_remove(&mmu->iommu);
	iommu_device_unregister(&mmu->iommu);

	arm_iommu_release_mapping(mmu->mapping);

	ipmmu_device_reset(mmu);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int ipmmu_resume_noirq(struct device *dev)
{
	struct ipmmu_vmsa_device *mmu = dev_get_drvdata(dev);
	unsigned int i;

	/* Reset root MMU and restore contexts */
	if (ipmmu_is_root(mmu)) {
		ipmmu_device_reset(mmu);

		for (i = 0; i < mmu->num_ctx; i++) {
			if (!mmu->domains[i])
				continue;

			ipmmu_domain_setup_context(mmu->domains[i]);
		}
	}

	/* Re-enable active micro-TLBs */
	for (i = 0; i < mmu->features->num_utlbs; i++) {
		if (mmu->utlb_ctx[i] == IPMMU_CTX_INVALID)
			continue;

		ipmmu_utlb_enable(mmu->root->domains[mmu->utlb_ctx[i]], i);
	}

	return 0;
}

static const struct dev_pm_ops ipmmu_pm  = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(NULL, ipmmu_resume_noirq)
};

/* Unsupported function */
#define DEV_PM_OPS      NULL
#endif /* CONFIG_PM_SLEEP */

static struct platform_driver ipmmu_driver = {
	.driver = {
		.name = "ipmmu-vmsa",
		.of_match_table = of_match_ptr(ipmmu_of_ids),
		.pm = DEV_PM_OPS,
	},
	.probe = ipmmu_probe,
	.remove	= ipmmu_remove,
};

static int __init ipmmu_init(void)
{
	struct device_node *np;
	static bool setup_done;
	int ret;

	if (setup_done)
		return 0;

	np = of_find_matching_node(NULL, ipmmu_of_ids);
	if (!np)
		return 0;

	of_node_put(np);

	ret = platform_driver_register(&ipmmu_driver);
	if (ret < 0)
		return ret;

#if defined(CONFIG_ARM) && !defined(CONFIG_IOMMU_DMA)
	if (!iommu_present(&platform_bus_type))
		bus_set_iommu(&platform_bus_type, &ipmmu_ops);
#endif

	setup_done = true;
	return 0;
}
subsys_initcall(ipmmu_init);
