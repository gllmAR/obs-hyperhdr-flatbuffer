// SPDX-License-Identifier: GPL-2.0-or-later
// Global settings: hyperhdr.json in the module config path (SDD 11.1).

#include "hhd_glue.h"

#include <obs-module.h>

namespace {

hhd_settings_data g_settings;

std::string config_path() {
    char* path = obs_module_config_path("hyperhdr.json");
    std::string out = path ? path : "";
    if (path) bfree(path);
    return out;
}

}  // namespace

int hhd_clamp_int(int value, int lo, int hi, const char* what) {
    if (value < lo || value > hi) {
        const int clamped = value < lo ? lo : hi;
        blog(LOG_WARNING, "[hyperhdr] %s %d out of range, clamped to %d", what, value,
             clamped);
        return clamped;
    }
    return value;
}

double hhd_clamp_double(double value, double lo, double hi, const char* what) {
    if (value < lo || value > hi) {
        const double clamped = value < lo ? lo : hi;
        blog(LOG_WARNING, "[hyperhdr] %s %.2f out of range, clamped to %.2f", what, value,
             clamped);
        return clamped;
    }
    return value;
}

hhd_settings_data& hhd_settings() { return g_settings; }

void hhd_settings_load() {
    const std::string path = config_path();
    obs_data_t* data = obs_data_create_from_json_file(path.c_str());
    if (!data) {
        blog(LOG_INFO, "[hyperhdr] no config at %s, using defaults", path.c_str());
        return;
    }

    obs_data_set_default_bool(data, "prefer_domain_socket", true);
    obs_data_set_default_string(data, "host", "127.0.0.1");
    obs_data_set_default_int(data, "port", 19400);

    hhd::Endpoint& ep = g_settings.endpoint;
    ep.preferDomainSocket = obs_data_get_bool(data, "prefer_domain_socket");
    ep.domainSocketPath = obs_data_get_string(data, "domain_socket_path");
    ep.host = obs_data_get_string(data, "host");
    ep.port = hhd_clamp_int((int)obs_data_get_int(data, "port"), 1, 65535, "port");
    if (ep.host.empty()) ep.host = "127.0.0.1";

    obs_data_t* main = obs_data_get_obj(data, "main_output");
    if (main) {
        hhd_main_settings& ms = g_settings.main;
        ms.enabled = obs_data_get_bool(main, "enabled");
        ms.origin = obs_data_get_string(main, "origin");
        if (ms.origin.empty()) ms.origin = "OBS Program";
        ms.priority = hhd_clamp_int((int)obs_data_get_int(main, "priority"), 0, 254,
                                    "priority");
        ms.width = hhd_clamp_int((int)obs_data_get_int(main, "width"), 8, 256, "width");
        ms.height =
            hhd_clamp_int((int)obs_data_get_int(main, "height"), 8, 256, "height");
        ms.max_fps = hhd_clamp_double(obs_data_get_double(main, "max_fps"), 0.0, 60.0,
                                      "max_fps");
        ms.flip_vertical = obs_data_get_bool(main, "flip_vertical");
        obs_data_release(main);
    }

    blog(LOG_INFO, "[hyperhdr] config loaded from %s", path.c_str());
    obs_data_release(data);
}

void hhd_settings_save() {
    const std::string path = config_path();
    if (path.empty()) {
        blog(LOG_ERROR, "[hyperhdr] no module config path, settings not saved");
        return;
    }

    obs_data_t* data = obs_data_create();
    const hhd::Endpoint& ep = g_settings.endpoint;
    obs_data_set_bool(data, "prefer_domain_socket", ep.preferDomainSocket);
    obs_data_set_string(data, "domain_socket_path", ep.domainSocketPath.c_str());
    obs_data_set_string(data, "host", ep.host.c_str());
    obs_data_set_int(data, "port", ep.port);
    obs_data_set_int(data, "version", 1);

    obs_data_t* main = obs_data_create();
    const hhd_main_settings& ms = g_settings.main;
    obs_data_set_bool(main, "enabled", ms.enabled);
    obs_data_set_string(main, "origin", ms.origin.c_str());
    obs_data_set_int(main, "priority", ms.priority);
    obs_data_set_int(main, "width", ms.width);
    obs_data_set_int(main, "height", ms.height);
    obs_data_set_double(main, "max_fps", ms.max_fps);
    obs_data_set_bool(main, "flip_vertical", ms.flip_vertical);
    obs_data_set_obj(data, "main_output", main);
    obs_data_release(main);

    if (!obs_data_save_json_safe(data, path.c_str(), "tmp", "bak")) {
        blog(LOG_ERROR, "[hyperhdr] failed to save %s", path.c_str());
    }
    obs_data_release(data);
}
