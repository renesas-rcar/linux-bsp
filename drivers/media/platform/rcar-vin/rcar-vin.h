/*
 * Driver for Renesas R-Car VIN
 *
 * Copyright (C) 2016-2017 Renesas Electronics Corp.
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

#include <linux/kref.h>

#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-device.h>
#include <media/v4l2-mc.h>
#include <media/videobuf2-v4l2.h>

#define DRV_NAME "rcar-vin"

/* Number of HW buffers */
#define HW_BUFFER_NUM 3

/* Address alignment mask for HW buffers */
#define HW_BUFFER_MASK 0x7f

/* Max number on VIN instances that can be in a system */
#define RCAR_VIN_NUM 8

/* Max number of CHSEL values for any Gen3 SoC */
#define RCAR_CHSEL_MAX 6

enum chip_id {
	RCAR_H1,
	RCAR_M1,
	RCAR_GEN2,
	RCAR_GEN3,
};

enum rvin_csi_id {
	RVIN_CSI20,
	RVIN_CSI21,
	RVIN_CSI40,
	RVIN_CSI41,
	RVIN_CSI_MAX,
	RVIN_NOOPE,
};

enum rvin_pads {
	RVIN_SINK,
	RVIN_PAD_MAX,
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
 * struct rvin_uds_regs - UDS register information
 * @ctrl:		UDS Control register
 * @scale:		UDS Scaling Factor register
 * @pass_bwidth:	UDS Passband Register
 * @clip_size:		UDS Output Size Clipping Register
 */
struct rvin_uds_regs {
	unsigned long ctrl;
	unsigned long scale;
	unsigned long pass_bwidth;
	unsigned long clip_size;
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
 * struct rvin_graph_entity - Video endpoint from async framework
 * @asd:		sub-device descriptor for async framework
 * @subdev:		subdevice matched using async framework
 * @code:		Media bus format from source
 * @mbus_cfg:		Media bus format from DT
 * @source_pad_idx:	source pad index on remote device
 * @sink_pad_idx:	sink pad index on remote device
 */
struct rvin_graph_entity {
	struct v4l2_async_subdev asd;
	struct v4l2_subdev *subdev;

	u32 code;
	struct v4l2_mbus_config mbus_cfg;

	int source_pad_idx;
	int sink_pad_idx;
};

struct rvin_group;


/** struct rvin_group_chsel - Map a CSI2 device and channel for a CHSEL value
 * @csi:		VIN internal number for CSI2 device
 * @chan:		CSI2 VC number on remote
 */
struct rvin_group_chsel {
	enum rvin_csi_id csi;
	int chan;
};

/**
 * struct rvin_info- Information about the particular VIN implementation
 * @chip:		type of VIN chip
 *
 * max_width:		max input with the VIN supports
 * max_height:		max input height the VIN supports
 */
struct rvin_info {
	enum chip_id chip;

	unsigned int max_width;
	unsigned int max_height;

	unsigned int num_chsels;
	struct rvin_group_chsel chsels[RCAR_VIN_NUM][RCAR_CHSEL_MAX];
};

/**
 * struct rvin_dev - Renesas VIN device structure
 * @dev:		(OF) device
 * @base:		device I/O register space remapped to virtual memory
 * @info		info about VIN instance
 *
 * @vdev:		V4L2 video device associated with VIN
 * @v4l2_dev:		V4L2 device
 * @ctrl_handler:	V4L2 control handler
 * @notifier:		V4L2 asynchronous subdevs notifier
 * @digital:		entity in the DT for local digital subdevice
 *
 * @group:		Gen3 CSI group
 * @pads:		pads for media controller
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
 * @last_input:		points to the last active input source
 * @source:		active format from the video source
 * @format:		active V4L2 pixel format
 *
 * @crop:		active cropping
 * @compose:		active composing
 */
struct rvin_dev {
	struct device *dev;
	void __iomem *base;
	const struct rvin_info *info;

	struct video_device vdev;
	struct v4l2_device v4l2_dev;
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_async_notifier notifier;
	struct rvin_graph_entity digital;

	struct rvin_group *group;
	struct media_pad pads[RVIN_PAD_MAX];

	struct mutex lock;
	struct vb2_queue queue;

	spinlock_t qlock;
	struct vb2_v4l2_buffer *queue_buf[HW_BUFFER_NUM];
	struct list_head buf_list;
	bool continuous;
	unsigned int sequence;
	enum rvin_dma_state state;

	struct rvin_graph_entity *last_input;
	struct rvin_source_fmt source;
	struct v4l2_pix_format format;

	struct v4l2_rect crop;
	struct v4l2_rect compose;

	unsigned int index;
	unsigned int chsel;
};

bool vin_have_bridge(struct rvin_dev *vin);
struct rvin_graph_entity *vin_to_entity(struct rvin_dev *vin);
struct v4l2_subdev *vin_to_source(struct rvin_dev *vin);
struct v4l2_subdev *vin_to_bridge(struct rvin_dev *vin);

/* Debug */
#define vin_dbg(d, fmt, arg...)		dev_dbg(d->dev, fmt, ##arg)
#define vin_info(d, fmt, arg...)	dev_info(d->dev, fmt, ##arg)
#define vin_warn(d, fmt, arg...)	dev_warn(d->dev, fmt, ##arg)
#define vin_err(d, fmt, arg...)		dev_err(d->dev, fmt, ##arg)

/**
 * struct rvin_group - VIN CSI2 group information
 * @refcount:		number of VIN instances using the group
 *
 * @mdev:		media device which represents the group
 *
 * @lock:		protects the vin, bridge and source members
 * @vin:		VIN instances which are part of the group
 * @bridge:		CSI2 bridge between video source and VIN
 * @source:		video source connected to each bridge
 */
struct rvin_group {
	struct kref refcount;

	struct media_device mdev;

	struct mutex lock;
	struct rvin_dev *vin[RCAR_VIN_NUM];
	struct rvin_graph_entity bridge[RVIN_CSI_MAX];
	struct rvin_graph_entity source[RVIN_CSI_MAX];
};

int rvin_dma_probe(struct rvin_dev *vin, int irq);
void rvin_dma_remove(struct rvin_dev *vin);

int rvin_v4l2_probe(struct rvin_dev *vin);
void rvin_v4l2_remove(struct rvin_dev *vin);

const struct rvin_video_format *rvin_format_from_pixel(u32 pixelformat);

/* Cropping, composing and scaling */
void rvin_scale_try(struct rvin_dev *vin, struct v4l2_pix_format *pix,
		    u32 width, u32 height);
void rvin_crop_scale_comp(struct rvin_dev *vin);

int rvin_set_chsel(struct rvin_dev *vin, u8 chsel);
int rvin_get_chsel(struct rvin_dev *vin);

int rvin_resume_start_streaming(struct rvin_dev *vin);
int rvin_suspend_stop_streaming(struct rvin_dev *vin);

#endif
