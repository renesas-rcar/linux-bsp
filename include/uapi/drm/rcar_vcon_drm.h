/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * rcar_vcon_drm.h  --  R-Car Video Interface Converter DRM driver
 *
 * Copyright (C) 2023-2024 Renesas Electronics Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __RCAR_VCON_DRM_H__
#define __RCAR_VCON_DRM_H__

struct rcar_vcon_vmute {
	int crtc_id;	/* CRTCs ID */
	int on;		/* Vmute function ON/OFF */
};

/* DRM_IOCTL_RCAR_VCON_SET_SCRSHOT:VSPD screen shot */
struct rcar_vcon_screen_shot {
	unsigned long	buff;
	unsigned int	buff_len;
	unsigned int	crtc_id;
	unsigned int	fmt;
	unsigned int	width;
	unsigned int	height;
};

/* rcar-vcon + vspd specific ioctls */
#define DRM_RCAR_VCON_SET_VMUTE		0
#define DRM_RCAR_VCON_SCRSHOT		4

#define DRM_IOCTL_RCAR_VCON_SET_VMUTE \
	DRM_IOW(DRM_COMMAND_BASE + DRM_RCAR_VCON_SET_VMUTE, \
		struct rcar_vcon_vmute)

#define DRM_IOCTL_RCAR_VCON_SCRSHOT \
	DRM_IOW(DRM_COMMAND_BASE + DRM_RCAR_VCON_SCRSHOT, \
		struct rcar_vcon_screen_shot)

#endif /* __RCAR_VCON_DRM_H__ */
