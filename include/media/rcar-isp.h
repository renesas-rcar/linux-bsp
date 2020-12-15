/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * rcar-isp.h  --  R-Car Image Signal Processor Driver
 *
 * Copyright (C) 2020 Renesas Electronics Corporation
 *
 */
#ifndef __MEDIA_RCAR_ISP_H__
#define __MEDIA_RCAR_ISP_H__

struct device_node;
struct rcar_isp_device;

#if IS_ENABLED(CONFIG_VIDEO_RCAR_ISP)
struct rcar_isp_device *rcar_isp_get(const struct device_node *np);
void rcar_isp_put(struct rcar_isp_device *isp);
struct device *rcar_isp_get_device(struct rcar_isp_device *isp);
int rcar_isp_enable(struct rcar_isp_device *isp);
void rcar_isp_disable(struct rcar_isp_device *isp);
int rcar_isp_init(struct rcar_isp_device *isp, u32 mbus_code);
#else
static inline struct rcar_isp_device *rcar_isp_get(const struct device_node *np)
{
	return ERR_PTR(-ENOENT);
}
static inline void rcar_isp_put(struct rcar_isp_device *isp) { }
static inline struct device *rcar_isp_get_device(struct rcar_isp_device *isp)
{
	return NULL;
}
static inline int rcar_isp_enable(struct rcar_isp_device *isp)
{
	return 0;
}
static inline void rcar_isp_disable(struct rcar_isp_device *isp) { }
static inline int rcar_isp_init(struct rcar_isp_device *isp, u32 mbus_code)
{
	return 0;
}
#endif

#endif /* __MEDIA_RCAR_ISP_H__ */
