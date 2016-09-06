/*
 * Driver for Renesas R-Car VIN
 *
 * Copyright (C) 2016 Renesas Electronics Corp.
 * Copyright (C) 2011-2013 Renesas Solutions Corp.
 * Copyright (C) 2013 Cogent Embedded, Inc., <source@cogentembedded.com>
 * Copyright (C) 2008 Magnus Damm
 *
 * Based on the soc-camera rcar_vin driver
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __RCAR_VIN__
#define __RCAR_VIN__

#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-device.h>
#include <media/videobuf2-v4l2.h>

#include "rcar-group.h"

/* Number of HW buffers */
#define HW_BUFFER_NUM 3

/* Address alignment mask for HW buffers */
#define HW_BUFFER_MASK 0x7f

enum chip_id {
	RCAR_H1,
	RCAR_M1,
	RCAR_GEN2,
	RCAR_GEN3,
};

/**
 * STOPPED  - No operation in progress
 * RUNNING  - Operation in progress have buffers
 * STALLED  - No operation in progress have no buffers
 * STOPPING - Stopping operation
 */
enum rvin_dma_state {
	STOPPED = 0,
	RUNNING,
	STALLED,
	STOPPING,
};

/**
 * struct rvin_source_fmt - Source information
 * @width:	Width from source
 * @height:	Height from source
 */
struct rvin_source_fmt {
	u32 width;
	u32 height;
};

/**
 * struct rvin_video_format - Data format stored in memory
 * @fourcc:	Pixelformat
 * @bpp:	Bytes per pixel
 */
struct rvin_video_format {
	u32 fourcc;
	u8 bpp;
};

/**
 * struct rvin_dev - Renesas VIN device structure
 * @dev:		(OF) device
 * @base:		device I/O register space remapped to virtual memory
 * @chip:		type of VIN chip
 *
 * @vdev:		V4L2 video device associated with VIN
 * @v4l2_dev:		V4L2 device
 * @src_pad_idx:	source pad index for media controller drivers
 * @ctrl_handler:	V4L2 control handler
 * @notifier:		V4L2 asynchronous subdevs notifier
 * @digital:		entity in the DT for local digital subdevice
 *
 * @lock:		protects @queue
 * @queue:		vb2 buffers queue
 *
 * @qlock:		protects @queue_buf, @buf_list, @continuous, @sequence
 *			@state
 * @queue_buf:		Keeps track of buffers given to HW slot
 * @buf_list:		list of queued buffers
 * @continuous:		tracks if active operation is continuous or single mode
 * @sequence:		V4L2 buffers sequence number
 * @state:		keeps track of operation state
 *
 * @source:		active format from the video source
 * @format:		active V4L2 pixel format
 *
 * @crop:		active cropping
 * @compose:		active composing
 *
 * @slave:		subdevice used to register with the group master
 * @api:		group api controller (only used on master channel)
 *
 * @current_input:	currently used input in @inputs
 * @inputs:		list of valid inputs sources
 */
struct rvin_dev {
	struct device *dev;
	void __iomem *base;
	enum chip_id chip;

	struct video_device vdev;
	struct v4l2_device v4l2_dev;
	int src_pad_idx;
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_async_notifier notifier;
	struct rvin_graph_entity digital;

	struct mutex lock;
	struct vb2_queue queue;

	spinlock_t qlock;
	struct vb2_v4l2_buffer *queue_buf[HW_BUFFER_NUM];
	struct list_head buf_list;
	bool continuous;
	unsigned int sequence;
	enum rvin_dma_state state;

	struct rvin_source_fmt source;
	struct v4l2_pix_format format;

	struct v4l2_rect crop;
	struct v4l2_rect compose;

	struct v4l2_subdev slave;
	struct rvin_group_api *api;

	int current_input;
	struct rvin_input_item inputs[RVIN_INPUT_MAX];
};

/* Debug */
#define vin_dbg(d, fmt, arg...)		dev_dbg(d->dev, fmt, ##arg)
#define vin_info(d, fmt, arg...)	dev_info(d->dev, fmt, ##arg)
#define vin_warn(d, fmt, arg...)	dev_warn(d->dev, fmt, ##arg)
#define vin_err(d, fmt, arg...)		dev_err(d->dev, fmt, ##arg)

int rvin_dma_probe(struct rvin_dev *vin, int irq);
void rvin_dma_remove(struct rvin_dev *vin);

int rvin_subdev_probe(struct rvin_dev *vin);
void rvin_subdev_remove(struct rvin_dev *vin);

int rvin_v4l2_probe(struct rvin_dev *vin);
void rvin_v4l2_remove(struct rvin_dev *vin);

const struct rvin_video_format *rvin_format_from_pixel(u32 pixelformat);

/* Cropping, composing and scaling */
void rvin_scale_try(struct rvin_dev *vin, struct v4l2_pix_format *pix,
		    u32 width, u32 height);
void rvin_crop_scale_comp(struct rvin_dev *vin);

/* Subdevice group helpers */
#define rvin_input_is_csi(v) (v->inputs[v->current_input].type == \
			      RVIN_INPUT_CSI2)
#define vin_to_group(v) container_of(v->slave.v4l2_dev, struct rvin_dev, \
				     v4l2_dev)->api
#define rvin_subdev_call_local(v, o, f, args...)			\
	(v->digital.subdev ?						\
	 v4l2_subdev_call(v->digital.subdev, o, f, ##args) : -ENODEV)
#define rvin_subdev_call_group(v, o, f, args...)			\
	(!(v)->slave.v4l2_dev ? -ENODEV :				\
	 (vin_to_group(v)->ops->o && vin_to_group(v)->ops->o->f) ?	\
	 vin_to_group(v)->ops->o->f(&v->slave, ##args) : -ENOIOCTLCMD)
#define rvin_subdev_call_group_input(v, i, f, args...)			\
	(!(v)->slave.v4l2_dev ? -ENODEV :				\
	 (vin_to_group(v)->input_ops->f ?				\
	  vin_to_group(v)->input_ops->f(&v->slave, i, ##args) : -ENOIOCTLCMD))
#define rvin_subdev_call(v, o, f, args...)				\
	(rvin_input_is_csi(v) ? rvin_subdev_call_group(v, o, f, ##args) :\
	 rvin_subdev_call_local(v, o, f, ##args))
#define rvin_subdev_call_input(v, i, o, f, args...)			\
	(v->inputs[i].type == RVIN_INPUT_CSI2 ?				\
	 rvin_subdev_call_group_input(v, &v->inputs[i], f, ##args) :	\
	 rvin_subdev_call_local(v, o, f, ##args))

int rvin_subdev_get(struct rvin_dev *vin);
int rvin_subdev_put(struct rvin_dev *vin);
int rvin_subdev_set_input(struct rvin_dev *vin, struct rvin_input_item *item);

int rvin_subdev_get_code(struct rvin_dev *vin, u32 *code);
int rvin_subdev_get_mbus_cfg(struct rvin_dev *vin,
			     struct v4l2_mbus_config *mbus_cfg);

int rvin_subdev_ctrl_add_handler(struct rvin_dev *vin);
struct v4l2_subdev_pad_config *rvin_subdev_alloc_pad_config(struct rvin_dev
							    *vin);
#endif
