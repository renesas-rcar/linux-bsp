/*
 * MAXIM max9286 GMSL driver
 *
 * Copyright (C) 2015-2020 Cogent Embedded, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

static const char *line_status[] =
{
	"BAT",
	"GND",
	"NORMAL",
	"OPEN"
};

static ssize_t max9286_link_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	int i = -1;
	u8 val = 0;
	bool lenghterr, linebuffof, hlocked, prbsok, vsyncdet, configdet, videodet;
	int lf;
	u8 prbserr = 0, deterr = 0, correrr = 0;
	struct i2c_client *client = to_i2c_client(dev);
	struct max9286_priv *priv = i2c_get_clientdata(client);

	if (!sscanf(attr->attr.name, "link_%d", &i))
		return -EINVAL;

	if ((i < 0) || (i > 3))
		return -EINVAL;

	reg8_read(client, 0x20, &val);
	lf = (val >> (2 * i)) & 0x03;

	reg8_read(client, 0x21, &val);
	hlocked = !!(val & (1 << i));
	prbsok = !!(val & (1 << (i + 4)));

	reg8_read(client, 0x22, &val);
	lenghterr = !!(val & (1 << i));
	linebuffof = !!(val & (1 << (i + 4)));

	reg8_read(client, 0x23 + i, &prbserr);
	priv->prbserr[i] += prbserr;

	reg8_read(client, 0x27, &val);
	vsyncdet = !!(val & (1 << i));

	reg8_read(client, 0x28 + i, &deterr);
	priv->deterr[i] += deterr;

	reg8_read(client, 0x2c + i, &correrr);
	priv->correrr[i] += correrr;

	reg8_read(client, 0x49, &val);
	configdet = !!(val & (1 << i));
	videodet = !!(val & (1 << (i + 4)));

	return sprintf(buf, "LINK:%d LF:%s HLOCKED:%d PRBSOK:%d LINBUFFOF:%d"
		" LENGHTERR:%d VSYNCDET:%d CONFIGDET:%d VIDEODET:%d"
		" PRBSERR:%d(%d) DETEERR:%d(%d) CORRERR:%d(%d)\n",
		i, line_status[lf], hlocked, prbsok, lenghterr,
		linebuffof, vsyncdet, configdet, videodet,
		priv->prbserr[i], prbserr,
		priv->deterr[i], deterr,
		priv->correrr[i], correrr);
}

static DEVICE_ATTR(link_0, S_IRUGO, max9286_link_show, NULL);
static DEVICE_ATTR(link_1, S_IRUGO, max9286_link_show, NULL);
static DEVICE_ATTR(link_2, S_IRUGO, max9286_link_show, NULL);
static DEVICE_ATTR(link_3, S_IRUGO, max9286_link_show, NULL);

static struct attribute *max9286_attributes_links[] = {
	&dev_attr_link_0.attr,
	&dev_attr_link_1.attr,
	&dev_attr_link_2.attr,
	&dev_attr_link_3.attr,
	NULL
};

static const struct attribute_group max9286_group = {
	.attrs = max9286_attributes_links,
};
