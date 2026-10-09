# obs-hyperhdr-flatbuffer

OBS Studio plugin that sends video to HyperHDR as RGB frames over the HyperHDR FlatBuffers protocol. It provides a main output (program canvas) and a per-source filter on one shared core.

Status: skeleton. Built against libobs 32.2.2 and verified to load on Linux; macOS and Windows CI builds are not yet verified. No video output yet.

## Install (dev build)

Every push to `main` replaces the rolling [dev release](https://github.com/gllmAR/obs-hyperhdr-flatbuffer/releases/tag/dev).

- Linux: extract `obs-hyperhdr-linux-x86_64.tar.gz` into `~/.config/obs-studio/plugins/`
- Windows: extract `obs-hyperhdr-windows-x64.zip` into `%APPDATA%\obs-studio\plugins\`
- macOS: unzip `obs-hyperhdr-macos-universal.zip` into `~/Library/Application Support/obs-studio/plugins/`, then run:

```
xattr -dr com.apple.quarantine "$HOME/Library/Application Support/obs-studio/plugins/obs-hyperhdr.plugin"
```

The dev builds are unsigned.

- Documentation index: [docs/README.md](docs/README.md)
- Software design: [docs/SDD.md](docs/SDD.md)
- Plan: [docs/PLAN.md](docs/PLAN.md)

