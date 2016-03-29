/*
 * vsp1.h
 *
 * Renesas R-Car VSP1 - User-space API
 *
 * Copyright (C) 2013 Renesas Corporation
 *
 * Contacts: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __VSP1_USER_H__
#define __VSP1_USER_H__

#include <linux/types.h>
#include <linux/videodev2.h>

/*
 * Private IOCTLs
 *
 * VIDIOC_VSP1_LUT_CONFIG - Configure the lookup table
 *
 * VIDIOC_VSP1_CLU_CONFIG - Configure the 3D lookup table
 * @nentries: number of entries in the entries array
 * @entries: CLU entries
 *
 * Each CLU entry is identified by an address and has a value. The address is
 * split in 4 bytes ; the MSB must be set to 0 and all 3 other bytes set to
 * values between 0 and 16 inclusive. The value must be in the range 0x00000000
 * to 0x00ffffff.
 *
 * The number of entries is limited to 17*17*17. If the number of entries or the
 * address or value of an entry is invalid the ioctl will return -EINVAL.
 * Otherwise it will program the hardware with the entries and return 0.
 */

#define VIDIOC_VSP1_LUT_CONFIG \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 1, struct vsp1_lut_config)
#define VIDIOC_VSP1_CLU_CONFIG \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 2, struct vsp1_clu_config)

struct vsp1_lut_config {
	__u32 lut[256];
};

struct vsp1_clu_entry {
	__u32 addr;
	__u32 value;
};

struct vsp1_clu_config {
	__u32 nentries;
	struct vsp1_clu_entry __user *entries;
};

#endif	/* __VSP1_USER_H__ */
