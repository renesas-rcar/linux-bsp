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

#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>

#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-device.h>
#include <media/v4l2-of.h>

#include "rcar-group.h"

/* Max chsel supported by HW */
#define RVIN_CHSEL_MAX 6

enum rvin_csi_id {
	RVIN_CSI20,
	RVIN_CSI21,
	RVIN_CSI40,
	RVIN_CSI41,
	RVIN_CSI_MAX,
	RVIN_CSI_ERROR,
};

enum rvin_chan_id {
	RVIN_CHAN0,
	RVIN_CHAN1,
	RVIN_CHAN2,
	RVIN_CHAN3,
	RVIN_CHAN4,
	RVIN_CHAN5,
	RVIN_CHAN6,
	RVIN_CHAN7,
	RVIN_CHAN_MAX,
	RVIN_CHAN_ERROR,
};

struct rvin_group_map_item {
	enum rvin_csi_id csi;
	const char *name;
};

static const struct rvin_group_map_item
rvin_group_map[RVIN_CHAN_MAX][RVIN_CHSEL_MAX] = {
	{
		{ .csi = RVIN_CSI40, .name = "CSI40/VC0 chsel1: 0" },
		{ .csi = RVIN_CSI20, .name = "CSI20/VC0 chsel1: 1" },
		{ .csi = RVIN_CSI21, .name = "CSI21/VC0 chsel1: 2" },
		{ .csi = RVIN_CSI40, .name = "CSI40/VC0 chsel1: 3" },
		{ .csi = RVIN_CSI20, .name = "CSI20/VC0 chsel1: 4" },
		{ .csi = RVIN_CSI21, .name = "CSI21/VC0 chsel1: 5" },
	}, {
		{ .csi = RVIN_CSI20, .name = "CSI20/VC0 chsel1: 0" },
		{ .csi = RVIN_CSI21, .name = "CSI21/VC0 chsel1: 1" },
		{ .csi = RVIN_CSI40, .name = "CSI40/VC0 chsel1: 2" },
		{ .csi = RVIN_CSI40, .name = "CSI40/VC1 chsel1: 3" },
		{ .csi = RVIN_CSI20, .name = "CSI20/VC1 chsel1: 4" },
		{ .csi = RVIN_CSI21, .name = "CSI21/VC1 chsel1: 5" },
	}, {
		{ .csi = RVIN_CSI21, .name = "CSI21/VC0 chsel1: 0" },
		{ .csi = RVIN_CSI40, .name = "CSI40/VC0 chsel1: 1" },
		{ .csi = RVIN_CSI20, .name = "CSI20/VC0 chsel1: 2" },
		{ .csi = RVIN_CSI40, .name = "CSI40/VC2 chsel1: 3" },
		{ .csi = RVIN_CSI20, .name = "CSI20/VC2 chsel1: 4" },
		{ .csi = RVIN_CSI21, .name = "CSI21/VC2 chsel1: 5" },
	}, {
		{ .csi = RVIN_CSI40, .name = "CSI40/VC1 chsel1: 0" },
		{ .csi = RVIN_CSI20, .name = "CSI20/VC1 chsel1: 1" },
		{ .csi = RVIN_CSI21, .name = "CSI21/VC1 chsel1: 2" },
		{ .csi = RVIN_CSI40, .name = "CSI40/VC3 chsel1: 3" },
		{ .csi = RVIN_CSI20, .name = "CSI20/VC3 chsel1: 4" },
		{ .csi = RVIN_CSI21, .name = "CSI21/VC3 chsel1: 5" },
	}, {
		{ .csi = RVIN_CSI41, .name = "CSI41/VC0 chsel2: 0" },
		{ .csi = RVIN_CSI20, .name = "CSI20/VC0 chsel2: 1" },
		{ .csi = RVIN_CSI21, .name = "CSI21/VC0 chsel2: 2" },
		{ .csi = RVIN_CSI41, .name = "CSI41/VC0 chsel2: 3" },
		{ .csi = RVIN_CSI20, .name = "CSI20/VC0 chsel2: 4" },
		{ .csi = RVIN_CSI21, .name = "CSI21/VC0 chsel2: 5" },
	}, {
		{ .csi = RVIN_CSI20, .name = "CSI20/VC0 chsel2: 0" },
		{ .csi = RVIN_CSI21, .name = "CSI21/VC0 chsel2: 1" },
		{ .csi = RVIN_CSI41, .name = "CSI41/VC0 chsel2: 2" },
		{ .csi = RVIN_CSI41, .name = "CSI41/VC1 chsel2: 3" },
		{ .csi = RVIN_CSI20, .name = "CSI20/VC1 chsel2: 4" },
		{ .csi = RVIN_CSI21, .name = "CSI21/VC1 chsel2: 5" },
	}, {
		{ .csi = RVIN_CSI21, .name = "CSI21/VC0 chsel2: 0" },
		{ .csi = RVIN_CSI41, .name = "CSI41/VC0 chsel2: 1" },
		{ .csi = RVIN_CSI20, .name = "CSI20/VC0 chsel2: 2" },
		{ .csi = RVIN_CSI41, .name = "CSI41/VC2 chsel2: 3" },
		{ .csi = RVIN_CSI20, .name = "CSI20/VC2 chsel2: 4" },
		{ .csi = RVIN_CSI21, .name = "CSI21/VC2 chsel2: 5" },
	}, {
		{ .csi = RVIN_CSI41, .name = "CSI41/VC1 chsel2: 0" },
		{ .csi = RVIN_CSI20, .name = "CSI20/VC1 chsel2: 1" },
		{ .csi = RVIN_CSI21, .name = "CSI21/VC1 chsel2: 2" },
		{ .csi = RVIN_CSI41, .name = "CSI41/VC3 chsel2: 3" },
		{ .csi = RVIN_CSI20, .name = "CSI20/VC3 chsel2: 4" },
		{ .csi = RVIN_CSI21, .name = "CSI21/VC3 chsel2: 5" },
	},
};

struct rvin_group {
	struct device *dev;
	struct v4l2_device *v4l2_dev;
	struct mutex lock;

	struct rvin_group_api api;

	struct v4l2_async_notifier notifier;

	struct rvin_graph_entity bridge[RVIN_CSI_MAX];
	struct rvin_graph_entity source[RVIN_CSI_MAX];
	int stream[RVIN_CSI_MAX];
	int power[RVIN_CSI_MAX];

	struct rvin_graph_entity chan[RVIN_CHAN_MAX];
	int users[RVIN_CHAN_MAX];

	int chsel1;
	int chsel2;
};

#define grp_dbg(d, fmt, arg...)		dev_dbg(d->dev, fmt, ##arg)
#define grp_info(d, fmt, arg...)	dev_info(d->dev, fmt, ##arg)
#define grp_err(d, fmt, arg...)		dev_err(d->dev, fmt, ##arg)

/* -----------------------------------------------------------------------------
 * Common - Helpers
 */

bool rvin_mbus_supported(struct rvin_graph_entity *entity)
{
	struct v4l2_subdev *sd = entity->subdev;
	struct v4l2_subdev_mbus_code_enum code = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};

	code.index = 0;
	code.pad = entity->source_idx;
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

unsigned int rvin_pad_idx(struct v4l2_subdev *sd, int direction)
{
	unsigned int pad_idx;

	for (pad_idx = 0; pad_idx < sd->entity.num_pads; pad_idx++)
		if (sd->entity.pads[pad_idx].flags == direction)
			return pad_idx;

	return 0;
}

/* -----------------------------------------------------------------------------
 * Group API - Helpers
 */

static enum rvin_chan_id sd_to_chan(struct rvin_group *grp,
				    struct v4l2_subdev *sd)
{
	enum rvin_chan_id chan = RVIN_CHAN_ERROR;
	int i;

	for (i = 0; i < RVIN_CHAN_MAX; i++) {
		if (grp->chan[i].subdev == sd) {
			chan =  i;
			break;
		}
	}

	/* Something is wrong, subdevice can't be resolved to channel id */
	BUG_ON(chan == RVIN_CHAN_ERROR);

	return chan;
}

static enum rvin_chan_id chan_to_master(struct rvin_group *grp,
					enum rvin_chan_id chan)
{
	enum rvin_chan_id master;

	switch (chan) {
	case RVIN_CHAN0:
	case RVIN_CHAN1:
	case RVIN_CHAN2:
	case RVIN_CHAN3:
		master = RVIN_CHAN0;
		break;
	case RVIN_CHAN4:
	case RVIN_CHAN5:
	case RVIN_CHAN6:
	case RVIN_CHAN7:
		master = RVIN_CHAN4;
		break;
	default:
		master = RVIN_CHAN_ERROR;
		break;
	}

	/* Something is wrong, subdevice can't be resolved to channel id */
	BUG_ON(master == RVIN_CHAN_ERROR);

	return master;
}

static enum rvin_csi_id rvin_group_get_csi(struct rvin_group *grp,
					   struct v4l2_subdev *sd, int chsel)
{
	enum rvin_chan_id chan;
	enum rvin_csi_id csi;

	if (chsel < 0 || chsel > RVIN_CHSEL_MAX)
		return RVIN_CSI_ERROR;

	chan = sd_to_chan(grp, sd);

	csi = rvin_group_map[sd_to_chan(grp, sd)][chsel].csi;

	/* Not all CSI source might be available */
	if (!grp->bridge[csi].subdev || !grp->source[csi].subdev)
		return RVIN_CSI_ERROR;

	return csi;
}

static int rvin_group_chsel_get(struct rvin_group *grp, struct v4l2_subdev *sd)
{
	enum rvin_chan_id master;

	master = chan_to_master(grp, sd_to_chan(grp, sd));

	if (master == RVIN_CHAN0)
		return grp->chsel1;

	return grp->chsel2;
}

static void rvin_group_chsel_set(struct rvin_group *grp, struct v4l2_subdev *sd,
				 int chsel)
{
	enum rvin_chan_id master;

	master = chan_to_master(grp, sd_to_chan(grp, sd));

	if (master == RVIN_CHAN0)
		grp->chsel1 = chsel;
	else
		grp->chsel2 = chsel;
}

static enum rvin_csi_id sd_to_csi(struct rvin_group *grp,
				  struct v4l2_subdev *sd)
{
	return rvin_group_get_csi(grp, sd, rvin_group_chsel_get(grp, sd));
}

/* -----------------------------------------------------------------------------
 * Group API - logic
 */

static int rvin_group_get_sink_idx(struct media_entity *entity, int source_idx)
{
	unsigned int i;

	/* Iterates through the pads to find a connected sink pad. */
	for (i = 0; i < entity->num_pads; ++i) {
		struct media_pad *sink = &entity->pads[i];

		if (!(sink->flags & MEDIA_PAD_FL_SINK))
			continue;

		if (sink->index == source_idx)
			continue;

		if (media_entity_has_route(entity, sink->index, source_idx))
			return sink->index;
	}

	/* If not found return the first pad, guaranteed to be a sink pad. */
	return 0;
}

static int rvin_group_get(struct v4l2_subdev *sd,
			  struct rvin_input_item *inputs)
{
	struct rvin_group *grp = v4l2_get_subdev_hostdata(sd);
	enum rvin_chan_id chan, master;
	enum rvin_csi_id csi;
	int i, num = 0;

	mutex_lock(&grp->lock);

	chan = sd_to_chan(grp, sd);

	/* If subgroup master is not present channel is useless */
	master = chan_to_master(grp, chan);
	if (!grp->chan[master].subdev) {
		grp_err(grp, "chan%d: No group master found\n", chan);
		goto out;
	}

	/* Make sure channel is usable with current chsel */
	if (sd_to_csi(grp, sd) == RVIN_CSI_ERROR) {
		grp_info(grp, "chan%d: Unavailable with current chsel\n", chan);
		goto out;
	}

	/* Create list of valid inputs */
	for (i = 0; i < RVIN_CHSEL_MAX; i++) {
		csi = rvin_group_get_csi(grp, sd, i);
		if (rvin_group_get_csi(grp, sd, i) != RVIN_CSI_ERROR) {
			inputs[num].type = RVIN_INPUT_CSI2;
			inputs[num].chsel = i;
			inputs[num].hint = rvin_group_chsel_get(grp, sd) == i;
			inputs[num].source_idx = grp->source[csi].source_idx;
			inputs[num].sink_idx =
				rvin_group_get_sink_idx(
					&grp->source[csi].subdev->entity,
					inputs[num].source_idx);
			strlcpy(inputs[num].name, rvin_group_map[chan][i].name,
				RVIN_INPUT_NAME_SIZE);
			grp_dbg(grp, "chan%d: %s source pad: %d sink pad: %d\n",
				chan, inputs[num].name, inputs[num].source_idx,
				inputs[num].sink_idx);
			num++;
		}
	}

	grp->users[chan]++;
out:
	mutex_unlock(&grp->lock);

	return num;
}

static int rvin_group_put(struct v4l2_subdev *sd)
{
	struct rvin_group *grp = v4l2_get_subdev_hostdata(sd);

	mutex_lock(&grp->lock);
	grp->users[sd_to_chan(grp, sd)]--;
	mutex_unlock(&grp->lock);

	return 0;
}

static int rvin_group_set_input(struct v4l2_subdev *sd,
				struct rvin_input_item *item)
{
	struct rvin_group *grp = v4l2_get_subdev_hostdata(sd);
	enum rvin_chan_id chan, master;
	int i, chsel, ret = 0;

	chan = sd_to_chan(grp, sd);
	chsel = item->chsel;

	mutex_lock(&grp->lock);

	/* No need to set chsel if it's already set */
	if (chsel == rvin_group_chsel_get(grp, sd))
		goto out;

	/* Do not allow a chsel that is not usable for channel */
	if (rvin_group_get_csi(grp, sd, chsel) == RVIN_CSI_ERROR) {
		grp_err(grp, "chan%d: Invalid chsel\n", chan);
		goto out;
	}

	/* If subgroup master is not present we can't write the chsel */
	master = chan_to_master(grp, chan);
	if (!grp->chan[master].subdev) {
		grp_err(grp, "chan%d: No group master found\n", chan);
		goto out;
	}

	/*
	 * Check that all needed resurces are free. We don't want to
	 * change input selection if somone else uses our subgroup or
	 * if there are another user of our channel.
	 */
	for (i = 0; i < RVIN_CHAN_MAX; i++) {

		/* Only look in our sub group */
		if (master != chan_to_master(grp, i))
			continue;

		/* Need to be only user of channel and subgroup to set hsel */
		if ((i == chan && grp->users[i] != 1) ||
		    (i != chan && grp->users[i] != 0)) {
			grp_info(grp, "chan%d: %s in use, can't set chsel\n",
				 chan, i == chan ? "Channel" : "Group");
			ret = -EBUSY;
			goto out;
		}
	}

	ret = v4l2_subdev_call(grp->chan[master].subdev, core, s_gpio, chsel);
	rvin_group_chsel_set(grp, sd, chsel);
out:
	mutex_unlock(&grp->lock);
	return ret;
}

static int rvin_group_get_code(struct v4l2_subdev *sd, u32 *code)
{
	struct rvin_group *grp = v4l2_get_subdev_hostdata(sd);
	enum rvin_csi_id csi;

	csi = sd_to_csi(grp, sd);
	if (csi == RVIN_CSI_ERROR)
		return -ENODEV;

	*code = grp->source[csi].code;

	return 0;
}

static int rvin_group_get_mbus_cfg(struct v4l2_subdev *sd,
				   struct v4l2_mbus_config *mbus_cfg)
{
	struct rvin_group *grp = v4l2_get_subdev_hostdata(sd);
	enum rvin_csi_id csi;

	csi = sd_to_csi(grp, sd);
	if (csi == RVIN_CSI_ERROR)
		return -ENODEV;

	*mbus_cfg = grp->source[csi].mbus_cfg;

	return 0;
}

static int rvin_group_ctrl_add_handler(struct v4l2_subdev *sd,
				       struct v4l2_ctrl_handler *hdl)
{
	struct rvin_group *grp = v4l2_get_subdev_hostdata(sd);
	enum rvin_csi_id csi;

	csi = sd_to_csi(grp, sd);
	if (csi == RVIN_CSI_ERROR)
		return -ENODEV;

	return v4l2_ctrl_add_handler(hdl, grp->source[csi].subdev->ctrl_handler,
				     NULL);
}

static int rvin_group_alloc_pad_config(struct v4l2_subdev *sd,
				       struct v4l2_subdev_pad_config **cfg)
{
	struct rvin_group *grp = v4l2_get_subdev_hostdata(sd);
	enum rvin_csi_id csi;

	csi = sd_to_csi(grp, sd);
	if (csi == RVIN_CSI_ERROR)
		return -ENODEV;

	*cfg =  v4l2_subdev_alloc_pad_config(grp->source[csi].subdev);

	return 0;
}

static int rvin_group_g_tvnorms_input(struct v4l2_subdev *sd,
				      struct rvin_input_item *item,
				      v4l2_std_id *std)
{
	struct rvin_group *grp = v4l2_get_subdev_hostdata(sd);
	enum rvin_csi_id csi;

	csi = rvin_group_get_csi(grp, sd, item->chsel);

	if (csi == RVIN_CSI_ERROR)
		return -EINVAL;

	return v4l2_subdev_call(grp->source[csi].subdev, video,
				g_tvnorms, std);
}

static int rvin_group_g_input_status(struct v4l2_subdev *sd,
				     struct rvin_input_item *item, u32 *status)
{
	struct rvin_group *grp = v4l2_get_subdev_hostdata(sd);
	enum rvin_csi_id csi;

	csi = rvin_group_get_csi(grp, sd, item->chsel);

	if (csi == RVIN_CSI_ERROR)
		return -EINVAL;

	return v4l2_subdev_call(grp->source[csi].subdev, video,
				g_input_status, status);
}

static int rvin_group_dv_timings_cap(struct v4l2_subdev *sd,
				     struct rvin_input_item *item,
				     struct v4l2_dv_timings_cap *cap)
{
	struct rvin_group *grp = v4l2_get_subdev_hostdata(sd);
	enum rvin_csi_id csi;

	csi = rvin_group_get_csi(grp, sd, item->chsel);

	if (csi == RVIN_CSI_ERROR)
		return -EINVAL;

	return v4l2_subdev_call(grp->source[csi].subdev, pad,
				dv_timings_cap, cap);
}

static int rvin_group_enum_dv_timings(struct v4l2_subdev *sd,
				      struct rvin_input_item *item,
				      struct v4l2_enum_dv_timings *timings)
{
	struct rvin_group *grp = v4l2_get_subdev_hostdata(sd);
	enum rvin_csi_id csi;

	csi = rvin_group_get_csi(grp, sd, item->chsel);

	if (csi == RVIN_CSI_ERROR)
		return -EINVAL;

	return v4l2_subdev_call(grp->source[csi].subdev, pad,
				enum_dv_timings, timings);
}

static const struct rvin_group_input_ops rvin_input_ops = {
	.g_tvnorms = &rvin_group_g_tvnorms_input,
	.g_input_status = &rvin_group_g_input_status,
	.dv_timings_cap = &rvin_group_dv_timings_cap,
	.enum_dv_timings = &rvin_group_enum_dv_timings,
};

/* -----------------------------------------------------------------------------
 * Basic group subdev operations
 */

static int rvin_group_s_power(struct v4l2_subdev *sd, int on)
{
	struct rvin_group *grp = v4l2_get_subdev_hostdata(sd);
	enum rvin_csi_id csi;
	int ret = 0;

	csi = sd_to_csi(grp, sd);
	if (csi == RVIN_CSI_ERROR)
		return -ENODEV;

	mutex_lock(&grp->lock);
	/* If we already are powerd just increment the usage */
	if ((on && grp->power[csi] != 0) || (!on && grp->power[csi] != 1))
		goto unlock;

	/* Important to start bridge fist, it needs a quiet bus to start */
	ret = v4l2_subdev_call(grp->bridge[csi].subdev, core, s_power, on);
	if (ret < 0 && ret != -ENOIOCTLCMD && ret != -ENODEV)
		goto unlock_err;
	ret = v4l2_subdev_call(grp->source[csi].subdev, core, s_power, on);
	if (ret < 0 && ret != -ENOIOCTLCMD && ret != -ENODEV)
		goto unlock_err;

	grp_dbg(grp, "csi%d: power: %d bridge: %s source: %s\n", csi,
		on, grp->bridge[csi].subdev->name,
		grp->source[csi].subdev->name);
unlock:
	grp->power[csi] += on ? 1 : -1;
unlock_err:
	mutex_unlock(&grp->lock);
	return ret;
}

static const struct v4l2_subdev_core_ops rvin_group_core_ops = {
	.s_power	= &rvin_group_s_power,
};

static int rvin_group_g_std(struct v4l2_subdev *sd, v4l2_std_id *std)
{
	struct rvin_group *grp = v4l2_get_subdev_hostdata(sd);
	enum rvin_csi_id csi;

	csi = sd_to_csi(grp, sd);
	if (csi == RVIN_CSI_ERROR)
		return -ENODEV;

	return v4l2_subdev_call(grp->source[csi].subdev, video, g_std, std);
}

static int rvin_group_s_std(struct v4l2_subdev *sd, v4l2_std_id std)
{
	struct rvin_group *grp = v4l2_get_subdev_hostdata(sd);
	enum rvin_csi_id csi;

	csi = sd_to_csi(grp, sd);
	if (csi == RVIN_CSI_ERROR)
		return -ENODEV;

	return v4l2_subdev_call(grp->source[csi].subdev, video, s_std, std);
}

static int rvin_group_querystd(struct v4l2_subdev *sd, v4l2_std_id *std)
{
	struct rvin_group *grp = v4l2_get_subdev_hostdata(sd);
	enum rvin_csi_id csi;

	csi = sd_to_csi(grp, sd);
	if (csi == RVIN_CSI_ERROR)
		return -ENODEV;

	return v4l2_subdev_call(grp->source[csi].subdev, video, querystd, std);
}

static int rvin_group_g_tvnorms(struct v4l2_subdev *sd, v4l2_std_id *tvnorms)
{
	struct rvin_group *grp = v4l2_get_subdev_hostdata(sd);
	enum rvin_csi_id csi;

	csi = sd_to_csi(grp, sd);
	if (csi == RVIN_CSI_ERROR)
		return -ENODEV;

	return v4l2_subdev_call(grp->source[csi].subdev, video, g_tvnorms,
				tvnorms);
}

static int rvin_group_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct rvin_group *grp = v4l2_get_subdev_hostdata(sd);
	enum rvin_csi_id csi;
	int ret = 0;

	mutex_lock(&grp->lock);

	csi = sd_to_csi(grp, sd);
	if (csi == RVIN_CSI_ERROR) {
		ret = -ENODEV;
		goto unlock_err;
	}

	/* If we already are streaming just increment the usage */
	if ((enable && grp->stream[csi] != 0) ||
	    (!enable && grp->stream[csi] != 1))
		goto unlock;

	/* Important to start bridge fist, it needs a quiet bus to start */
	ret = v4l2_subdev_call(grp->bridge[csi].subdev, video, s_stream,
			       enable);
	if (ret < 0 && ret != -ENOIOCTLCMD && ret != -ENODEV)
		goto unlock_err;
	ret = v4l2_subdev_call(grp->source[csi].subdev, video, s_stream,
			       enable);
	if (ret < 0 && ret != -ENOIOCTLCMD && ret != -ENODEV)
		goto unlock_err;

	grp_dbg(grp, "csi%d: stream: %d bridge: %s source %s\n", csi,
		enable, grp->bridge[csi].subdev->name,
		grp->source[csi].subdev->name);
unlock:
	grp->stream[csi] += enable ? 1 : -1;
unlock_err:
	mutex_unlock(&grp->lock);
	return ret;
}

static int rvin_group_cropcap(struct v4l2_subdev *sd, struct v4l2_cropcap *crop)
{
	struct rvin_group *grp = v4l2_get_subdev_hostdata(sd);
	enum rvin_csi_id csi;

	csi = sd_to_csi(grp, sd);
	if (csi == RVIN_CSI_ERROR)
		return -ENODEV;

	return v4l2_subdev_call(grp->source[csi].subdev, video, cropcap, crop);
}

static int rvin_group_g_dv_timings(struct v4l2_subdev *sd,
				   struct v4l2_dv_timings *timings)
{
	struct rvin_group *grp = v4l2_get_subdev_hostdata(sd);
	enum rvin_csi_id csi;

	csi = sd_to_csi(grp, sd);
	if (csi == RVIN_CSI_ERROR)
		return -ENODEV;

	return v4l2_subdev_call(grp->source[csi].subdev, video,
				g_dv_timings, timings);
}

static int rvin_group_s_dv_timings(struct v4l2_subdev *sd,
				   struct v4l2_dv_timings *timings)
{
	struct rvin_group *grp = v4l2_get_subdev_hostdata(sd);
	enum rvin_csi_id csi;

	csi = sd_to_csi(grp, sd);
	if (csi == RVIN_CSI_ERROR)
		return -ENODEV;

	return v4l2_subdev_call(grp->source[csi].subdev, video,
				s_dv_timings, timings);
}

static int rvin_group_query_dv_timings(struct v4l2_subdev *sd,
				       struct v4l2_dv_timings *timings)
{
	struct rvin_group *grp = v4l2_get_subdev_hostdata(sd);
	enum rvin_csi_id csi;

	csi = sd_to_csi(grp, sd);
	if (csi == RVIN_CSI_ERROR)
		return -ENODEV;

	return v4l2_subdev_call(grp->source[csi].subdev, video,
				query_dv_timings, timings);
}

static const struct v4l2_subdev_video_ops rvin_group_video_ops = {
	.g_std			= rvin_group_g_std,
	.s_std			= rvin_group_s_std,
	.querystd		= rvin_group_querystd,
	.g_tvnorms		= rvin_group_g_tvnorms,
	.s_stream		= rvin_group_s_stream,
	.cropcap		= rvin_group_cropcap,
	.g_dv_timings		= rvin_group_g_dv_timings,
	.s_dv_timings		= rvin_group_s_dv_timings,
	.query_dv_timings	= rvin_group_query_dv_timings,
};

static int rvin_group_get_fmt(struct v4l2_subdev *sd,
			      struct v4l2_subdev_pad_config *pad_cfg,
			      struct v4l2_subdev_format *fmt)
{
	struct rvin_group *grp = v4l2_get_subdev_hostdata(sd);
	enum rvin_csi_id csi;

	csi = sd_to_csi(grp, sd);
	if (csi == RVIN_CSI_ERROR)
		return -ENODEV;

	return v4l2_subdev_call(grp->source[csi].subdev, pad, get_fmt,
				pad_cfg, fmt);
}

static int rvin_group_set_fmt(struct v4l2_subdev *sd,
			      struct v4l2_subdev_pad_config *pad_cfg,
			      struct v4l2_subdev_format *fmt)
{
	struct rvin_group *grp = v4l2_get_subdev_hostdata(sd);
	enum rvin_csi_id csi;
	int ret;

	csi = sd_to_csi(grp, sd);
	if (csi == RVIN_CSI_ERROR)
		return -ENODEV;

	/* First the source and then inform the bridge about the format. */
	ret = v4l2_subdev_call(grp->source[csi].subdev, pad, set_fmt,
			       pad_cfg, fmt);
	if (ret < 0 && ret != -ENOIOCTLCMD && ret != -ENODEV)
		return ret;
	return v4l2_subdev_call(grp->bridge[csi].subdev, pad, set_fmt,
				NULL, fmt);
}

static const struct v4l2_subdev_pad_ops rvin_group_pad_ops = {
	.get_fmt	= rvin_group_get_fmt,
	.set_fmt	= rvin_group_set_fmt,
};

static const struct v4l2_subdev_ops rvin_group_ops = {
	.core		= &rvin_group_core_ops,
	.video		= &rvin_group_video_ops,
	.pad		= &rvin_group_pad_ops,
};

/* -----------------------------------------------------------------------------
 * Async notifier
 */

#define notifier_to_grp(n) container_of(n, struct rvin_group, notifier)

static void __verify_source_pad(struct rvin_graph_entity *entity)
{
	if (entity->source_idx >= entity->subdev->entity.num_pads)
		goto use_default;
	if (entity->subdev->entity.pads[entity->source_idx].flags !=
	    MEDIA_PAD_FL_SOURCE)
		goto use_default;
	return;
use_default:
	entity->source_idx = rvin_pad_idx(entity->subdev, MEDIA_PAD_FL_SOURCE);
}


static int rvin_graph_notify_complete(struct v4l2_async_notifier *notifier)
{
	struct rvin_group *grp = notifier_to_grp(notifier);
	int i, ret;

	for (i = 0; i < RVIN_CSI_MAX; i++) {
		if (!grp->source[i].subdev)
			continue;

		__verify_source_pad(&grp->source[i]);

		if (!rvin_mbus_supported(&grp->source[i])) {
			grp_err(grp,
				"Unsupported media bus format for %s pad %d\n",
				grp->source[i].subdev->name,
				grp->source[i].source_idx);
			return -EINVAL;
		}

		grp_dbg(grp, "Found media bus format for %s pad %d: %d\n",
			grp->source[i].subdev->name, grp->source[i].source_idx,
			grp->source[i].code);
	}

	ret = v4l2_device_register_subdev_nodes(grp->v4l2_dev);
	if (ret < 0) {
		grp_err(grp, "Failed to register subdev nodes\n");
		return ret;
	}

	return 0;
}

static void rvin_graph_notify_unbind(struct v4l2_async_notifier *notifier,
				     struct v4l2_subdev *subdev,
				     struct v4l2_async_subdev *asd)
{
	struct rvin_group *grp = notifier_to_grp(notifier);
	bool matched = false;
	int i;

	for (i = 0; i < RVIN_CSI_MAX; i++) {
		if (grp->bridge[i].subdev == subdev) {
			grp_dbg(grp, "unbind bridge subdev %s\n", subdev->name);
			grp->bridge[i].subdev = NULL;
			matched = true;
		}
		if (grp->source[i].subdev == subdev) {
			grp_dbg(grp, "unbind source subdev %s\n", subdev->name);
			grp->source[i].subdev = NULL;
			matched = true;
		}
	}

	for (i = 0; i < RVIN_CHAN_MAX; i++) {
		if (grp->chan[i].subdev == subdev) {
			grp_dbg(grp, "unbind chan subdev %s\n", subdev->name);
			grp->chan[i].subdev = NULL;
			matched = true;
		}
	}

	if (!matched)
		grp_err(grp, "no entity for subdev %s to unbind\n",
			subdev->name);
}

static int rvin_graph_notify_bound(struct v4l2_async_notifier *notifier,
				   struct v4l2_subdev *subdev,
				   struct v4l2_async_subdev *asd)
{
	struct rvin_group *grp = notifier_to_grp(notifier);
	bool matched = false;
	int i;

	v4l2_set_subdev_hostdata(subdev, grp);

	for (i = 0; i < RVIN_CSI_MAX; i++) {
		if (grp->bridge[i].asd.match.of.node == subdev->dev->of_node) {
			grp_dbg(grp, "bound bridge subdev %s\n", subdev->name);
			grp->bridge[i].subdev = subdev;
			matched = true;
		}
		if (grp->source[i].asd.match.of.node == subdev->dev->of_node) {
			grp_dbg(grp, "bound source subdev %s\n", subdev->name);
			grp->source[i].subdev = subdev;
			matched = true;
		}
	}

	for (i = 0; i < RVIN_CHAN_MAX; i++) {
		if (grp->chan[i].asd.match.of.node == subdev->dev->of_node) {
			grp_dbg(grp, "bound chan subdev %s\n", subdev->name);
			grp->chan[i].subdev = subdev;

			/* Write initial chsel if binding subgroup master */
			if (i == RVIN_CHAN0)
				v4l2_subdev_call(subdev, core, s_gpio,
						 grp->chsel1);
			if (i == RVIN_CHAN4)
				v4l2_subdev_call(subdev, core, s_gpio,
						 grp->chsel2);

			matched = true;
		}
	}

	if (matched)
		return 0;

	grp_err(grp, "no entity for subdev %s to bind\n", subdev->name);
	return -EINVAL;
}

static int rvin_parse_v4l2_endpoint(struct rvin_group *grp,
				    struct device_node *ep,
				    struct v4l2_mbus_config *mbus_cfg)
{
	struct v4l2_of_endpoint v4l2_ep;
	int ret;

	ret = v4l2_of_parse_endpoint(ep, &v4l2_ep);
	if (ret) {
		grp_err(grp, "Could not parse v4l2 endpoint\n");
		return -EINVAL;
	}

	if (v4l2_ep.bus_type != V4L2_MBUS_CSI2) {
		grp_err(grp, "Unsupported media bus type for %s\n",
			of_node_full_name(ep));
		return -EINVAL;
	}

	mbus_cfg->type = v4l2_ep.bus_type;
	mbus_cfg->flags = v4l2_ep.bus.mipi_csi2.flags;

	return 0;
}

static int rvin_get_csi_source(struct rvin_group *grp, int id)
{
	struct device_node *ep, *np, *rp, *bridge = NULL, *source = NULL;
	struct v4l2_mbus_config mbus_cfg;
	struct of_endpoint endpoint;
	int ret;

	grp->bridge[id].asd.match.of.node = NULL;
	grp->bridge[id].subdev = NULL;
	grp->source[id].asd.match.of.node = NULL;
	grp->source[id].subdev = NULL;

	/* Not all indexes will be defined, this is OK */
	ep = of_graph_get_endpoint_by_regs(grp->dev->of_node, RVIN_PORT_CSI,
					   id);
	if (!ep)
		return 0;

	/* Get bridge */
	bridge = of_graph_get_remote_port_parent(ep);
	of_node_put(ep);
	if (!bridge) {
		grp_err(grp, "No bridge found for endpoint '%s'\n",
			of_node_full_name(ep));
		return -EINVAL;
	}

	/* Not all bridges are available, this is OK */
	if (!of_device_is_available(bridge)) {
		of_node_put(bridge);
		return 0;
	}

	/* Get source */
	for_each_endpoint_of_node(bridge, ep) {
		np = of_graph_get_remote_port_parent(ep);
		if (!np) {
			grp_err(grp, "No remote found for endpoint '%s'\n",
				of_node_full_name(ep));
			of_node_put(bridge);
			of_node_put(ep);
			return -EINVAL;
		}

		if (grp->dev->of_node == np) {
			/* Ignore loop-back */
		} else if (!of_device_is_available(np)) {
			/* Not all sources are available, this is OK */
		} else if (source) {
			grp_err(grp, "Multiple source for %s, will use first\n",
				of_node_full_name(bridge));
		} else {
			/* Get endpoint information */
			rp = of_graph_get_remote_port(ep);
			of_graph_parse_endpoint(rp, &endpoint);
			of_node_put(rp);

			ret = rvin_parse_v4l2_endpoint(grp, ep, &mbus_cfg);
			if (ret) {
				of_node_put(bridge);
				of_node_put(ep);
				of_node_put(np);
				return ret;
			}
			source = np;
		}

		of_node_put(np);
	}
	of_node_put(bridge);

	grp->source[id].mbus_cfg = mbus_cfg;
	grp->source[id].source_idx = endpoint.id;

	grp->bridge[id].asd.match.of.node = bridge;
	grp->bridge[id].asd.match_type = V4L2_ASYNC_MATCH_OF;
	grp->source[id].asd.match.of.node = source;
	grp->source[id].asd.match_type = V4L2_ASYNC_MATCH_OF;

	grp_dbg(grp, "csi%d: bridge: %s source: %s pad: %d", id,
		of_node_full_name(grp->bridge[id].asd.match.of.node),
		of_node_full_name(grp->source[id].asd.match.of.node),
		grp->source[id].source_idx);

	return ret;
}

static int rvin_get_remote_channels(struct rvin_group *grp, int id)
{
	struct device_node *ep, *remote;
	int ret = 0;

	grp->chan[id].asd.match.of.node = NULL;
	grp->chan[id].subdev = NULL;

	/* Not all indexes will be defined, this is OK */
	ep = of_graph_get_endpoint_by_regs(grp->dev->of_node, RVIN_PORT_REMOTE,
					   id);
	if (!ep)
		return 0;

	/* Find remote subdevice */
	remote = of_graph_get_remote_port_parent(ep);
	if (!remote) {
		grp_err(grp, "No remote parent for endpoint '%s'\n",
			of_node_full_name(ep));
		ret = -EINVAL;
		goto out_ep;
	}

	/* Not all remotes will be available, this is OK */
	if (!of_device_is_available(remote)) {
		ret = 0;
		goto out_remote;
	}

	grp->chan[id].asd.match.of.node = remote;
	grp->chan[id].asd.match_type = V4L2_ASYNC_MATCH_OF;

	grp_dbg(grp, "chan%d: node: '%s'\n", id,
		of_node_full_name(grp->chan[id].asd.match.of.node));

out_remote:
	of_node_put(remote);
out_ep:
	of_node_put(ep);

	return ret;
}

static int __node_add(struct v4l2_async_subdev **subdev, int num,
		       struct rvin_graph_entity *entity)
{
	int i;

	if (!entity->asd.match.of.node)
		return 0;

	for (i = 0; i < num; i++) {

		if (!subdev[i]) {
			subdev[i] = &entity->asd;
			return 1;
		}

		if (subdev[i]->match.of.node == entity->asd.match.of.node)
			break;
	}

	return 0;
}

static int rvin_graph_init(struct rvin_group *grp)
{
	struct v4l2_async_subdev **subdevs = NULL;
	int i, ret, found = 0, matched = 0;

	/* Try to get CSI2 sources */
	for (i = 0; i < RVIN_CSI_MAX; i++) {
		ret = rvin_get_csi_source(grp, i);
		if (ret)
			return ret;
		if (grp->bridge[i].asd.match.of.node &&
		    grp->source[i].asd.match.of.node)
			found += 2;
	}

	/* Try to get slave channels */
	for (i = 0; i < RVIN_CHAN_MAX; i++) {
		ret = rvin_get_remote_channels(grp, i);
		if (ret)
			return ret;
		if (grp->chan[i].asd.match.of.node)
			found++;
	}

	if (!found)
		return -ENODEV;

	/* Register the subdevices notifier. */
	subdevs = devm_kzalloc(grp->dev, sizeof(*subdevs) * found, GFP_KERNEL);
	if (subdevs == NULL)
		return -ENOMEM;

	for (i = 0; i < RVIN_CSI_MAX; i++) {
		matched += __node_add(subdevs, found, &grp->bridge[i]);
		matched += __node_add(subdevs, found, &grp->source[i]);
	}
	for (i = 0; i < RVIN_CHAN_MAX; i++)
		matched += __node_add(subdevs, found, &grp->chan[i]);

	grp_dbg(grp, "found %d group subdevice(s) %d are unique\n", found,
		matched);

	grp->notifier.num_subdevs = matched;
	grp->notifier.subdevs = subdevs;
	grp->notifier.bound = rvin_graph_notify_bound;
	grp->notifier.unbind = rvin_graph_notify_unbind;
	grp->notifier.complete = rvin_graph_notify_complete;

	ret = v4l2_async_notifier_register(grp->v4l2_dev, &grp->notifier);
	if (ret < 0) {
		grp_err(grp, "Notifier registration failed\n");
		return ret;
	}

	return 0;
}

/* -----------------------------------------------------------------------------
 * Base
 */

struct rvin_group_api *rvin_group_probe(struct device *dev,
					struct v4l2_device *v4l2_dev)
{
	struct rvin_group *grp;
	int i, ret;

	grp = devm_kzalloc(dev, sizeof(*grp), GFP_KERNEL);
	if (!grp)
		return NULL;

	grp->dev = dev;
	grp->v4l2_dev = v4l2_dev;
	grp->chsel1 = 0;
	grp->chsel2 = 0;

	for (i = 0; i < RVIN_CSI_MAX; i++) {
		grp->power[i] = 0;
		grp->stream[i] = 0;
	}

	for (i = 0; i < RVIN_CHAN_MAX; i++)
		grp->users[i] = 0;

	ret = rvin_graph_init(grp);
	if (ret) {
		devm_kfree(dev, grp);
		return NULL;
	}

	mutex_init(&grp->lock);

	grp->api.ops = &rvin_group_ops;
	grp->api.input_ops = &rvin_input_ops;

	grp->api.get = rvin_group_get;
	grp->api.put = rvin_group_put;
	grp->api.set_input = rvin_group_set_input;
	grp->api.get_code = rvin_group_get_code;
	grp->api.get_mbus_cfg = rvin_group_get_mbus_cfg;
	grp->api.ctrl_add_handler = rvin_group_ctrl_add_handler;
	grp->api.alloc_pad_config = rvin_group_alloc_pad_config;

	return &grp->api;
}

int rvin_group_remove(struct rvin_group_api *api)
{
	struct rvin_group *grp = container_of(api, struct rvin_group, api);

	v4l2_async_notifier_unregister(&grp->notifier);

	mutex_destroy(&grp->lock);

	return 0;
}
