/*
 * vsp1_request.c  --  R-Car VSP1 Request Management
 *
 * Copyright (C) 2015 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/delay.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/wait.h>

#include <media/media-entity.h>
#include <media/v4l2-subdev.h>

#include "vsp1.h"
#include "vsp1_dl.h"
#include "vsp1_entity.h"
#include "vsp1_pipe.h"
#include "vsp1_request.h"
#include "vsp1_rwpf.h"
#include "vsp1_video.h"

struct media_device_request *vsp1_request_alloc(struct media_device *mdev)
{
	struct vsp1_request *req;

	req = kzalloc(sizeof(*req), GFP_KERNEL);
	if (!req)
		return NULL;

	return &req->req;
}

void vsp1_request_free(struct media_device *mdev,
		       struct media_device_request *mreq)
{
	struct vsp1_request *req = to_vsp1_request(mreq);

	kfree(req);
}

static int vsp1_request_pipeline_build(struct vsp1_request *req,
				       struct vsp1_pipeline *pipe,
				       struct vsp1_rwpf *wpf)
{
	return 0;
}

static void vsp1_request_pipeline_destroy(struct vsp1_pipeline *pipe)
{
	vsp1_pipeline_reset(pipe);
	kfree(pipe);
}

static struct vsp1_pipeline *
vsp1_request_pipeline_create(struct vsp1_request *req, struct vsp1_rwpf *wpf)
{
	struct vsp1_pipeline *pipe;
	int ret;

	pipe = kzalloc(sizeof(*pipe), GFP_KERNEL);
	if (!pipe)
		return ERR_PTR(-ENOMEM);

	vsp1_pipeline_init(pipe);

	pipe->frame_end = vsp1_video_pipeline_frame_end;

	ret = vsp1_request_pipeline_build(req, pipe, wpf);
	if (ret < 0) {
		vsp1_request_pipeline_destroy(pipe);
		return ERR_PTR(ret);
	}

	return pipe;
}

int vsp1_request_queue(struct media_device *mdev,
		       struct media_device_request *mreq)
{
	struct vsp1_device *vsp1 =
		container_of(mdev, struct vsp1_device, media_dev);
	struct vsp1_request *req = to_vsp1_request(mreq);
	struct vsp1_rwpf *wpf = NULL;
	struct vsp1_pipeline *pipe;
	struct vsp1_video *video;
	bool has_request;
	unsigned int i;

	/* 1. Find the capture video node for which a buffer corresponding to
	 * the request has been prepared. This will be our main entry point to
	 * the pipeline.
	 *
	 * TODO: Fix race condition. There would be no need to lock the list
	 * walk as we don't add or remove video nodes at runtime. However, the
	 * media device is registered before the video nodes, so userspace could
	 * call us at probe time before all video nodes are registered.
	 */
	for (i = 0; i < ARRAY_SIZE(vsp1->wpf); ++i) {
		if (!vsp1->wpf[i])
			continue;

		video = vsp1->wpf[i]->video;

		if (mutex_lock_interruptible(&video->lock))
			return -ERESTARTSYS;
		has_request = vb2_is_streaming(&video->queue) &&
			      vb2_queue_has_request(&video->queue, mreq->id);
		mutex_unlock(&video->lock);

		if (has_request) {
			/* A pipeline has a single output. */
			if (wpf)
				return -EINVAL;
			wpf = vsp1->wpf[i];
		}
	}

	if (!wpf)
		return -EINVAL;

	/* 2. Create the pipeline based on the request. */
	pipe = vsp1_request_pipeline_create(req, wpf);
	if (IS_ERR(pipe))
		return PTR_ERR(pipe);

	/* 3. Validate the pipeline. Verify streaming state, buffers and
	 * formats.
	 */
	for (i = 0; i < ARRAY_SIZE(pipe->inputs); ++i) {
		if (!pipe->inputs[i].rpf)
			continue;

		video = pipe->inputs[i].rpf->video;

		if (mutex_lock_interruptible(&video->lock))
			return -ERESTARTSYS;
		has_request = vb2_is_streaming(&video->queue) &&
			      vb2_queue_has_request(&video->queue, mreq->id);
		mutex_unlock(&video->lock);

		if (!has_request)
			return -EINVAL;
	}

	/* 4. Allocate and fill the display list. */
	req->dl = vsp1_dl_list_get(pipe->output->dlm);
	if (!req->dl)
		return -ENOMEM;

	vsp1_pipeline_setup(pipe, req->dl, mreq);

	/* 5. Queue the request. */
	vsp1_pipeline_queue_request(pipe, req);

	return 0;
}
