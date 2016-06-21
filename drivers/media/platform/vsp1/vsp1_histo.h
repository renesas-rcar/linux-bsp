/*
 * vsp1_histo.h  --  R-Car VSP1 Histogram API
 *
 * Copyright (C) 2016 Renesas Electronics Corporation
 * Copyright (C) 2016 Laurent Pinchart
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef __VSP1_HISTO_H__
#define __VSP1_HISTO_H__

#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>

#include <media/media-entity.h>
#include <media/v4l2-dev.h>
#include <media/videobuf2-v4l2.h>

struct vsp1_device;
struct vsp1_pipeline;

struct vsp1_histogram_buffer {
	struct vb2_v4l2_buffer buf;
	struct list_head queue;
	void *addr;
};

struct vsp1_histogram {
	struct vsp1_device *vsp1;
	struct vsp1_pipeline *pipe;

	struct video_device video;
	struct media_pad pad;

	size_t data_size;

	struct mutex lock;
	struct vb2_queue queue;

	spinlock_t irqlock;
	struct list_head irqqueue;

	wait_queue_head_t wait_queue;
	bool readout;
};

static inline struct vsp1_histogram *to_vsp1_histo(struct video_device *vdev)
{
	return container_of(vdev, struct vsp1_histogram, video);
}

int vsp1_histogram_init(struct vsp1_device *vsp1, struct vsp1_histogram *histo,
			const char *name, size_t data_size);
void vsp1_histogram_cleanup(struct vsp1_histogram *histo);

struct vsp1_histogram_buffer *
vsp1_histogram_buffer_get(struct vsp1_histogram *histo);
void vsp1_histogram_buffer_complete(struct vsp1_histogram *histo,
				    struct vsp1_histogram_buffer *buf,
				    size_t size);

#endif /* __VSP1_HISTO_H__ */
