#!/usr/bin/env bash
#
# setup.sh — one-shot clone-and-run for the FxSound macOS port.
# Checks dependencies, builds a Release universal binary, and launches the app.
#
#   git clone https://github.com/okku007/fxsound-macos.git
#   cd fxsound-macos
#   ./setup.sh
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ="$REPO_ROOT/FxSoundMac"
BUILD="$PROJ/build"
APP="$BUILD/FxSoundMac_artefacts/Release/FxSound.app"

say()  { printf "\033[1;36m==>\033[0m %s\n" "$*"; }
warn() { printf "\033[1;33m!!\033[0m %s\n" "$*"; }
die()  { printf "\033[1;31mERROR:\033[0m %s\n" "$*" >&2; exit 1; }

[[ "$(uname)" == "Darwin" ]] || die "macOS only."

# 1. Xcode Command Line Tools (clang + the Xcode CMake generator)
if ! xcode-select -p >/dev/null 2>&1; then
  warn "Xcode Command Line Tools not found. Launching the installer..."
  xcode-select --install || true
  die "Re-run ./setup.sh after the Command Line Tools finish installing."
fi

# 2. Homebrew (used to install cmake + BlackHole if missing)
HAS_BREW=1
command -v brew >/dev/null 2>&1 || HAS_BREW=0

# 3. CMake
if ! command -v cmake >/dev/null 2>&1; then
  if [[ "$HAS_BREW" == 1 ]]; then
    say "Installing cmake via Homebrew..."
    brew install cmake
  else
    die "cmake not found and Homebrew is not installed. Install Homebrew (https://brew.sh) or cmake manually, then re-run."
  fi
fi

# 4. BlackHole 2ch (virtual device FxSound captures system audio from)
if ! (brew list --cask blackhole-2ch >/dev/null 2>&1 || \
      system_profiler SPAudioDataType 2>/dev/null | grep -qi "BlackHole"); then
  if [[ "$HAS_BREW" == 1 ]]; then
    say "Installing BlackHole 2ch (you'll be asked for your password)..."
    brew install --cask blackhole-2ch
  else
    warn "BlackHole 2ch not detected and Homebrew is missing."
    warn "Install it from https://existential.audio/blackhole/ then re-run."
  fi
fi

# 5. Configure (fetches JUCE 7.0.12 on first run) + build Release
say "Configuring (first run downloads JUCE ~1-2 min)..."
cmake -B "$BUILD" -S "$PROJ" -G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"

say "Building Release universal binary (first run ~2-4 min)..."
cmake --build "$BUILD" --config Release --target FxSoundMac

[[ -d "$APP" ]] || die "Build finished but $APP is missing."

say "Done. Launching FxSound."
open "$APP"

cat <<EOF

------------------------------------------------------------
FxSound is built and running (locally code-signed — no
Gatekeeper warning, since you built it yourself).

To use it:
  1. In FxSound: pick your real output device, click Start.
  2. System Settings > Sound > Output: choose "BlackHole 2ch".
     FxSound enhances that audio and plays it on the device
     you selected in step 1.

App bundle: $APP
Rebuild anytime with: ./setup.sh
------------------------------------------------------------
EOF
