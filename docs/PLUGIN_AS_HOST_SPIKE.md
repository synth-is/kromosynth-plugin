# Plugin-as-host spike: Surge XT WCLAP inside the Synth.is plugin (synthis-aa4.23)

Run 2026-10-12, unattended, on the Mac Studio-class arm64 macOS laptop. Follows
`WCLAP_DESK_CHECK.md` (aa4.10), whose facts were unverified; some are now checked, below.

**Headline.** The Surge XT WCLAP was driven headlessly through the wclap-bridge C
API and produced audio, and a state save/load round trip worked on the instance that
survived creation. It was **not** run inside the Synth.is plugin and **not** in a DAW.
Plugin creation in the bridge was flaky (3 of 12 runs) and a second instance crashed;
both were found in aa4.40 (findings 4 and 5: a bridge bug and a probe bug, both fixed here). Nothing about the editor was exercised. Of the four checks, none is fully
demonstrated; what each one did and did not show is listed in section 2.

## 1. What was built

- `tools/wclap-probe/main.cpp`: a standalone CLAP host, not part of the plugin build
  or the gate. It loads either a WCLAP directory (via `wclap_open*` /
  `wclap_get_factory`) or a native `.clap` (dlopen) and runs identical code: create,
  activate, one note-on, render N seconds in 256-frame blocks, save state, load it,
  save again, compare. It prints `KEY=VALUE` lines. Exit 0 only if all checks pass.
- `src/SubstrateRouting.h` + `tools/wclap-probe/routing_test.cpp`: a pure function
  that maps the file list of an unpacked archive entry to a substrate
  (`sampler` / `faust` / `wclap` / `none`). Nothing in the plugin calls it yet.
  The `instrument.wclap/module.wasm` layout is a **proposal**; no server produces it.
- `scripts/wclap-spike.sh`: reproduces everything (`routing | build | run | all`).
  Needs the network and two artefacts built elsewhere (see section 4). Not in the gate.

Pinned: wclap-bridge `cd11d22` (BSL-1.0, 2026-01-30), which fetches Wasmtime
**v39.0.1**. The plugin pins Wasmtime **48.0.1** (`cmake/Wasmtime.cmake`), so linking
the bridge into the plugin as is means two Wasmtimes in one binary. Not attempted.

## 2. The four checks

| Check | Status | Evidence |
|---|---|---|
| Surge XT WCLAP plays inside the Synth.is plugin with its own UI | **Partly. Plays headlessly in a standalone host. Not in the plugin, no UI.** | `CHECK_PLAYS=PASS`, RMS 0.15 to 0.19, in 3 of 12 runs |
| State save/load round-trips in a DAW session | **Partly. Same-instance round trip passes, headless. Fresh-instance and DAW: not shown.** | `CHECK_STATE_ROUNDTRIP_SAME_INSTANCE=PASS` (58603 bytes, byte-identical after reload) |
| Clicking an archive sound opens the right substrate | **Not shown. Only the routing decision is tested.** | `routing_test: PASS` (8 cases). No UI, no acquisition, no engine switch |
| CPU measured against native Surge XT | **Measured once, one voice, one default patch: about 2.2x native.** | below |

### Findings

1. **The shipped Surge WCLAP cannot be loaded by Wasmtime as built.** Its
   `module.wasm` uses legacy wasm exceptions (`-fwasm-exceptions`; fine in browsers).
   Wasmtime v39 fails with "exceptions proposal not enabled", and even once the
   bridge is patched with `wasmtime_config_wasm_exceptions_set(config, true)` it
   fails with "legacy_exceptions feature required for try instruction": Wasmtime
   implements only the newer exnref form, and the C API exposes no legacy switch.
   What worked: binaryen 133 `wasm-opt --translate-to-exnref` on a copy of the module.
   Emscripten's bundled binaryen is too old for that flag. Lasting fix: build the
   WCLAP with the exnref lowering. Whether Wasmtime 48.0.1 (the plugin's) accepts the
   translated module is **untested**.
2. **The bridge's default 2000 ms per-call limit kills Surge XT's init**
   (`wclap_global_init(2000)`); I passed 0. A real limit needs the
   instantiate-off-thread plan from the desk check.
3. **Surge wants a writable `/user` directory.** With the plain `wclap_open` it
   logs "User directory is non-writable" and creation fails; `wclap_open_with_dirs`
   with real preset/cache/var directories gets further. Whether that is the whole
   cause is unknown.
4. **Plugin creation was flaky: a wclap-bridge bug (synthis-aa4.40).** 3 of 12 runs reached audio
   (the rerun in aa4.40 saw 1 to 3 of 12); the rest returned a null plugin from `create_plugin`.
   Root cause: `WclapModuleBase::hostTemplate` (`source/_generic/wclap-module-base.h:127`,
   bridge cd11d22) is declared `wclap_host hostTemplate;` and only its four function fields are
   ever filled, so `host->clap_version` (and name, vendor, url, version) is whatever the heap
   held. The JUCE WCLAP wrapper (`juce_webclap_clap.cpp`, factory `create_plugin`) returns null
   unless `clap_version_is_compatible(host->clap_version)` (major >= 1); fresh heap is mostly
   zero, so most runs failed silently, which is why nothing was logged. It is not the probe's
   single-thread driving: a single-thread WCLAP (this module has no `wasi thread-spawn`) uses one
   Wasmtime store for the whole module and every call came from one thread. Fix (2 lines, in
   `build()` of `scripts/wclap-spike.sh`): `wclap_host hostTemplate{};` and
   `hostTemplate.clap_version = {CLAP_VERSION_MAJOR, CLAP_VERSION_MINOR, CLAP_VERSION_REVISION};`
   in the `WclapModule` constructor. With it: 12 of 12 creations and a passing
   `CHECK_STATE_ROUNDTRIP_FRESH_INSTANCE`, on Wasmtime 39.0.1 and on 48.0.1 (the bug is in the
   bridge, so 48.0.1 alone does not fix it: unpatched it failed 11 of 12 there too). To upstream:
   see `.bdloop/proposals.json`.
5. **The "second instance panic" was the probe's mistake, with a bridge sharp edge
   (synthis-aa4.40).** CLAP extensions are per plugin instance, and wclap-bridge stores the
   guest's extension pointers in its `Plugin` object, filling them in on `get_extension()`. The
   probe asked only the first plugin and reused its `clap_plugin_state_t` on the second, so the
   bridge's `Plugin::state_load` called guest function-table slot 0 (a null funcref, which has no
   store), and Wasmtime panicked with "object used with the wrong store" (`store/data.rs:196`;
   the log showed `fnP=0`). Fixed in `tools/wclap-probe/main.cpp` by calling
   `b->get_extension(b, CLAP_EXT_STATE)`. The bridge still turns a call before `get_extension`
   into an abort instead of an error (`wtCall` does not check for a null funcref); worth a
   guard upstream. A DAW calls `get_extension` per instance, so it is not exposed to this.
6. **CPU.** Same host code, 4 s of a held middle-C, default patch, 256-frame blocks,
   fraction of real time on one core. Native: 0.0034 to 0.0039 (3 runs). WCLAP:
   0.0077 to 0.0084 (3 runs). About **2.1 to 2.4x**, against WebCLAP's quoted ~1.5x.
   One voice and the default patch say little about dense patches; the
   `juce-webclap-plugins` notes measured 1.2 % median in a Node host with 3-voice chords.
   Not measured: patch-load cost (that project reports up to 44 ms inside one
   `process()`), memory, startup time (the 6.5 MB module compiles on first load; the
   bridge enables Wasmtime's cache).
7. **Native Surge XT does not render two fresh instances identically** (RMS 0.10 to
   0.18 between runs: random oscillator phase), so the probe compares saved state
   bytes, not audio samples. A null-test against a Node-host render, as the desk check
   proposed, cannot be sample-exact for this engine.
8. **The module is DSP only.** The editor is a separate `ui.wasm` drawn in a webview
   page (juce-webclap-plugins PLAN, "Surge XT findings"). The bridge links a
   `webview-gui` module, but the probe never opened a window, so "its own UI inside
   the plugin" is unproven.

## 3. Needs a human with a DAW (not demonstrated)

1. **UI.** Build the bridge's `plugin/` (CLAP/VST3 wrapper), put the Surge WCLAP
   (with the translated `module.wasm`) where it scans, open "[WCLAP] Surge XT" in
   Reaper, and confirm the editor appears and the patch browser works.
2. **In the Synth.is plugin.** Needs a `WclapEngine` beside `FaustEngine` and the
   Wasmtime version question settled (desk check s.1; estimate 14 to 18 days). Not
   started here.
3. **State round trip in a session.** In Reaper: load, change a parameter, save the
   project, close, reopen, confirm the sound and parameter are restored. Blocked by
   finding 5 until fixed upstream or worked around.
4. **Archive click to substrate.** Needs the shell page to hand an archive entry to
   the plugin, an artefact kind for hosted guests in `ArtifactAcquisition`, and
   `InstrumentLibrary::Kind::wclap`. Not started.
5. **CPU in a DAW.** Repeat finding 6 inside Reaper with a dense patch, both
   versions, and compare the DAW's per-track meter.

## 4. Reproduce the headless parts

```
./scripts/wclap-spike.sh routing   # no network; the substrate-routing test
./scripts/wclap-spike.sh all       # clones the bridge, builds, translates, runs
```

Inputs it expects (override with `WCLAP_SRC`, `NATIVE_CLAP`): a Surge XT WCLAP
directory built by `~/wclap-native/juce-webclap-plugins/scripts/build-surge-xt.sh`
(its `module.wasm`) and a native Surge XT `.clap` from `~/wclap-native/surge-native`.
Both were already on this machine; neither is in this repo. The script patches the
bridge for finding 4 and keeps a six-attempt retry only as a guard.

## 5. Recommendation

Do not start `WclapEngine` yet. First: (a) done in aa4.40 (findings 4 and 5: an uninitialised
`hostTemplate` in the bridge, and a probe that skipped per-instance `get_extension`); send
the bridge fix upstream; (b) ask the juce-webclap-plugins build for exnref output; (c) Wasmtime 48.0.1 retried in aa4.40: works with the same patch (the C API there takes a bool for `wasi_config_preopen_dir` permissions, so the bridge needs a small port).
If (a) turns out to be a bridge defect with no quick fix, the alternative is to host
the WCLAP with the Wasmtime that `FaustEngine` already uses and write the CLAP-over-wasm
glue ourselves, which is larger than the desk check's estimate.
