#!/usr/bin/env bash
# ios-build.sh — build the iOS AUv3 and its containing app.
#
# WHY THIS EXISTS
#
# The iOS build procedure was nowhere on disk. BUILD_STEPS.md is Stage 0 desktop
# only, build.sh has no iOS path, and the recipe had to be reconstructed from
# build/ios-auv3's CMake cache — six tool calls to recover something that gets
# done repeatedly. This is that reconstruction, written down.
#
# TOUCHES NO DEVICE. Deliberately a separate file from ios-install.sh, which
# does, and which can take an AU host down (AUV3_IOS_FINDINGS C15). Running this
# is always safe, and that separation is the reason why.
#
# ── What one target gets you ─────────────────────────────────────────────────
#
# KromosynthPlugin_Standalone, and that is not a shortcut. JUCE embeds the AUv3
# extension INSIDE the standalone app, at PlugIns/Synth.is.appex, so installing
# the .app delivers both surfaces. Verified rather than assumed. Building
# KromosynthPlugin_AUv3 on its own produces an appex with nothing to install it
# from.
#
# ── The build directory is already configured, and this does not reconfigure ─
#
# build/ios-auv3 carries generator Xcode, CMAKE_SYSTEM_NAME=iOS,
# CMAKE_OSX_SYSROOT=iphoneos, deployment target 16.0, DEVELOPMENT_TEAM
# ATA8MQ4L7K, KROMOSYNTH_WASM3=ON, both engines ON.
#
# If that cache is gone this script STOPS rather than guessing a configure line.
# The cache holds KROMOSYNTH_WASM3_SOURCE_DIR pointing into kromosynth-projects,
# and a guessed line would quietly produce a build without wasm3 — which on iOS
# cannot JIT and so cannot run at all. Better to fail loudly here.
#
# ── wasm3, not wasmtime, and it matters beyond the build ────────────────────
#
# iOS forbids JIT, so this build interprets. wasm3 runs 5.56x slower than
# Cranelift at the median, which is why runtimeId() must differ and why an rtf
# measured by this binary is not comparable with a desktop one. A voice cap
# derived from a wasmtime figure would be roughly five times too generous.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${KROMOSYNTH_IOS_BUILD_DIR:-$REPO_ROOT/build/ios-auv3}"
CONFIG="${KROMOSYNTH_IOS_CONFIG:-Release}"
TARGET="KromosynthPlugin_Standalone"

APP_REL="src/KromosynthPlugin_artefacts/$CONFIG/Standalone/Synth.is.app"
APP="$BUILD_DIR/$APP_REL"
APPEX="$APP/PlugIns/Synth.is.appex"

say()  { printf '%s\n' "$*"; }
fail() { printf 'FAIL  %s\n' "$*" >&2; exit 1; }

# ── Preflight ───────────────────────────────────────────────────────────────

[ -f "$BUILD_DIR/CMakeCache.txt" ] \
  || fail "no CMake cache at $BUILD_DIR — this script does not configure.
      That cache holds KROMOSYNTH_WASM3_SOURCE_DIR pointing into
      kromosynth-projects; a guessed configure line would silently build
      without wasm3. Configure by hand, then rerun."

# Guards against pointing this at a desktop build dir and discovering it
# twenty minutes later in a link error.
grep -q '^CMAKE_SYSTEM_NAME:UNINITIALIZED=iOS' "$BUILD_DIR/CMakeCache.txt" \
  || fail "$BUILD_DIR is not an iOS build directory."

if ! grep -q '^KROMOSYNTH_WASM3:BOOL=ON' "$BUILD_DIR/CMakeCache.txt"; then
  say "WARN  KROMOSYNTH_WASM3 is not ON in this cache."
  say "      iOS forbids JIT, so this build will not run on the device."
  say ""
fi

# ── Before and after, so "did my build actually happen" is answerable ───────
#
# C0's most expensive lesson is that a stale binary is indistinguishable from a
# fix that did not work. Three shell fixes appeared to do nothing and the
# diagnosis was wrong for an hour. Recording the binary's identity on both
# sides makes the question mechanical instead of a matter of belief.

before_mtime=""
[ -f "$APPEX/Synth.is" ] && before_mtime="$(stat -f '%m' "$APPEX/Synth.is" 2>/dev/null || true)"

say "Building $TARGET ($CONFIG)"
say "  $BUILD_DIR"
say ""

cmake --build "$BUILD_DIR" --config "$CONFIG" --target "$TARGET"

# ── Report ──────────────────────────────────────────────────────────────────

[ -d "$APP" ] || fail "build reported success but $APP_REL is missing."
[ -d "$APPEX" ] || fail "app built with no embedded appex at PlugIns/ —
      installing it would deliver no AUv3 at all."

after_mtime="$(stat -f '%m' "$APPEX/Synth.is")"

say ""
say "app    $APP_REL"
say "appex  PlugIns/Synth.is.appex"
say "built  $(date -r "$after_mtime" '+%Y-%m-%d %H:%M:%S')"

if [ -n "$before_mtime" ] && [ "$before_mtime" = "$after_mtime" ]; then
  say ""
  say "NOTE   the appex binary did NOT change — nothing it depends on was dirty."
  say "       If you expected a change, your edit is somewhere this target does"
  say "       not compile. Establish that BEFORE installing and concluding the"
  say "       fix had no effect on the device (C0)."
fi

say ""
say "Next   ./scripts/ios-install.sh"
