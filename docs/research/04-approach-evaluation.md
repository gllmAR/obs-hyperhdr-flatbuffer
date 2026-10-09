# Research 04: Approach evaluation

Status: decision record input for the SDD. Compares the options and records the recommended choice for each design axis.

## 1. Plugin modes

| Option | Description | Pros | Cons |
|---|---|---|---|
| A. Main output only | One `obs_output` on the program canvas | One sink, simplest, cheapest CPU | Cannot drive LEDs from a single source |
| B. Filter only | Per-source filter | Per-source control | N render paths; a source must be rendered to be sent; no single program mix |
| C. Main output + filter (chosen) | DistroAV-style: program mix plus per-source filter sharing one core | Covers both use cases; core shared | Two capture paths to test |
| D. External sender process | Standalone program reading NDI, Syphon, or a virtual camera | No OBS API work | Extra hop and latency; depends on another tool |

Choice: **C**. The shared core (encoder, transport, sink, mailbox) keeps the two modes thin.

## 2. Wire payload

| Option | Pros | Cons |
|---|---|---|
| RGB24 `RawImage` at 64x36 (chosen) | Tiny (6,912 bytes per frame), proven in ofxhyperhdr and HIVE RFC | Converts from NV12 or BGRA |
| NV12 `Image` | Saves a conversion from NV12 sources | Not needed for the target size; extra test surface |
| HEVC over HIVE, decoded by HyperHDR | Reuses HIVE | HyperHDR does not take HEVC on this path; adds an encoder and latency |

Choice: **RGB24 at 64x36**, configurable from 8x8 to 256x256. NV12 remains a later option.

## 3. Capture path for the filter

| Option | Pros | Cons |
|---|---|---|
| CPU readback of full-resolution frame, then scale | Simple | Full-frame readback every tick; expensive at 1080p |
| GPU downscale into a 64x36 texrender, then stage (chosen) | Tiny readback; GPU does the resample | Needs careful render-state push and pop |

Choice: **GPU downscale**, based on HIVE's offscreen pattern.

## 4. Capture path for the main output

| Option | Pros | Cons |
|---|---|---|
| `obs_output` raw video with `obs_output_set_video_conversion` to RGBA 64x36 (chosen for v1) | Uses libobs' own conversion. Simple. No custom GPU code | CPU scaler on the video thread; cost to be measured |
| Custom offscreen GPU path (same as filter) | Same as the filter, consistent | More code to get right at once |

Choice: **obs_output raw path in v1**. Move to the GPU path only if measurements show the CPU scaler is a bottleneck. This must be verified in the Phase 3 spike.

## 5. Settings UI

| Option | Pros | Cons |
|---|---|---|
| Qt6 dialog from Tools menu, optional | Matches HIVE and OBS conventions for outputs | Qt build dependency in CI |
| `obs_properties` only | No Qt | Main output has no natural home for its settings |
| JSON file only | Simplest | Poor UX |

Choice: **Qt dialog for the main output, behind a CMake option (on by default)**. The filter uses `obs_properties`. A Qt-less build is kept for CI smoke builds.

## 6. Language and layering

| Option | Pros | Cons |
|---|---|---|
| C11 everywhere (HIVE plugin core) | Matches OBS C API | Harder threading and mailbox code |
| C++17 core, C-linkage OBS glue (chosen) | Standard threads and mutexes; easy unit tests | Keep the OBS boundary strict |

Choice: **C++17 core with no OBS dependency**, plus a thin OBS glue layer written in C++ with `extern "C"` entry points where OBS requires them.

## 7. FlatBuffers encoder

| Option | Pros | Cons |
|---|---|---|
| Official FlatBuffers C++ runtime | Maintained; generated code | Apache-2.0. Incompatible with GPL-2.0-only; compatible with GPL-3.0. Adds a dependency and `flatc` step |
| Hand-rolled writer from the MIT schema (chosen) | No dependency; proven byte-exact in both references; GPL-compatible | We maintain it. Mitigated by golden tests against the Python builder |

Choice: **hand-rolled writer**, written from the schema and validated against the Python `flatbuffers` builder.

## 8. CI platform

| Option | Pros | Cons |
|---|---|---|
| GitHub Actions (chosen) | Repo is on GitHub (`gllmAR/obs-hyperhdr-flatbuffer`); native macOS and Windows runners | Runner minutes; libobs build time needs caching |
| Woodpecker (HIVE) | Matches HIVE | Repo is not on Codeberg; Linux-only builds |

Choice: **GitHub Actions**, with a matrix over Linux, Windows, and macOS.

## 9. Summary of decisions

| Axis | Decision |
|---|---|
| Modes | Main output (raw) plus per-source filter (GPU downscale), one shared core |
| Payload | RGB24, default 64x36 |
| Core | C++17, no OBS or Qt dependency |
| Encoder | Hand-rolled, golden-tested |
| UI | Qt dialog (optional build) for the main output; `obs_properties` for the filter |
| CI | GitHub Actions matrix (Linux, Windows, macOS) |
