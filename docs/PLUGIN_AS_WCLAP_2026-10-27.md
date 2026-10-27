# The Synth.is plugin as a WCLAP: feasibility note and skeleton (synthis-et4.9)

Run 2026-10-27, unattended. Step 1 only. Follows `WCLAP_GUEST_HOSTING_ROUTE.md` (aa4.25) section 3, which said
"Plugin builds as a WCLAP: not done, estimated 8 to 12 working days to a loading skeleton, none of it attempted".

## 0. What was done, and what was not

**Done and run:**
- `-DSYNTHIS_WEBCLAP_TARGET=ON` (default OFF) adds the `synthis_webclap` target. It builds a **skeleton WCLAP**:
  a `juce::AudioProcessor` (synth, MIDI in, stereo out, one `Gain` parameter, state) with a sine voice,
  wrapped by the open-webclap kit's `juce_webclap_clap.cpp`.
- Built with the kit's own toolchain (`~/wclap-native/emsdk`, Emscripten 3.1.51, legacy exceptions): `module.wasm`
  **249,904 bytes**. Also built with `emsdk-4` (Emscripten 4.0.20, `-DSYNTHIS_WEBCLAP_EXNREF=ON`): **251,240 bytes**.
  Both link in about 10 s once the JUCE objects exist (first build about 60 s).
- Loaded in the headless Node host (`kromosynth-burst/packages/wclap-host-node`, Node v22.23.1; the legacy build):
  plugin list `is.synth.plugin.skeleton`, param `Gain`, key 69 held for 24000 frames at 48 kHz: **RMS 0.3536**
  (a 0.5-amplitude sine is 0.3536, so it is the expected value; the bar was above 0.05).
- With the option OFF the configure is unchanged (no `synthis_webclap` target in the generated Makefile; the new
  code is two `if(SYNTHIS_WEBCLAP_TARGET)` lines and an `option()`). The gate result is in section 9.

**Not done:**
- The exnref build was **not loaded** anywhere (Node 22 here takes the legacy form; a Wasmtime-based host would need
  exnref, and none was run). Only built.
- Not loaded in the kit's browser test host, in webclap.github.io or in openDAW (needs a browser; a human step).
- **Not our real plugin class.** The skeleton processor is a 100-line stand-in, not `KromosynthAudioProcessor`,
  which cannot compile for wasm today (section 1). "Our plugin class" is therefore met only in shape.
- No Faust, no sampler, no UI page (`ui/`), no engine guests. No libfaust or sfizz was built for wasm.
- Nothing was shipped, signed, published, or added to the gate. The gate must not need an emsdk.

## 1. Dependency inventory: what in `src/` is native-only

Counted from the `#include`s and types in `src/`; "blocker" means it cannot be built for wasm32-emscripten as is.

| File | Native-only dependency | Verdict for a WCLAP |
|---|---|---|
| `FaustEngine.cpp/.h` | `wasmtime.h` / `wasm.h` (Cranelift JIT) or wasm3 (`KROMOSYNTH_WASM3`); runs the artefact's compiled Faust `.wasm` | **Blocker.** Wasmtime cannot exist inside wasm. wasm3 is portable C and could be compiled to wasm (wasm-in-wasm, unmeasured), or replaced by libfaust (section 3). `FaustEngine.h` names no runtime type, so the voice logic can be kept. |
| `SamplerEngine.cpp` | sfizz (C++, `third_party/sfizz`), its file reads and its threads (background loading) | Reachable, with work (section 4). |
| `WclapEngine.cpp/.h`, `WclapEngineView.*`, `cmake/WclapBridge.cmake` | `dlfcn.h`, `wclap-bridge.h` (native, runs on Wasmtime), the macOS webview | **Blocker.** Whole feature is the native hosting path (et4.3/et4.4). Replaced by a merge (section 5). |
| `PluginProcessor.cpp` | `fcntl.h`/`unistd.h` (file locking / output), `juce::File`, the engine switch | Most is portable. The POSIX parts and file-system use need `#if` or a shim. |
| `AcquisitionRunner.*`, `ArtifactAcquisition.*`, `RenderOutbox.*`, `RenderList.*`, `InstrumentLibrary.*` | `juce::File`, `juce::URL` downloads, `juce::ThreadPool pool_{1}`, `~/Library/...` library and outbox, App Group on iOS | **Blocker as written.** No threads (the kit's module is single-threaded; `wclap-host-node` refuses `thread-spawn`), no `JUCE_USE_CURL` (the kit sets `JUCE_USE_CURL=0`), no persistent disk. In a WCLAP these move to the page: the UI page downloads and hands bytes to the module (section 6). |
| `PluginEditor.*`, `ShellPage.h`, `ExternalBrowser.*`, `AuthSentinel.h`, `CheckoutSentinel.h` | `juce::WebBrowserComponent` (WKWebView), `pageAboutToLoad` sentinel URLs, `launchInDefaultBrowser` | **Dropped.** The kit has `JUCE_WEB_BROWSER=0`. In a WCLAP the page *is* the browser; sentinel URLs become page messages or plain links. |
| Format glue | `juce_audio_plugin_client`, `juce_audio_utils`, `juce_gui_extra`, AU/VST3 | Not used; the kit wraps `createPluginFilter()` itself. |
| `juce_dsp` | Used by `FaustEngine.cpp` | Supported by the kit (`juce_wasm`); `juce_wasm_dsp` (the DSP-only library, the one the skeleton links) has no `juce_dsp`, so a real build links `juce_wasm` or adds it. |

JUCE modules the kit does **not** support: `juce_audio_devices`, `juce_audio_plugin_client`, `juce_audio_utils`
(only its keyboard components), the web browser, curl, threads in practice, freetype/fontconfig (it ships its own
font path). `juce_audio_formats` is available as `juce_wasm_audio_formats` (Ogg Vorbis off).

## 2. CMake target plan

Built, as the skeleton:
- Top level: `option(SYNTHIS_WEBCLAP_TARGET OFF)`; when ON, `add_subdirectory(webclap)`.
- `webclap/CMakeLists.txt` (native tree): checks the kit and emsdk exist, adds custom target `synthis_webclap` that
  runs `scripts/webclap-skeleton.sh build`. Cache variables: `SYNTHIS_WEBCLAP_KIT` (default
  `~/wclap-native/open-webclap`, pinned 0833f5b), `SYNTHIS_WEBCLAP_EMSDK` (`~/wclap-native/emsdk`),
  `SYNTHIS_WEBCLAP_BUILD_DIR`, `SYNTHIS_WEBCLAP_EXNREF`.
- **Why out of tree:** the top-level project is a native macOS/Windows build (it even `FATAL_ERROR`s on other
  platforms) and uses `juce_add_plugin`; the kit uses its own JUCE copy and a hand-made `add_library` set under the
  Emscripten toolchain. One CMake tree cannot hold both toolchains. The custom target is the seam.
- `webclap/emscripten/CMakeLists.txt` (Emscripten tree): `include(juce_webclap.cmake)`, `add_executable(module ...)`
  with `juce_webclap_clap.cpp` + `juce_webclap_standalone.cpp` + our processor, link `juce_wasm_dsp`, link options
  `JUCE_WEBCLAP_DSP_LINK_OPTIONS`. Modelled on the kit's `ports/obxf`.
- Two JUCE trees (ours at 9.0.1 under `third_party/JUCE`, the kit's `external/JUCE`) is a risk to settle: the kit's JUCE
  has wasm patches, so the real target must compile our sources against the kit's JUCE, not ours.

Later steps, in order: (1) compile `PluginProcessor` with the native-only parts behind `SYNTHIS_WASM` `#if`s;
(2) libfaust-interpreter or wasm3-in-wasm for `FaustEngine`; (3) sfizz; (4) `ui/` page; (5) merge of guests;
(6) `scripts/pack-wclap.py` for the `.wclap.tar.gz` and `bundle-license.sh` for the licences.

Gotchas found: a `JucePlugin_Name` containing parentheses broke the kit's `juce_gui_basics_wasm.cpp`
(`return JucePlugin_Name;` lost its value; use plain words); `module.wasm` needs only `--target module` (the GUI
library is not needed); bash 3.2 (macOS) rejects an empty array under `set -u`.

## 3. Faust genomes in wasm

What the native plugin does: the artefact carries a **compiled Faust `.wasm`** (from faustwasm), run by
FaustEngine on Wasmtime/Cranelift or wasm3; the `.dsp` source is also part of an artefact. A WCLAP cannot start
Wasmtime. Two routes:

| Route | What it is | Speed | Size |
|---|---|---|---|
| A. libfaust **interpreter backend** compiled into `module.wasm`, fed the `.dsp` | Same route as in the aa4.25 table. Needs libfaust's interpreter + the Faust libraries (`stdfaust.lib` etc.) embedded as a virtual file system. | **Measured natively on an A15, not in wasm:** the interpreter ran 0.49x wasm3's throughput (`probe/gate0-interp`, "rejected 0.49x"). wasm3 itself was a median 5.56x slower than Wasmtime/Cranelift. From those two, the interpreter is about **11x slower than Cranelift** (derived, not measured), and then loses more running as wasm inside a browser's JIT (unmeasured). | Not measured. No libfaust-for-wasm build was attempted here. Expect some MB (LLVM is not needed for the interpreter backend, which is the point). **Gap.** |
| B. wasm3 compiled to wasm, runs the artefact `.wasm` | Reuses artefacts and the existing `FaustEngine` wasm3 path (`KROMOSYNTH_WASM3`), no `.dsp` compile at run time, bit-identical with the null test baseline by construction (same bytes). | wasm3 native is 5.56x behind Cranelift; wasm3 *as* wasm adds the host JIT's overhead on top. Unmeasured. | wasm3 is small (a few hundred KB). |

Recommendation: **B first** (it keeps the artefact format and the null test, `scripts/null-test.sh` can be run
against it in `wclap-host-node`), and measure it before investing in A. A is only better if the artefact must be
compiled at run time or if B's voice count is unusable. Voice counts on iOS with wasm3 were 1-2 voices at the slow end
(`NEXT_SESSION_PROMPT_IOS`), so expect a WCLAP with an interpreter to be sampler-first, as iOS already is.
A third option, the one the browser is best at, is to let the page instantiate the artefact `.wasm` with the browser's
own `WebAssembly` API and stream audio into the module; that is not WCLAP-pure (the module alone no longer plays) and
is listed only because it is the fast path in a browser.

## 4. Sampler (sfizz) compiled in

sfizz 1.2.3 (BSD-2-Clause) is plain C++ with a CMake build we already drive (`cmake/Sfizz.cmake`). For wasm:
build with `SFIZZ_JACK/LV2/VST/AU/RENDER=OFF`, static library, `-fwasm-exceptions -msimd128` to match the kit;
no threads (sfizz's background file loading uses a thread pool: set `num_workers`/`SFIZZ_USE_SNDFILE` paths off or
run synchronously; sfizz has a `--no-threads` style option in recent versions, not verified here); `st_audiofile`
reads WAV/FLAC/MP3 with dr_libs (public domain) from memory, which suits a wasm module whose "file system" is the
Emscripten MEMFS. SFZ and sample bytes arrive from the page (section 6) and are written into MEMFS. Memory is the real
limit: an instrument's samples live in the module's linear memory (wasm32, up to 4 GB, host-limited; the skeleton asks
for 32 MB initial). **Not attempted;** nothing about sfizz-in-wasm was built or timed.

## 5. Engine guests inside a plugin that is itself a WCLAP

Native today: `WclapEngine` hosts a guest WCLAP through the wclap-bridge on Wasmtime. That does not exist inside a
WCLAP. The measured route is the **merge** (aa4.42, `scripts/wclap-merged-host.mjs`): `wasm-merge` of our module
(host) with the guest gives one module with two memories; Surge XT played in a minimal CLAP host loop, RMS 0.1011,
state round-trip byte-identical. Open points from there still hold: the host half never calls the guest across the
merge (import-to-export linking untried), both WASI namespaces must be separated, and the guest's buffers are
copied between memories.

New for this note:
- **Exceptions form.** JUCE guests (Surge XT etc.) and our module must use the same exception form, because a merged
  module cannot mix `try` (legacy) and `try_table` (exnref) in one instance on a host that only knows one. Legacy:
  browsers (older) and Node 22. exnref: Wasmtime hosts, newer browsers. Our build already switches with
  `SYNTHIS_WEBCLAP_EXNREF` (3.1.51 legacy, 4.0.20 exnref); the kit also ships `pin-*-exnref` and `pin-*-legacy` guest
  builds, so a merge needs a matching pair, one merged module per form, i.e. two bundles or a loader choosing by
  feature test. Mixed forms inside one merge: **untested**.
- The merge is a build step on a *fixed* guest set. Dynamic guests (any engine the user picks) still need route 4
  (a guest-hosting extension upstream), unchanged from aa4.25.

## 6. UI path

The kit's model: the bundle's `ui/` directory holds `index.html`, the kit's `js/webclap-ui.js` +
`webclap-ui-worker.js`, and (for JUCE editors) `ui.wasm`; the module answers `clap.webview` `get_uri`
(`ClapPluginInfo::webviewUri`, default `/ui/index.html`) and talks to the page with the frames in
`juce_webclap_protocol.h`; ports needing more define `createPageExtension()`. Meters, parameters, state are covered;
anything else is a port frame type.

For us this means: **the existing synth.is web UI is not reused as is.** The native plugin's editor is a webview onto
`https://synth.is` with sentinel URLs. In a WCLAP the page is served from the bundle, so options are
(a) a small page that embeds synth.is in an iframe and relays acquisition as page messages (needs the site to speak a
`postMessage` protocol; it already has the sentinel one), or (b) a bundled page with a parameter view only. (b) is the
cheap first step. Acquisition (downloads, library, render outbox) moves into the page; the page writes bytes into the
module through a port frame. Unbuilt: the skeleton has `hasEditor() == false` and no `ui/` directory, so the
skeleton exposes no page.

## 7. Licence stack of the resulting bundle

| Component | Licence | In bundle |
|---|---|---|
| Our code | AGPL-3.0-or-later (plugin is open source) | yes |
| JUCE (the kit's copy, 8.x/9.x) | AGPLv3 option (dual licence); same option as the native build | yes; this is what makes the whole bundle AGPL, which is consistent with the plugin's own licence |
| open-webclap kit (`juce_webclap`, ports) | see `~/wclap-native/open-webclap/LICENSE` and `docs/OPEN_WEBCLAP_UPSTREAM_2026-10-22.md` | yes (the module layer) |
| CLAP headers | MIT | yes |
| sfizz | BSD-2-Clause | when added |
| wasm3 | MIT | if route B |
| libfaust | LGPL-2.1+ (to be confirmed; **UNVERIFIED** here) with the Faust architecture-file exception for generated code | if route A |
| Faust libraries | various per library file; see `ARTEFACT_LICENCE_DECISION.md` section 1 (unverified there too) | if route A |
| Merged guests (Surge XT) | GPL-3.0 | when merged; GPLv3 is compatible with AGPLv3 only one way (the combination is AGPL-3.0, with GPL-3.0 parts, via GPLv3 section 13) |
| HarfBuzz, SheenBidi, fonts (DejaVu) | MIT, Apache-2.0, free | only with the GUI libraries, not in the skeleton |

The kit's `scripts/bundle-license.sh` produces `LICENSE`, `NOTICES.md` and `licenses/` inside the bundle; the real
target must call it. `THIRD_PARTY_NOTICES.md` needs a WCLAP section when the real target lands. The brand assets rule
(`ARTEFACT_LICENCE_DECISION.md` section 3) applies unchanged: AGPL does not license the name or icon.

## 8. Estimate and recommendation

The 8-12 day estimate for "a loading skeleton" is **superseded**: the skeleton took a day and loads. What remains is the
real work, in rough order and unmeasured: processor port with `#if` guards 2-3 days, Faust route B 3-5 days plus
measurements, sfizz 2-4 days, page and acquisition 3-5 days, merge of guests 2-4 days, licence bundle 1 day. Treat as a
guess. Cheapest next measurement: wasm3-as-wasm running one baseline elite in `wclap-host-node`, to get the number
section 3 lacks.

## 9. Reproduce, and the gate

```
cmake -S . -B build/wc -DSYNTHIS_WEBCLAP_TARGET=ON && cmake --build build/wc --target synthis_webclap
bash scripts/webclap-skeleton.sh test        # loads the bundle in wclap-host-node, asserts RMS > 0.05
SYNTHIS_WEBCLAP_EMSDK=~/wclap-native/emsdk-4 SYNTHIS_WEBCLAP_EXNREF=ON SYNTHIS_WEBCLAP_BUILD_DIR=$PWD/build/wc-exnref \
  bash scripts/webclap-skeleton.sh build      # exnref form, built only
```
`WCLAP_HOST_NODE` overrides where `wclap-host-node` is (default `~/Developer/apps/synth.is/kromosynth-burst/...`).
Gate (`scripts/gate.sh`, option OFF, run after these changes): **green, exit 0** (build, acquire_probe, null test, processor-diff).
