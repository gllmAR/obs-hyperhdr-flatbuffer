# Research 05: Build, CI, and licensing

Status: research complete. Two decisions need the project owner (license and OBS version pin). See the SDD open questions.

## 1. Repository state

- Remote: `https://github.com/gllmAR/obs-hyperhdr-flatbuffer` (`origin`, branch `main`).
- Workspace contents: `README.md` (stub), `docs/`, `inspiration/`. No CI configuration, no source tree, and no root `LICENSE` file in the workspace listing.

## 2. Build requirements

| Requirement | Version or source | Notes |
|---|---|---|
| CMake | 3.24 or later | Required for `find_package(libobs)` and generator expressions used by OBS plugin templates |
| C++ | C++17 | Core |
| libobs, obs-frontend-api | OBS 30.x or later; pin 31.0.0 (matches HIVE) | Built from obs-studio source or taken from the OBS dev packages (to verify per platform) |
| obs-deps | Date-tagged prebuilt packages from the `obsproject/obs-deps` releases (HIVE pins 2025-08-23 on macOS) | Needed to build libobs on macOS and Windows |
| Qt6 Widgets | Optional (`HYPERHDR_QT_UI`, default ON) | Only for the Tools dialog. OBS ships Qt6 |
| Python 3 with `flatbuffers` | Test-only | Golden byte tests and mock server |

## 3. Platform matrix

| Platform | Architecture | Transport | Compiler | Plugin file |
|---|---|---|---|---|
| Linux (Ubuntu 24.04) | x86_64 | Domain socket or TCP | GCC 13 or Clang 18 | `obs-hyperhdr.so` in `bin/64bit/` |
| Windows 10/11 | x64 | TCP only in v1 | MSVC 2022 | `obs-hyperhdr.dll` in `bin/64bit/` |
| macOS 14+ | arm64 (universal optional) | Domain socket (`TMPDIR`) or TCP | Apple Clang | `obs-hyperhdr.plugin` bundle |

Notes:

- HIVE's Windows build uses a Docker cross image with clang-cl and xwin. This project prefers a native MSVC runner in GitHub Actions. The cross image is kept as a fallback.
- HIVE's macOS setup script only supports Apple Silicon. This project matches that for v1.

## 4. CI design (GitHub Actions)

Proposed workflow `.github/workflows/ci.yml`, triggered on push and pull request.

| Job | Runner | Steps | Gate |
|---|---|---|---|
| `core-test` | ubuntu-24.04 | Build core, golden byte tests (Python `flatbuffers`), mock HyperHDR tests, ThreadSanitizer run | Must pass |
| `build-obs` | matrix: ubuntu-24.04, windows-2022, macos-14 | Restore libobs and obs-deps cache, build libobs (pinned), build plugin, verify exported `obs_module_load` symbol, package artifact | Must pass |
| `package` | on tags only | Assemble zip or tar per OS, write SHA-256 sums, attach to release | Tag builds only |

Caching:

- Key the libobs build on OS plus OBS tag plus obs-deps date. A cold libobs build is several minutes, so the cache is required.

Not in CI (manual or self-hosted):

- Live HyperHDR smoke test (needs a HyperHDR instance).
- Loading the plugin in a running OBS and checking LEDs.
- Quit-with-scripts deadlock test (needs OBS with Lua scripting).

## 5. Licensing analysis

| Component | License | Constraint |
|---|---|---|
| libobs, obs-frontend-api | GPL-2.0-or-later | Plugins that link libobs must be GPL-compatible. This project's plugin must be GPL-2.0-or-later (or GPL-3.0-or-later). |
| ofxhyperhdr source | AGPL-3.0-or-later (header: Guillaume Arseneault) | AGPL-3.0 is not compatible with GPL-2.0-only. Combining it into the plugin needs the owner's relicense or a clean-room rewrite. |
| HyperHDR schema | MIT (awawa-dev) | Compatible. Keep copyright and license notice in `NOTICE` or `THIRD_PARTY`. |
| HIVE `obs-hive` | GPL-2.0-or-later | Reference only. Do not copy code into this project without matching license terms. |
| Qt6 | LGPL-3.0 (dynamic linking) | Acceptable. Link dynamically. |
| FlatBuffers runtime | Apache-2.0 | Avoided. Incompatible with GPL-2.0-only. |

Recommendations:

1. Set the project license to GPL-2.0-or-later. This matches libobs and HIVE's plugin.
2. For the protocol code, choose one of:
   - (a) Clean-room rewrite from the MIT schema, validated against the Python builder (what HIVE did). Requires no permission.
   - (b) Relicense the ofxhyperhdr protocol files to GPL-2.0-or-later. This is viable if the owner of the ofxhyperhdr copyright is the project owner. Confirm before using.
3. Record the chosen approach in the SDD decision log.

## 6. Packaging for OBS

- Linux: `<plugins>/obs-hyperhdr/bin/64bit/obs-hyperhdr.so` plus `obs-hyperhdr/data/`. Install path `~/.config/obs-studio/plugins`.
- Windows: `obs-hyperhdr/bin/64bit/obs-hyperhdr.dll` plus `data/`. Install path `%APPDATA%/obs-studio/plugins`.
- macOS: `obs-hyperhdr.plugin` bundle. Install path `~/Library/Application Support/obs-studio/plugins`. Distribution outside the developer machine needs Developer ID signing and notarization (not in v1).

## 7. Open items

- Confirm the libobs source of truth per platform (source build vs OBS development packages). Spike in Phase 2.
- Pin the OBS version. Default proposal: 31.0.0, matching HIVE.
- Confirm the license decision with the owner (see SDD Q-1).
