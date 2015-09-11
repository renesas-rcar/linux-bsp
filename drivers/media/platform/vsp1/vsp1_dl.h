

#ifndef __VSP1_DL_H__
#define __VSP1_DL_H__

/* Repeat mode */
enum {
	DL_REPEAT_NONE = 0, /* one shot. for mem to mem */
	DL_REPEAT_MANUAL,
	DL_REPEAT_AUTO,
};

/* Header mode */
enum {
	DL_NOT_USE = 0,
	DL_HEADER_MODE,
	DL_HEADER_LESS,
};


enum {
	DL_BODY_RPF0 = 0,
	DL_BODY_RPF1,
	DL_BODY_RPF2,
	DL_BODY_RPF3,
	DL_BODY_RPF4,
#if 0
	DL_BODY_CTRL,
	DL_BODY_LUT,
	DL_BODY_CLUT,
#else
	DL_BODY_WPF,
	DL_BODY_BRU,
	DL_BODY_DPR,
#endif
};


void vsp1_dl_set(struct vsp1_device *vsp1, u32 reg, u32 data);
int vsp1_dl_setup_control(struct vsp1_device *vsp1, int mode, int repeat);
void vsp1_dl_reset(struct vsp1_device *vsp1);
int vsp1_dl_is_use(struct vsp1_device *vsp1);
int vsp1_dl_is_auto_repeat(struct vsp1_device *vsp1);
int vsp1_dl_get(struct vsp1_device *vsp1, int module);
int vsp1_dl_irq_dl_frame_end(struct vsp1_device *vsp1);
int vsp1_dl_irq_display_start(struct vsp1_device *vsp1);
int vsp1_dl_set_stream(struct vsp1_device *vsp1);
int vsp1_dl_create(struct vsp1_device *vsp1);
#endif /* __VSP1_DL_H__ */
