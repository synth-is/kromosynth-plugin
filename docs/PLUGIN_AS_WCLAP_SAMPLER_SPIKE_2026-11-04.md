# Plugin as a WCLAP: CPPN+DSP sounds as SFZ, sfizz inside the module (synthis-et4.18)

Run 2026-11-04, unattended, on the Mac Studio. Follows `PLUGIN_AS_WCLAP_2026-10-27.md` (the skeleton, synthis-et4.9),
section 4 ("sfizz is plain C++ and should build for wasm with its plugin targets off, no threads, samples read from
memory; not attempted"), and is the sibling of the Faust spike (`PLUGIN_AS_WCLAP_FAUST_SPIKE_2026-11-04.md`).

## 0. Result in one paragraph

It works, and it is fast. sfizz 1.2.3 builds for wasm32 with the kit's Emscripten (3.1.51, legacy exceptions, and
4.0.20, exnref) after **one small source patch** (two files, 40 lines, all behind a define: no threads, and an
`atomic_queue` CPU check that did not know wasm) and **one link change** (WasmFS instead of the kit's empty
read-only file system). The plugin's own `src/SamplerEngine.cpp`, **unchanged**, runs inside `module.wasm` and plays
a generated test SFZ whose bytes are compiled into the module and written into the module's in-memory file system
at activate. Through the headless Node host, key 69 (the sample's own pitch) **is the source sample times one gain,
to -140 dB** (float rounding), key 81 is every second source sample, to -138 dB, and pitch is right at keys 57, 69
and 81 (220.00, 439.97, 879.87 Hz). Against **native sfizz** rendering the same SFZ, the module agrees to
**-128 to -133 dB** after a constant 0.0033 dB gain. Cost: **0.70 ms per voice-second**, flat from 16 to 1024 voices,
so about **1,400 voices in real time** in one thread on an M4 Max under load (about 330 at the plugin's 4x
headroom rule); one instance with this instrument uses **3.98 MB of heap** in a 32 MB linear memory. Module
**1,459,693 bytes** (576 KB gzipped). Recommendation (section 8): this is the substrate to put in a WCLAP first.

## 1. What was built

All new code is behind `-DSYNTHIS_WEBCLAP_TARGET=ON` (default OFF) or in scripts the gate does not run. No file
under `src/` changed (unlike the Faust spike), and `third_party/sfizz` is not touched (it is copied, then patched).

| File | What |
|---|---|
| `webclap/emscripten/SamplerProcessor.cpp` (new) | A `juce::AudioProcessor` around `SamplerEngine`. `prepareToPlay` (= CLAP activate) writes the embedded files to `/synthis/` in WasmFS, then `engine.prepare()` and `engine.load(File("/synthis/test.sfz"))`, exactly as the native plugin loads an artefact from disk. One `Gain` parameter. Prints the load result, heap use and memory size, and `voices: N` whenever sfizz's active voice count rises. Plugin id `is.synth.plugin.sampler-spike`. |
| `webclap/emscripten/sfizz/CMakeLists.txt` (new) | Copies `third_party/sfizz` into the build tree (its `external/` linked, except `atomic_queue`, copied), applies the patch, sets sfizz's options and adds it. Only reached with `SYNTHIS_WEBCLAP_PROCESSOR=sampler`. |
| `webclap/emscripten/patches/sfizz-1.2.3-no-threads.patch` (new) | The patch (section 2.2). |
| `webclap/emscripten/StandaloneWasmfs.cpp` (new) | Replaces the kit's `juce_webclap_standalone.cpp` for this processor (section 2.3). |
| `webclap/emscripten/EmbedFiles.cmake` (new) | Writes a set of files into a generated header (`synthis_files::kFiles[]`). |
| `webclap/emscripten/CMakeLists.txt` | `SYNTHIS_WEBCLAP_PROCESSOR=skeleton\|faust\|sampler` (default `skeleton`, unchanged). `sampler` builds into `web/synthis-sampler.wclap`. |
| `webclap/CMakeLists.txt` | Third custom target `synthis_webclap_sampler` (own build dir `webclap-sampler`). |
| `scripts/webclap-sampler-data.mjs` (new) | Generates the test instrument (section 3). |
| `scripts/webclap-sampler.sh`, `scripts/webclap-sampler-check.mjs` (new) | `build`, `check`, `voices`, `native`. Not in the gate. |
| `scripts/webclap-sampler-native.cpp` (new) | Native reference: the same SFZ through native sfizz (the gate build's own static libraries), as raw float32, plus a native voice-cost timer. Compiled by `webclap-sampler.sh native` with `c++`; not part of any CMake build. |

## 2. sfizz for wasm: exact flags and patches

### 2.1 Flags

Toolchain: the kit's, `~/wclap-native/emsdk` = **Emscripten 3.1.51** (legacy exceptions); also
`~/wclap-native/emsdk-4` = **4.0.20** with `-DWEBCLAP_EXNREF=ON`. Kit: `~/wclap-native/open-webclap` at **a2e8984**
(the task names 0833f5b; the checkout is three commits on, as the Faust spike found; used as found).

sfizz cache options (`webclap/emscripten/sfizz/CMakeLists.txt`), as `cmake/Sfizz.cmake` does natively except where
marked:

```
SFIZZ_SHARED=OFF  SFIZZ_RENDER=OFF  ENABLE_LTO=OFF  SFIZZ_JACK=OFF  SFIZZ_TESTS=OFF  SFIZZ_DEMOS=OFF
SFIZZ_DEVTOOLS=OFF  SFIZZ_BENCHMARKS=OFF  SFIZZ_USE_SNDFILE=OFF (dr_libs, never the LGPL libsndfile)
SFIZZ_GIT_SUBMODULE_CHECK=OFF   (the copy has no .git)
USE_LIBCPP=OFF                  wasm: no -stdlib flags; Emscripten's libc++ is the only one
PROJECT_SYSTEM_PROCESSOR=wasm32 wasm: Emscripten's toolchain reports CMAKE_SYSTEM_PROCESSOR x86, which would add
                                -msse2 and -mavx; wasm32 matches none of sfizz's x86/arm branches -> scalar SIMD helpers
CMAKE_CXX_STANDARD=14           as natively (sfizz and its abseil at their own standard)
```

There are no `SFIZZ_LV2/VST/AU` options at 1.2.3 (the plugin wrappers moved to sfizz-ui; `cmake/Sfizz.cmake` says
so); the task's list is satisfied by the above.

Compile options on every sfizz target (directory-wide in that subdirectory only):
`-msimd128 -fwasm-exceptions` (the kit's `JUCE_WEBCLAP_FLAGS`; plus `-sWASM_LEGACY_EXCEPTIONS=0` in the exnref
form) `-O3 -Wno-unused-function -Wno-unused-parameter -Wno-deprecated-declarations`, and `-DSFIZZ_NO_THREADS=1`.
sfizz adds its own `-ffast-math` and `SFIZZ_ENABLE_RELEASE_ASSERT=1` (unconditional upstream, as natively).
`-Wno-error=missing-template-arg-list-after-template-kw` per target when the compiler knows it (the emsdk-4 clang
does; the 3.1.51 clang does not and does not need it), as `cmake/Sfizz.cmake`.

Module link options: the kit's `JUCE_WEBCLAP_DSP_LINK_OPTIONS` (`-sSTANDALONE_WASM=1 --no-entry
-sINITIAL_MEMORY=32MB -sSTACK_SIZE=1MB -sALLOW_MEMORY_GROWTH=1 ...`) **plus `-sWASMFS=1`**. No `-pthread`
anywhere (checked in the generated `flags.make`; `find_package(Threads)` succeeds on Emscripten's stubs without
adding a flag). The finished module imports only WASI: `random_get clock_time_get fd_write fd_read proc_exit
environ_sizes_get environ_get` (no `env` imports; `wclap-host-node` answers the last three, `fd_read` with ENOSYS).

### 2.2 The patch (`webclap/emscripten/patches/sfizz-1.2.3-no-threads.patch`)

Applied with `patch -p1` to the copy; everything in sfizz's sources is behind `SFIZZ_NO_THREADS`.

1. **`src/sfizz/FilePool.h/.cpp`: no threads.** `FilePool` starts two `std::thread`s in member initialisers
   (dispatch, garbage) and a global `ThreadPool` in its constructor. Without `-pthread`, Emscripten's
   `pthread_create` fails and `std::thread` throws, so an `sfz::Sfizz` could not even be constructed. With the
   define: no threads and no pool; **RAM loading forced on** (`loadInRam = true`: every sample is read whole at load,
   nothing ever streams, so the loader queue is never used); `setRamLoading(false)`, which `Synth::clear()` calls on
   every load, is ignored; the garbage pass frees inline. This is the "background loading made synchronous" route.
   sfizz's own `hint_ram_based=1` opcode would not do it alone (threads are still created, and the opcode's `case`
   falls through into `hint_stealing` for lack of a `break`, Synth.cpp:441).
2. **`external/atomic_queue/include/atomic_queue/defs.h`: wasm is "Unknown CPU architecture."** It defines
   `spin_loop_pause()` for x86 and ARM only and `#error`s otherwise. Added a `__wasm__` branch with an empty pause
   (single-threaded: nothing spins). This one is not behind the define (it is a compile error without it).

### 2.3 The file system: WasmFS, and the kit's stub

The task asks for Emscripten's MEMFS. MEMFS is part of Emscripten's **JS** runtime, which a WCLAP (`STANDALONE_WASM`,
no JS glue) does not have. Its standalone equivalent is **WasmFS** (`-sWASMFS`): the file system is C++ inside the
module, its default root is an in-memory backend, and it imports nothing beyond WASI (checked first on a 10-line test
module, then on the real one). `fopen`/`fwrite` on `/synthis/...` and sfizz's and `juce::File`'s reads then just work.

What broke: the kit's `juce_webclap_standalone.cpp` (which every DSP module links to stay free of `env` imports)
defines **every file syscall** as an empty read-only file system (`openat` ENOENT, `mkdirat` EROFS, ...). WasmFS
defines the same `__syscall_*` functions, and wasm-ld stops with 17 `duplicate symbol` errors. Not
`-sFORCE_FILESYSTEM` (that pulls in WasmFS's JS-backed backends: five `env._wasmfs_jsimpl_*` imports). Fix: the
sampler links `webclap/emscripten/StandaloneWasmfs.cpp` instead, which keeps the kit file's two other definitions
(`emscripten_notify_memory_growth`, `getentropy` over WASI `random_get`) and leaves the syscalls to WasmFS. Skeleton
and Faust builds are unchanged (they still link the kit's file).

Build time: a fresh build directory (sfizz, abseil, JUCE) is **33 s** on the Studio (`cmake --build build/wc-et418
--target synthis_webclap_sampler`, 4.4 cores busy), the exnref form 43 s; a rebuild after a processor change is the
link, a few seconds.

## 3. The bundled test data

`scripts/webclap-sampler-data.mjs` generates, at build time, into `build/webclap-sampler/data/` (nothing binary in
git):

- `tone.wav`: 48 kHz, mono, 16-bit PCM, 1.000 s, **96,044 bytes**. A4 (440 Hz) + 0.5 x 2nd + 0.25 x 3rd harmonic,
  5 ms linear fade-in, exponential decay (0.4 s), 20 ms linear fade-out to exactly 0. Integer maths on a formula:
  deterministic, not a recording, not a user's or the platform's instrument.
- `test.sfz` (234 bytes): `<region> sample=tone.wav pitch_keycenter=69 lokey=0 hikey=127 amp_veltrack=0
  loop_mode=one_shot`.

Embedded total **96,278 bytes** (limit 200 KB). `EmbedFiles.cmake` writes them into `gen/synthis_sampler_files.h`;
`SamplerProcessor` writes them to `/synthis/test.sfz` and `/synthis/tone.wav` at activate.

## 4. Render, and the check

Host: `kromosynth-burst/packages/wclap-host-node` (read, not changed), Node **v22.23.1**, 48 kHz, block 512, a
CLAP note at velocity 0.8 held for the sample's length / step + 0.1 s. `bash scripts/webclap-sampler.sh check`:

```
sampler: loaded, 1 regions, 64 voices, prepare 0.14 ms, load 0.15 ms, tail query 0.03 ms, 48000 Hz, block 512,
  files written; heap in use 1404328 -> 1501096 (files) -> 3975856 (loaded) B, linear memory 33554432 B
key 69: 52800 frames in 6 ms (189x real time), peak 0.1632, L-R max 0, f0 439.97 Hz (want 440.00, 0.006 %)
  vs source: first block gain 0.265691, lag 0, residual -147.6 dB, max |diff| 2.22e-8; rest gain 0.265691, lag 1, residual -140.1 dB, max |diff| 2.26e-8
  vs native sfizz: gain 1.000384, lag 0, residual -128.5 dB, max |diff| 3.50e-8
key 81: 28800 frames, peak 0.1629, L-R max 0, f0 879.87 Hz (want 880.00, 0.015 %)
  vs source read at step 2: first block ... residual -148.6 dB; rest gain 0.265691, lag 1, residual -137.6 dB, max |diff| 1.82e-8
  vs native sfizz: gain 1.000384, lag 0, residual -133.4 dB
key 57: 100800 frames, peak 0.1632, L-R max 0, f0 220.00 Hz (want 220.00, 0.002 %)
  vs source read at step 0.5 (not judged): residual -63.0 / -63.1 dB against LINEAR interpolation
  vs native sfizz: gain 1.000384, lag 0, residual -130.2 dB
check: ok
```

**The check, as stated** (in `scripts/webclap-sampler-check.mjs`; exit 1 on any failure):

- not silent: peak > 0.05; left and right identical (mono sample, centre pan);
- pitch: the first autocorrelation peak over the first 0.25 s gives 440 x 2^((key-69)/12) Hz within 0.5 %;
- **key 69 equals the source sample**: least-squares gain and best lag (-64..64) of the source against the output;
  residual energy relative to the output's below -60 dB; **key 81** the same against every second source sample
  (step exactly 2.0, no interpolation between source samples). Fitted in two parts, the first host block (512
  frames) and the rest, whose gains must agree to 1e-4 (they agree to 6 digits);
- if native renders exist (`webclap-sampler.sh native`): each key against native sfizz's render of the same SFZ,
  residual below -55 dB after a least-squares gain.
- Key 57 (step 0.5) falls between source samples, so its exact values belong to sfizz's interpolator; judged on
  pitch and against native only. Its -63 dB against linear interpolation is printed, not judged.

Two findings from making the check honest:

1. **sfizz repeats one frame at a voice's first block boundary.** The first 512 frames are the source at lag 0, all
   later frames at lag 1 (at key 81 too: the repeated frame is an output frame). Native sfizz does exactly the same
   (lag 0 then lag 1, same residuals), so it is sfizz 1.2.3's behaviour, not the module's. Inaudible at this level
   but real; it is why the fit is in two parts.
2. **Freewheeling changes the interpolator.** The first native reference used `enableFreeWheeling()` (synchronous
   loading, as `tools/sfz-probe` does). It differed from the module by -63.6 dB, deviated from the source by the
   same, and cost 3x more CPU: freewheeling switches sfizz to its *freewheeling sample quality*. `SamplerEngine`
   never freewheels (its header says why), and neither does the WCLAP. With the native reference set to the live
   quality, native and module agree to -128 dB. **`sfz-probe`'s render numbers are therefore at the freewheeling
   quality, not what the plugin plays**; a learning for whoever uses it to compare against the plugin.

The remaining **constant gain of 1.000384** (+0.0033 dB) between module and native is not velocity (native renders
at velocities 101, 102 and 127 are identical; `amp_veltrack=0`); likely the native NEON SIMD helpers against the
module's scalar ones (`PROJECT_SYSTEM_PROCESSOR=wasm32`) or fast-math in a gain/pan computation. Not found; harmless.

The same check passes on the **exnref** build (Emscripten 4.0.20, 1,584 `try_table`, no legacy `try`), with
identical fingerprints (`51d5cad99b561ff6`, `9a91d21519dc905e`, `5d4bb0a534e3a079`).

## 5. Numbers

Machine: **Mac Studio, Apple M4 Max, 14 cores (10P + 4E), 36 GB**, macOS (Darwin 25.6.0), Node v22.23.1.
Load during the measurements: load average **10.5 to 11.3**; busy processes were the live platform services
(`quality_clap_prompts` ~226 % CPU, `clap_service` ~122 %, `kromosynth-render-socket-server` ~100 %). Nothing was
stopped or restarted. The render is single-threaded (the module has no threads).

### 5.1 Module size

| Build | `module.wasm` bytes | gzip -9 |
|---|---|---|
| Skeleton (sine), 3.1.51 | 249,904 | |
| Sampler spike, 3.1.51 legacy (final, `scripts/webclap-sampler.sh build`) | **1,459,693** | 575,943 |
| Same source through the native tree (`build/wc-et418`, `--target synthis_webclap_sampler`) | 1,460,027 | |
| Sampler spike, 4.0.20 exnref | 1,441,280 | 571,534 |
| Sampler, 3.1.51, after `wasm-opt -Oz` (not shipped) | 1,451,336 | 576,216 |

Of the ~1.21 MB over the skeleton, 96 KB is the embedded test instrument; the rest is sfizz (with abseil,
st_audiofile's WAV/FLAC/MP3/AIFF/WavPack readers, pugixml, kissfft, its effects), WasmFS and `SamplerEngine`. Not
broken down further. `-Oz` buys under 1 % (emcc's own `wasm-opt` pass has already run).

### 5.2 Speed and polyphony

`bash scripts/webclap-sampler.sh voices`: N notes held (keys 69, 68, ..., all at least 1 s long), 48 kHz, block 512,
0.5 s timed after a warm-up block, three fresh modules per row. Above 64 (sfizz's default polyphony per instance),
several plugin instances in one module and one thread. **Every instance reported all N notes as active sfizz
voices** (the check fails otherwise), so no voice was stolen or culled.

| Voices | ms per second of audio (3 modules) | Real-time factor | us per voice-second |
|---|---|---|---|
| 1 | 3.6 / 3.9 / 5.9 | 171 .. 278 | (fixed costs dominate) |
| 8 | 7.8 / 8.2 / 8.9 | 112 .. 128 | 976 .. 1115 |
| 16 | 12.6 / 12.7 / 12.8 | 78 | 790 .. 800 |
| 64 (1 x 64) | 44.6 / 44.8 / 44.8 | **22.3** | 697 .. 701 |
| 256 (4 x 64) | 177.6 / 178.6 / 179.7 | 5.6 | 694 .. 702 |
| 512 (8 x 64) | 351.0 / 359.3 / 360.1 | 2.8 | 685 .. 703 |
| 1024 (16 x 64) | 718.5 / 720.5 / 729.9 | **1.4** | 702 .. 713 |

- Cost is linear at **~0.70 ms per voice-second** from 16 voices up. **Polyphony limit in real time: about 1,400
  voices** (extrapolated from 1,024 at 1.4x; not run at the edge), **about 330** at the plugin's own 4x-headroom
  rule (`FaustEngine::capForRtf` divides by 4.2). sfizz's default of 64 voices per instance costs 4.5 % of one core.
- **Native sfizz for comparison** (same SFZ, live quality, `webclap-sampler.sh native`, best of 3): 1 voice 0.79 ms,
  8 voices 3.25 ms, 64 voices 26.3 ms per second of audio (~0.41 ms per voice-second). So the WCLAP is **~1.7x
  slower than native**, against the Faust spike's 34x. No bimodal instance speed was seen (the Faust spike's was).
- Single notes render at 190x (key 69) to 650x (key 57) real time in the check above.
- This is the cheapest case for sfizz (one region, no filter, no envelope opcodes, no effects). Generated
  artefacts carry `ampeg_*_oncc*` envelope CCs; filters or effects in a future artefact would cost more per voice.

### 5.3 Memory (wasm32 linear memory)

| What | Bytes |
|---|---|
| Linear memory at start (the kit's `INITIAL_MEMORY`) | 33,554,432 (32 MB); **never grew** for one instance |
| Heap in use after construction (JUCE, the processor, `sfz::Sfizz` with 64 voices) | 1,404,328 |
| + the two files written into WasmFS | 1,501,096 (+96,768: the file bytes, kept in memory) |
| + `prepare` and `load` (decoded sample, voice buffers for block 512) | **3,975,856** (+2,474,760) |
| 16 instances in one module | linear memory 61.1 MB (grows from 12 instances on); ~3.6 MB per instance |
| Max linear memory (the kit's link line) | 2 GB (`--max-memory=2147483648`) |

Peak for this instrument: **3.98 MB of heap in a 32 MB memory**. For a real instrument, the derived (not measured)
scaling: RAM loading keeps every sample decoded as float32 (frames x channels x 4 bytes; 2x a 16-bit WAV) **and**
the WasmFS copy of the file (1x, unless deleted after load: untested). The artefacts recorded in
`STAGE0_FINDINGS.md` are 15 MB (42 regions) and 62 MB (42 samples, velocity layers and round robins); for 16-bit WAVs
that is roughly 45 MB and 190 MB of linear memory per instance. Under the 2 GB ceiling, but heavy for a browser tab
with several instances; streaming is gone with the threads (section 7).

### 5.4 Start-up

`WclapHost.load` (compile + instantiate) 2-4 ms (warm; 20 ms on the first); `createPlugin` **~16.5 ms** (the sfizz
constructor; the skeleton's is 0.7 ms); `activate` (write files, prepare, load, tail query) **~1 ms**, of which sfizz's
prepare 0.14, load 0.15 and the tail query 0.03 ms (timed inside with the host's real clock).

## 6. How a page would hand the module an instrument (not built)

The module never reads a host file system; a page has to push bytes in. The route that fits the kit as it is:

1. The plugin defines `juce::webclap::createPageExtension()` (`juce_webclap_clap.h`) returning a `PageExtension`
   whose `receive(processor, type, payload, size)` handles two port frame types (`protocol::firstPluginType` = 64 and
   up, `juce_webclap_protocol.h`; the frame header is u8 type, u8 flags, u16 reserved):
   - `64 fileChunk`: u16 name length, name (e.g. `samples/c4.wav`), u32 offset, u32 total size, bytes. Written with
     `fopen`/`fseek`/`fwrite` into WasmFS under `/instruments/<id>/`. Chunked because the page-to-module path is a
     webview message per frame (size limits unknown per host; e.g. 256 KB chunks).
   - `65 loadSfz`: path of the `.sfz` in WasmFS. The extension calls `SamplerEngine::load()` between two `process`
     calls (the module is single-threaded and the kit pumps page frames from the plugin's calls, so no audio call can
     be in flight: that is SamplerEngine's "OFF" contract for free), then answers with a frame carrying the
     `LoadResult` (ok, regions, detail).
2. The page side sends them with the kit's `ProcessorBridge::sendPluginFrame` (`juce_webclap_bridge.h`) after
   fetching the artefact (the acquisition that is in `ArtifactAcquisition`/`AcquisitionRunner` natively moves to the
   page, as the skeleton note's section 6 says).
3. State: the instrument id (not its bytes) goes in `getStateInformation`, so a host project reopens and the page
   fetches again. Putting the bytes in the CLAP state would make the project self-contained at the cost of state
   size (tens of MB). A decision for later.
4. Memory: after a successful load, the WasmFS copies could be deleted (sfizz holds the decoded samples), halving the
   footprint; whether sfizz ever re-reads a file after load with RAM loading forced on (sample-rate change,
   `setPreloadSize`) must be checked first.

## 7. What broke, and what to know

1. Threads: `FilePool` cannot be constructed without them (section 2.2). Patched; consequence: **no streaming**, every
   sample fully in memory.
2. `atomic_queue` rejects wasm as an unknown CPU (section 2.2). Patched.
3. The kit's standalone shim and WasmFS both define the file syscalls (section 2.3). Replaced the shim for this
   processor. Worth raising upstream: a kit option for a module that wants a writable in-memory file system.
4. The first configure failed because sfizz's `src/CMakeLists.txt` `configure_file`s `scripts/doxygen/Doxyfile.in`,
   so `scripts/` must be in the copy.
5. Emscripten reports `CMAKE_SYSTEM_PROCESSOR` x86; left alone, sfizz would compile its SSE/AVX paths for wasm.
6. Not a problem, noted: `wclap-host-node` answers `environ_get`/`environ_sizes_get`; a stricter host might not. Both
   come from abseil/libc start-up reading the environment.

## 8. Recommendation

**Put the sampler substrate in the WCLAP first.** It builds with a small, contained patch, runs the plugin's own
engine unchanged, is exact against the source sample and native sfizz, and has headroom for hundreds of voices
(~1.7x native cost, against 34x for Faust on wasm3). Its real limit is memory, not CPU: no streaming, so an
instrument lives fully decoded in linear memory.

Next steps, in order:
1. The page path of section 6 (port frames into WasmFS, load between process calls) with a real artefact fetched by
   a page, in the kit's browser test host; measure a 15 MB and a 62 MB artefact's memory and load time.
2. Delete the WasmFS copies after load (and prove sfizz does not re-read), halving the footprint.
3. Compile `PluginProcessor` itself with the sampler path for wasm (the skeleton note's step 1), so the WCLAP has the
   real parameters (envelope CCs), trim and state rather than this spike's processor.
4. Upstream: offer sfizz a `SFIZZ_NO_THREADS` option and the `__wasm__` atomic_queue branch (sfizz is barely
   maintained; liquidsfz remains the fallback named in `cmake/Sfizz.cmake`).

## 9. Reproduce, the gate, the default build

```
# through the native tree (what the gate never does)
cmake -S . -B build/wc -DSYNTHIS_WEBCLAP_TARGET=ON
cmake --build build/wc --target synthis_webclap_sampler      # -> build/wc/webclap-sampler/web/synthis-sampler.wclap

# or directly
bash scripts/webclap-sampler.sh build      # generate the test data, Emscripten 3.1.51, legacy exceptions
bash scripts/webclap-sampler.sh native     # native sfizz references (needs build/verify-nocopy, i.e. scripts/build.sh)
bash scripts/webclap-sampler.sh check      # the check of section 4
bash scripts/webclap-sampler.sh voices     # section 5.2
SYNTHIS_WEBCLAP_EMSDK=~/wclap-native/emsdk-4 SYNTHIS_WEBCLAP_EXNREF=ON \
  SYNTHIS_WEBCLAP_BUILD_DIR=$PWD/build/webclap-sampler-exnref bash scripts/webclap-sampler.sh build
```

- **Default OFF configure and build: run.** `./scripts/gate.sh` (whose `scripts/build.sh` configures `build/verify-nocopy`
  with `SYNTHIS_WEBCLAP_TARGET:BOOL=OFF`, checked in its CMakeCache) exited 0 at the end of this run: build, acquire
  self-test, Faust null test and processor-diff (`identical`) all green, 54 s.
- The two module sizes differ by 334 bytes; not chased (build-directory paths compiled into the module are the
  likely cause). Both pass `webclap-sampler.sh check`.
- Option ON: `cmake -S . -B build/wc-et418 -DSYNTHIS_WEBCLAP_TARGET=ON` configured, and
  `--target synthis_webclap_sampler` built the bundle from scratch in 33 s.
- **Nothing added to the gate.** The gate needs no emsdk.

## 10. Not verified

- **No browser, no openDAW, no webclap.github.io, no Wasmtime-based host.** Node 22.23.1 only, both exception forms.
- **No real artefact.** Only the generated one-region instrument; real CPPN+DSP artefacts (many regions, velocity
  layers, round robins, `ampeg_*_oncc*` envelope CCs, possibly 24-bit or stereo samples) were not loaded, so their
  memory, load time and per-voice cost are projections (section 5.3), not measurements.
- **The page path (section 6) is not built**; nothing pushes bytes in at run time.
- **Real-time behaviour in a real audio callback**: offline throughput in one Node thread only; no AudioWorklet, no
  deadline misses measured. The 1,400-voice figure is extrapolated from 1,024 voices at 1.4x.
- **Peak memory of a large instrument**: derived, not measured. Whether deleting the WasmFS copies after load is safe:
  untested.
- The patched `setRamLoading` path with an SFZ that sets `hint_ram_based=0`, sample-rate changes after load,
  `SamplerEngine::setEnvelope` and the chunked render path were not exercised in the module.
- The +0.0033 dB gain against native sfizz is unexplained.
- **The real plugin class** is still not compiled for wasm; this is a spike processor around `SamplerEngine`.
- Other machines (laptop, iOS-class devices): not measured.
- The bundle carries no licence files yet (sfizz BSD-2-Clause, its abseil Apache-2.0, dr_libs public domain/MIT-0,
  pugixml MIT, kissfft BSD-3-Clause, WavPack BSD-3-Clause would join the list; `bundle-license.sh` unused).
