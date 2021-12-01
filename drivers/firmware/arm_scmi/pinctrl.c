// SPDX-License-Identifier: GPL-2.0
/*
 * System Control and Management Interface (SCMI) Pinctrl Protocol
 *
 * Copyright (C) 2021 EPAM.
 */

#define pr_fmt(fmt) "SCMI Notifications PINCTRL - " fmt

#include <linux/scmi_protocol.h>

#include "common.h"
#include "notify.h"

enum scmi_pinctrl_protocol_cmd {
	GET_GROUP_PINS = 0x3,
	GET_FUNCTION_GROUPS = 0x4,
	SET_MUX = 0x5,
	GET_PINS = 0x6,
	GET_CONFIG = 0x7,
	SET_CONFIG = 0x8,
	GET_CONFIG_GROUP = 0x9,
	SET_CONFIG_GROUP = 0xa,
};

struct scmi_group_info {
	bool has_name;
	char name[SCMI_MAX_STR_SIZE];
	unsigned group_pins[SCMI_PINCTRL_MAX_PINS_CNT];
	unsigned nr_pins;
};

struct scmi_function_info {
	bool has_name;
	char name[SCMI_MAX_STR_SIZE];
	u16 groups[SCMI_PINCTRL_MAX_GROUPS_CNT];
	u8 nr_groups;
};

struct scmi_pinctrl_info {
	u32 version;
	u16 nr_groups;
	u16 nr_functions;
	u16 nr_pins;
	struct scmi_group_info *groups;
	struct scmi_function_info *functions;
	u16 pins[SCMI_PINCTRL_MAX_PINS_CNT];
};

static int scmi_pinctrl_attributes_get(const struct scmi_handle *handle,
				     struct scmi_pinctrl_info *pi)
{
	int ret;
	struct scmi_xfer *t;
	struct scmi_msg_pinctrl_protocol_attributes {
		__le16 nr_functions;
		__le16 nr_groups;
	} *attr;

	ret = scmi_xfer_get_init(handle, PROTOCOL_ATTRIBUTES,
				 SCMI_PROTOCOL_PINCTRL, 0, sizeof(attr), &t);
	if (ret)
		return ret;

	attr = t->rx.buf;

	ret = scmi_do_xfer(handle, t);
	if (!ret) {
		pi->nr_functions = le16_to_cpu(attr->nr_functions);
		pi->nr_groups = le16_to_cpu(attr->nr_groups);
	}

	scmi_xfer_put(handle, t);
	return ret;
}

static int scmi_pinctrl_get_groups_count(const struct scmi_handle *handle)
{
	struct scmi_pinctrl_info *pi = handle->pinctrl_priv;

	return pi->nr_groups;
}

static int scmi_pinctrl_get_group_name(const struct scmi_handle *handle,
								u32 selector, const char **name)
{
	struct scmi_pinctrl_info *pi = handle->pinctrl_priv;

	if (selector > SCMI_PINCTRL_MAX_GROUPS_CNT)
		return -EINVAL;

	if (!pi->groups[selector].has_name) {
		snprintf(pi->groups[selector].name, SCMI_MAX_STR_SIZE, "%d", selector);
		pi->groups[selector].has_name = true;
	}

	*name = pi->groups[selector].name;

	return 0;
}

static int scmi_pinctrl_get_group_pins(const struct scmi_handle *handle,
									   u32 selector, const unsigned **pins,
									   unsigned *nr_pins)
{
	struct scmi_pinctrl_info *pi = handle->pinctrl_priv;
	u16 *list;
	int loop, ret = 0;
	struct scmi_xfer *t;
	__le32 *num_ret;
	u16 tot_num_ret = 0, loop_num_ret;
	struct scmi_group_pins_tx {
		__le16 selector;
		__le16 skip;
	} *tx;

	if (selector > SCMI_PINCTRL_MAX_GROUPS_CNT)
		return -EINVAL;

	if (pi->groups[selector].nr_pins) {
		*nr_pins = pi->groups[selector].nr_pins;
		*pins = pi->groups[selector].group_pins;
		return 0;
	}

	ret = scmi_xfer_get_init(handle, GET_GROUP_PINS,
				 SCMI_PROTOCOL_PINCTRL, sizeof(*tx), 0, &t);
	if (ret)
		return ret;

	tx = t->tx.buf;
	num_ret = t->rx.buf;
	list = t->rx.buf + sizeof(*num_ret);

	do {
		/* Set the number of pins to be skipped/already read */
		tx->skip = cpu_to_le16(tot_num_ret);
		tx->selector = cpu_to_le16(selector);

		ret = scmi_do_xfer(handle, t);
		if (ret)
			break;

		loop_num_ret = le32_to_cpu(*num_ret);
		if (tot_num_ret + loop_num_ret > SCMI_PINCTRL_MAX_PINS_CNT) {
			dev_err(handle->dev, "No. of PINS > SCMI_PINCTRL_MAX_PINS_CNT");
			break;
		}

		for (loop = 0; loop < loop_num_ret; loop++) {
			pi->groups[selector].group_pins[tot_num_ret + loop] =
				le16_to_cpu(list[loop]);
		}

		tot_num_ret += loop_num_ret;

		scmi_reset_rx_to_maxsz(handle, t);
	} while (loop_num_ret);

	scmi_xfer_put(handle, t);
	pi->groups[selector].nr_pins = tot_num_ret;
	*pins = pi->groups[selector].group_pins;
	*nr_pins = pi->groups[selector].nr_pins;

	return ret;
}

static int scmi_pinctrl_get_functions_count(const struct scmi_handle *handle)
{
	struct scmi_pinctrl_info *pi = handle->pinctrl_priv;

	return pi->nr_functions;
}

static int scmi_pinctrl_get_function_name(const struct scmi_handle *handle,
								   u32 selector, const char **name)
{
	struct scmi_pinctrl_info *pi = handle->pinctrl_priv;

	if (selector >= pi->nr_functions)
		return -EINVAL;

	if (!pi->functions[selector].has_name) {
		snprintf(pi->functions[selector].name, SCMI_MAX_STR_SIZE,
				 "%d", selector);
		pi->functions[selector].has_name = true;
	}

	*name = pi->functions[selector].name;
	return 0;
}

static int scmi_pinctrl_get_function_groups(const struct scmi_handle *handle,
									 u32 selector, u32 *nr_groups,
									 const u16 **groups)
{
	struct scmi_pinctrl_info *pi = handle->pinctrl_priv;
	u16 *list;
	int loop, ret = 0;
	struct scmi_xfer *t;
	struct scmi_func_groups {
		__le16 selector;
		__le16 skip;
	} *tx;
	__le32 *num_ret;
	u16 tot_num_ret = 0, loop_num_ret;

	if (selector >= pi->nr_functions)
		return -EINVAL;

	if (pi->functions[selector].nr_groups) {
		*nr_groups = pi->functions[selector].nr_groups;
		*groups = pi->functions[selector].groups;
		return 0;
	}

	ret = scmi_xfer_get_init(handle, GET_FUNCTION_GROUPS,
				 SCMI_PROTOCOL_PINCTRL, sizeof(*tx), 0, &t);
	if (ret)
		return ret;

	tx = t->tx.buf;
	num_ret = t->rx.buf;
	list = t->rx.buf + sizeof(*num_ret);

	do {
		/* Set the number of pins to be skipped/already read */
		tx->skip = cpu_to_le16(tot_num_ret);
		tx->selector = cpu_to_le16(selector);

		ret = scmi_do_xfer(handle, t);
		if (ret)
			break;

		loop_num_ret = le32_to_cpu(*num_ret);
		if (tot_num_ret + loop_num_ret > SCMI_PINCTRL_MAX_GROUPS_CNT) {
			dev_err(handle->dev, "No. of PINS > SCMI_PINCTRL_MAX_GROUPS_CNT");
			break;
		}

		for (loop = 0; loop < loop_num_ret; loop++) {
			pi->functions[selector].groups[tot_num_ret + loop] = le16_to_cpu(list[loop]);
		}

		tot_num_ret += loop_num_ret;

		scmi_reset_rx_to_maxsz(handle, t);
	} while (loop_num_ret);

	scmi_xfer_put(handle, t);
	pi->functions[selector].nr_groups = tot_num_ret;
	*groups = pi->functions[selector].groups;
	*nr_groups = pi->functions[selector].nr_groups;

	return ret;
}

static int scmi_pinctrl_set_mux(const struct scmi_handle *handle, u32 selector,
						u32 group)
{
	struct scmi_xfer *t;
	struct scmi_mux_tx {
		__le16 function;
		__le16 group;
	} *tx;
	int ret;

	ret = scmi_xfer_get_init(handle, SET_MUX, SCMI_PROTOCOL_PINCTRL,
							 sizeof(*tx), 0, &t);
	if (ret)
		return ret;

	tx = t->tx.buf;
	tx->function = cpu_to_le16(selector);
	tx->group = cpu_to_le16(group);

	ret = scmi_do_xfer(handle, t);
	scmi_xfer_put(handle, t);

	return ret;
}

static int scmi_pinctrl_get_pins(const struct scmi_handle *handle, u32 *nr_pins,
						  const u16 **pins)
{
	struct scmi_pinctrl_info *pi = handle->pinctrl_priv;
	u16 *list;
	int loop, ret = 0;
	struct scmi_xfer *t;
	__le32 *num_skip, *num_ret;
	u32 tot_num_ret = 0, loop_num_ret;

	if (pi->nr_pins) {
		*nr_pins = pi->nr_pins;
		*pins = pi->pins;
		return 0;
	}

	ret = scmi_xfer_get_init(handle, GET_PINS,
				 SCMI_PROTOCOL_PINCTRL, sizeof(*num_skip), 0, &t);
	if (ret)
		return ret;

	num_skip = t->tx.buf;
	num_ret = t->rx.buf;
	list = t->rx.buf + sizeof(*num_ret);

	do {
		/* Set the number of pins to be skipped/already read */
		*num_skip = cpu_to_le32(tot_num_ret);

		ret = scmi_do_xfer(handle, t);
		if (ret)
			break;

		loop_num_ret = le32_to_cpu(*num_ret);
		if (tot_num_ret + loop_num_ret > SCMI_PINCTRL_MAX_PINS_CNT) {
			dev_err(handle->dev, "No. of PINS > SCMI_PINCTRL_MAX_PINS_CNT");
			break;
		}

		for (loop = 0; loop < loop_num_ret; loop++) {
			pi->pins[tot_num_ret + loop] = le16_to_cpu(list[loop]);
		}

		tot_num_ret += loop_num_ret;

		scmi_reset_rx_to_maxsz(handle, t);
	} while (loop_num_ret);

	scmi_xfer_put(handle, t);
	pi->nr_pins = tot_num_ret;
	*pins = pi->pins;
	*nr_pins = pi->nr_pins;

	return ret;
}

static int scmi_pinctrl_get_config(const struct scmi_handle *handle, u32 pin,
				  u32 *config)
{
	struct scmi_xfer *t;
	struct scmi_conf_tx {
		__le32 pin;
		__le32 config;
	} *tx;
	__le32 *packed_config;
	int ret;

	ret = scmi_xfer_get_init(handle, GET_CONFIG, SCMI_PROTOCOL_PINCTRL,
							 sizeof(*tx), sizeof(*packed_config), &t);
	if (ret)
		return ret;

	tx = t->tx.buf;
	packed_config = t->rx.buf;
	tx->pin = cpu_to_le32(pin);
	tx->config = cpu_to_le32(*config);
	ret = scmi_do_xfer(handle, t);

	if (!ret)
		*config = le32_to_cpu(*packed_config);

	scmi_xfer_put(handle, t);
	return ret;
}

static int scmi_pinctrl_set_config(const struct scmi_handle *handle, u32 pin,
				  u32 config)
{
	struct scmi_xfer *t;
	struct scmi_conf_tx {
		__le32 pin;
		__le32 config;
	} *tx;
	int ret;

	ret = scmi_xfer_get_init(handle, SET_CONFIG, SCMI_PROTOCOL_PINCTRL,
							 sizeof(*tx), 0, &t);
	if (ret)
		return ret;

	tx = t->tx.buf;
	tx->pin = cpu_to_le32(pin);
	tx->config = cpu_to_le32(config);
	ret = scmi_do_xfer(handle, t);

	scmi_xfer_put(handle, t);
	return ret;
}

static int scmi_pinctrl_get_config_group(const struct scmi_handle *handle,
										 u32 group, u32 *config)
{
	struct scmi_xfer *t;
	struct scmi_conf_tx {
		__le32 group;
		__le32 config;
	} *tx;
	__le32 *packed_config;
	int ret;

	ret = scmi_xfer_get_init(handle, GET_CONFIG_GROUP, SCMI_PROTOCOL_PINCTRL,
							 sizeof(*tx), sizeof(*packed_config), &t);
	if (ret)
		return ret;

	tx = t->tx.buf;
	packed_config = t->rx.buf;
	tx->group = cpu_to_le32(group);
	tx->config = cpu_to_le32(*config);
	ret = scmi_do_xfer(handle, t);

	if (!ret)
		*config = le32_to_cpu(*packed_config);

	scmi_xfer_put(handle, t);
	return ret;
}

static int scmi_pinctrl_set_config_group(const struct scmi_handle *handle,
										 u32 group, u32 config)
{
	struct scmi_xfer *t;
	struct scmi_conf_tx {
		__le32 group;
		__le32 config;
	} *tx;
	int ret;

	ret = scmi_xfer_get_init(handle, SET_CONFIG_GROUP, SCMI_PROTOCOL_PINCTRL,
							 sizeof(*tx), 0, &t);
	if (ret)
		return ret;

	tx = t->tx.buf;
	tx->group = cpu_to_le32(group);
	tx->config = cpu_to_le32(config);
	ret = scmi_do_xfer(handle, t);

	scmi_xfer_put(handle, t);
	return ret;
}

static const struct scmi_pinctrl_ops pinctrl_ops = {
	.get_groups_count = scmi_pinctrl_get_groups_count,
	.get_group_name = scmi_pinctrl_get_group_name,
	.get_group_pins = scmi_pinctrl_get_group_pins,
	.get_functions_count = scmi_pinctrl_get_functions_count,
	.get_function_name = scmi_pinctrl_get_function_name,
	.get_function_groups = scmi_pinctrl_get_function_groups,
	.set_mux = scmi_pinctrl_set_mux,
	.get_pins = scmi_pinctrl_get_pins,
	.get_config = scmi_pinctrl_get_config,
	.set_config = scmi_pinctrl_set_config,
	.get_config_group = scmi_pinctrl_get_config_group,
	.set_config_group = scmi_pinctrl_set_config_group,
};

static int scmi_pinctrl_protocol_init(struct scmi_handle *handle)
{
	u32 version;
	struct scmi_pinctrl_info *pinfo;
	int ret;

	scmi_version_get(handle, SCMI_PROTOCOL_PINCTRL, &version);

	dev_dbg(handle->dev, "Pinctrl Version %d.%d\n",
		PROTOCOL_REV_MAJOR(version), PROTOCOL_REV_MINOR(version));

	pinfo = devm_kzalloc(handle->dev, sizeof(*pinfo), GFP_KERNEL);
	if (!pinfo)
		return -ENOMEM;

	ret = scmi_pinctrl_attributes_get(handle, pinfo);
	if (ret)
		goto free;

	pinfo->groups = devm_kcalloc(handle->dev, pinfo->nr_groups,
								 sizeof(*pinfo->groups), GFP_KERNEL);
	if (!pinfo->groups) {
		ret = -ENOMEM;
		goto free;
	}

	pinfo->functions = devm_kcalloc(handle->dev, pinfo->nr_functions,
								 sizeof(*pinfo->functions), GFP_KERNEL);
	if (!pinfo->functions) {
		ret = -ENOMEM;
		goto free;
	}

	pinfo->version = version;
	handle->pinctrl_ops = &pinctrl_ops;
	handle->pinctrl_priv = pinfo;

	return 0;
free:
	if (pinfo) {
		if (pinfo->functions)
			devm_kfree(handle->dev,pinfo->functions);

		if (pinfo->groups)
			devm_kfree(handle->dev,pinfo->groups);

		devm_kfree(handle->dev,pinfo);
	}

	return ret;
}

DEFINE_SCMI_PROTOCOL_REGISTER_UNREGISTER(SCMI_PROTOCOL_PINCTRL, pinctrl)
