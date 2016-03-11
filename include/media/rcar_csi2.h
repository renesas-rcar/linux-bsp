/*
 * include/media/rcar_csi2.h
 *     This file is the driver header
 *     for the Renesas R-Car MIPI CSI-2 unit.
 *
 * Copyright (C) 2015 Renesas Electronics Corporation
 *
 * This file is based on the include/media/sh_mobile_csi2.h
 *
 * Driver header for the SH-Mobile MIPI CSI-2 unit
 *
 * Copyright (C) 2010, Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef RCAR_MIPI_CSI
#define RCAR_MIPI_CSI

#include <linux/list.h>

enum rcar_csi2_phy {
	RCAR_CSI2_PHY_CSI40,  /* CSI0 */
	RCAR_CSI2_PHY_CSI20,  /* CSI1 */
	RCAR_CSI2_PHY_CSI41,  /* CSI2 */
	RCAR_CSI2_PHY_CSI21,  /* CSI3 */
};

enum rcar_csi2_link {
	RCAR_CSI2_LINK_CSI40,
	RCAR_CSI2_LINK_CSI20,
	RCAR_CSI2_LINK_CSI41,
	RCAR_CSI2_LINK_CSI21,
};

enum rcar_csi2_type {
	RCAR_CSI2_CSI4X,
	RCAR_CSI2_CSI2X,
};

#define RCAR_CSI2_CRC	(1 << 0)
#define RCAR_CSI2_ECC	(1 << 1)

struct platform_device;

struct rcar_csi2_client_config {
	enum rcar_csi2_phy phy;
	enum rcar_csi2_link link;
	unsigned char lanes;		/* bitmask[3:0] */
	unsigned char channel;		/* 0..3 */
	struct platform_device *pdev;	/* client platform device */
	const char *name;		/* async matching: client name */
};

struct v4l2_device;

struct rcar_csi2_pdata {
	enum rcar_csi2_type type;
	unsigned int flags;
	struct rcar_csi2_client_config *clients;
	int num_clients;
};

#endif
