/*
 * loox720.c -- SoC audio for Loox 720
 * for now only two speakers and the headphone jack are supported
 *
 * Copyright 2011 Alexander Tarasikov <alexander.tarasikov@gmail.com>
 *
 * based on loox driver from 2.6.26 and tosa.c which is
 * Copyright 2005 Wolfson Microelectronics PLC.
 * Copyright 2005 Openedhand Ltd.
 *
 * Authors: Liam Girdwood <lrg@slimlogic.co.uk>
 *          Richard Purdie <richard@openedhand.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/list.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/jack.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>

#include <asm/mach-types.h>
#include <mach/loox720.h>
#include "../codecs/wm8750.h"
#include "pxa2xx-i2s.h"

enum {
	LOOX_SPK_OFF, LOOX_SPK_ON,
};

 /* audio clock in Hz - rounded from 12.235MHz */
#define LOOX_AUDIO_CLOCK 12288000

#define EARPIECE_SPK_NAME "Earpiece"
#define HP_NAME "Headphone"
#define SPK_NAME "Speaker"

static int loox720_spk_func = LOOX_SPK_OFF;
static int loox720_earpiece_func = LOOX_SPK_OFF;

static struct snd_soc_card snd_soc_loox720;

static void loox720_ext_control(struct snd_soc_codec *codec)
{
	if (loox720_earpiece_func == LOOX_SPK_ON) {
		snd_soc_dapm_enable_pin(codec, EARPIECE_SPK_NAME);
	}
	else {
		snd_soc_dapm_disable_pin(codec, EARPIECE_SPK_NAME);
	}

	if (loox720_spk_func == LOOX_SPK_ON) {
		snd_soc_dapm_enable_pin(codec, SPK_NAME);
	}
	else {
		snd_soc_dapm_disable_pin(codec, SPK_NAME);
	}
	snd_soc_dapm_sync(codec);
}

static int loox720_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;

	/* check the jack status at stream startup */
	loox720_ext_control(codec);
	return 0;
}

static int loox720_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	unsigned int clk = 0;
	int ret = 0;

	switch (params_rate(params)) {
	case 8000:
	case 16000:
	case 48000:
	case 96000:
		clk = 12288000;
		break;
	case 11025:
	case 22050:
	case 44100:
		clk = 11289600;
		break;
	}

	/* set codec DAI configuration */
	ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S |
		SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0)
		return ret;

	/* set cpu DAI configuration */
	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S |
		SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0)
		return ret;

	/* set the codec system clock for DAC and ADC */
	ret = snd_soc_dai_set_sysclk(codec_dai, WM8750_SYSCLK, clk,
		SND_SOC_CLOCK_IN);
	if (ret < 0)
		return ret;

	/* set the I2S system clock as input (unused) */
	ret = snd_soc_dai_set_sysclk(cpu_dai, PXA2XX_I2S_SYSCLK, 0,
		SND_SOC_CLOCK_IN);
	if (ret < 0)
		return ret;

	return 0;
}

static struct snd_soc_ops loox720_ops = {
	.startup = loox720_startup,
	.hw_params = loox720_hw_params,
};

static int loox720_get_spk(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = loox720_spk_func;
	return 0;
}

static int loox720_set_spk(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);

	if (loox720_spk_func == ucontrol->value.integer.value[0])
		return 0;

	loox720_spk_func = ucontrol->value.integer.value[0];
	loox720_ext_control(codec);
	return 1;
}

static int loox720_get_earpiece(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = loox720_earpiece_func;
	return 0;
}

static int loox720_set_earpiece(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec =  snd_kcontrol_chip(kcontrol);

	if (loox720_earpiece_func == ucontrol->value.integer.value[0])
		return 0;

	loox720_earpiece_func = ucontrol->value.integer.value[0];
	loox720_ext_control(codec);
	return 1;
}

/* Headphones jack detection DAPM pins */
static struct snd_soc_jack_pin hs_jack_pins[] = {
	{
		.pin    = HP_NAME,
		.mask   = SND_JACK_HEADPHONE,
	},
//	{
//		.pin	= SPK_NAME,
//		.mask	= SND_JACK_HEADPHONE,
//		.invert	= 1,
//	}
};

/* Headphones jack detection gpios */
static struct snd_soc_jack_gpio hs_jack_gpios[] = {
	[0] = {
		/* gpio is set on per-platform basis */
		.name           = "hp-gpio",
		.report         = SND_JACK_HEADPHONE,
		.debounce_time	= 200,
		.gpio		= GPIO_LOOX720_HEADPHONE_DET,
	},
};

/* loox720 machine dapm widgets */
static const struct snd_soc_dapm_widget wm8750_dapm_widgets[] = {
	SND_SOC_DAPM_HP(HP_NAME, NULL),
	SND_SOC_DAPM_SPK(SPK_NAME, NULL),
	SND_SOC_DAPM_SPK(EARPIECE_SPK_NAME, NULL),
};

static struct snd_soc_jack hs_jack;

/* loox720 machine audio_map */
static const struct snd_soc_dapm_route audio_map[] = {

	{HP_NAME, NULL, "LOUT1"},
	{HP_NAME, NULL, "ROUT1"},

	{EARPIECE_SPK_NAME, NULL , "ROUT2"},
	{EARPIECE_SPK_NAME, NULL , "LOUT2"},

	{SPK_NAME, NULL, "ROUT1"},
	{SPK_NAME, NULL, "OUT3"},
};

static const char *spk_function[] = {"Off", "On"};
static const char *earpiece_function[] = {"Off", "On"};
static const struct soc_enum loox720_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(spk_function), spk_function),
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(earpiece_function), earpiece_function),
};

static const struct snd_kcontrol_new wm8750_loox720_controls[] = {
	SOC_ENUM_EXT("Main Speaker Function", loox720_enum[0], loox720_get_spk,
		loox720_set_spk),
	SOC_ENUM_EXT("VoIP Speaker Function", loox720_enum[1], loox720_get_earpiece,
		loox720_set_earpiece),
};

static int loox720_wm8750_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	int err;
	struct snd_soc_card *card = rtd->card;

	/* Add loox720 specific controls */
	err = snd_soc_add_controls(codec, wm8750_loox720_controls,
				ARRAY_SIZE(wm8750_loox720_controls));
	if (err)
		return err;

	/* Add loox720 specific widgets */
	err = snd_soc_dapm_new_controls(codec, wm8750_dapm_widgets,
				  ARRAY_SIZE(wm8750_dapm_widgets));
	if (err)
		return err;


	/* Set up loox720 specific audio paths */
	err= snd_soc_dapm_add_routes(codec, audio_map, ARRAY_SIZE(audio_map));
	if (err)
		return err;

	err = snd_soc_dapm_sync(codec);
	if (err)
		return err;

	/* Jack detection API stuff */
	err = snd_soc_jack_new(card, "Headphone Jack",
				SND_JACK_HEADPHONE, &hs_jack);
	if (err)
		return err;

	err = snd_soc_jack_add_pins(&hs_jack, ARRAY_SIZE(hs_jack_pins),
				hs_jack_pins);
	if (err)
		return err;

	err = snd_soc_jack_add_gpios(&hs_jack, ARRAY_SIZE(hs_jack_gpios),
				hs_jack_gpios);

	return err;
}

int loox_snd_suspend_post(struct platform_device *pdev, pm_message_t state) {
	gpio_direction_output(LOOX720_EGPIO_SOUND, 0);
	gpio_direction_output(LOOX720_EGPIO_SOUND_AMP, 0);
	return 0;
}

int loox_snd_resume_pre(struct platform_device *pdev) {
	gpio_direction_output(LOOX720_EGPIO_SOUND, 1);
	gpio_direction_output(LOOX720_EGPIO_SOUND_AMP, 1);
	return 0;
}

/* loox720 digital audio interface glue - connects codec <--> CPU */
static struct snd_soc_dai_link loox720_dai = {
	.name = "wm8750",
	.stream_name = "WM8750",
	//.cpu_dai = &pxa_i2s_dai,
	//.codec_dai = &wm8750_dai,
	.init = loox720_wm8750_init,
	.ops = &loox720_ops,
	.cpu_dai_name = "pxa-is2",
	.codec_dai_name = "wm8750-hifi",
	.platform_name = "pxa-pcm-audio",
	.codec_name = "wm8750-codec.0-001a",
};

/* loox720 audio machine driver */
static struct snd_soc_card snd_soc_loox720 = {
	.name = "loox720",
	//.platform = &pxa2xx_soc_platform,
	.dai_link = &loox720_dai,
	.num_links = 1,
	.suspend_post = loox_snd_suspend_post,
	.resume_pre = loox_snd_resume_pre,
};

/* loox720 audio private data */
/*static struct wm8750_setup_data loox720_wm8750_setup = {
	.i2c_bus = 0,
	.i2c_address = 0x1a,
};*/

/* loox720 audio subsystem */
/*static struct snd_soc_device loox720_snd_devdata = {
	.card = &snd_soc_loox720,
	.codec_dev = &soc_codec_dev_wm8750,
	.codec_data = &loox720_wm8750_setup,
};*/

static struct platform_device *loox720_snd_device;

static int __init loox720_init(void)
{
	int ret;

	if (!(machine_is_loox720()))
		return -ENODEV;

	loox720_snd_device = platform_device_alloc("soc-audio", -1);
	if (!loox720_snd_device)
		return -ENOMEM;

	ret = gpio_request(LOOX720_EGPIO_SOUND, "Loox 720 sound");
	if (ret)
		goto fail;

	ret = gpio_request(LOOX720_EGPIO_SOUND_AMP, "Loox 720 sound amplifier");
	if (ret)
		goto fail;


	platform_set_drvdata(loox720_snd_device, &snd_soc_loox720);
	//loox720_snd_devdata.dev = &loox720_snd_device->dev;

	gpio_direction_output(LOOX720_EGPIO_SOUND, 1);
	gpio_direction_output(LOOX720_EGPIO_SOUND_AMP, 1);

	ret = platform_device_add(loox720_snd_device);
	if (!ret)
		return ret;

	gpio_direction_output(LOOX720_EGPIO_SOUND, 0);
	gpio_direction_output(LOOX720_EGPIO_SOUND_AMP, 0);

fail:
	platform_device_put(loox720_snd_device);

	return ret;
}

static void __exit loox720_exit(void)
{
	platform_device_unregister(loox720_snd_device);
}

module_init(loox720_init);
module_exit(loox720_exit);

MODULE_AUTHOR("Richard Purdie");
MODULE_DESCRIPTION("ALSA SoC loox720");
MODULE_LICENSE("GPL");
