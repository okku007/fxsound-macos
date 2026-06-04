#!/usr/bin/env bash
#
# package_release.sh — build a Release FxSound.app and zip it for Homebrew-cask
# distribution. Prints the version, sha256 and download URL you paste into the
# cask (packaging/homebrew/Casks/fxsound.rb) and the gh command to publish.
#
# Usage:
#   FxSoundMac/scripts/package_release.sh 1.0.0
#
# Output: FxSoundMac/dist/FxSound-<version>.zip
#
set -euo pipefail

VERSION="${1:?usage: package_release.sh <version>   e.g. 1.0.0}"

# Resolve repo paths relative to this script (works from any CWD).
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MAC_DIR="$(dirname "$SCRIPT_DIR")"            # .../FxSoundMac
BUILD_DIR="$MAC_DIR/build"
DIST_DIR="$MAC_DIR/dist"
APP="$BUILD_DIR/FxSoundMac_artefacts/Release/FxSound.app"
ZIP="$DIST_DIR/FxSound-$VERSION.zip"

echo "==> Configuring (if needed) + building Release universal binary"
if [[ ! -d "$BUILD_DIR" ]]; then
  cmake -B "$BUILD_DIR" -S "$MAC_DIR" -G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
fi
cmake --build "$BUILD_DIR" --config Release --target FxSoundMac

[[ -d "$APP" ]] || { echo "ERROR: $APP not found after build"; exit 1; }

echo "==> Verifying code signature (ad-hoc is expected)"
codesign --verify --deep --strict "$APP" || echo "WARN: codesign verify reported issues"

echo "==> Zipping app bundle (ditto preserves symlinks + signature)"
mkdir -p "$DIST_DIR"
rm -f "$ZIP"
/usr/bin/ditto -c -k --keepParent "$APP" "$ZIP"

SHA="$(shasum -a 256 "$ZIP" | awk '{print $1}')"
URL="https://github.com/okku007/fxsound-mac/releases/download/v$VERSION/FxSound-$VERSION.zip"

cat <<EOF

============================================================
  Built:   $ZIP
  Size:    $(du -h "$ZIP" | awk '{print $1}')
  version  "$VERSION"
  sha256   "$SHA"
  url      "$URL"
============================================================

Next steps:

  1) Publish the GitHub release (uploads the zip as an asset):
       gh release create "v$VERSION" "$ZIP" \\
         --repo okku007/fxsound-mac \\
         --title "FxSound $VERSION (macOS)" \\
         --notes "macOS build. Requires BlackHole 2ch (auto-installed by the cask)."

  2) Update the cask in your tap with the version + sha256 above, then push:
       packaging/homebrew/Casks/fxsound.rb

  3) Users install with:
       brew install --cask okku007/fxsound/fxsound
============================================================
EOF
