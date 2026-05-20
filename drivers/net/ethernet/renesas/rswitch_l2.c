// SPDX-License-Identifier: GPL-2.0
/* Renesas Ethernet Switch device driver
 *
 * Copyright (C) 2025 - 2026 Renesas Electronics Corporation
 */

#include <linux/err.h>
#include <linux/etherdevice.h>
#include <linux/if_bridge.h>
#include <linux/if_vlan.h>
#include <linux/kernel.h>
#include <net/switchdev.h>

#include "rswitch.h"
#include "rswitch_l2.h"

static bool rdev_for_l2_offload(struct rswitch_device *rdev)
{
	return rdev->priv->offload_brdev &&
	       rdev->brdev == rdev->priv->offload_brdev &&
	       (test_bit(rdev->port, rdev->priv->opened_ports));
}

static void rswitch_change_l2_hw_offloading(struct rswitch_device *rdev,
					    bool start, bool learning)
{
	u32 bits = learning ? MACSSA | MACHLA | MACHMA | MACRUDA : MACDSA;
	u32 clear = start ? 0 : bits;
	u32 set = start ? bits : 0;

	if ((learning && rdev->learning_offloaded == start) ||
	    (!learning && rdev->forwarding_offloaded == start))
		return;

	rswitch_modify(rdev->priv->addr_mfwd, FWPC0(rdev->port), clear, set);

	if (learning)
		rdev->learning_offloaded = start;
	else
		rdev->forwarding_offloaded = start;

	netdev_info(rdev->ndev, "%s hw %s\n", start ? "starting" : "stopping",
		    learning ? "learning" : "forwarding");
}

static void rswitch_update_l2_hw_learning(struct rswitch_private *priv)
{
	struct rswitch_device *rdev;
	bool learning_needed;

	rswitch_for_all_ports(priv, rdev) {
		if (rdev_for_l2_offload(rdev))
			learning_needed = rdev->learning_requested;
		else
			learning_needed = false;

		rswitch_change_l2_hw_offloading(rdev, learning_needed, true);
	}
}

static void rswitch_update_l2_hw_forwarding(struct rswitch_private *priv)
{
	u8 num_agents = priv->info->rswitch_num_agents;
	struct rswitch_device *rdev;
	bool new_forwarding_offload;
	unsigned int fwd_mask;

	/* calculate fwd_mask with zeroes in bits corresponding to ports that
	 * shall participate in hardware forwarding
	 */
	fwd_mask = GENMASK(num_agents - 1, 0);

	rswitch_for_all_ports(priv, rdev) {
		if (rdev_for_l2_offload(rdev) && rdev->forwarding_requested)
			fwd_mask &= ~BIT(rdev->port);
	}

	rswitch_for_all_ports(priv, rdev) {
		new_forwarding_offload = (rdev_for_l2_offload(rdev) && rdev->forwarding_requested);

		if (new_forwarding_offload || rdev->forwarding_offloaded) {
			/* Update allowed offload destinations even for ports
			 * with L2 offload enabled earlier.
			 *
			 * Do not allow L2 forwarding to self for hw port.
			 */
			rswitch_modify(priv->addr_mfwd, FWPC2(rdev->port),
				       field_prep(LTWFM(num_agents), ~(fwd_mask | BIT(rdev->port))),
				       0);
		}

		if (new_forwarding_offload && !rdev->forwarding_offloaded)
			rswitch_change_l2_hw_offloading(rdev, true, false);
		else if (!new_forwarding_offload && rdev->forwarding_offloaded)
			rswitch_change_l2_hw_offloading(rdev, false, false);
	}
}

static void rswitch_update_l2_hw_forwarding_gwca(struct rswitch_private *priv)
{
	u8 num_agents = priv->info->rswitch_num_agents;
	u32 fwpc0_set, fwpc0_clr, fwpc2_set, fwpc2_clr;
	struct rswitch_device *rdev;

	fwpc0_clr = MACSSA | MACDSA | MACRUDA;
	fwpc0_set = fwpc0_clr;
	fwpc2_clr = field_prep(LTWFM(num_agents), BIT(AGENT_INDEX_GWCA));
	fwpc2_set = fwpc2_clr;

	(priv->offload_brdev) ? (fwpc0_clr = 0, fwpc2_set = 0)
			      : (fwpc0_set = 0, fwpc2_clr = 0);

	rswitch_modify(priv->addr_mfwd, FWPC0(AGENT_INDEX_GWCA), fwpc0_clr, fwpc0_set);

	rswitch_for_all_ports(priv, rdev) {
		rswitch_modify(priv->addr_mfwd, FWPC2(rdev->etha->index),
			       fwpc2_clr, fwpc2_set);
	}
}

void rswitch_update_l2_offload(struct rswitch_private *priv)
{
	rswitch_update_l2_hw_learning(priv);
	rswitch_update_l2_hw_forwarding(priv);
	rswitch_update_l2_hw_forwarding_gwca(priv);
}

static void rswitch_update_offload_brdev(struct rswitch_private *priv)
{
	struct net_device *offload_brdev = NULL;
	struct rswitch_device *rdev, *rdev2;

	rswitch_for_all_ports(priv, rdev) {
		if (!rdev->brdev)
			continue;
		rswitch_for_all_ports(priv, rdev2) {
			if (rdev2 == rdev)
				break;
			if (rdev2->brdev == rdev->brdev) {
				offload_brdev = rdev->brdev;
				break;
			}
		}
		if (offload_brdev)
			break;
	}

	if (offload_brdev == priv->offload_brdev)
		dev_dbg(&priv->pdev->dev,
			"changing l2 offload from %s to %s\n",
			netdev_name(priv->offload_brdev),
			netdev_name(offload_brdev));
	else if (offload_brdev)
		dev_dbg(&priv->pdev->dev, "starting l2 offload for %s\n",
			netdev_name(offload_brdev));
	else if (!offload_brdev)
		dev_dbg(&priv->pdev->dev, "stopping l2 offload for %s\n",
			netdev_name(priv->offload_brdev));

	priv->offload_brdev = offload_brdev;

	rswitch_update_l2_offload(priv);
}

static void rswitch_port_update_brdev(struct net_device *ndev,
				      struct net_device *brdev)
{
	struct rswitch_device *rdev;

	if (!is_rdev(ndev))
		return;

	rdev = netdev_priv(ndev);
	rdev->brdev = brdev;
	rswitch_update_offload_brdev(rdev->priv);
}

static int rswitch_netdevice_event(struct notifier_block *nb,
				   unsigned long event,
				   void *ptr)
{
	struct net_device *ndev = netdev_notifier_info_to_dev(ptr);
	struct netdev_notifier_changeupper_info *info;
	struct net_device *brdev;

	if (!is_rdev(ndev))
		return NOTIFY_DONE;
	if (event != NETDEV_CHANGEUPPER)
		return NOTIFY_DONE;

	info = ptr;

	if (netif_is_bridge_master(info->upper_dev)) {
		brdev = info->linking ? info->upper_dev : NULL;
		rswitch_port_update_brdev(ndev, brdev);
	}

	return NOTIFY_OK;
}

static int rswitch_port_update_stp_state(struct net_device *ndev, u8 stp_state)
{
	struct rswitch_device *rdev;

	if (!is_rdev(ndev))
		return -ENODEV;

	rdev = netdev_priv(ndev);
	rdev->learning_requested = (stp_state == BR_STATE_LEARNING ||
				    stp_state == BR_STATE_FORWARDING);
	rdev->forwarding_requested = (stp_state == BR_STATE_FORWARDING);
	rswitch_update_l2_offload(rdev->priv);

	return 0;
}

static int rswitch_update_ageing_time(struct rswitch_private *priv, clock_t time)
{
	u32 reg_val;

	if (!FIELD_FIT(MACAGT, time))
		return -EINVAL;

	reg_val = FIELD_PREP(MACAGT, time);
	reg_val |= MACAGE | MACAGSL;
	iowrite32(reg_val, priv->addr_mfwd + FWMACAGC);

	return 0;
}

static void rswitch_update_vlan_filtering(struct rswitch_private *priv,
					  bool vlan_filtering)
{
	if (vlan_filtering)
		rswitch_modify(priv->addr_mfwd, FWPC0(AGENT_INDEX_GWCA),
			       0, VLANSA | VLANRU);
	else
		rswitch_modify(priv->addr_mfwd, FWPC0(AGENT_INDEX_GWCA),
			       VLANSA | VLANRU, 0);
}

static int rswitch_handle_port_attr_set(struct net_device *ndev,
					struct notifier_block *nb,
					struct switchdev_notifier_port_attr_info *info)
{
	const struct switchdev_attr *attr = info->attr;
	struct rswitch_private *priv;
	int err = 0;

	priv = container_of(nb, struct rswitch_private, rswitch_switchdev_blocking_nb);

	switch (attr->id) {
	case SWITCHDEV_ATTR_ID_PORT_STP_STATE:
		err = rswitch_port_update_stp_state(ndev, attr->u.stp_state);

		break;
	case SWITCHDEV_ATTR_ID_BRIDGE_AGEING_TIME:
		err = rswitch_update_ageing_time(priv, attr->u.ageing_time);

		break;
	case SWITCHDEV_ATTR_ID_BRIDGE_VLAN_FILTERING:
		rswitch_update_vlan_filtering(priv, attr->u.vlan_filtering);

		break;
	case SWITCHDEV_ATTR_ID_BRIDGE_MC_DISABLED:

		break;
	default:
		return -EOPNOTSUPP;
	}

	if (err < 0)
		return err;

	info->handled = true;

	return NOTIFY_DONE;
}

static int rswitch_read_vlan_table(struct rswitch_private *priv, u16 vid,
				   u32 *vlanslvs, u32 *vlandvs)
{
	int err;

	iowrite32(FIELD_PREP(VLANVIDS, vid), priv->addr_mfwd + FWVLANTS);
	err = rswitch_reg_wait(priv->addr_mfwd, FWVLANTSR0, VLANTS, 0);
	if (err < 0)
		return err;

	/* check if vlans are present in table */
	if (!(ioread32(priv->addr_mfwd + FWVLANTSR0) & VLANSNF)) {
		*vlanslvs = (ioread32(priv->addr_mfwd + FWVLANTSR1) & VLANSLVS);
		*vlandvs = (ioread32(priv->addr_mfwd + FWVLANTSR3) & VLANDVS);
	}

	return 0;
}

static int rswitch_write_vlan_table(struct rswitch_private *priv, u16 vid, u32 index)
{
	u32 vlancsdl = priv->gwca.l2_shared_rx_queue->index;
	u32 vlanslvs = 0, vlandvs = 0;
	int err;

	err = rswitch_read_vlan_table(priv, vid, &vlanslvs, &vlandvs);
	if (err < 0)
		return err;

	rswitch_modify(priv->addr_mfwd, FWVLANTL0, VLANED, 0);
	iowrite32(FIELD_PREP(VLANVIDL, vid), priv->addr_mfwd + FWVLANTL1);

	vlanslvs |= BIT(index);
	vlandvs  |= BIT(index);
	iowrite32(FIELD_PREP(VLANSLVL, vlanslvs), priv->addr_mfwd + FWVLANTL2);
	iowrite32(FIELD_PREP(VLANCSDL, vlancsdl), priv->addr_mfwd + FWVLANTL3(GWCA_INDEX));
	iowrite32(FIELD_PREP(VLANDVL, vlandvs), priv->addr_mfwd + FWVLANTL4);

	return rswitch_reg_wait(priv->addr_mfwd, FWVLANTLR, VLANTL, 0);
}

static int rswitch_erase_vlan_table(struct rswitch_private *priv, u16 vid, u32 index)
{
	u32 vlanslvs = 0, vlandvs = 0;
	int err;

	err = rswitch_read_vlan_table(priv, vid, &vlanslvs, &vlandvs);
	if (err < 0)
		return err;

	vlanslvs &= ~BIT(index);
	vlandvs  &= ~BIT(index);

	/* only erase empty vlan table entries */
	if (vlanslvs == 0)
		rswitch_modify(priv->addr_mfwd, FWVLANTL0, 0, VLANED);

	iowrite32(FIELD_PREP(VLANVIDL, vid), priv->addr_mfwd + FWVLANTL1);
	iowrite32(FIELD_PREP(VLANSLVL, vlanslvs), priv->addr_mfwd + FWVLANTL2);
	iowrite32(FIELD_PREP(VLANDVL, vlandvs), priv->addr_mfwd + FWVLANTL4);

	return rswitch_reg_wait(priv->addr_mfwd, FWVLANTLR, VLANTL, 0);
}

static int rswitch_port_set_vlan_tag(struct rswitch_etha *etha,
				     struct switchdev_obj_port_vlan *p_vlan,
				     bool delete)
{
	u32 vem_val;
	int err;

	err = rswitch_etha_change_mode(etha, OPC_CONFIG);
	if (err < 0)
		return err;

	rswitch_modify(etha->addr, EAVCC, VIM, 0);

	if (((ioread32(etha->addr + EAVTC) & CTV) == p_vlan->vid) && delete) {
		rswitch_modify(etha->addr, EAVTC, CTV, 0);
		rswitch_modify(etha->addr, EAVCC, VEM, 0);
	} else if (!delete) {
		if ((p_vlan->flags & BRIDGE_VLAN_INFO_PVID) &&
		    (p_vlan->flags & BRIDGE_VLAN_INFO_UNTAGGED))
			vem_val = FIELD_PREP(VEM, C_TAG_VLAN);
		else if (p_vlan->flags & BRIDGE_VLAN_INFO_PVID)
			vem_val = FIELD_PREP(VEM, HW_C_TAG_VLAN);
		else
			vem_val = 0;
		rswitch_modify(etha->addr, EAVCC, VEM, vem_val);
		rswitch_modify(etha->addr, EAVTC, CTV, FIELD_PREP(CTV, p_vlan->vid));
	}

	return rswitch_etha_change_mode(etha, OPC_OPERATION);
}

static int rswitch_gwca_set_vlan_tag(struct rswitch_private *priv,
				     struct switchdev_obj_port_vlan *p_vlan,
				     bool delete)
{
	u32 vem_val;
	int err;

	err = rswitch_gwca_change_mode(priv, OPC_CONFIG);
	if (err < 0)
		return err;

	rswitch_modify(priv->addr_gwca, GWVCC, VIM, 0);

	if (((ioread32(priv->addr_gwca + GWVTC) & CTV) == p_vlan->vid) && delete) {
		rswitch_modify(priv->addr_gwca, GWVTC, CTV, 0);
		rswitch_modify(priv->addr_gwca, GWVCC, VEM, 0);
	} else  if (!delete) {
		if ((p_vlan->flags & BRIDGE_VLAN_INFO_PVID) &&
		    (p_vlan->flags & BRIDGE_VLAN_INFO_UNTAGGED))
			vem_val = FIELD_PREP(VEM, C_TAG_VLAN);
		else if (p_vlan->flags & BRIDGE_VLAN_INFO_PVID)
			vem_val = FIELD_PREP(VEM, HW_C_TAG_VLAN);
		else
			vem_val = 0;
		rswitch_modify(priv->addr_gwca, GWVCC, VEM, vem_val);
		rswitch_modify(priv->addr_gwca, GWVTC, CTV, FIELD_PREP(CTV, p_vlan->vid));
	}

	return rswitch_gwca_change_mode(priv, OPC_OPERATION);
}

static int rswitch_port_obj_do_add(struct net_device *ndev,
				   struct switchdev_obj_port_vlan *p_vlan)
{
	struct rswitch_device *rdev = netdev_priv(ndev);
	struct rswitch_private *priv = rdev->priv;
	struct rswitch_etha *etha = rdev->etha;
	u8 num_agents;
	int err;

	num_agents = priv->info->rswitch_num_agents;

	/* Set Rswitch VLAN mode */
	iowrite32(br_vlan_enabled(rdev->brdev) ? FIELD_PREP(SVM, C_TAG) : 0,
		  priv->addr_mfwd + FWGC);

	err = rswitch_write_vlan_table(priv, p_vlan->vid, etha->index);
	if (err < 0)
		return err;

	/* If the default vlan for this port has been set, don't overwrite it. */
	if (ioread32(etha->addr + EAVCC))
		return NOTIFY_DONE;

	if (br_vlan_enabled(rdev->brdev))
		rswitch_modify(priv->addr_mfwd, FWPC0(etha->index), 0, VLANSA | VLANRU);

	rswitch_modify(priv->addr_mfwd, FWPC2(AGENT_INDEX_GWCA),
		       field_prep(LTWFM(num_agents), BIT(etha->index)),
		       0);

	return rswitch_port_set_vlan_tag(etha, p_vlan, false);
}

static int rswitch_port_obj_do_add_gwca(struct net_device *ndev,
					struct rswitch_private *priv,
					struct switchdev_obj_port_vlan *p_vlan)
{
	int err;

	if (!(p_vlan->flags & BRIDGE_VLAN_INFO_BRENTRY))
		return NOTIFY_DONE;

	/* Set Rswitch VLAN mode */
	iowrite32(br_vlan_enabled(ndev) ? FIELD_PREP(SVM, C_TAG) : 0, priv->addr_mfwd + FWGC);

	err = rswitch_write_vlan_table(priv, p_vlan->vid, AGENT_INDEX_GWCA);
	if (err < 0)
		return err;

	/* If the default vlan for this port has been set, don't overwrite it. */
	if (ioread32(priv->addr_gwca + GWVCC))
		return NOTIFY_DONE;

	return rswitch_gwca_set_vlan_tag(priv, p_vlan, false);
}

static int rswitch_port_obj_do_del(struct net_device *ndev,
				   struct switchdev_obj_port_vlan *p_vlan)
{
	struct rswitch_device *rdev = netdev_priv(ndev);
	struct rswitch_private *priv = rdev->priv;
	struct rswitch_etha *etha = rdev->etha;
	u8 num_agents;
	int err;

	num_agents = priv->info->rswitch_num_agents;

	err = rswitch_port_set_vlan_tag(etha, p_vlan, true);
	if (err < 0)
		return err;

	rswitch_modify(priv->addr_mfwd, FWPC0(etha->index), VLANSA | VLANRU, 0);
	rswitch_modify(priv->addr_mfwd, FWPC2(AGENT_INDEX_GWCA), 0,
		       field_prep(LTWFM(num_agents), BIT(etha->index)));
	rswitch_modify(priv->addr_mfwd, FWPC2(rdev->port),
		       0, field_prep(LTWFM(num_agents), GENMASK(num_agents - 1, 0)));

	return rswitch_erase_vlan_table(priv, p_vlan->vid, etha->index);
}

static int rswitch_port_obj_do_del_gwca(struct net_device *ndev,
					struct rswitch_private *priv,
					struct switchdev_obj_port_vlan *p_vlan)
{
	int err;

	err = rswitch_gwca_set_vlan_tag(priv, p_vlan, true);
	if (err < 0)
		return err;

	rswitch_modify(priv->addr_mfwd, FWPC0(AGENT_INDEX_GWCA), VLANSA | VLANRU, 0);

	return rswitch_erase_vlan_table(priv, p_vlan->vid, AGENT_INDEX_GWCA);
}

static int rswitch_handle_port_obj_add(struct net_device *ndev,
				       struct notifier_block *nb,
				       struct switchdev_notifier_port_obj_info *info)
{
	struct switchdev_obj_port_vlan *p_vlan;
	struct rswitch_private *priv;
	int err;

	switch (info->obj->id) {
	case SWITCHDEV_OBJ_ID_PORT_VLAN:
		p_vlan = SWITCHDEV_OBJ_PORT_VLAN(info->obj);
		if ((p_vlan->flags & BRIDGE_VLAN_INFO_MASTER) ||
		    (p_vlan->flags & BRIDGE_VLAN_INFO_RANGE_BEGIN) ||
		    (p_vlan->flags & BRIDGE_VLAN_INFO_RANGE_END) ||
		    (p_vlan->flags & BRIDGE_VLAN_INFO_ONLY_OPTS))
			return NOTIFY_DONE;

		priv = container_of(nb, struct rswitch_private,
				    rswitch_switchdev_blocking_nb);

		if (!is_rdev(ndev))
			err = rswitch_port_obj_do_add_gwca(ndev, priv, p_vlan);
		else
			err = rswitch_port_obj_do_add(ndev, p_vlan);

		if (err < 0)
			return err;

		break;
	default:
		return -EOPNOTSUPP;
	}

	info->handled = true;

	return NOTIFY_DONE;
}

static int rswitch_handle_port_obj_del(struct net_device *ndev,
				       struct notifier_block *nb,
				       struct switchdev_notifier_port_obj_info *info)
{
	struct switchdev_obj_port_vlan *p_vlan;
	struct rswitch_private *priv;
	int err;

	switch (info->obj->id) {
	case SWITCHDEV_OBJ_ID_PORT_VLAN:
		p_vlan = SWITCHDEV_OBJ_PORT_VLAN(info->obj);
		if ((p_vlan->flags & BRIDGE_VLAN_INFO_MASTER) ||
		    (p_vlan->flags & BRIDGE_VLAN_INFO_RANGE_BEGIN) ||
		    (p_vlan->flags & BRIDGE_VLAN_INFO_RANGE_END) ||
		    (p_vlan->flags & BRIDGE_VLAN_INFO_ONLY_OPTS))
			return NOTIFY_DONE;

		priv = container_of(nb, struct rswitch_private,
				    rswitch_switchdev_blocking_nb);

		if (!is_rdev(ndev))
			err = rswitch_port_obj_do_del_gwca(ndev, priv, p_vlan);
		else
			err = rswitch_port_obj_do_del(ndev, p_vlan);

		if (err < 0)
			return err;

		break;
	default:
		return -EOPNOTSUPP;
	}

	info->handled = true;

	return NOTIFY_DONE;
}

static int rswitch_switchdev_blocking_event(struct notifier_block *nb,
					    unsigned long event,
					    void *ptr)
{
	struct net_device *ndev = switchdev_notifier_info_to_dev(ptr);
	int err;

	switch (event) {
	case SWITCHDEV_PORT_OBJ_ADD:
		err = rswitch_handle_port_obj_add(ndev, nb, ptr);

		return notifier_from_errno(err);
	case SWITCHDEV_PORT_OBJ_DEL:
		err = rswitch_handle_port_obj_del(ndev, nb, ptr);

		return notifier_from_errno(err);
	case SWITCHDEV_PORT_ATTR_SET:
		err = rswitch_handle_port_attr_set(ndev, nb, ptr);

		return notifier_from_errno(err);
	}

	return NOTIFY_DONE;
}

static int rswitch_gwca_write_mac_address(struct rswitch_private *priv, const u8 *mac)
{
	int err;

	err = rswitch_gwca_change_mode(priv, OPC_CONFIG);
	if (err < 0)
		return err;

	iowrite32((mac[0] << 8) | mac[1], priv->addr_gwca + GWMAC0);
	iowrite32((mac[2] << 24) | (mac[3] << 16) | (mac[4] << 8) | mac[5],
		  priv->addr_gwca + GWMAC1);

	return rswitch_gwca_change_mode(priv, OPC_OPERATION);
}

static int rswitch_add_addr_to_mactable(struct rswitch_private *priv, const u8 *mac)
{
	u32 index = priv->gwca.l2_shared_rx_queue->index;
	u8 num_agents = priv->info->rswitch_num_agents;
	int err;

	rswitch_modify(priv->addr_mfwd, FWMACTL0, MACED, 0);
	iowrite32((mac[0] << 8) | mac[1], priv->addr_mfwd + FWMACTL1);
	iowrite32((mac[2] << 24) | (mac[3] << 16) | (mac[4] << 8) | mac[5],
		  priv->addr_mfwd + FWMACTL2);
	iowrite32(MACDSLVL(num_agents) | MACSSLVL(num_agents),
		  priv->addr_mfwd + FWMACTL3);
	iowrite32(FIELD_PREP(MACCSDL, index),
		  priv->addr_mfwd + FWMACTL4(GWCA_INDEX));
	iowrite32(FIELD_PREP(MACDVL, BIT(AGENT_INDEX_GWCA)),
		  priv->addr_mfwd + FWMACTL5);

	err = rswitch_reg_wait(priv->addr_mfwd, FWMACTLR, MACTL, 0);
	if (err < 0)
		return err;

	if (ioread32(priv->addr_mfwd + FWMACTLR))
		return NOTIFY_BAD;

	return NOTIFY_DONE;
}

static int rswitch_del_addr_from_mactable(struct rswitch_private *priv, const u8 *mac)
{
	u8 num_agents = priv->info->rswitch_num_agents;
	int err;

	rswitch_modify(priv->addr_mfwd, FWMACTL0, 0, MACED);
	iowrite32((mac[0] << 8) | mac[1], priv->addr_mfwd + FWMACTL1);
	iowrite32((mac[2] << 24) | (mac[3] << 16) | (mac[4] << 8) | mac[5],
		  priv->addr_mfwd + FWMACTL2);
	iowrite32(MACDSLVL(num_agents) | MACSSLVL(num_agents),
		  priv->addr_mfwd + FWMACTL3);
	iowrite32(FIELD_PREP(MACCSDL, BIT(0)),
		  priv->addr_mfwd + FWMACTL4(GWCA_INDEX));
	iowrite32(FIELD_PREP(MACDVL, BIT(AGENT_INDEX_GWCA)),
		  priv->addr_mfwd + FWMACTL5);

	err = rswitch_reg_wait(priv->addr_mfwd, FWMACTLR, MACTL, 0);
	if (err < 0)
		return err;

	err = ioread32(priv->addr_mfwd + FWMACTLR);
	if (err)
		return NOTIFY_BAD;

	return NOTIFY_DONE;
}

static void rswitch_switchdev_bridge_fdb_event_work(struct work_struct *work)
{
	struct rswitch_switchdev_event_work *switchdev_work;
	const unsigned char addr[] = {0, 0, 0, 0, 0, 0};
	struct rswitch_device *rdev;
	struct net_device *ndev;

	switchdev_work = container_of(work, struct rswitch_switchdev_event_work, work);
	ndev = switchdev_work->ndev;

	rtnl_lock();

	/* Unfortunately all net_device members point to br0, there is no simple way to check
	 * if the event was triggered by a port device setting.
	 */
	rswitch_for_all_ports(switchdev_work->priv, rdev) {
		if (ether_addr_equal(rdev->ndev->dev_addr, switchdev_work->fdb_info.addr))
			goto out;
	}

	/* Handle only bridge device */
	if (is_rdev(ndev) || !netif_is_bridge_master(ndev))
		goto out;

	switch (switchdev_work->event) {
	case SWITCHDEV_FDB_ADD_TO_DEVICE:
		rswitch_gwca_write_mac_address(switchdev_work->priv, switchdev_work->fdb_info.addr);
		rswitch_add_addr_to_mactable(switchdev_work->priv, switchdev_work->fdb_info.addr);
		break;
	case SWITCHDEV_FDB_DEL_TO_DEVICE:
		rswitch_del_addr_from_mactable(switchdev_work->priv, switchdev_work->fdb_info.addr);
		rswitch_gwca_write_mac_address(switchdev_work->priv, addr);
		break;
	default:
		break;
	}

out:
	rtnl_unlock();

	kfree(switchdev_work->fdb_info.addr);
	rdev = netdev_priv(ndev);
	netdev_put(ndev, &switchdev_work->dev_tracker);
	kfree(switchdev_work);
}

/* called under rcu_read_lock() */
static int rswitch_switchdev_event(struct notifier_block *nb,
				   unsigned long event,
				   void *ptr)
{
	struct net_device *ndev = switchdev_notifier_info_to_dev(ptr);
	struct rswitch_switchdev_event_work *switchdev_work;
	struct switchdev_notifier_fdb_info *fdb_info;
	struct switchdev_notifier_info *info = ptr;
	struct rswitch_private *priv;
	struct rswitch_device *rdev;

	priv = container_of(nb, struct rswitch_private, rswitch_switchdev_nb);

	switch (event) {
	case SWITCHDEV_FDB_ADD_TO_DEVICE:
		fallthrough;
	case SWITCHDEV_FDB_DEL_TO_DEVICE:
		switchdev_work = kzalloc(sizeof(*switchdev_work), GFP_ATOMIC);

		if (!switchdev_work)
			return NOTIFY_BAD;

		switchdev_work->ndev = info->dev;
		switchdev_work->priv = priv;
		switchdev_work->event = event;
		rdev = netdev_priv(switchdev_work->ndev);

		fdb_info = container_of(info,
					struct switchdev_notifier_fdb_info,
					info);

		INIT_WORK(&switchdev_work->work, rswitch_switchdev_bridge_fdb_event_work);

		memcpy(&switchdev_work->fdb_info, ptr, sizeof(switchdev_work->fdb_info));

		switchdev_work->fdb_info.addr = kzalloc(ETH_ALEN, GFP_ATOMIC);
		if (!switchdev_work->fdb_info.addr)
			goto err_addr_alloc;

		priv->switchdev_work = switchdev_work;

		ether_addr_copy((u8 *)switchdev_work->fdb_info.addr,
				fdb_info->addr);
		netdev_hold(ndev, &switchdev_work->dev_tracker, GFP_ATOMIC);
		queue_work(system_long_wq, &switchdev_work->work);

		break;
	}

	return NOTIFY_DONE;

err_addr_alloc:
	kfree(switchdev_work);

	return NOTIFY_BAD;
}

int rswitch_register_notifiers(struct rswitch_private *priv)
{
	int err;

	priv->rswitch_netdevice_nb.notifier_call = rswitch_netdevice_event;
	err = register_netdevice_notifier(&priv->rswitch_netdevice_nb);
	if (err)
		goto register_netdevice_notifier_failed;

	priv->rswitch_switchdev_nb.notifier_call = rswitch_switchdev_event;
	err = register_switchdev_notifier(&priv->rswitch_switchdev_nb);
	if (err)
		goto register_switchdev_notifier_failed;

	priv->rswitch_switchdev_blocking_nb.notifier_call = rswitch_switchdev_blocking_event;
	err = register_switchdev_blocking_notifier(&priv->rswitch_switchdev_blocking_nb);
	if (err)
		goto register_switchdev_blocking_notifier_failed;

	return 0;

register_switchdev_blocking_notifier_failed:
	unregister_switchdev_notifier(&priv->rswitch_switchdev_nb);
register_switchdev_notifier_failed:
	unregister_netdevice_notifier(&priv->rswitch_netdevice_nb);
register_netdevice_notifier_failed:

	return err;
}

void rswitch_unregister_notifiers(struct rswitch_private *priv)
{
	unregister_switchdev_blocking_notifier(&priv->rswitch_switchdev_blocking_nb);
	unregister_switchdev_notifier(&priv->rswitch_switchdev_nb);
	unregister_netdevice_notifier(&priv->rswitch_netdevice_nb);
}
