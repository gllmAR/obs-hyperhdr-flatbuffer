// SPDX-License-Identifier: GPL-2.0-or-later
// Filter: publishes one source as RGB frames (SDD 10). Pass-through filter:
// video_render only taps the picture, the capture runs in an offscreen
// callback so it works even when the source is not on the active scene.

#include "hhd_glue.h"

#include <obs-module.h>

#include <atomic>
#include <mutex>
#include <vector>

#define T_(s) obs_module_text(s)

namespace {

struct hhd_filter {
    obs_source_t* source = nullptr;

    // Settings snapshot. Written in update (UI thread), read in tick and in
    // the render callback, always under mu.
    std::mutex mu;
    hhd::SinkConfig cfg;
    bool keep_aspect = false;

    std::atomic<bool> restart_needed{true};
    std::atomic<bool> in_capture{false};
    std::atomic<bool> rendered{false};  // one capture per video tick (SDD 10.2)

    // Sink lifecycle runs on the video tick; submit runs on the graphics
    // thread. sink_mu covers both the pointer and submit. sink_active tracks
    // the OBS enable state so a disabled filter releases its priority slot.
    std::mutex sink_mu;
    hhd::Sink* sink = nullptr;
    std::atomic<bool> sink_active{false};

    // Graphics-thread resources, sized (gw, gh).
    gs_texrender_t* tex = nullptr;
    gs_stagesurf_t* stage = nullptr;
    uint32_t gw = 0;
    uint32_t gh = 0;
    std::vector<uint8_t> rgb;

    // SDD 12.2: a graphics failure during capture is logged once, per point.
    bool logged_begin_error = false;
    bool logged_tex_error = false;
    bool logged_map_error = false;
    void log_once(bool& flag, const char* what) {
        if (flag) return;
        flag = true;
        blog(LOG_WARNING, "[hyperhdr] filter capture failed: %s (further failures "
                          "not logged)",
             what);
    }
};

void filter_offscreen(void* data, uint32_t cx, uint32_t cy);

void filter_stop_sink(void* data) {
    auto* f = static_cast<hhd_filter*>(data);
    std::lock_guard<std::mutex> lock(f->sink_mu);
    if (f->sink) {
        f->sink->stop();  // bounded Clear (SDD 8.2)
        delete f->sink;
        f->sink = nullptr;
    }
}

void filter_remove_render(void* data) {
    obs_remove_main_render_callback(filter_offscreen, data);
}

// ---- Settings ----

void filter_defaults(obs_data_t* settings) {
    obs_data_set_default_int(settings, "priority", 150);
    obs_data_set_default_int(settings, "width", 64);
    obs_data_set_default_int(settings, "height", 36);
    obs_data_set_default_double(settings, "max_fps", 30.0);
    obs_data_set_default_bool(settings, "flip_vertical", false);
    obs_data_set_default_bool(settings, "keep_aspect", false);
}

void filter_update(void* data, obs_data_t* settings) {
    auto* f = static_cast<hhd_filter*>(data);

    hhd::SinkConfig cfg;
    cfg.origin = obs_data_get_string(settings, "origin");
    cfg.priority =
        hhd_clamp_int((int)obs_data_get_int(settings, "priority"), 0, 254, "priority");
    cfg.width = hhd_clamp_int((int)obs_data_get_int(settings, "width"), 8, 256, "width");
    cfg.height =
        hhd_clamp_int((int)obs_data_get_int(settings, "height"), 8, 256, "height");
    cfg.maxFps =
        hhd_clamp_double(obs_data_get_double(settings, "max_fps"), 0.0, 60.0, "max_fps");
    cfg.flipVertical = obs_data_get_bool(settings, "flip_vertical");
    cfg.endpoint = hhd_settings().endpoint;
    const bool keep_aspect = obs_data_get_bool(settings, "keep_aspect");

    std::lock_guard<std::mutex> lock(f->mu);
    if (cfg.origin != f->cfg.origin || cfg.priority != f->cfg.priority ||
        cfg.width != f->cfg.width || cfg.height != f->cfg.height ||
        cfg.maxFps != f->cfg.maxFps || cfg.flipVertical != f->cfg.flipVertical ||
        cfg.endpoint.host != f->cfg.endpoint.host ||
        cfg.endpoint.port != f->cfg.endpoint.port ||
        cfg.endpoint.preferDomainSocket != f->cfg.endpoint.preferDomainSocket ||
        cfg.endpoint.domainSocketPath != f->cfg.endpoint.domainSocketPath) {
        f->restart_needed.store(true);
    }
    f->cfg = cfg;
    f->keep_aspect = keep_aspect;
}

obs_properties_t* filter_properties(void*) {
    obs_properties_t* props = obs_properties_create();
    obs_properties_add_text(props, "origin", T_("Origin"), OBS_TEXT_DEFAULT);
    obs_properties_add_int(props, "priority", T_("Priority"), 0, 254, 1);
    obs_properties_add_int(props, "width", T_("Width"), 8, 256, 1);
    obs_properties_add_int(props, "height", T_("Height"), 8, 256, 1);
    obs_properties_add_float(props, "max_fps", T_("MaxFps"), 0.0, 60.0, 1.0);
    obs_properties_add_bool(props, "flip_vertical", T_("FlipVertical"));
    obs_properties_add_bool(props, "keep_aspect", T_("KeepAspect"));
    return props;
}

// ---- Lifecycle ----

void* filter_create(obs_data_t* settings, obs_source_t* source) {
    auto* f = new hhd_filter;
    f->source = source;
    filter_update(f, settings);
    // Offscreen capture runs even when the parent is not on the active scene.
    obs_add_main_render_callback(filter_offscreen, f);
    return f;
}

void filter_destroy(void* data) {
    auto* f = static_cast<hhd_filter*>(data);
    hhd_registry_remove(f);
    obs_remove_main_render_callback(filter_offscreen, f);
    filter_stop_sink(f);

    // Graphics objects die on the graphics thread.
    obs_enter_graphics();
    if (f->stage) gs_stagesurface_destroy(f->stage);
    if (f->tex) gs_texrender_destroy(f->tex);
    obs_leave_graphics();

    delete f;
}

// ---- Video tick (video thread): start and stop the sink here, never on the
// graphics thread (SDD D-12) ----

void filter_tick(void* data, float) {
    auto* f = static_cast<hhd_filter*>(data);
    f->rendered.store(false);

    // After the shutdown event no new sink may start; the registry already
    // stopped every live one.
    if (hhd_shutting_down()) return;

    // The OBS enable checkbox is the on/off switch for this instance: when it
    // is cleared, stop the sink and release the HyperHDR priority slot.
    if (!obs_source_enabled(f->source)) {
        if (f->sink_active.exchange(false)) {
            hhd_registry_remove(f);
            filter_stop_sink(f);
            blog(LOG_INFO, "[hyperhdr] filter '%s' disabled, sink stopped",
                 obs_source_get_name(f->source));
        }
        return;
    }
    // A filter that was disabled or has no sink yet needs a fresh start even
    // when its settings did not change.
    if (!f->sink_active.load()) f->restart_needed.store(true);

    if (!f->restart_needed.exchange(false)) return;

    hhd::SinkConfig cfg;
    {
        std::lock_guard<std::mutex> lock(f->mu);
        cfg = f->cfg;
    }
    if (cfg.origin.empty()) cfg.origin = obs_source_get_name(f->source);

    {
        std::lock_guard<std::mutex> lock(f->sink_mu);
        if (f->sink) {
            f->sink->stop();
            delete f->sink;
        }
        f->sink = new hhd::Sink(cfg);
        f->sink->start();
    }

    hhd_registry_entry entry = {f, cfg.priority, filter_stop_sink, filter_remove_render};
    hhd_registry_add(entry);
    hhd_registry_warn_duplicate(f, cfg.priority, cfg.origin.c_str());
    f->sink_active.store(true);
    blog(LOG_INFO, "[hyperhdr] filter '%s' streaming: %dx%d max %.0f fps, priority %d",
         cfg.origin.c_str(), cfg.width, cfg.height, cfg.maxFps, cfg.priority);
}

void filter_render(void* data, gs_effect_t*) {
    auto* f = static_cast<hhd_filter*>(data);
    // Tap only: never change the picture. skip_video_filter (not
    // default_render) so async sources such as cameras draw (SDD 10.2).
    obs_source_skip_video_filter(f->source);
}

// ---- Offscreen capture (graphics thread) ----

void filter_offscreen(void* data, uint32_t, uint32_t) {
    auto* f = static_cast<hhd_filter*>(data);
    if (hhd_shutting_down()) return;
    if (f->in_capture.load() || f->rendered.load()) return;
    f->in_capture.store(true);

    obs_source_t* parent = obs_filter_get_parent(f->source);
    const uint32_t pw = parent ? obs_source_get_width(parent) : 0;
    const uint32_t ph = parent ? obs_source_get_height(parent) : 0;

    hhd::SinkConfig cfg;
    bool keep_aspect;
    {
        std::lock_guard<std::mutex> lock(f->mu);
        cfg = f->cfg;
        keep_aspect = f->keep_aspect;
    }
    const uint32_t w = (uint32_t)cfg.width;
    const uint32_t h = (uint32_t)cfg.height;

    if (!parent || pw == 0 || ph == 0) {
        f->in_capture.store(false);
        return;
    }

    if (!f->tex) f->tex = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
    if (f->gw != w || f->gh != h) {
        if (f->stage) {
            gs_stagesurface_destroy(f->stage);
            f->stage = nullptr;
        }
        f->gw = w;
        f->gh = h;
    }
    if (!f->stage) f->stage = gs_stagesurface_create(w, h, GS_BGRA);

    if (!f->tex || !f->stage) {
        blog(LOG_WARNING, "[hyperhdr] filter capture: graphics allocation failed");
        f->in_capture.store(false);
        return;
    }

    // A texrender renders once until reset (gs_texrender_end sets its
    // rendered flag; gs_texrender_begin refuses while it is set).
    gs_texrender_reset(f->tex);

    // Paired render state so one instance cannot poison another (SDD 10.2).
    gs_blend_state_push();
    gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
    const bool srgb = gs_framebuffer_srgb_enabled();
    gs_enable_framebuffer_srgb(false);

    if (gs_texrender_begin(f->tex, w, h)) {
        // gs_ortho with the parent size: the GPU does the downscale.
        float left = 0.0f, right = (float)pw, bottom = 0.0f, top = (float)ph;
        if (keep_aspect) {
            // Letterbox (D-10): widen the ortho range so the source keeps
            // its aspect inside the target.
            const float sx = (float)w / (float)pw;
            const float sy = (float)h / (float)ph;
            const float s = sx < sy ? sx : sy;
            const float rangew = (float)w / s;
            const float rangeh = (float)h / s;
            left = -(rangew - (float)pw) * 0.5f;
            right = left + rangew;
            bottom = -(rangeh - (float)ph) * 0.5f;
            top = bottom + rangeh;
        }
        gs_ortho(left, right, bottom, top, -100.0f, 100.0f);

        struct vec4 black;
        vec4_set(&black, 0.0f, 0.0f, 0.0f, 1.0f);
        gs_clear(GS_CLEAR_COLOR, &black, 0.0f, 0);

        obs_source_skip_video_filter(f->source);
        gs_texrender_end(f->tex);
        f->rendered.store(true);
    } else {
        f->log_once(f->logged_begin_error, "texrender begin failed");
    }

    gs_enable_framebuffer_srgb(srgb);
    gs_blend_state_pop();

    if (f->rendered.load()) {
        gs_texture_t* texture = gs_texrender_get_texture(f->tex);
        if (!texture) {
            f->log_once(f->logged_tex_error, "no texture after texrender");
        } else {
            gs_stage_texture(f->stage, texture);
            uint8_t* bits = nullptr;
            uint32_t linesize = 0;
            if (!gs_stagesurface_map(f->stage, &bits, &linesize)) {
                f->log_once(f->logged_map_error, "stagesurface map failed");
            } else {
                f->rgb.resize((size_t)w * (size_t)h * 3);
                for (uint32_t y = 0; y < h; ++y) {
                    const uint8_t* s = bits + (size_t)y * linesize;
                    uint8_t* d = f->rgb.data() + (size_t)y * (size_t)w * 3;
                    for (uint32_t x = 0; x < w; ++x) {
                        d[0] = s[2];  // R (source is BGRA)
                        d[1] = s[1];  // G
                        d[2] = s[0];  // B
                        d += 3;
                        s += 4;
                    }
                }
                gs_stagesurface_unmap(f->stage);

                std::lock_guard<std::mutex> lock(f->sink_mu);
                if (f->sink) f->sink->submit(f->rgb.data(), f->rgb.size());
            }
        }
    }

    f->in_capture.store(false);
}

obs_source_info g_filter_info;

}  // namespace

void hhd_register_filter() {
    g_filter_info = {};
    g_filter_info.id = "hyperhdr_filter";
    g_filter_info.type = OBS_SOURCE_TYPE_FILTER;
    g_filter_info.output_flags = OBS_SOURCE_VIDEO;
    g_filter_info.get_name = [](void*) -> const char* { return T_("Filter"); };
    g_filter_info.create = filter_create;
    g_filter_info.destroy = filter_destroy;
    g_filter_info.get_defaults = filter_defaults;
    g_filter_info.get_properties = filter_properties;
    g_filter_info.update = filter_update;
    g_filter_info.video_tick = filter_tick;
    g_filter_info.video_render = filter_render;
    obs_register_source(&g_filter_info);
}
