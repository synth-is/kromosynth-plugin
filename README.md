# kromosynth-plugin

Synth.is as an audio plugin. macOS AU + VST3.

**Stage 0** of `WP_SYNTHIS_PLUGIN.md` §7: the smallest thing that can kill the
two riskiest assumptions behind the work package.

## What is keepable and what is not

| | |
|---|---|
| **Keepable** | `CMakeLists.txt`, `cmake/`, `CMakePresets.json`, submodule vendoring, target configuration, `.github/workflows/` |
| **Disposable** | everything in `src/`. No state, no parameters, no abstractions. Each file says so at the top. |

Do not build on `src/`. Build beside it and delete it.

## Build

```sh
bash scripts/check-deps.sh          # preflight: toolchain, hosts, disk, origin
```

Then follow **`docs/BUILD_STEPS.md`** step by step. Short version:

```sh
git init && git add . && git commit -m "Stage 0"
git submodule add https://github.com/juce-framework/JUCE.git third_party/JUCE
git -C third_party/JUCE checkout 8.0.x     # a TAG, never a branch
cmake --preset stage0                       # <- prints the JUCE licence verdict
cmake --build --preset stage0
auval -v aumu Krm0 Sthi
```

The default preset uses **Unix Makefiles**, so nothing beyond the Xcode
Command Line Tools and cmake ≥ 3.24 is required. `stage0-ninja` and
`stage0-xcode` exist if you want them.

`COPY_PLUGIN_AFTER_BUILD` installs to `~/Library/Audio/Plug-Ins/`.
Runtime logs land at `~/Library/Logs/Synth.is/stage0-<HostName>.log`.

## Task 1 — the webview

A `WebBrowserComponent` on `https://synth.is` (origin taken from
`kromosynth-auth/.env.example`, `APP_PUBLIC_URL`). No audio, no parameters, no
state, no native integration, no resource provider.

Findings go in `docs/STAGE0_FINDINGS.md`. Runtime logs land at
`~/Library/Logs/Synth.is/stage0-<HostName>.log`, one per host.

## Task 2 — sfizz

Gated on Task 1. `cmake --preset sampler` once `third_party/sfizz` exists.
BSD-2-Clause, static, `dr_libs` backend, no libsndfile.

## Licence

**AGPL-3.0-or-later.** The full text is in [`LICENSE`](LICENSE); every
first-party source file carries `SPDX-License-Identifier: AGPL-3.0-or-later`
and a copyright line. Copyright (C) 2026 Björn Þór Jónsson. A few build and
test scripts (`scripts/gate.sh`, `scripts/null-test.sh`,
`scripts/processor-diff.sh`, `tools/processor-probe/diff_wav.py`) and
`cmake/JuceLicenceCheck.cmake` carry no header; they are first-party and
covered by `LICENSE` all the same.

- It relies on **JUCE's AGPLv3 option** (JUCE 9.0.1 is dual-licensed AGPLv3 /
  commercial). `cmake/JuceLicenceCheck.cmake` verifies this at configure time
  and fails the build if the JUCE header stops mentioning AGPL.
- **libfaust is not linked.** Faust instruments are pre-compiled to WebAssembly
  elsewhere and only the wasm bytes are executed (wasmtime, or wasm3 on iOS),
  so the Faust compiler's GPL does not enter this binary.
- The iOS app and its AUv3 extension ship under the AGPL too.
- Code vs content: the AGPL covers the code in this repository. The licence of
  evolved Faust `.dsp`/`.wasm` artefacts and of any sample content is a
  separate matter, described (with what is still open) in
  [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md), as are the licences of
  JUCE, sfizz, wasmtime, wasm3, dr_libs and the rest. The "Synth.is" name and
  logo are not licensed by the AGPL.
