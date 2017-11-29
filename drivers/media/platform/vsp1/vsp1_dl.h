/*
 * vsp1_dl.h  --  R-Car VSP1 Display List
 *
 * Copyright (C) 2015-2017 Renesas Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef __VSP1_DL_H__
#define __VSP1_DL_H__

#include <linux/types.h>

#include "vsp1_rwpf.h"

struct vsp1_device;
struct vsp1_dl_fragment;
struct vsp1_dl_fragment_pool;
struct vsp1_dl_list;
struct vsp1_dl_manager;

void vsp1_dlm_setup(struct vsp1_device *vsp1, unsigned int pipe_index);

struct vsp1_dl_manager *vsp1_dlm_create(struct vsp1_device *vsp1,
					unsigned int index,
					unsigned int prealloc);
void vsp1_dlm_destroy(struct vsp1_dl_manager *dlm);
void vsp1_dlm_reset(struct vsp1_dl_manager *dlm);
bool vsp1_dlm_irq_frame_end(struct vsp1_dl_manager *dlm, bool interlaced);

struct vsp1_dl_list *vsp1_dl_list_get(struct vsp1_dl_manager *dlm);
void vsp1_dl_list_put(struct vsp1_dl_list *dl);
struct vsp1_dl_body *vsp1_dl_list_body(struct vsp1_dl_list *dl);
void vsp1_dl_list_commit(struct vsp1_dl_list *dl, unsigned int pipe_index);

struct vsp1_dl_fragment_pool *
vsp1_dl_fragment_pool_alloc(struct vsp1_device *vsp1, unsigned int qty,
			    unsigned int num_entries, size_t extra_size);
void vsp1_dl_fragment_pool_free(struct vsp1_dl_fragment_pool *pool);
struct vsp1_dl_body *vsp1_dl_fragment_get(struct vsp1_dl_fragment_pool *pool);
void vsp1_dl_fragment_put(struct vsp1_dl_body *dlb);

void vsp1_dl_fragment_write(struct vsp1_dl_body *dlb, u32 reg, u32 data);
int vsp1_dl_list_add_fragment(struct vsp1_dl_list *dl,
			      struct vsp1_dl_body *dlb);
int vsp1_dl_list_add_chain(struct vsp1_dl_list *head, struct vsp1_dl_list *dl);
void vsp1_dl_set_addr_auto_fld(struct vsp1_dl_body *dlb,
			       struct vsp1_rwpf *rpf,
			       struct vsp1_rwpf_memory mem);

#endif /* __VSP1_DL_H__ */
