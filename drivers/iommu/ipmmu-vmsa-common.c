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

#define IPMMU_CTX_MAX		16U
#define IPMMU_CTX_INVALID	-1

#define IPMMU_UTLB_MAX		63U

#include "ipmmu-vmsa.h"

struct ipmmu_vmsa_domain *to_vmsa_domain(struct iommu_domain *dom)
{
	return container_of(dom, struct ipmmu_vmsa_domain, io_domain);
}

struct ipmmu_vmsa_device *to_ipmmu(struct device *dev)
{
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);

	return fwspec ? fwspec->iommu_priv : NULL;
}

/* -----------------------------------------------------------------------------
 * Read/Write Access
 */

u32 ipmmu_read(struct ipmmu_vmsa_device *mmu, unsigned int offset)
{
	return ioread32(mmu->base + offset);
}

void ipmmu_write(struct ipmmu_vmsa_device *mmu, unsigned int offset,
		 u32 data)
{
	iowrite32(data, mmu->base + offset);
}

/* -----------------------------------------------------------------------------
 * Domain/Context Management
 */

int ipmmu_domain_allocate_context(struct ipmmu_vmsa_device *mmu,
				  struct ipmmu_vmsa_domain *domain)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&mmu->lock, flags);

	ret = find_first_zero_bit(mmu->ctx, mmu->num_ctx);
	if (ret != mmu->num_ctx) {
		mmu->domains[ret] = domain;
		set_bit(ret, mmu->ctx);
	} else
		ret = -EBUSY;

	spin_unlock_irqrestore(&mmu->lock, flags);

	return ret;
}

void ipmmu_domain_free_context(struct ipmmu_vmsa_device *mmu,
			       unsigned int context_id)
{
	unsigned long flags;

	spin_lock_irqsave(&mmu->lock, flags);

	clear_bit(context_id, mmu->ctx);
	mmu->domains[context_id] = NULL;

	spin_unlock_irqrestore(&mmu->lock, flags);
}

/* -----------------------------------------------------------------------------
 * IOMMU Operations
 */

struct iommu_domain *__ipmmu_domain_alloc(unsigned type)
{
	struct ipmmu_vmsa_domain *domain;

	domain = kzalloc(sizeof(*domain), GFP_KERNEL);
	if (!domain)
		return NULL;

	mutex_init(&domain->mutex);

	return &domain->io_domain;
}

struct iommu_domain *ipmmu_domain_alloc(unsigned type)
{
	struct iommu_domain *io_domain = NULL;

	switch (type) {
	case IOMMU_DOMAIN_UNMANAGED:
		io_domain = __ipmmu_domain_alloc(type);
		break;

	case IOMMU_DOMAIN_DMA:
		io_domain = __ipmmu_domain_alloc(type);
		if (io_domain && iommu_get_dma_cookie(io_domain)) {
			kfree(io_domain);
			io_domain = NULL;
		}
		break;
	}

	return io_domain;
}

int ipmmu_map(struct iommu_domain *io_domain, unsigned long iova,
	      phys_addr_t paddr, size_t size, int prot)
{
	struct ipmmu_vmsa_domain *domain = to_vmsa_domain(io_domain);

	if (!domain)
		return -ENODEV;

	return domain->iop->map(domain->iop, iova, paddr, size, prot);
}

size_t ipmmu_unmap(struct iommu_domain *io_domain, unsigned long iova,
		   size_t size, struct iommu_iotlb_gather *gather)
{
	struct ipmmu_vmsa_domain *domain = to_vmsa_domain(io_domain);

	return domain->iop->unmap(domain->iop, iova, size, gather);
}

phys_addr_t ipmmu_iova_to_phys(struct iommu_domain *io_domain,
			       dma_addr_t iova)
{
	struct ipmmu_vmsa_domain *domain = to_vmsa_domain(io_domain);

	/* TODO: Is locking needed ? */

	return domain->iop->iova_to_phys(domain->iop, iova);
}

int ipmmu_init_platform_device(struct device *dev,
			       struct of_phandle_args *args)
{
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
	struct platform_device *ipmmu_pdev;

	ipmmu_pdev = of_find_device_by_node(args->np);
	if (!ipmmu_pdev)
		return -ENODEV;

	fwspec->iommu_priv = platform_get_drvdata(ipmmu_pdev);

	return 0;
}

int ipmmu_init_arm_mapping(struct device *dev)
{
	struct ipmmu_vmsa_device *mmu = to_ipmmu(dev);
	struct iommu_group *group;
	int ret;

	/* Create a device group and add the device to it. */
	group = iommu_group_alloc();
	if (IS_ERR(group)) {
		dev_err(dev, "Failed to allocate IOMMU group\n");
		return PTR_ERR(group);
	}

	ret = iommu_group_add_device(group, dev);
	iommu_group_put(group);

	if (ret < 0) {
		dev_err(dev, "Failed to add device to IPMMU group\n");
		return ret;
	}

	/*
	 * Create the ARM mapping, used by the ARM DMA mapping core to allocate
	 * VAs. This will allocate a corresponding IOMMU domain.
	 *
	 * TODO:
	 * - Create one mapping per context (TLB).
	 * - Make the mapping size configurable ? We currently use a 2GB mapping
	 *   at a 1GB offset to ensure that NULL VAs will fault.
	 */
	if (!mmu->mapping) {
		struct dma_iommu_mapping *mapping;

		mapping = arm_iommu_create_mapping(&platform_bus_type,
						   SZ_1G, SZ_2G);
		if (IS_ERR(mapping)) {
			dev_err(mmu->dev, "failed to create ARM IOMMU mapping\n");
			ret = PTR_ERR(mapping);
			goto error;
		}

		mmu->mapping = mapping;
	}

	/* Attach the ARM VA mapping to the device. */
	ret = arm_iommu_attach_device(dev, mmu->mapping);
	if (ret < 0) {
		dev_err(dev, "Failed to attach device to VA mapping\n");
		goto error;
	}

	return 0;

error:
	iommu_group_remove_device(dev);
	if (mmu->mapping)
		arm_iommu_release_mapping(mmu->mapping);

	return ret;
}

struct device *ipmmu_get_pci_host_device(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct pci_bus *bus = pdev->bus;

	/* Walk up to the root bus to look for PCI Host controller */
	while (!pci_is_root_bus(bus))
		bus = bus->parent;

	return bus->bridge->parent;
}

int ipmmu_add_device(struct device *dev)
{
	struct ipmmu_vmsa_device *mmu = to_ipmmu(dev);
	struct device *root_dev;
	struct iommu_group *group;
	int ret;

	/*
	 * Only let through devices that have been verified in xlate()
	 */
	if (!mmu)
		return -ENODEV;

	if (IS_ENABLED(CONFIG_ARM) && !IS_ENABLED(CONFIG_IOMMU_DMA)) {
		ret = ipmmu_init_arm_mapping(dev);
		if (ret)
			return ret;
	} else {
	    /*
	     * The IOMMU can't distinguish between different PCI Functions.
	     * Use PCI Host controller as a proxy for all connected PCI devices
	     */
		if (dev_is_pci(dev)) {
			root_dev = ipmmu_get_pci_host_device(dev);

			if (root_dev->iommu_group)
				dev->iommu_group = root_dev->iommu_group;
		}

		group = iommu_group_get_for_dev(dev);
		if (IS_ERR(group))
			return PTR_ERR(group);

		iommu_group_put(group);
	}

	iommu_device_link(&mmu->iommu, dev);
	return 0;
}

void ipmmu_remove_device(struct device *dev)
{
	struct ipmmu_vmsa_device *mmu = to_ipmmu(dev);

	iommu_device_unlink(&mmu->iommu, dev);
	arm_iommu_detach_device(dev);
	iommu_group_remove_device(dev);
}

struct iommu_group *ipmmu_find_group(struct device *dev)
{
	struct ipmmu_vmsa_device *mmu = to_ipmmu(dev);
	struct iommu_group *group;

	if (mmu->group)
		return iommu_group_ref_get(mmu->group);

	group = iommu_group_alloc();
	if (!IS_ERR(group))
		mmu->group = group;

	return group;
}
