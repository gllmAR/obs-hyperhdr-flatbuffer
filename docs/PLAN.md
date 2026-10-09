# Plan: obs-hyperhdr-flatbuffer

Companion to [SDD.md](SDD.md). Phases run in order. Each phase has an exit criterion. A phase is not complete until its criterion is met and the result is recorded in the repository.

## Phase 0: Decisions and scaffolding

Tasks:

- Q-1 closed: GPL-2.0-or-later; owner-authored ofxhyperhdr protocol files may be reused (see SDD Q-1). Q-2 closed: OBS 32.2.2, obs-deps 2026-07-15.
- Add `LICENSE` (GPL-2.0-or-later text) and a `THIRD_PARTY` notice for the HyperHDR MIT schema.
- Create the layout in SDD section 14.1 (empty modules and CMake skeleton). Add a root `.gitignore` for build output.

Exit: decisions recorded in SDD section 19 and 20; `cmake -S . -B build` configures with the core target only.

## Phase 1: Core protocol and transport

Tasks:

- Implement the encoder (`Register`, `Image` RGB, `Clear`) and framing with limits.
- Implement the transport (POSIX unix and TCP, Winsock TCP) and the reply drain.
- Implement `Mailbox` and `Sink` with the state machine and throttle.
- Write `tools/mock_hyperhdr.py` (accepts frames, records the sequence, sends replies).
- Write golden tests against `tools/verify_flat.py`.
- Spikes: Q-3 (Windows transport) and Q-4 (duplicate priority behaviour) on hardware.

Exit: all core unit and golden tests pass on Linux, macOS, and Windows. A live smoke run against a real HyperHDR shows the priority active and cleared.

## Phase 2: CI and OBS build bootstrap

Tasks:

- Write `.github/workflows/ci.yml` with the `core` job (Linux, with TSan) and the `obs-build` matrix (Linux, Windows, macOS).
- Build libobs and obs-frontend-api at the pinned version on each runner, with caching.
- Add the plugin target with an empty module (`OBS_DECLARE_MODULE`) and confirm it loads in OBS on each OS.
- Spike Q-5 (raw output conversion cost) and Q-6 (Tools menu API) in this phase if needed.

Exit: CI is green on all jobs with the empty plugin. A tagged artifact per OS is produced on a test tag.

## Phase 3: Main output (program mix)

Tasks:

- Register `hyperhdr_output` (raw video, no encoder).
- Start and stop on the video tick. Stop sends `Clear`.
- `raw_video` callback publishes to the mailbox. RGBA to RGB conversion runs in the sink thread.
- Tools dialog (Qt, optional build) for origin, priority, size, max FPS, flip, endpoint.
- Settings persistence to `hyperhdr.json`.

Exit: with the output enabled, a test pattern in the program canvas drives the LEDs. Stopping the output releases the priority. Settings survive an OBS restart. Measured callback cost is recorded against the budget in SDD section 17.

## Phase 4: Filter (per source)

Tasks:

- Register `hyperhdr_filter` with `obs_source_info` (filter type).
- Implement the offscreen GPU capture into a 64x36 texrender, with the render-state rules from SDD section 10.2.
- Registry and shutdown ordering from SDD section 12.1.
- Per-instance state and names.

Exit: three filters on three sources publish concurrently, including one camera or media source. No render artefacts in the main scene (manual checklist). Quit with Lua scripts loaded finishes within 5 s.

## Phase 5: Configuration and UI

Tasks:

- `obs_properties` page for the filter (origin, priority, size, max FPS, flip, keep aspect).
- Sink status shown in both UIs (state and last error).
- Duplicate-priority warning (FR-8).
- Locale file `data/locale/en-US.ini`.

Exit: a user can configure and monitor each mode without editing files. Validation clamps and reports out-of-range values.

## Phase 6: Hardening and performance

Tasks:

- Reconnect and backoff tests with HyperHDR restarts.
- 1 h soak test with memory and FD checks.
- Sink CPU measurement at 64x36 and 30 fps. Compare against the budget.
- Sandbox check for Q-8 (Flatpak or sandboxed OBS).
- End-to-end latency measurement against the budget.
- Re-evaluate Q-7 (default size) with hardware.

Exit: soak passes. Budget items met or recorded as accepted deviations.

## Phase 7: Packaging and release

Tasks:

- Package per OS as described in SDD section 15 (`package` job on tags), with SHA-256 sums.
- Document installation paths for each OS (research/05, section 6).
- macOS signing and notarization plan (R-9). Implement only if the release needs it.
- Write user documentation: installation, configuration, troubleshooting (socket path, TCP fallback).

Exit: a tagged release contains a plugin per OS. The install steps work on a clean machine for each OS.

## Milestone summary

| Phase | Output | Gate |
|---|---|---|
| 0 | Decisions, layout | Decisions closed |
| 1 | Core, golden tests, mock | Core tests green on 3 OS; live smoke |
| 2 | CI and OBS build | CI green on 3 OS |
| 3 | Main output | LEDs follow program; clean stop |
| 4 | Filter | Three filters; clean quit |
| 5 | Config UI | Configurable without files |
| 6 | Hardening | Soak and budget |
| 7 | Release | Tagged artifacts |

## Risks to watch across phases

See SDD section 18. The highest-impact items are R-3 (render state), R-4 (quit deadlock), and R-7 (license). Each has a named check in Phases 4 and 0.
