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

#define CS4344_NUM_RATES 10

/* codec private data */
struct cs4344_priv {
	unsigned int sysclk;
	unsigned int rate_constraint_list[CS4344_NUM_RATES];
	struct snd_pcm_hw_constraint_list rate_constraint;
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

static const struct {
	int value;
	int ratio;
} lrclk_ratios[CS4344_NUM_RATES] = {
	{ 1, 64 },
	{ 2, 96 },
	{ 3, 128 },
	{ 4, 192 },
	{ 5, 256 },
	{ 6, 384 },
	{ 7, 512 },
	{ 8, 768 },
	{ 9, 1024 },
	{ 10, 1152 },
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
					   &cs4344->rate_constraint);

	return 0;
}

static int cs4344_set_dai_sysclk(struct snd_soc_dai *codec_dai,
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_component *component = codec_dai->component;
	struct cs4344_priv *cs4344 = snd_soc_component_get_drvdata(component);
	unsigned int val;
	int i, j = 0;

	cs4344->sysclk = freq;

	cs4344->rate_constraint.count = 0;
	for (i = 0; i < ARRAY_SIZE(lrclk_ratios); i++) {
		val = freq / lrclk_ratios[i].ratio;
		/* Check that it's a standard rate since core can't
		 * cope with others and having the odd rates confuses
		 * constraint matching.
		 */
		switch (val) {
		case 32000:
		case 44100:
		case 48000:
		case 64000:
		case 88200:
		case 96000:
		case 128000:
		case 176400:
		case 192000:
			dev_dbg(component->dev, "Supported sample rate: %dHz\n",
				val);
			cs4344->rate_constraint_list[j++] = val;
			cs4344->rate_constraint.count++;
			break;
		default:
			dev_dbg(component->dev, "Skipping sample rate: %dHz\n",
				val);
		}
	}

	/* Need at least one supported rate... */
	if (cs4344->rate_constraint.count == 0)
		return -EINVAL;

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

#define CS4344_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE |\
			SNDRV_PCM_FMTBIT_S32_LE)

static const struct snd_soc_dai_ops cs4344_dai_ops = {
	.startup	= cs4344_startup,
	.set_sysclk	= cs4344_set_dai_sysclk,
	.set_fmt	= cs4344_set_fmt,
};

static struct snd_soc_dai_driver cs4344_dai = {
	.name = "cs4344-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = CS4344_RATES,
		.formats = CS4344_FORMATS,
	},
	.ops = &cs4344_dai_ops,
};

static int cs4344_probe(struct snd_soc_component *component)
{
	struct cs4344_priv *cs4344 = snd_soc_component_get_drvdata(component);

	cs4344->rate_constraint.list = &cs4344->rate_constraint_list[0];
	cs4344->rate_constraint.count =
		ARRAY_SIZE(cs4344->rate_constraint_list);

	return 0;
}

static const struct snd_soc_component_driver soc_component_dev_cs4344 = {
	.probe			= cs4344_probe,
	.dapm_widgets		= cs4344_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(cs4344_dapm_widgets),
	.dapm_routes		= cs4344_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(cs4344_dapm_routes),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static const struct of_device_id cs4344_of_match[] = {
	{ .compatible = "cirrus,cs4344" },
	{ /* sentinel*/ }
};
MODULE_DEVICE_TABLE(of, cs4344_of_match);

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

static struct platform_driver cs4344_codec_driver = {
	.probe		= cs4344_codec_probe,
	.driver		= {
		.name	= "cs4344-codec",
		.of_match_table = cs4344_of_match,
	},
};
module_platform_driver(cs4344_codec_driver);

MODULE_DESCRIPTION("ASoC CS4344 driver");
MODULE_AUTHOR("Nick <nick.ttf@avnet.com>");
MODULE_ALIAS("platform:cs4344-codec");
MODULE_LICENSE("GPL");
