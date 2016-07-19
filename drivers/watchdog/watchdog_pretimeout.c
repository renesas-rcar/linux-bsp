/*
 * Watchdog pretimout governor framework
 *
 * Copyright (C) 2015 Mentor Graphics
 * Copyright (C) 2016 Renesas Electronics Corporation
 * Copyright (C) 2016 Sang Engineering, Wolfram Sang
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/watchdog.h>

#include "watchdog_pretimeout.h"

/* The mutex protects governor list and serializes external interfaces */
static DEFINE_MUTEX(governor_lock);

/* List of registered watchdog pretimeout governors */
static LIST_HEAD(governor_list);

/* The spinlock protects wdd->gov */
static DEFINE_SPINLOCK(pretimeout_lock);

struct governor_priv {
	struct watchdog_governor *gov;
	bool is_default;
	struct list_head entry;
};

static struct governor_priv *find_governor_by_name(const char *gov_name)
{
	struct governor_priv *priv;

	list_for_each_entry(priv, &governor_list, entry)
		if (!strncmp(gov_name, priv->gov->name, WATCHDOG_GOV_NAME_LEN))
			return priv;

	return NULL;
}

static struct watchdog_governor *find_default_governor(void)
{
	struct governor_priv *priv;

	list_for_each_entry(priv, &governor_list, entry)
		if (priv->is_default)
			return priv->gov;

	return NULL;
}

void watchdog_notify_pretimeout(struct watchdog_device *wdd)
{
	unsigned long flags;

	if (!wdd)
		return;

	spin_lock_irqsave(&pretimeout_lock, flags);

	if (wdd->gov)
		wdd->gov->pretimeout(wdd);

	spin_unlock_irqrestore(&pretimeout_lock, flags);
}
EXPORT_SYMBOL_GPL(watchdog_notify_pretimeout);

int watchdog_register_governor(struct watchdog_governor *gov)
{
	struct governor_priv	*priv;

	if (!gov || !gov->pretimeout || strlen(gov->name) >= WATCHDOG_GOV_NAME_LEN)
		return -EINVAL;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	mutex_lock(&governor_lock);

	if (find_governor_by_name(gov->name)) {
		kfree(priv);
		mutex_unlock(&governor_lock);
		return -EBUSY;
	}

	priv->gov = gov;

	if (!strncmp(WATCHDOG_PRETIMEOUT_DEFAULT_GOV,
		     gov->name, WATCHDOG_GOV_NAME_LEN)) {
		priv->is_default = true;
	}

	list_add(&priv->entry, &governor_list);

	mutex_unlock(&governor_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(watchdog_register_governor);

void watchdog_unregister_governor(struct watchdog_governor *gov)
{
	struct governor_priv *priv;

	if (!gov)
		return;

	mutex_lock(&governor_lock);

	list_for_each_entry(priv, &governor_list, entry) {
		if (priv->gov == gov) {
			list_del(&priv->entry);
			kfree(priv);
			break;
		}
	}

	mutex_unlock(&governor_lock);
}
EXPORT_SYMBOL_GPL(watchdog_unregister_governor);

static ssize_t pretimeout_governor_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	struct watchdog_device *wdd = dev_get_drvdata(dev);
	struct governor_priv *priv;

	mutex_lock(&governor_lock);

	priv = find_governor_by_name(buf);
	if (!priv) {
		mutex_unlock(&governor_lock);
		return -EINVAL;
	}

	if (!try_module_get(priv->gov->owner)) {
		mutex_unlock(&governor_lock);
		return -ENODEV;
	}

	spin_lock_irq(&pretimeout_lock);
	if (wdd->gov)
		module_put(wdd->gov->owner);
	wdd->gov = priv->gov;
	spin_unlock_irq(&pretimeout_lock);

	mutex_unlock(&governor_lock);

	return count;
}

static ssize_t pretimeout_governor_show(struct device *dev,
				struct device_attribute *devattr, char *buf)
{
	struct watchdog_device *wdd = dev_get_drvdata(dev);
	char gov_name[WATCHDOG_GOV_NAME_LEN] = { 0 };
	int count;

	mutex_lock(&governor_lock);

	spin_lock_irq(&pretimeout_lock);
	if (wdd->gov)
		strncpy(gov_name, wdd->gov->name, WATCHDOG_GOV_NAME_LEN);
	spin_unlock_irq(&pretimeout_lock);

	count = sprintf(buf, "%s\n", gov_name);

	mutex_unlock(&governor_lock);

	return count;
}
static DEVICE_ATTR_RW(pretimeout_governor);

static ssize_t pretimeout_available_governors_show(struct device *dev,
				struct device_attribute *devattr, char *buf)
{
	struct governor_priv *priv;
	int count = 0;

	mutex_lock(&governor_lock);

	list_for_each_entry(priv, &governor_list, entry)
		count += sprintf(buf + count, "%s\n", priv->gov->name);

	mutex_unlock(&governor_lock);

	return count;
}
static DEVICE_ATTR_RO(pretimeout_available_governors);

static struct attribute *wdd_pretimeout_attrs[] = {
	&dev_attr_pretimeout_governor.attr,
	&dev_attr_pretimeout_available_governors.attr,
	NULL,
};

static umode_t wdd_pretimeout_attr_is_visible(struct kobject *kobj,
				   struct attribute *attr, int n)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct watchdog_device *wdd = dev_get_drvdata(dev);

	if (wdd->info->options & WDIOF_PRETIMEOUT)
		return attr->mode;

	return 0;
}

static const struct attribute_group wdd_pretimeout_group = {
	.is_visible = wdd_pretimeout_attr_is_visible,
	.attrs = wdd_pretimeout_attrs,
};

static const struct attribute_group *wdd_pretimeout_groups[] = {
	&wdd_pretimeout_group,
	NULL,
};

int watchdog_register_pretimeout(struct watchdog_device *wdd, struct device *dev)
{
	struct watchdog_governor *gov;
	int ret;

	if (!(wdd->info->options & WDIOF_PRETIMEOUT))
		return 0;

	ret = sysfs_create_groups(&dev->kobj, wdd_pretimeout_groups);
	if (ret)
		return ret;

	mutex_lock(&governor_lock);
	gov = find_default_governor();
	if (gov && !try_module_get(gov->owner)) {
		mutex_unlock(&governor_lock);
		return -ENODEV;
	}

	spin_lock_irq(&pretimeout_lock);
	wdd->gov = gov;
	spin_unlock_irq(&pretimeout_lock);

	mutex_unlock(&governor_lock);

	return 0;
}

void watchdog_unregister_pretimeout(struct watchdog_device *wdd)
{
	if (!(wdd->info->options & WDIOF_PRETIMEOUT))
		return;

	spin_lock_irq(&pretimeout_lock);
	if (wdd->gov)
		module_put(wdd->gov->owner);
	wdd->gov = NULL;
	spin_unlock_irq(&pretimeout_lock);
}

MODULE_AUTHOR("Vladimir Zapolskiy <vladimir_zapolskiy@mentor.com>");
MODULE_DESCRIPTION("Watchdog pretimeout governor framework");
MODULE_LICENSE("GPL");
