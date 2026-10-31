# Surge XT WCLAP inside the plugin, on one Wasmtime (synthis-et4.3)

Run 2026-10-26, unattended, macOS arm64. Follows `PLUGIN_AS_HOST_SPIKE.md` (aa4.23/.40/.52), which
proved the bridge headlessly but not inside the plugin and not with a fresh instance.

**Result.** With `-DSYNTHIS_WCLAP_HOST=ON` the plugin links the wclap-bridge against its own Wasmtime
**48.0.1** (one `libwasmtime.a`, shared with `FaustEngine`), loads the installed Surge XT exnref bundle,
plays a note from `processBlock`'s MIDI buffer at RMS 0.101, and round-trips `clap.state` on the same
instance AND into a fresh `KromosynthAudioProcessor` restored from the session. Default OFF: the shipping
build and the gate do not see any of it. Not done: the editor/UI, a real DAW, signing, packaging, and
acquisition of a wclap archive entry (see "Not done").

## What was built

| Piece | Where |
|---|---|
| CMake option, default OFF | `CMakeLists.txt` (`SYNTHIS_WCLAP_HOST`), `src/CMakeLists.txt` (adds `KROMOSYNTH_HAS_WCLAP=1`, `WclapEngine.*`, the bridge lib) |
| Bridge fetched + patched, built on the plugin's Wasmtime | `cmake/WclapBridge.cmake`, `cmake/wclap-bridge/synthis-bridge.patch` (bridge `cd11d22`) |
| The engine | `src/WclapEngine.{h,cpp}` |
| Routing wired | `src/InstrumentLibrary.cpp` `resolve()` calls `kromosynth::substrateForFiles` (`src/SubstrateRouting.h`); `Kind::wclap`; `PluginProcessor` `Active::wclap` |
| Session state | `PluginProcessor::get/setStateInformation` carry the guest's clap.state as `wclapState` |
| In-plugin probe | `tools/wclap-host-probe` (only with the option ON; not in the gate) |

Bridge patches (all four are in the one `.patch`): `hostTemplate{}` zero-init plus `clap_version`
(finding 4 of the earlier spike); null funcref guard in `wtCall` (finding 5, the `state_load` abort);
`wasmtime_config_wasm_exceptions_set(true)`; `wasi_config_preopen_dir` ported to Wasmtime 48's bool
signature. The per-call time limit is the runtime argument of `wclap_global_init`: **60000 ms** (bridge
default 2000 ms kills Surge XT's init), env `KROMOSYNTH_WCLAP_TIME_LIMIT_MS` overrides.
The bridge applied cleanly and compiled against 48.0.1 with only those changes.

Archive layout (unchanged proposal): `instrument.wclap/module.wasm` selects `Substrate::wclap`, and wins
over a bare `.sfz`/`.wasm`. In a build WITHOUT the option `resolve()` ignores it, exactly as before.

## Reproduce

```
cmake -S . -B build/wclap-on -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
  -DKROMOSYNTH_BUILD_SAMPLER=ON -DKROMOSYNTH_BUILD_FAUST=ON -DKROMOSYNTH_COPY_AFTER_BUILD=OFF -DSYNTHIS_WCLAP_HOST=ON
cmake --build build/wclap-on --target wclap_host_probe KromosynthPlugin_AU KromosynthPlugin_VST3
mkdir -p /tmp/lib/Instruments/surge-xt
ln -s ~/wclap-bundles/surge-xt/surge-xt-*.wclap /tmp/lib/Instruments/surge-xt/instrument.wclap
KROMOSYNTH_PROBE_LIBRARY=/tmp/lib build/wclap-on/tools/wclap-host-probe/wclap_host_probe_artefacts/Release/wclap_host_probe surge-xt
# native reference, same code path: KROMOSYNTH_WCLAP_NATIVE_CLAP="$HOME/wclap-native/surge-native/build/surge_xt_products/Surge XT.clap"
```

## Checks (wclap_host_probe, Surge XT, 48 kHz, 256-frame blocks, MIDI note 60 velocity 100)

| Check | Result |
|---|---|
| `resolve()` picks `wclap` from the file list | PASS |
| Plays from the processor's MIDI path, RMS over 0.1 to 1.5 s | **0.101** (needs > 0.05), peak 0.268. PASS |
| clap.state captures the patch (a parameter moved first; state differs from default) | PASS (58603 bytes both) |
| Same-instance round trip, saved bytes identical | PASS |
| Plays after `loadState` | RMS 0.101. PASS |
| **Fresh instance**: `getStateInformation` of processor A, `setStateInformation` into a new processor B, B's clap.state byte-identical to A's | PASS |
| Fresh instance plays | RMS 0.101. PASS |
| Load time | 1.0 to 1.2 s cold, 0.07 to 0.08 s with Wasmtime's compile cache warm |

Audio level equals the native build's (RMS 0.1012 both) at the same notes.

Decisions and caveats:
- **Output level.** The processor's Output default is -9 dB, headroom for genome instruments. Applied
  as is, Surge played at RMS 0.036 (under the 0.05 bar). For a hosted guest Output is applied relative
  to its default (unity untouched, the knob still works). Review this.
- **Empty settle block.** One 512-frame empty block is rendered after activate and after every
  `loadState` (the Six Sines finding). I did not test what happens without it for Surge XT.
- **"User directory is non-writable: /user"** is logged by Surge on every load, but loading and
  playing are unaffected. The bridge maps presets/cache/var, not `/user`.
- Loading is synchronous on the message thread inside the suspend window (about 1 s cold). A bad
  guest can block the UI up to the time limit.
- Several plugin instances share the one bridge engine (counted init/deinit in `WclapEngine.cpp`).
  Not tested with two simultaneous instances in one process beyond sequential A then B.

## CPU, in the plugin (fraction of one core, best of 3 x 4 s, `processBlock` only)

Same engine, same event path, same chords; the native column loads Surge XT's `.clap` with `dlopen`
inside `WclapEngine` (`KROMOSYNTH_WCLAP_NATIVE_REFERENCE`, probe-only).

| Voices | Native | WCLAP, limit 60000 ms | ratio | WCLAP, limit 0 | ratio |
|---|---|---|---|---|---|
| 0 (idle) | 0.00154 | 0.00426 | 2.8x | 0.00330 | 2.1x |
| 1 | 0.00329 | 0.00790 | 2.4x | 0.00577 | 1.8x |
| 4 | 0.00500 | 0.01303 | 2.6x | 0.00931 | 1.9x |
| 8 | 0.00822 | 0.02087 | 2.5x | 0.01498 | 1.8x |
| 16 | 0.01478 | 0.03715 | 2.5x | 0.02625 | 1.8x |

Per voice, (rt(16) - rt(0)) / 16: native 0.00083, WCLAP 0.00206 (**2.5x**), WCLAP without a limit
0.00142 (**1.7x**). The earlier spike's 2.1 to 2.4x (one voice, standalone, Wasmtime 39, no limit) is the
same order. At 16 voices the WCLAP uses 3.7 % of a core against 1.5 % native.

**The time limit costs about 30 %.** Any non-zero limit turns on Wasmtime epoch interruption for the whole
engine (and an epoch thread); the guest is slower even when the limit is never hit. Shipped default
here is the safe one (a guest stuck in a loop times out instead of hanging the audio thread). Dropping it
needs a decision: either accept a hang risk, or run init under a limit and `process()` without one
(separate engines), which is a bridge change.

**Decision (steering 2026-10-26, closed bead synthis-et4.11): keep the per-call limit at 60000 ms.**
A hung guest taking down the DAW is worse than the ~30 % more CPU the limit costs (Surge 2.5x vs
1.7x native per voice, above). Rejected options:

- **Drop the limit** (accept the hang risk). A guest stuck in a loop would hang the audio thread —
  and with it the DAW — for as long as the host lives. Rejected.
- **Init under a limit, `process()` without one** (two engines). Recovers the ~30 %, but the bridge
  would need two Wasmtime engines: one with epoch interruption for init, one without for `process()`.
  Later optimisation, not now. Rejected for now.

`WclapEngine::timeLimitMs()` is pinned to 60000 with a comment pointing here; the
`KROMOSYNTH_WCLAP_TIME_LIMIT_MS` override stays for measurement only.

## Build size (Release, arm64, same flags)

| | OFF (default) | ON | change |
|---|---|---|---|
| VST3 binary | 42,131,408 B | 42,704,256 B | **+572,848 B (+1.4 %)** |
| AU binary | 42,058,872 B | 42,618,280 B | +559,408 B |
| bundle `du` (VST3) | 41,188 KB | 41,748 KB | +560 KB |

Small because Wasmtime is already linked for `FaustEngine`; the bridge, `WclapEngine` and the vendored
webview-gui add about 0.55 MB. The 6.8 MB `module.wasm` is data, installed separately, and is not in
the bundle. The GPL-3.0 Surge XT bundle is never linked in; the bridge is BSL-1.0.

## Not done

- **UI.** The bridge's webview GUI (`ui/ui.wasm`) is linked (its `webview-gui` code) but nothing opens
  it; the plugin's editor is unchanged. "Its own UI inside the plugin" remains unproven.
- **Real DAW.** Everything above is a headless processor. No Reaper/Logic session reopen, no auval.
- **Acquisition.** `ArtifactAcquisition` does not recognise `instrument.wclap/...` in an archive, so a
  wclap entry cannot be downloaded into the library yet; `resolve()` finds one placed by hand.
- **Editor list**: `PluginEditor` reports `kind: "wclap"` to the shell page; the page has no badge for it.
- **Windows**, **iOS** (Wasmtime JIT is not permitted there), universal binaries.
- Parameters are not exposed as host automation; MIDI and clap.state only.
- Signing and packaging untouched.
