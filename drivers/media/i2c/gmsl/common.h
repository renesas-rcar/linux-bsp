/*
 * MAXIM GMSL common header
 *
 * Copyright (C) 2019-2020 Cogent Embedded, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/i2c-mux.h>
#include "max9295.h"

#define MAX9271_ID			0x09
#define MAX9286_ID			0x40
#define MAX9288_ID			0x2A
#define MAX9290_ID			0x2C
#define MAX9295A_ID			0x91
#define MAX9295B_ID			0x93
#define MAX9296A_ID			0x94
#define MAX96705_ID			0x41
#define MAX96706_ID			0x4A
#define MAX96707_ID			0x45 /* MAX96715: same but lack of HS pin */
#define MAX96708_ID			0x4C
#define MAX96712_ID			0x20

#define UB960_ID			0x00 /* strapped */

#define BROADCAST			0x6f

#define REG8_NUM_RETRIES		1 /* number of read/write retries */
#define REG16_NUM_RETRIES		10 /* number of read/write retries */

static inline char* chip_name(int id)
{
	switch (id) {
	case MAX9271_ID:
		return "MAX9271";
	case MAX9286_ID:
		return "MAX9286";
	case MAX9288_ID:
		return "MAX9288";
	case MAX9290_ID:
		return "MAX9290";
	case MAX9295A_ID:
		return "MAX9295A";
	case MAX9295B_ID:
		return "MAX9295B";
	case MAX9296A_ID:
		return "MAX9296A";
	case MAX96705_ID:
		return "MAX96705";
	case MAX96706_ID:
		return "MAX96706";
	case MAX96707_ID:
		return "MAX96707";
	case MAX96712_ID:
		return "MAX96712";
	default:
		return "serializer";
	}
}

enum gmsl_mode {
	MODE_GMSL1 = 1,
	MODE_GMSL2,
};

#define MAXIM_I2C_I2C_SPEED_837KHZ	(0x7 << 2) /* 837kbps */
#define MAXIM_I2C_I2C_SPEED_533KHZ	(0x6 << 2) /* 533kbps */
#define MAXIM_I2C_I2C_SPEED_339KHZ	(0x5 << 2) /* 339 kbps */
#define MAXIM_I2C_I2C_SPEED_173KHZ	(0x4 << 2) /* 174kbps */
#define MAXIM_I2C_I2C_SPEED_105KHZ	(0x3 << 2) /* 105 kbps */
#define MAXIM_I2C_I2C_SPEED_085KHZ	(0x2 << 2) /* 84.7 kbps */
#define MAXIM_I2C_I2C_SPEED_028KHZ	(0x1 << 2) /* 28.3 kbps */
#define MAXIM_I2C_I2C_SPEED		MAXIM_I2C_I2C_SPEED_339KHZ

#define MIPI_DT_GENERIC			0x10
#define MIPI_DT_GENERIC_1		0x11
#define MIPI_DT_EMB			0x12
#define MIPI_DT_YUV8			0x1e
#define MIPI_DT_YUV10			0x1f
#define MIPI_DT_RGB565			0x22
#define MIPI_DT_RGB666			0x23
#define MIPI_DT_RGB888			0x24
#define MIPI_DT_RAW8			0x2a
#define MIPI_DT_RAW10			0x2b
#define MIPI_DT_RAW12			0x2c
#define MIPI_DT_RAW14			0x2d
#define MIPI_DT_RAW16			0x2e
#define MIPI_DT_RAW20			0x2f
#define MIPI_DT_YUV12			0x30

static inline int mipi_dt_to_bpp(unsigned int dt)
{
	switch (dt) {
		case 0x2a:
		case 0x10 ... 0x12:
		case 0x31 ... 0x37:
			return 0x08;
		case 0x2b:
			return 0x0a;
		case 0x2c:
			return 0x0c;
		case 0x0d:
			return 0x0e;
		case 0x22:
		case 0x1e:
		case 0x2e:
			return 0x10;
		case 0x23:
			return 0x12;
		case 0x1f:
		case 0x2f:
			return 0x14;
		case 0x24:
		case 0x30:
			return 0x18;
		default:
			return 0x08;
	}
}

static inline int reg8_read(struct i2c_client *client, u8 reg, u8 *val)
{
	int ret, retries;

	for (retries = REG8_NUM_RETRIES; retries; retries--) {
		ret = i2c_smbus_read_byte_data(client, reg);
		if (!(ret < 0))
			break;
	}

	if (ret < 0) {
		dev_dbg(&client->dev,
			"read fail: chip 0x%x register 0x%x: %d\n",
			client->addr, reg, ret);
	} else {
		*val = ret;
	}

	return ret < 0 ? ret : 0;
}

static inline int reg8_write(struct i2c_client *client, u8 reg, u8 val)
{
	int ret, retries;

	for (retries = REG8_NUM_RETRIES; retries; retries--) {
		ret = i2c_smbus_write_byte_data(client, reg, val);
		if (!(ret < 0))
			break;
	}

	if (ret < 0) {
		dev_dbg(&client->dev,
			"write fail: chip 0x%x register 0x%x: %d\n",
			client->addr, reg, ret);
	} else {
#ifdef WRITE_VERIFY
		u8 val2;
		reg8_read(client, reg, &val2);
		if (val != val2)
			dev_err(&client->dev,
				"write verify mismatch: chip 0x%x reg=0x%x "
				"0x%x->0x%x\n", client->addr, reg, val, val2);
#endif
	}

	return ret < 0 ? ret : 0;
}

static inline int reg16_read(struct i2c_client *client, u16 reg, u8 *val)
{
	int ret, retries;
	u8 buf[2] = {reg >> 8, reg & 0xff};

	for (retries = REG16_NUM_RETRIES; retries; retries--) {
		ret = i2c_master_send(client, buf, 2);
		if (ret == 2) {
			ret = i2c_master_recv(client, buf, 1);
			if (ret == 1)
				break;
		}
	}

	if (ret < 0) {
		dev_dbg(&client->dev,
			"read fail: chip 0x%x register 0x%x: %d\n",
			client->addr, reg, ret);
	} else {
		*val = buf[0];
	}

	return ret < 0 ? ret : 0;
}

static inline int reg16_write(struct i2c_client *client, u16 reg, u8 val)
{
	int ret, retries;
	u8 buf[3] = {reg >> 8, reg & 0xff, val};

	for (retries = REG16_NUM_RETRIES; retries; retries--) {
		ret = i2c_master_send(client, buf, 3);
		if (ret == 3)
			break;
	}

	if (ret < 0) {
		dev_dbg(&client->dev,
			"write fail: chip 0x%x register 0x%x: %d\n",
			client->addr, reg, ret);
	} else {
#ifdef WRITE_VERIFY
		u8 val2;
		reg16_read(client, reg, &val2);
		if (val != val2)
			dev_err(&client->dev,
				"write verify mismatch: chip 0x%x reg=0x%x "
				"0x%x->0x%x\n", client->addr, reg, val, val2);
#endif
	}

	return ret < 0 ? ret : 0;
}

static inline int reg16_read16(struct i2c_client *client, u16 reg, u16 *val)
{
	int ret, retries;
	u8 buf[2] = {reg >> 8, reg & 0xff};

	for (retries = REG8_NUM_RETRIES; retries; retries--) {
		ret = i2c_master_send(client, buf, 2);
		if (ret == 2) {
			ret = i2c_master_recv(client, buf, 2);
			if (ret == 2)
				break;
		}
	}

	if (ret < 0) {
		dev_dbg(&client->dev,
			"read fail: chip 0x%x register 0x%x: %d\n",
			client->addr, reg, ret);
	} else {
		*val = ((u16)buf[0] << 8) | buf[1];
	}

	return ret < 0 ? ret : 0;
}

static inline int reg16_write16(struct i2c_client *client, u16 reg, u16 val)
{
	int ret, retries;
	u8 buf[4] = {reg >> 8, reg & 0xff, val >> 8, val & 0xff};

	for (retries = REG8_NUM_RETRIES; retries; retries--) {
		ret = i2c_master_send(client, buf, 4);
		if (ret == 4)
			break;
	}

	if (ret < 0) {
		dev_dbg(&client->dev,
			"write fail: chip 0x%x register 0x%x: %d\n",
			client->addr, reg, ret);
	}

	return ret < 0 ? ret : 0;
}

static inline int reg16_read_n(struct i2c_client *client, u16 reg, u8 *val, int n)
{
	int ret, retries;
	u8 buf[2] = {reg >> 8, reg & 0xff};

	for (retries = REG16_NUM_RETRIES; retries; retries--) {
		ret = i2c_master_send(client, buf, 2);
		if (ret == 2) {
			ret = i2c_master_recv(client, val, n);
			if (ret == n)
				break;
		}
	}

	if (ret < 0) {
		dev_dbg(&client->dev,
			"read fail: chip 0x%x registers 0x%x-0x%x: %d\n",
			client->addr, reg, reg + n, ret);
	}

	return ret < 0 ? ret : 0;
}

static inline int reg16_write_n(struct i2c_client *client, u16 reg, const u8* val, int n)
{
	int ret, retries;
	u8 buf[8];

	buf[0] = reg >> 8;
	buf[1] = reg & 0xff;
	memcpy(&buf[2], val, n);

	for (retries = REG16_NUM_RETRIES; retries; retries--) {
		ret = i2c_master_send(client, buf, 2 + n);
		if (ret == 2 + n)
			break;
	}

	if (ret < 0) {
		dev_dbg(&client->dev,
			"write fail: chip 0x%x register 0x%x-0x%x: %d\n",
			client->addr, reg, reg + n, ret);
	} else {
#ifdef WRITE_VERIFY
		u8 val2[n];
		ret = reg16_read_n(client, reg, val2, n);
		if (ret < 0)
			return ret;

		if (memcmp(val, val2, n)) {
			dev_err(&client->dev,
				"write verify mismatch: chip 0x%x reg=0x%x-0x%x "
				"'%*phN'->'%*phN'\n", client->addr, reg, reg + n,
				n, val, n, val2);
				ret = -EBADE;
		}
#endif
	}

	return ret < 0 ? ret : 0;
}

static inline int reg8_read_addr(struct i2c_client *client, int addr, u8 reg, u8 *val)
{
	int ret, retries;
	union i2c_smbus_data data;

	for (retries = REG8_NUM_RETRIES; retries; retries--) {
		ret = i2c_smbus_xfer(client->adapter, addr, client->flags,
				     I2C_SMBUS_READ, reg, I2C_SMBUS_BYTE_DATA, &data);
		if (!(ret < 0))
			break;
	}

	if (ret < 0) {
		dev_dbg(&client->dev,
			"read fail: chip 0x%x register 0x%x: %d\n",
			addr, reg, ret);
	} else {
		*val = data.byte;
	}

	return ret < 0 ? ret : 0;
}

static inline int reg8_write_addr(struct i2c_client *client, u8 addr, u8 reg, u8 val)
{
	int ret, retries;
	union i2c_smbus_data data;

	data.byte = val;

	for (retries = REG8_NUM_RETRIES; retries; retries--) {
		ret = i2c_smbus_xfer(client->adapter, addr, client->flags,
				     I2C_SMBUS_WRITE, reg, I2C_SMBUS_BYTE_DATA, &data);
		if (!(ret < 0))
			break;
	}

	if (ret < 0) {
		dev_dbg(&client->dev,
			"write fail: chip 0x%x register 0x%x value 0x%0x: %d\n",
			addr, reg, val, ret);
	}

	return ret < 0 ? ret : 0;
}


static inline int reg16_write_addr(struct i2c_client *client, int chip, u16 reg, u8 val)
{
	struct i2c_msg msg[1];
	u8 wbuf[3];
	int ret;

	msg->addr = chip;
	msg->flags = 0;
	msg->len = 3;
	msg->buf = wbuf;
	wbuf[0] = reg >> 8;
	wbuf[1] = reg & 0xff;
	wbuf[2] = val;

	ret = i2c_transfer(client->adapter, msg, 1);
	if (ret < 0) {
		dev_dbg(&client->dev, "i2c fail: chip 0x%02x wr 0x%04x (0x%02x): %d\n",
			chip, reg, val, ret);
		return ret;
	}

	return 0;
}

static inline int reg16_read_addr(struct i2c_client *client, int chip, u16 reg, int *val)
{
	struct i2c_msg msg[2];
	u8 wbuf[2];
	u8 rbuf[1];
	int ret;

	msg[0].addr = chip;
	msg[0].flags = 0;
	msg[0].len = 2;
	msg[0].buf = wbuf;
	wbuf[0] = reg >> 8;
	wbuf[1] = reg & 0xff;

	msg[1].addr = chip;
	msg[1].flags = I2C_M_RD;
	msg[1].len = 1;
	msg[1].buf = rbuf;

	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret < 0) {
		dev_dbg(&client->dev, "i2c fail: chip 0x%02x rd 0x%04x: %d\n", chip, reg, ret);
		return ret;
	}

	*val = rbuf[0];

	return 0;
}

#define __reg8_read(addr, reg, val)		reg8_read_addr(priv->client, addr, reg, val)
#define __reg8_write(addr, reg, val)		reg8_write_addr(priv->client, addr, reg, val)
#define __reg16_read(addr, reg, val)		reg16_read_addr(priv->client, addr, reg, val)
#define __reg16_write(addr, reg, val)		reg16_write_addr(priv->client, addr, reg, val)

/* copy this struct from drivers/i2c/i2c-mux.c for getting muxc from adapter private data */
struct i2c_mux_priv {
	struct i2c_adapter adap;
	struct i2c_algorithm algo;
	struct i2c_mux_core *muxc;
	u32 chan_id;
};

static inline int get_des_id(struct i2c_client *client)
{
	struct i2c_mux_priv *mux_priv = client->adapter->algo_data;

	if (!strcmp(mux_priv->muxc->dev->driver->name, "max9286"))
		return MAX9286_ID;
	if (!strcmp(mux_priv->muxc->dev->driver->name, "max9288"))
		return MAX9288_ID;
	if (!strcmp(mux_priv->muxc->dev->driver->name, "max9296"))
		return MAX9296A_ID;
	if (!strcmp(mux_priv->muxc->dev->driver->name, "max96706"))
		return MAX96706_ID;
	if (!strcmp(mux_priv->muxc->dev->driver->name, "max96712"))
		return MAX96712_ID;
	if (!strcmp(mux_priv->muxc->dev->driver->name, "ub9x4"))
		return UB960_ID;

	return -EINVAL;
}

static inline int get_des_addr(struct i2c_client *client)
{
	struct i2c_mux_priv *mux_priv = client->adapter->algo_data;

	return to_i2c_client(mux_priv->muxc->dev)->addr;
}

static inline void setup_i2c_translator(struct i2c_client *client, int ser_addr, int sensor_addr)
{
	int gmsl_mode = MODE_GMSL2;

	switch (get_des_id(client)) {
	case MAX9286_ID:
	case MAX9288_ID:
	case MAX96706_ID:
		reg8_write_addr(client, ser_addr, 0x09, client->addr << 1);	/* Sensor translated I2C address */
		reg8_write_addr(client, ser_addr, 0x0A, sensor_addr << 1);	/* Sensor native I2C address */
		break;
	case MAX9296A_ID:
	case MAX96712_ID:
		/* parse gmsl mode from deserializer */
		reg16_read_addr(client, get_des_addr(client), 6, &gmsl_mode);
		gmsl_mode = !!(gmsl_mode & BIT(7)) + 1;

		if (gmsl_mode == MODE_GMSL1) {
			reg8_write_addr(client, ser_addr, 0x09, client->addr << 1);	/* Sensor translated I2C address */
			reg8_write_addr(client, ser_addr, 0x0A, sensor_addr << 1);	/* Sensor native I2C address */
		}
		if (gmsl_mode == MODE_GMSL2) {
			reg16_write_addr(client, ser_addr, MAX9295_I2C2, client->addr << 1); /* Sensor translated I2C address */
			reg16_write_addr(client, ser_addr, MAX9295_I2C3, sensor_addr << 1); /* Sensor native I2C address */
		}
		break;
	case UB960_ID:
		reg8_write_addr(client, get_des_addr(client), 0x65, client->addr << 1);	/* Sensor translated I2C address */
		reg8_write_addr(client, get_des_addr(client), 0x5d, sensor_addr << 1);	/* Sensor native I2C address */
		break;
	}
	usleep_range(2000, 2500);
}
