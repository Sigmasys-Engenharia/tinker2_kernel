/*
 * ASoC Driver for IQaudIO DAC
 *
 * Author:	Florian Meier <florian.meier@koalo.de>
 *		Copyright 2013
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <linux/module.h>
#include <linux/gpio/consumer.h>
#include <linux/platform_device.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/jack.h>

#ifdef ROCKCHIP_AUDIO
#define ROCKCHIP_I2S_MCLK 512
#endif

static bool digital_gain_0db_limit = true;

static struct gpio_desc *mute_gpio;

static int snd_rpi_iqaudio_dac_init(struct snd_soc_pcm_runtime *rtd)
{
	if (digital_gain_0db_limit)
	{
		int ret;
		struct snd_soc_card *card = rtd->card;

		ret = snd_soc_limit_volume(card, "Digital Playback Volume", 207);
		if (ret < 0)
			dev_warn(card->dev, "Failed to set volume limit: %d\n", ret);
	}

	return 0;
}

static int snd_rpi_iqaudio_dac_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
#ifdef ROCKCHIP_AUDIO
	unsigned int mclk;

	mclk = params_rate(params) * ROCKCHIP_I2S_MCLK;
	return snd_soc_dai_set_sysclk(cpu_dai, 0, mclk,
					SND_SOC_CLOCK_OUT);
#else
	unsigned int sample_bits =
		snd_pcm_format_physical_width(params_format(params));

	return snd_soc_dai_set_bclk_ratio(cpu_dai, sample_bits * 2);
#endif
}

static void snd_rpi_iqaudio_gpio_mute(struct snd_soc_card *card)
{
	if (mute_gpio) {
		dev_info(card->dev, "%s: muting amp using GPIO22\n",
			 __func__);
		gpiod_set_value_cansleep(mute_gpio, 0);
	}
}

static void snd_rpi_iqaudio_gpio_unmute(struct snd_soc_card *card)
{
	if (mute_gpio) {
		dev_info(card->dev, "%s: un-muting amp using GPIO22\n",
			 __func__);
		gpiod_set_value_cansleep(mute_gpio, 1);
	}
}

static int snd_rpi_iqaudio_set_bias_level(struct snd_soc_card *card,
	struct snd_soc_dapm_context *dapm, enum snd_soc_bias_level level)
{
	struct snd_soc_pcm_runtime *rtd;
	struct snd_soc_dai *codec_dai;

	rtd = snd_soc_get_pcm_runtime(card, card->dai_link[0].name);
	codec_dai = rtd->codec_dai;

	if (dapm->dev != codec_dai->dev)
		return 0;

	switch (level) {
	case SND_SOC_BIAS_PREPARE:
		if (dapm->bias_level != SND_SOC_BIAS_STANDBY)
			break;

		/* UNMUTE AMP */
		snd_rpi_iqaudio_gpio_unmute(card);

		break;
	case SND_SOC_BIAS_STANDBY:
		if (dapm->bias_level != SND_SOC_BIAS_PREPARE)
			break;

		/* MUTE AMP */
		snd_rpi_iqaudio_gpio_mute(card);

		break;
	default:
		break;
	}

	return 0;
}

/* machine stream operations */
static struct snd_soc_ops snd_rpi_iqaudio_dac_ops = {
	.hw_params = snd_rpi_iqaudio_dac_hw_params,
};

static struct snd_soc_dai_link snd_rpi_iqaudio_dac_dai[] = {
{
	.cpu_dai_name	= "bcm2708-i2s.0",
	.codec_dai_name	= "pcm512x-hifi",
	.platform_name	= "bcm2708-i2s.0",
	.codec_name	= "pcm512x.6-004c",
	.dai_fmt	= SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
				SND_SOC_DAIFMT_CBS_CFS,
	.ops		= &snd_rpi_iqaudio_dac_ops,
	.init		= snd_rpi_iqaudio_dac_init,
},
};

/* audio machine driver */
static struct snd_soc_card snd_rpi_iqaudio_dac = {
	.owner        = THIS_MODULE,
	.dai_link     = snd_rpi_iqaudio_dac_dai,
	.num_links    = ARRAY_SIZE(snd_rpi_iqaudio_dac_dai),
};

static int snd_rpi_iqaudio_dac_probe(struct platform_device *pdev)
{
	int ret = 0;
	bool gpio_unmute = false;

	snd_rpi_iqaudio_dac.dev = &pdev->dev;

	if (pdev->dev.of_node) {
		struct device_node *i2s_node;
		struct snd_soc_card *card = &snd_rpi_iqaudio_dac;
		struct snd_soc_dai_link *dai = &snd_rpi_iqaudio_dac_dai[0];
		bool auto_gpio_mute = false;

		i2s_node = of_parse_phandle(pdev->dev.of_node,
					    "i2s-controller", 0);
		if (i2s_node) {
			dai->cpu_dai_name = NULL;
			dai->cpu_of_node = i2s_node;
			dai->platform_name = NULL;
			dai->platform_of_node = i2s_node;
		}

		digital_gain_0db_limit = !of_property_read_bool(
			pdev->dev.of_node, "iqaudio,24db_digital_gain");

		if (of_property_read_string(pdev->dev.of_node, "card_name",
					    &card->name))
			card->name = "IQaudIODAC";

		if (of_property_read_string(pdev->dev.of_node, "dai_name",
					    &dai->name))
			dai->name = "IQaudIO DAC";

		if (of_property_read_string(pdev->dev.of_node,
					"dai_stream_name", &dai->stream_name))
			dai->stream_name = "IQaudIO DAC HiFi";

		/* gpio_unmute - one time unmute amp using GPIO */
		gpio_unmute = of_property_read_bool(pdev->dev.of_node,
						    "iqaudio-dac,unmute-amp");

		/* auto_gpio_mute - mute/unmute amp using GPIO */
		auto_gpio_mute = of_property_read_bool(pdev->dev.of_node,
						"iqaudio-dac,auto-mute-amp");

		if (auto_gpio_mute || gpio_unmute) {
			mute_gpio = devm_gpiod_get_optional(&pdev->dev, "mute",
							    GPIOD_OUT_LOW);
			if (IS_ERR(mute_gpio)) {
				ret = PTR_ERR(mute_gpio);
				dev_err(&pdev->dev,
					"Failed to get mute gpio: %d\n", ret);
				return ret;
			}

			if (auto_gpio_mute && mute_gpio)
				snd_rpi_iqaudio_dac.set_bias_level =
						snd_rpi_iqaudio_set_bias_level;
		}
	}

	ret = snd_soc_register_card(&snd_rpi_iqaudio_dac);
	if (ret) {
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev,
				"snd_soc_register_card() failed: %d\n", ret);
		return ret;
	}

	if (gpio_unmute && mute_gpio)
		snd_rpi_iqaudio_gpio_unmute(&snd_rpi_iqaudio_dac);

	return 0;
}

static int snd_rpi_iqaudio_dac_remove(struct platform_device *pdev)
{
	snd_rpi_iqaudio_gpio_mute(&snd_rpi_iqaudio_dac);

	return snd_soc_unregister_card(&snd_rpi_iqaudio_dac);
}

static const struct of_device_id iqaudio_of_match[] = {
	{ .compatible = "iqaudio,iqaudio-dac", },
	{},
};
MODULE_DEVICE_TABLE(of, iqaudio_of_match);

static struct platform_driver snd_rpi_iqaudio_dac_driver = {
	.driver = {
		.name   = "snd-rpi-iqaudio-dac",
		.owner  = THIS_MODULE,
		.of_match_table = iqaudio_of_match,
	},
	.probe          = snd_rpi_iqaudio_dac_probe,
	.remove         = snd_rpi_iqaudio_dac_remove,
};

module_platform_driver(snd_rpi_iqaudio_dac_driver);

MODULE_AUTHOR("Florian Meier <florian.meier@koalo.de>");
MODULE_DESCRIPTION("ASoC Driver for IQAudio DAC");
MODULE_LICENSE("GPL v2");
