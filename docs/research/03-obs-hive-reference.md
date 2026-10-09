# Research 03: OBS plugin reference (HIVE `obs-hive`)

Status: research complete for the plugin structure, the output and filter patterns, lifecycle, and build. Source: [inspiration/HIVE/plugins/obs-hive](../../inspiration/HIVE/plugins/obs-hive/CMakeLists.txt). HIVE is an OBS plugin that ships both a main output and a per-source filter, which is the pattern this project needs.

## 1. Facts

| Item | Value |
|---|---|
| Size | 23 source files, about 8,700 lines (`wc -l`) |
| License | GPL-2.0-or-later (plugin); HIVE overall states MPL-2.0 in its SDD |
| OBS API | `OBS_DECLARE_MODULE`, `obs_register_source`, `obs_register_output`, `obs_frontend_add_event_callback` |
| Build deps | `libobs`, `obs-frontend-api` (CMake packages), Qt6 Core/Widgets/Network (Tools dialog), FFmpeg avcodec/avutil (decoder only) |
| Pinned OBS | 31.0.0 with obs-deps 2025-08-23 (macOS universal) |
| CI | Woodpecker on Codeberg with Rust images. The plugin is excluded because libobs bootstrap is about 2 GB and about 5 min. |
| Local build | `scripts/dev/build-obs-plugin.sh` (macOS only); `scripts/dev/setup-obs-build-env.sh` clones obs-studio and obs-deps |
| Windows build | Docker cross image plus `build-obs-windows-cross.sh` (clang-cl, xwin) |

Key references:

- [plugin-main.c](../../inspiration/HIVE/plugins/obs-hive/src/plugin-main.c): module entry, frontend event hook.
- [hive-output.c](../../inspiration/HIVE/plugins/obs-hive/src/hive-output.c): network output (`OBS_OUTPUT_VIDEO | OBS_OUTPUT_ENCODED`).
- [main-output.c](../../inspiration/HIVE/plugins/obs-hive/src/main-output.c): program output (canvas mix), encoder settings.
- [hive-filter.c](../../inspiration/HIVE/plugins/obs-hive/src/hive-filter.c): per-source filter with offscreen capture.
- [hive-outputs.h](../../inspiration/HIVE/plugins/obs-hive/src/hive-outputs.h): internal API for the Tools dialog and outputs.
- [hive_net.h](../../inspiration/HIVE/plugins/obs-hive/src/net/hive_net.h): cross-platform socket shim (POSIX and Winsock).
- [tools-menu.cpp](../../inspiration/HIVE/plugins/obs-hive/src/tools-menu.cpp): Tools menu entry through `obs_frontend_add_tools_menu_qaction` (Qt).

## 2. Main output pattern (program mix)

- `obs_output_create("hive_output", ...)` is started from the output settings. The program output consumes the canvas.
- The HIVE output is encoded (HEVC). This project does not need an encoder: the target is a small RGB image, so the output can be raw video.
- The program output is applied through `hive_program_output_apply`, and released on exit or on profile change.

Implication for this project: use `OBS_OUTPUT_VIDEO` without `OBS_OUTPUT_ENCODED`. Frames arrive raw through the `raw_video` callback on the OBS video thread.

## 3. Filter pattern (per source)

HIVE's filter is the design to follow, with one change (no encoder).

Pipeline in the HIVE filter:

1. `obs_add_main_render_callback(offscreen, f)` in create. Capture runs even when the parent is not on the active scene.
2. `video_render` calls `obs_source_skip_video_filter(self)` and does nothing else. The filter is a tap, not an effect.
3. In the offscreen callback: `gs_texrender_begin` at the parent size, `obs_source_skip_video_filter`, `gs_texrender_end`, `gs_stage_texture`, map, convert.
4. Pipeline (re)start is done on the OBS video tick, never on the graphics thread. Starting an encoder inside render deadlocks on the VideoToolbox path.
5. A per-frame `rendered` flag and an `in_capture` re-entry guard.
6. Registry of live instances. `hive_filter_shutdown_all` runs on `OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN` and `EXIT`.

Changes for this project:

- Render directly into a 64x36 texrender (GPU downscale). Use `gs_ortho` with the parent's size so the GPU does the resample. Stage only 64x36 BGRA.
- Replace the encoder with the sink (mailbox, then the HyperHDR sender thread).

## 4. Lessons recorded in the HIVE code

| Lesson | Source |
|---|---|
| `obs_source_default_render(parent)` renders async sources (cameras, media) as blank. Use `obs_source_skip_video_filter`. | [hive-filter.c](../../inspiration/HIVE/plugins/obs-hive/src/hive-filter.c) header comment |
| Render state must be paired. A missing sRGB or blend pop makes the second filter render black. | hive-filter.c, `gs_blend_state_push`, `gs_set_linear_srgb` |
| Shutdown deadlock with Lua or Python scripting. Stop filter pipelines before scripting unloads. | hive-filter.c, `hive_filter_shutdown_all`; plugin-main.c, `SCRIPTING_SHUTDOWN` |
| Keep the video queue cache small. `cache_size=1` saves about 16 ms at 60 fps when the consumer is fast. | hive-filter.c, `voi.cache_size` comment |
| Cross-platform sockets need an abstraction. `SIGPIPE` handling differs (`MSG_NOSIGNAL`, `SO_NOSIGPIPE`). | hive_net.h |

## 5. Build and packaging

- Linux and Windows layout (OBS): `<plugins>/<name>/bin/64bit/<name>.so|.dll` plus `<name>/data/`.
- macOS: `.plugin` bundle.
- Per-OS user plugin paths: macOS `~/Library/Application Support/obs-studio/plugins`, Windows `%APPDATA%/obs-studio/plugins`, Linux `~/.config/obs-studio/plugins`.
- HIVE's `build-obs-windows-cross.sh` packages the output into `dist/windows-obs-x64/obs-hive/bin/64bit/`.

## 6. Gaps in the HIVE code that this project avoids

- HIVE builds libobs locally on a developer Mac. CI does not build the plugin, so there is no cross-platform guarantee.
- HIVE depends on Qt6 for the dialog and FFmpeg for the decoder. This project needs neither for the sender, so Qt is optional and the core builds without OBS.
- HIVE's filter and output share a lot of implicit state through the registry. The new design gives each sink its own state.

## 7. Open questions for the OBS side

- Confirm `obs_frontend_add_tools_menu_item` (non-Qt) exists in the pinned `obs-frontend-api.h`. HIVE uses only the Qt variant.
- Confirm `obs_output_set_video_conversion` is available for raw output scaling in the pinned OBS version (not used in HIVE).
