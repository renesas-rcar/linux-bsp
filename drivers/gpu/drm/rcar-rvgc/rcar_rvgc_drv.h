/*
 * rcar_rvgc_drv.h  --  R-Car Display Unit DRM driver
 *
 * Copyright (C) 2013-2017 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __RCAR_RVGC_DRV_H__
#define __RCAR_RVGC_DRV_H__

#include <linux/kernel.h>
#include "rcar_rvgc_pipe.h"

struct taurus_rvgc_res_msg;

struct taurus_event_list {
	uint32_t id;
	struct taurus_rvgc_res_msg *result;
	struct list_head list;
	struct completion ack;
	bool ack_received;
	struct completion completed;
};

struct rcar_rvgc_device {
	struct device *dev;

	struct drm_device *ddev;
	struct drm_fbdev_cma *fbdev;

	struct rpmsg_device *rpdev;

	unsigned int nr_rvgc_pipes;
	struct rcar_rvgc_pipe *rvgc_pipes;

	uint8_t vblank_pending;
	wait_queue_head_t vblank_pending_wait_queue;

	struct task_struct *vsync_thread;

	struct list_head taurus_event_list_head;
	rwlock_t event_list_lock;
};

#endif /* __RCAR_RVGC_DRV_H__ */
