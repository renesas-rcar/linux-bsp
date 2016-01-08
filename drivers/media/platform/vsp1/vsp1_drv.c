/*
 * vsp1_drv.c  --  R-Car VSP1 Driver
 *
 * Copyright (C) 2013-2016 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifdef CONFIG_VIDEO_RENESAS_DEBUG
#define DEBUG
#endif

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/videodev2.h>

#include <media/v4l2-subdev.h>

#include "vsp1.h"
#include "vsp1_bru.h"
#include "vsp1_dl.h"
#include "vsp1_drm.h"
#include "vsp1_hsit.h"
#include "vsp1_lif.h"
#include "vsp1_lut.h"
#include "vsp1_rwpf.h"
#include "vsp1_sru.h"
#include "vsp1_uds.h"
#include "vsp1_video.h"

#define VSP1_UT_IRQ	0x01

static unsigned int vsp1_debug;	/* 1 to enable debug output */
module_param_named(debug, vsp1_debug, int, 0600);
static int underrun_vspd[4];
module_param_array(underrun_vspd, int, NULL, 0600);

#ifdef CONFIG_VIDEO_RENESAS_DEBUG
#define VSP1_IRQ_DEBUG(fmt, args...)					\
	do {								\
		if (unlikely(vsp1_debug & VSP1_UT_IRQ))			\
			vsp1_ut_debug_printk(__func__, fmt, ##args);	\
	} while (0)
#else
#define VSP1_IRQ_DEBUG(fmt, args...)
#endif

void vsp1_ut_debug_printk(const char *function_name, const char *format, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, format);
	vaf.fmt = format;
	vaf.va = &args;

	pr_debug("[vsp1 :%s] %pV", function_name, &vaf);

	va_end(args);
}

#define SRCR7_REG		0xe61501cc
#define	FCPVD0_REG		0xfea27000
#define	FCPVD1_REG		0xfea2f000
#define	FCPVD2_REG		0xfea37000
#define	FCPVD3_REG		0xfea3f000

#define FCP_RST_REG		0x0010
#define FCP_RST_SOFTRST		0x00000001
#define FCP_RST_WORKAROUND	0x00000010

#define FCP_STA_REG		0x0018
#define FCP_STA_ACT		0x00000001

static void __iomem *fcpv_reg[4];
static const unsigned int fcpvd_offset[] = {
	FCPVD0_REG, FCPVD1_REG, FCPVD2_REG, FCPVD3_REG
};

void vsp1_underrun_workaround(struct vsp1_device *vsp1, bool reset)
{
	unsigned int timeout = 0;

	/* 1. Disable clock stop of VSP */
	vsp1_write(vsp1, VI6_CLK_CTRL0, VI6_CLK_CTRL0_WORKAROUND);
	vsp1_write(vsp1, VI6_CLK_CTRL1, VI6_CLK_CTRL1_WORKAROUND);
	vsp1_write(vsp1, VI6_CLK_DCSWT, VI6_CLK_DCSWT_WORKAROUND1);
	vsp1_write(vsp1, VI6_CLK_DCSM0, VI6_CLK_DCSM0_WORKAROUND);
	vsp1_write(vsp1, VI6_CLK_DCSM1, VI6_CLK_DCSM1_WORKAROUND);

	/* 2. Stop operation of VSP except bus access with module reset */
	vsp1_write(vsp1, VI6_MRESET_ENB0, VI6_MRESET_ENB0_WORKAROUND1);
	vsp1_write(vsp1, VI6_MRESET_ENB1, VI6_MRESET_ENB1_WORKAROUND);
	vsp1_write(vsp1, VI6_MRESET, VI6_MRESET_WORKAROUND);

	/* 3. Stop operation of FCPV with software reset */
	iowrite32(FCP_RST_SOFTRST, fcpv_reg[vsp1->index] + FCP_RST_REG);

	/* 4. Wait until FCP_STA.ACT become 0. */
	while (1) {
		if ((ioread32(fcpv_reg[vsp1->index] + FCP_STA_REG) &
			FCP_STA_ACT) != FCP_STA_ACT)
			break;

		if (timeout == 100)
			break;

		timeout++;
		udelay(1);
	}

	/* 5. Initialize the whole FCPV with module reset */
	iowrite32(FCP_RST_WORKAROUND, fcpv_reg[vsp1->index] + FCP_RST_REG);

	/* 6. Stop the whole operation of VSP with module reset */
	/*    (note that register setting is not cleared) */
	vsp1_write(vsp1, VI6_MRESET_ENB0, VI6_MRESET_ENB0_WORKAROUND2);
	vsp1_write(vsp1, VI6_MRESET_ENB1, VI6_MRESET_ENB1_WORKAROUND);
	vsp1_write(vsp1, VI6_MRESET, VI6_MRESET_WORKAROUND);

	/* 7. Enable clock stop of VSP */
	vsp1_write(vsp1, VI6_CLK_CTRL0, 0);
	vsp1_write(vsp1, VI6_CLK_CTRL1, 0);
	vsp1_write(vsp1, VI6_CLK_DCSWT, VI6_CLK_DCSWT_WORKAROUND2);
	vsp1_write(vsp1, VI6_CLK_DCSM0, 0);
	vsp1_write(vsp1, VI6_CLK_DCSM1, 0);

	/* 8. Restart VSPD */
	if (!reset) {
		/* Necessary when headerless display list */
		vsp1_write(vsp1, VI6_DL_HDR_ADDR(0), vsp1->dl_addr);
		vsp1_write(vsp1, VI6_DL_BODY_SIZE, vsp1->dl_body);
		vsp1_write(vsp1, VI6_CMD(0), VI6_CMD_STRCMD);
	}
}

/* -----------------------------------------------------------------------------
 * Interrupt Handling
 */
static irqreturn_t vsp1_irq_handler(int irq, void *data)
{
	u32 mask = VI6_WFP_IRQ_STA_DFE | VI6_WFP_IRQ_STA_FRE
				       | VI6_WFP_IRQ_STA_UND;
	struct vsp1_device *vsp1 = data;
	irqreturn_t ret = IRQ_NONE;
	unsigned int i;
	u32 status;
	unsigned int vsp_und_cnt = 0;

	for (i = 0; i < vsp1->pdata.wpf_count; ++i) {
		struct vsp1_rwpf *wpf = vsp1->wpf[i];
		struct vsp1_pipeline *pipe;

		if (wpf == NULL)
			continue;

		pipe = to_vsp1_pipeline(&wpf->entity.subdev.entity);
		status = vsp1_read(vsp1, VI6_WPF_IRQ_STA(i));
		vsp1_write(vsp1, VI6_WPF_IRQ_STA(i), ~status & mask);

		if (vsp1_debug && (status & VI6_WFP_IRQ_STA_UND)) {
			vsp_und_cnt = ++underrun_vspd[vsp1->index];

			VSP1_IRQ_DEBUG(
				"Underrun occurred num[%d] at VSPD (%s)\n",
				vsp_und_cnt, dev_name(vsp1->dev));
		}

		if (status & VI6_WFP_IRQ_STA_FRE) {
			vsp1_pipeline_frame_end(pipe);
			ret = IRQ_HANDLED;
		}
	}

	status = vsp1_read(vsp1, VI6_DISP_IRQ_STA);
	vsp1_write(vsp1, VI6_DISP_IRQ_STA, ~status & VI6_DISP_IRQ_STA_DST);

	if (status & VI6_DISP_IRQ_STA_DST) {
		struct vsp1_rwpf *wpf = vsp1->wpf[0];
		struct vsp1_pipeline *pipe;

		if (wpf) {
			pipe = to_vsp1_pipeline(&wpf->entity.subdev.entity);
			vsp1_pipeline_display_start(pipe);
		}

		ret = IRQ_HANDLED;
	}

	if (((vsp1->info->wc) & VSP1_UNDERRUN_WORKAROUND) &&
		(status & VI6_WFP_IRQ_STA_UND))
		vsp1_underrun_workaround(vsp1, false);

	return ret;
}

/* -----------------------------------------------------------------------------
 * Entities
 */

/*
 * vsp1_create_sink_links - Create links from all sources to the given sink
 *
 * This function creates media links from all valid sources to the given sink
 * pad. Links that would be invalid according to the VSP1 hardware capabilities
 * are skipped. Those include all links
 *
 * - from a UDS to a UDS (UDS entities can't be chained)
 * - from an entity to itself (no loops are allowed)
 */
static int vsp1_create_sink_links(struct vsp1_device *vsp1,
				  struct vsp1_entity *sink)
{
	struct media_entity *entity = &sink->subdev.entity;
	struct vsp1_entity *source;
	unsigned int pad;
	int ret;

	list_for_each_entry(source, &vsp1->entities, list_dev) {
		u32 flags;

		if (source->type == sink->type)
			continue;

		if (source->type == VSP1_ENTITY_LIF ||
		    source->type == VSP1_ENTITY_WPF)
			continue;

		flags = source->type == VSP1_ENTITY_RPF &&
			sink->type == VSP1_ENTITY_WPF &&
			source->index == sink->index
		      ? MEDIA_LNK_FL_ENABLED : 0;

		for (pad = 0; pad < entity->num_pads; ++pad) {
			if (!(entity->pads[pad].flags & MEDIA_PAD_FL_SINK))
				continue;

			ret = media_entity_create_link(&source->subdev.entity,
						       source->source_pad,
						       entity, pad, flags);
			if (ret < 0)
				return ret;

			if (flags & MEDIA_LNK_FL_ENABLED)
				source->sink = entity;
		}
	}

	return 0;
}

static int vsp1_uapi_create_links(struct vsp1_device *vsp1)
{
	struct vsp1_entity *entity;
	unsigned int i;
	int ret;

	list_for_each_entry(entity, &vsp1->entities, list_dev) {
		if (entity->type == VSP1_ENTITY_LIF ||
		    entity->type == VSP1_ENTITY_RPF)
			continue;

		ret = vsp1_create_sink_links(vsp1, entity);
		if (ret < 0)
			return ret;
	}

	if (vsp1->pdata.features & VSP1_HAS_LIF) {
		ret = media_entity_create_link(
			&vsp1->wpf[0]->entity.subdev.entity, RWPF_PAD_SOURCE,
			&vsp1->lif->entity.subdev.entity, LIF_PAD_SINK, 0);
		if (ret < 0)
			return ret;
	}

	for (i = 0; i < vsp1->pdata.rpf_count; ++i) {
		struct vsp1_rwpf *rpf = vsp1->rpf[i];

		ret = media_entity_create_link(&rpf->video->video.entity, 0,
					       &rpf->entity.subdev.entity,
					       RWPF_PAD_SINK,
					       MEDIA_LNK_FL_ENABLED |
					       MEDIA_LNK_FL_IMMUTABLE);
		if (ret < 0)
			return ret;
	}

	for (i = 0; i < vsp1->pdata.wpf_count; ++i) {
		/* Connect the video device to the WPF. All connections are
		 * immutable except for the WPF0 source link if a LIF is
		 * present.
		 */
		struct vsp1_rwpf *wpf = vsp1->wpf[i];
		unsigned int flags = MEDIA_LNK_FL_ENABLED;

		if (!(vsp1->pdata.features & VSP1_HAS_LIF) || i != 0)
			flags |= MEDIA_LNK_FL_IMMUTABLE;

		ret = media_entity_create_link(&wpf->entity.subdev.entity,
					       RWPF_PAD_SOURCE,
					       &wpf->video->video.entity,
					       0, flags);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static void vsp1_destroy_entities(struct vsp1_device *vsp1)
{
	struct vsp1_entity *entity, *_entity;
	struct vsp1_video *video, *_video;

	list_for_each_entry_safe(entity, _entity, &vsp1->entities, list_dev) {
		list_del(&entity->list_dev);
		vsp1_entity_destroy(entity);
	}

	list_for_each_entry_safe(video, _video, &vsp1->videos, list) {
		list_del(&video->list);
		vsp1_video_cleanup(video);
	}

	v4l2_device_unregister(&vsp1->v4l2_dev);
	media_device_unregister(&vsp1->media_dev);

	if (!vsp1->info->uapi)
		vsp1_drm_cleanup(vsp1);
}

static int vsp1_create_entities(struct vsp1_device *vsp1)
{
	struct media_device *mdev = &vsp1->media_dev;
	struct v4l2_device *vdev = &vsp1->v4l2_dev;
	struct vsp1_entity *entity;
	unsigned int i;
	int ret;

	mdev->dev = vsp1->dev;
	strlcpy(mdev->model, "VSP1", sizeof(mdev->model));
	snprintf(mdev->bus_info, sizeof(mdev->bus_info), "platform:%s",
		 dev_name(mdev->dev));
	ret = media_device_register(mdev);
	if (ret < 0) {
		dev_err(vsp1->dev, "media device registration failed (%d)\n",
			ret);
		return ret;
	}

	vsp1->media_ops.link_setup = vsp1_entity_link_setup;
	/* Don't perform link validation when the userspace API is disabled as
	 * the pipeline is configured internally by the driver in that case, and
	 * its configuration can thus be trusted.
	 */
	if (vsp1->info->uapi)
		vsp1->media_ops.link_validate = v4l2_subdev_link_validate;

	vdev->mdev = mdev;
	ret = v4l2_device_register(vsp1->dev, vdev);
	if (ret < 0) {
		dev_err(vsp1->dev, "V4L2 device registration failed (%d)\n",
			ret);
		goto done;
	}

	/* Instantiate all the entities. */
	if (vsp1->pdata.features & VSP1_HAS_BRU) {
		vsp1->bru = vsp1_bru_create(vsp1);
		if (IS_ERR(vsp1->bru)) {
			ret = PTR_ERR(vsp1->bru);
			goto done;
		}

		list_add_tail(&vsp1->bru->entity.list_dev, &vsp1->entities);
	}

	vsp1->hsi = vsp1_hsit_create(vsp1, true);
	if (IS_ERR(vsp1->hsi)) {
		ret = PTR_ERR(vsp1->hsi);
		goto done;
	}

	list_add_tail(&vsp1->hsi->entity.list_dev, &vsp1->entities);

	vsp1->hst = vsp1_hsit_create(vsp1, false);
	if (IS_ERR(vsp1->hst)) {
		ret = PTR_ERR(vsp1->hst);
		goto done;
	}

	list_add_tail(&vsp1->hst->entity.list_dev, &vsp1->entities);

	if (vsp1->pdata.features & VSP1_HAS_LIF) {
		vsp1->lif = vsp1_lif_create(vsp1);
		if (IS_ERR(vsp1->lif)) {
			ret = PTR_ERR(vsp1->lif);
			goto done;
		}

		list_add_tail(&vsp1->lif->entity.list_dev, &vsp1->entities);
	}

	if (vsp1->pdata.features & VSP1_HAS_LUT) {
		vsp1->lut = vsp1_lut_create(vsp1);
		if (IS_ERR(vsp1->lut)) {
			ret = PTR_ERR(vsp1->lut);
			goto done;
		}

		list_add_tail(&vsp1->lut->entity.list_dev, &vsp1->entities);
	}

	for (i = 0; i < vsp1->pdata.rpf_count; ++i) {
		struct vsp1_rwpf *rpf;

		rpf = vsp1_rpf_create(vsp1, i);
		if (IS_ERR(rpf)) {
			ret = PTR_ERR(rpf);
			goto done;
		}

		vsp1->rpf[i] = rpf;
		list_add_tail(&rpf->entity.list_dev, &vsp1->entities);

		if (vsp1->info->uapi) {
			struct vsp1_video *video = vsp1_video_create(vsp1, rpf);

			if (IS_ERR(video)) {
				ret = PTR_ERR(video);
				goto done;
			}

			list_add_tail(&video->list, &vsp1->videos);
		}
	}

	if (vsp1->pdata.features & VSP1_HAS_SRU) {
		vsp1->sru = vsp1_sru_create(vsp1);
		if (IS_ERR(vsp1->sru)) {
			ret = PTR_ERR(vsp1->sru);
			goto done;
		}

		list_add_tail(&vsp1->sru->entity.list_dev, &vsp1->entities);
	}

	for (i = 0; i < vsp1->pdata.uds_count; ++i) {
		struct vsp1_uds *uds;

		uds = vsp1_uds_create(vsp1, i);
		if (IS_ERR(uds)) {
			ret = PTR_ERR(uds);
			goto done;
		}

		vsp1->uds[i] = uds;
		list_add_tail(&uds->entity.list_dev, &vsp1->entities);
	}

	for (i = 0; i < vsp1->pdata.wpf_count; ++i) {
		struct vsp1_rwpf *wpf;

		wpf = vsp1_wpf_create(vsp1, i);
		if (IS_ERR(wpf)) {
			ret = PTR_ERR(wpf);
			goto done;
		}

		vsp1->wpf[i] = wpf;
		list_add_tail(&wpf->entity.list_dev, &vsp1->entities);

		if (vsp1->info->uapi) {
			struct vsp1_video *video = vsp1_video_create(vsp1, wpf);

			if (IS_ERR(video)) {
				ret = PTR_ERR(video);
				goto done;
			}

			list_add_tail(&video->list, &vsp1->videos);
			wpf->entity.sink = &video->video.entity;
		}
	}

	/* Create links. */
	if (vsp1->info->uapi)
		ret = vsp1_uapi_create_links(vsp1);
	else
		ret = vsp1_drm_create_links(vsp1);
	if (ret < 0)
		goto done;

	/* Register all subdevs. */
	list_for_each_entry(entity, &vsp1->entities, list_dev) {
		ret = v4l2_device_register_subdev(&vsp1->v4l2_dev,
						  &entity->subdev);
		if (ret < 0)
			goto done;
	}

	if (vsp1->info->uapi) {
		vsp1->use_dl = false;
		ret = v4l2_device_register_subdev_nodes(&vsp1->v4l2_dev);
	} else {
		vsp1->use_dl = true;
		ret = vsp1_drm_init(vsp1);
	}

done:
	if (ret < 0)
		vsp1_destroy_entities(vsp1);

	return ret;
}

int vsp1_reset_wpf(struct vsp1_device *vsp1, unsigned int index)
{
	unsigned int timeout;
	u32 status;

	status = vsp1_read(vsp1, VI6_STATUS);
	if (!(status & VI6_STATUS_SYS_ACT(index)))
		return 0;

	if ((vsp1->info->wc) & VSP1_UNDERRUN_WORKAROUND)
		vsp1_underrun_workaround(vsp1, true);
	else
		vsp1_write(vsp1, VI6_SRESET, VI6_SRESET_SRTS(index));

	for (timeout = 10; timeout > 0; --timeout) {
		status = vsp1_read(vsp1, VI6_STATUS);
		if (!(status & VI6_STATUS_SYS_ACT(index)))
			break;

		usleep_range(1000, 2000);
	}

	if (!timeout) {
		dev_err(vsp1->dev, "failed to reset wpf.%u\n", index);
		return -ETIMEDOUT;
	}

	return 0;
}

static int vsp1_device_init(struct vsp1_device *vsp1)
{
	unsigned int i;
	int ret;

	/* Reset any channel that might be running. */
	for (i = 0; i < vsp1->pdata.wpf_count; ++i) {
		ret = vsp1_reset_wpf(vsp1, i);
		if (ret < 0)
			return ret;
	}

	vsp1_write(vsp1, VI6_CLK_DCSWT, (8 << VI6_CLK_DCSWT_CSTPW_SHIFT) |
		   (8 << VI6_CLK_DCSWT_CSTRW_SHIFT));

	for (i = 0; i < vsp1->pdata.rpf_count; ++i)
		vsp1_write(vsp1, VI6_DPR_RPF_ROUTE(i), VI6_DPR_NODE_UNUSED);

	for (i = 0; i < vsp1->pdata.uds_count; ++i)
		vsp1_write(vsp1, VI6_DPR_UDS_ROUTE(i), VI6_DPR_NODE_UNUSED);

	vsp1_write(vsp1, VI6_DPR_SRU_ROUTE, VI6_DPR_NODE_UNUSED);
	vsp1_write(vsp1, VI6_DPR_LUT_ROUTE, VI6_DPR_NODE_UNUSED);
	vsp1_write(vsp1, VI6_DPR_CLU_ROUTE, VI6_DPR_NODE_UNUSED);
	vsp1_write(vsp1, VI6_DPR_HST_ROUTE, VI6_DPR_NODE_UNUSED);
	vsp1_write(vsp1, VI6_DPR_HSI_ROUTE, VI6_DPR_NODE_UNUSED);
	vsp1_write(vsp1, VI6_DPR_BRU_ROUTE, VI6_DPR_NODE_UNUSED);

	vsp1_write(vsp1, VI6_DPR_HGO_SMPPT, (7 << VI6_DPR_SMPPT_TGW_SHIFT) |
		   (VI6_DPR_NODE_UNUSED << VI6_DPR_SMPPT_PT_SHIFT));
	vsp1_write(vsp1, VI6_DPR_HGT_SMPPT, (7 << VI6_DPR_SMPPT_TGW_SHIFT) |
		   (VI6_DPR_NODE_UNUSED << VI6_DPR_SMPPT_PT_SHIFT));

	if (vsp1->use_dl)
		vsp1_dl_setup(vsp1);

	return 0;
}

/*
 * vsp1_device_get - Acquire the VSP1 device
 *
 * Increment the VSP1 reference count and initialize the device if the first
 * reference is taken.
 *
 * Return 0 on success or a negative error code otherwise.
 */
int vsp1_device_get(struct vsp1_device *vsp1)
{
	int ret = 0;

	mutex_lock(&vsp1->lock);
	if (vsp1->ref_count > 0)
		goto done;

	ret = clk_prepare_enable(vsp1->clock);
	if (ret < 0)
		goto done;

	if (vsp1->info->fcpvd) {
		ret = clk_prepare_enable(vsp1->fcpvd_clock);
		if (ret < 0) {
			clk_disable_unprepare(vsp1->clock);
			goto done;
		}
	}

	ret = vsp1_device_init(vsp1);
	if (ret < 0) {
		clk_disable_unprepare(vsp1->clock);
		if (vsp1->info->fcpvd)
			clk_disable_unprepare(vsp1->fcpvd_clock);
		goto done;
	}

done:
	if (!ret)
		vsp1->ref_count++;

	mutex_unlock(&vsp1->lock);
	return ret;
}

/*
 * vsp1_device_put - Release the VSP1 device
 *
 * Decrement the VSP1 reference count and cleanup the device if the last
 * reference is released.
 */
void vsp1_device_put(struct vsp1_device *vsp1)
{
	if (vsp1->ref_count == 0)
		return;

	mutex_lock(&vsp1->lock);

	if (--vsp1->ref_count == 0) {
		clk_disable_unprepare(vsp1->clock);
		if (vsp1->info->fcpvd)
			clk_disable_unprepare(vsp1->fcpvd_clock);
	}
	mutex_unlock(&vsp1->lock);
}

/* -----------------------------------------------------------------------------
 * Power Management
 */

#ifdef CONFIG_PM_SLEEP
static int vsp1_pm_suspend(struct device *dev)
{
	struct vsp1_device *vsp1 = dev_get_drvdata(dev);

	WARN_ON(mutex_is_locked(&vsp1->lock));

	if (vsp1->ref_count == 0)
		return 0;

	vsp1_pipelines_suspend(vsp1);

	clk_disable_unprepare(vsp1->clock);
	if (vsp1->info->fcpvd)
		clk_disable_unprepare(vsp1->fcpvd_clock);

	return 0;
}

static int vsp1_pm_resume(struct device *dev)
{
	struct vsp1_device *vsp1 = dev_get_drvdata(dev);

	WARN_ON(mutex_is_locked(&vsp1->lock));

	if (vsp1->ref_count == 0)
		return 0;

	clk_prepare_enable(vsp1->clock);
	if (vsp1->info->fcpvd)
		clk_prepare_enable(vsp1->fcpvd_clock);

	vsp1_pipelines_resume(vsp1);

	return 0;
}
#endif

static const struct dev_pm_ops vsp1_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(vsp1_pm_suspend, vsp1_pm_resume)
};

/* -----------------------------------------------------------------------------
 * Platform Driver
 */

static int vsp1_parse_dt(struct vsp1_device *vsp1)
{
	struct device_node *np = vsp1->dev->of_node;
	struct vsp1_platform_data *pdata = &vsp1->pdata;

	vsp1->info = of_device_get_match_data(vsp1->dev);

	if (of_property_read_bool(np, "renesas,has-bru"))
		pdata->features |= VSP1_HAS_BRU;
	if (of_property_read_bool(np, "renesas,has-lif"))
		pdata->features |= VSP1_HAS_LIF;
	if (of_property_read_bool(np, "renesas,has-lut"))
		pdata->features |= VSP1_HAS_LUT;
	if (of_property_read_bool(np, "renesas,has-sru"))
		pdata->features |= VSP1_HAS_SRU;

	of_property_read_u32(np, "renesas,#rpf", &pdata->rpf_count);
	of_property_read_u32(np, "renesas,#uds", &pdata->uds_count);
	of_property_read_u32(np, "renesas,#wpf", &pdata->wpf_count);

	if (pdata->rpf_count <= 0 || pdata->rpf_count > VSP1_MAX_RPF) {
		dev_err(vsp1->dev, "invalid number of RPF (%u)\n",
			pdata->rpf_count);
		return -EINVAL;
	}

	if (pdata->uds_count > VSP1_MAX_UDS) {
		dev_err(vsp1->dev, "invalid number of UDS (%u)\n",
			pdata->uds_count);
		return -EINVAL;
	}

	if (pdata->wpf_count <= 0 || pdata->wpf_count > VSP1_MAX_WPF) {
		dev_err(vsp1->dev, "invalid number of WPF (%u)\n",
			pdata->wpf_count);
		return -EINVAL;
	}

	/* Backward compatibility: all Gen2 VSP instances have a BRU, the
	 * renesas,has-bru property was thus not available. Set the HAS_BRU
	 * feature automatically in that case.
	 */
	if (vsp1->info->num_bru_inputs == 4)
		pdata->features |= VSP1_HAS_BRU;

	return 0;
}

static int vsp1_probe(struct platform_device *pdev)
{
	struct vsp1_device *vsp1;
	struct resource *irq;
	struct resource *io;
	int ret;

	vsp1 = devm_kzalloc(&pdev->dev, sizeof(*vsp1), GFP_KERNEL);
	if (vsp1 == NULL)
		return -ENOMEM;

	vsp1->dev = &pdev->dev;
	mutex_init(&vsp1->lock);
	INIT_LIST_HEAD(&vsp1->entities);
	INIT_LIST_HEAD(&vsp1->videos);

	ret = vsp1_parse_dt(vsp1);
	if (ret < 0)
		return ret;

	/* I/O, IRQ and clock resources */
	io = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	vsp1->mmio = devm_ioremap_resource(&pdev->dev, io);
	if (IS_ERR(vsp1->mmio))
		return PTR_ERR(vsp1->mmio);

	vsp1->clock = of_clk_get(vsp1->dev->of_node, 0);
	if (IS_ERR(vsp1->clock)) {
		dev_err(&pdev->dev, "failed to get clock\n");
		return PTR_ERR(vsp1->clock);
	}

	if (vsp1->info->fcpvd) {
		vsp1->fcpvd_clock = of_clk_get(vsp1->dev->of_node, 1);
		if (IS_ERR(vsp1->fcpvd_clock)) {
			dev_err(&pdev->dev, "failed to get fcpvd clock\n");
			return PTR_ERR(vsp1->fcpvd_clock);
		}
	}

	irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!irq) {
		dev_err(&pdev->dev, "missing IRQ\n");
		return -EINVAL;
	}

	ret = devm_request_irq(&pdev->dev, irq->start, vsp1_irq_handler,
			      IRQF_SHARED, dev_name(&pdev->dev), vsp1);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to request IRQ\n");
		return ret;
	}

	/* Instanciate entities */
	ret = vsp1_create_entities(vsp1);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to create entities\n");
		return ret;
	}

	if (strcmp(dev_name(vsp1->dev), "fea20000.vsp") == 0)
		vsp1->index = 0;
	else if (strcmp(dev_name(vsp1->dev), "fea28000.vsp") == 0)
		vsp1->index = 1;
	else if (strcmp(dev_name(vsp1->dev), "fea30000.vsp") == 0)
		vsp1->index = 2;
	else if (strcmp(dev_name(vsp1->dev), "fea38000.vsp") == 0)
		vsp1->index = 3;

	platform_set_drvdata(pdev, vsp1);

	if ((vsp1->info->wc) & VSP1_UNDERRUN_WORKAROUND)
		fcpv_reg[vsp1->index] =
			 ioremap(fcpvd_offset[vsp1->index], 0x20);

	return 0;
}

static int vsp1_remove(struct platform_device *pdev)
{
	struct vsp1_device *vsp1 = platform_get_drvdata(pdev);

	vsp1_destroy_entities(vsp1);

	if ((vsp1->info->wc) & VSP1_UNDERRUN_WORKAROUND)
		iounmap(fcpv_reg[vsp1->index]);

	return 0;
}

static const struct vsp1_device_info vsp1_gen2_info = {
	.num_bru_inputs = 4,
	.uapi = true,
	.wc = 0,
	.fcpvd = false,
};

static const struct vsp1_device_info vsp1_gen3_info = {
	.num_bru_inputs = 5,
	.uapi = true,
	.wc = 0,
	.fcpvd = false,
};

static const struct vsp1_device_info vsp1_gen3_vspd_info = {
	.num_bru_inputs = 5,
	.uapi = false,
	.wc = VSP1_UNDERRUN_WORKAROUND,
	.fcpvd = true,
};

static const struct of_device_id vsp1_of_match[] = {
	{ .compatible = "renesas,vsp1", .data = &vsp1_gen2_info },
	{ .compatible = "renesas,vsp2", .data = &vsp1_gen3_info },
	{ .compatible = "renesas,vsp2d", .data = &vsp1_gen3_vspd_info },
	{ },
};

static struct platform_driver vsp1_platform_driver = {
	.probe		= vsp1_probe,
	.remove		= vsp1_remove,
	.driver		= {
		.name	= "vsp1",
		.pm	= &vsp1_pm_ops,
		.of_match_table = vsp1_of_match,
	},
};

module_platform_driver(vsp1_platform_driver);

MODULE_ALIAS("vsp1");
MODULE_AUTHOR("Laurent Pinchart <laurent.pinchart@ideasonboard.com>");
MODULE_DESCRIPTION("Renesas VSP1 Driver");
MODULE_LICENSE("GPL");
