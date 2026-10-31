#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Björn Þór Jónsson
# wclap-exnref-build.sh — builds the Surge XT WCLAP with exnref exceptions (synthis-aa4.52), the form Wasmtime
# loads. Reproduces docs/PLUGIN_AS_HOST_SPIKE.md section 6. NOT part of the gate (network, ~10 min, 20 GB disk).
#
# Emscripten 4.0.20 goes into ~/wclap-native/emsdk-4, BESIDE the 3.1.51 emsdk in ~/wclap-native/emsdk, which
# the other build recipes (OB-Xf, Odin 2, ...) still use. The flag change is a patch to the
# juce-webclap-plugins clone (a separate repository); this script applies it if it is not applied yet.
# The built bundle is GPL-3.0: do not commit it.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
KIT="${KIT:-$HOME/wclap-native/juce-webclap-plugins}"
EMSDK4="${EMSDK4:-$HOME/wclap-native/emsdk-4}"
EMSCRIPTEN_VERSION="${EMSCRIPTEN_VERSION:-4.0.20}"
PATCH="$PWD/scripts/wclap-exnref/juce-webclap-plugins-exnref.patch"

if [[ ! -d "$EMSDK4" ]]; then
  git clone -q --depth 1 https://github.com/emscripten-core/emsdk.git "$EMSDK4"
fi
"$EMSDK4/emsdk" install "$EMSCRIPTEN_VERSION"
"$EMSDK4/emsdk" activate "$EMSCRIPTEN_VERSION"

if git -C "$KIT" apply --check "$PATCH" 2>/dev/null; then
  git -C "$KIT" apply "$PATCH"
elif ! git -C "$KIT" apply --check -R "$PATCH" 2>/dev/null; then
  echo "the patch neither applies nor is already applied to $KIT"; exit 1
fi

# Objects from the 3.1.51 (legacy exceptions) build cannot be linked with exnref ones: build in a fresh directory.
export SURGE_BUILD_DIR="${SURGE_BUILD_DIR:-$KIT/build/surge-xt}"
[[ -e "$SURGE_BUILD_DIR/CMakeCache.txt" ]] && grep -q "emsdk/upstream" "$SURGE_BUILD_DIR/CMakeCache.txt" \
  && { echo "$SURGE_BUILD_DIR was configured with the 3.1.51 emsdk: move it aside first"; exit 1; }
( source "$EMSDK4/emsdk_env.sh" >/dev/null; bash "$KIT/scripts/build-surge-xt.sh" )

# Verify: try_table present, no legacy try
WASM="$SURGE_BUILD_DIR/web/surge-xt.wclap/module.wasm"
"$EMSDK4/upstream/bin/wasm-dis" --all-features "$WASM" -o /tmp/surge-module.wat
n=$(grep -c try_table /tmp/surge-module.wat || true); l=$(grep -cE '\(try( |$)' /tmp/surge-module.wat || true)
echo "try_table=$n legacy_try=$l"
(( n > 0 && l == 0 )) || { echo "not an exnref module"; exit 1; }
