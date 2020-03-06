#ifndef __RCAR_MFIS_PUBLIC_H__
#define __RCAR_MFIS_PUBLIC_H__

struct rcar_mfis_msg {
	u32 icr;
	u32 mbr;
};

int rcar_mfis_trigger_interrupt(int channel, struct rcar_mfis_msg msg);
int rcar_mfis_register_notifier(int channel, struct notifier_block *nb, void *data);
int rcar_mfis_unregister_notifier(int channel, struct notifier_block *nb);

#endif
