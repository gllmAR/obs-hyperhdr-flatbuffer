/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <obs-module.h>

OBS_DECLARE_MODULE()

#ifndef HYPERHDR_VERSION
#define HYPERHDR_VERSION "0.0.0"
#endif

#ifndef HYPERHDR_BUILD_ID
#define HYPERHDR_BUILD_ID "unknown"
#endif

const char *obs_module_name(void)
{
	return "HyperHDR FlatBuffer output";
}

bool obs_module_load(void)
{
	blog(LOG_INFO, "[obs-hyperhdr] loaded version %s (build %s)",
	     HYPERHDR_VERSION, HYPERHDR_BUILD_ID);
	return true;
}

void obs_module_unload(void)
{
	blog(LOG_INFO, "[obs-hyperhdr] unloaded");
}
