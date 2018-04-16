/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2010 Uwe Hermann <uwe@hermann-uwe.de>
 * Copyright (C) 2011 Olivier Fauchon <olivier@aixmarseille.com>
 * Copyright (C) 2012 Alexandru Gagniuc <mr.nuke.me@gmail.com>
 * Copyright (C) 2015 Bartosz Golaszewski <bgolaszewski@baylibre.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "protocol.h"

#define DEFAULT_NUM_LOGIC_CHANNELS	12
#define DEFAULT_LOGIC_PATTERN		PATTERN_INC

/* Note: No spaces allowed because of sigrok-cli. */
static const char *logic_pattern_str[] = {
	"sigrok",
	"random",
	"incremental",
	"walking-one",
	"walking-zero",
	"all-low",
	"all-high",
	"squid",
};

static const uint32_t scanopts[] = {
	SR_CONF_NUM_LOGIC_CHANNELS,
};

static const uint32_t drvopts[] = {
	SR_CONF_DEMO_DEV,
	SR_CONF_LOGIC_ANALYZER,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_MATCH | SR_CONF_LIST,
	SR_CONF_CAPTURE_RATIO | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_HOLDOFF | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_REPEAT_TRIGGER | SR_CONF_GET | SR_CONF_SET,
};

static const uint32_t devopts_cg_logic[] = {
	SR_CONF_PATTERN_MODE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static const int32_t trigger_matches[] = {
       SR_TRIGGER_ZERO,
       SR_TRIGGER_ONE,
       SR_TRIGGER_RISING,
       SR_TRIGGER_FALLING,
       SR_TRIGGER_EDGE,
};

static const uint64_t samplerates[] = {
	SR_HZ(1),
	SR_GHZ(1),
	SR_HZ(1),
};

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct dev_context *devc;
	struct sr_dev_inst *sdi;
	struct sr_channel *ch;
	struct sr_channel_group *cg;
	struct sr_config *src;
	GSList *l;
	int num_logic_channels, i;
	char channel_name[16];

	num_logic_channels = DEFAULT_NUM_LOGIC_CHANNELS;
	for (l = options; l; l = l->next) {
		src = l->data;
		switch (src->key) {
		case SR_CONF_NUM_LOGIC_CHANNELS:
			num_logic_channels = g_variant_get_int32(src->data);
			break;
		}
	}

	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->status = SR_ST_INACTIVE;
	sdi->model = g_strdup("Demo device");

	devc = g_malloc0(sizeof(struct dev_context));
	devc->limit_samples = 10000;
	devc->cur_samplerate = SR_MHZ(100);
	devc->num_logic_channels = num_logic_channels;
	devc->logic_pattern = DEFAULT_LOGIC_PATTERN;

	if (num_logic_channels > 0) {
		/* Logic channels, all in one channel group. */
		cg = g_malloc0(sizeof(struct sr_channel_group));
		cg->name = g_strdup("Logic");
		for (i = 0; i < num_logic_channels; i++) {
			sprintf(channel_name, "D%d", i);
			ch = sr_channel_new(sdi, i, SR_CHANNEL_LOGIC, TRUE, channel_name);
			cg->channels = g_slist_append(cg->channels, ch);
		}
		sdi->channel_groups = g_slist_append(NULL, cg);
	}

	sdi->priv = devc;

	return std_scan_complete(di, g_slist_append(NULL, sdi));
}

static int dev_clear(const struct sr_dev_driver *di)
{
	return std_dev_clear_with_callback(di, NULL);
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	int pattern;

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;
	switch (key) {
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(devc->cur_samplerate);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		*data = g_variant_new_uint64(devc->limit_samples);
		break;
	case SR_CONF_PATTERN_MODE:
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		/* Any channel in the group will do. */
		pattern = devc->logic_pattern;
		*data = g_variant_new_string(logic_pattern_str[pattern]);
		break;
    case SR_CONF_CAPTURE_RATIO:
		*data = g_variant_new_uint64(devc->capture_ratio);
		break;
	case SR_CONF_HOLDOFF:
		*data = g_variant_new_uint64(devc->holdoff_samples);
		break;
	case SR_CONF_REPEAT_TRIGGER:
		*data = g_variant_new_boolean(devc->repeat_trigger);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	GSList *l;
	int logic_pattern;

	devc = sdi->priv;

	switch (key) {
	case SR_CONF_SAMPLERATE:
		devc->cur_samplerate = g_variant_get_uint64(data);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		devc->limit_samples = g_variant_get_uint64(data);
		break;
	case SR_CONF_PATTERN_MODE:
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		logic_pattern = std_str_idx(data, ARRAY_AND_SIZE(logic_pattern_str));
		if (logic_pattern < 0)
			return SR_ERR_ARG;
		for (l = cg->channels; l; l = l->next) {
			if (logic_pattern == -1)
				return SR_ERR_ARG;
			sr_dbg("Setting logic pattern to %s",
					logic_pattern_str[logic_pattern]);
			devc->logic_pattern = logic_pattern;
			/* Might as well do this now, these are static. */
			if (logic_pattern == PATTERN_ALL_LOW)
				memset(devc->logic_data, 0x00, LOGIC_BUFSIZE);
			else if (logic_pattern == PATTERN_ALL_HIGH)
				memset(devc->logic_data, 0xff, LOGIC_BUFSIZE);
		}
		break;
    case SR_CONF_CAPTURE_RATIO:
            devc->capture_ratio = g_variant_get_uint64(data);
            break;
	case SR_CONF_HOLDOFF:
            devc->holdoff_samples = g_variant_get_uint64(data);
            break;
	case SR_CONF_REPEAT_TRIGGER:
			devc->repeat_trigger = g_variant_get_boolean(data);
			break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	if (!cg) {
		switch (key) {
		case SR_CONF_SCAN_OPTIONS:
		case SR_CONF_DEVICE_OPTIONS:
			return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
		case SR_CONF_SAMPLERATE:
			*data = std_gvar_samplerates_steps(ARRAY_AND_SIZE(samplerates));
			break;
		case SR_CONF_TRIGGER_MATCH:
			*data = std_gvar_array_i32(ARRAY_AND_SIZE(trigger_matches));
			break;
		default:
			return SR_ERR_NA;
		}
	} else {
		switch (key) {
		case SR_CONF_DEVICE_OPTIONS:
			*data = std_gvar_array_u32(ARRAY_AND_SIZE(devopts_cg_logic));
			break;
		case SR_CONF_PATTERN_MODE:
			*data = g_variant_new_strv(ARRAY_AND_SIZE(logic_pattern_str));
			break;
		default:
			return SR_ERR_NA;
		}
	}

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_trigger *trigger;
	GSList *l;
	struct sr_channel *ch;

	devc = sdi->priv;
	devc->sent_samples = 0;
	devc->sent_frame_samples = 0;

    if ((trigger = sr_session_trigger_get(sdi->session))) {
            int pre_trigger_samples = 0;
            if (devc->limit_samples > 0)
                    pre_trigger_samples = (devc->capture_ratio * devc->limit_samples) / 100;
            devc->stl = soft_trigger_logic_new(sdi, trigger, pre_trigger_samples);
            if (!devc->stl)
                    return SR_ERR_MALLOC;
			devc->stl->holdoff_samples = devc->holdoff_samples;
            devc->trigger_fired = FALSE;
    } else
            devc->trigger_fired = TRUE;

	/* Check for enabled channels and define map + max logic_unitsize*/
	devc->enabled_logic_ch_map = 0x0;
	for (l = sdi->channels; l; l = l->next) {
		ch = l->data;
		if (ch->type != SR_CHANNEL_LOGIC)
			continue;
		if (ch->enabled) {
			devc->logic_unitsize = MAX((devc->logic_unitsize), (unsigned int)((ch->index + 1 + 7) / 8));
			devc->enabled_logic_ch_map |= 1 << ch->index;
		}
	}

	sr_session_source_add(sdi->session, -1, 0, 100,
			demo_prepare_data, (struct sr_dev_inst *)sdi);

	std_session_send_df_header(sdi);

	if (SAMPLES_PER_FRAME > 0)
		std_session_send_frame_begin(sdi);

	/* We use this timestamp to decide how many more samples to send. */
	devc->start_us = g_get_monotonic_time();
	devc->spent_us = 0;
	devc->step = 0;

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	sr_session_source_remove(sdi->session, -1);

	if (SAMPLES_PER_FRAME > 0)
		std_session_send_frame_end(sdi);

	std_session_send_df_end(sdi);

	return SR_OK;
}

static struct sr_dev_driver demo_driver_info = {
	.name = "demo",
	.longname = "Demo driver and pattern generator",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = std_dummy_dev_open,
	.dev_close = std_dummy_dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(demo_driver_info);
