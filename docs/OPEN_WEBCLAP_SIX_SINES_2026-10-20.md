# Six Sines from the open-webclap kit, legacy and exnref (synthis-aa4.90, 2026-10-20)

We build from source; nothing here depends on openDAW's hosted bundles. The bundles are GPL-3.0 as built and are
**not committed**. Everything was built on the Mac Studio under `~/wclap-native/open-webclap` (outside git).

## 1. Pins

| What | Pin |
|---|---|
| open-webclap (github.com/andremichelle/open-webclap, MIT) | `0833f5b` ("README: a screenshot of OB-Xf on the site") |
| emsdk legacy | `~/wclap-native/emsdk`, Emscripten 3.1.51 (the kit's tested version) |
| emsdk exnref | `~/wclap-native/emsdk-4`, Emscripten 4.0.20 (`6913738e`) |
| Six Sines | baconpaul/six-sines `03f3b9af04511705148bca654a57f68742b5fb08` |
| sst-jucegui | `3aeed6ea9508e7aa2b12cf33c1f6252be7ec22b6` |
| sst-basic-blocks | `d9c4ce25a739afa8b8e850a8b973a5a912c34ea7` |
| sst-cpputils | `df02974f3d1a0da9f1caeb0cd65c944d068613a8` |
| sst-filters | `abef77ff1c32fe65260d84c3f8a3e637474213fc` |
| sst-voicemanager | `c7dec29113ff9391af2029246208c46b85693164` |
| sst-plugininfra | `783b28d35d47176badd3ce34453ce27b99591f3d` (+ miniz `16413c213de38e703d883006193734e8b1178d5d`) |
| sst-clap-helpers | `f17567a8166a50bfb50ddc9acca7e77c72e6096f` |
| clap | `29ffcc273be7c7c651f6c9953b99e69700e2387a` |
| clap-helpers | `a61bcdf0ecc2c8db1e80bfe8bf9cb7e8d9fd2bbc` |
| fmt | `407c905e45ad75fc29bf0f9bb7c5c2fd3475976f` |
| libsamplerate | `15c392d47e71b9395a759544b3818a1235fe1a1d` |
| pffft | `d027f2a290ca0047d95513c2a20570d13557e381` |
| simde (Six Sines' own) | `60a3a24462d5c074f669ed8b39c85f5b1128108f` |
| MTS-ESP (Six Sines') | `f214739b8832e7f297cb9970d0c0efbf783f1462` |
| JUCE (shared, newer than Six Sines' own 8.0.10 pin) | `a8c7c714c7564b9248749e1e8cefcc5ef2b6f0cc` |

These are the `scripts/fetch-deps.py` pins at 0833f5b, confirmed against the `.fetched` stamps in `external/`.
`fetch-deps.py` also applies `patches/juce-8-wasm.patch` and `patches/six-sines-no-threads.patch`.

## 2. Commands

```sh
cd ~/wclap-native/open-webclap
# legacy (emsdk 3.1.51 is found by build-six-sines.sh via $EMSDK / ~/emsdk, or source it first)
source ~/wclap-native/emsdk/emsdk_env.sh
WEBCLAP_BUILD_DIR=$PWD/build/six-sines bash scripts/build-six-sines.sh
# exnref (needs the patch of section 4, which adds -DWEBCLAP_EXNREF=ON and WEBCLAP_BUILD_DIR)
source ~/wclap-native/emsdk-4/emsdk_env.sh
WEBCLAP_BUILD_DIR=$PWD/build/six-sines-exnref bash scripts/build-six-sines.sh -DWEBCLAP_EXNREF=ON
```

A clean exnref build took 51 s. Objects from the two toolchains cannot be linked together: keep separate build dirs.
Output: `build/<dir>/web/six-sines.wclap` (module.wasm + ui/), `build/<dir>/six-sines.wclap.tar.gz` (6.9 MB, 17 files).

Exception form, by `wasm-dis --all-features` on module.wasm:

| bundle | `try_table` | legacy `(try` |
|---|---|---|
| legacy (3.1.51) | 0 | 3093 |
| exnref (4.0.20) | 2949 | 0 |

## 3. Headless render through the Node host (kromosynth-burst `packages/wclap-host-node`)

Script: `scripts/wclap-six-verify/run-all.sh <bundle> <tag>` (uses `six-verify.mjs`; each mode runs in 2 separate
Node processes; Node 22.23.1). It renders MIDI key 60, velocity 0.8, 1 s note + 0.5 s tail at 48 kHz / 512 block.

Results, **identical for the legacy and the exnref bundle** (same fingerprint `a741a2354950cfbf`, so the exception
form does not change the audio):

- Plugin id `org.baconpaul.six-sines`; **2591 parameters**; peak 0.1418; saved state 90 653 bytes.
- 5 renders bit-identical in each of 2 processes, and the fingerprints match across the processes, in all three
  determinism modes: fresh plugin after `host.resetDeterminism()`, snapshot `restore()`, and one long-lived plugin
  ("warm", only with the priming block).
- State round trip (Main Level, Main Pan, Main Attack Shape: set by event, `saveState`, `loadState` into a fresh
  plugin): `getParam` and the render match the event render, the audio really changes versus default, and re-saving
  gives byte-identical state. All pass.
- **Reset needed: render one empty block (512 frames) after `createPlugin().activate()` / `loadState()`, before the
  note.** Without it: the default render differs (fingerprint `82a5168969d75f54` instead of `a741…`), a note arriving
  in the first block after `loadState` is silent (peak 0, `scripts/wclap-six-verify/state-priming.mjs`), the state
  round trip fails, and the long-lived "warm" mode is not repeatable (5 renders differ). Loaded state is evidently
  applied on the first `process()` call. With the priming block everything is deterministic.
- No `rand()`/`std::rand`/`random_device`/`mt19937`/`steady_clock`/`system_clock`/`time(` in `external/six-sines/src`
  (grep), unlike OB-Xf; the determinism seen needs no RNG reset beyond the host's.
- Cosmetic: at plugin creation Six Sines logs `Unable to resolve user documents path: Read-only file system`
  (no preopened home in the sandbox); it does not affect audio.

## 4. The exnref change against open-webclap

`scripts/wclap-exnref/juce-webclap-plugins-exnref.patch` (against juce-webclap-plugins `ecab895`) **applies cleanly**
to open-webclap `0833f5b` (`git apply --check` on a pristine `git archive` of that commit: no rejects, no fuzz). All
three files it touches (`modules/juce_webclap/juce_webclap.cmake` `JUCE_WEBCLAP_FLAGS`,
`ports/surge-xt/CMakeLists.txt`, `scripts/build-surge-xt.sh`) are unchanged at those places. The other ports
(`six-sines`, `obxf`, `odin2`, `ripplerx`) take their wasm flags from `juce_webclap.cmake`, so only that hunk matters
for them; `ports/*/CMakeLists.txt` contain no other `-fwasm-exceptions`.

The original makes exnref the unconditional default, which would break the kit's tested 3.1.51 build. For a PR that
could be merged, I rebased it as an **opt-in**: `scripts/wclap-exnref/open-webclap-exnref.patch` (7 files, +17/-5):

- `juce_webclap.cmake` and `ports/surge-xt/CMakeLists.txt`: `option(WEBCLAP_EXNREF ... OFF)`; when ON append
  `-sWASM_LEGACY_EXCEPTIONS=0`. Default behaviour is byte-for-byte what the kit builds today.
- `scripts/build-{six-sines,surge-xt,obxf,odin2,ripplerx}.sh`: `BUILD="${WEBCLAP_BUILD_DIR:-$ROOT/build/<name>}"`,
  so the legacy and exnref variants can sit side by side (they cannot share a build dir).
- Not included (our local convenience, not for the PR): the `~/wclap-native/emsdk-4/emsdk_env.sh` lookup.

Apply: `git -C ~/wclap-native/open-webclap apply ~/Developer/apps/synth.is/kromosynth-plugin/scripts/wclap-exnref/open-webclap-exnref.patch`
(the Studio clone already carries it as uncommitted changes). No PR opened.

Verification of the exnref Six Sines bundle:

- `wasm-dis`: 2949 `try_table`, 0 legacy `try` (above).
- **Wasmtime via the wclap-bridge probe**: `WORK=/tmp/aa490 WCLAP_SRC=<bundle> scripts/wclap-spike.sh build`
  (wclap-bridge `cd11d22`, with the script's two patches), then `wclap-probe six-sines.wclap`. Result:
  `MODE=wclap`, `PLUGIN_ID=org.baconpaul.six-sines`, `PARAM_COUNT=2591`, `RMS=0.0978`, `CPU_RT_FRACTION=0.015`,
  `CHECK_PLAYS=PASS`, `STATE_BYTES=90653`, `CHECK_STATE_ROUNDTRIP_SAME_INSTANCE=PASS`. The legacy bundle is refused
  by Wasmtime ("legacy_exceptions feature required for try instruction"), as expected.
  `CHECK_STATE_ROUNDTRIP_FRESH_INSTANCE=FAIL` (`rms=0`): this is the probe loading state into a fresh instance and
  rendering immediately, i.e. the same missing-priming-block behaviour as in section 3; I did not change the probe to
  confirm that in Wasmtime, so treat the attribution as likely, not proven.
- **Node host**: renders and passes everything in section 3.

## 5. Surge XT: open-webclap's port against ours

Ours is juce-webclap-plugins `ecab895` (+ the exnref patch). Both consume the same Surge pin (`4f425b04…`), the same
JUCE, and the same `surge-webclap.patch` / `surge-no-threads.patch` (diffs empty). `modules/juce_webclap` differs only
in the exnref flag and the test-host worklet. open-webclap's Surge XT is ecab895 plus three commits:
`ecc2afc` (the editor's meters follow the engine), `9ca4b51`/`c994477` (NOTICES, README, licence rows: the port is now
labelled GPL-3.0-or-later with its own `LICENSE`), `464f44f` (test host audio inputs / WASI without preopens).

Lua, OSC and MTS-ESP are dropped in **both** (ecab895 already says "formula LFOs without Lua"); the task's premise that
open-webclap drops them but ours does not does not hold. The only functional difference: `dsp_main.cpp`/`ui_main.cpp`/
`surge_frames.h` add a `meters` frame (DSP -> UI: VU peaks, CPU level) for the editor. That touches the editor only,
not the engine or the parameter list or the state blob. Consequence for our genomes and landmarks: **none expected**
(same engine source, same parameter set and 0..1 host-facing values, same state format); our adapter needs no change.
The licence/NOTICES wording is the thing to track for the hosting-obligations task.

**Unverified:** I did not build open-webclap's Surge XT or render it; the "no impact" conclusion is from diffing
sources and pins. The exnref patch was not built for Surge XT/OB-Xf/Odin 2/RipplerX in this run (Surge's hunk is the
same as our verified aa4.52 one; the others share the cmake hunk Six Sines used). Six Sines under Wasmtime was only
exercised by the probe's single note and state check, not by the 5-render determinism test (that is Node-host only).
Only one platform (macOS arm64, Studio) was used.
