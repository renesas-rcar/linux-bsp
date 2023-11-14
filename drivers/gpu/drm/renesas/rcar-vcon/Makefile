# SPDX-License-Identifier: GPL-2.0
rcar_vcon_drm-y := rcar_vcon_drv.o \
		   rcar_vcon_vsp.o \
		   rcar_vcon_encoder.o \
		   rcar_vcon_crtc.o \
		   rcar_vcon_kms.o \
		   rcar_vcon_writeback.o

obj-$(CONFIG_DRM_RCAR_VCON)	+= rcar_vcon_drm.o

rcar_dw_dp-y	:= rcar_dw_dp.o rcar_dw_dp_dummy.o

obj-$(CONFIG_DRM_RCAR_DW_DP)	+= rcar_dw_dp.o
