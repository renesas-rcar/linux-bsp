// SPDX-License-Identifier: GPL-2.0
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/genalloc.h>
#include <linux/gfp.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/memremap.h>
#include <linux/slab.h>

#include <asm/page.h>

#include <xen/balloon.h>
#include <xen/page.h>
#include <xen/xen.h>

static DEFINE_MUTEX(pool_lock);
static struct gen_pool *unpopulated_pool;

static struct resource *target_resource;

/*
 * If arch is not happy with system "iomem_resource" being used for
 * the region allocation it can provide it's own view by creating specific
 * Xen resource with unused regions of guest physical address space provided
 * by the hypervisor.
 */
int __weak __init arch_xen_unpopulated_init(struct resource **res)
{
	*res = &iomem_resource;

	return 0;
}

static int fill_pool(unsigned int nr_pages)
{
	struct dev_pagemap *pgmap;
	struct resource *res, *tmp_res = NULL;
	void *vaddr;
	unsigned int alloc_pages = round_up(nr_pages, PAGES_PER_SECTION);
	struct range mhp_range;
	int ret;

	res = kzalloc(sizeof(*res), GFP_KERNEL);
	if (!res)
		return -ENOMEM;

	res->name = "Xen scratch";
	res->flags = IORESOURCE_MEM | IORESOURCE_BUSY;

	mhp_range = mhp_get_pluggable_range(true);

	ret = allocate_resource(target_resource, res,
				alloc_pages * PAGE_SIZE, mhp_range.start, mhp_range.end,
				PAGES_PER_SECTION * PAGE_SIZE, NULL, NULL);
	if (ret < 0) {
		pr_err("Cannot allocate new IOMEM resource\n");
		goto err_resource;
	}

	/*
	 * Reserve the region previously allocated from Xen resource to avoid
	 * re-using it by someone else.
	 */
	if (target_resource != &iomem_resource) {
		tmp_res = kzalloc(sizeof(*tmp_res), GFP_KERNEL);
		if (!tmp_res) {
			ret = -ENOMEM;
			goto err_insert;
		}

		tmp_res->name = res->name;
		tmp_res->start = res->start;
		tmp_res->end = res->end;
		tmp_res->flags = res->flags;

		ret = request_resource(&iomem_resource, tmp_res);
		if (ret < 0) {
			pr_err("Cannot request resource %pR (%d)\n", tmp_res, ret);
			kfree(tmp_res);
			goto err_insert;
		}
	}

	pgmap = kzalloc(sizeof(*pgmap), GFP_KERNEL);
	if (!pgmap) {
		ret = -ENOMEM;
		goto err_pgmap;
	}

	pgmap->type = MEMORY_DEVICE_GENERIC;
	pgmap->range = (struct range) {
		.start = res->start,
		.end = res->end,
	};
	pgmap->nr_range = 1;
	pgmap->owner = res;

#ifdef CONFIG_XEN_HAVE_PVMMU
        /*
         * memremap will build page tables for the new memory so
         * the p2m must contain invalid entries so the correct
         * non-present PTEs will be written.
         *
         * If a failure occurs, the original (identity) p2m entries
         * are not restored since this region is now known not to
         * conflict with any devices.
         */
	if (!xen_feature(XENFEAT_auto_translated_physmap)) {
		unsigned int i;
		xen_pfn_t pfn = PFN_DOWN(res->start);

		for (i = 0; i < alloc_pages; i++) {
			if (!set_phys_to_machine(pfn + i, INVALID_P2M_ENTRY)) {
				pr_warn("set_phys_to_machine() failed, no memory added\n");
				ret = -ENOMEM;
				goto err_memremap;
			}
                }
	}
#endif

	vaddr = memremap_pages(pgmap, NUMA_NO_NODE);
	if (IS_ERR(vaddr)) {
		pr_err("Cannot remap memory range\n");
		ret = PTR_ERR(vaddr);
		goto err_memremap;
	}

	ret = gen_pool_add_virt(unpopulated_pool, (unsigned long)vaddr, res->start,
			alloc_pages * PAGE_SIZE, NUMA_NO_NODE);
	if (ret) {
		pr_err("Cannot add memory range to the unpopulated pool\n");
		goto err_pool;
	}

	return 0;

err_pool:
	memunmap_pages(pgmap);
err_memremap:
	kfree(pgmap);
err_pgmap:
	if (tmp_res) {
		release_resource(tmp_res);
		kfree(tmp_res);
	}
err_insert:
	release_resource(res);
err_resource:
	kfree(res);
	return ret;
}

static int alloc_unpopulated_pages(unsigned int nr_pages, struct page **pages,
		bool contiguous)
{
	unsigned int i;
	int ret = 0;
	void *vaddr;
	bool filled = false;

	/*
	 * Fallback to default behavior if we do not have any suitable resource
	 * to allocate required region from and as the result we won't be able to
	 * construct pages.
	 */
	if (!target_resource) {
		if (contiguous && nr_pages > 1)
			return -ENODEV;

		return xen_alloc_ballooned_pages(nr_pages, pages);
	}

	mutex_lock(&pool_lock);

	while (!(vaddr = (void *)gen_pool_alloc(unpopulated_pool,
			nr_pages * PAGE_SIZE))) {
		if (filled)
			ret = -ENOMEM;
		else {
			ret = fill_pool(nr_pages);
			filled = true;
		}
		if (ret)
			goto out;
	}

	for (i = 0; i < nr_pages; i++) {
		pages[i] = virt_to_page(vaddr + PAGE_SIZE * i);

#ifdef CONFIG_XEN_HAVE_PVMMU
		if (!xen_feature(XENFEAT_auto_translated_physmap)) {
			ret = xen_alloc_p2m_entry(page_to_pfn(pages[i]));
			if (ret < 0) {
				gen_pool_free(unpopulated_pool, (unsigned long)vaddr,
						nr_pages * PAGE_SIZE);
				goto out;
			}
		}
#endif
	}

out:
	mutex_unlock(&pool_lock);
	return ret;
}

static bool in_unpopulated_pool(unsigned int nr_pages, struct page *page)
{
	if (!target_resource)
		return false;

	return gen_pool_has_addr(unpopulated_pool,
			(unsigned long)page_to_virt(page), nr_pages * PAGE_SIZE);
}

static void free_unpopulated_pages(unsigned int nr_pages, struct page **pages,
		bool contiguous)
{
	if (!target_resource) {
		if (contiguous && nr_pages > 1)
			return;

		xen_free_ballooned_pages(nr_pages, pages);
		return;
	}

	mutex_lock(&pool_lock);

	/* XXX Do we need to check the range (gen_pool_has_addr)? */
	if (contiguous)
		gen_pool_free(unpopulated_pool, (unsigned long)page_to_virt(pages[0]),
				nr_pages * PAGE_SIZE);
	else {
		unsigned int i;

		for (i = 0; i < nr_pages; i++)
			gen_pool_free(unpopulated_pool,
					(unsigned long)page_to_virt(pages[i]), PAGE_SIZE);
	}

	mutex_unlock(&pool_lock);
}

/**
 * is_xen_unpopulated_page - check whether page is unpopulated
 * @page: page to be checked
 * @return true if page is unpopulated, else otherwise
 */
bool is_xen_unpopulated_page(struct page *page)
{
	return in_unpopulated_pool(1, page);
}
EXPORT_SYMBOL(is_xen_unpopulated_page);

/**
 * xen_alloc_unpopulated_pages - alloc unpopulated pages
 * @nr_pages: Number of pages
 * @pages: pages returned
 * @return 0 on success, error otherwise
 */
int xen_alloc_unpopulated_pages(unsigned int nr_pages, struct page **pages)
{
	return alloc_unpopulated_pages(nr_pages, pages, false);
}
EXPORT_SYMBOL(xen_alloc_unpopulated_pages);

/**
 * xen_free_unpopulated_pages - return unpopulated pages
 * @nr_pages: Number of pages
 * @pages: pages to return
 */
void xen_free_unpopulated_pages(unsigned int nr_pages, struct page **pages)
{
	free_unpopulated_pages(nr_pages, pages, false);
}
EXPORT_SYMBOL(xen_free_unpopulated_pages);

/**
 * xen_alloc_unpopulated_contiguous_pages - alloc unpopulated contiguous pages
 * @dev: valid struct device pointer
 * @nr_pages: Number of pages
 * @pages: pages returned
 * @return 0 on success, error otherwise
 */
int xen_alloc_unpopulated_contiguous_pages(struct device *dev,
		unsigned int nr_pages, struct page **pages)
{
	/* XXX Handle devices which support 64-bit DMA address only for now */
	if (dma_get_mask(dev) != DMA_BIT_MASK(64))
		return -EINVAL;

	return alloc_unpopulated_pages(nr_pages, pages, true);
}
EXPORT_SYMBOL(xen_alloc_unpopulated_contiguous_pages);

/**
 * xen_free_unpopulated_contiguous_pages - return unpopulated contiguous pages
 * @dev: valid struct device pointer
 * @nr_pages: Number of pages
 * @pages: pages to return
 */
void xen_free_unpopulated_contiguous_pages(struct device *dev,
		unsigned int nr_pages, struct page **pages)
{
	free_unpopulated_pages(nr_pages, pages, true);
}
EXPORT_SYMBOL(xen_free_unpopulated_contiguous_pages);

static int __init unpopulated_init(void)
{
	int ret;

	if (!xen_domain())
		return -ENODEV;

	unpopulated_pool = gen_pool_create(PAGE_SHIFT, NUMA_NO_NODE);
	if (!unpopulated_pool) {
		pr_err("xen:unpopulated: Cannot create unpopulated pool\n");
		return -ENOMEM;
	}

	gen_pool_set_algo(unpopulated_pool, gen_pool_best_fit, NULL);

	ret = arch_xen_unpopulated_init(&target_resource);
	if (ret) {
		pr_err("xen:unpopulated: Cannot initialize target resource\n");
		gen_pool_destroy(unpopulated_pool);
		unpopulated_pool = NULL;
		target_resource = NULL;
	}

	return ret;
}
early_initcall(unpopulated_init);
