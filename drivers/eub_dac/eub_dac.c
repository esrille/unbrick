/*
 * ASoC Driver for Esrille Unbrick Sound Board
 *
 * Copyright (C) 2018, 2019 Esrille Inc.
 *
 * Based on code written by Baswaraj K <jaikumar@cem-solutions.net>
 * Based on code written by Daniel Matuschek and
 *			    Stuart MacLean <stuart@hifiberry.com>
 * Based on code written by Florian Meier <florian.meier@koalo.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

/*
 * Note we should create a single unified PCM512x sound board driver
 * that supports various GPIO and CLK configurations around PCM512x. See
 *   https://github.com/raspberrypi/linux/pull/2048
 *
 * eub_dac uses GPIO4 and GPIO5 to control GAIN0 and GAIN1 of TPA6043A4
 * which is on the same board.  The clock configuration is same as
 * hifiberry_dacplus.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "pcm512x.h"

#define SPEAKER_GAIN	0x18

#define MASTER_NOCLOCK	0
#define MASTER_CLK44EN	1
#define MASTER_CLK48EN	2

struct pcm512x_priv {
	struct regmap *regmap;
	struct clk *sclk;
};

/* Clock rate of CLK44EN attached to GPIO6 pin */
#define CLK_44EN_RATE 22579200UL
/* Clock rate of CLK48EN attached to GPIO3 pin */
#define CLK_48EN_RATE 24576000UL

static bool slave;
static bool digital_gain_0db_limit = true;

static void snd_eub_dac_select_clk(struct snd_soc_component *component,
	int clk_id)
{
	switch (clk_id) {
	case MASTER_NOCLOCK:
		snd_soc_component_update_bits(component, PCM512x_GPIO_CONTROL_1, 0x24, 0x00);
		break;
	case MASTER_CLK44EN:
		snd_soc_component_update_bits(component, PCM512x_GPIO_CONTROL_1, 0x24, 0x20);
		break;
	case MASTER_CLK48EN:
		snd_soc_component_update_bits(component, PCM512x_GPIO_CONTROL_1, 0x24, 0x04);
		break;
	}
}

static int snd_eub_dac_clk_for_rate(int sample_rate)
{
	int type;

	switch (sample_rate) {
	case 11025:
	case 22050:
	case 44100:
	case 88200:
	case 176400:
	case 352800:
		type = MASTER_CLK44EN;
		break;
	default:
		type = MASTER_CLK48EN;
		break;
	}
	return type;
}

static void snd_eub_dac_set_sclk(struct snd_soc_component *component,
	int sample_rate)
{
	struct pcm512x_priv *pcm512x = snd_soc_component_get_drvdata(component);

	if (!IS_ERR(pcm512x->sclk)) {
		int ctype;

		ctype = snd_eub_dac_clk_for_rate(sample_rate);
		clk_set_rate(pcm512x->sclk, (ctype == MASTER_CLK44EN)
			? CLK_44EN_RATE : CLK_48EN_RATE);
		snd_eub_dac_select_clk(component, ctype);
	}
}

static int snd_eub_dac_gain_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_card *card = snd_kcontrol_chip(kcontrol);
	struct snd_soc_pcm_runtime *rtd =
		snd_soc_get_pcm_runtime(card, card->dai_link[0].name);
	struct snd_soc_component *component = rtd->codec_dai->component;
	unsigned int gain;
	snd_soc_component_read(component, PCM512x_GPIO_CONTROL_1, &gain);
	gain = (gain & 0x18) >> 3;
	ucontrol->value.integer.value[0] = gain;
	return 0;
}

static int snd_eub_dac_gain_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_card *card = snd_kcontrol_chip(kcontrol);
	struct snd_soc_pcm_runtime *rtd =
		snd_soc_get_pcm_runtime(card, card->dai_link[0].name);
	struct snd_soc_component *component = rtd->codec_dai->component;
	unsigned int gain = (ucontrol->value.integer.value[0] << 3) & 0x18;
	return snd_soc_component_update_bits(component, PCM512x_GPIO_CONTROL_1, 0x18, gain);
}

static const char * const eub_gain_texts[] = {
	"6dB",
	"10dB",
	"15.6dB",
	"21.6dB",
};

static const SOC_ENUM_SINGLE_DECL(eub_gain_enum,
		0, 0, eub_gain_texts);

/* Do not begin control names with Master, Front, PCM, LineOut, Digital, or
 * Headphone. It will confuse volumealsa LX panel plugin.
 */
static const struct snd_kcontrol_new eub_dac_controls[] = {
	SOC_ENUM_EXT("Amplifier Gain",
			eub_gain_enum,
			snd_eub_dac_gain_get,
			snd_eub_dac_gain_put),
};

static void snd_eub_dac_clk_gpio(struct snd_soc_component *component)
{
	snd_soc_component_update_bits(component, PCM512x_GPIO_EN, 0x24, 0x24);
	snd_soc_component_update_bits(component, PCM512x_GPIO_OUTPUT_3, 0x0f, 0x02);
	snd_soc_component_update_bits(component, PCM512x_GPIO_OUTPUT_6, 0x0f, 0x02);
}

static int snd_eub_dac_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_component *component = rtd->codec_dai->component;
	struct pcm512x_priv *priv = snd_soc_component_get_drvdata(component);

	if (!slave) {
		struct snd_soc_dai_link *dai = rtd->dai_link;

		dai->name = "Esrille Unbrick DAC (master)";
		dai->stream_name = "Esrille Unbrick DAC (master)";
		dai->dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
			| SND_SOC_DAIFMT_CBM_CFM;

		snd_eub_dac_clk_gpio(component);
		snd_soc_component_update_bits(component, PCM512x_BCLK_LRCLK_CFG, 0x31, 0x11);
		snd_soc_component_update_bits(component, PCM512x_MASTER_MODE, 0x03, 0x03);
		snd_soc_component_update_bits(component, PCM512x_MASTER_CLKDIV_2, 0x7f, 63);
		if (!IS_ERR(priv->sclk))
			clk_set_rate(priv->sclk, CLK_48EN_RATE);
	} else {
		priv->sclk = ERR_PTR(-ENOENT);
	}

	/*
	 * unbrick DAC controls GAIN0 and GAIN1 of TPA6043A4 via GPIO4 and
	 * GPIO5.
	 */
	snd_soc_component_update_bits(component, PCM512x_GPIO_EN, 0x18, 0x18);
	/* Set the default speaker gain */
	snd_soc_component_update_bits(component, PCM512x_GPIO_CONTROL_1, 0x18, SPEAKER_GAIN);
	/* Set Register GPIO4 output */
	snd_soc_component_update_bits(component, PCM512x_GPIO_OUTPUT_4, 0x0f, 0x02);
	/* Set Register GPIO5 output */
	snd_soc_component_update_bits(component, PCM512x_GPIO_OUTPUT_5, 0x0f, 0x02);

	if (digital_gain_0db_limit) {
		int ret;
		struct snd_soc_card *card = rtd->card;

		ret = snd_soc_limit_volume(card, "Digital Playback Volume",
					   207);
		if (ret < 0)
			dev_warn(card->dev, "Failed to set volume limit: %d\n",
				 ret);
	}

	return 0;
}

static int snd_eub_dac_update_rate_den(
	struct snd_pcm_substream *substream, struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_component *component = rtd->codec_dai->component;
	struct pcm512x_priv *pcm512x = snd_soc_component_get_drvdata(component);
	struct snd_ratnum *rats_no_pll;
	unsigned int num = 0, den = 0;
	int err;

	rats_no_pll = devm_kzalloc(rtd->dev, sizeof(*rats_no_pll), GFP_KERNEL);
	if (!rats_no_pll)
		return -ENOMEM;

	rats_no_pll->num = clk_get_rate(pcm512x->sclk) / 64;
	rats_no_pll->den_min = 1;
	rats_no_pll->den_max = 128;
	rats_no_pll->den_step = 1;

	err = snd_interval_ratnum(hw_param_interval(params,
		SNDRV_PCM_HW_PARAM_RATE), 1, rats_no_pll, &num, &den);
	if (err >= 0 && den) {
		params->rate_num = num;
		params->rate_den = den;
	}

	devm_kfree(rtd->dev, rats_no_pll);
	return 0;
}

static int snd_eub_dac_hw_params(
	struct snd_pcm_substream *substream, struct snd_pcm_hw_params *params)
{
	int ret = 0;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	int channels = params_channels(params);
	int width = 32;

	if (!slave) {
		struct snd_soc_component *component = rtd->codec_dai->component;

		width = snd_pcm_format_physical_width(params_format(params));

		snd_eub_dac_set_sclk(component,
			params_rate(params));

		ret = snd_eub_dac_update_rate_den(
			substream, params);
	}

	ret = snd_soc_dai_set_tdm_slot(rtd->cpu_dai, 0x03, 0x03,
		channels, width);
	if (ret)
		return ret;
	ret = snd_soc_dai_set_tdm_slot(rtd->codec_dai, 0x03, 0x03,
		channels, width);
	return ret;
}

/* machine stream operations */
static struct snd_soc_ops snd_eub_dac_ops = {
	.hw_params = snd_eub_dac_hw_params,
};

static struct snd_soc_dai_link snd_eub_dac_dai[] = {
{
	.name		= "Esrille Unbrick DAC (slave)",
	.stream_name	= "Esrille Unbrick DAC (slave)",
	.cpu_dai_name	= "bcm2708-i2s.0",
	.codec_dai_name	= "pcm512x-hifi",
	.platform_name	= "bcm2708-i2s.0",
	.codec_name	= "pcm512x.3-004d",
	.dai_fmt	= SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
				SND_SOC_DAIFMT_CBS_CFS,
	.ops		= &snd_eub_dac_ops,
	.init		= snd_eub_dac_init,
},
};

/* audio machine driver */
static struct snd_soc_card snd_eub_dac = {
	.name         = "snd_eub_dac",
	.owner        = THIS_MODULE,
	.dai_link     = snd_eub_dac_dai,
	.num_links    = ARRAY_SIZE(snd_eub_dac_dai),
	.controls     = eub_dac_controls,
	.num_controls = ARRAY_SIZE(eub_dac_controls),
};

static int snd_eub_dac_probe(struct platform_device *pdev)
{
	int ret = 0;

	snd_eub_dac.dev = &pdev->dev;
	if (pdev->dev.of_node) {
		struct device_node *i2s_node;
		struct snd_soc_dai_link *dai;

		dai = &snd_eub_dac_dai[0];
		i2s_node = of_parse_phandle(pdev->dev.of_node,
			"i2s-controller", 0);

		if (i2s_node) {
			dai->cpu_dai_name = NULL;
			dai->cpu_of_node = i2s_node;
			dai->platform_name = NULL;
			dai->platform_of_node = i2s_node;
		}

		digital_gain_0db_limit = !of_property_read_bool(
			pdev->dev.of_node, "esrille,24db_digital_gain");
		slave = of_property_read_bool(pdev->dev.of_node,
					      "esrille,slave");
	}

	ret = devm_snd_soc_register_card(&pdev->dev, &snd_eub_dac);
	if (ret && ret != -EPROBE_DEFER)
		dev_err(&pdev->dev,
			"snd_soc_register_card() failed: %d\n", ret);

	return ret;
}

static const struct of_device_id snd_eub_dac_of_match[] = {
	{ .compatible = "esrille,eub_dac", },
	{},
};
MODULE_DEVICE_TABLE(of, snd_eub_dac_of_match);

static struct platform_driver snd_eub_dac_driver = {
	.driver = {
		.name   = "snd-rpi-eub_dac",
		.owner  = THIS_MODULE,
		.of_match_table = snd_eub_dac_of_match,
	},
	.probe          = snd_eub_dac_probe,
};

module_platform_driver(snd_eub_dac_driver);

MODULE_AUTHOR("Esrille Inc. <info@esrille.com>");
MODULE_DESCRIPTION("ASoC Driver for Esrille Unbrick Sound Board");
MODULE_LICENSE("GPL v2");
MODULE_SOFTDEP("pre: eub_mobo");
