#ifndef __RCAR_RVGC_KMS_H__
#define __RCAR_RVGC_KMS_H__

#include <linux/types.h>

struct drm_file;
struct drm_device;
struct drm_mode_create_dumb;
struct rcar_rvgc_device;

struct rcar_rvgc_format_info {
	u32 fourcc;
	unsigned int bpp;
	unsigned int planes;
};

const struct rcar_rvgc_format_info *rcar_rvgc_format_info(u32 fourcc);

int rcar_rvgc_modeset_init(struct rcar_rvgc_device *rcrvgc);

int rcar_rvgc_async_commit(struct drm_device *dev, struct drm_crtc *crtc);
int rcar_rvgc_crtc_enable_vblank(struct drm_device *dev, unsigned int pipe);
void rcar_rvgc_crtc_disable_vblank(struct drm_device *dev, unsigned int pipe);

#endif /* __RCAR_RVGC_KMS_H__ */
