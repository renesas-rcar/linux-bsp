#ifndef __RCAR_RVGC_TAURUS_H__
#define __RCAR_RVGC_TAURUS_H__

#include <linux/types.h>

struct rcar_rvgc_device;
struct taurus_rvgc_res_msg;

int rvgc_taurus_display_init(struct rcar_rvgc_device *rcrvgc,
			uint32_t display,
			uint32_t layer,
			struct taurus_rvgc_res_msg *res_msg);

int rvgc_taurus_display_get_info(struct rcar_rvgc_device *rcrvgc,
				uint32_t display,
				struct taurus_rvgc_res_msg *res_msg);

int rvgc_taurus_display_flush(struct rcar_rvgc_device *rcrvgc,
			uint32_t display,
			uint32_t blocking,
			struct taurus_rvgc_res_msg *res_msg);

int rvgc_taurus_layer_set_addr(struct rcar_rvgc_device *rcrvgc,
			uint32_t display,
			uint32_t layer,
			uint32_t paddr,
			struct taurus_rvgc_res_msg *res_msg);

int rvgc_taurus_layer_set_size(struct rcar_rvgc_device *rcrvgc,
			uint32_t display,
			uint32_t layer,
			uint32_t width,
			uint32_t height,
			struct taurus_rvgc_res_msg *res_msg);

#endif /* __RCAR_RVGC_TAURUS_H__ */
