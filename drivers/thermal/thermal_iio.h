/*
 * thermal iio interface driver interface file
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

#ifndef __THERMAL_IIO_H__
#define __THERMAL_IIO_H__

#if defined(CONFIG_THERMAL_IIO)

int thermal_iio_sensor_register(struct thermal_zone_device *tz);
int thermal_iio_sensor_unregister(struct thermal_zone_device *tz);
int thermal_iio_sensor_notify(struct thermal_zone_device *tz,
			      enum thermal_device_event_type event);

#else

static int thermal_iio_sensor_register(struct thermal_zone_device *tz)
{
	return 0;
}

static int thermal_iio_sensor_unregister(struct thermal_zone_device *tz)
{
	return 0;
}

static int thermal_iio_sensor_notify(struct thermal_zone_device *tz,
				     enum thermal_device_event_type event)

{
	return 0;
}

#endif
#endif
