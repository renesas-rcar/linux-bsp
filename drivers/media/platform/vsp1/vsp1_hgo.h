/*
 * vsp1_hgo.h  --  R-Car VSP1 Histogram Generator 1D
 *
 * Copyright (C) 2016 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef __VSP1_HGO_H__
#define __VSP1_HGO_H__

#include <media/media-entity.h>
#include <media/v4l2-subdev.h>

#include "vsp1_entity.h"

struct vsp1_device;

#define HGO_PAD_SINK				0

struct vsp1_hgo {
	struct vsp1_entity entity;
};

struct vsp1_hgo *vsp1_hgo_create(struct vsp1_device *vsp1);
void vsp1_hgo_frame_end(struct vsp1_entity *hgo);

#endif /* __VSP1_HGO_H__ */
