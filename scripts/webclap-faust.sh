#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# synthis-et4.17: build and measure the Faust spike WCLAP (FaustEngine on wasm3, compiled into module.wasm, one
# embedded Faust artefact). NOT part of the gate (the gate must not need an emsdk).
#
#   scripts/webclap-faust.sh build    emcmake-configure webclap/emscripten with SYNTHIS_WEBCLAP_PROCESSOR=faust, build
#   scripts/webclap-faust.sh null     replay the artefact's plan in wclap-host-node, compare with the platform renders
#   scripts/webclap-faust.sh note     play key 60 through the voice path (writes <build>/note60.wav)
#   scripts/webclap-faust.sh voices   real-time factor for 1..8 simultaneous voices
#   scripts/webclap-faust.sh measure  FaustEngine::measure() inside the module (single-voice RTF, peaks)
#   scripts/webclap-faust.sh          build, null, note
#
# Env: as scripts/webclap-skeleton.sh (SYNTHIS_WEBCLAP_KIT, SYNTHIS_WEBCLAP_EMSDK, SYNTHIS_WEBCLAP_EXNREF,
#      WCLAP_HOST_NODE), plus SYNTHIS_WEBCLAP_BUILD_DIR (default build/webclap-faust), SYNTHIS_WASM3_TAIL_CALL=ON|OFF,
#      and SYNTHIS_FAUST_ARTEFACT, a path without extension to <stem>.wasm/.json/.plan (default: Gate 0 elite
#      elite_08_deep_pluck_c23-12 in the kromosynth-projects sibling, the artefacts scripts/null-test.sh uses).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
KIT="${SYNTHIS_WEBCLAP_KIT:-$HOME/wclap-native/open-webclap}"
EMSDK="${SYNTHIS_WEBCLAP_EMSDK:-$HOME/wclap-native/emsdk}"
BUILD="${SYNTHIS_WEBCLAP_BUILD_DIR:-$ROOT/build/webclap-faust}"
BUNDLE="$BUILD/web/synthis-faust.wclap"
HOSTNODE="${WCLAP_HOST_NODE:-$HOME/Developer/apps/synth.is/kromosynth-burst/packages/wclap-host-node}"
GATE0="${KROMOSYNTH_GATE0_ARTEFACTS:-$ROOT/../kromosynth-projects/Evolutionary Sound Discovery Platform/faust-genome-substrate/probe/out/gate0}"
ARTEFACT="${SYNTHIS_FAUST_ARTEFACT:-$GATE0/elite_08_deep_pluck_c23-12}"
mode="${1:-all}"

build() {
  # shellcheck disable=SC1091
  set +u; source "$EMSDK/emsdk_env.sh" >/dev/null 2>&1; set -u
  local extra=()
  [[ "${SYNTHIS_WEBCLAP_EXNREF:-OFF}" == "ON" ]] && extra+=(-DWEBCLAP_EXNREF=ON)
  emcmake cmake -S "$ROOT/webclap/emscripten" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release \
    -DSYNTHIS_WEBCLAP_KIT="$KIT" -DSYNTHIS_WEBCLAP_PROCESSOR=faust -DSYNTHIS_WEBCLAP_FAUST_ARTEFACT="$ARTEFACT" \
    -DSYNTHIS_WASM3_TAIL_CALL="${SYNTHIS_WASM3_TAIL_CALL:-ON}" ${extra[@]+"${extra[@]}"}
  cmake --build "$BUILD" --target module -j "$(sysctl -n hw.ncpu 2>/dev/null || nproc)"
  ls -l "$BUNDLE/module.wasm"
}

check() {
  [[ -d "$HOSTNODE/src" ]] || { echo "wclap-host-node not found at $HOSTNODE (set WCLAP_HOST_NODE)"; exit 2; }
  node "$ROOT/scripts/webclap-faust-check.mjs" "$HOSTNODE/src" "$BUNDLE" "$ARTEFACT" "$1" ${2+"$2"} ${3+"$3"}
}

case "$mode" in
  build)  build ;;
  null)   check null ;;
  note)   check note --out "$BUILD/note60.wav" ;;
  voices) check voices ;;
  measure) check measure ;;
  all)    build; check null; check note --out "$BUILD/note60.wav" ;;
  *) echo "usage: $0 [build|null|note|voices|measure|all]"; exit 2 ;;
esac
