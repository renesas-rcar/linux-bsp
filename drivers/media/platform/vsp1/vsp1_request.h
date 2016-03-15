/*
 * vsp1_request.h  --  R-Car VSP1 Request Management
 *
 * Copyright (C) 2015 Renesas Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef __VSP1_REQUEST_H__
#define __VSP1_REQUEST_H__

#include <linux/kernel.h>
#include <linux/list.h>

struct media_device;
struct media_device_request;
struct vsp1_dl_list;

struct vsp1_request {
	struct media_device_request req;
	struct vsp1_dl_list *dl;
	struct list_head list;
};

static inline struct vsp1_request *
to_vsp1_request(struct media_device_request *req)
{
	return container_of(req, struct vsp1_request, req);
}

struct media_device_request *vsp1_request_alloc(struct media_device *mdev);
void vsp1_request_free(struct media_device *mdev,
		       struct media_device_request *req);
int vsp1_request_queue(struct media_device *mdev,
		       struct media_device_request *req);

#endif /* __VSP1_REQUEST_H__ */
