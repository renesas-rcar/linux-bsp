/*
 * vsp1_brs.h  --  R-Car VSP1 Blend ROP Sub Unit
 *
 * Copyright (C) 2016 Renesas Corporation
 *
 * This file is based on the drivers/media/platform/vsp1/vsp1_bru.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef __VSP1_BRS_H__
#define __VSP1_BRS_H__

#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>

#include "vsp1_entity.h"

struct vsp1_device;
struct vsp1_rwpf;

#define BRS_PAD_SINK(n)				(n)

struct vsp1_brs {
	struct vsp1_entity entity;

	struct v4l2_ctrl_handler ctrls;

	struct {
		struct vsp1_rwpf *rpf;
	} inputs[VSP1_MAX_RPF];

	u32 bgcolor;
};

static inline struct vsp1_brs *to_brs(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct vsp1_brs, entity.subdev);
}

struct vsp1_brs *vsp1_brs_create(struct vsp1_device *vsp1);

#endif /* __VSP1_BRS_H__ */
