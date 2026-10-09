// SPDX-License-Identifier: GPL-2.0-or-later
// Module entry: registers the main output and the filter, wires the frontend
// events for startup and the SDD 12.1 shutdown ordering.

#include "hhd_glue.h"

#include <obs-module.h>
#include <obs-frontend-api.h>

#ifdef HYPERHDR_QT_UI
#include "hhd-dialog.h"
#include <QAction>
#endif

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-hyperhdr", "en-US")

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

static void on_frontend_event(enum obs_frontend_event event, void *)
{
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
		// Video is up: safe to start the main output if enabled.
		hhd_main_output_apply();
	} else if (event == OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN ||
		   event == OBS_FRONTEND_EVENT_EXIT) {
		// SDD 12.1: shutdown flag first (render callbacks early-out),
		// then render callbacks off, then every sink stopped with a
		// bounded Clear. This ordering avoids the scripting-mutex
		// deadlock recorded in HIVE.
		hhd_registry_shutdown();
		hhd_main_output_release();
	}
}

#ifndef HYPERHDR_QT_UI
// Fallback when the Qt dialog is not built: a plain toggle.
static void on_tools_menu(void *)
{
	hhd_settings().main.enabled = !hhd_settings().main.enabled;
	hhd_settings_save();
	hhd_main_output_apply();
	blog(LOG_INFO, "[hyperhdr] main output %s",
	     hhd_settings().main.enabled ? "enabled" : "disabled");
}
#endif

bool obs_module_load(void)
{
	blog(LOG_INFO, "[obs-hyperhdr] loaded version %s (build %s)",
	     HYPERHDR_VERSION, HYPERHDR_BUILD_ID);

	hhd_settings_load();
	hhd_register_output();
	hhd_register_filter();

	obs_frontend_add_event_callback(on_frontend_event, nullptr);
#ifdef HYPERHDR_QT_UI
	obs_frontend_push_ui_translation(obs_module_get_string);
	QAction *action = static_cast<QAction *>(
		obs_frontend_add_tools_menu_qaction("HyperHDR"));
	obs_frontend_pop_ui_translation();
	if (action)
		QObject::connect(action, &QAction::triggered,
				 [] { hhd_ui::showMainOutputDialog(); });
#else
	obs_frontend_add_tools_menu_item("HyperHDR Main Output (toggle)",
					 on_tools_menu, nullptr);
#endif
	return true;
}

void obs_module_unload(void)
{
	hhd_registry_shutdown();
	hhd_main_output_release();
	blog(LOG_INFO, "[obs-hyperhdr] unloaded");
}
