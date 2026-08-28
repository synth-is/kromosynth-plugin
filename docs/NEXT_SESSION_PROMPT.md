# Next session prompt

Copy-paste into a fresh session. Written at the end of the Gate 0 §4.1 +
acquisition-auth session, 2026-08-28. Supersedes
`NEXT_SESSION_PROMPT.2026-08-28b.md`.

---

Continue work on the Synth.is DAW plugin. **Stage 0 is complete, Stage 1 has
started, and Task 1 is now unblocked.** The plugin is signed, notarised, loads
in GarageBand with no security dialog, plays instruments, and serves its own
offline UI from which you can switch between them.

Two things changed since the last prompt, and both remove a blocker:

- **Gate 0 §4.1 is answered.** The Faust toolchain emits a `.wasm` plus JSON
  metadata that a non-browser host can drive. Path B is not falsified.
- **Acquisition auth is decided** (WP §13). The web-app half now has a
  specification, so the site component can be built.

Use the filesystem MCP and the shell. Be critical; I want pushback, not
validation. Report failures as findings, not as things to work around.

## Context to load

- `docs/STAGE0_FINDINGS.md` — read **§L, §M and §N first**; they are the plugin's
  state. §A–§K are the Stage 0 record and still accurate, with corrections
  recorded in place (§K8 and §L6 correct earlier findings — read those alongside
  what they correct, not instead of it).
- `kromosynth-projects/Evolutionary Sound Discovery Platform/daw-plugin/WP_SYNTHIS_PLUGIN.md`
  — §11 reconciliation, §12 two web contexts, **§13 acquisition auth (new)**.
  §13 is what Task 1 is built on; read it before proposing anything there.
- `daw-plugin/EXPERIMENT_01_4.1_RESULT.md` — **new.** §4.1 and §4.3 only.
  Deliberately *not* `EXPERIMENT_01_RESULT.md`, because **Gate 0 is still open**
  and no branch of §5 has fired.
- `src/ShellPage.h` — the local UI. The invoke-protocol comment cites JUCE's own
  frontend source; check against it rather than trusting the comment.
- `src/InstrumentLibrary.{h,cpp}` — the library, its path-traversal guard, and a
  known defect (see task 3).
- `src/SamplerEngine.h` — thread-safety contract and why the render is chunked.

## Repository state

`kromosynth-plugin`: clean, 29 commits, no remote — Björn is handling it. Do not
raise it again.

`kromosynth-projects`: `086ed26` carries the §4.1 result, WP §13 and the ABI
probe. Six untracked/modified files remain, all unrelated (IQD reports,
`WP_TEMPORAL_BD_TEMPLATES.md`, `GATE_PRUNING_2026-08-27.md`).

`kromosynth-recommend`: deployed and committed as of `78c615f`.

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

**Any rebuild re-signs and un-notarises.** Re-run `notarize.sh` before testing in
an Apple host. **The certificate expires 1 Feb 2027** — already-notarised builds
survive expiry because `--timestamp` proves the signature predates it; new builds
will not be signable (§L5).

Local library: `~/Music/Synth.is/Instruments/<id>/instrument.sfz`, currently the
`rr` and `xy` artefacts.

Test tools, developer-only, never shipped: `tools/sfz-probe`,
`tools/engine-probe`, `tools/reaper-render` (always `-nosplash -ignoreerrors
-splashlog`).

Faust ABI probe, in `kromosynth-projects`:
`faust-genome-substrate/probe/12-wasm-abi.mjs` — `[n]` for the bare-drive and
null test, `--all` for the ABI inventory over the elite set.

## TASK — pick from these

**Propose the design before writing it.**

### 1. The site component and acquisition. Now unblocked, and the obvious next thing.

§12 gives the shape: a second `WebBrowserComponent` with native integration
**off**, showing `https://synth.is`, with acquisition by navigation interception.
§13 gives what was missing — what authorises the download.

Plugin half, and it is small:

- second component, native integration off, `allowedOrigin` empty
- sentinel check in `pageAboutToLoad` **before** the origin check, returning
  `false`
- HTTP fetch of the artefact with the ticket
- atomic write into the library — download to temp, move into place, or write
  `instrument.sfz` last (§13, and `InstrumentLibrary.cpp` says why)
- **redact the ticket in the log.** `pageAboutToLoad` currently logs every URL it
  sees; a sentinel carrying a ticket would land in plaintext in
  `~/Library/Logs/Synth.is/`. Log the artefact id and the fact of a ticket.

Web-app half is Björn's: `plugin_download` added to `EXTRACTION_SURFACES`, the
quota call, the ticket mint, the sentinel navigation.

Three things §13 leaves open and worth settling before building: where the shared
HMAC secret lives, whether re-downloads are free on this surface, and whether
`/vi/file/` verifies tickets or the download moves behind recommend. The file
route's own comments mention Cloudflare — check whether anything already fronts
those routes in production before assuming.

### 2. Gate 0 — still the only falsifying item, but much smaller than it was.

`EXPERIMENT_01_4.1_RESULT.md` closes §4.1 and §4.2-for-path-B, and answers §4.3
on desk evidence. **§3 is untouched and no §5 branch has fired.**

If §4.3 holds — WAMR's own README omits iOS from its AOT platform list, the only
public device attempt failed, and a `.aot` is downloaded native code —
then **§5 rule 2 is unreachable** and the gate turns on one measurement:

> interpreted B (wasm3 or WAMR-interp), 8 voices, slowest decile, at RTF ≥ 4×,
> **on real iOS hardware.**

Measure peak RSS before RTF. DSP struct is **0.85 MB per voice at p90, 1.30 MB
worst** — 13.6 MB at 16 voices for state alone, before runtime, sampler or host.
Memory may bind first, and it is cheaper to check.

§3.4 numerical agreement still needs real runtimes. The Node null test does not
stand in for it: both paths ran the same bytes on the same V8, so identity was
expected, and what it validated was the probe's own memory layout.

### 3. Display names, and a defect underneath them.

Instruments show a ULID or a directory name (§N2). Neither is a name and the VI
service emits neither. The SFZ header carries `Range`, `Velocity Layers`,
`Round Robins`, `XY Morph`, which would label better than either identifier.
**Decide the convention with Björn before implementing.**

Underneath it, found this session and unfixed: **`readDisplayName` does the
opposite of what its comment claims.** The comment says it reads only the header
because opening every SFZ on every scan would be expensive. The code calls
`juce::FileInputStream::readString()`, which loops on `readByte()` until a zero
byte (`juce_InputStream.cpp:194-206`), `readByte()` returns 0 at EOF (`:68-73`),
and `FileInputStream::read` has no buffering (`juce_FileInputStream.cpp:55-68`,
no buffer member in the header). So it reads the **whole file, one syscall per
byte**, then throws away everything past 2048 characters.

Calibrate honestly: the two artefacts here are 4.7 KB and 3.3 KB, so this is a
millisecond. The finding is the contradiction, not the cost. Fix it with the
naming work, not before.

### 4. Explicit bus layout. Small, and currently a false claim.

`auval` reports `[0, -1]` — any output count to 8 — while `process()` fills two
channels and clears the rest, and auval warns about the missing channel layout.
Not broken (`engine-probe` confirms 1/2/6 behave as described) but it claims more
than it honours.

### 5. Periodic stats flush. Small, and backwards today.

`sampler_render_stats` writes only on `releaseResources` and the destructor, so
the log is silent exactly while the plugin is alive and working. §M1: this cost a
channel-layout investigation to answer "did it make a sound".

## Constraints

- sfizz pinned at tag **1.2.3**, verified at configure time. `SFIZZ_USE_SNDFILE`
  stays OFF. **liquidsfz is deferred, not rejected** (§F2); §E4 found the library
  repo archived and §9 of the WP defers on the milder reading — revisit.
- macOS AU + VST3 only. Universal builds with the sampler hard-error by design
  (§E2); `sampler-release` is native-arch and a measuring build.
- **No Faust engine** until Gate 0 returns. §4.1 passing is permission to
  *measure*, not to build.
- **No engine abstraction** until there is a second engine.
- Native integration stays **off** on any component showing the live site.
- The plugin holds **no account credential** (WP §13). Tickets only.
- git commits fine; still no remote to push to.

## Standing rules

Every one of these was earned. The first has now fired nine times.

- Propose significant changes before writing them.
- **When a diagnostic and the thing it measures can both be broken, verify the
  diagnostic first.** The expensive failures all *succeeded incorrectly*: a
  latched render counter reporting zero on a healthy build (§I6), `auval` passing
  a fully culled instrument (§I5), a silent Reaper render that was a missing
  plugin (§J2), a pre-flight that rejected a valid signature via SIGPIPE (§L7),
  `spctl` reporting notarised for an unstapled bundle (§L7). **Two more this
  session, both in code written to measure carefully:** the ABI probe reported
  correct behaviour as a fault ("ABI divergence" for an import set that is
  supposed to vary), and a `git status` piped through `cat -A | sed` returned
  empty output that read as a clean tree. An error message is a cheap failure; a
  green light is not, and neither is silence.
- **A statistic compared without its requirement beside it means nothing**
  (§G7.3). The same probe reported "32 elites with <64 KB headroom", which sounds
  alarming and is not: the requirement is ~4 KB at a realistic block size. Asked
  properly — a fit test at 128/512/1024/8192 — it is 0 overruns across 120
  elites, tightest 236 bytes at 8192.
- **Do not generalise from one example.** Three instances now (§N2, §L1, and the
  Faust import set: the first elite imported two math functions, the archive
  needs eleven, and a host built from one sample would link a handful of it).
- **A proxy sharing no code with the thing it stands in for is not a measurement
  of it** (§I2). Node is a non-browser host and it is not wasm3.
- **When a tool is driven non-interactively, its interactive output channel still
  exists and is invisible** (§J6, §J7). Ask what the human sees, early.
- **Grepping a binary for a token proves it exists somewhere, not that it is
  valid where you are writing it** (§J7).
- **A chain of scale-invariant statistics cannot see a gain-only difference**
  (§K5).
- **Attributing a finding outside the component under test is the start of the
  reasoning, not the end** (§K8). Prefer asking the person who built it over
  inferring from source.
- **Defensive code that never runs is not verified** (§I3). Force it or say it is
  untested.
- **Read the actual tree you are reasoning about** (§E1). The JUCE invoke
  protocol was in the vendored submodule while it was being guessed (§N1); the
  Faust memory layout and the quota gate were both already written down while
  this session was about to design them.
- Non-ASCII in C++ string literals is mangled. ASCII or `juce::CharPointer_UTF8`.
- Selective git staging; never `git add -A` in a repo with unrelated WIP.
  `kromosynth-projects` has six unrelated files sitting in the worktree.

## Elsewhere, not the plugin

- **`kromosynth-vi` — `expiresAt` is decorative.** `artifactManager.js:445,476`
  compute `now + 24h`, `vi.js:388` returns it, nothing reads it, and
  `/vi/file/:artifactId/:fileName` has no auth and no expiry check. WP §13 turns
  this into a real constraint; until then the URLs work indefinitely.
- **`kromosynth-vi` display metadata** — see task 3.
- **`kromosynth/.nvmrc` says 18** while `package.json` requires `>=22` and the
  fleet runs v22.23.1. Harmless today because nothing reads it; wrong if anything
  ever does.
- **§K stands corrected by §K8.** In the tested artefact the velocity layers are
  one waveform at three gains (median 1.76% residual) and the round robins were
  identical because `layerParamsPerSound` was not supplied. **Neither is a
  defect.** What remains useful: a VI whose layers are near-identical is larger
  than it needs to be, and a residual check at generation time could say so.
- **The Faust substrate track** is separate and moving: `E1_RESULT_2026-08-27`,
  `E2_RESULT_2026-08-28`. E2's verdict is that its rule does not resolve because
  20–23% of the archive is inaudible — which rhymes with §K5 and may deserve the
  same treatment.
