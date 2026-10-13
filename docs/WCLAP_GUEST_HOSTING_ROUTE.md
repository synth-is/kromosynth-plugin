# Synth.is plugin as a WCLAP hosting guests: route decision (synthis-aa4.25)

Run 2026-10-12, unattended. Builds on `WCLAP_DESK_CHECK.md` (aa4.10) and
`PLUGIN_AS_HOST_SPIKE.md` (aa4.23). Rule from Björn (2026-10-11): no reliance on one
product's special handling; no openDAW-only API.

**Headline.** The Synth.is plugin was **not** built as a WCLAP and was **not** loaded in
any host in this run. One thing was shown: route 1 (binary merge) is mechanically
possible for Surge XT. Everything else below is reasoning, labelled as such.

## 1. What was measured

Tools: Binaryen `wasm-merge` from `~/wclap-native/emsdk` (wasm-opt version 116), Node
v22.23.1, the Surge XT WCLAP `module.wasm` (6.8 MB) from `juce-webclap-plugins`.

```
wasm-merge surge/module.wasm host surge/module.wasm guest -o merged.wasm --all-features --rename-export-conflicts
```

- Merge succeeded in about 1 s; output 14.0 MB: **two separate memories**
  (`(memory $0 512 32768)`, `(memory $1 512 32768)`), each module's `memory.size`/`memory.grow`
  bound to its own memory. Exports keep the host's names (`memory`, `clap_entry`,
  `_initialize`, `malloc`) and the guest's are suffixed (`memory_11`, `clap_entry_15`,
  `_initialize_17`).
- Node 22 compiled and instantiated it (WASI imports stubbed to return 0) and both
  `_initialize` functions ran. Both memories were 32 MiB at start.

**Not shown:** that Surge then makes sound out of the merged copy (no CLAP host loop was
driven); that a guest reached through `wasm-merge`'s import-to-export linking works when the
host module calls it; Firefox, Chrome, Safari; WebCLAP browser host; openDAW. The test used
the same module twice, so it says nothing about merging two *different* toolchains' output.
It also says nothing about the wasm-exceptions issue (finding 1 of the spike) for native
hosts: browsers and Node accept the legacy form, Wasmtime does not.

## 2. Route per guest engine (reasoning; no product-specific handling anywhere)

| Guest | Route | Why |
|---|---|---|
| Faust (evolved genomes) | **Route 3 for Faust only: libfaust interpreter backend compiled into our module** | Each genome is a new module, so build-time merge cannot cover it. A DSP string is compiled at run time by the interpreter in the same wasm, no second wasm module needed. Cost: interpreter speed, below the wasm Faust backend we use natively (FaustEngine on wasmtime/wasm3). **Unproven**: no libfaust-interpreter-to-wasm build was attempted; measure voices on a mid-range laptop before committing. Fallback if too slow: Faust guests as WCLAPs via route 4 later. |
| Sample VIs (SFZ) | **Compiled in** (sfizz is source we already build) | Pure C++, no JUCE wrapper problem. Works in every browser. |
| Surge XT | **Route 1 (merge) as the first attempt; route 2 as the Safari answer** | Dynamic loading of a 6.8 MB module through a wasm interpreter (route 3) is not realistic for Surge-class DSP (the spike already measured 2.2x native under *Wasmtime JIT*). Route 2 (build Surge's synth core into our module without JUCE) is the portable option: Surge's core builds that way (its Python bindings do; the juce-webclap port already builds `surge-common` from upstream's source list with a headless patch). Merge is a build step that reuses the existing WCLAP unchanged; route 2 is per-engine work. |
| Other JUCE engines (Six Sines, Odin 2, RipplerX, OB-Xf) | Route 1 while few; no route-2 attempt | Each is a ready WCLAP from the same kit, so merging is a build line each. Route 2 with several JUCE plugins in one module is the hard case named in the task and is not attempted. |

Route 4 (a standard guest-hosting extension in wclap-host-js and wclap-bridge) is the only
route that is dynamic **and** full speed. It depends on the WebCLAP maintainers and does not
exist; propose it upstream, do not wait on it. Not verified by this run that it does not
already exist (WCLAP source was not re-read).

### What the merged WCLAP must do (design, untested)

1. Our module is the `host`; the engines are `guest` modules with their `clap_entry`
   imported as a global and `memory` kept private, so only our module's `memory` and
   `clap_entry` are exported (a host reads exactly those).
2. Our code calls the guest's `clap_plugin` function table directly, in-process. Pointers
   differ per memory, so audio and event buffers are **copied** across (a small per-block
   cost, plus `memcpy` bound by each module's own memory). This is the part most likely to
   hide a problem; not tried.
3. Guest imports (`wasi_snapshot_preview1`, any `env.*`) must be satisfied by our module's
   imports so a single WCLAP host import set suffices. The Surge module imports only ten
   WASI functions (seen in `wasm-dis`), so this looks small for Surge.
4. Guest UI (`ui.wasm` in a webview) is separate from `module.wasm` and does not merge; the
   merged WCLAP would need to ship the guest's `ui/` bundle in its own directory. The page
   protocol is the kit's own, not a host's. Unproven.

## 3. Acceptance, item by item

- **Route chosen per guest engine, no product-specific handling**: done, section 2.
  Nothing relies on openDAW's API or any host feature beyond the WCLAP/CLAP ABI.
- **Plugin builds as a WCLAP**: **not done.** `FaustEngine.cpp` is a wasmtime/wasm3 host
  and cannot go inside a wasm module. A WCLAP build is a new target (juce-webclap kit)
  with Faust through libfaust-interpreter, sfizz compiled in, and the Wasmtime/wasm3
  paths excluded. Estimated 8 to 12 working days to a loading skeleton (a guess; none of it
  attempted). Unverified.
- **Loads in two hosts**: **needs a human.** The kit's own `modules/juce_webclap/test-host`
  loads a `.wclap` directory in a browser; that is not a second product's host. Steps:
  1. `python3 ~/wclap-native/juce-webclap-plugins/scripts/serve.py` and open the test host
     with the built `synthis.wclap`; confirm the plugin lists, plays a note, and the merged
     Surge sounds.
  2. WebCLAP browser host (webclap.github.io or the project's current URL): load the same
     bundle URL.
  3. openDAW: add the bundle as a WCLAP instrument, play, save the project, reopen.
  Record which of the three fail. If openDAW needs any special handling, record that and
  keep the rule: do not ship it.
- **Surge XT plays with a discovered configuration applied**: **not shown.** Reason it is
  not blocked in principle: a configuration is Surge's state (`.fxp` minus header, per the
  kit's page protocol; the spike saved/loaded 58,603 bytes headlessly under the bridge) and
  `clap.state` is the standard carrier. Not tried inside the merged module. Known blocker
  from the spike: a second *bridge/Wasmtime* instance panics. That is a native-bridge
  problem; merged-in-a-browser does not use it, but this is not shown.
- **Faust playback path**: decided: libfaust interpreter compiled in (route 3, Faust only).
  Unproven for speed and size.
- **If no route works, scope to sample VIs + Faust**: not triggered. Route 1 is plausible
  on Chrome 120+, Firefox 125+, Node 22+. **Safari**: multi-memory unsupported per the task's
  feature table; not re-checked. On Safari the WCLAP build ships sample VIs and Faust,
  which is the fallback the rule already allows. Engines needing route 2 would reopen it.

## 4. Recommendation and follow-ups (not filed)

1. Prove the merge end to end first, cheaply: write a ~100-line CLAP host in Node 22 (or
   reuse `clap-host-worklet.js`) that loads `merged.wasm`, drives the guest's
   `clap_entry`, plays a note and checks RMS. Half a day.
2. Then the WCLAP target for the plugin (sampler + libfaust interpreter) and a measurement of
   Faust voice count.
3. Raise route 4 with the WebCLAP maintainers.
