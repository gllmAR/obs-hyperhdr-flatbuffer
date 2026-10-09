// SPDX-License-Identifier: GPL-2.0-or-later
// Shared glue between the module entry, the main output and the filter.
#pragma once

#include "hhd_sink.h"

#include <string>

// ---- Settings (hyperhdr.json in the module config path, SDD 11.1) ----

struct hhd_main_settings {
    bool enabled = false;
    std::string origin = "OBS Program";
    int priority = 150;
    int width = 64;
    int height = 36;
    double max_fps = 30.0;
    bool flip_vertical = false;
};

struct hhd_settings_data {
    hhd::Endpoint endpoint;
    hhd_main_settings main;
};

// Process-wide settings. Written on the UI thread (load, menu toggle) and read
// from the UI or video thread. No locking: writers and readers never overlap
// on the same field while it changes.
hhd_settings_data& hhd_settings();
void hhd_settings_load();
void hhd_settings_save();

// Clamps per SDD 11.3. Logs once per call when the value was out of range.
int hhd_clamp_int(int value, int lo, int hi, const char* what);
double hhd_clamp_double(double value, double lo, double hi, const char* what);

// ---- Registry and shutdown (SDD 12.1) ----

struct hhd_registry_entry {
    void* owner;
    int priority;
    void (*stop)(void* owner);           // bounded Clear + join
    void (*remove_render)(void* owner);  // optional, may be null
};

bool hhd_shutting_down();
// Adds or replaces the entry for owner.
void hhd_registry_add(const hhd_registry_entry& entry);
void hhd_registry_remove(void* owner);
// FR-8: logs a warning when another live sink already claims priority.
void hhd_registry_warn_duplicate(void* owner, int priority, const char* name);
// Sets the shutdown flag, removes render callbacks, stops every sink. Idempotent.
void hhd_registry_shutdown();

// ---- Registration (called from obs_module_load) ----

void hhd_register_output();
void hhd_register_filter();

// ---- Main output control (UI thread only) ----

void hhd_main_output_apply();    // create+start or stop+release per settings;
                                 // restarts a running output to apply new parameters
void hhd_main_output_release();  // unconditional teardown at exit, idempotent
// Live sink status for the UI (FR-9). False when the output is not running.
bool hhd_main_output_sink_status(hhd::SinkState* state, std::string* last_error);
