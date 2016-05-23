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

enum rvin_input_type {
	RVIN_INPUT_NONE,
	RVIN_INPUT_DIGITAL,
};

/* Max number of inputs supported */
#define RVIN_INPUT_MAX 1
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
 */
struct rvin_graph_entity {
	struct v4l2_async_subdev asd;
	struct v4l2_subdev *subdev;

	u32 code;
	struct v4l2_mbus_config mbus_cfg;
};

static inline int rvin_mbus_supported(struct rvin_graph_entity *entity)
{
	struct v4l2_subdev *sd = entity->subdev;
	struct v4l2_subdev_mbus_code_enum code = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};

	code.index = 0;
	while (!v4l2_subdev_call(sd, pad, enum_mbus_code, NULL, &code)) {
		code.index++;
		switch (code.code) {
		case MEDIA_BUS_FMT_YUYV8_1X16:
		case MEDIA_BUS_FMT_YUYV8_2X8:
		case MEDIA_BUS_FMT_YUYV10_2X10:
		case MEDIA_BUS_FMT_RGB888_1X24:
			entity->code = code.code;
			return true;
		default:
			break;
		}
	}

	return false;
}

static inline int sd_to_pad_idx(struct v4l2_subdev *sd, int flag)
{
	int pad_idx;
#if defined(CONFIG_MEDIA_CONTROLLER)
	for (pad_idx = 0; pad_idx < sd->entity.num_pads; pad_idx++)
		if (sd->entity.pads[pad_idx].flags == flag)
			break;

	if (pad_idx >= sd->entity.num_pads)
		pad_idx = 0;
#else
	pad_idx = 0;
#endif
	return pad_idx;
}

#endif
