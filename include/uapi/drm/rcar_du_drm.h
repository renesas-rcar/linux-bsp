/*
 * rcar_du_drm.h  --  R-Car Display Unit DRM driver
 *
 * Copyright (C) 2017 Renesas Electronics Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __RCAR_DU_DRM_H__
#define __RCAR_DU_DRM_H__

struct rcar_du_vmute {
	int crtc_id;	/* CRTCs ID */
	int on;		/* Vmute function ON/OFF */
};

/* rcar-du + vspd specific ioctls */
#define DRM_RCAR_DU_SET_VMUTE		0

#define DRM_IOCTL_RCAR_DU_SET_VMUTE \
	DRM_IOW(DRM_COMMAND_BASE + DRM_RCAR_DU_SET_VMUTE, \
		struct rcar_du_vmute)

#endif /* __RCAR_DU_DRM_H__ */
