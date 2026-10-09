#!/bin/bash
# Builds the unsigned macOS installer package from the staged plugin bundle.
# Usage (from anywhere): installer/macos/build-pkg.sh <stage-dir> <output.pkg>
set -euo pipefail

cd "$(dirname "$0")/../.."
stage=${1:-stage}
out=${2:-dist/obs-hyperhdr-macos-universal.pkg}
version=$(sed -n 's/^project(obs-hyperhdr VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)

# The payload lands in a staging folder; postinstall moves it into the user's OBS plugin folder.
pkgbuild \
  --root "$stage" \
  --identifier com.gllmar.obs-hyperhdr \
  --version "$version" \
  --install-location "/Library/Application Support/obs-hyperhdr" \
  --scripts installer/macos/scripts \
  "$out"
