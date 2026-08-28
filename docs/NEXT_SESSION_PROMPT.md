# Next session prompt

Copy-paste into a fresh session. Written at the end of the signing + shell
session, 2026-08-28. Supersedes `NEXT_SESSION_PROMPT.2026-08-28a.md`.

---

Continue work on the Synth.is DAW plugin. **Stage 0 is complete and Stage 1 has
started.** The plugin is signed, notarised, loads in GarageBand with no security
dialog, plays instruments, and serves its own offline UI from which you can
switch between them.

Use the filesystem MCP and the shell. Be critical; I want pushback, not
validation. Report failures as findings, not as things to work around.

## Context to load

- `docs/STAGE0_FINDINGS.md` — read **§L, §M and §N first**; they are this
  session's state. §A–§K are the Stage 0 record and still accurate, with
  corrections recorded in place (§K8 and §L6 are corrections to earlier
  findings — read those alongside what they correct, not instead of it).
- `kromosynth-projects/Evolutionary Sound Discovery Platform/daw-plugin/WP_SYNTHIS_PLUGIN.md`
  — **reconciled 2026-08-28.** §11 is the reconciliation table; §12 is the
  two-web-contexts decision that Stage 1 is built on. No longer stale.
- `src/ShellPage.h` — the local UI. The invoke-protocol comment cites JUCE's
  own frontend source; check against it rather than trusting the comment.
- `src/InstrumentLibrary.{h,cpp}` — the local library and its path-traversal
  guard.
- `src/SamplerEngine.h` — the thread-safety contract and why the render is
  chunked. Both read out of vendored source with line numbers.
- `cmake/Signing.cmake`, `scripts/notarize.sh` — signing and notarisation, both
  working. The ordering comment in Signing.cmake is load-bearing.

## Repository state

`kromosynth-plugin`: clean, ~15 commits, **still no remote configured**. Björn
said he would set one up; it has not happened, so nothing here is backed up.

`kromosynth-projects`: the daw-plugin documents are now tracked (`1d6c840`,
`48fda2c`). Five untracked files remain, none cited by anything tracked.

`kromosynth-recommend`: fully deployed and committed, including four
uncommitted hub-only fixes found and committed on 2026-08-28 (`78c615f`).

## Build

```
cmake --preset sampler-release \
      -DKROMOSYNTH_SFZ_PATH=/abs/path/instrument.sfz \
      -DKROMOSYNTH_SIGN_IDENTITY="Developer ID Application: Nemur net ehf. (ATA8MQ4L7K)"
cmake --build --preset sampler-release
./scripts/notarize.sh            # ~5 min, installs and staples

auval -v aumu Krm0 Sthi
/Applications/pluginval.app/Contents/MacOS/pluginval --strictness-level 10 \
    --skip-gui-tests --random-seed 0x1234 --validate <bundle>
tail -f ~/Library/Logs/Synth.is/stage0-*.log
```

**Any rebuild re-signs and un-notarises.** Re-run `notarize.sh` before testing
in an Apple host, or GarageBand will refuse the build.

**The certificate expires 1 Feb 2027** — 157 days from issue, where Developer ID
certificates are normally five years. Already-notarised builds survive expiry
because `--timestamp` proves the signature predates it; new builds will not be
signable. Worth understanding before then (§L5).

Local instrument library: `~/Music/Synth.is/Instruments/<id>/instrument.sfz`,
currently holding copies of the `rr` and `xy` artefacts.

Test tools, both developer-only and never shipped:
`tools/sfz-probe` (sfizz directly), `tools/engine-probe` (SamplerEngine through
arbitrary channel layouts), `tools/reaper-render` (headless DAW render — always
with `-nosplash -ignoreerrors -splashlog`).

## TASK — pick from these

**Propose the design before writing it.**

### 1. The site component and acquisition. The largest remaining piece.

§12: a second `WebBrowserComponent` with native integration **off**, showing
`https://synth.is`, and a channel back. The site cannot call native functions,
so acquisition works by navigation interception — the site navigates to a
sentinel URL, `pageAboutToLoad` recognises it, returns `false`, and the plugin
acts.

The plugin half is small. **The web app half is Björn's** and needs discussing
before either is written: what the sentinel URL looks like, what it carries, and
what the plugin does with it (it must download the artefact itself — sfizz needs
real files, and anything in the webview's OPFS is unreachable).

### 2. Gate 0 — the only thing that can still invalidate the architecture.

`daw-plugin/EXPERIMENT_01_DSP_EXECUTION.md`, still **"protocol,
pre-registered"** with no result. Runs in `kromosynth-render`, no JUCE, no
plugin. Decides how an evolved `.dsp` becomes sound: WASM interpreted, WASM AOT,
native AOT, or libfaust JIT (which cannot ship on iOS).

Everything else on this list is additive. This one is falsifying: if nothing
reaches the polyphony target on Apple silicon, the WP's own gate says the Faust
engine ships desktop-only and iOS carries the sampler alone — which changes what
the plugin is. Better known before more is built on top.

Note it wants a session that can sit with long measurements.

### 3. Display names. Small, blocked on a decision.

Instruments currently show a ULID or a directory name (§N2). Neither is a name
and the VI service emits neither. The SFZ header already carries `Range`,
`Velocity Layers`, `Round Robins`, `XY Morph` — which would label better than
either identifier. **Decide the convention with Björn before implementing**; do
not accrete one.

### 4. Explicit bus layout. Small, and currently a false claim.

`auval` reports `[0, -1]` — the plugin advertises any output count to 8 — while
`process()` fills two channels and clears the rest, and auval warns about the
missing channel layout. Not broken (`engine-probe` confirms 1/2/6 all behave as
described) but it claims more than it honours.

### 5. Periodic stats flush. Small, and backwards today.

`sampler_render_stats` writes only on `releaseResources` and the destructor, so
the log is silent exactly while the plugin is alive and working. §M1: this cost
a channel-layout investigation to answer "did it make a sound". A low-frequency
flush would make it a glance.

## Constraints

- sfizz pinned at tag **1.2.3**, verified at configure time. `SFIZZ_USE_SNDFILE`
  stays OFF. **liquidsfz is deferred, not rejected** (§F2), and §E4 found the
  library repo archived — §9 of the WP defers this to Stage 3 on the milder
  reading and should be revisited.
- macOS AU + VST3 only. Universal builds with the sampler hard-error by design
  (§E2); `sampler-release` is native-arch and a measuring build.
- **No Faust engine** until Gate 0 returns.
- **No engine abstraction** until there is a second engine.
- Native integration stays **off** on any component showing the live site.
- git commits fine; still no remote to push to.

## Standing rules

Every one of these was earned this session.

- Propose significant changes before writing them.
- **When a diagnostic and the thing it measures can both be broken, verify the
  diagnostic first.** Seven instances now. The expensive failures all
  *succeeded incorrectly*: a latched render counter reporting zero on a healthy
  build (§I6), `auval` passing a fully culled instrument (§I5), a silent Reaper
  render that was a missing plugin (§J2), a pre-flight that rejected a valid
  signature via SIGPIPE (§L7), `spctl` reporting notarised for an unstapled
  bundle (§L7). An error message is a cheap failure; a green light is not.
- **When a tool is driven non-interactively, its interactive output channel
  still exists and is invisible.** Two wrong conclusions came from evidence on
  Björn's screen rather than stdout (§J6, §J7). Ask what the human sees, early.
- **A proxy sharing no code with the thing it stands in for is not a
  measurement of it** (§I2). Overstated sfizz's load cost by 100×.
- **Grepping a binary for a token proves it exists somewhere, not that it is
  valid where you are writing it** (§J7).
- **Do not generalise from one example.** Two bugs this session from reading one
  artefact's header and one plugin's signature (§N2, §L1).
- **A statistic compared for equality needs a tolerance** (§G7.3). §J3 read a
  1.5% spread as "distinct" and called hollow velocity layers working; a
  listener caught it in ten seconds (§K).
- **A chain of scale-invariant statistics cannot see a gain-only difference**
  (§K5). When every instrument is invariant to X, nothing in the chain sees X.
- **Attributing a finding outside the component under test is the start of the
  reasoning, not the end** (§K8). Check whether the behaviour is configured or
  intended there, and prefer asking the person who built it over inferring from
  source.
- **Defensive code that never runs is not verified** (§I3). Force it or say it
  is untested.
- Read the actual tree you are reasoning about (§E1). The JUCE invoke protocol
  was in the vendored submodule while it was being guessed (§N1).
- Non-ASCII in C++ string literals is mangled. ASCII or `juce::CharPointer_UTF8`.
- Selective git staging; never `git add -A` in a repo with unrelated WIP.
- **Load-bearing work outside version control is a recurring failure here.**
  Four hub-only fixes in `kromosynth-recommend`, the entire `daw-plugin/`
  directory, and a pre-registered rule cited by a committed result. Check.

## Elsewhere, not the plugin

- **`kromosynth-vi` display metadata** — see task 3.
- **§K stands corrected by §K8.** In the tested artefact the velocity layers are
  one waveform at three gains (median 1.76% residual) and the round robins were
  identical because `layerParamsPerSound` was not supplied. **Neither is a
  defect** — velocity is a CPPN input by design, per-RR settings are an optional
  request field the performance pads populate. What remains useful: a VI whose
  layers are near-identical is larger than it needs to be, and a residual check
  at generation time could say so.
- **Gate 0 / EXPERIMENT_01** — see task 2.
- **The Faust substrate track** is separate and moving: `E1_RESULT_2026-08-27`,
  `E2_RESULT_2026-08-28`. E2's verdict is that its rule does not resolve because
  20–23% of the archive is inaudible — which rhymes with §K5 and may deserve the
  same treatment.
