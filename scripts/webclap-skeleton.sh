#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# synthis-et4.9: build and prove the skeleton WCLAP. NOT part of the gate (the gate must not need an emsdk).
#
#   scripts/webclap-skeleton.sh build   emcmake-configure webclap/emscripten, build module.wasm, write the bundle
#   scripts/webclap-skeleton.sh test    load the bundle in kromosynth-burst's wclap-host-node, play a note, check RMS
#   scripts/webclap-skeleton.sh         both
#
# Env: SYNTHIS_WEBCLAP_KIT (open-webclap checkout), SYNTHIS_WEBCLAP_EMSDK, SYNTHIS_WEBCLAP_BUILD_DIR,
#      SYNTHIS_WEBCLAP_EXNREF=ON|OFF, WCLAP_HOST_NODE (path to packages/wclap-host-node).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
KIT="${SYNTHIS_WEBCLAP_KIT:-$HOME/wclap-native/open-webclap}"
EMSDK="${SYNTHIS_WEBCLAP_EMSDK:-$HOME/wclap-native/emsdk}"
BUILD="${SYNTHIS_WEBCLAP_BUILD_DIR:-$ROOT/build/webclap}"
BUNDLE="$BUILD/web/synthis.wclap"
HOSTNODE="${WCLAP_HOST_NODE:-$HOME/Developer/apps/synth.is/kromosynth-burst/packages/wclap-host-node}"
mode="${1:-all}"

build() {
  # shellcheck disable=SC1091
  set +u; source "$EMSDK/emsdk_env.sh" >/dev/null 2>&1; set -u
  local extra=()
  [[ "${SYNTHIS_WEBCLAP_EXNREF:-OFF}" == "ON" ]] && extra+=(-DWEBCLAP_EXNREF=ON)
  emcmake cmake -S "$ROOT/webclap/emscripten" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release \
    -DSYNTHIS_WEBCLAP_KIT="$KIT" ${extra[@]+"${extra[@]}"}
  cmake --build "$BUILD" --target module -j "$(sysctl -n hw.ncpu 2>/dev/null || nproc)"
  ls -l "$BUNDLE/module.wasm"
}

test_bundle() {
  [[ -d "$HOSTNODE/src" ]] || { echo "wclap-host-node not found at $HOSTNODE (set WCLAP_HOST_NODE)"; exit 2; }
  node --input-type=module -e "
import { WclapHost, loadBundle } from '$HOSTNODE/src/index.mjs';
const host = await WclapHost.load(loadBundle('$BUNDLE').wasm);
console.log('plugins:', JSON.stringify(host.plugins));
const p = host.createPlugin();
p.activate({ sampleRate: 48000, blockSize: 512 });
console.log('params:', p.params.map(x => x.name).join(','));
const [l] = p.renderNote({ key: 69, velocity: 0.8, noteFrames: 24000, tailFrames: 4800 });
let s = 0; for (let i = 0; i < 24000; i++) s += l[i] * l[i];
const rms = Math.sqrt(s / 24000);
console.log('RMS over the held note:', rms.toFixed(4));
if (!(rms > 0.05)) { console.error('FAIL: RMS not above 0.05'); process.exit(1); }
console.log('ok');
"
}

case "$mode" in
  build) build ;;
  test)  test_bundle ;;
  all)   build; test_bundle ;;
  *) echo "usage: $0 [build|test|all]"; exit 2 ;;
esac
