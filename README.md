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

**AGPL-3.0-or-later.** The full text is in [`LICENSE`](LICENSE). Copyright (C) 2026
Björn Þór Jónsson. Source: <https://github.com/synth-is/kromosynth-plugin>;
each released installer has a matching tag (`v<version>`).

- Code vs content: the AGPL covers the code in this repository. Evolved
  Faust `.dsp`/`.wasm` artefacts and renders are not AGPL: Synth.is claims no
  copyright in them and they may be used freely, as is (see
  [`docs/ARTEFACT_LICENCE_DECISION.md`](docs/ARTEFACT_LICENCE_DECISION.md)).
- Third-party components and their licences (JUCE under its AGPLv3 option,
  sfizz, wasmtime, wasm3, dr_libs, ...) are listed in
  [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).
- The "Synth.is" name, logo and app icon are not licensed by the AGPL.
