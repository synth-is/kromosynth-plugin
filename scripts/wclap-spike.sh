#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Björn Þór Jónsson
# wclap-spike.sh — reproduces docs/PLUGIN_AS_HOST_SPIKE.md (synthis-aa4.23). NOT part of the gate:
# it needs the network and two artefacts built elsewhere.
#
#   WCLAP_SRC     a Surge XT WCLAP directory (module.wasm), default below
#   NATIVE_CLAP   a native Surge XT .clap bundle, default below
#   WORK          scratch dir (default /tmp/aa423)
#   ./scripts/wclap-spike.sh [routing|build|run|all]
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
ROOT="$PWD"
WORK="${WORK:-/tmp/aa423}"
WCLAP_SRC="${WCLAP_SRC:-$HOME/wclap-native/juce-webclap-plugins/build/surge-xt/web/surge-xt.wclap}"
NATIVE_CLAP="${NATIVE_CLAP:-$HOME/wclap-native/surge-native/build/surge_xt_products/Surge XT.clap}"
BRIDGE_REV=cd11d22afbe2af350f24cd56e6e0536e5ca86452
BINARYEN_URL=https://github.com/WebAssembly/binaryen/releases/download/version_133/binaryen-version_133-arm64-macos.tar.gz
mkdir -p "$WORK"

routing() {
  c++ -std=c++17 -Wall -Wextra tools/wclap-probe/routing_test.cpp -o "$WORK/routing_test"
  "$WORK/routing_test"
}

build() {
  if [[ ! -d "$WORK/wclap-bridge" ]]; then
    git clone -q --recursive https://github.com/WebCLAP/wclap-bridge "$WORK/wclap-bridge"
    git -C "$WORK/wclap-bridge" checkout -q "$BRIDGE_REV"
    git -C "$WORK/wclap-bridge" submodule update -q --init --recursive
    # Wasmtime refuses a module that uses wasm exceptions unless asked (see the spike note, finding 1).
    python3 - "$WORK/wclap-bridge/source/wclap-instance-wasmtime/wclap-instance-wasmtime.cpp" <<'PY'
import sys
p = sys.argv[1]; s = open(p).read()
k = '\tauto error = wasmtime_config_cache_config_load(config, nullptr);'
assert k in s
open(p, 'w').write(s.replace(k, '\twasmtime_config_wasm_exceptions_set(config, true);\n' + k, 1))
PY
  fi
  cmake -S "$WORK/wclap-bridge" -B "$WORK/wclap-bridge/build" -DCMAKE_BUILD_TYPE=Release >/dev/null
  cmake --build "$WORK/wclap-bridge/build" -j"$(sysctl -n hw.ncpu)" >/dev/null
  cmake -S tools/wclap-probe -B "$WORK/probe-build" -DCMAKE_BUILD_TYPE=Release -DWCLAP_BRIDGE_DIR="$WORK/wclap-bridge" >/dev/null
  cmake --build "$WORK/probe-build" >/dev/null
  if [[ ! -x "$WORK/bin/bin/wasm-opt" ]]; then
    mkdir -p "$WORK/bin"; curl -sL "$BINARYEN_URL" | tar xz -C "$WORK/bin" --strip-components=1
  fi
  # The Surge XT WCLAP is built with legacy wasm exceptions (-fwasm-exceptions); Wasmtime only
  # implements the exnref form. Translate a copy.
  rm -rf "$WORK/surge.wclap"; cp -R "$WCLAP_SRC" "$WORK/surge.wclap"
  "$WORK/bin/bin/wasm-opt" "$WCLAP_SRC/module.wasm" --translate-to-exnref \
    --enable-exception-handling --enable-simd --enable-bulk-memory --enable-sign-ext \
    --enable-nontrapping-float-to-int --enable-mutable-globals --enable-multivalue \
    --enable-reference-types --enable-threads --enable-gc -o "$WORK/surge.wclap/module.wasm"
}

run() {
  mkdir -p "$WORK/data/presets" "$WORK/data/cache" "$WORK/data/var"
  export WCLAP_PROBE_DATA="$WORK/data" WCLAP_PROBE_TIME_LIMIT_MS=0
  echo "── native Surge XT"; "$WORK/probe-build/wclap-probe" "$NATIVE_CLAP" || true
  # Creating the WCLAP plugin is FLAKY (about 40% succeed) and a second instance panics Wasmtime
  # ("object used with the wrong store"); retry until one run gets as far as it can.
  for i in 1 2 3 4 5 6; do
    echo "── WCLAP Surge XT, attempt $i"
    "$WORK/probe-build/wclap-probe" "$WORK/surge.wclap" >"$WORK/wclap-run.txt" 2>&1 && ok=0 || ok=$?
    grep -E "^(MODE|RMS|CPU_RT|CHECK_|STATE_|STEP_FAILED|FAIL)|panicked" "$WORK/wclap-run.txt" || true
    grep -q "^CHECK_PLAYS" "$WORK/wclap-run.txt" && break
  done
}

case "${1:-all}" in
  routing) routing ;;
  build) build ;;
  run) run ;;
  all) routing; build; run ;;
esac
