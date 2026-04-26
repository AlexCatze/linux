/* A Template for further Alsa driver for Asus P525
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/gpio.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>

#include <asm/mach-types.h>
#include <mach/hardware.h>
#include <mach/audio.h>

#include "../codecs/wm9713.h"
#include "pxa2xx-ac97.h"

static int spk_amplifier_event(struct snd_soc_dapm_widget *widget,
			  struct snd_kcontrol *kctl, int event)
{
	gpio_set_value(89, SND_SOC_DAPM_EVENT_ON(event));
	return 0;
}

static const struct snd_soc_dapm_widget wm9713_dapm_widgets[] = {
	SND_SOC_DAPM_SPK("Front Speaker", NULL),
	SND_SOC_DAPM_SPK("Rear Speaker", spk_amplifier_event),
	SND_SOC_DAPM_MIC("Headset", NULL),
};

static const struct snd_soc_dapm_route dapm_routes[] = {
	{"Headset", NULL, "HPL"},
	{"Headset", NULL, "HPR"},
	
	{"Front Speaker", NULL, "HPL"},
	{"Front Speaker", NULL, "OUT3"},

	{"Rear Speaker", NULL, "SPKL"},
	{"Rear Speaker", NULL, "HPR"},
};

static int asusp5x5_ac97_init(struct snd_soc_pcm_runtime *rtd)
{
	int err;
	struct snd_soc_codec *codec = rtd->codec;

	err = snd_soc_dapm_new_controls(&codec->dapm, wm9713_dapm_widgets,
				  ARRAY_SIZE(wm9713_dapm_widgets));
	if (err)
		return err;

	err = snd_soc_dapm_add_routes(&codec->dapm, dapm_routes, ARRAY_SIZE(dapm_routes));
	if (err)
		return err;

	snd_soc_dapm_enable_pin(&codec->dapm, "Front Speaker");
	snd_soc_dapm_enable_pin(&codec->dapm, "Rear Speaker");
	snd_soc_dapm_enable_pin(&codec->dapm, "Headset");
	snd_soc_dapm_sync(&codec->dapm);
	return 0;
}

static struct snd_soc_dai_link asusp5x5_dai[] = {
	{
		.name = "AC97",
		.stream_name = "AC97 HiFi",
		.cpu_dai_name = "pxa2xx-ac97",
		.codec_dai_name = "wm9713-hifi",
		.codec_name = "wm9713-codec",
		.platform_name = "pxa-pcm-audio",
		.init = asusp5x5_ac97_init,
	},
	{
		.name = "AC97 Aux",
		.stream_name = "AC97 Aux",
		.cpu_dai_name = "pxa2xx-ac97-aux",
		.codec_name = "wm9713-codec",
		.platform_name = "pxa-pcm-audio",
		.codec_dai_name = "wm9713-aux",
	},
};

static struct snd_soc_card asusp5x5 = {
	.name = "Asus P525",
	.dai_link = asusp5x5_dai,
	.num_links = ARRAY_SIZE(asusp5x5_dai),
};

static struct platform_device *asusp5x5_snd_device;

static int asusp5x5_probe(struct platform_device *pdev)
{
	int ret;

	if (!machine_is_asusp525() && !machine_is_asusp535())
		return -ENODEV;

	printk(KERN_INFO "Asus Sound: Machine passed\n");
	asusp5x5_snd_device = platform_device_alloc("soc-audio", -1);
	
	if (!asusp5x5_snd_device)
		return -ENOMEM;
	printk(KERN_INFO "Asus SND: Device Alloc passed\n");

	platform_set_drvdata(asusp5x5_snd_device, &asusp5x5);
	
	ret = platform_device_add(asusp5x5_snd_device);
	if (!ret)
		return 0;
	printk(KERN_INFO "Asus SND: Platform passed\n");

	
	platform_device_put(asusp5x5_snd_device);
	printk(KERN_INFO "Asus SND: Device Registered");
	return ret;
}

static int __devexit asusp5x5_remove(struct platform_device *pdev)
{
	platform_device_unregister(asusp5x5_snd_device);
	return 0;
}

static struct platform_driver asusp5x5_audio_driver = {
	.probe	= asusp5x5_probe,
	.remove	= asusp5x5_remove,
	.driver	= {
		.name	=	"asusp5x5-audio",
		.owner	=	THIS_MODULE,
	}
};

static int __init asusp5x5_init(void) {
	return platform_driver_register(&asusp5x5_audio_driver);
};

static void __exit asusp5x5_exit(void) {
	platform_driver_unregister(&asusp5x5_audio_driver);
};

module_init(asusp5x5_init);
module_exit(asusp5x5_exit);

/* Module information */
MODULE_AUTHOR("Alexander Tarasikov, alexander.tarasikov@gmail.com");
MODULE_DESCRIPTION("ALSA SoC for Asus P525");
MODULE_LICENSE("GPL");
