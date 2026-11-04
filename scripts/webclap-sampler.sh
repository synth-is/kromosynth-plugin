#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# synthis-et4.18: build and measure the sampler spike WCLAP (SamplerEngine on sfizz, no threads, compiled into
# module.wasm, one generated test SFZ + WAV embedded). NOT part of the gate (the gate must not need an emsdk).
#
#   scripts/webclap-sampler.sh build    generate the test data, emcmake-configure webclap/emscripten with
#                                       SYNTHIS_WEBCLAP_PROCESSOR=sampler, build
#   scripts/webclap-sampler.sh check    render keys 69 and 81 in wclap-host-node, compare with the source sample
#   scripts/webclap-sampler.sh voices   real-time factor for 1..1024 simultaneous voices, and linear memory
#   scripts/webclap-sampler.sh native   render the same keys through NATIVE sfizz (scripts/webclap-sampler-native.cpp
#                                       linked against the gate build's libraries, build/verify-nocopy, which must
#                                       exist: run scripts/build.sh first); check then also compares with these
#   scripts/webclap-sampler.sh          build, check
#
# Env: as scripts/webclap-skeleton.sh (SYNTHIS_WEBCLAP_KIT, SYNTHIS_WEBCLAP_EMSDK, SYNTHIS_WEBCLAP_EXNREF,
#      WCLAP_HOST_NODE), plus SYNTHIS_WEBCLAP_BUILD_DIR (default build/webclap-sampler).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
KIT="${SYNTHIS_WEBCLAP_KIT:-$HOME/wclap-native/open-webclap}"
EMSDK="${SYNTHIS_WEBCLAP_EMSDK:-$HOME/wclap-native/emsdk}"
BUILD="${SYNTHIS_WEBCLAP_BUILD_DIR:-$ROOT/build/webclap-sampler}"
BUNDLE="$BUILD/web/synthis-sampler.wclap"
DATA="$BUILD/data"
HOSTNODE="${WCLAP_HOST_NODE:-$HOME/Developer/apps/synth.is/kromosynth-burst/packages/wclap-host-node}"
mode="${1:-all}"

build() {
  node "$ROOT/scripts/webclap-sampler-data.mjs" "$DATA"
  # shellcheck disable=SC1091
  set +u; source "$EMSDK/emsdk_env.sh" >/dev/null 2>&1; set -u
  local extra=()
  [[ "${SYNTHIS_WEBCLAP_EXNREF:-OFF}" == "ON" ]] && extra+=(-DWEBCLAP_EXNREF=ON)
  emcmake cmake -S "$ROOT/webclap/emscripten" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release \
    -DSYNTHIS_WEBCLAP_KIT="$KIT" -DSYNTHIS_WEBCLAP_PROCESSOR=sampler -DSYNTHIS_WEBCLAP_SAMPLER_DATA="$DATA" \
    ${extra[@]+"${extra[@]}"}
  cmake --build "$BUILD" --target module -j "$(sysctl -n hw.ncpu 2>/dev/null || nproc)"
  ls -l "$BUNDLE/module.wasm"
}

native() {
  local nb="${KROMOSYNTH_NATIVE_BUILD:-$ROOT/build/verify-nocopy}" link
  [[ -f "$nb/library/lib/libsfizz.a" && -f "$nb/tools/sfz-probe/CMakeFiles/sfz_probe.dir/link.txt" ]] || {
    echo "no native sfizz build in $nb (scripts/build.sh makes it)"; exit 2; }
  # The library list sfz-probe links with, in its order.
  link="$(grep -oE 'library/lib/[A-Za-z0-9_]+\.a|-framework [A-Za-z]+|-lm' "$nb/tools/sfz-probe/CMakeFiles/sfz_probe.dir/link.txt" |
          sed -E "s#^library/#$nb/library/#" | tr '\n' ' ')"
  # shellcheck disable=SC2086
  c++ -std=c++20 -O3 -I"$ROOT/third_party/sfizz/src" "$ROOT/scripts/webclap-sampler-native.cpp" $link -o "$BUILD/sampler-native"
  local k
  for k in "69 52800" "81 28800" "57 100800"; do
    set -- $k
    "$BUILD/sampler-native" "$DATA/test.sfz" "$1" "$2" "$BUILD/native-key$1.f32"
  done
  for k in 1 8 64; do "$BUILD/sampler-native" "$DATA/test.sfz" time "$k" 2>/dev/null; done
}

check() {
  [[ -d "$HOSTNODE/src" ]] || { echo "wclap-host-node not found at $HOSTNODE (set WCLAP_HOST_NODE)"; exit 2; }
  node "$ROOT/scripts/webclap-sampler-check.mjs" "$HOSTNODE/src" "$BUNDLE" "$DATA/tone.wav" "$1" "$BUILD"
}

case "$mode" in
  build)  build ;;
  check)  check check ;;
  voices) check voices ;;
  native) native ;;
  all)    build; check check ;;
  *) echo "usage: $0 [build|check|voices|native|all]"; exit 2 ;;
esac
