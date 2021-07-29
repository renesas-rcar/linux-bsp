# SPDX-License-Identifier: GPL-2.0
dtb-$(CONFIG_ARCH_R8A774A1) += r8a774a1-hihope-rzg2m.dtb
dtb-$(CONFIG_ARCH_R8A774A1) += r8a774a1-hihope-rzg2m-ex.dtb
dtb-$(CONFIG_ARCH_R8A774C0) += r8a774c0-cat874.dtb r8a774c0-ek874.dtb
dtb-$(CONFIG_ARCH_R8A7795) += r8a7795-salvator-x.dtb r8a7795-h3ulcb.dtb
dtb-$(CONFIG_ARCH_R8A7795) += r8a7795-h3ulcb-kf.dtb r8a7795-h3ulcb-4x2g-kf.dtb
dtb-$(CONFIG_ARCH_R8A7795) += r8a7795-salvator-xs.dtb
dtb-$(CONFIG_ARCH_R8A7795) += r8a7795-salvator-xs-4x2g.dtb
dtb-$(CONFIG_ARCH_R8A7795) += r8a7795-salvator-xs-2x2g.dtb
dtb-$(CONFIG_ARCH_R8A7795) += r8a7795-h3ulcb-4x2g.dtb
dtb-$(CONFIG_ARCH_R8A7795) += r8a7795-es1-salvator-x.dtb r8a7795-es1-h3ulcb.dtb
dtb-$(CONFIG_ARCH_R8A7795) += r8a7795-es1-h3ulcb-kf.dtb
dtb-$(CONFIG_ARCH_R8A7796) += r8a7796-salvator-x.dtb r8a7796-m3ulcb.dtb
dtb-$(CONFIG_ARCH_R8A7796) += r8a7796-m3ulcb-2x4g.dtb
dtb-$(CONFIG_ARCH_R8A7796) += r8a7796-m3ulcb-kf.dtb r8a7796-m3ulcb-2x4g-kf.dtb
dtb-$(CONFIG_ARCH_R8A7796) += r8a7796-salvator-xs.dtb
dtb-$(CONFIG_ARCH_R8A7796) += r8a7796-salvator-xs-2x4g.dtb
dtb-$(CONFIG_ARCH_R8A77965) += r8a77965-salvator-x.dtb r8a77965-salvator-xs.dtb
dtb-$(CONFIG_ARCH_R8A77965) += r8a77965-m3nulcb.dtb
dtb-$(CONFIG_ARCH_R8A77965) += r8a77965-m3nulcb-kf.dtb
dtb-$(CONFIG_ARCH_R8A77970) += r8a77970-eagle.dtb r8a77970-v3msk.dtb
#dtb-$(CONFIG_ARCH_R8A77970) += r8a77970-v3msk-kf.dtb r8a77970-es1-v3msk-kf.dtb
dtb-$(CONFIG_ARCH_R8A77980) += r8a77980-condor.dtb r8a77980-v3hsk.dtb
dtb-$(CONFIG_ARCH_R8A77990) += r8a77990-ebisu.dtb
dtb-$(CONFIG_ARCH_R8A77990) += r8a77990-ebisu-4d.dtb
dtb-$(CONFIG_ARCH_R8A77990) += r8a77990-es10-ebisu.dtb r8a77990-es10-ebisu-4d.dtb
dtb-$(CONFIG_ARCH_R8A77995) += r8a77995-draak.dtb
dtb-$(CONFIG_ARCH_R8A779A0) += r8a779a0-falcon.dtb

dtb-$(CONFIG_ARCH_R8A77970) += r8a77970-eagle-function.dtb
dtb-$(CONFIG_ARCH_R8A77970) += r8a77970-es1-eagle.dtb r8a77970-es1-eagle-function.dtb
dtb-$(CONFIG_ARCH_R8A77970) += r8a77970-es1-v3msk.dtb
dtb-$(CONFIG_ARCH_R8A77970) += r8a77970-v3msk-vbm.dtb r8a77970-es1-v3msk-vbm.dtb
dtb-$(CONFIG_ARCH_R8A77970) += r8a77970-v3msk-vbm-v2.dtb r8a77970-es1-v3msk-vbm-v2.dtb
dtb-$(CONFIG_ARCH_R8A77970) += r8a77970-v3msk-vbm-v3.dtb r8a77970-es1-v3msk-vbm-v3.dtb
dtb-$(CONFIG_ARCH_R8A77970) += r8a77970-v3msk-view.dtb r8a77970-es1-v3msk-view.dtb

dtb-$(CONFIG_ARCH_R8A77980) += r8a77980-v3hsk-vbm.dtb r8a77980-v3hsk-vb-4ch.dtb r8a77980-v3hsk-vb-8ch.dtb
dtb-$(CONFIG_ARCH_R8A77980) += r8a77980-v3hsk-vb-gmsl-8ch.dtb
dtb-$(CONFIG_ARCH_R8A77980) += r8a77980-v3hsk-vb-gmsl2-2x2.dtb
dtb-$(CONFIG_ARCH_R8A77980) += r8a77980-v3hsk-vb-gmsl2-4.dtb
dtb-$(CONFIG_ARCH_R8A77980) += r8a77980-v3hsk-vb-gmsl2-8.dtb

dtb-$(CONFIG_ARCH_R8A77980) += r8a77980-es2-condor.dtb
dtb-$(CONFIG_ARCH_R8A77980) += r8a77980-es2-v3hsk.dtb

# overlays
DTC_FLAGS += -@
ulcb-kf-dtbo := ulcb-kf-cn11.dtbo ulcb-kf-most.dtbo ulcb-kf-sd3.dtbo \
		ulcb-kf-cn10-pca.dtbo ulcb-kf-cn11-pca.dtbo \
		ulcb-kf-cn10-gmsl2.dtbo ulcb-kf-cn11-gmsl2.dtbo \
		ulcb-kf-panel-koe-tx31d200vm0baa-1280x480.dtbo \
		ulcb-kf-panel-mitsubishi-aa104xd12-1024x768.dtbo \
		ulcb-kf-panel-mitsubishi-aa121td01-1280x800.dtbo \
		ulcb-kf-imx219.dtbo

vb-dtbo := vb-fdplink-output.dtbo

dtb-$(CONFIG_ARCH_R8A7795) += $(ulcb-kf-dtbo)
dtb-$(CONFIG_ARCH_R8A7796) += $(ulcb-kf-dtbo)
dtb-$(CONFIG_ARCH_R8A77965) += $(ulcb-kf-dtbo)
dtb-$(CONFIG_ARCH_R8A77970) += $(vb-dtbo)
dtb-$(CONFIG_ARCH_R8A77980) += $(vb-dtbo)
