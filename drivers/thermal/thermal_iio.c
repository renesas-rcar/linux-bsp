/*
 * thermal iio interface driver
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/thermal.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger.h>
#include <linux/iio/events.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

struct thermal_iio_data {
	struct thermal_zone_device *tz;
	struct iio_trigger *interrupt_trig;
	struct iio_chan_spec *channels;
	bool enable_trigger;
	int temp_thres;
	bool ev_enable_state;
	struct mutex mutex;
};

static const struct iio_event_spec thermal_event = {
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_EITHER,
		.mask_separate = BIT(IIO_EV_INFO_VALUE) |
				 BIT(IIO_EV_INFO_ENABLE)
};

static const struct iio_chan_spec thermal_iio_channels[] = {
	{
		.type = IIO_TEMP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.scan_index = 0,
		.scan_type = {
			.sign = 'u',
			.realbits = 32,
			.storagebits = 32,
			.endianness = IIO_CPU,
		}
	},
	IIO_CHAN_SOFT_TIMESTAMP(1)
};

static const struct iio_chan_spec thermal_iio_channels_with_events[] = {
	{
		.type = IIO_TEMP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.scan_index = 0,
		.scan_type = {
			.sign = 'u',
			.realbits = 32,
			.storagebits = 32,
			.endianness = IIO_CPU,
		},
		.event_spec = &thermal_event,
		.num_event_specs = 1,

	},
	IIO_CHAN_SOFT_TIMESTAMP(1)
};

static int thermal_iio_read_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan,
				int *val, int *val2, long mask)
{
	struct thermal_iio_data *iio_data = iio_priv(indio_dev);
	int temp;
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = thermal_zone_get_temp(iio_data->tz, &temp);
		if (!ret) {
			*val = temp;
			ret = IIO_VAL_INT;
		}
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static irqreturn_t thermal_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct thermal_iio_data *iio_data = iio_priv(indio_dev);
	u32 buffer[4];
	int temp;
	int ret;

	ret = thermal_zone_get_temp(iio_data->tz, &temp);
	if (ret)
		goto err_read;

	*(u32 *)buffer = temp;
	iio_push_to_buffers_with_timestamp(indio_dev, buffer,
					   iio_get_time_ns());
err_read:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static int thermal_data_rdy_trigger_set_state(struct iio_trigger *trig,
					      bool state)
{
	struct iio_dev *indio_dev = iio_trigger_get_drvdata(trig);
	struct thermal_iio_data *iio_data = iio_priv(indio_dev);
	int ret = 0;

	mutex_lock(&iio_data->mutex);
	if (iio_data->tz->ops->set_notification_status) {
		ret = iio_data->tz->ops->set_notification_status(iio_data->tz,
								 state);
		if (!ret)
			iio_data->enable_trigger = state;
	} else
		iio_data->enable_trigger = state;
	mutex_unlock(&iio_data->mutex);

	return ret;
}

static int thermal_iio_validate_trigger(struct iio_dev *indio_dev,
					struct iio_trigger *trig)
{
	struct thermal_iio_data *iio_data = iio_priv(indio_dev);

	if (iio_data->interrupt_trig && iio_data->interrupt_trig != trig)
		return -EINVAL;

	return 0;
}

static const struct iio_trigger_ops thermal_trigger_ops = {
	.set_trigger_state = thermal_data_rdy_trigger_set_state,
	.owner = THIS_MODULE,
};

static int thermal_iio_read_event(struct iio_dev *indio_dev,
				  const struct iio_chan_spec *chan,
				  enum iio_event_type type,
				  enum iio_event_direction dir,
				  enum iio_event_info info,
				  int *val, int *val2)
{
	struct thermal_iio_data *iio_data = iio_priv(indio_dev);
	int ret;

	mutex_lock(&iio_data->mutex);
	switch (info) {
	case IIO_EV_INFO_VALUE:
		*val = iio_data->temp_thres;
		ret = IIO_VAL_INT;
		break;
	default:
		ret = -EINVAL;
	}
	mutex_unlock(&iio_data->mutex);

	return ret;
}

static int thermal_iio_write_event(struct iio_dev *indio_dev,
				   const struct iio_chan_spec *chan,
				   enum iio_event_type type,
				   enum iio_event_direction dir,
				   enum iio_event_info info,
				   int val, int val2)
{
	struct thermal_iio_data *iio_data = iio_priv(indio_dev);
	int ret = 0;

	mutex_lock(&iio_data->mutex);
	switch (info) {
	case IIO_EV_INFO_VALUE:
		iio_data->temp_thres = val;
		break;
	default:
		ret = -EINVAL;
	}
	mutex_unlock(&iio_data->mutex);

	return ret;
}

static int thermal_iio_read_event_config(struct iio_dev *indio_dev,
					 const struct iio_chan_spec *chan,
					 enum iio_event_type type,
					 enum iio_event_direction dir)
{
	struct thermal_iio_data *iio_data = iio_priv(indio_dev);
	bool state;

	mutex_lock(&iio_data->mutex);
	state = iio_data->ev_enable_state;
	mutex_unlock(&iio_data->mutex);

	return state;
}

static int thermal_iio_write_event_config(struct iio_dev *indio_dev,
					  const struct iio_chan_spec *chan,
					  enum iio_event_type type,
					  enum iio_event_direction dir,
					  int state)
{
	struct thermal_iio_data *iio_data = iio_priv(indio_dev);
	int temp_thres;
	int ret = 0;

	mutex_lock(&iio_data->mutex);
	if ((state && iio_data->ev_enable_state) ||
	    (!state && !iio_data->ev_enable_state))
		goto done_write_event;

	if (state && !iio_data->temp_thres) {
		ret = -EINVAL;
		goto done_write_event;
	}

	if (state)
		temp_thres = iio_data->temp_thres;
	else
		temp_thres = 0;

	ret = iio_data->tz->ops->set_threshold_temp(iio_data->tz, 0,
						    temp_thres);
	if (ret)
		goto done_write_event;

	if (iio_data->tz->ops->set_notification_status) {
		ret = iio_data->tz->ops->set_notification_status(iio_data->tz,
								 state > 0);
		if (!ret)
			iio_data->ev_enable_state = state;
	} else
		iio_data->ev_enable_state = state;

done_write_event:
	mutex_unlock(&iio_data->mutex);

	return ret;
}

static int thermal_iio_setup_trig(struct iio_trigger **iio_trig,
				  struct thermal_zone_device *tz,
				  const char *format)
{
	struct iio_trigger *trig;
	int ret;

	trig = devm_iio_trigger_alloc(&tz->device, format, tz->type,
				      tz->indio_dev->id);
	if (!trig) {
		dev_err(&tz->device, "Trigger Allocate Failed\n");
		return -ENOMEM;
	}
	trig->dev.parent = &tz->device;
	trig->ops = &thermal_trigger_ops;
	iio_trigger_set_drvdata(trig, tz->indio_dev);
	ret = iio_trigger_register(trig);
	if (ret) {
		dev_err(&tz->device, "Trigger Allocate Failed\n");
		return ret;
	}
	*iio_trig = trig;

	return 0;
}

static const struct iio_info thermal_iio_info = {
	.read_raw		= thermal_iio_read_raw,
	.read_event_value	= thermal_iio_read_event,
	.write_event_value	= thermal_iio_write_event,
	.write_event_config	= thermal_iio_write_event_config,
	.read_event_config	= thermal_iio_read_event_config,
	.validate_trigger	= thermal_iio_validate_trigger,
	.driver_module		= THIS_MODULE,
};

int thermal_iio_sensor_register(struct thermal_zone_device *tz)
{
	struct thermal_iio_data *iio_data;
	int ret;

	tz->indio_dev = devm_iio_device_alloc(&tz->device, sizeof(*iio_data));
	if (!tz->indio_dev)
		return -ENOMEM;

	iio_data = iio_priv(tz->indio_dev);
	iio_data->tz = tz;
	mutex_init(&iio_data->mutex);

	tz->indio_dev->dev.parent = &tz->device;
	if (tz->ops->set_threshold_temp)
		tz->indio_dev->channels = thermal_iio_channels_with_events;
	else
		tz->indio_dev->channels = thermal_iio_channels;
	tz->indio_dev->num_channels = ARRAY_SIZE(thermal_iio_channels);
	tz->indio_dev->name = tz->type;
	tz->indio_dev->info = &thermal_iio_info;
	tz->indio_dev->modes = INDIO_DIRECT_MODE;

	if (tz->ops->check_notification_support &&
	    tz->ops->check_notification_support(tz)) {
		ret = thermal_iio_setup_trig(&iio_data->interrupt_trig, tz,
					     "%s-dev%d");
		if (ret)
			return ret;

		tz->indio_dev->trig = iio_trigger_get(iio_data->interrupt_trig);
	}
	ret = iio_triggered_buffer_setup(tz->indio_dev,
					 &iio_pollfunc_store_time,
					 thermal_trigger_handler, NULL);
	if (ret) {
		dev_err(&tz->device, "failed to init trigger buffer\n");
		goto err_unreg_int_trig;
	}
	ret = iio_device_register(tz->indio_dev);
	if (ret < 0) {
		dev_err(&tz->device, "unable to register iio device\n");
		goto err_cleanup_trig;
	}

	return 0;

err_cleanup_trig:
	iio_triggered_buffer_cleanup(tz->indio_dev);
err_unreg_int_trig:
	if (iio_data->interrupt_trig)
		iio_trigger_unregister(iio_data->interrupt_trig);

	return ret;
}

int thermal_iio_sensor_unregister(struct thermal_zone_device *tz)
{
	struct thermal_iio_data *iio_data = iio_priv(tz->indio_dev);

	iio_device_unregister(tz->indio_dev);
	iio_triggered_buffer_cleanup(tz->indio_dev);
	if (iio_data->interrupt_trig)
		iio_trigger_unregister(iio_data->interrupt_trig);

	return 0;
}

int thermal_iio_sensor_notify(struct thermal_zone_device *tz,
			      enum thermal_device_event_type event)
{
	struct thermal_iio_data *iio_data = iio_priv(tz->indio_dev);

	mutex_lock(&iio_data->mutex);
	if (iio_data->ev_enable_state &&
	    event == THERMAL_DEVICE_EVENT_THRESHOLD)
		iio_push_event(tz->indio_dev,
			       IIO_UNMOD_EVENT_CODE(IIO_TEMP, 0,
						    IIO_EV_TYPE_THRESH,
						    IIO_EV_DIR_EITHER),
			       iio_get_time_ns());
	if (iio_data->enable_trigger)
		iio_trigger_poll(tz->indio_dev->trig);
	mutex_unlock(&iio_data->mutex);

	return 0;
}
