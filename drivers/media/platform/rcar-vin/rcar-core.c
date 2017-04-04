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

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>

#include <media/v4l2-of.h>

#include "rcar-vin.h"

/* -----------------------------------------------------------------------------
 * Media Controller link notification
 */

static unsigned int rvin_group_csi_pad_to_chan(unsigned int pad)
{
	/*
	 * The CSI2 driver is rcar-csi2 and we know it's pad layout are
	 * 0: Source 1-4: Sinks so if we remove one from the pad we
	 * get the rcar-vin internal CSI2 channel number
	 */
	return pad - 1;
}

/* group lock should be held when calling this function */
static int rvin_group_entity_to_vin_num(struct rvin_group *group,
					struct media_entity *entity)
{
	struct video_device *vdev;
	int i;

	if (!is_media_entity_v4l2_video_device(entity))
		return -ENODEV;

	vdev = media_entity_to_video_device(entity);

	for (i = 0; i < RCAR_VIN_NUM; i++) {
		if (!group->vin[i])
			continue;

		if (group->vin[i]->vdev == vdev)
			return i;
	}

	return -ENODEV;
}

/* group lock should be held when calling this function */
static int rvin_group_entity_to_csi_num(struct rvin_group *group,
					struct media_entity *entity)
{
	struct v4l2_subdev *sd;
	int i;

	if (!is_media_entity_v4l2_subdev(entity))
		return -ENODEV;

	sd = media_entity_to_v4l2_subdev(entity);

	for (i = 0; i < RVIN_CSI_MAX; i++)
		if (group->bridge[i].subdev == sd)
			return i;

	return -ENODEV;
}

/* group lock should be held when calling this function */
static void __rvin_group_build_link_list(struct rvin_group *group,
					 struct rvin_group_chsel *map,
					 int start, int len)
{
	struct media_pad *vin_pad, *remote_pad;
	unsigned int n;

	for (n = 0; n < len; n++) {
		map[n].csi = -1;
		map[n].chan = -1;

		if (!group->vin[start + n])
			continue;

		vin_pad = &group->vin[start + n]->vdev->entity.pads[0];

		remote_pad = media_entity_remote_pad(vin_pad);
		if (!remote_pad)
			continue;

		map[n].csi =
			rvin_group_entity_to_csi_num(group, remote_pad->entity);
		map[n].chan = rvin_group_csi_pad_to_chan(remote_pad->index);
	}
}

/* group lock should be held when calling this function */
static int __rvin_group_try_get_chsel(struct rvin_group *group,
				      struct rvin_group_chsel *map,
				      int start, int len)
{
	const struct rvin_group_chsel *sel;
	unsigned int i, n;
	int chsel;

	for (i = 0; i < group->vin[start]->info->num_chsels; i++) {
		chsel = i;
		for (n = 0; n < len; n++) {

			/* If the link is not active it's OK */
			if (map[n].csi == -1)
				continue;

			/* Check if chsel match requested link */
			sel = &group->vin[start]->info->chsels[start + n][i];
			if (map[n].csi != sel->csi ||
			    map[n].chan != sel->chan) {
				chsel = -1;
				break;
			}
		}

		/* A chsel which satisfy the links have been found */
		if (chsel != -1)
			return chsel;
	}

	/* No chsel can satisfy the requested links */
	return -1;
}

/* group lock should be held when calling this function */
static bool rvin_group_in_use(struct rvin_group *group)
{
	struct media_entity *entity;

	media_device_for_each_entity(entity, &group->mdev)
		if (entity->use_count)
			return true;

	return false;
}

static int rvin_group_link_notify(struct media_link *link, u32 flags,
				  unsigned int notification)
{
	struct rvin_group *group = container_of(link->graph_obj.mdev,
						struct rvin_group, mdev);
	struct rvin_group_chsel chsel_map[4];
	int vin_num, vin_master, csi_num, csi_chan;
	unsigned int chsel;

	mutex_lock(&group->lock);

	vin_num = rvin_group_entity_to_vin_num(group, link->sink->entity);
	csi_num = rvin_group_entity_to_csi_num(group, link->source->entity);
	csi_chan = rvin_group_csi_pad_to_chan(link->source->index);

	/*
	 * Figure out which VIN node is the subgroup master.
	 *
	 * VIN0-3 are controlled by VIN0
	 * VIN4-7 are controlled by VIN4
	 */
	vin_master = vin_num < 4 ? 0 : 4;

	/* If not all devices exists something is horribly wrong */
	if (vin_num < 0 || csi_num < 0 || !group->vin[vin_master])
		goto error;

	/* Special checking only needed for links which are to be enabled */
	if (notification != MEDIA_DEV_NOTIFY_PRE_LINK_CH ||
	    !(flags & MEDIA_LNK_FL_ENABLED))
		goto out;

	/* If any link in the group are in use, no new link can be enabled */
	if (rvin_group_in_use(group))
		goto error;

	/* If the VIN already have a active link it's busy */
	if (media_entity_remote_pad(&link->sink->entity->pads[0]))
		goto error;

	/* Build list of active links */
	__rvin_group_build_link_list(group, chsel_map, vin_master, 4);

	/* Add the new proposed link */
	chsel_map[vin_num - vin_master].csi = csi_num;
	chsel_map[vin_num - vin_master].chan = csi_chan;

	/* See if there is a chsel value which match our link selection */
	chsel = __rvin_group_try_get_chsel(group, chsel_map, vin_master, 4);

	/* No chsel can provide the request links */
	if (chsel == -1)
		goto error;

	/* Update chsel value at group master */
	if (rvin_set_chsel(group->vin[vin_master], chsel))
		goto error;

out:
	mutex_unlock(&group->lock);

	return v4l2_pipeline_link_notify(link, flags, notification);
error:
	mutex_unlock(&group->lock);

	return -EMLINK;
}

static const struct media_device_ops rvin_media_ops = {
	.link_notify = rvin_group_link_notify,
};

/* -----------------------------------------------------------------------------
 * Gen3 CSI2 Group Allocator
 */

static DEFINE_MUTEX(rvin_group_lock);
static struct rvin_group *rvin_group_data;

static void rvin_group_release(struct kref *kref)
{
	struct rvin_group *group =
		container_of(kref, struct rvin_group, refcount);

	mutex_lock(&rvin_group_lock);

	media_device_unregister(&group->mdev);
	media_device_cleanup(&group->mdev);

	rvin_group_data = NULL;

	mutex_unlock(&rvin_group_lock);

	kfree(group);
}

static struct rvin_group *__rvin_group_allocate(struct rvin_dev *vin)
{
	struct rvin_group *group;

	if (rvin_group_data) {
		group = rvin_group_data;
		kref_get(&group->refcount);
		vin_dbg(vin, "%s: get group=%p\n", __func__, group);
		return group;
	}

	group = kzalloc(sizeof(*group), GFP_KERNEL);
	if (!group)
		return NULL;

	kref_init(&group->refcount);
	rvin_group_data = group;

	vin_dbg(vin, "%s: alloc group=%p\n", __func__, group);
	return group;
}

static struct rvin_group *rvin_group_allocate(struct rvin_dev *vin)
{
	struct rvin_group *group;
	struct media_device *mdev;
	int ret;

	mutex_lock(&rvin_group_lock);

	group = __rvin_group_allocate(vin);
	if (!group) {
		mutex_unlock(&rvin_group_lock);
		return ERR_PTR(-ENOMEM);
	}

	/* Init group data if its not already initialized */
	mdev = &group->mdev;
	if (!mdev->dev) {
		mutex_init(&group->lock);
		mdev->dev = vin->dev;

		strlcpy(mdev->driver_name, "Renesas VIN",
			sizeof(mdev->driver_name));
		strlcpy(mdev->model, vin->dev->of_node->name,
			sizeof(mdev->model));
		strlcpy(mdev->bus_info, of_node_full_name(vin->dev->of_node),
			sizeof(mdev->bus_info));
		mdev->driver_version = LINUX_VERSION_CODE;
		media_device_init(mdev);

		mdev->ops = &rvin_media_ops;

		ret = media_device_register(mdev);
		if (ret) {
			vin_err(vin, "Failed to register media device\n");
			kref_put(&group->refcount, rvin_group_release);
			mutex_unlock(&rvin_group_lock);
			return ERR_PTR(ret);
		}
	}

	vin->v4l2_dev.mdev = mdev;

	mutex_unlock(&rvin_group_lock);

	return group;
}

static void rvin_group_delete(struct rvin_dev *vin)
{
	vin_dbg(vin, "%s: group=%p\n", __func__, &vin->group);
	kref_put(&vin->group->refcount, rvin_group_release);
}

/* -----------------------------------------------------------------------------
 * Async notifier
 */

#define notifier_to_vin(n) container_of(n, struct rvin_dev, notifier)

static int rvin_find_pad(struct v4l2_subdev *sd, int direction)
{
	unsigned int pad;

	if (sd->entity.num_pads <= 1)
		return 0;

	for (pad = 0; pad < sd->entity.num_pads; pad++)
		if (sd->entity.pads[pad].flags & direction)
			return pad;

	return -EINVAL;
}

static int rvin_parse_v4l2(struct rvin_dev *vin,
			   struct device_node *ep,
			   struct v4l2_mbus_config *mbus_cfg)
{
	struct v4l2_of_endpoint v4l2_ep;
	int ret;

	ret = v4l2_of_parse_endpoint(ep, &v4l2_ep);
	if (ret) {
		vin_err(vin, "Could not parse v4l2 endpoint\n");
		return -EINVAL;
	}

	mbus_cfg->type = v4l2_ep.bus_type;

	if (vin->info->chip == RCAR_GEN3) {
		switch (mbus_cfg->type) {
		case V4L2_MBUS_CSI2:
			vin_dbg(vin, "Found CSI-2 media bus\n");
			mbus_cfg->flags = 0;
			return 0;
		default:
			break;
		}
	} else {
		switch (mbus_cfg->type) {
		case V4L2_MBUS_PARALLEL:
			vin_dbg(vin, "Found PARALLEL media bus\n");
			mbus_cfg->flags = v4l2_ep.bus.parallel.flags;
			return 0;
		case V4L2_MBUS_BT656:
			vin_dbg(vin, "Found BT656 media bus\n");
			mbus_cfg->flags = 0;
			return 0;
		default:
			break;
		}
	}

	vin_err(vin, "Unknown media bus type\n");
	return -EINVAL;
}

/* -----------------------------------------------------------------------------
 * Digital async notifier
 */

static bool rvin_mbus_supported(struct rvin_dev *vin)
{
	struct v4l2_subdev *sd = vin->digital.subdev;
	struct v4l2_subdev_mbus_code_enum code = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};

	code.index = 0;
	code.pad = vin->digital.source_pad;
	while (!v4l2_subdev_call(sd, pad, enum_mbus_code, NULL, &code)) {
		code.index++;
		switch (code.code) {
		case MEDIA_BUS_FMT_YUYV8_1X16:
		case MEDIA_BUS_FMT_UYVY8_2X8:
		case MEDIA_BUS_FMT_UYVY10_2X10:
		case MEDIA_BUS_FMT_RGB888_1X24:
			vin->code = code.code;
			return true;
		default:
			break;
		}
	}

	return false;
}

static int rvin_digital_notify_complete(struct v4l2_async_notifier *notifier)
{
	struct rvin_dev *vin = notifier_to_vin(notifier);
	int ret;

	/* Verify subdevices mbus format */
	if (!rvin_mbus_supported(vin)) {
		vin_err(vin, "Unsupported media bus format for %s\n",
			vin->digital.subdev->name);
		return -EINVAL;
	}

	vin_dbg(vin, "Found media bus format for %s: %d\n",
		vin->digital.subdev->name, vin->code);

	ret = v4l2_device_register_subdev_nodes(&vin->v4l2_dev);
	if (ret < 0) {
		vin_err(vin, "Failed to register subdev nodes\n");
		return ret;
	}

	return rvin_v4l2_probe(vin);
}

static void rvin_digital_notify_unbind(struct v4l2_async_notifier *notifier,
				       struct v4l2_subdev *subdev,
				       struct v4l2_async_subdev *asd)
{
	struct rvin_dev *vin = notifier_to_vin(notifier);

	if (vin->digital.subdev == subdev) {
		vin_dbg(vin, "unbind digital subdev %s\n", subdev->name);
		rvin_v4l2_remove(vin);
		vin->digital.subdev = NULL;
		return;
	}

	vin_err(vin, "no entity for subdev %s to unbind\n", subdev->name);
}

static int rvin_digital_notify_bound(struct v4l2_async_notifier *notifier,
				     struct v4l2_subdev *subdev,
				     struct v4l2_async_subdev *asd)
{
	struct rvin_dev *vin = notifier_to_vin(notifier);
	int ret;

	v4l2_set_subdev_hostdata(subdev, vin);

	if (vin->digital.asd.match.of.node == subdev->dev->of_node) {
		/* Find surce and sink pad of remote subdevice */

		ret = rvin_find_pad(subdev, MEDIA_PAD_FL_SOURCE);
		if (ret < 0)
			return ret;
		vin->digital.source_pad = ret;

		ret = rvin_find_pad(subdev, MEDIA_PAD_FL_SINK);
		if (ret < 0)
			return ret;
		vin->digital.sink_pad = ret;

		vin->digital.subdev = subdev;

		vin_dbg(vin, "bound subdev %s source pad: %d sink pad: %d\n",
			subdev->name, vin->digital.source_pad,
			vin->digital.sink_pad);
		return 0;
	}

	vin_err(vin, "no entity for subdev %s to bind\n", subdev->name);
	return -EINVAL;
}

static int rvin_digital_graph_parse(struct rvin_dev *vin)
{
	struct device_node *ep, *np;
	int ret;

	vin->digital.asd.match.of.node = NULL;
	vin->digital.subdev = NULL;

	/*
	 * Port 0 id 0 is local digital input, try to get it.
	 * Not all instances can or will have this, that is OK
	 */
	ep = of_graph_get_endpoint_by_regs(vin->dev->of_node, 0, 0);
	if (!ep)
		return 0;

	np = of_graph_get_remote_port_parent(ep);
	if (!np) {
		vin_err(vin, "No remote parent for digital input\n");
		of_node_put(ep);
		return -EINVAL;
	}
	of_node_put(np);

	ret = rvin_parse_v4l2(vin, ep, &vin->mbus_cfg);
	of_node_put(ep);
	if (ret)
		return ret;

	vin->digital.asd.match.of.node = np;
	vin->digital.asd.match_type = V4L2_ASYNC_MATCH_OF;

	return 0;
}

static int rvin_digital_graph_init(struct rvin_dev *vin)
{
	struct v4l2_async_subdev **subdevs = NULL;
	int ret;

	ret = rvin_digital_graph_parse(vin);
	if (ret)
		return ret;

	if (!vin->digital.asd.match.of.node) {
		vin_dbg(vin, "No digital subdevice found\n");
		return -ENODEV;
	}

	/* Register the subdevices notifier. */
	subdevs = devm_kzalloc(vin->dev, sizeof(*subdevs), GFP_KERNEL);
	if (subdevs == NULL)
		return -ENOMEM;

	subdevs[0] = &vin->digital.asd;

	vin_dbg(vin, "Found digital subdevice %s\n",
		of_node_full_name(subdevs[0]->match.of.node));

	vin->notifier.num_subdevs = 1;
	vin->notifier.subdevs = subdevs;
	vin->notifier.bound = rvin_digital_notify_bound;
	vin->notifier.unbind = rvin_digital_notify_unbind;
	vin->notifier.complete = rvin_digital_notify_complete;

	ret = v4l2_async_notifier_register(&vin->v4l2_dev, &vin->notifier);
	if (ret < 0) {
		vin_err(vin, "Notifier registration failed\n");
		return ret;
	}

	return 0;
}

/* -----------------------------------------------------------------------------
 * Group async notifier
 */

/* group lock should be held when calling this function */
static void rvin_group_update_pads(struct rvin_graph_entity *entity)
{
	struct media_entity *ent = &entity->subdev->entity;
	unsigned int i;

	/* Make sure source pad idx are sane */
	if (entity->source_pad >= ent->num_pads ||
	    ent->pads[entity->source_pad].flags != MEDIA_PAD_FL_SOURCE) {
		entity->source_pad =
			rvin_find_pad(entity->subdev, MEDIA_PAD_FL_SOURCE);
	}

	/* Try to find sink for source, fall back 0 which always is sink */
	entity->sink_pad = 0;
	for (i = 0; i < ent->num_pads; ++i) {
		struct media_pad *sink = &ent->pads[i];

		if (!(sink->flags & MEDIA_PAD_FL_SINK))
			continue;

		if (sink->index == entity->source_pad)
			continue;

		if (media_entity_has_route(ent, sink->index,
					   entity->source_pad))
			entity->sink_pad = sink->index;
	}
}

/* group lock should be held when calling this function */
static int rvin_group_add_link(struct rvin_dev *vin,
			       struct media_entity *source,
			       unsigned int source_idx,
			       struct media_entity *sink,
			       unsigned int sink_idx,
			       u32 flags)
{
	struct media_pad *source_pad, *sink_pad;
	int ret = 0;

	source_pad = &source->pads[source_idx];
	sink_pad = &sink->pads[sink_idx];

	if (!media_entity_find_link(source_pad, sink_pad))
		ret = media_create_pad_link(source, source_idx,
					    sink, sink_idx, flags);

	if (ret)
		vin_err(vin, "Error adding link from %s to %s\n",
			source->name, sink->name);

	return ret;
}

static int rvin_group_update_links(struct rvin_dev *vin)
{
	struct media_entity *source, *sink;
	struct rvin_dev *master;
	unsigned int i, n, idx, chsel, csi;
	u32 flags;
	int ret;

	mutex_lock(&vin->group->lock);

	/* Update Source -> Bridge */
	for (i = 0; i < RVIN_CSI_MAX; i++) {
		if (!vin->group->source[i].subdev)
			continue;

		if (!vin->group->bridge[i].subdev)
			continue;

		source = &vin->group->source[i].subdev->entity;
		sink = &vin->group->bridge[i].subdev->entity;
		idx = vin->group->source[i].source_pad;
		flags = MEDIA_LNK_FL_ENABLED | MEDIA_LNK_FL_IMMUTABLE;

		ret = rvin_group_add_link(vin, source, idx, sink, 0, flags);
		if (ret)
			goto out;
	}

	/* Update Bridge -> VIN */
	for (n = 0; n < RCAR_VIN_NUM; n++) {

		/* Check that VIN is part of the group */
		if (!vin->group->vin[n])
			continue;

		/* Check that subgroup master is part of the group */
		master = vin->group->vin[n < 4 ? 0 : 4];
		if (!master)
			continue;

		chsel = rvin_get_chsel(master);

		for (i = 0; i < vin->info->num_chsels; i++) {
			csi = vin->info->chsels[n][i].csi;

			/* If the CSI is out of bounds it's a no operate skip */
			if (csi >= RVIN_CSI_MAX)
				continue;

			/* Check that bridge are part of the group */
			if (!vin->group->bridge[csi].subdev)
				continue;

			source = &vin->group->bridge[csi].subdev->entity;
			sink = &vin->group->vin[n]->vdev->entity;
			idx = vin->info->chsels[n][i].chan + 1;
			flags = i == chsel ? MEDIA_LNK_FL_ENABLED : 0;

			ret = rvin_group_add_link(vin, source, idx, sink, 0,
						  flags);
			if (ret)
				goto out;
		}
	}
out:
	mutex_unlock(&vin->group->lock);

	return ret;
}

static int rvin_group_notify_complete(struct v4l2_async_notifier *notifier)
{
	struct rvin_dev *vin = notifier_to_vin(notifier);
	unsigned int i;
	int ret;

	mutex_lock(&vin->group->lock);
	for (i = 0; i < RVIN_CSI_MAX; i++) {
		if (!vin->group->source[i].subdev)
			continue;

		rvin_group_update_pads(&vin->group->source[i]);
	}
	mutex_unlock(&vin->group->lock);

	ret = v4l2_device_register_subdev_nodes(&vin->v4l2_dev);
	if (ret) {
		vin_err(vin, "Failed to register subdev nodes\n");
		return ret;
	}

	return rvin_group_update_links(vin);
}

static void rvin_group_notify_unbind(struct v4l2_async_notifier *notifier,
				     struct v4l2_subdev *subdev,
				     struct v4l2_async_subdev *asd)
{
	struct rvin_dev *vin = notifier_to_vin(notifier);
	unsigned int i;

	mutex_lock(&vin->group->lock);
	for (i = 0; i < RVIN_CSI_MAX; i++) {
		struct device_node *del = subdev->dev->of_node;

		if (vin->group->bridge[i].asd.match.of.node == del) {
			vin_dbg(vin, "Unbind bridge %s\n", subdev->name);
			vin->group->bridge[i].subdev = NULL;
			mutex_unlock(&vin->group->lock);
			return;
		}

		if (vin->group->source[i].asd.match.of.node == del) {
			vin_dbg(vin, "Unbind source %s\n", subdev->name);
			vin->group->source[i].subdev = NULL;
			mutex_unlock(&vin->group->lock);
			return;
		}
	}
	mutex_unlock(&vin->group->lock);

	vin_err(vin, "No entity for subdev %s to unbind\n", subdev->name);
}

static int rvin_group_notify_bound(struct v4l2_async_notifier *notifier,
				   struct v4l2_subdev *subdev,
				   struct v4l2_async_subdev *asd)
{
	struct rvin_dev *vin = notifier_to_vin(notifier);
	unsigned int i;

	v4l2_set_subdev_hostdata(subdev, vin);

	mutex_lock(&vin->group->lock);
	for (i = 0; i < RVIN_CSI_MAX; i++) {
		struct device_node *new = subdev->dev->of_node;

		if (vin->group->bridge[i].asd.match.of.node == new) {
			vin_dbg(vin, "Bound bridge %s\n", subdev->name);
			vin->group->bridge[i].subdev = subdev;
			mutex_unlock(&vin->group->lock);
			return 0;
		}

		if (vin->group->source[i].asd.match.of.node == new) {
			vin_dbg(vin, "Bound source %s\n", subdev->name);
			vin->group->source[i].subdev = subdev;
			mutex_unlock(&vin->group->lock);
			return 0;
		}
	}
	mutex_unlock(&vin->group->lock);

	vin_err(vin, "No entity for subdev %s to bind\n", subdev->name);
	return -EINVAL;
}

static struct device_node *rvin_group_get_bridge(struct rvin_dev *vin,
						 struct device_node *node)
{
	struct device_node *bridge;

	bridge = of_graph_get_remote_port_parent(node);
	if (!bridge) {
		vin_err(vin, "No bridge found %s\n", of_node_full_name(node));
		return ERR_PTR(-EINVAL);
	}

	/* Not all bridges are available, this is OK */
	if (!of_device_is_available(bridge)) {
		vin_dbg(vin, "Bridge %s not available\n",
			of_node_full_name(bridge));
		of_node_put(bridge);
		return NULL;
	}

	return bridge;
}

static struct device_node *
rvin_group_get_source(struct rvin_dev *vin,
		      struct device_node *bridge,
		      unsigned int *remote_pad)
{
	struct device_node *source, *ep, *rp;
	struct v4l2_mbus_config mbus_cfg;
	struct of_endpoint endpoint;
	int ret;

	ep = of_graph_get_endpoint_by_regs(bridge, 0, 0);
	if (!ep) {
		vin_dbg(vin, "Endpoint %s not connected to source\n",
			of_node_full_name(ep));
		return ERR_PTR(-EINVAL);
	}

	/* Check that source uses a supported media bus */
	ret = rvin_parse_v4l2(vin, ep, &mbus_cfg);
	if (ret) {
		of_node_put(ep);
		return ERR_PTR(ret);
	}

	rp = of_graph_get_remote_port(ep);
	of_graph_parse_endpoint(rp, &endpoint);
	of_node_put(rp);
	*remote_pad = endpoint.id;

	source = of_graph_get_remote_port_parent(ep);
	of_node_put(ep);
	if (!source) {
		vin_err(vin, "No source found for endpoint '%s'\n",
			of_node_full_name(ep));
		return ERR_PTR(-EINVAL);
	}

	return source;
}

/* group lock should be held when calling this function */
static int rvin_group_graph_parse(struct rvin_dev *vin, unsigned long *bitmap)
{
	struct device_node *ep, *bridge, *source;
	unsigned int i, remote_pad = 0;
	u32 val;
	int ret;

	*bitmap = 0;

	/* Figure out which VIN we are */
	ret = of_property_read_u32(vin->dev->of_node, "renesas,id", &val);
	if (ret) {
		vin_err(vin, "No renesas,id property found\n");
		return ret;
	}

	if (val >= RCAR_VIN_NUM) {
		vin_err(vin, "Invalid renesas,id '%u'\n", val);
		return -EINVAL;
	}

	if (vin->group->vin[val] != NULL) {
		vin_err(vin, "VIN number %d already occupied\n", val);
		return -EINVAL;
	}

	vin_dbg(vin, "I'm VIN number %u", val);
	vin->group->vin[val] = vin;

	/* Parse all CSI-2 nodes */
	for (i = 0; i < RVIN_CSI_MAX; i++) {

		/* Check if instance is connected to the bridge */
		ep = of_graph_get_endpoint_by_regs(vin->dev->of_node, 1, i);
		if (!ep) {
			vin_dbg(vin, "Bridge: %d not connected\n", i);
			continue;
		}

		if (vin->group->bridge[i].asd.match.of.node) {
			of_node_put(ep);
			vin_dbg(vin, "Bridge: %d handled by other device\n", i);
			continue;
		}

		bridge = rvin_group_get_bridge(vin, ep);
		of_node_put(ep);
		if (IS_ERR(bridge))
			return PTR_ERR(bridge);
		if (bridge == NULL)
			continue;

		source = rvin_group_get_source(vin, bridge, &remote_pad);
		of_node_put(bridge);
		if (IS_ERR(source))
			return PTR_ERR(source);
		if (source == NULL)
			continue;

		of_node_put(source);

		vin->group->bridge[i].asd.match.of.node = bridge;
		vin->group->bridge[i].asd.match_type = V4L2_ASYNC_MATCH_OF;
		vin->group->source[i].asd.match.of.node = source;
		vin->group->source[i].asd.match_type = V4L2_ASYNC_MATCH_OF;
		vin->group->source[i].source_pad = remote_pad;

		*bitmap |= BIT(i);

		vin_dbg(vin, "Handle bridge %s and source %s pad %d\n",
			of_node_full_name(bridge), of_node_full_name(source),
			remote_pad);
	}

	/* All our sources are CSI-2 */
	vin->mbus_cfg.type = V4L2_MBUS_CSI2;
	vin->mbus_cfg.flags = 0;

	return 0;
}

/* group lock should be held when calling this function */
static void rvin_group_graph_revert(struct rvin_dev *vin, unsigned long bitmap)
{
	int bit;

	for_each_set_bit(bit, &bitmap, RVIN_CSI_MAX) {
		vin_dbg(vin, "Reverting graph for %s\n",
			of_node_full_name(vin->dev->of_node));
		vin->group->bridge[bit].asd.match.of.node = NULL;
		vin->group->bridge[bit].asd.match_type = 0;
		vin->group->source[bit].asd.match.of.node = NULL;
		vin->group->source[bit].asd.match_type = 0;
	}
}

static int rvin_group_graph_init(struct rvin_dev *vin)
{
	struct v4l2_async_subdev **subdevs = NULL;
	unsigned long bitmap;
	int i, bit, count, ret;

	mutex_lock(&vin->group->lock);

	ret = rvin_group_graph_parse(vin, &bitmap);
	if (ret) {
		rvin_group_graph_revert(vin, bitmap);
		mutex_unlock(&vin->group->lock);
		return ret;
	}

	/* Check if instance need to handle subdevices on behalf of the group */
	count = hweight_long(bitmap) * 2;
	if (!count) {
		mutex_unlock(&vin->group->lock);
		return 0;
	}

	subdevs = devm_kzalloc(vin->dev, sizeof(*subdevs) * count, GFP_KERNEL);
	if (subdevs == NULL) {
		rvin_group_graph_revert(vin, bitmap);
		mutex_unlock(&vin->group->lock);
		return -ENOMEM;
	}

	i = 0;
	for_each_set_bit(bit, &bitmap, RVIN_CSI_MAX) {
		subdevs[i++] = &vin->group->bridge[bit].asd;
		subdevs[i++] = &vin->group->source[bit].asd;
	}

	vin_dbg(vin, "Claimed %d subdevices for group\n", count);

	vin->notifier.num_subdevs = count;
	vin->notifier.subdevs = subdevs;
	vin->notifier.bound = rvin_group_notify_bound;
	vin->notifier.unbind = rvin_group_notify_unbind;
	vin->notifier.complete = rvin_group_notify_complete;

	mutex_unlock(&vin->group->lock);

	ret = v4l2_async_notifier_register(&vin->v4l2_dev, &vin->notifier);
	if (ret < 0) {
		vin_err(vin, "Notifier registration failed\n");
		return ret;
	}

	return 0;
}

static int rvin_group_init(struct rvin_dev *vin)
{
	int ret;

	vin->group = rvin_group_allocate(vin);
	if (IS_ERR(vin->group))
		return PTR_ERR(vin->group);

	ret = rvin_v4l2_mc_probe(vin);
	if (ret)
		goto error_group;

	vin->pad.flags = MEDIA_PAD_FL_SINK;
	ret = media_entity_pads_init(&vin->vdev->entity, 1, &vin->pad);
	if (ret)
		goto error_v4l2;

	ret = rvin_group_graph_init(vin);
	if (ret)
		goto error_v4l2;

	ret = rvin_group_update_links(vin);
	if (ret)
		goto error_async;

	return 0;
error_async:
	v4l2_async_notifier_unregister(&vin->notifier);
error_v4l2:
	rvin_v4l2_mc_remove(vin);
error_group:
	rvin_group_delete(vin);

	return ret;
}

/* -----------------------------------------------------------------------------
 * Platform Device Driver
 */

static const struct rvin_info rcar_info_h1 = {
	.chip = RCAR_H1,
	.use_mc = false,
	.max_width = 2048,
	.max_height = 2048,
};

static const struct rvin_info rcar_info_m1 = {
	.chip = RCAR_M1,
	.use_mc = false,
	.max_width = 2048,
	.max_height = 2048,
};

static const struct rvin_info rcar_info_r8a7795 = {
	.chip = RCAR_GEN3,
	.use_mc = true,
	.max_width = 4096,
	.max_height = 4096,

	.num_chsels = 6,
	.chsels = {
		{
			{ .csi = RVIN_CSI40, .chan = 0 },
			{ .csi = RVIN_CSI20, .chan = 0 },
			{ .csi = RVIN_CSI21, .chan = 0 },
			{ .csi = RVIN_CSI40, .chan = 0 },
			{ .csi = RVIN_CSI20, .chan = 0 },
			{ .csi = RVIN_CSI21, .chan = 0 },
		}, {
			{ .csi = RVIN_CSI20, .chan = 0 },
			{ .csi = RVIN_CSI21, .chan = 0 },
			{ .csi = RVIN_CSI40, .chan = 0 },
			{ .csi = RVIN_CSI40, .chan = 1 },
			{ .csi = RVIN_CSI20, .chan = 1 },
			{ .csi = RVIN_CSI21, .chan = 1 },
		}, {
			{ .csi = RVIN_CSI21, .chan = 0 },
			{ .csi = RVIN_CSI40, .chan = 0 },
			{ .csi = RVIN_CSI20, .chan = 0 },
			{ .csi = RVIN_CSI40, .chan = 2 },
			{ .csi = RVIN_CSI20, .chan = 2 },
			{ .csi = RVIN_CSI21, .chan = 2 },
		}, {
			{ .csi = RVIN_CSI40, .chan = 1 },
			{ .csi = RVIN_CSI20, .chan = 1 },
			{ .csi = RVIN_CSI21, .chan = 1 },
			{ .csi = RVIN_CSI40, .chan = 3 },
			{ .csi = RVIN_CSI20, .chan = 3 },
			{ .csi = RVIN_CSI21, .chan = 3 },
		}, {
			{ .csi = RVIN_CSI41, .chan = 0 },
			{ .csi = RVIN_CSI20, .chan = 0 },
			{ .csi = RVIN_CSI21, .chan = 0 },
			{ .csi = RVIN_CSI41, .chan = 0 },
			{ .csi = RVIN_CSI20, .chan = 0 },
			{ .csi = RVIN_CSI21, .chan = 0 },
		}, {
			{ .csi = RVIN_CSI20, .chan = 0 },
			{ .csi = RVIN_CSI21, .chan = 0 },
			{ .csi = RVIN_CSI41, .chan = 0 },
			{ .csi = RVIN_CSI41, .chan = 1 },
			{ .csi = RVIN_CSI20, .chan = 1 },
			{ .csi = RVIN_CSI21, .chan = 1 },
		}, {
			{ .csi = RVIN_CSI21, .chan = 0 },
			{ .csi = RVIN_CSI41, .chan = 0 },
			{ .csi = RVIN_CSI20, .chan = 0 },
			{ .csi = RVIN_CSI41, .chan = 2 },
			{ .csi = RVIN_CSI20, .chan = 2 },
			{ .csi = RVIN_CSI21, .chan = 2 },
		}, {
			{ .csi = RVIN_CSI41, .chan = 1 },
			{ .csi = RVIN_CSI20, .chan = 1 },
			{ .csi = RVIN_CSI21, .chan = 1 },
			{ .csi = RVIN_CSI41, .chan = 3 },
			{ .csi = RVIN_CSI20, .chan = 3 },
			{ .csi = RVIN_CSI21, .chan = 3 },
		},
	},
};

static const struct rvin_info rcar_info_r8a7796 = {
	.chip = RCAR_GEN3,
	.use_mc = true,
	.max_width = 4096,
	.max_height = 4096,

	.num_chsels = 5,
	.chsels = {
		{
			{ .csi = RVIN_CSI40, .chan = 0 },
			{ .csi = RVIN_CSI20, .chan = 0 },
			{ .csi = RVIN_NC, .chan = 0 },
			{ .csi = RVIN_CSI40, .chan = 0 },
			{ .csi = RVIN_CSI20, .chan = 0 },
		}, {
			{ .csi = RVIN_CSI20, .chan = 0 },
			{ .csi = RVIN_NC, .chan = 0 },
			{ .csi = RVIN_CSI40, .chan = 0 },
			{ .csi = RVIN_CSI40, .chan = 1 },
			{ .csi = RVIN_CSI20, .chan = 1 },
		}, {
			{ .csi = RVIN_NC, .chan = 0 },
			{ .csi = RVIN_CSI40, .chan = 0 },
			{ .csi = RVIN_CSI20, .chan = 0 },
			{ .csi = RVIN_CSI40, .chan = 2 },
			{ .csi = RVIN_CSI20, .chan = 2 },
		}, {
			{ .csi = RVIN_CSI40, .chan = 1 },
			{ .csi = RVIN_CSI20, .chan = 1 },
			{ .csi = RVIN_NC, .chan = 1 },
			{ .csi = RVIN_CSI40, .chan = 3 },
			{ .csi = RVIN_CSI20, .chan = 3 },
		}, {
			{ .csi = RVIN_CSI40, .chan = 0 },
			{ .csi = RVIN_CSI20, .chan = 0 },
			{ .csi = RVIN_NC, .chan = 0 },
			{ .csi = RVIN_CSI40, .chan = 0 },
			{ .csi = RVIN_CSI20, .chan = 0 },
		}, {
			{ .csi = RVIN_CSI20, .chan = 0 },
			{ .csi = RVIN_NC, .chan = 0 },
			{ .csi = RVIN_CSI40, .chan = 0 },
			{ .csi = RVIN_CSI40, .chan = 1 },
			{ .csi = RVIN_CSI20, .chan = 1 },
		}, {
			{ .csi = RVIN_NC, .chan = 0 },
			{ .csi = RVIN_CSI40, .chan = 0 },
			{ .csi = RVIN_CSI20, .chan = 0 },
			{ .csi = RVIN_CSI40, .chan = 2 },
			{ .csi = RVIN_CSI20, .chan = 2 },
		}, {
			{ .csi = RVIN_CSI40, .chan = 1 },
			{ .csi = RVIN_CSI20, .chan = 1 },
			{ .csi = RVIN_NC, .chan = 1 },
			{ .csi = RVIN_CSI40, .chan = 3 },
			{ .csi = RVIN_CSI20, .chan = 3 },
		},
	},
};

static const struct rvin_info rcar_info_gen2 = {
	.chip = RCAR_GEN2,
	.use_mc = false,
	.max_width = 2048,
	.max_height = 2048,
};

static const struct of_device_id rvin_of_id_table[] = {
	{
		.compatible = "renesas,vin-r8a7795",
		.data = &rcar_info_r8a7795,
	},
	{
		.compatible = "renesas,vin-r8a7796",
		.data = &rcar_info_r8a7796,
	},
	{
		.compatible = "renesas,vin-r8a7794",
		.data = &rcar_info_gen2,
	},
	{
		.compatible = "renesas,vin-r8a7793",
		.data = &rcar_info_gen2,
	},
	{
		.compatible = "renesas,vin-r8a7791",
		.data = &rcar_info_gen2,
	},
	{
		.compatible = "renesas,vin-r8a7790",
		.data = &rcar_info_gen2,
	},
	{
		.compatible = "renesas,vin-r8a7779",
		.data = &rcar_info_h1,
	},
	{
		.compatible = "renesas,vin-r8a7778",
		.data = &rcar_info_m1,
	},
	{
		.compatible = "renesas,rcar-gen2-vin",
		.data = &rcar_info_gen2,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, rvin_of_id_table);

static int rcar_vin_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct rvin_dev *vin;
	struct resource *mem;
	int irq, ret;

	vin = devm_kzalloc(&pdev->dev, sizeof(*vin), GFP_KERNEL);
	if (!vin)
		return -ENOMEM;

	match = of_match_device(of_match_ptr(rvin_of_id_table), &pdev->dev);
	if (!match)
		return -ENODEV;

	vin->dev = &pdev->dev;
	vin->info = match->data;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (mem == NULL)
		return -EINVAL;

	vin->base = devm_ioremap_resource(vin->dev, mem);
	if (IS_ERR(vin->base))
		return PTR_ERR(vin->base);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = rvin_dma_probe(vin, irq);
	if (ret)
		return ret;

	if (vin->info->use_mc)
		ret = rvin_group_init(vin);
	else
		ret = rvin_digital_graph_init(vin);
	if (ret < 0)
		goto error;

	pm_suspend_ignore_children(&pdev->dev, true);
	pm_runtime_enable(&pdev->dev);

	platform_set_drvdata(pdev, vin);

	return 0;
error:
	rvin_dma_remove(vin);

	return ret;
}

static int rcar_vin_remove(struct platform_device *pdev)
{
	struct rvin_dev *vin = platform_get_drvdata(pdev);

	pm_runtime_disable(&pdev->dev);

	v4l2_async_notifier_unregister(&vin->notifier);

	if (vin->info->use_mc) {
		rvin_v4l2_mc_remove(vin);
		rvin_group_delete(vin);
	} else {
		rvin_v4l2_remove(vin);
	}

	rvin_dma_remove(vin);

	return 0;
}

static struct platform_driver rcar_vin_driver = {
	.driver = {
		.name = "rcar-vin",
		.of_match_table = rvin_of_id_table,
	},
	.probe = rcar_vin_probe,
	.remove = rcar_vin_remove,
};

module_platform_driver(rcar_vin_driver);

MODULE_AUTHOR("Niklas Söderlund <niklas.soderlund@ragnatech.se>");
MODULE_DESCRIPTION("Renesas R-Car VIN camera host driver");
MODULE_LICENSE("GPL v2");
