# Plugin-as-host spike: Surge XT WCLAP inside the Synth.is plugin (synthis-aa4.23)

Run 2026-10-12, unattended, on the Mac Studio-class arm64 macOS laptop. Follows
`WCLAP_DESK_CHECK.md` (aa4.10), whose facts were unverified; some are now checked, below.

**Headline.** The Surge XT WCLAP was driven headlessly through the wclap-bridge C
API and produced audio, and a state save/load round trip worked on the instance that
survived creation. It was **not** run inside the Synth.is plugin and **not** in a DAW.
Plugin creation in the bridge is flaky (3 of 12 runs), a second instance crashes,
and nothing about the editor was exercised. Of the four checks, none is fully
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
4. **Plugin creation is flaky.** 3 of 12 consecutive runs reached audio; the rest
   returned a null plugin from `create_plugin` (sometimes with "libc++abi:
   terminating"). Cause not found. Do not build on this until it is.
5. **A second instance panics Wasmtime**: "object used with the wrong store"
   (`store/data.rs:196`), aborting the process. It hit in every run that reached
   the fresh-instance stage, so `CHECK_STATE_ROUNDTRIP_FRESH_INSTANCE` never ran for
   the WCLAP. That is what a DAW does when it reloads a session, so this blocks the
   state check in a DAW. The native `.clap` passes both stages.
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
Both were already on this machine; neither is in this repo. The WCLAP half is flaky
(finding 4): the script retries up to six times.

## 5. Recommendation

Do not start `WclapEngine` yet. First: (a) find why creation is flaky and why a second
instance panics (a bridge/Wasmtime-39 bug, or a mistake in how the probe drives it);
(b) ask the juce-webclap-plugins build for exnref output; (c) retry on Wasmtime 48.
If (a) turns out to be a bridge defect with no quick fix, the alternative is to host
the WCLAP with the Wasmtime that `FaustEngine` already uses and write the CLAP-over-wasm
glue ourselves, which is larger than the desk check's estimate.
