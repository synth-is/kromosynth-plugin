# Plugin as a WCLAP: Faust artefacts on wasm3 inside the module (synthis-et4.17)

Run 2026-11-04, unattended, on the Mac Studio. Follows `PLUGIN_AS_WCLAP_2026-10-27.md` (the skeleton, synthis-et4.9),
section 3, route B: compile wasm3 into the WebCLAP module, feed it the artefact `.wasm`, keep the artefact format
and the null test, and measure before investing.

## 0. Result in one paragraph

It works, and the audio is **bit-identical**. The plugin's own `src/FaustEngine.cpp`, built with `KROMOSYNTH_WASM3=1`
and the repo's vendored wasm3, compiles for wasm32 and runs inside `module.wasm`. Loaded in the headless Node host,
the artefact's Gate 0 plan replays with **0 of 480,000 samples different** from the platform reference
(`elite_08_deep_pluck_c23-12`; the same holds for the legacy and the exnref builds and for Node 22 and Node 24).
Across the whole Gate 0 archive the result is in section 4.2. It is **slow**. One voice runs at about **4x real time**
(3.1x to 4.6x), which is about **4.5x slower than native wasm3** and about **34x slower than the native plugin's
wasmtime/Cranelift** on the same genome. Voices in real time on an M4 Max under load: **3** (no headroom left);
by the plugin's own rule (keep 4x headroom, `FaustEngine::capForRtf`) **1 voice**. Recommendation (section 7):
keep wasm3 as the correctness path and stop optimising it; do **not** try the libfaust interpreter; spike next
on merging the artefact into the module at bundle time, which runs it at the host JIT's speed.

## 1. What was built

All new code is behind `-DSYNTHIS_WEBCLAP_TARGET=ON` (default OFF) except one refactor in `FaustEngine`, below.

| File | What |
|---|---|
| `webclap/emscripten/FaustProcessor.cpp` (new) | A `juce::AudioProcessor` around `FaustEngine`. MIDI notes go through FaustEngine's own voice path (8 voices). Three test controls (not genome parameters, which stay out): `Voices` (1..32), `Replay plan` (a note-on replays the artefact's `.plan` on one voice, exactly as `faust-probe`'s `runNullTest` does natively), `Measure` (runs `FaustEngine::measure()` once on one voice and prints it). Plugin id `is.synth.plugin.faust-spike`. |
| `webclap/emscripten/EmbedArtefact.cmake` (new) | Writes `<stem>.wasm/.json/.plan` into a generated header at configure time. A WCLAP has no file system: `wclap-host-node` gives no preopened directories and the kit links `STANDALONE_WASM`, so the artefact is compiled into the module. One artefact per build. |
| `webclap/emscripten/CMakeLists.txt` | `SYNTHIS_WEBCLAP_PROCESSOR=skeleton|faust` (default `skeleton`, unchanged). `faust` adds wasm3, a `juce_dsp` library, `src/FaustEngine.cpp`, the embedded artefact, and builds into `web/synthis-faust.wclap`. `SYNTHIS_WASM3_TAIL_CALL` (ON). |
| `webclap/CMakeLists.txt` | Second custom target `synthis_webclap_faust` (own build dir `webclap-faust`, so the skeleton bundle is not replaced). |
| `scripts/webclap-faust.sh`, `scripts/webclap-faust-check.mjs` (new) | `build`, `null`, `note`, `voices`, `measure`. Not in the gate. |
| `src/FaustEngine.h/.cpp` | **Refactor, compiled in the native build too:** `load(File, File, ...)` now reads the two files and calls a new `loadFromMemory(bytes, size, json, ...)`, which holds the old body. Needed because there is no file to load in a WCLAP. The gate's null test is IDENTICAL to the committed baseline after it (section 8). |

wasm3: the copy the repo already vendors and ships a licence for, `tools/ios-gate0/wasm3` (0.9.1-beta.1 with local
patches, **MIT**, Copyright (c) 2019 Steven Massey, Volodymyr Shymanskyy; already in `THIRD_PARTY_NOTICES.md`), with
the same source set as `cmake/Wasm3.cmake` (the WASI and tracer shims excluded, `d_m3HasWASI=0`). Nothing fetched.
The bundle's licence set grows by wasm3 (MIT) only; no `LICENSE`/`NOTICES.md` is written into the spike bundle yet
(the kit's `bundle-license.sh` is still unused, as for the skeleton).

The artefact: `elite_08_deep_pluck_c23-12` from the Gate 0 archive (`kromosynth-projects/.../faust-genome-substrate/
probe/out/gate0/`, the directory `scripts/null-test.sh` runs over), a deep pluck, 38,578-byte `.wasm` made by
faustwasm (Faust 2.84.3, `-lang wasm-i ... -single`), DSP struct 843,836 bytes, one output. Chosen because the iOS
Gate 0 README (`tools/ios-gate0/README.md`) lists it first, at 12.5x one voice under native wasm3 on an M1 Pro. Its references beside it: `.wasmtime-archive.f32` (the native
plugin's null-test reference, rendered on wasmtime, identical to the platform's) and `.wasm3-archive.f32`.

## 2. Build recipe

Kit: `~/wclap-native/open-webclap` at **a2e8984** (the skeleton note pinned 0833f5b; the checkout is three commits
on: the exnref option, a build-dir override and an OB-Xf link fix. Used as found, not changed). JUCE inside it is
8.0.12, not our 9.0.1.

```
# through the native tree (what the gate never does)
cmake -S . -B build/wc -DSYNTHIS_WEBCLAP_TARGET=ON
cmake --build build/wc --target synthis_webclap_faust        # -> build/wc/webclap-faust/web/synthis-faust.wclap

# or directly
bash scripts/webclap-faust.sh build                           # Emscripten 3.1.51, legacy exceptions
SYNTHIS_WEBCLAP_EMSDK=~/wclap-native/emsdk-4 SYNTHIS_WEBCLAP_EXNREF=ON \
  SYNTHIS_WEBCLAP_BUILD_DIR=$PWD/build/webclap-faust-exnref bash scripts/webclap-faust.sh build   # 4.0.20, exnref
SYNTHIS_FAUST_ARTEFACT=<dir>/<stem> bash scripts/webclap-faust.sh build                         # another artefact

bash scripts/webclap-faust.sh null      # replay the plan in wclap-host-node, compare with both references
bash scripts/webclap-faust.sh note      # key 60 through the voice path, writes build/webclap-faust/note60.wav
bash scripts/webclap-faust.sh voices    # 1..8 simultaneous voices, three fresh instances each
bash scripts/webclap-faust.sh measure   # FaustEngine::measure() inside the module (host on its real clock)
```

A fresh build directory (JUCE objects included) builds in about 10 s on the Studio; a rebuild for another artefact
is one translation unit and the link, a few seconds.

Things that broke on the way, all fixed in the files above:
1. **`init: [trap] stack overflow` on load.** wasm3's patched native-stack guard (`d_m3MaxNativeStack`, default
   8 MB - 128 KB) is larger than the kit's `STACK_SIZE=1MB`; on wasm32 the limit `sp - budget` wraps below address 0
   and every call traps. Set to 768 KB on the wasm3 target.
2. **`juce_dsp` is not in `juce_wasm_dsp`** (the kit has it only in its GUI library `juce_wasm`), and `juce_dsp.cpp`
   includes `juce_audio_formats`. Built both as one extra static library on top of `juce_wasm_dsp`. FaustEngine
   needs it for the FFT in `measure()`.
3. **UTF-8 in the DSP JSON** (Faust library metadata) narrowed into `char` in the generated header; the arrays are
   `unsigned char`.
4. **Tail calls are required for speed, not correctness.** wasm3's dispatch is `musttail`-threaded; on wasm32 that
   needs `-mtail-call` (the tail-call proposal, `return_call`; V8 in Node 22 has it). Without it the same module is
   bit-identical but **3.7x slower** (1.11x real time against 4.1x). `SYNTHIS_WASM3_TAIL_CALL=OFF` builds that form.
5. **`measure()`'s rtf read 0.00** under the host's default *virtual* WASI clock (time only advances with frames
   rendered, so none passes inside one `process()` call). The `measure` mode loads the host with `clock: 'real'`.
6. **`measure()` drives every loaded voice.** Measured with 8 voices loaded, its peaks read exactly 8x (8.7375 for
   1.0922). The native plugin always loads one voice before measuring (`PluginProcessor.cpp`, both paths), so this is
   a precondition, not a bug there; the spike's `Measure` drops to one voice first.

## 3. Module size

| Build | `module.wasm` bytes |
|---|---|
| Skeleton (sine), 3.1.51 (rebuilt today, unchanged from the skeleton note) | 249,904 |
| Faust spike, 3.1.51 legacy exceptions, tail calls (final, with `Measure`) | **534,303** |
| same, before `Measure` was added (measure()'s FFT/spectrogram code dead-stripped) | 512,854 |
| Faust spike, 4.0.20 exnref, tail calls | 540,193 |
| Faust spike, 3.1.51, no tail calls | 533,174 |

Of the ~284 KB over the skeleton, 47.8 KB is the embedded artefact (38,578 `.wasm` + 9,013 `.json` + 169 `.plan`);
the rest is FaustEngine, wasm3 (with its validator and exception-handling support) and `juce_dsp`. Uncompressed;
no `wasm-opt -Oz` pass, no gzip measured.

## 4. Render and comparison

### 4.1 The one artefact, through the headless host

Host: `kromosynth-burst/packages/wclap-host-node` (read, not changed), 48 kHz, block 128 (the plan's), plan replayed
on one voice from a note-on, 10 s:

```
faust: elite_08_deep_pluck_c23-12 loaded, 8 voices, 48000 Hz, block 128, struct 843836 B, gate=1
null vs wasmtime-archive.f32: compared 480000, peak ours 0.674630 ref 0.674630, maxAbsDiff 0, 0 samples differ  IDENTICAL
null vs wasm3-archive.f32:    compared 480000, peak ours 0.674630 ref 0.674630, maxAbsDiff 0, 0 samples differ  IDENTICAL
```

**Residual: none.** Not "within tolerance": every sample equal. Same result for the exnref build, the no-tail-call
build, and on Node 24.19.0. This is the comparison the null test makes natively (`faust_probe --plan --ref`), so the
WCLAP passes the same test against the same reference the native plugin's gate uses.

Through the voice path (MIDI key 60, velocity 0.8, 1.5 s held + 0.5 s tail, block 512): it plays; peak 1.0218,
RMS 0.2663, fingerprint `975af9cd59a400d2`, `build/webclap-faust/note60.wav`. Peak above 1 is expected: the spike
does not apply the trim (`setOutputGain`) or ceiling that the plugin derives from `measure()` (here trim 0.229,
-12.8 dB). **This path is not compared** against a native render (section 9).

`measure()` inside the module (48 kHz/128, one voice): peak **1.092192**, a4 1.092192, a5 0.595902, the same as the
native `faust_probe --measure` (wasmtime); c3 0.508247 against the probe's 0.508273. The probe's `--measure` is a
separate implementation in `tools/faust-probe/main.cpp`, not `FaustEngine::measure()`, so that c3 difference is
between two measuring functions, not evidence of a runtime difference (the plan replay above is the runtime test).

### 4.2 The whole Gate 0 archive

RESULTS_120

## 5. Speed, and voices in real time

Machine: **Mac Studio, Apple M4 Max, 14 cores (10 performance + 4 efficiency), 36 GB**, macOS (Darwin 25.6.0).
Node **v22.23.1** (the host's default; V8 12.4) unless stated; Node 24.19.0 is emsdk-4's.
Load during all measurements: load average **10 to 12**; the busy processes were the live platform services
(`quality_clap_prompts` ~158% CPU, two `clap_service` ~136% and ~89%, two `kromosynth-render-socket-server` 100%
each), so about 6 of 14 cores were taken. Nothing was stopped or restarted for the measurement. The render is single
threaded (the kit's module has no threads).

Single voice, the plan replay above (10 s of audio):

| Runtime | Real-time factor |
|---|---|
| Native plugin, wasmtime/Cranelift (`faust_probe --measure`, 3 runs) | **137.9, 137.9, 138.3** |
| Native wasm3, same vendored sources, `-O3` (`probe/gate0/host_wasm3.c`, 10 runs, 3 s) | **17.4 to 18.6** |
| WCLAP, wasm3 in wasm, 3.1.51 legacy, tail calls, Node 22 | **4.02, 4.03, 4.13, 4.22** |
| same, `measure()` inside the module (105 s of audio, real clock) | 4.15, 3.07 |
| WCLAP, 4.0.20 exnref, Node 22 | 3.92, 4.04, 4.14 |
| WCLAP, legacy or exnref, Node 24.19.0 | 4.61, 4.60 |
| WCLAP, no tail calls, Node 22 | 1.11, 1.12, 1.12 |

So wasm-in-wasm costs about **4.5x** on top of native wasm3 (17.9 / 4.0), and the WCLAP runs this genome about
**34x slower** than the native plugin. Native wasm3 being 7.7x behind wasmtime on this genome is within the archive's
recorded range (2.0x to 24.5x, median 5.56x).

Simultaneous voices (MIDI chord, keys 48, 50, ..., 48 kHz, block 512, steady state after a warm-up block, 1 s timed,
three fresh module instances per row, Node 22):

| Voices | ms per second of audio (3 instances) | Real-time factor | ms per voice |
|---|---|---|---|
| 1 | 234 / 249 / 323 | 3.10 .. 4.28 | 234 .. 323 |
| 2 | 481 / 506 / 637 | 1.57 .. 2.08 | 241 .. 319 |
| 3 | 729 / 754 / 961 | **1.04 .. 1.37** | 243 .. 320 |
| 4 | 980 / 1009 / 1318 | 0.76 .. 1.02 | 245 .. 330 |
| 6 | 1481 / 1955 / 1958 | 0.51 .. 0.68 | 247 .. 326 |
| 8 | 1880 / 1962 / 2596 | 0.39 .. 0.53 | 235 .. 325 |

- Cost is **linear in voices** (one wasm3 runtime per voice, as natively).
- **Voices in real time: 3** in every instance (worst 1.04x, i.e. no headroom for anything else in the audio
  callback); 4 only in a good instance. By the plugin's rule (`capForRtf`: rtf / 4.2), `measure()` inside the
  module returns **capAt4x = 1**. The native plugin's cap for the same genome is 32 (the maximum).
- **Per-instance speed is bimodal**: a fresh module instance runs at either ~235 or ~325 ms per voice-second and
  stays there. Not the pitch (each key 48..60 lands in either mode on different runs), not V8's Liftoff tier-up
  (identical with `--no-liftoff`), not the machine (native wasm3 is steady at 17.4 to 18.6). Cause **not found**;
  candidates are code or memory placement of the JIT-compiled interpreter. Real, and it costs a quarter of the
  throughput when it hits.
- Native wasm3 for comparison (same host_wasm3.c, 10 s): 1 voice 17.2x, 2 voices 8.85x, 4 voices 4.40x,
  8 voices 2.22x; 1 MB of wasm memory per voice.

This is a fast genome (the iOS Gate 0 README lists it first at 12.5x one voice on an M1 Pro under native wasm3).
Slower genomes in the archive will fit fewer; the per-artefact real-time factors are in section 4.2.

## 6. Exceptions forms

Both forms built and both **loaded and rendered in Node 22.23.1 without a flag**: the legacy build has 559 `try` and
no `try_table`, the exnref build 551 `try_table` and no `try` (counted with `wasm-dis`). The skeleton note assumed
Node 22 takes only the legacy form; this Node build takes both. Not tried in a browser or a Wasmtime-based host.

## 7. Recommendation

**Carry on with wasm3, but only as the correctness path, and do not try the libfaust interpreter.**

- wasm3 in the module does exactly what route B promised: same artefact bytes, same engine code, bit-identical
  output, the null test carried over unchanged. That is worth keeping as the reference and as a fallback that runs
  any artefact handed to the module at run time.
- As an instrument it is not enough: 1 voice at the plugin's headroom rule, 3 at the edge, on the fastest machine we
  have, on a fast genome. Optimising the interpreter will not close a 34x gap.
- The **libfaust interpreter** would be slower still (measured natively at 0.49x wasm3's throughput,
  `probe/gate0-interp`), needs the `.dsp` compiled at run time, and gives up bit identity. Not worth a spike.
- **Next spike: merge the artefact into the module at bundle time** (`wasm-merge`, as aa4.42 did for Surge XT),
  so the artefact's functions become part of `module.wasm` and the host's own JIT runs them. That keeps the artefact
  bytes (so bit identity is likely by construction, to be shown with this null test) at roughly the browser's wasm
  speed, at the price of one bundle per instrument, which the platform can build server-side when it builds the
  artefact. The math imports (`_powf` etc.) become calls into the module's own double-precision functions, as
  FaustEngine's tables already are. The third route in the skeleton note (the page instantiates the artefact with
  the browser's `WebAssembly` and streams audio in) remains the fallback if a merge cannot be made to work, but it
  is not WCLAP-pure.

## 8. The gate, and the default build

- **Default OFF configure and build: run.** `scripts/gate.sh` (which configures and builds with the option OFF via
  `scripts/build.sh`) after these changes: **green, exit 0**: build, acquire_probe selftest, null test
  **PASS 120 FAIL 0, IDENTICAL to the committed baseline** (this is what proves the `loadFromMemory` refactor changed
  no sample), processor-diff identical. The OFF tree's Makefile has no `synthis_webclap*` target.
- Option ON: `cmake -S . -B build/wc-et417 -DSYNTHIS_WEBCLAP_TARGET=ON` and `--target synthis_webclap_faust` built
  the bundle (512,852 bytes at that point, before `Measure`). The skeleton still builds and plays through
  `scripts/webclap-skeleton.sh` (249,904 bytes, RMS 0.3536, as before).
- **Nothing added to the gate.**

## 9. Not verified

- **No browser, no openDAW, no webclap.github.io, no Wasmtime-based host.** Node only (22.23.1 and 24.19.0).
- **The MIDI voice path is not compared** against a native render: only the plan replay (parameter writes on one
  voice) is null-tested. Voice allocation, stealing, envelope, forced release, the limiter and pitch compensation ran
  in the module but their output was not compared with the native plugin's.
- **The real plugin class** (`KromosynthAudioProcessor`) is still not compiled for wasm. The spike wraps
  `FaustEngine` in its own small processor; no trim, ceiling, macros, parameters, state or acquisition.
- **Dynamic artefacts.** One artefact is compiled into each build. Handing an artefact to a running module (from
  the page, through a port frame) is not built.
- **Real-time behaviour in a real audio callback.** The numbers are offline throughput in one Node thread; no
  AudioWorklet, no deadline misses measured. The bimodal instance speed is unexplained.
- Memory: not measured beyond the module's linear memory being 40 MB with 8 voices loaded.
- `wasm-opt` size optimisation, gzip size, start-up (instantiate + load) time: not measured.
- Other machines: Studio only; the iOS-class and laptop numbers will be lower.
- The bundle carries no licence files yet.
