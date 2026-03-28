/* Copyright (c) 2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "msm-audio-pinctrl.h"

struct cdc_pinctrl_info {
	struct pinctrl *pinctrl;
	struct pinctrl_state **cdc_lines;
	int active_set;
};

struct cdc_gpioset_info {
	char **gpiosets;
	char **gpiosets_comb_names;
	u8 *gpioset_state;
	int gpiosets_max;
	int gpiosets_comb_max;
};

static struct cdc_pinctrl_info pinctrl_info[MAX_PINCTRL_CLIENT];
static struct cdc_gpioset_info gpioset_info[MAX_PINCTRL_CLIENT];

int msm_get_gpioset_index(enum pinctrl_client client, char *keyword)
{
	int index;

	for (index = 0; index < gpioset_info[client].gpiosets_max; index++) {
		if (!strcmp(gpioset_info[client].gpiosets[index], keyword))
			break;
	}

	if (index != gpioset_info[client].gpiosets_max)
		return index;

	return -EINVAL;
}

int msm_gpioset_initialize(enum pinctrl_client client, struct device *dev)
{
	struct pinctrl *pinctrl;
	const char *gpioset_names = "qcom,msm-gpios";
	const char *gpioset_combinations = "qcom,pinctrl-names";
	const char *gpioset_names_str = NULL;
	const char *gpioset_comb_str = NULL;
	int num_strings;
	int ret = 0;
	int index;
	int gpioset_str;

	pr_debug("%s\n", __func__);
	pinctrl = devm_pinctrl_get(dev);
	if (IS_ERR(pinctrl)) {
		pr_err("%s: Unable to get pinctrl handle\n", __func__);
		return -EINVAL;
	}
	pinctrl_info[client].pinctrl = pinctrl;

	num_strings = of_property_count_strings(dev->of_node, gpioset_names);
	if (num_strings < 0) {
		dev_err(dev,
			"%s: missing %s in dt node or length is incorrect\n",
			__func__, gpioset_names);
		goto err;
	}

	gpioset_info[client].gpiosets_max = num_strings;
	gpioset_info[client].gpiosets = devm_kzalloc(dev,
		gpioset_info[client].gpiosets_max * sizeof(char *), GFP_KERNEL);
	if (!gpioset_info[client].gpiosets) {
		dev_err(dev, "Can't allocate memory for gpio set names\n");
		ret = -ENOMEM;
		goto err;
	}

	for (index = 0; index < num_strings; index++) {
		ret = of_property_read_string_index(dev->of_node, gpioset_names,
			index, &gpioset_names_str);
		if (ret)
			goto err;

		gpioset_str = strlen(gpioset_names_str) + 1;
		gpioset_info[client].gpiosets[index] = devm_kzalloc(dev,
			gpioset_str, GFP_KERNEL);
		if (!gpioset_info[client].gpiosets[index]) {
			dev_err(dev, "%s: Can't allocate gpiosets[%d] data\n",
				__func__, index);
			ret = -ENOMEM;
			goto err;
		}

		strlcpy(gpioset_info[client].gpiosets[index], gpioset_names_str,
			gpioset_str);
	}

	gpioset_info[client].gpioset_state = devm_kzalloc(dev,
		gpioset_info[client].gpiosets_max * sizeof(u8), GFP_KERNEL);
	if (!gpioset_info[client].gpioset_state) {
		dev_err(dev, "Can't allocate memory for gpio set counter\n");
		ret = -ENOMEM;
		goto err;
	}

	num_strings = of_property_count_strings(dev->of_node,
		gpioset_combinations);
	if (num_strings < 0) {
		dev_err(dev,
			"%s: missing %s in dt node or length is incorrect\n",
			__func__, gpioset_combinations);
		goto err;
	}

	gpioset_info[client].gpiosets_comb_max = num_strings;
	gpioset_info[client].gpiosets_comb_names = devm_kzalloc(dev,
		num_strings * sizeof(char *), GFP_KERNEL);
	if (!gpioset_info[client].gpiosets_comb_names) {
		ret = -ENOMEM;
		goto err;
	}

	for (index = 0; index < gpioset_info[client].gpiosets_comb_max; index++) {
		ret = of_property_read_string_index(dev->of_node,
			gpioset_combinations, index, &gpioset_comb_str);
		if (ret)
			goto err;

		gpioset_str = strlen(gpioset_comb_str) + 1;
		gpioset_info[client].gpiosets_comb_names[index] = devm_kzalloc(dev,
			gpioset_str, GFP_KERNEL);
		if (!gpioset_info[client].gpiosets_comb_names[index]) {
			ret = -ENOMEM;
			goto err;
		}

		strlcpy(gpioset_info[client].gpiosets_comb_names[index],
			gpioset_comb_str, gpioset_str);
		pr_debug("%s: GPIO configuration %s\n", __func__,
			gpioset_info[client].gpiosets_comb_names[index]);
	}

	pinctrl_info[client].cdc_lines = devm_kzalloc(dev,
		num_strings * sizeof(struct pinctrl_state *), GFP_KERNEL);
	if (!pinctrl_info[client].cdc_lines) {
		ret = -ENOMEM;
		goto err;
	}

	for (index = 0; index < num_strings; index++) {
		pinctrl_info[client].cdc_lines[index] = pinctrl_lookup_state(
			pinctrl,
			(const char *)gpioset_info[client].gpiosets_comb_names[index]);
		if (IS_ERR(pinctrl_info[client].cdc_lines[index]))
			pr_err("%s: Unable to get pinctrl handle for %s\n", __func__,
				gpioset_info[client].gpiosets_comb_names[index]);
	}

	return ret;

err:
	for (index = 0; index < gpioset_info[client].gpiosets_max; index++) {
		if (gpioset_info[client].gpiosets &&
		    gpioset_info[client].gpiosets[index])
			devm_kfree(dev, gpioset_info[client].gpiosets[index]);
	}
	if (gpioset_info[client].gpiosets)
		devm_kfree(dev, gpioset_info[client].gpiosets);

	for (index = 0; index < gpioset_info[client].gpiosets_comb_max; index++) {
		if (gpioset_info[client].gpiosets_comb_names &&
		    gpioset_info[client].gpiosets_comb_names[index])
			devm_kfree(dev,
				gpioset_info[client].gpiosets_comb_names[index]);
	}
	if (gpioset_info[client].gpiosets_comb_names)
		devm_kfree(dev, gpioset_info[client].gpiosets_comb_names);

	if (pinctrl_info[client].cdc_lines)
		devm_kfree(dev, pinctrl_info[client].cdc_lines);

	if (gpioset_info[client].gpioset_state)
		devm_kfree(dev, gpioset_info[client].gpioset_state);

	return ret;
}

int msm_gpioset_activate(enum pinctrl_client client, char *keyword)
{
	int ret = 0;
	int gp_set;
	int active_set;

	gp_set = msm_get_gpioset_index(client, keyword);
	if (gp_set < 0) {
		pr_err("%s: gpio set name does not exist\n", __func__);
		return gp_set;
	}

	if (!gpioset_info[client].gpioset_state[gp_set]) {
		active_set = pinctrl_info[client].active_set;
		if (IS_ERR(pinctrl_info[client].cdc_lines[active_set]))
			return 0;

		pinctrl_info[client].active_set |= (1 << gp_set);
		active_set = pinctrl_info[client].active_set;
		pr_debug("%s: pinctrl.active_set: %d\n", __func__, active_set);

		ret = pinctrl_select_state(pinctrl_info[client].pinctrl,
			pinctrl_info[client].cdc_lines[active_set]);
	}

	gpioset_info[client].gpioset_state[gp_set]++;
	return ret;
}

int msm_gpioset_suspend(enum pinctrl_client client, char *keyword)
{
	int ret = 0;
	int gp_set;
	int active_set;

	gp_set = msm_get_gpioset_index(client, keyword);
	if (gp_set < 0) {
		pr_err("%s: gpio set name does not exist\n", __func__);
		return gp_set;
	}

	if (gpioset_info[client].gpioset_state[gp_set] == 1) {
		pinctrl_info[client].active_set &= ~(1 << gp_set);
		active_set = pinctrl_info[client].active_set;
		if (IS_ERR(pinctrl_info[client].cdc_lines[active_set]))
			return -EINVAL;

		pr_debug("%s: pinctrl.active_set: %d\n", __func__,
			pinctrl_info[client].active_set);
		ret = pinctrl_select_state(pinctrl_info[client].pinctrl,
			pinctrl_info[client].cdc_lines[pinctrl_info[client].active_set]);
	}

	if (!gpioset_info[client].gpioset_state[gp_set]) {
		pr_err("%s: Invalid call to de activate gpios: %d\n", __func__,
			gpioset_info[client].gpioset_state[gp_set]);
		return -EINVAL;
	}

	gpioset_info[client].gpioset_state[gp_set]--;
	return ret;
}