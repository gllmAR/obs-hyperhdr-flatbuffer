// SPDX-License-Identifier: GPL-2.0-or-later
// Main output: publishes the program canvas as RGB frames (SDD 9).
// Raw output, no encoder. OBS scales and converts to RGBA at the configured
// size; the raw_video callback strips alpha and publishes to the sink.

#include "hhd_glue.h"

#include <obs-module.h>

#include <vector>

namespace {

struct hhd_output {
    obs_output_t* output = nullptr;
    hhd::Sink* sink = nullptr;  // owned by the output callbacks
    int width = 64;
    int height = 36;
    std::vector<uint8_t> rgb;
};

obs_output_t* g_output = nullptr;
hhd_output* g_output_data = nullptr;  // mirrors g_output for status reads

const char* out_get_name(void*) { return "HyperHDR Main Output"; }

void out_stop_sink(void* data) {
    auto* o = static_cast<hhd_output*>(data);
    if (o->sink) {
        o->sink->stop();  // bounded Clear (SDD 8.2)
        delete o->sink;
        o->sink = nullptr;
    }
}

void* out_create(obs_data_t*, obs_output_t* output) {
    auto* o = new hhd_output;
    o->output = output;
    g_output_data = o;
    return o;
}

void out_destroy(void* data) {
    auto* o = static_cast<hhd_output*>(data);
    out_stop_sink(o);
    if (g_output_data == o) g_output_data = nullptr;
    delete o;
}

bool out_start(void* data) {
    auto* o = static_cast<hhd_output*>(data);
    const hhd_main_settings& ms = hhd_settings().main;

    hhd::SinkConfig cfg;
    cfg.origin = ms.origin.empty() ? "OBS Program" : ms.origin;
    cfg.priority = ms.priority;
    cfg.width = ms.width;
    cfg.height = ms.height;
    cfg.maxFps = ms.max_fps;
    cfg.flipVertical = ms.flip_vertical;
    cfg.endpoint = hhd_settings().endpoint;
    o->width = ms.width;
    o->height = ms.height;

    o->sink = new hhd::Sink(cfg);
    if (!o->sink->start()) {
        blog(LOG_ERROR, "[hyperhdr] main output: sink failed to start");
        delete o->sink;
        o->sink = nullptr;
        return false;
    }

    // OBS does the downscale and the RGBA conversion.
    struct video_scale_info conv = {};
    conv.format = VIDEO_FORMAT_RGBA;
    conv.width = (uint32_t)ms.width;
    conv.height = (uint32_t)ms.height;
    conv.range = VIDEO_RANGE_DEFAULT;
    conv.colorspace = VIDEO_CS_DEFAULT;
    obs_output_set_video_conversion(o->output, &conv);

    hhd_registry_entry entry = {o, ms.priority, out_stop_sink, nullptr};
    hhd_registry_add(entry);
    hhd_registry_warn_duplicate(o, ms.priority, "main output");

    blog(LOG_INFO, "[hyperhdr] main output started: %dx%d max %.0f fps, priority %d",
         ms.width, ms.height, ms.max_fps, ms.priority);
    return obs_output_begin_data_capture(o->output, 0);
}

void out_stop(void* data, uint64_t) {
    auto* o = static_cast<hhd_output*>(data);
    obs_output_end_data_capture(o->output);
    hhd_registry_remove(o);
    out_stop_sink(o);
    blog(LOG_INFO, "[hyperhdr] main output stopped");
}

void out_raw_video(void* data, struct video_data* frame) {
    auto* o = static_cast<hhd_output*>(data);
    if (!o->sink || !frame->data[0]) return;

    const int w = o->width;
    const int h = o->height;
    o->rgb.resize((size_t)w * (size_t)h * 3);

    const uint8_t* src = frame->data[0];
    const uint32_t linesize = frame->linesize[0];
    for (int y = 0; y < h; ++y) {
        const uint8_t* s = src + (size_t)y * linesize;
        uint8_t* d = o->rgb.data() + (size_t)y * (size_t)w * 3;
        for (int x = 0; x < w; ++x) {
            d[0] = s[0];  // R
            d[1] = s[1];  // G
            d[2] = s[2];  // B
            d += 3;
            s += 4;
        }
    }
    o->sink->submit(o->rgb.data(), o->rgb.size());
}

obs_output_info g_output_info;

}  // namespace

void hhd_register_output() {
    g_output_info = {};
    g_output_info.id = "hyperhdr_output";
    g_output_info.flags = OBS_OUTPUT_VIDEO;
    g_output_info.get_name = out_get_name;
    g_output_info.create = out_create;
    g_output_info.destroy = out_destroy;
    g_output_info.start = out_start;
    g_output_info.stop = out_stop;
    g_output_info.raw_video = out_raw_video;
    obs_register_output(&g_output_info);
}

void hhd_main_output_apply() {
    const bool want = hhd_settings().main.enabled;
    if (want && !g_output) {
        obs_data_t* settings = obs_data_create();
        g_output = obs_output_create("hyperhdr_output", "hyperhdr_main", settings, nullptr);
        obs_data_release(settings);
        if (!g_output) {
            blog(LOG_ERROR, "[hyperhdr] failed to create main output");
            return;
        }
        if (!obs_output_start(g_output)) {
            blog(LOG_WARNING, "[hyperhdr] main output failed to start");
            obs_output_release(g_output);
            g_output = nullptr;
        }
    } else if (!want && g_output) {
        obs_output_stop(g_output);
        obs_output_release(g_output);
        g_output = nullptr;
    } else if (want && g_output) {
        // Running and still wanted: restart so new parameters take effect.
        obs_output_stop(g_output);
        if (!obs_output_start(g_output)) {
            blog(LOG_WARNING, "[hyperhdr] main output failed to restart");
        }
    }
}

bool hhd_main_output_sink_status(hhd::SinkState* state, std::string* last_error) {
    if (!g_output_data || !g_output_data->sink) return false;
    if (state) *state = g_output_data->sink->state();
    if (last_error) *last_error = g_output_data->sink->lastError();
    return true;
}

void hhd_main_output_release() {
    if (!g_output) return;
    obs_output_stop(g_output);
    obs_output_release(g_output);
    g_output = nullptr;
}
