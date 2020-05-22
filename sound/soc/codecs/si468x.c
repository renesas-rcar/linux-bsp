/*
 * Dummy sound driver for Si468x DAB/FM/AM chips
 * Copyright 2016 Andrey Gusakov <andrey.gusakov@cogentembedded.com>
 *
 * Based on: Driver for the DFBM-CS320 bluetooth module
 * Copyright 2011 Lars-Peter Clausen <lars@metafoo.de>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <sound/soc.h>

static struct snd_soc_dai_driver si468x_dai = {
	.name = "si468x-pcm",
	.capture = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
};

static struct snd_soc_component_driver soc_component_dev_si468x;

static int si468x_probe(struct platform_device *pdev)
{
	return devm_snd_soc_register_component(&pdev->dev,
					       &soc_component_dev_si468x,
					       &si468x_dai, 1);
}

static const struct of_device_id si468x_of_match[] = {
	{ .compatible = "si,si468x-pcm", },
	{ }
};
MODULE_DEVICE_TABLE(of, si468x_of_match);

static struct platform_driver si468x_driver = {
	.driver = {
		.name = "si468x",
		.of_match_table = si468x_of_match,
		.owner = THIS_MODULE,
	},
	.probe = si468x_probe,
};

module_platform_driver(si468x_driver);

MODULE_AUTHOR("Andrey Gusakov <andrey.gusakov@cogentembedded.com>");
MODULE_DESCRIPTION("ASoC Si468x radio chip driver");
MODULE_LICENSE("GPL");
