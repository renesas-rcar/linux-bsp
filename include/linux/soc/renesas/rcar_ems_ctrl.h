/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __RCAR_EMS_CTRL_H__
#define __RCAR_EMS_CTRL_H__

#define RCAR_EMS_MODE_OFF       0
#define RCAR_EMS_MODE_ON        1

extern int register_rcar_ems_notifier(struct notifier_block *nb);
extern void unregister_rcar_ems_notifier(struct notifier_block *nb);
extern int rcar_ems_get_mode(void);

#endif /* __RCAR_EMS_CTRL_H__  */
