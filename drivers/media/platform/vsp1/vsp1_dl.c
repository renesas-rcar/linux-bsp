
#include <linux/device.h>
#include <linux/gfp.h>
#include <linux/dma-mapping.h>

#include "vsp1.h"
#include "vsp1_dl.h"

#ifdef VSP1_DL_SUPPORT

#define DL_MEM_SIZE (1024 * 3)
#define DISPLAY_LIST_NUM 3
#define DISPLAY_LIST_BODY_NUM 8

/* flag */
#define DL_FLAG_BODY_WRITEBLE (1 << 0)

/* Display List header/body use stat */
enum {
	DL_MEM_NO_USE = 0,
	DL_MEM_USE,
};

struct vsp1_device;

/* display list header format */
struct display_header {
	/* zero_bits:29 + num_list_minus1:3 */
	u32 num_list_minus1;
	struct {
		/* zero_bits:15 + num_bytes:17 */
		u32 num_bytes;
		u32 plist;
	} display_list[DISPLAY_LIST_BODY_NUM];
	u32 pnext_header;
	/* zero_bits:30 + current_frame_int_enable:1 + */
	/* next_frame_auto_start:1 */
	u32 int_auto;


	/* External data */
	/* TODO...       */
};

/* display list body format */
struct display_list { /* 8byte align */
	u32 set_address; /* resistor address */
	u32 set_data; /* resistor data */
};

struct dl_body {
	int size;
	int use;
	int reg_count;
	dma_addr_t paddr;
	struct display_list *dlist;
	unsigned long dlist_offset;
	int flag;
	/* struct dl_body *next; */
};

struct dl_head {
	int size;
	int use;
	int module;
	dma_addr_t paddr;
	struct display_header *dheader;
	unsigned long dheader_offset;
	struct dl_body *dl_body_list[DISPLAY_LIST_BODY_NUM];
	int flag;
	/* struct dl_head *next; */
};

struct vsp1_dl {
	struct vsp1_device *vsp1;
	bool active;
	int repeat;
	int mode;
	unsigned int flag;
	spinlock_t lock;

	/* memory */
	int size;
	dma_addr_t paddr;
	void *vaddr;

	struct dl_head *setting_header;
	struct dl_body *setting_body;

	/* header mode */
	struct dl_head head[DISPLAY_LIST_NUM];
	struct dl_body body[DISPLAY_LIST_NUM][DISPLAY_LIST_BODY_NUM];
	struct dl_head *active_header;
	struct dl_head *next_header;

	/* header less mode */
	struct dl_body single_body[DISPLAY_LIST_NUM];
	struct dl_body *active_body;
	struct dl_body *next_body;
	struct dl_body *pending_body;
#if 0
	struct dl_body *active_body_now;
	struct dl_body *active_body_next_set;
#endif
};

/* -----------------------------------------------------------------------------
 * Device Access
 */

static inline u32 dl_read(struct vsp1_dl *dl, u32 reg)
{
	return vsp1_read(dl->vsp1, reg);
}

static inline void dl_write(struct vsp1_dl *dl, u32 reg, u32 data)
{
	vsp1_write(dl->vsp1, reg, data);
}

void vsp1_dl_set(struct vsp1_device *vsp1, u32 reg, u32 data)
{
	struct vsp1_dl *dl = vsp1->dl;
	struct dl_body *body = dl->setting_body;

	if (body == NULL)
		return;

	body->dlist[body->reg_count].set_address = reg;
	body->dlist[body->reg_count].set_data = data;
	body->reg_count++;
}

int vsp1_dl_setup_control(struct vsp1_device *vsp1, int mode, int repeat)
{
	struct vsp1_dl *dl = vsp1->dl;
	int _mode, _repeat;

	if (dl->active)
		return -EBUSY;

	dl->repeat = DL_REPEAT_NONE;
	dl->mode = DL_NOT_USE;

	switch (mode) {
	case DL_NOT_USE:
		return 0;

	case DL_HEADER_MODE:
	case DL_HEADER_LESS:
		_mode = mode;
		break;

	default:
		return -EINVAL;
	}

	switch (repeat) {
	case DL_REPEAT_NONE:
	case DL_REPEAT_MANUAL:
	case DL_REPEAT_AUTO:
		_repeat = repeat;
		break;

	default:
		return -EINVAL;
	}

	dl->repeat = _repeat;
	dl->mode = _mode;

	return 0;
}

static void dl_set_control(struct vsp1_dl *dl)
{
	unsigned long dl_ctrl = (256 << VI6_DL_CTRL_AR_WAIT_SHIFT) |
				 VI6_DL_CTRL_DC2 | VI6_DL_CTRL_DC1 |
				 VI6_DL_CTRL_DC0 | VI6_DL_CTRL_DLE;

	if (dl->active)
		return;

#if 0
	dl_write(dl, VI6_WPF_IRQ_STA(0), 0);
	dl_write(dl, VI6_DISP_IRQ_STA, 0);
#endif

	if (dl->mode == DL_HEADER_LESS) {
		dl_ctrl |= VI6_DL_CTRL_NH0;

		if (dl->repeat == DL_REPEAT_AUTO)
			dl_ctrl |= VI6_DL_CTRL_CFM0;
#if 0
		dl_write(dl, VI6_WPF_IRQ_ENB(0),
				VI6_WFP_IRQ_ENB_FREE | VI6_WFP_IRQ_ENB_DFEE);
		dl_write(dl, VI6_DISP_IRQ_ENB, 0);
	} else {
		dl_write(dl, VI6_WPF_IRQ_ENB(0), VI6_WFP_IRQ_ENB_DFEE);
		dl_write(dl, VI6_DISP_IRQ_ENB, VI6_DISP_IRQ_STA_DST);
#endif
	}

	/* DL control */
	dl_write(dl, VI6_DL_CTRL, dl_ctrl);

	/* DL LWORD swap */
	dl_write(dl, VI6_DL_SWAP, VI6_DL_SWAP_LWS);
}

void vsp1_dl_reset(struct vsp1_device *vsp1)
{
	struct vsp1_dl *dl = vsp1->dl;
	int i, j;

	dl->active = false;
	dl->flag = 0;

	dl->setting_header = NULL;
	dl->setting_body = NULL;
	dl->active_header = NULL;
	dl->next_header = NULL;
	dl->active_body = NULL;
	dl->next_body = NULL;
	dl->pending_body = NULL;

	for (i = 0; i < DISPLAY_LIST_NUM; i++) {
		dl->head[i].use = DL_MEM_NO_USE;

		for (j = 0; j < DISPLAY_LIST_BODY_NUM; j++)
			dl->body[i][j].use = DL_MEM_NO_USE;

		dl->single_body[i].use = DL_MEM_NO_USE;
	}
}


int vsp1_dl_is_use(struct vsp1_device *vsp1)
{
	struct vsp1_dl *dl = vsp1->dl;

	switch (dl->mode) {
	case DL_HEADER_MODE:
	case DL_HEADER_LESS:
		return dl->mode;
	}

	return 0;
}

int vsp1_dl_is_auto_repeat(struct vsp1_device *vsp1)
{
	struct vsp1_dl *dl = vsp1->dl;

	switch (dl->repeat) {
	case DL_REPEAT_AUTO:
		return 1;
	}

	return 0;
}

static inline void dl_free_body(struct dl_body *body)
{
	if (body == NULL)
		return;

	body->use = DL_MEM_NO_USE;
}

static inline void dl_free_header(struct dl_head *head)
{
	int i;

	if (head == NULL)
		return;

	/* in header mode, dl_head->next is NULL */
	for (i = 0; i < DISPLAY_LIST_BODY_NUM; i++)
		if (head->dl_body_list[i] != NULL)
			head->dl_body_list[i]->use = DL_MEM_NO_USE;

	head->use = DL_MEM_NO_USE;
	/* head->next = NULL; */
}

static int dl_header_mode_dl(struct vsp1_dl *dl, int module)
{
	struct dl_head *head = NULL;
	struct dl_body *body = NULL;
	int i;
	unsigned long flags;

	switch (module) {
	case DL_BODY_RPF0:
	case DL_BODY_RPF1:
	case DL_BODY_RPF2:
	case DL_BODY_RPF3:
	case DL_BODY_RPF4:
	case DL_BODY_WPF:
	case DL_BODY_BRU:
	case DL_BODY_DPR:
		break;
	default:
		return -EINVAL;
	}

	spin_lock_irqsave(&dl->lock, flags);
	if (dl->setting_header == NULL) {
		for (i = 0; i < DISPLAY_LIST_NUM; i++) {
			if (dl->head[i].use == DL_MEM_NO_USE) {
				head = &dl->head[i];
				head->use = DL_MEM_USE;
				break;
			}
		}

		if (head == NULL) {
			spin_unlock_irqrestore(&dl->lock, flags);
			return -ENOMEM;
		}

		for (i = 0; i < DISPLAY_LIST_BODY_NUM; i++)
			head->dl_body_list[i] = NULL;

		dl->setting_header = head;
	} else {
		head = dl->setting_header;
	}

	if (head->dl_body_list[module] == NULL) {
		for (i = 0; i < DISPLAY_LIST_NUM; i++) {
			if (dl->body[i][module].use == DL_MEM_NO_USE) {
				body = &dl->body[i][module];
				body->use = DL_MEM_USE;
				break;
			}
		}

		if (body == NULL) {
			spin_unlock_irqrestore(&dl->lock, flags);
			return -ENOMEM;
		}

		body->reg_count = 0;

		head->dl_body_list[module] = body;
		dl->setting_body = body;
	} else {
		dl->setting_body = head->dl_body_list[module];
	}


	spin_unlock_irqrestore(&dl->lock, flags);

	return 0;
}


static int dl_header_less_body_get(struct vsp1_dl *dl)
{
	struct dl_body *body = NULL;
	int i;
	unsigned long flags;

	if (dl->setting_body != NULL)
		return 0;

	spin_lock_irqsave(&dl->lock, flags);

	for (i = 0; i < DISPLAY_LIST_NUM; i++) {
		if (dl->single_body[i].use == DL_MEM_NO_USE) {
			body = &dl->single_body[i];
			body->use = DL_MEM_USE;
			break;
		}
	}

	spin_unlock_irqrestore(&dl->lock, flags);

	if (body == NULL)
		return -ENOMEM;

	body->reg_count = 0;

	dl->setting_body = body;

	return 0;
}

int vsp1_dl_get(struct vsp1_device *vsp1, int module)
{
	struct vsp1_dl *dl = vsp1->dl;

	switch (dl->mode) {
	case DL_HEADER_MODE:
		return dl_header_mode_dl(dl, module);

	case DL_HEADER_LESS:
		return dl_header_less_body_get(dl);
	}

	return 0;
}

static int dl_irq_dl_frame_end_header_mode(struct vsp1_dl *dl)
{
	struct dl_head *free_head = NULL;

	spin_lock(&dl->lock);

	switch (dl->repeat) {
	case DL_REPEAT_MANUAL:
	case DL_REPEAT_AUTO:
		if (dl->next_header == NULL)
			break;

		/* update next frame & free old Display List */
		free_head = dl->active_header;
		dl->active_header = dl->next_header;
		dl->next_header = NULL;
		dl_write(dl, VI6_DL_HDR_ADDR(0),
				dl->active_header->paddr);
		break;

	case DL_REPEAT_NONE:
		free_head = dl->active_header;
		dl->active_header = NULL;
		break;
	}

	spin_unlock(&dl->lock);

	dl_free_header(free_head);

	return 0;
}

static int dl_irq_dl_frame_end_header_less(struct vsp1_dl *dl)
{
	struct dl_body *free_body = NULL;

	spin_lock(&dl->lock);

	dl->flag &= ~DL_FLAG_BODY_WRITEBLE;

	switch (dl->repeat) {
	case DL_REPEAT_MANUAL:
	case DL_REPEAT_AUTO:
		if (dl->next_body == NULL)
			break;

		/* free old Display List */
		free_body = dl->active_body;
		dl->active_body = dl->next_body;
		dl->next_body = NULL;
		break;

	case DL_REPEAT_NONE:
		free_body = dl->active_body;
		dl->active_body = NULL;
		break;
	}

	dl_free_body(free_body);

	spin_unlock(&dl->lock);

	return 0;
}

int vsp1_dl_irq_dl_frame_end(struct vsp1_device *vsp1)
{
	struct vsp1_dl *dl = vsp1->dl;

	switch (dl->mode) {
	case DL_HEADER_MODE:
		return dl_irq_dl_frame_end_header_mode(dl);

	case DL_HEADER_LESS:
		return dl_irq_dl_frame_end_header_less(dl);
	}

	return 0;
}


int vsp1_dl_irq_display_start(struct vsp1_device *vsp1)
{
	struct vsp1_dl *dl = vsp1->dl;
	struct dl_body *next_body = NULL;
	struct dl_body *free_body = NULL;

	if (dl->mode != DL_HEADER_LESS)
		return 0;

	spin_lock(&dl->lock);

	dl->flag |= DL_FLAG_BODY_WRITEBLE;

	if (dl->pending_body) {
		/* update next frame for pending. */
		free_body = dl->next_body;
		next_body = dl->next_body = dl->pending_body;
		dl->pending_body = NULL;
	}

	dl_free_body(free_body);

	spin_unlock(&dl->lock);

	if (next_body) {
		dl_write(dl, VI6_DL_HDR_ADDR(0), next_body->paddr);
		dl_write(dl, VI6_DL_BODY_SIZE,
			(next_body->reg_count * 8) | VI6_DL_BODY_SIZE_UPD);
	}

	return 0;
}

static int dl_header_setup(struct vsp1_dl *dl, struct dl_head *head)
{
	int i;
	struct display_header *dheader = head->dheader;
	int dl_count = 0;

	memset(dheader, 0, sizeof(*dheader));

	for (i = 0; i < DISPLAY_LIST_BODY_NUM; i++) {
		struct dl_body *dl_body_list = head->dl_body_list[i];

		if (dl_body_list == NULL)
			continue;

		dheader->display_list[dl_count].num_bytes =
			(dl_body_list->reg_count * 8) << 0;
		dheader->display_list[dl_count].plist = dl_body_list->paddr;
		dl_count++;
	}

	for (i = dl_count; i < DISPLAY_LIST_BODY_NUM; i++) {
		dheader->display_list[i].num_bytes = 0;
		dheader->display_list[i].plist = 0;
	}

	/* display list body num */
	dheader->num_list_minus1 = (dl_count - 1) << 0;

	dheader->pnext_header = (unsigned long)head->paddr;

	/* Enable Display List interrupt */
	dheader->int_auto = 1 << 1;

	/* Enable auto repeat mode */
	if (dl->repeat == DL_REPEAT_AUTO)
		dheader->int_auto |= 1 << 0;

	return 0;
}


/* TODO:Auto repeat mode */
static void dl_set_header_mode(struct vsp1_dl *dl)
{
	struct dl_head *head = dl->setting_header;
	unsigned long flags;

	if (head == NULL)
		return;

	dl_header_setup(dl, head);

	dl_set_control(dl);

	/* Set Display Header address */
	if (!dl->active)
		dl_write(dl, VI6_DL_HDR_ADDR(0), head->paddr);

	spin_lock_irqsave(&dl->lock, flags);
	dl->setting_header = NULL;
	dl->setting_body = NULL;
	if (dl->active) {
		dl->next_header = head;
	} else {
		dl->active_header = head;
		dl->active = true;
	}
	spin_unlock_irqrestore(&dl->lock, flags);
}

static void dl_set_header_less(struct vsp1_dl *dl)
{
	struct dl_body *body = dl->setting_body;
	unsigned long stat;
	bool write_enable = true;
	unsigned long flags;

	if (body == NULL)
		return;

	if (!dl->active) {
		/* stream start */
		dl->flag |= DL_FLAG_BODY_WRITEBLE;
	} else if (dl->flag & DL_FLAG_BODY_WRITEBLE) {
		stat = dl_read(dl, VI6_DL_BODY_SIZE);
		if (!(stat & VI6_DL_BODY_SIZE_UPD) &&
		     (dl->next_body != NULL)) {
			write_enable = false;
		}
	} else {
		write_enable = false;
	}

	if (!write_enable) {
		spin_lock_irqsave(&dl->lock, flags);
		dl_free_body(dl->pending_body);
		dl->setting_body = NULL;
		dl->pending_body = body;
		spin_unlock_irqrestore(&dl->lock, flags);
		return;
	}

	dl_set_control(dl);

	/* Set Single Display List Body address & size */
	dl_write(dl, VI6_DL_HDR_ADDR(0), body->paddr);
	dl_write(dl, VI6_DL_BODY_SIZE,
			(body->reg_count * 8) | VI6_DL_BODY_SIZE_UPD);

	spin_lock_irqsave(&dl->lock, flags);
	dl->setting_body = NULL;
	dl_free_body(dl->next_body);
	if (dl->active) {
		dl->next_body = body;
	} else {
		dl->active_body = body;
		dl->active = true;
	}
	spin_unlock_irqrestore(&dl->lock, flags);
}

int vsp1_dl_set_stream(struct vsp1_device *vsp1)
{
	struct vsp1_dl *dl = vsp1->dl;

	switch (dl->mode) {
	case DL_HEADER_MODE:
		dl_set_header_mode(dl);
		break;

	case DL_HEADER_LESS:
		dl_set_header_less(dl);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}


static void dl_config(struct vsp1_dl *dl)
{
	int i, j;
	unsigned long offset = 0;
	int dl_header_size = 76;
	int dl_body_size[DISPLAY_LIST_BODY_NUM] = {
		/* RPF0 */ 256,
		/* RPF1 */ 256,
		/* RPF2 */ 256,
		/* RPF3 */ 256,
		/* RPF4 */ 256,
		/* WPF */ 256,
		/* BRU */ 256,
		/* DPR */ 256,
	};

	offset = (((dl->paddr + 15) / 16)) * 16 - dl->paddr;

	/* header config */
	for (i = 0; i < DISPLAY_LIST_NUM; i++) {
		struct dl_head *head = &dl->head[i];

		head->size = dl_header_size;
		head->use = DL_MEM_NO_USE;
		head->paddr = dl->paddr + offset;
		head->dheader = dl->vaddr + offset;
		head->dheader_offset = offset;
		/* head->next = NULL; */
		for (j = 0; j < DISPLAY_LIST_BODY_NUM; j++)
			head->dl_body_list[j] = NULL;

		offset += ((((head->size + 15) / 16)) * 16);
	}

	/* body config */
	for (i = 0; i < DISPLAY_LIST_NUM; i++) {
		for (j = 0; j < DISPLAY_LIST_BODY_NUM; j++) {
			struct dl_body *body = &(dl->body[i][j]);

			body->size = dl_body_size[j];
			body->reg_count = 0;
			body->use = DL_MEM_NO_USE;
			body->paddr = dl->paddr + offset;
			body->dlist = dl->vaddr + offset;
			body->dlist_offset = offset;

			offset += ((((body->size + 7) / 8)) * 8);
		}
	}

	if (dl->size < offset)
		pr_warn("[Warning] display list size over\n");

	/* header less body config */
	for (i = 0; i < DISPLAY_LIST_NUM; i++) {
		struct dl_body *single_body = &dl->single_body[i];

		single_body->size = DL_MEM_SIZE;
		single_body->reg_count = 0;
		single_body->use = DL_MEM_NO_USE;
		single_body->paddr = dl->paddr + DL_MEM_SIZE * i;
		single_body->dlist = dl->vaddr + DL_MEM_SIZE * i;
		single_body->dlist_offset = DL_MEM_SIZE * i;
	}

}

int vsp1_dl_create(struct vsp1_device *vsp1)
{
	struct vsp1_dl *dl;

	vsp1->dl = devm_kzalloc(vsp1->dev, sizeof(*vsp1->dl), GFP_KERNEL);
	if (!vsp1->dl)
		return -ENOMEM;

	dl = vsp1->dl;

	dl->vaddr = dma_alloc_writecombine(vsp1->dev,
			DL_MEM_SIZE * DISPLAY_LIST_NUM,
			&dl->paddr, GFP_KERNEL);
	if (!dl->vaddr)
		return -ENOMEM;

	dl->size = DL_MEM_SIZE * DISPLAY_LIST_NUM;

	dl->setting_header = NULL;
	dl->setting_body = NULL;

	dl->active_header = NULL;
	dl->next_header = NULL;

	dl->active_body = NULL;
	dl->next_body = NULL;
	dl->pending_body = NULL;

	dl->vsp1 = vsp1;
	spin_lock_init(&dl->lock);
	dl->flag = 0;
	dl->active = false;
	dl->repeat = DL_REPEAT_NONE;
	dl->mode = DL_NOT_USE;

	dl_config(dl);

	return 0;
}
#endif /* VSP1_DL_SUPPORT */

