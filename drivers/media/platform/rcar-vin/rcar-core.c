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

#include <media/v4l2-fwnode.h>

#include "rcar-vin.h"

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
	struct v4l2_fwnode_endpoint v4l2_ep;
	int ret;

	ret = v4l2_fwnode_endpoint_parse(of_fwnode_handle(ep), &v4l2_ep);
	if (ret) {
		vin_err(vin, "Could not parse v4l2 endpoint\n");
		return -EINVAL;
	}

	mbus_cfg->type = v4l2_ep.bus_type;

	switch (mbus_cfg->type) {
	case V4L2_MBUS_PARALLEL:
		vin_dbg(vin, "Found PARALLEL media bus\n");
		mbus_cfg->flags = v4l2_ep.bus.parallel.flags;
		break;
	case V4L2_MBUS_BT656:
		vin_dbg(vin, "Found BT656 media bus\n");
		mbus_cfg->flags = 0;
		break;
	default:
		vin_err(vin, "Unknown media bus type\n");
		return -EINVAL;
	}

	return 0;
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
	struct v4l2_subdev *sd = vin_to_source(vin);
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

	/* Add the controls */
	/*
	 * Currently the subdev with the largest number of controls (13) is
	 * ov6550. So let's pick 16 as a hint for the control handler. Note
	 * that this is a hint only: too large and you waste some memory, too
	 * small and there is a (very) small performance hit when looking up
	 * controls in the internal hash.
	 */
	ret = v4l2_ctrl_handler_init(&vin->ctrl_handler, 16);
	if (ret < 0)
		return ret;

	ret = v4l2_ctrl_add_handler(&vin->ctrl_handler, sd->ctrl_handler, NULL);
	if (ret < 0)
		return ret;

	ret = v4l2_subdev_call(sd, video, g_tvnorms, &vin->vdev.tvnorms);
	if (ret < 0 && ret != -ENOIOCTLCMD && ret != -ENODEV)
		return ret;

	if (vin->vdev.tvnorms == 0) {
		/* Disable the STD API if there are no tvnorms defined */
		v4l2_disable_ioctl(&vin->vdev, VIDIOC_G_STD);
		v4l2_disable_ioctl(&vin->vdev, VIDIOC_S_STD);
		v4l2_disable_ioctl(&vin->vdev, VIDIOC_QUERYSTD);
		v4l2_disable_ioctl(&vin->vdev, VIDIOC_ENUMSTD);
	}

	return rvin_reset_format(vin);
}

static void rvin_digital_notify_unbind(struct v4l2_async_notifier *notifier,
				       struct v4l2_subdev *subdev,
				       struct v4l2_async_subdev *asd)
{
	struct rvin_dev *vin = notifier_to_vin(notifier);

	vin_dbg(vin, "unbind digital subdev %s\n", subdev->name);
	v4l2_ctrl_handler_free(&vin->ctrl_handler);
	vin->digital.subdev = NULL;
}

static int rvin_digital_notify_bound(struct v4l2_async_notifier *notifier,
				     struct v4l2_subdev *subdev,
				     struct v4l2_async_subdev *asd)
{
	struct rvin_dev *vin = notifier_to_vin(notifier);
	int ret;

	v4l2_set_subdev_hostdata(subdev, vin);

	/* Find source and sink pad of remote subdevice */

	ret = rvin_find_pad(subdev, MEDIA_PAD_FL_SOURCE);
	if (ret < 0)
		return ret;
	vin->digital.source_pad = ret;

	ret = rvin_find_pad(subdev, MEDIA_PAD_FL_SINK);
	vin->digital.sink_pad = ret < 0 ? 0 : ret;

	vin->digital.subdev = subdev;

	vin_dbg(vin, "bound subdev %s source pad: %u sink pad: %u\n",
		subdev->name, vin->digital.source_pad,
		vin->digital.sink_pad);

	return 0;
}

static int rvin_digital_graph_parse(struct rvin_dev *vin)
{
	struct device_node *ep, *np;
	int ret;

	vin->digital.asd.match.fwnode.fwnode = NULL;
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

	vin->digital.asd.match.fwnode.fwnode = of_fwnode_handle(np);
	vin->digital.asd.match_type = V4L2_ASYNC_MATCH_FWNODE;

	return 0;
}

static int rvin_digital_graph_init(struct rvin_dev *vin)
{
	struct v4l2_async_subdev **subdevs = NULL;
	int ret;

	ret = rvin_v4l2_probe(vin);
	if (ret)
		return ret;

	ret = rvin_digital_graph_parse(vin);
	if (ret)
		return ret;

	if (!vin->digital.asd.match.fwnode.fwnode) {
		vin_dbg(vin, "No digital subdevice found\n");
		return -ENODEV;
	}

	/* Register the subdevices notifier. */
	subdevs = devm_kzalloc(vin->dev, sizeof(*subdevs), GFP_KERNEL);
	if (subdevs == NULL)
		return -ENOMEM;

	subdevs[0] = &vin->digital.asd;

	vin_dbg(vin, "Found digital subdevice %s\n",
		of_node_full_name(to_of_node(subdevs[0]->match.fwnode.fwnode)));

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

static int rvin_group_init(struct rvin_dev *vin)
{
	int ret;

	vin->group = rvin_group_allocate(vin);
	if (IS_ERR(vin->group))
		return PTR_ERR(vin->group);

	ret = rvin_v4l2_probe(vin);
	if (ret)
		goto error_group;

	vin->pad.flags = MEDIA_PAD_FL_SINK;
	ret = media_entity_pads_init(&vin->vdev.entity, 1, &vin->pad);
	if (ret)
		goto error_v4l2;

	return 0;

error_v4l2:
	rvin_v4l2_remove(vin);
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

static const struct rvin_info rcar_info_gen2 = {
	.chip = RCAR_GEN2,
	.use_mc = false,
	.max_width = 2048,
	.max_height = 2048,
};

static const struct of_device_id rvin_of_id_table[] = {
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
		goto error_dma;

	pm_suspend_ignore_children(&pdev->dev, true);
	pm_runtime_enable(&pdev->dev);

	platform_set_drvdata(pdev, vin);

	return 0;

error_dma:
	rvin_dma_remove(vin);

	return ret;
}

static int rcar_vin_remove(struct platform_device *pdev)
{
	struct rvin_dev *vin = platform_get_drvdata(pdev);

	pm_runtime_disable(&pdev->dev);

	v4l2_async_notifier_unregister(&vin->notifier);

	/* Checks internaly if handlers have been init or not */
	v4l2_ctrl_handler_free(&vin->ctrl_handler);

	if (vin->info->use_mc)
		rvin_group_delete(vin);

	rvin_v4l2_remove(vin);
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
