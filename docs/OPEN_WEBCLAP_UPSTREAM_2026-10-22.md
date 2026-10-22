# open-webclap as upstream: patch queue, rebuilt Surge XT and OB-Xf, comparisons (synthis-aa4.95, 2026-10-22)

Decision (Bjorn, 2026-10-21): open-webclap (github.com/andremichelle/open-webclap, MIT) is the upstream of our WCLAP engine
builds. We build the bundles ourselves from a pinned commit and host them on synth.is (docs/WCLAP_SELF_HOSTING_2026-10-20.md);
our additions are a small patch queue. **Nothing was pushed, no pull request opened, no message sent.** The bundles are
GPL-3.0 as built and are not committed. Everything ran on the Mac Studio, macOS arm64, Node 22.23.1.

## 0. State

- Studio clone `~/wclap-native/open-webclap` (outside git): remotes `upstream` = andremichelle/open-webclap,
  `origin` = `https://github.com/synth-is/open-webclap.git` (Bjorn's fork, created 2026-10-21; **no push credential on the
  Studio, nothing pushed**). Branch `synthis/main` = upstream `0833f5b` + three commits:
  1. `583c4ce` `WEBCLAP_BUILD_DIR` overrides the build directory in `scripts/build-*.sh`.
  2. `f58f9a8` opt-in `-DWEBCLAP_EXNREF=ON` (`-sWASM_LEGACY_EXCEPTIONS=0`; JUCE-kit ports and the Surge XT port).
  3. `a2e8984` OB-Xf: link the editor module (`ui.wasm`) at `-O1` when `WEBCLAP_EXNREF=ON` (new in this run, section 2).
- Pin file: `scripts/wclap-pins/open-webclap.json` (upstream commit, patch list with the patches exported beside it in
  `scripts/wclap-pins/open-webclap-patches/`, emsdk/Binaryen versions, engine pins, bundle hashes). The public commit URL
  in it is a placeholder, valid **after the first push**; the source offer should link
  `https://github.com/synth-is/open-webclap/tree/synthis/main` once that exists.
- `scripts/wclap-exnref/open-webclap-exnref.patch` (aa4.90) is superseded by commits 1 and 2 of the queue (same change).
- Upstream moved on: `upstream/main` is 12+ commits ahead of the pin (shared test host `871ccc3`, coopiano port, a site
  Import page). Not adopted; the pin stays on the tree the builds below came from.

## 1. Tracking workflow (bumping the pin)

```sh
cd ~/wclap-native/open-webclap
git fetch upstream
git log --oneline 0833f5b..upstream/main -- ports/surge-xt ports/obxf modules scripts/fetch-deps.py   # what changed for us
git checkout synthis/main && git rebase upstream/main        # three small commits; resolve, re-run git format-patch
git format-patch --no-signature upstream/main..synthis/main -o <kromosynth-plugin>/scripts/wclap-pins/open-webclap-patches
# rebuild, new directories only (never a directory a running comparison reads)
source ~/wclap-native/emsdk/emsdk_env.sh   && WEBCLAP_BUILD_DIR=$PWD/build/NEW-surge-xt-legacy bash scripts/build-surge-xt.sh
source ~/wclap-native/emsdk-4/emsdk_env.sh && WEBCLAP_BUILD_DIR=$PWD/build/NEW-surge-xt-exnref bash scripts/build-surge-xt.sh -DWEBCLAP_EXNREF=ON
#   same for scripts/build-obxf.sh. Detach long builds (nohup ... &) and poll the log.
# fingerprint check against the bundles currently in use (read-only on both):
BURST=~/Developer/apps/synth.is/kromosynth-burst node scripts/wclap-upstream/compare.mjs surge \
    --ref <current surge-xt.wclap.tar.gz> --n 12 build/NEW-surge-xt-legacy/surge-xt.wclap.tar.gz build/NEW-surge-xt-exnref/surge-xt.wclap.tar.gz
# then update scripts/wclap-pins/open-webclap.json (commit, patch list, hashes) and, after the first push, git push origin synthis/main
```

Rules: a legacy and an exnref build cannot share a build directory; the exnref build needs `emsdk-4`; Surge XT is
bit-identical across everything below, so a bump that changes its fingerprint is a real change to investigate; OB-Xf is
**not** comparable across compilers (section 3), so for OB-Xf compare legacy against its own translated form
(section 4) and only gate on same-build determinism.

## 2. Builds (new directories `build/pin-*`; no bundle burst uses was touched)

| Bundle | Emscripten | Build time (log) | module.wasm | tar.gz | try_table / legacy try (module) |
|---|---|---|---|---|---|
| surge-xt legacy | 3.1.51 | 253 s | 6 796 075 B | 45.78 MB | 0 / 6128 |
| surge-xt exnref | 4.0.20 | 164 s (objects partly cached; not a clean-build time) | 6 811 655 B | 45.72 MB | 5912 / 0 |
| obxf legacy | 3.1.51 | 433 s | 2 580 312 B | 11.9 MB | 0 / 12053 |
| obxf exnref | 4.0.20 | 488 s to the failure below; then ~4 s to relink | 2 268 893 B | 11.6 MB | 11785 / 0 |

**OB-Xf exnref initially failed**: `wasm-opt` (Binaryen 124, Emscripten 4.0.20) dies with SIGBUS while writing `ui.wasm`. Reproduced on
the bare tool: `--optimize-level=2/3`, `--shrink-level=1` and `--optimize-stack-ir` crash; level 0/1 and `--no-stack-ir` pass. So it
is a Binaryen StackIR bug on this module with exnref input, not memory or disk (52 GB free). Workaround, commit 3 of the queue:
link `ui` at `-O1` under `WEBCLAP_EXNREF` (`ui.wasm` 11.7 MB instead of 10.9 MB; the DSP `module.wasm` is still `-O3`). Six Sines
and Surge XT are not affected. Worth reporting to Binaryen with the module if it persists on a newer emsdk (not done).

## 3. Fingerprints against the bundles in use (step 3)

Tool: `scripts/wclap-upstream/compare.mjs`, which uses kromosynth-burst's own adapters (`createSurgeEngine`, `createObxfEngine`):
fresh plugin per render after `resetDeterminism()` plus a memory snapshot restore (OB-Xf: rewind), same genomes (Init patch + 12
random ones from a seeded RNG), same notes (key 60, velocity 0.8, 2 notes of 2 s, 48 kHz / 512 frames), stereo samples compared.
Residual = 20·log10(max|a−b| / peak). Rule: bit-identical, or ≤ −90 dB.

Which bundle burst uses today is not recorded in the repo (the adapters read `$WCLAP_SURGE` / `/tmp/w/...`, and `/tmp/w` is
emptied nightly; the running server has no `WCLAP_*` in its pm2 env). So I compared against **every** Surge bundle on the Studio:

| Reference (read-only) | what it is | legacy (open-webclap) | exnref (open-webclap) | translated (wasm-opt) |
|---|---|---|---|---|
| `~/wclap-build/juce-webclap-plugins/build/surge-xt/surge-xt.wclap.tar.gz` (Oct 21, juce-webclap-plugins at `6055f4e`, 3.1.51) | most likely what burst reads | bit-identical, 13/13 | bit-identical, 13/13 | bit-identical, 13/13 |
| `~/wclap-native/juce-webclap-plugins/build/surge-xt/…` (Oct 13, ecab895 + exnref patch, 4.0.20) | the bundle the task names | bit-identical | bit-identical | bit-identical |
| `~/wclap-native/juce-webclap-plugins/build/surge-xt-legacy-3.1.51/…` (ecab895, 3.1.51) | | bit-identical | bit-identical | bit-identical |

**Surge XT: bit-identical everywhere** (98 genes, same catalogue). Expected: the same Surge pin `4f425b04` and JUCE; the three
upstream commits (meters frame for the editor, NOTICES/licence rows, test-host WASI) touch the editor and docs, not the engine,
and different Emscripten versions (3.1.51 vs 4.0.20) and exception forms give the same audio for Surge. So the genomes and
landmarks burst has produced stay valid on any of these bundles.

**OB-Xf: no burst reference bundle exists on disk** (no `obxf.wclap*` outside my new directories; juce-webclap-plugins `ecab895`
has no OB-Xf build). Open-webclap legacy vs open-webclap exnref:

- **Not bit-identical, nowhere near −90 dB**: per-genome residual +5.1, +2.8, +4.9, +2.3, −1.7, +1.8, +1.8, +3.4, +3.0, +5.4, +3.6,
  +3.1, +0.5 dB (positive = the difference is as large as the signal peak). Each build is bit-reproducible with itself (13/13,
  separate processes). Same catalogue (60 genes).
- It is the same sound, not a different one: on the Init patch, normalised cross-correlation ≈ 1 (best lag 3 samples), zero
  crossings 1592 vs 1590, RMS 0.0478 vs 0.0494.
- Cause, partly established. OB-Xf seeds noise generators from `std::rand()` with no `srand()` (OscillatorBlock.h:159,
  Voice.h:427), and the generator state lives in module memory. Evidence it is that: in one bundle, merely creating one extra plugin
  before the render (advancing `rand()`) changes the render by 3.6 dB (legacy) and 3.5 dB (exnref), the same size as the
  build-to-build difference (5.1 dB); but it is **not** the noise oscillator itself (Noise Volume = 0 leaves the difference at 5.1 dB).
  Not proven: *why* the two builds start from a different generator state or consume it differently. Emscripten's musl `rand.c`
  is identical in 3.1.51 and 4.0.20, so it is not libc; a codegen/libm difference between the compilers is not excluded.
  **Key result: the exception form is not the cause**, section 4: legacy → wasm-opt-translated exnref of the *same* compile is bit-identical.
- Consequence: an OB-Xf exnref bundle built natively with Emscripten 4 is a different instrument for burst's purposes (new
  fingerprints) and would invalidate comparisons with legacy-built OB-Xf output. If OB-Xf must run in a Wasmtime host *and*
  keep fingerprints, translate the legacy bundle (section 4b).

## 4. Native exnref vs wasm-opt translation (step 4)

(a) native: `-DWEBCLAP_EXNREF=ON`, Emscripten 4.0.20. (b) translate the upstream legacy `module.wasm` with
`wasm-opt --all-features --translate-to-exnref in.wasm -o out.wasm` (Binaryen 124 from emsdk-4).

| | Surge XT (a) | Surge XT (b) | OB-Xf (a) | OB-Xf (b) |
|---|---|---|---|---|
| time | 164 s incremental (253 s legacy clean as a scale) | **0.4 s** module, 0.8 s ui | 488 s (+ a failure and workaround) | ~0.4 s |
| module.wasm | 6 811 655 B | 6 890 097 B (+1.2 %) | 2 268 893 B | 2 768 031 B (+7 % vs legacy 2 580 312) |
| ui.wasm | 20 538 634 B | 20 845 686 B | 11 716 024 B | not translated (browser only) |
| `try_table` / legacy `try` | 5912 / 0 | 6128 / 0 | 11785 / 0 | 12053 / 0 (translated: one `try_table` per legacy `try`: Surge 6128 = 6128, OB-Xf 12053) |
| fingerprint vs legacy | identical | identical | **different** (section 3) | **identical** |
| Wasmtime bridge probe (`/tmp/aa490` build, wclap-bridge `cd11d22` + the spike's two patches) | loads; PLAYS PASS; same-instance state round trip PASS; 775 params; CPU 0.0086 of realtime; RMS 0.152 | same: PASS/PASS, RMS 0.114 | no result: no output in 90 s or 300 s (cause not diagnosed; the legacy bundle is refused at open, immediately) | same: no output in 300 s (so it is the probe or OB-Xf init under Wasmtime, not the build route) |

Surge `RMS` differs between (a) and (b) in the probe (0.152 vs 0.114) although the Node host renders them bit-identically: the
probe's note is not seed-controlled (it also logs `Surge Error [Filesystem Error]` for the missing user data directory), so its
RMS is not a determinism measure. The legacy bundle in Wasmtime fails with "legacy_exceptions feature required for try
instruction" for both engines. Node 22 (V8) loads and renders all of them, legacy, native exnref and translated.

**Which is simpler as a patch queue: (b).** It needs no patch at all: no flag, no second emsdk, no per-port workaround (OB-Xf's
`-O1` link, any future Binaryen bug), no second 4–8 minute build, and it keeps the audio of the tested 3.1.51 build exactly,
which matters for OB-Xf. The costs: a 1–7 % larger module and a `wasm-opt` step in the pipeline (Binaryen's translation is
in its test suite, but here only checked by rendering: Surge 13 genomes × 3 references identical, OB-Xf 13 identical). (a)'s
only advantages are a smaller module and a bundle produced by the compiler directly. **So yes: (b) makes the upstream patch
unnecessary for our own use**, and (a) is only worth keeping as a contribution (section 6) and a cross-check. The UI module is not
needed by native hosts; for browsers, see section 5.

## 5. Browser support (step 5)

Minimum versions, from the WebAssembly feature-status data (fetched 2026-10-22 as `WebAssembly/website` `features.json`
and MDN browser-compat-data `webassembly/exception-handling.json`):

| | Chrome | Firefox | Safari |
|---|---|---|---|
| Legacy exceptions (`try`/`catch`) | 95 | 100 | 15.2 |
| exnref (`try_table`/`exnref`, phase 5) | 137 | 131 | 18.4 |

Source status: the legacy numbers match both sources; the exnref numbers come from the feature-status page only (a fetch through
a summarising tool; MDN has no exnref entry, its URL 404s). **UNVERIFIED against vendor release notes** for all exnref numbers;
Bjorn's own tests (Chrome 152 and Safari, aa4.52/aa4.7x) are consistent with them but test newer versions. **UNVERIFIED**: that
any vendor plans to remove legacy; the page lists it as phase "Inactive" and every current browser still runs it.

Recommendation, native hosts: **exnref only** (Wasmtime refuses legacy; the Node host takes both).
Public synth.is page: **both, chosen by a feature test** (`WebAssembly.validate` on a tiny `try_table` module), exnref preferred,
legacy as the fallback. Reasons: legacy reaches ~2 years older browsers (Chrome 95 vs 137 is a wide gap) and is what upstream tests and
ships, exnref is the future-proof form and the one our native hosts already need. Cost: two ~45 MB Surge bundles on disk (the page
fetches one). If that is too much, serve **legacy only** to browsers until a vendor deprecation is announced: it is the
unpatched upstream build, works in every browser, and the exnref copy then exists only for native hosts. Do not serve exnref only
unless the audience is on Chrome ≥ 137, Firefox ≥ 131, Safari ≥ 18.4.

## 6. Candidate contributions, in the order to offer them (draft text only; Bjorn sends)

1. **Opt-in exnref (`WEBCLAP_EXNREF`, commits 1–2).** *Why they might want it:* it lets Wasmtime-based hosts (wclap-bridge, others) load the
   bundles; it is the standardised form and future-proofs against legacy being dropped. *Honest limits:* browsers do not need it
   today, it needs Emscripten ≥ 4.0.7 while their tested version is 3.1.51, and it doubles their build matrix. Offer as "off by default, one flag".
   Draft: "Hi André, we run your Surge XT/OB-Xf/Six Sines bundles in Wasmtime, which accepts only the exnref exception form. Here is a
   small opt-in (`-DWEBCLAP_EXNREF=ON`, default off, nothing changes for 3.1.51). Totally fine if you'd rather not carry it; a note
   in the README that `wasm-opt --translate-to-exnref` also works would do the same for us." (The translation note is the lowest-cost contribution.)
2. **Empty 512-frame block after `activate()`/`loadState()` for Six Sines** (aa4.90). *Why:* without it, loaded state is applied only on the first
   `process()`, so a note arriving in the first block is silent and renders differ; a host-side note in their README/test host helps other
   hosts. *Limit:* a Six Sines/clap-helpers behaviour, arguably upstream of them; may belong to the sst projects.
3. **wclap-bridge fixes** (uninitialised `hostTemplate`, the Wasmtime exceptions switch; docs/WCLAP_BRIDGE_UPSTREAM_ISSUE.md): belong to
   the WebCLAP/wclap-bridge project, not open-webclap; mention only as context.
4. **Our Apache-2.0 DX7 core** (kromosynth-burst `engines/dx7`). *Why:* a licence-clean DX7 for the site's catalogue. *Limit:* a new port to
   maintain; they may prefer GPL ports from the Surge family; offer once the DX7 licence review is closed.
5. **Exnref Surge XT fingerprints** (this document, section 3): shows engine output is identical across Emscripten 3.1.51/4.0.20 and both exception forms,
   a ready regression check for their build. *Limit:* interest unknown; cheap to include with item 1.
6. **A Binaryen bug report** for the `ui.wasm` SIGBUS (section 2), if it reproduces on a current emsdk; not for open-webclap.

## 7. Not done / open

- No OB-Xf bundle from burst's actual pipeline was available; the OB-Xf comparison is between my own builds.
- OB-Xf under the Wasmtime probe produced no output; a manual look is needed (could be the probe, which is Surge-oriented, or slow init).
- Which Surge bundle burst reads cannot be read from the running process; all candidates were compared and all agree.
- The exnref browser numbers are unverified against vendor sources (section 5). Only macOS arm64 was used.
- Bundle sizes quoted in "Build time" come from the earlier logs (`~/wclap-native/pin-*.log`), not re-timed; the Surge exnref time reused cached objects.
