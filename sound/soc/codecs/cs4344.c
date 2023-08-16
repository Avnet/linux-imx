/*
 * CS4344 ALSA SoC (ASoC) codec driver
 *
 * Copyright 2019-2020 Freescale Semiconductor, Inc.  This file is licensed
 * under the terms of the GNU General Public License version 2.  This
 * program is licensed "as is" without any warranty of any kind, whether
 * express or implied.
 *
 * This is an ASoC device driver for the Cirrus Logic CS4344 codec.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/gpio/consumer.h>
#include <linux/of_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>


/* codec private data */
struct cs4344_priv {
	unsigned int sysclk;
	const struct snd_pcm_hw_constraint_list *sysclk_constraints;
};

static const struct snd_soc_dapm_widget cs4344_dapm_widgets[] = {
SND_SOC_DAPM_DAC("DAC", "Playback", SND_SOC_NOPM, 0, 0),
SND_SOC_DAPM_OUTPUT("LINEVOUTL"),
SND_SOC_DAPM_OUTPUT("LINEVOUTR"),
};

static const struct snd_soc_dapm_route cs4344_dapm_routes[] = {
	{ "LINEVOUTL", NULL, "DAC" },
	{ "LINEVOUTR", NULL, "DAC" },
};

static const unsigned int rates_8_1920[] = {
	32000, 64000, 128000,
};
static const unsigned int rates_11_2896[] = {
	44100, 88200, 176400,
};
static const unsigned int rates_12_2880[] = {
	32000, 48000, 64000, 96000, 128000, 192000,
};
static const unsigned int rates_18_4320[] = {
	48000, 96000, 192000,
};
static const unsigned int rates_36_8640[] = {
	32000, 48000, 96000, 192000,
};
static const unsigned int rates_45_1580[] = {
	44100,
};
static const unsigned int rates_49_1520[] = {
	48000, 64000, 128000,
};

static const struct snd_pcm_hw_constraint_list constraints_8192 = {
	.count	= ARRAY_SIZE(rates_8_1920),
	.list	= rates_8_1920,
};
static const struct snd_pcm_hw_constraint_list constraints_11289 = {
	.count	= ARRAY_SIZE(rates_11_2896),
	.list	= rates_11_2896,
};
static const struct snd_pcm_hw_constraint_list constraints_12288 = {
	.count	= ARRAY_SIZE(rates_12_2880),
	.list	= rates_12_2880,
};
static const struct snd_pcm_hw_constraint_list constraints_18432 = {
	.count	= ARRAY_SIZE(rates_18_4320),
	.list	= rates_18_4320,
};
static const struct snd_pcm_hw_constraint_list constraints_36864 = {
	.count	= ARRAY_SIZE(rates_36_8640),
	.list	= rates_36_8640,
};
static const struct snd_pcm_hw_constraint_list constraints_45158 = {
	.count	= ARRAY_SIZE(rates_45_1580),
	.list	= rates_45_1580,
};
static const struct snd_pcm_hw_constraint_list constraints_49152 = {
	.count	= ARRAY_SIZE(rates_49_1520),
	.list	= rates_49_1520,
};

static int cs4344_startup(struct snd_pcm_substream *substream,
			  struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct cs4344_priv *cs4344 = snd_soc_component_get_drvdata(component);
	struct snd_soc_pcm_runtime *rtd = substream->private_data;

	/* The set of sample rates that can be supported depends on the
	 * MCLK supplied to the CODEC - enforce this.
	 */
	if (!cs4344->sysclk) {
		dev_err(component->dev,
			"No MCLK configured, call set_sysclk() on init\n");
		return -EINVAL;
	}

	if (!rtd->dai_link->be_hw_params_fixup)
		snd_pcm_hw_constraint_list(substream->runtime, 0,
					   SNDRV_PCM_HW_PARAM_RATE,
					   cs4344->sysclk_constraints);

	return 0;
}

static int cs4344_set_dai_sysclk(struct snd_soc_dai *codec_dai,
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_component *component = codec_dai->component;
	struct cs4344_priv *cs4344 = snd_soc_component_get_drvdata(component);

	dev_dbg(component->dev, "cs4344->sysclk: %dHz\n", freq);

	switch (freq) {
	case 0:
		cs4344->sysclk_constraints = NULL;
		break;
	case 8192000:
	case 32768000:
		cs4344->sysclk_constraints = &constraints_8192;
		break;
	case 11289600:
	case 16934400:
	case 22579200:
	case 33868000:
		cs4344->sysclk_constraints = &constraints_11289;
		break;
	case 12288000:
		cs4344->sysclk_constraints = &constraints_12288;
		break;
	case 18432000:
	case 24576000:
		cs4344->sysclk_constraints = &constraints_18432;
		break;
	case 36864000:
		cs4344->sysclk_constraints = &constraints_36864;
		break;
	case 45158000:
		cs4344->sysclk_constraints = &constraints_45158;
		break;
	case 49152000:
		cs4344->sysclk_constraints = &constraints_49152;
		break;
	default:
		dev_err(component->dev, "sample rate not supported!\n");
		return -EINVAL;
	}

	cs4344->sysclk = freq;

	return 0;
}

static int cs4344_set_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
	fmt &= (SND_SOC_DAIFMT_FORMAT_MASK | SND_SOC_DAIFMT_INV_MASK |
		SND_SOC_DAIFMT_MASTER_MASK);

	if (fmt != (SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
		    SND_SOC_DAIFMT_CBS_CFS)) {
		dev_err(codec_dai->dev, "Invalid DAI format\n");
		return -EINVAL;
	}

	return 0;
}

#define CS4344_RATES (SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100|\
						SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_64000|\
						SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000|\
						SNDRV_PCM_RATE_176400 | SNDRV_PCM_RATE_192000)

#define CS4344_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S20_3LE)

static const struct snd_soc_dai_ops cs4344_dai_ops = {
	.startup	= cs4344_startup,
	.set_sysclk	= cs4344_set_dai_sysclk,
	.set_fmt	= cs4344_set_fmt,
};

static struct snd_soc_dai_driver cs4344_dai = {
	.name = "cs4344-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = CS4344_RATES,
		.formats = CS4344_FORMATS,
	},
	.ops = &cs4344_dai_ops,
};

static const struct snd_soc_component_driver soc_component_dev_cs4344 = {
	.dapm_widgets		= cs4344_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(cs4344_dapm_widgets),
	.dapm_routes		= cs4344_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(cs4344_dapm_routes),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

static int cs4344_codec_probe(struct platform_device *pdev)
{
	struct cs4344_priv *cs4344;
	int ret;

	cs4344 = devm_kzalloc(&pdev->dev, sizeof(struct cs4344_priv),
						  GFP_KERNEL);
	if (cs4344 == NULL)
		return -ENOMEM;

	platform_set_drvdata(pdev, cs4344);

	ret = devm_snd_soc_register_component(&pdev->dev,
			&soc_component_dev_cs4344, &cs4344_dai, 1);
	if (ret < 0)
		dev_err(&pdev->dev, "Failed to register component: %d\n", ret);

	return ret;
}

static const struct of_device_id cs4344_of_match[] = {
	{ .compatible = "cirrus,cs4344" },
	{ }
};
MODULE_DEVICE_TABLE(of, cs4344_of_match);

static struct platform_driver cs4344_codec_driver = {
	.probe		= cs4344_codec_probe,
	.driver		= {
		.name	= "cs4344-codec",
		.of_match_table = cs4344_of_match,
	},
};
module_platform_driver(cs4344_codec_driver);

MODULE_DESCRIPTION("ASoC CS4344 driver");
MODULE_AUTHOR("Nick <nick.chain@avnet.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:cs4344-codec");
