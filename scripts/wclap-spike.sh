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
# The exnref build (wclap-exnref-build.sh) writes here; a legacy-exception build will not load.
WCLAP_SRC="${WCLAP_SRC:-$HOME/wclap-native/juce-webclap-plugins/build/surge-xt/web/surge-xt.wclap}"
NATIVE_CLAP="${NATIVE_CLAP:-$HOME/wclap-native/surge-native/build/surge_xt_products/Surge XT.clap}"
BRIDGE_REV=cd11d22afbe2af350f24cd56e6e0536e5ca86452
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
  # wclap-bridge cd11d22 never initialises WclapModuleBase::hostTemplate, so host->clap_version handed to
  # the guest's create_plugin is heap garbage; the JUCE WCLAP wrapper returns null unless
  # clap_version_is_compatible() (major >= 1). That was the "flaky creation" (synthis-aa4.40). Idempotent.
  python3 - "$WORK/wclap-bridge/source/_generic" <<'PY'
import sys
d = sys.argv[1]
p = d + '/wclap-module-base.h'; s = open(p).read()
if 'wclap_host hostTemplate{};' not in s:
    assert '\twclap_host hostTemplate;\n' in s
    open(p, 'w').write(s.replace('\twclap_host hostTemplate;\n', '\twclap_host hostTemplate{};\n', 1))
p = d + '/wclap-module.h'; s = open(p).read()
k = '\t\tif (hasError) return; // base class failed\n'
if 'hostTemplate.clap_version' not in s:
    assert k in s
    open(p, 'w').write(s.replace(k, k + '\t\thostTemplate.clap_version = {CLAP_VERSION_MAJOR, CLAP_VERSION_MINOR, CLAP_VERSION_REVISION};\n', 1))
PY
  cmake -S "$WORK/wclap-bridge" -B "$WORK/wclap-bridge/build" -DCMAKE_BUILD_TYPE=Release >/dev/null
  cmake --build "$WORK/wclap-bridge/build" -j"$(sysctl -n hw.ncpu)" >/dev/null
  cmake -S tools/wclap-probe -B "$WORK/probe-build" -DCMAKE_BUILD_TYPE=Release -DWCLAP_BRIDGE_DIR="$WORK/wclap-bridge" >/dev/null
  cmake --build "$WORK/probe-build" >/dev/null
  # The WCLAP must be built with exnref exceptions (scripts/wclap-exnref-build.sh, synthis-aa4.52): Wasmtime loads
  # nothing else, so there is no wasm-opt translation step any more.
  rm -rf "$WORK/surge.wclap"; cp -R "$WCLAP_SRC" "$WORK/surge.wclap"
}

run() {
  mkdir -p "$WORK/data/presets" "$WORK/data/cache" "$WORK/data/var"
  export WCLAP_PROBE_DATA="$WORK/data" WCLAP_PROBE_TIME_LIMIT_MS=0
  echo "── native Surge XT"; "$WORK/probe-build/wclap-probe" "$NATIVE_CLAP" || true
  # With the build() patches creation succeeds every time; the retry is kept only as a guard
  # (before the hostTemplate patch about 1 run in 4 to 12 got through).
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
