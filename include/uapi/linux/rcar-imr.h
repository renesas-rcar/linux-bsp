/*
 * imr.h  --  R-Car IMR-LX4 Driver UAPI
 *
 * Copyright (C) 2016  Cogent Embedded, Inc.  <source@cogentembedded.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef RCAR_IMR_USER_H
#define RCAR_IMR_USER_H

#include <linux/videodev2.h>

/*******************************************************************************
 * Mapping specification descriptor
 ******************************************************************************/

struct imr_map_desc {
	/* ...mapping types */
	u32             type;

	/* ...total size of the mesh structure */
	u32             size;

	/* ...map-specific user-pointer */
	void           *data;

}   __attribute__((packed));

/* ...regular mesh specification */
#define IMR_MAP_MESH                    (1 << 0)

/* ...auto-generated source coordinates */
#define IMR_MAP_AUTODG                  (1 << 1)

/* ...auto-generated destination coordinates */
#define IMR_MAP_AUTOSG                  (1 << 2)

/* ...luminance correction flag */
#define IMR_MAP_LUCE                    (1 << 3)

/* ...chromacity correction flag */
#define IMR_MAP_CLCE                    (1 << 4)

/* ...vertex clockwise-mode order */
#define IMR_MAP_TCM                     (1 << 5)

/* ...texture mapping enable flag */
#define IMR_MAP_TME                     (1 << 6)

/* ...bilinear filtration enable flag */
#define IMR_MAP_BFE                     (1 << 7)

/* ...extended functionality (rotation/scaling) enable flag */
#define IMR_MAP_RSE                     (1 << 21)

/* ...source coordinate decimal point position bit index */
#define __IMR_MAP_UVDPOR_SHIFT          8
#define __IMR_MAP_UVDPOR(v)             (((v) >> __IMR_MAP_UVDPOR_SHIFT) & 0x7)
#define IMR_MAP_UVDPOR(n)               ((n & 0x7) << __IMR_MAP_UVDPOR_SHIFT)

/* ...destination coordinate sub-pixel mode */
#define IMR_MAP_DDP                     (1 << 11)

/* ...luminance correction offset decimal point position */
#define __IMR_MAP_YLDPO_SHIFT           12
#define __IMR_MAP_YLDPO(v)              (((v) >> __IMR_MAP_YLDPO_SHIFT) & 0x7)
#define IMR_MAP_YLDPO(n)                ((n & 0x7) << __IMR_MAP_YLDPO_SHIFT)

/* ...chromacity (U) correction offset decimal point position */
#define __IMR_MAP_UBDPO_SHIFT           15
#define __IMR_MAP_UBDPO(v)              (((v) >> __IMR_MAP_UBDPO_SHIFT) & 0x7)
#define IMR_MAP_UBDPO(n)                ((n & 0x7) << __IMR_MAP_UBDPO_SHIFT)

/* ...chromacity (V) correction offset decimal point position */
#define __IMR_MAP_VRDPO_SHIFT           18
#define __IMR_MAP_VRDPO(v)              (((v) >> __IMR_MAP_VRDPO_SHIFT) & 0x7)
#define IMR_MAP_VRDPO(n)                ((n & 0x7) << __IMR_MAP_VRDPO_SHIFT)

/* ...regular mesh specification */
struct imr_mesh {
	/* ...rectangular mesh size */
	u16             rows, columns;

	/* ...mesh parameters */
	u16             x0, y0, dx, dy;

}   __attribute__((packed));

/*******************************************************************************
 * V3H Extension destination data
 ******************************************************************************/
/* ...number of V3H extension destination buffers (rotated/non-rotated, scaled 1/1, 1/2, 1/4, 1/8) */
#define IMR_EXTDST_NUM                      8

struct imr_rse_param {
	/* ...logical right shift data */
	u8		sc8, sc4, sc2;
	/* ...destination buffers stride */
	u32            *strides;
};

/*******************************************************************************
 * Private IOCTL codes
 ******************************************************************************/

#define VIDIOC_IMR_MESH                 _IOW('V', BASE_VIDIOC_PRIVATE + 0, struct imr_map_desc)
#define VIDIOC_IMR_MESH_RAW             _IOW('V', BASE_VIDIOC_PRIVATE + 1, struct imr_map_desc)
#define VIDIOC_IMR_COLOR                _IOW('V', BASE_VIDIOC_PRIVATE + 2, u32)
#define VIDIOC_IMR_EXTDST               _IOW('V', BASE_VIDIOC_PRIVATE + 3, u32 *)
#define VIDIOC_IMR_EXTSTRIDE            _IOW('V', BASE_VIDIOC_PRIVATE + 4, struct imr_rse_param)

#endif  /* RCAR_IMR_USER_H */
