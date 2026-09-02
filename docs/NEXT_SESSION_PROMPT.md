# Next session — kromosynth-plugin

Previous prompt preserved as `NEXT_SESSION_PROMPT.2026-09-01b.md`.

## Where things stand

**Stage 2 works.** The plugin synthesises live from an evolved Faust genome in
GarageBand — MIDI in, wasm out, no samples. Verified by Björn on three
instruments: audible, pitched, and they stop. That is the one job nothing else
in the platform does.

**Stage 1 (sampler) unregressed.** Both engines in one signed bundle, `auval`
passes, `InstrumentLibrary::resolve` picks the engine from what is in the folder.

### The gate to re-run after any engine change

```
faust-probe <wasm> <json> --plan X.plan --ref X.ref.f32     # 33/33, maxAbsDiff 0.0
```

`FaustEngine` is **bit-identical to the platform reference on all 33** protocol
elites. Gate 0 measured three runtimes at exactly zero against those references,
so the bar is zero and not a tolerance. It caught three regressions last session
that were green everywhere else, all from the idle-voice optimisation.

### Load-bearing facts

1. **Math imports compute in DOUBLE.** faustwasm binds `_powf` to `Math.pow`;
   a C host calling `powf()` diverges by −50.5 dB on a math-heavy elite.
2. **wasmtime v48.0.1**, pinned with SHA256 in `cmake/Wasmtime.cmake`, verified
   byte-identical to the build Gate 0 measured. Breaks `VENDORING.md`
   deliberately and keeps the reason behind it.
3. **At load, the plugin measures the genome** and uses the result: `trim` to
   0.25 peak, and a per-genome voice cap from RTF. Both are stopgaps for numbers
   the artefact should carry.

## Björn's stated priorities, in his order

### 1. The web app → plugin flow for Faust instruments

**Nothing produces a Faust artefact.** `kromosynth-vi` contains no reference to
wasm or faust at all — checked, not assumed. Takeout still emits a ZIP of
samples, so every Faust instrument in the library was hand-built by copying
Gate 0 probe files into `~/Music/Synth.is/Instruments/faust-*`.

So the acquisition chain that works beautifully for SFZ has **never been
demonstrated end to end for Faust**, and that is the gap to close.

`InstrumentLibrary::resolve` invents `instrument.wasm` + `instrument.json` and
those names are **provisional** — they have to be agreed with kromosynth-vi
before anything depends on them.

Four fields should ride along in the same change, because adding them later
means re-rendering the archive. All four are measured and their values are in
`ARTEFACT_NUMBERS_2026-09-01.jsonl`:

| field | why | archive range |
|---|---|---|
| `rtf` | per-platform voice cap | 36× – 2231× |
| `trim` | audible vs painful | −19.6 dB to +20.5 dB |
| `articulation` | is it a MIDI instrument or a pad | 48/120 drone |
| `hasGate` | can it be triggered at all | 6/120 cannot |

Open questions that are Björn's, not the plugin's: does takeout emit a Faust
artefact *instead of* or *alongside* the SFZ one; does the site card show which
kind before download; does kromosynth-vi grow a second output path or does the
compile service emit both.

### 2. Envelope and duration controls

Wanted for both engines. The blocker underneath is bigger than the controls:

**`getStateInformation` writes nothing.** Load an instrument, save the project,
reopen — nothing is loaded. There are no plugin parameters at all. The header
says why: *"no AudioProcessorValueTreeState, on purpose. Adding one now would be
an abstraction over zero parameters"* and *"State is Stage 1."* **Both conditions
have now expired.**

The shape that answers everything at once is four **`AudioProcessorParameter`s**
— attack, decay, sustain, release — which every wrapper exposes as AU, VST3 and
(via `clap-juce-extensions`) CLAP parameters. One declaration, every format,
plus host automation and session persistence, none of which webview sliders get.

- **Sampler:** the generated SFZ already carries `ampeg_attack/decay/sustain/
  release` with `ampeg_*_oncc70..73`. The controls exist and are effectively
  unreachable, because sending arbitrary CCs in GarageBand is awkward enough
  that nobody will.
- **Faust:** no equivalent, and the genome's own parameters cannot substitute —
  only 31% publish `dec`, 20% `rel`, and none of them stops a drone at any
  setting (measured, `ELITE_ARTICULATION_2026-09-01`). Needs a plugin-side
  envelope on the voice output, which is the same mechanism forced release
  already uses.

This also **dissolves a fork** left open last session: release = 0 means
"adaptive — let the genome decide, force-stop if it will not", any non-zero
value is an explicit release. Fidelity by default, control when wanted. And it
simplifies the engine: the decay-detection heuristic exists only because there
is no envelope.

### 3. Visual design of the plugin

`ShellPage.h` is a functional list with a status footer. Untouched since Stage 1
and never designed. Worth doing alongside (1), since the acquisition flow is
what a user actually sees.

## Deferred deliberately

**iOS — its own session.** `tools/ios-gate0/` is built and answers the gate on
desktop already: **0 of 12** slowest-decile genomes sustain 8 voices at ≥4× on an
M1 Pro, one below real time. An A15 is slower, an A11 much slower, so the device
number can only be worse. That is the experiment's rule 2 or 3.

The remaining step is six manual lines in `tools/ios-gate0/README.md` — the
Xcode MCP server timed out creating the project. Deployment target 15.0 works
(checked in the SDK), so an iPhone 8 is reachable.

**The implication is worth weighing before more iOS work:** if the device
confirms it, iOS ships the sampler engine only in v1. That makes the SFZ path
load-bearing rather than redundant, which cuts against retiring it.

**Gate 0's session term.** `rt-sweep.mjs --label session` in the Gate 0 probe
tree, built and unrun. Needs a quiet machine and a DAW session playing; the
machine was at load 16 with Xcode and a Simulator running. It barely matters on
desktop — nothing caps below 8 voices either way — and matters on iOS, which is
now deferred.

**§O10, QWERTY typing latches the host's own keys.** Open, characterised,
includes a measured negative: refusing keyboard focus at the JUCE level did not
fix it, because a WKWebView takes AppKit first responder natively. Affects both
engines and only typing; mouse, MIDI regions and hardware controllers are fine.

## Tools

```
acquire_probe scan                 # the library exactly as the plugin sees it
faust_probe <wasm> <json> [note] [vel] [voices]
    --measure                      # rtf, trim, articulation, hasGate as JSON
    --plan X.plan --ref X.ref.f32  # the null test — run it after engine changes
    --seq                          # ten overlapping notes, offs off block boundaries
    --set dec=0.01                 # override any published control
    --rate 44100 --block 1024      # match a host
./scripts/build.sh install         # signs, installs; Faust ON by default
```

Shell footer shows voices, notes on/off, trim and voice cap live. `notesOn`
ahead of `notesOff` means the host is not delivering them.

## Standing rules

- **Run the null test after touching the engine.** Three silent regressions last
  session, all green elsewhere.
- **A green check is not evidence until you know which path it ran** (§O3). The
  listing/loading split in `365574b` is the same shape from the other side.
- **Measure the measurement.** The articulation table was wrong first time — it
  divided noise by noise and returned +650 dB — and the wrong version was
  convincing.
- **Probe multiple pitches for level.** 44 of 120 genomes would have their trim
  wrong by >6 dB from one pitch, and the direction is inconsistent.
- **Compile or run to confirm an API exists.** `m3_GetMemory` takes `size_t*`.
- `kromosynth-projects` has pre-staged WIP: `git add -- <paths>` then
  `git commit -F msg -- <paths>` from the terminal, never MCP `git_commit`.
- `local-terminal` intermittently returns `null` for multi-line commands mixing
  variable assignment with `~`. Absolute paths on one line work. `echo alive`
  first if it recurs. `rm` is blocked — ask Björn.
