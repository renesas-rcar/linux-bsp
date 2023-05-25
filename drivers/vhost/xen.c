// SPDX-License-Identifier: GPL-2.0-only
/*
 * A specific module for accessing descriptors in virtio rings which contain
 * guest grant based addresses instead of pseudo-physical addresses.
 * Please see Xen grant DMA-mapping layer at drivers/xen/grant-dma-ops.c
 * which is the origin of such mapping scheme.
 *
 * Copyright (C) 2023 EPAM Systems Inc.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vhost.h>
#include <xen/grant_table.h>
#include <xen/xen.h>

#include "vhost.h"

/* TODO: Make it possible to get domid */
static domid_t guest_domid = 2;

struct vhost_xen_grant_map {
	struct list_head next;
	int count;
	int flags;
	grant_ref_t *grefs;
	domid_t domid;
	struct gnttab_map_grant_ref *map_ops;
	struct gnttab_unmap_grant_ref *unmap_ops;
	struct page **pages;
	unsigned long vaddr;
};

static void vhost_xen_free_map(struct vhost_xen_grant_map *map)
{
	if (!map)
		return;

	if (map->pages)
		gnttab_free_pages(map->count, map->pages);

	kvfree(map->pages);
	kvfree(map->grefs);
	kvfree(map->map_ops);
	kvfree(map->unmap_ops);
	kfree(map);
}

static struct vhost_xen_grant_map *vhost_xen_alloc_map(int count)
{
	struct vhost_xen_grant_map *map;
	int i;

	map = kzalloc(sizeof(*map), GFP_KERNEL);
	if (!map)
		return NULL;

	map->grefs = kvcalloc(count, sizeof(map->grefs[0]), GFP_KERNEL);
	map->map_ops = kvcalloc(count, sizeof(map->map_ops[0]), GFP_KERNEL);
	map->unmap_ops = kvcalloc(count, sizeof(map->unmap_ops[0]), GFP_KERNEL);
	map->pages = kvcalloc(count, sizeof(map->pages[0]), GFP_KERNEL);
	if (!map->grefs || !map->map_ops || !map->unmap_ops || !map->pages)
		goto err;

	if (gnttab_alloc_pages(count, map->pages))
		goto err;

	for (i = 0; i < count; i++) {
		map->map_ops[i].handle = -1;
		map->unmap_ops[i].handle = -1;
	}

	map->count = count;

	return map;

err:
	vhost_xen_free_map(map);

	return NULL;
}

static int vhost_xen_map_pages(struct vhost_xen_grant_map *map)
{
	int i, ret = 0;

	if (map->map_ops[0].handle != -1)
		return -EINVAL;

	for (i = 0; i < map->count; i++) {
		unsigned long vaddr = (unsigned long)
			pfn_to_kaddr(page_to_xen_pfn(map->pages[i]));
		gnttab_set_map_op(&map->map_ops[i], vaddr, map->flags,
			map->grefs[i], map->domid);
		gnttab_set_unmap_op(&map->unmap_ops[i], vaddr, map->flags, -1);
	}

	ret = gnttab_map_refs(map->map_ops, NULL, map->pages, map->count);
	for (i = 0; i < map->count; i++) {
		if (map->map_ops[i].status == GNTST_okay)
			map->unmap_ops[i].handle = map->map_ops[i].handle;
		else if (!ret)
			ret = -EINVAL;
	}

	return ret;
}

static int vhost_xen_unmap_pages(struct vhost_xen_grant_map *map)
{
	int i, ret;

	if (map->unmap_ops[0].handle == -1)
		return -EINVAL;

	ret = gnttab_unmap_refs(map->unmap_ops, NULL, map->pages, map->count);
	if (ret)
		return ret;

	for (i = 0; i < map->count; i++) {
		if (map->unmap_ops[i].status != GNTST_okay)
			ret = -EINVAL;
		map->unmap_ops[i].handle = -1;
	}

	return ret;
}

static void vhost_xen_put_map(struct vhost_xen_grant_map *map)
{
	if (!map)
		return;

	if (map->vaddr) {
		if (map->count > 1)
			vunmap((void *)map->vaddr);
		map->vaddr = 0;
	}

	if (map->pages) {
		int ret;

		ret = vhost_xen_unmap_pages(map);
		if (ret)
			pr_err("%s: Failed to unmap pages from dom%d (ret=%d)\n",
					__func__, map->domid, ret);
	}
	vhost_xen_free_map(map);
}

static struct vhost_xen_grant_map *vhost_xen_find_map(struct vhost_virtqueue *vq,
		unsigned long vaddr, int count)
{
	struct vhost_xen_grant_map *map;

	list_for_each_entry(map, &vq->desc_maps, next) {
		if (map->vaddr != vaddr)
			continue;
		if (count && map->count != count)
			continue;
		return map;
	}

	return NULL;
}

void vhost_xen_unmap_desc_all(struct vhost_virtqueue *vq)
{
	struct vhost_xen_grant_map *map;

	if (!xen_domain())
		return;

	while (!list_empty(&vq->desc_maps)) {
		map = list_entry(vq->desc_maps.next, struct vhost_xen_grant_map, next);
		list_del(&map->next);

		pr_debug("%s: dom%d: vaddr 0x%lx count %u\n",
				__func__, map->domid, map->vaddr, map->count);
		vhost_xen_put_map(map);
	}
}

void *vhost_xen_map_desc(struct vhost_virtqueue *vq, u64 addr, u32 size, int access)
{
	struct vhost_xen_grant_map *map;
	unsigned long offset = xen_offset_in_page(addr);
	int count = XEN_PFN_UP(offset + size);
	int i, ret;

	if (!xen_domain() || guest_domid == DOMID_INVALID)
		return ERR_PTR(-ENODEV);

	if (!(addr & XEN_GRANT_DMA_ADDR_OFF)) {
		pr_err("%s: Descriptor from dom%d cannot be mapped (0x%llx is not a Xen grant address)\n",
				__func__, guest_domid, addr);
		return ERR_PTR(-EINVAL);
	}

	map = vhost_xen_alloc_map(count);
	if (!map)
		return ERR_PTR(-ENOMEM);

	map->domid = guest_domid;
	for (i = 0; i < count; i++)
		map->grefs[i] = ((addr & ~XEN_GRANT_DMA_ADDR_OFF) >> XEN_PAGE_SHIFT) + i;

	map->flags |= GNTMAP_host_map;
	if (access == VHOST_ACCESS_RO)
		map->flags |= GNTMAP_readonly;

	ret = vhost_xen_map_pages(map);
	if (ret) {
		pr_err("%s: Failed to map pages from dom%d (ret=%d)\n",
				__func__, map->domid, ret);
		vhost_xen_put_map(map);
		return ERR_PTR(ret);
	}

	/*
	 * Consider allocating xen_alloc_unpopulated_contiguous_pages() instead of
	 * xen_alloc_unpopulated_pages() to avoid maping as with the later
	 * map->pages are not guaranteed to be contiguous.
	 */
	if (map->count > 1) {
		map->vaddr = (unsigned long)vmap(map->pages, map->count, VM_MAP,
				PAGE_KERNEL);
		if (!map->vaddr) {
			pr_err("%s: Failed to create virtual mappings\n", __func__);
			vhost_xen_put_map(map);
			return ERR_PTR(-ENOMEM);
		}
	} else
		map->vaddr = (unsigned long)pfn_to_kaddr(page_to_xen_pfn(map->pages[0]));

	list_add_tail(&map->next, &vq->desc_maps);

	pr_debug("%s: dom%d: addr 0x%llx size 0x%x (access 0x%x) -> vaddr 0x%lx count %u (paddr 0x%llx)\n",
			__func__, map->domid, addr, size, access, map->vaddr, count,
			page_to_phys(map->pages[0]));

	return (void *)(map->vaddr + offset);
}

void vhost_xen_unmap_desc(struct vhost_virtqueue *vq, void *ptr, u32 size)
{
	struct vhost_xen_grant_map *map;
	unsigned long offset = xen_offset_in_page(ptr);
	int count = XEN_PFN_UP(offset + size);

	if (!xen_domain())
		return;

	map = vhost_xen_find_map(vq, (unsigned long)ptr & XEN_PAGE_MASK, count);
	if (map) {
		list_del(&map->next);

		pr_debug("%s: dom%d: vaddr 0x%lx count %u\n",
				__func__, map->domid, map->vaddr, map->count);
		vhost_xen_put_map(map);
	}
}

static int __init vhost_xen_init(void)
{
	if (!xen_domain())
		return -ENODEV;

	pr_info("%s: Initialize module for Xen grant mappings\n", __func__);

	return 0;
}

static void __exit vhost_xen_exit(void)
{

}

module_init(vhost_xen_init);
module_exit(vhost_xen_exit);

MODULE_DESCRIPTION("Xen grant mappings module for vhost");
MODULE_AUTHOR("Oleksandr Tyshchenko <oleksandr_tyshchenko@epam.com>");
MODULE_LICENSE("GPL v2");
