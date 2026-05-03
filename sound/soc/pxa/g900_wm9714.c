/*
 * Handles the Toshiba Portege g900 SoC system
 *
 * Copyright (C) 2010 Serhij Stasyuk
 * Based on mioa701_wm9713.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation in version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * This is a little schema of the sound interconnections :
 *
 *    Qualcom                    Wolfson WM9714
 *    +--------+             +-------------------+      Rear Speaker
 *    |        |             |                   |           /-+
 *    |        +--->     >---+                   +--->----+-+  |
 *    |  GSM   |             |                   |        | |  |
 *    |        +--->     >---+                   +--->----+-+  |
 *    |  CHIP  |             |                   |           \-+
 *    |        +---<     <---+                   |
 *    |        |             |                   |      Front Speaker
 *    +--------+             |                   |           /-+
 *                           |                   +--->----+-+  |
 *                           |                   |        | |  |
 *                           |                   +--->----+-+  |
 *                           |                   |           \-+
 *                           |                   |
 *                           |                   |     Front Micro
 *                           |                   |         +
 *                           |                   +-----<--+o+
 *                           |                   |         +
 *                           +-------------------+        ---
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>

#include <asm/mach-types.h>
#include <mach/audio.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/ac97_codec.h>

#include "pxa2xx-pcm.h"
#include "pxa2xx-ac97.h"
#include "../codecs/wm9713.h"

#define ARRAY_AND_SIZE(x)	(x), ARRAY_SIZE(x)

#define AC97_GPIO_PULL		0x58

/* Use GPIO8 for rear speaker amplifier */
//static int rear_amp_power(struct snd_soc_codec *codec, int power)
//{
//	unsigned short reg;
//
//	if (power) {
//		reg = snd_soc_read(codec, AC97_GPIO_CFG);
//		snd_soc_write(codec, AC97_GPIO_CFG, reg | 0x0100);
//		reg = snd_soc_read(codec, AC97_GPIO_PULL);
//		snd_soc_write(codec, AC97_GPIO_PULL, reg | (1<<15));
//	} else {
//		reg = snd_soc_read(codec, AC97_GPIO_CFG);
//		snd_soc_write(codec, AC97_GPIO_CFG, reg & ~0x0100);
//		reg = snd_soc_read(codec, AC97_GPIO_PULL);
//		snd_soc_write(codec, AC97_GPIO_PULL, reg & ~(1<<15));
//	}
//
//	return 0;
//}

//static int rear_amp_event(struct snd_soc_dapm_widget *widget,
//			  struct snd_kcontrol *kctl, int event)
//{
//	struct snd_soc_codec *codec = widget->codec;
//
//	return rear_amp_power(codec, SND_SOC_DAPM_EVENT_ON(event));
//}

/* g900 machine dapm widgets */
static const struct snd_soc_dapm_widget g900_dapm_widgets[] = {
	SND_SOC_DAPM_SPK("Front Speaker", NULL),
//	SND_SOC_DAPM_SPK("Rear Speaker", rear_amp_event),
//	SND_SOC_DAPM_MIC("Headset", NULL),
//	SND_SOC_DAPM_LINE("GSM Line Out", NULL),
//	SND_SOC_DAPM_LINE("GSM Line In", NULL),
//	SND_SOC_DAPM_MIC("Headset Mic", NULL),
//	SND_SOC_DAPM_MIC("Front Mic", NULL),
};

static const struct snd_soc_dapm_route audio_map[] = {
	/* Call Mic */
//	{"Mic Bias", NULL, "Front Mic"},
//	{"MIC1", NULL, "Mic Bias"},

	/* Headset Mic */
//	{"LINEL", NULL, "Headset Mic"},
//	{"LINER", NULL, "Headset Mic"},

	/* GSM Module */
//	{"MONOIN", NULL, "GSM Line Out"},
//	{"PCBEEP", NULL, "GSM Line Out"},
//	{"GSM Line In", NULL, "MONO"},

	/* headphone connected to HPL, HPR */
//	{"Headset", NULL, "HPL"},
//	{"Headset", NULL, "HPR"},

	/* front speaker connected to HPL, OUT3 */
	{"Front Speaker", NULL, "HPL"},
	{"Front Speaker", NULL, "OUT3"},

	/* rear speaker connected to SPKL, SPKR */
//	{"Rear Speaker", NULL, "SPKL"},
//	{"Rear Speaker", NULL, "SPKR"},
};

static int g900_wm9714_init(struct snd_soc_codec *codec)
{
//	unsigned short reg;

	/* Add g900 specific widgets */
	snd_soc_dapm_new_controls(codec, ARRAY_AND_SIZE(g900_dapm_widgets));

	/* Set up g900 specific audio path audio_mapnects */
	snd_soc_dapm_add_routes(codec, ARRAY_AND_SIZE(audio_map));

	/* Prepare GPIO8 for rear speaker amplifier */
//	reg = codec->read(codec, AC97_GPIO_CFG);
//	codec->write(codec, AC97_GPIO_CFG, reg | 0x0100);

	/* Prepare MIC input */
//	reg = codec->read(codec, AC97_3D_CONTROL);
//	codec->write(codec, AC97_3D_CONTROL, reg | 0xc000);

	snd_soc_dapm_enable_pin(codec, "Front Speaker");
//	snd_soc_dapm_enable_pin(codec, "Rear Speaker");
//	snd_soc_dapm_enable_pin(codec, "Front Mic");
//	snd_soc_dapm_enable_pin(codec, "GSM Line In");
//	snd_soc_dapm_enable_pin(codec, "GSM Line Out");
	snd_soc_dapm_sync(codec);

	return 0;
}

static struct snd_soc_ops g900_ops;

static struct snd_soc_dai_link g900_dai[] = {
	{
		.name = "AC97",
		.stream_name = "AC97 HiFi",
		.cpu_dai = &pxa_ac97_dai[PXA2XX_DAI_AC97_HIFI],
		.codec_dai = &wm9713_dai[WM9713_DAI_AC97_HIFI],
		.init = g900_wm9714_init,
		.ops = &g900_ops,
	},
	{
		.name = "AC97 Aux",
		.stream_name = "AC97 Aux",
		.cpu_dai = &pxa_ac97_dai[PXA2XX_DAI_AC97_AUX],
		.codec_dai = &wm9713_dai[WM9713_DAI_AC97_AUX],
		.ops = &g900_ops,
	},
};

static struct snd_soc_card g900 = {
	.name = "PortegeG900",
	.platform = &pxa2xx_soc_platform,
	.dai_link = g900_dai,
	.num_links = ARRAY_SIZE(g900_dai),
};

static struct snd_soc_device g900_snd_devdata = {
	.card = &g900,
	.codec_dev = &soc_codec_dev_wm9713,
};

static struct platform_device *g900_snd_device;

static int g900_wm9714_probe(struct platform_device *pdev)
{
	int ret;

	if (!machine_is_g900())
		return -ENODEV;

	dev_warn(&pdev->dev, "Be warned that incorrect mixers/muxes setup will"
		 "lead to overheating and possible destruction of your device."
		 "Do not use without a good knowledge of G900's board design!\n");

	g900_snd_device = platform_device_alloc("soc-audio", -1);
	if (!g900_snd_device)
		return -ENOMEM;

	platform_set_drvdata(g900_snd_device, &g900_snd_devdata);
	g900_snd_devdata.dev = &g900_snd_device->dev;

	ret = platform_device_add(g900_snd_device);
	if (!ret)
		return 0;

	platform_device_put(g900_snd_device);
	return ret;
}

static int __devexit g900_wm9714_remove(struct platform_device *pdev)
{
	platform_device_unregister(g900_snd_device);
	return 0;
}

static struct platform_driver g900_wm9714_driver = {
	.probe		= g900_wm9714_probe,
	.remove		= __devexit_p(g900_wm9714_remove),
	.driver		= {
		.name		= "g900-wm9714",
		.owner		= THIS_MODULE,
	},
};

static int __init g900_asoc_init(void)
{
	return platform_driver_register(&g900_wm9714_driver);
}

static void __exit g900_asoc_exit(void)
{
	platform_driver_unregister(&g900_wm9714_driver);
}

module_init(g900_asoc_init);
module_exit(g900_asoc_exit);

/* Module information */
MODULE_AUTHOR("Serhij Stasyuk (stas@onlineua.net)");
MODULE_DESCRIPTION("ALSA SoC WM9714 Portege G900");
MODULE_LICENSE("GPL");
