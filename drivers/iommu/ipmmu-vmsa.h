/* SPDX-License-Identifier: GPL-2.0 */
/*
 * IOMMU API for Renesas VMSA-compatible IPMMU
 *
 * Copyright (C) 2020 Renesas Electronics Corporation
 */

#ifndef _IPMMU_VMSA_H
#define _IPMMU_VMSA_H

#if defined(CONFIG_ARM) && !defined(CONFIG_IOMMU_DMA)
#include <asm/dma-iommu.h>
#include <asm/pgalloc.h>
#else
#define arm_iommu_create_mapping(...)	NULL
#define arm_iommu_attach_device(...)	-ENODEV
#define arm_iommu_release_mapping(...)	do {} while (0)
#define arm_iommu_detach_device(...)	do {} while (0)
#endif

#define IPMMU_CTX_MAX           16U
#define IPMMU_UTLB_MAX          63U

struct ipmmu_features {
	bool use_ns_alias_offset;
	bool has_cache_leaf_nodes;
	unsigned int number_of_contexts;
	unsigned int num_utlbs;
	bool setup_imbuscr;
	bool twobit_imttbcr_sl0;
	bool reserved_context;
	bool cache_snoop;
};

struct ipmmu_vmsa_device {
	struct device *dev;
	void __iomem *base;
	struct iommu_device iommu;
	struct ipmmu_vmsa_device *root;
	const struct ipmmu_features *features;
	unsigned int num_ctx;
	spinlock_t lock;			/* Protects ctx and domains[] */
	DECLARE_BITMAP(ctx, IPMMU_CTX_MAX);
	struct ipmmu_vmsa_domain *domains[IPMMU_CTX_MAX];
	s8 utlb_ctx[IPMMU_UTLB_MAX];

	struct iommu_group *group;
	struct dma_iommu_mapping *mapping;
};

struct ipmmu_vmsa_domain {
	struct ipmmu_vmsa_device *mmu;
	struct iommu_domain io_domain;

	struct io_pgtable_cfg cfg;
	struct io_pgtable_ops *iop;

	unsigned int context_id;
	struct mutex mutex;			/* Protects mappings */
};

struct ipmmu_vmsa_domain *to_vmsa_domain(struct iommu_domain *dom);
struct ipmmu_vmsa_device *to_ipmmu(struct device *dev);

/* -----------------------------------------------------------------------------
 * Read/Write Access
 */

u32 ipmmu_read(struct ipmmu_vmsa_device *mmu, unsigned int offset);
void ipmmu_write(struct ipmmu_vmsa_device *mmu, unsigned int offset,
		 u32 data);

/* -----------------------------------------------------------------------------
 * Domain/Context Management
 */

int ipmmu_domain_allocate_context(struct ipmmu_vmsa_device *mmu,
				  struct ipmmu_vmsa_domain *domain);
void ipmmu_domain_free_context(struct ipmmu_vmsa_device *mmu,
			       unsigned int context_id);

/* -----------------------------------------------------------------------------
 * IOMMU Operations
 */

struct iommu_domain *__ipmmu_domain_alloc(unsigned type);
struct iommu_domain *ipmmu_domain_alloc(unsigned type);
int ipmmu_map(struct iommu_domain *io_domain, unsigned long iova,
	      phys_addr_t paddr, size_t size, int prot);
size_t ipmmu_unmap(struct iommu_domain *io_domain, unsigned long iova,
		   size_t size, struct iommu_iotlb_gather *gather);
phys_addr_t ipmmu_iova_to_phys(struct iommu_domain *io_domain,
			       dma_addr_t iova);
int ipmmu_init_platform_device(struct device *dev,
			       struct of_phandle_args *args);

static const struct soc_device_attribute soc_rcar_gen3[] = {
	{ .soc_id = "r8a774a1", },
	{ .soc_id = "r8a774c0", },
	{ .soc_id = "r8a7795", },
	{ .soc_id = "r8a7796", },
	{ .soc_id = "r8a77965", },
	{ .soc_id = "r8a77970", },
	{ .soc_id = "r8a77990", },
	{ .soc_id = "r8a77995", },
	{ .soc_id = "r8a779a0", },
	{ /* sentinel */ }
};

static const struct soc_device_attribute soc_rcar_gen3_whitelist[] = {
	{ .soc_id = "r8a774c0", },
	{ .soc_id = "r8a7795", .revision = "ES3.*" },
	{ .soc_id = "r8a7796", },
	{ .soc_id = "r8a77965", },
	{ .soc_id = "r8a77990", },
	{ .soc_id = "r8a77995", },
	{ .soc_id = "r8a779a0", },
	{ /* sentinel */ }
};

int ipmmu_init_arm_mapping(struct device *dev);
struct device *ipmmu_get_pci_host_device(struct device *dev);
int ipmmu_add_device(struct device *dev);
void ipmmu_remove_device(struct device *dev);
struct iommu_group *ipmmu_find_group(struct device *dev);

#endif /* _IPMMU_VMSA_H */
