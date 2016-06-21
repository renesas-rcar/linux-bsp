/*
 *  thermal_sysfs.c - sysfs interface of thermal devices
 *
 *  Copyright (C) 2016 Eduardo Valentin <edubezval@gmail.com>
 *
 *  Highly based on original thermal_core.c
 *  Copyright (C) 2008 Intel Corp
 *  Copyright (C) 2008 Zhang Rui <rui.zhang@intel.com>
 *  Copyright (C) 2008 Sujith Thomas <sujith.thomas@intel.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/sysfs.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "thermal_core.h"

/*
 * sys I/F for thermal zone
 *
 * Note on locking. The sysfs interface will always first
 * lock the zone to serialize data access and ops calls.
 * All calls to thermal_core and thermal_helpers are assumed
 * to handle locking properly.
 */

static ssize_t
type_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct thermal_zone_device *tz = to_thermal_zone(dev);
	char *type;

	mutex_lock(&tz->lock);
	type = tz->type;
	mutex_unlock(&tz->lock);

	return sprintf(buf, "%s\n", type);
}

static ssize_t
temp_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct thermal_zone_device *tz = to_thermal_zone(dev);
	int temperature, ret;

	ret = thermal_zone_get_temp(tz, &temperature);

	if (ret)
		return ret;

	return sprintf(buf, "%d\n", temperature);
}

static ssize_t
mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct thermal_zone_device *tz = to_thermal_zone(dev);
	enum thermal_device_mode mode;
	int result;

	if (!tz->ops->get_mode)
		return -EPERM;

	mutex_lock(&tz->lock);
	result = tz->ops->get_mode(tz, &mode);
	mutex_unlock(&tz->lock);
	if (result)
		return result;

	return sprintf(buf, "%s\n", mode == THERMAL_DEVICE_ENABLED ? "enabled"
		       : "disabled");
}

static ssize_t
mode_store(struct device *dev, struct device_attribute *attr,
	   const char *buf, size_t count)
{
	struct thermal_zone_device *tz = to_thermal_zone(dev);
	enum thermal_device_mode mode = THERMAL_DEVICE_DISABLED;
	int result;

	if (!tz->ops->set_mode)
		return -EPERM;

	if (!strncmp(buf, "enabled", sizeof("enabled") - 1))
		mode = THERMAL_DEVICE_ENABLED;
	else if (!strncmp(buf, "disabled", sizeof("disabled") - 1))
		mode = THERMAL_DEVICE_DISABLED;
	else
		return -EINVAL;

	mutex_lock(&tz->lock);
	result = tz->ops->set_mode(tz, mode);
	mutex_unlock(&tz->lock);

	if (result)
		return result;

	return count;
}

static ssize_t
trip_point_type_show(struct device *dev, struct device_attribute *attr,
		     char *buf)
{
	struct thermal_zone_device *tz = to_thermal_zone(dev);
	enum thermal_trip_type type;
	int trip, result;

	if (!tz->ops->get_trip_type)
		return -EPERM;

	if (sscanf(attr->attr.name, "trip_point_%d_type", &trip) != 1)
		return -EINVAL;

	mutex_lock(&tz->lock);
	result = tz->ops->get_trip_type(tz, trip, &type);
	mutex_unlock(&tz->lock);
	if (result)
		return result;

	switch (type) {
	case THERMAL_TRIP_CRITICAL:
		return sprintf(buf, "critical\n");
	case THERMAL_TRIP_HOT:
		return sprintf(buf, "hot\n");
	case THERMAL_TRIP_PASSIVE:
		return sprintf(buf, "passive\n");
	case THERMAL_TRIP_ACTIVE:
		return sprintf(buf, "active\n");
	default:
		return sprintf(buf, "unknown\n");
	}
}

static ssize_t
trip_point_temp_store(struct device *dev, struct device_attribute *attr,
		      const char *buf, size_t count)
{
	struct thermal_zone_device *tz = to_thermal_zone(dev);
	int trip, ret;
	int temperature;

	if (!tz->ops->set_trip_temp)
		return -EPERM;

	if (sscanf(attr->attr.name, "trip_point_%d_temp", &trip) != 1)
		return -EINVAL;

	if (kstrtoint(buf, 10, &temperature))
		return -EINVAL;

	mutex_lock(&tz->lock);
	ret = tz->ops->set_trip_temp(tz, trip, temperature);
	mutex_unlock(&tz->lock);
	if (ret)
		return ret;

	thermal_zone_device_update(tz);

	return count;
}

static ssize_t
trip_point_temp_show(struct device *dev, struct device_attribute *attr,
		     char *buf)
{
	struct thermal_zone_device *tz = to_thermal_zone(dev);
	int trip, ret;
	int temperature;

	if (!tz->ops->get_trip_temp)
		return -EPERM;

	if (sscanf(attr->attr.name, "trip_point_%d_temp", &trip) != 1)
		return -EINVAL;

	mutex_lock(&tz->lock);
	ret = tz->ops->get_trip_temp(tz, trip, &temperature);
	mutex_unlock(&tz->lock);

	if (ret)
		return ret;

	return sprintf(buf, "%d\n", temperature);
}

static ssize_t
trip_point_hyst_store(struct device *dev, struct device_attribute *attr,
		      const char *buf, size_t count)
{
	struct thermal_zone_device *tz = to_thermal_zone(dev);
	int trip, ret;
	int temperature;

	if (!tz->ops->set_trip_hyst)
		return -EPERM;

	if (sscanf(attr->attr.name, "trip_point_%d_hyst", &trip) != 1)
		return -EINVAL;

	if (kstrtoint(buf, 10, &temperature))
		return -EINVAL;

	/*
	 * We are not doing any check on the 'temperature' value
	 * here. The driver implementing 'set_trip_hyst' has to
	 * take care of this.
	 */
	mutex_lock(&tz->lock);
	ret = tz->ops->set_trip_hyst(tz, trip, temperature);
	mutex_unlock(&tz->lock);

	return ret ? ret : count;
}

static ssize_t
trip_point_hyst_show(struct device *dev, struct device_attribute *attr,
		     char *buf)
{
	struct thermal_zone_device *tz = to_thermal_zone(dev);
	int trip, ret;
	int temperature;

	if (!tz->ops->get_trip_hyst)
		return -EPERM;

	if (sscanf(attr->attr.name, "trip_point_%d_hyst", &trip) != 1)
		return -EINVAL;

	mutex_lock(&tz->lock);
	ret = tz->ops->get_trip_hyst(tz, trip, &temperature);
	mutex_unlock(&tz->lock);

	return ret ? ret : sprintf(buf, "%d\n", temperature);
}

static ssize_t
passive_store(struct device *dev, struct device_attribute *attr,
	      const char *buf, size_t count)
{
	struct thermal_zone_device *tz = to_thermal_zone(dev);
	int state;

	if (sscanf(buf, "%d\n", &state) != 1)
		return -EINVAL;

	/* sanity check: values below 1000 millicelcius don't make sense
	 * and can cause the system to go into a thermal heart attack
	 */
	if (state && state < 1000)
		return -EINVAL;

	mutex_lock(&tz->lock);
	if (state && !tz->forced_passive) {
		if (!tz->passive_delay)
			tz->passive_delay = 1000;
		mutex_unlock(&tz->lock);
		thermal_zone_device_rebind_exception(tz, "Processor",
						     sizeof("Processor"));
		mutex_lock(&tz->lock);
	} else if (!state && tz->forced_passive) {
		tz->passive_delay = 0;
		mutex_unlock(&tz->lock);
		thermal_zone_device_unbind_exception(tz, "Processor",
						     sizeof("Processor"));
		mutex_lock(&tz->lock);
	}

	tz->forced_passive = state;
	mutex_unlock(&tz->lock);

	thermal_zone_device_update(tz);

	return count;
}

static ssize_t
passive_show(struct device *dev, struct device_attribute *attr,
	     char *buf)
{
	struct thermal_zone_device *tz = to_thermal_zone(dev);
	unsigned int passive;

	mutex_lock(&tz->lock);
	passive = tz->forced_passive;
	mutex_unlock(&tz->lock);

	return sprintf(buf, "%u\n", passive);
}

static ssize_t
policy_store(struct device *dev, struct device_attribute *attr,
	     const char *buf, size_t count)
{
	struct thermal_zone_device *tz = to_thermal_zone(dev);
	char name[THERMAL_NAME_LENGTH];
	int ret;

	snprintf(name, sizeof(name), "%s", buf);

	ret = thermal_zone_device_set_policy(tz, name);
	if (!ret)
		ret = count;

	return ret;
}

static ssize_t
policy_show(struct device *dev, struct device_attribute *devattr, char *buf)
{
	struct thermal_zone_device *tz = to_thermal_zone(dev);
	char *name;

	/* locking the zone because governor->name does not change */
	mutex_lock(&tz->lock);
	name = tz->governor->name;
	mutex_unlock(&tz->lock);

	return sprintf(buf, "%s\n", name);
}

static ssize_t
available_policies_show(struct device *dev, struct device_attribute *devattr,
			char *buf)
{
	return thermal_build_list_of_policies(buf);
}

#if (IS_ENABLED(CONFIG_THERMAL_EMULATION))
static ssize_t
emul_temp_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct thermal_zone_device *tz = to_thermal_zone(dev);
	int ret = 0;
	int temperature;

	if (kstrtoint(buf, 10, &temperature))
		return -EINVAL;

	mutex_lock(&tz->lock);
	if (!tz->ops->set_emul_temp)
		tz->emul_temperature = temperature;
	else
		ret = tz->ops->set_emul_temp(tz, temperature);
	mutex_unlock(&tz->lock);

	if (!ret)
		thermal_zone_device_update(tz);

	return ret ? ret : count;
}
static DEVICE_ATTR(emul_temp, S_IWUSR, NULL, emul_temp_store);
#endif

static ssize_t
sustainable_power_show(struct device *dev, struct device_attribute *devattr,
		       char *buf)
{
	struct thermal_zone_device *tz = to_thermal_zone(dev);
	unsigned int sustainable_power;

	mutex_lock(&tz->lock);
	sustainable_power = tz->tzp->sustainable_power;
	mutex_unlock(&tz->lock);

	if (tz->tzp)
		return sprintf(buf, "%u\n", sustainable_power);
	else
		return -EIO;
}

static ssize_t
sustainable_power_store(struct device *dev, struct device_attribute *devattr,
			const char *buf, size_t count)
{
	struct thermal_zone_device *tz = to_thermal_zone(dev);
	u32 sustainable_power;

	if (!tz->tzp)
		return -EIO;

	if (kstrtou32(buf, 10, &sustainable_power))
		return -EINVAL;

	mutex_lock(&tz->lock);
	tz->tzp->sustainable_power = sustainable_power;
	mutex_unlock(&tz->lock);

	return count;
}

#define create_s32_tzp_attr(name)					\
	static ssize_t							\
	name##_show(struct device *dev, struct device_attribute *devattr, \
		char *buf)						\
	{								\
	struct thermal_zone_device *tz = to_thermal_zone(dev);		\
	int value;							\
									\
	if (!tz->tzp)							\
		return -EIO;						\
									\
	mutex_lock(&tz->lock);						\
	value = tz->tzp->name;						\
	mutex_unlock(&tz->lock);					\
									\
	return sprintf(buf, "%d\n", value);				\
	}								\
									\
	static ssize_t							\
	name##_store(struct device *dev, struct device_attribute *devattr, \
		const char *buf, size_t count)				\
	{								\
		struct thermal_zone_device *tz = to_thermal_zone(dev);	\
		s32 value;						\
									\
		if (!tz->tzp)						\
			return -EIO;					\
									\
		if (kstrtos32(buf, 10, &value))				\
			return -EINVAL;					\
									\
		mutex_lock(&tz->lock);					\
		tz->tzp->name = value;					\
		mutex_unlock(&tz->lock);				\
									\
		return count;						\
	}								\
	static DEVICE_ATTR(name, S_IWUSR | S_IRUGO, name##_show, name##_store)

create_s32_tzp_attr(k_po);
create_s32_tzp_attr(k_pu);
create_s32_tzp_attr(k_i);
create_s32_tzp_attr(k_d);
create_s32_tzp_attr(integral_cutoff);
create_s32_tzp_attr(slope);
create_s32_tzp_attr(offset);
#undef create_s32_tzp_attr

/*
 * These are thermal zone device attributes that will always be present.
 * All the attributes created for tzp (create_s32_tzp_attr) also are always
 * present on the sysfs interface.
 */
static DEVICE_ATTR(type, 0444, type_show, NULL);
static DEVICE_ATTR(temp, 0444, temp_show, NULL);
static DEVICE_ATTR(policy, S_IRUGO | S_IWUSR, policy_show, policy_store);
static DEVICE_ATTR(available_policies, S_IRUGO, available_policies_show, NULL);
static DEVICE_ATTR(sustainable_power, S_IWUSR | S_IRUGO, sustainable_power_show,
		   sustainable_power_store);

/* These thermal zone device attributes are created based on conditions */
static DEVICE_ATTR(mode, 0644, mode_show, mode_store);
static DEVICE_ATTR(passive, S_IRUGO | S_IWUSR, passive_show, passive_store);

/* These attributes are unconditionally added to a thermal zone */
static struct attribute *thermal_zone_dev_attrs[] = {
	&dev_attr_type.attr,
	&dev_attr_temp.attr,
#if (IS_ENABLED(CONFIG_THERMAL_EMULATION))
	&dev_attr_emul_temp.attr,
#endif
	&dev_attr_policy.attr,
	&dev_attr_available_policies.attr,
	&dev_attr_sustainable_power.attr,
	&dev_attr_k_po.attr,
	&dev_attr_k_pu.attr,
	&dev_attr_k_i.attr,
	&dev_attr_k_d.attr,
	&dev_attr_integral_cutoff.attr,
	&dev_attr_slope.attr,
	&dev_attr_offset.attr,
	NULL,
};

static struct attribute_group thermal_zone_attribute_group = {
	.attrs = thermal_zone_dev_attrs,
};

/* We expose mode only if .get_mode is present */
static struct attribute *thermal_zone_mode_attrs[] = {
	&dev_attr_mode.attr,
	NULL,
};

static umode_t thermal_zone_mode_is_visible(struct kobject *kobj,
					    struct attribute *attr,
					    int attrno)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct thermal_zone_device *tz;

	tz = container_of(dev, struct thermal_zone_device, device);

	if (tz->ops->get_mode)
		return attr->mode;

	return 0;
}

static struct attribute_group thermal_zone_mode_attribute_group = {
	.attrs = thermal_zone_mode_attrs,
	.is_visible = thermal_zone_mode_is_visible,
};

/* We expose passive only if passive trips are present */
static struct attribute *thermal_zone_passive_attrs[] = {
	&dev_attr_passive.attr,
	NULL,
};

static umode_t thermal_zone_passive_is_visible(struct kobject *kobj,
					       struct attribute *attr,
					       int attrno)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct thermal_zone_device *tz;
	enum thermal_trip_type trip_type;
	int count;

	tz = container_of(dev, struct thermal_zone_device, device);

	for (count = 0; count < tz->trips; count++) {
		mutex_lock(&tz->lock);
		tz->ops->get_trip_type(tz, count, &trip_type);
		mutex_unlock(&tz->lock);

		if (trip_type == THERMAL_TRIP_PASSIVE)
			return attr->mode;
	}

	return 0;
}

static struct attribute_group thermal_zone_passive_attribute_group = {
	.attrs = thermal_zone_passive_attrs,
	.is_visible = thermal_zone_passive_is_visible,
};

static const struct attribute_group *thermal_zone_attribute_groups[] = {
	&thermal_zone_attribute_group,
	&thermal_zone_mode_attribute_group,
	&thermal_zone_passive_attribute_group,
	/* This is not NULL terminated as we create the group dynamically */
};

/**
 * create_trip_attrs() - create attributes for trip points
 * @tz:		the thermal zone device
 * @mask:	Writeable trip point bitmap.
 *
 * helper function to instantiate sysfs entries for every trip
 * point and its properties of a struct thermal_zone_device.
 *
 * This function is assumed to be called only during probe,
 * and therefore no locking in the thermal zone device is done.
 *
 * Return: 0 on success, the proper error value otherwise.
 */
static int create_trip_attrs(struct thermal_zone_device *tz, int mask)
{
	struct attribute **attrs;
	int indx;

	tz->trip_type_attrs = kcalloc(tz->trips, sizeof(*tz->trip_type_attrs),
				      GFP_KERNEL);
	if (!tz->trip_type_attrs)
		return -ENOMEM;

	tz->trip_temp_attrs = kcalloc(tz->trips, sizeof(*tz->trip_temp_attrs),
				      GFP_KERNEL);
	if (!tz->trip_temp_attrs) {
		kfree(tz->trip_type_attrs);
		return -ENOMEM;
	}

	if (tz->ops->get_trip_hyst) {
		tz->trip_hyst_attrs = kcalloc(tz->trips,
					      sizeof(*tz->trip_hyst_attrs),
					      GFP_KERNEL);
		if (!tz->trip_hyst_attrs) {
			kfree(tz->trip_type_attrs);
			kfree(tz->trip_temp_attrs);
			return -ENOMEM;
		}
	}

	attrs = kcalloc(tz->trips * 3 + 1, sizeof(*attrs), GFP_KERNEL);
	if (!attrs) {
		kfree(tz->trip_type_attrs);
		kfree(tz->trip_temp_attrs);
		if (tz->ops->get_trip_hyst)
			kfree(tz->trip_hyst_attrs);
		return -ENOMEM;
	}

	for (indx = 0; indx < tz->trips; indx++) {
		/* create trip type attribute */
		snprintf(tz->trip_type_attrs[indx].name, THERMAL_NAME_LENGTH,
			 "trip_point_%d_type", indx);

		sysfs_attr_init(&tz->trip_type_attrs[indx].attr.attr);
		tz->trip_type_attrs[indx].attr.attr.name =
						tz->trip_type_attrs[indx].name;
		tz->trip_type_attrs[indx].attr.attr.mode = S_IRUGO;
		tz->trip_type_attrs[indx].attr.show = trip_point_type_show;
		attrs[indx] = &tz->trip_type_attrs[indx].attr.attr;

		/* create trip temp attribute */
		snprintf(tz->trip_temp_attrs[indx].name, THERMAL_NAME_LENGTH,
			 "trip_point_%d_temp", indx);

		sysfs_attr_init(&tz->trip_temp_attrs[indx].attr.attr);
		tz->trip_temp_attrs[indx].attr.attr.name =
						tz->trip_temp_attrs[indx].name;
		tz->trip_temp_attrs[indx].attr.attr.mode = S_IRUGO;
		tz->trip_temp_attrs[indx].attr.show = trip_point_temp_show;
		if (IS_ENABLED(CONFIG_THERMAL_WRITABLE_TRIPS) &&
		    mask & (1 << indx)) {
			tz->trip_temp_attrs[indx].attr.attr.mode |= S_IWUSR;
			tz->trip_temp_attrs[indx].attr.store =
							trip_point_temp_store;
		}
		attrs[indx + tz->trips] = &tz->trip_temp_attrs[indx].attr.attr;

		/* create Optional trip hyst attribute */
		if (!tz->ops->get_trip_hyst)
			continue;
		snprintf(tz->trip_hyst_attrs[indx].name, THERMAL_NAME_LENGTH,
			 "trip_point_%d_hyst", indx);

		sysfs_attr_init(&tz->trip_hyst_attrs[indx].attr.attr);
		tz->trip_hyst_attrs[indx].attr.attr.name =
					tz->trip_hyst_attrs[indx].name;
		tz->trip_hyst_attrs[indx].attr.attr.mode = S_IRUGO;
		tz->trip_hyst_attrs[indx].attr.show = trip_point_hyst_show;
		if (tz->ops->set_trip_hyst) {
			tz->trip_hyst_attrs[indx].attr.attr.mode |= S_IWUSR;
			tz->trip_hyst_attrs[indx].attr.store =
					trip_point_hyst_store;
		}
		attrs[indx + tz->trips * 2] =
					&tz->trip_hyst_attrs[indx].attr.attr;
	}
	attrs[tz->trips * 3] = NULL;

	tz->trips_attribute_group.attrs = attrs;

	return 0;
}

int thermal_zone_create_device_groups(struct thermal_zone_device *tz,
				      int mask)
{
	const struct attribute_group **groups;
	int i, size, result;

	result = create_trip_attrs(tz, mask);
	if (result)
		return result;

	/* we need one extra for trips and the NULL to terminate the array */
	size = ARRAY_SIZE(thermal_zone_attribute_groups) + 2;
	/* This also takes care of API requirement to be NULL terminated */
	groups = kcalloc(size, sizeof(*groups), GFP_KERNEL);
	if (!groups)
		return -ENOMEM;

	for (i = 0; i < size - 2; i++)
		groups[i] = thermal_zone_attribute_groups[i];

	groups[size - 2] = &tz->trips_attribute_group;

	tz->device.groups = groups;

	return 0;
}

/* sys I/F for cooling device */
static ssize_t
thermal_cooling_device_type_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct thermal_cooling_device *cdev = to_cooling_device(dev);
	char *type;

	mutex_lock(&cdev->lock);
	type = cdev->type;
	mutex_unlock(&cdev->lock);

	return sprintf(buf, "%s\n", type);
}

static ssize_t
thermal_cooling_device_max_state_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct thermal_cooling_device *cdev = to_cooling_device(dev);
	unsigned long state;
	int ret;

	mutex_lock(&cdev->lock);
	ret = cdev->ops->get_max_state(cdev, &state);
	mutex_unlock(&cdev->lock);
	if (ret)
		return ret;
	return sprintf(buf, "%ld\n", state);
}

static ssize_t
thermal_cooling_device_cur_state_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct thermal_cooling_device *cdev = to_cooling_device(dev);
	unsigned long state;
	int ret;

	mutex_lock(&cdev->lock);
	ret = cdev->ops->get_cur_state(cdev, &state);
	mutex_unlock(&cdev->lock);
	if (ret)
		return ret;
	return sprintf(buf, "%ld\n", state);
}

static ssize_t
thermal_cooling_device_cur_state_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	struct thermal_cooling_device *cdev = to_cooling_device(dev);
	unsigned long state;
	int result;

	if (sscanf(buf, "%ld\n", &state) != 1)
		return -EINVAL;

	if ((long)state < 0)
		return -EINVAL;

	mutex_lock(&cdev->lock);
	result = cdev->ops->set_cur_state(cdev, state);
	mutex_unlock(&cdev->lock);
	if (result)
		return result;
	return count;
}

static struct device_attribute dev_attr_cdev_type =
__ATTR(type, 0444, thermal_cooling_device_type_show, NULL);
static DEVICE_ATTR(max_state, 0444,
		   thermal_cooling_device_max_state_show, NULL);
static DEVICE_ATTR(cur_state, 0644,
		   thermal_cooling_device_cur_state_show,
		   thermal_cooling_device_cur_state_store);

static struct attribute *cooling_device_attrs[] = {
	&dev_attr_cdev_type.attr,
	&dev_attr_max_state.attr,
	&dev_attr_cur_state.attr,
	NULL,
};

static const struct attribute_group cooling_device_attr_group = {
	.attrs = cooling_device_attrs,
};

static const struct attribute_group *cooling_device_attr_groups[] = {
	&cooling_device_attr_group,
	NULL,
};

/*
 * Assumed to be called at the creation of the cooling device
 * and for this reason, no locking is done
 */
void thermal_cooling_device_setup_sysfs(struct thermal_cooling_device *cdev)
{
	cdev->device.groups = cooling_device_attr_groups;
}

/* these helper will be used only at the time of bindig */
ssize_t
thermal_cooling_device_trip_point_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct thermal_instance *instance;
	int trip;

	instance =
	    container_of(attr, struct thermal_instance, attr);

	mutex_lock(&instance->tz->lock);
	mutex_lock(&instance->cdev->lock);
	trip = instance->trip;
	mutex_unlock(&instance->cdev->lock);
	mutex_unlock(&instance->tz->lock);
	if (instance->trip == THERMAL_TRIPS_NONE)
		return sprintf(buf, "-1\n");
	else
		return sprintf(buf, "%d\n", trip);
}

ssize_t
thermal_cooling_device_weight_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct thermal_instance *instance;
	int weight;

	instance = container_of(attr, struct thermal_instance, weight_attr);
	mutex_lock(&instance->tz->lock);
	mutex_lock(&instance->cdev->lock);
	weight = instance->weight;
	mutex_unlock(&instance->cdev->lock);
	mutex_unlock(&instance->tz->lock);

	return sprintf(buf, "%d\n", weight);
}

ssize_t
thermal_cooling_device_weight_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct thermal_instance *instance;
	int ret, weight;

	ret = kstrtoint(buf, 0, &weight);
	if (ret)
		return ret;

	instance = container_of(attr, struct thermal_instance, weight_attr);
	mutex_lock(&instance->tz->lock);
	mutex_lock(&instance->cdev->lock);
	instance->weight = weight;
	mutex_unlock(&instance->cdev->lock);
	mutex_unlock(&instance->tz->lock);

	return count;
}
