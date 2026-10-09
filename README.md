# obs-hyperhdr-flatbuffer

OBS Studio plugin that sends video to HyperHDR as RGB frames over the HyperHDR FlatBuffers protocol. It provides a main output (program canvas) and a per-source filter on one shared core.

Status: main output and filter implemented. Live-tested on macOS against a mock HyperHDR server: Register/Image/Clear framing, throttling, reconnect with backoff, duplicate-priority warning, and Clear on stop/exit all verified. Not yet tested against a real HyperHDR with LEDs. Linux and Windows builds not yet verified.

## Use

- **Main output (program canvas):** Tools → HyperHDR opens the dialog. The enable checkbox and all parameters (origin, priority, size, max FPS, flip, endpoint) persist in `hyperhdr.json` in the module config folder. The dialog shows the live sink state and last error. Apply restarts a running output.
- **Filter (one source):** add the "HyperHDR (FlatBuffer)" filter to any source or scene. Origin, priority, size, max FPS, flip and keep-aspect are set per instance in the filter properties. The capture also runs when the source is not on the active scene.

## Install (dev build)

Every push to `main` replaces the rolling [dev release](https://github.com/gllmAR/obs-hyperhdr-flatbuffer/releases/tag/dev).

- Windows: run `obs-hyperhdr-windows-x64-setup.exe`. It asks for administrator rights and installs to `%ProgramData%\obs-studio\plugins\obs-hyperhdr\`, the folder OBS scans on Windows.
- macOS: open `obs-hyperhdr-macos-universal.pkg`. It installs into `~/Library/Application Support/obs-studio/plugins/` for the logged-in user.
- Linux: extract `obs-hyperhdr-linux-x86_64.tar.gz` into `~/.config/obs-studio/plugins/`

Manual alternatives:

- Windows: extract `obs-hyperhdr-windows-x64.zip` into `%ProgramData%\obs-studio\plugins\`
- macOS: unzip `obs-hyperhdr-macos-universal.zip` into `~/Library/Application Support/obs-studio/plugins/`, then run:

```
xattr -dr com.apple.quarantine "$HOME/Library/Application Support/obs-studio/plugins/obs-hyperhdr.plugin"
```

The dev builds are unsigned. On macOS, Control-click the `.pkg` and choose Open the first time. Gatekeeper blocks a plain double-click.

## Develop

The protocol core, sink, and transport need no OBS install. Run their tests with the presets in `CMakePresets.json` (build output goes to `build/<preset>`):

- Linux: `cmake --preset core-linux && cmake --build --preset core-linux && ctest --preset core-linux` (needs Ninja)
- macOS: `brew install ninja`, then the same three commands with `core-macos`
- Windows (Visual Studio 2022): `cmake --preset core-windows`, `cmake --build --preset core-windows`, `ctest --preset core-windows`

CI runs the same presets on macOS (`core-macos`) and the equivalent commands on Linux and Windows. The plugin also needs `obs-frontend-api`: point `libobs_DIR` at `<obs-build>/libobs` and `obs-frontend-api_DIR` at `<obs-build>/frontend/api` (or add both to `CMAKE_PREFIX_PATH`) in a `CMakeUserPresets.json`.

- Documentation index: [docs/README.md](docs/README.md)
- Software design: [docs/SDD.md](docs/SDD.md)
- Plan: [docs/PLAN.md](docs/PLAN.md)

