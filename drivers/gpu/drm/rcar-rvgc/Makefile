# SPDX-License-Identifier: GPL-2.0
# CFLAGS_<file>.o += -Og
#or a bit more global
ccflags-y += -Og
CFLAGS_rcar-rvgc_kms.o    += -Og
CFLAGS_rcar_rvgc_pipe.o   += -Og
CFLAGS_rcar_rvgc_taurus.o += -Og
CFLAGS_rcar-rvgc-drm.o    += -Og
rcar-rvgc-drm-y := rcar_rvgc_drv.o \
		   rcar_rvgc_kms.o \
		   rcar_rvgc_pipe.o \
		   rcar_rvgc_taurus.o

obj-$(CONFIG_DRM_RCAR_RVGC)		+= rcar-rvgc-drm.o

