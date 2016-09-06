/*
 * Driver for Renesas R-Car VIN
 *
 * Copyright (C) 2016 Renesas Electronics Corp.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __RCAR_GROUP__
#define __RCAR_GROUP__

#include <media/v4l2-dev.h>
#include <media/v4l2-device.h>

#define RVIN_PORT_LOCAL 0
#define RVIN_PORT_CSI 1
#define RVIN_PORT_REMOTE 2

enum rvin_input_type {
	RVIN_INPUT_NONE,
	RVIN_INPUT_DIGITAL,
	RVIN_INPUT_CSI2,
};

/* Max number of inputs supported */
#define RVIN_INPUT_MAX 7
#define RVIN_INPUT_NAME_SIZE 32

/**
 * struct rvin_input_item - One possible input for the channel
 * @name:	User-friendly name of the input
 * @type:	Type of the input or RVIN_INPUT_NONE if not available
 * @chsel:	The chsel value needed to select this input
 * @sink_idx:	Sink pad number from the subdevice associated with the input
 * @source_idx:	Source pad number from the subdevice associated with the input
 */
struct rvin_input_item {
	char name[RVIN_INPUT_NAME_SIZE];
	enum rvin_input_type type;
	int chsel;
	bool hint;
	int sink_idx;
	int source_idx;
};

/**
 * struct rvin_graph_entity - Video endpoint from async framework
 * @asd:	sub-device descriptor for async framework
 * @subdev:	subdevice matched using async framework
 * @code:	Media bus format from source
 * @mbus_cfg:	Media bus format from DT
 * @source_idx:	Source pad on remote device
 */
struct rvin_graph_entity {
	struct v4l2_async_subdev asd;
	struct v4l2_subdev *subdev;

	u32 code;
	struct v4l2_mbus_config mbus_cfg;

	unsigned int source_idx;
};

bool rvin_mbus_supported(struct rvin_graph_entity *entity);
unsigned int rvin_pad_idx(struct v4l2_subdev *sd, int direction);

struct rvin_group_input_ops {
	int (*g_input_status)(struct v4l2_subdev *sd,
			      struct rvin_input_item *item, u32 *status);
	int (*g_tvnorms)(struct v4l2_subdev *sd,
			 struct rvin_input_item *item, v4l2_std_id *std);
	int (*dv_timings_cap)(struct v4l2_subdev *sd,
			      struct rvin_input_item *item,
			      struct v4l2_dv_timings_cap *cap);
	int (*enum_dv_timings)(struct v4l2_subdev *sd,
			       struct rvin_input_item *item,
			       struct v4l2_enum_dv_timings *timings);
};

struct rvin_group_api {
	int (*get)(struct v4l2_subdev *sd, struct rvin_input_item *inputs);
	int (*put)(struct v4l2_subdev *sd);
	int (*set_input)(struct v4l2_subdev *sd, struct rvin_input_item *item);
	int (*get_code)(struct v4l2_subdev *sd, u32 *code);
	int (*get_mbus_cfg)(struct v4l2_subdev *sd,
			    struct v4l2_mbus_config *mbus_cfg);

	int (*ctrl_add_handler)(struct v4l2_subdev *sd,
				struct v4l2_ctrl_handler *hdl);
	int (*alloc_pad_config)(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config **cfg);

	const struct v4l2_subdev_ops *ops;
	const struct rvin_group_input_ops *input_ops;
};

struct rvin_group_api *rvin_group_probe(struct device *dev,
					struct v4l2_device *v4l2_dev);
int rvin_group_remove(struct rvin_group_api *grp);

#endif
