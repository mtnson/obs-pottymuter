#include "pottymuter-filter.h"

struct obs_source_info pottymuter_filter_info = {
	.id = "pottymuter_audio_filter",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_AUDIO,
	.get_name = pottymuter_name,
	.create = pottymuter_create,
	.destroy = pottymuter_destroy,
	.get_defaults = pottymuter_defaults,
	.get_properties = pottymuter_properties,
	.update = pottymuter_update,
	.activate = pottymuter_activate,
	.deactivate = pottymuter_deactivate,
	.filter_audio = pottymuter_filter_audio,
};
