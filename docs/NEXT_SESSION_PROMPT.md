# Next session — kromosynth-plugin

Previous prompt preserved as `NEXT_SESSION_PROMPT.2026-09-01.md`.

## Where things stand

**Stage 2 exists and plays.** The plugin synthesises live from an evolved Faust
genome in GarageBand: MIDI in, wasm out, no samples. That is the one job nothing
else in the platform does — `kromosynth-live` already covers "browse, commit a
file to the session", and §C's own table says the sampler path does not need a
plugin for it.

Verified by Björn on 2026-09-01: three instruments, all audible, all stopping.

**Stage 1 (sampler) unchanged and unregressed.** Both engines ship in one signed
bundle; `auval` passes.

### The load-bearing facts

1. **The math imports must compute in DOUBLE.** faustwasm binds `_powf` to
   `Math.pow`, which is double and narrows on return. A C host calling `powf()`
   diverges by **−50.5 dB** on a math-heavy elite. This is the single most
   important implementation detail in `FaustEngine`.
2. **The engine is bit-identical to the platform reference, 33/33.** Replaying a
   Gate 0 `.plan` and comparing against that experiment's own render gives
   `maxAbsDiff` exactly 0.0 on every elite. Gate 0 measured three runtimes at
   zero against those references, so **the bar is zero, not a tolerance**. Run it
   after any change to the engine — it caught three regressions this session that
   nothing else would have.
3. **wasmtime v48.0.1**, pinned with SHA256 in `cmake/Wasmtime.cmake`. Not the
   version recorded elsewhere: the `libwasmtime.a` in that release is
   byte-identical to the copy Gate 0 measured, checked rather than inferred.
   This deliberately breaks `VENDORING.md`'s one-mechanism rule and keeps the
   reason behind it.
4. **wasmtime/Cranelift beats the wasm3 interpreter by a median 5.56×** on
   single-voice RTF over the archive (min 2.02×, max 24.49×), which at the ≥4×
   bar is a median cap of 16 voices against 4. iOS forbids JIT and will need an
   interpreter; `FaustEngine` is the seam, and the abstraction gets built when
   there is a second real consumer.

### Two findings that belong to the platform, not the plugin

**Half the archive does not articulate.** 16 of 33 elites publish a `gate` and
ignore it, still sounding 2–3 s after gate-off, worst at −6.7 dB. It is
structural, not a parameter setting: no envelope control stops the worst one at
any value in any combination. `ELITE_ARTICULATION_2026-09-01.md`.

**Level spread is 64 dB.** Reference peaks run 0.001142 to 1.792551, nothing
normalises them, and eight voices sum with no headroom. Picking a Faust
instrument is currently a coin flip between inaudible and painful.

Both are one render to compute at export and neither is recoverable afterwards.
Together with the measured single-voice RTF that `EXPERIMENT_01_3_DESKTOP_RESULT`
§2.7 already proposes carrying, that is **three numbers the artefact should
carry** and currently carries none of.

## Task — pick one

### 1. The artefact format (recommended)

Nothing produces a Faust instrument folder. `InstrumentLibrary::resolve` invents
`instrument.wasm` + `instrument.json` and those names are **provisional** — they
have to be agreed with `kromosynth-vi` before anything depends on them. The
three measured numbers above should ride along in the same change, because
adding them later means re-rendering the archive.

Test instruments are hand-built at `~/Music/Synth.is/Instruments/faust-*`.

### 2. Per-genome voice caps

`kFaustVoices` is hardcoded at 8 in `PluginProcessor.cpp`. §2.7's answer is
measured single-voice RTF carried with the artefact, from which a per-platform
cap follows arithmetically. Waits on task 1.

### 3. Gate 0's session term

`rt-sweep.mjs` in the Gate 0 probe tree is built and unrun. It needs a quiet
machine and `--label session` with a DAW playing. It decides whether the ≥4× bar
is right or should be ≥2×, which on iOS is the difference between a median cap
of 4 voices and 8. Note the cadence finding: free-running RTF is optimistic,
and by how much is exactly what this measures.

### 4. §O10 — QWERTY typing latches the host's keys

Open and fully characterised, including a measured negative. Not blocking:
mouse, MIDI regions and hardware controllers are all unaffected, and it hits
the sampler equally, so it is a Stage 0 editor defect rather than Stage 2 work.

## Tools

```
# the library exactly as the plugin sees it — no DAW, no webview
build/stage1/tools/acquire-probe/.../acquire_probe scan

# does a genome make a sound, does it stop, what does it publish
build/faust/tools/faust-probe/.../faust_probe <wasm> <json> [note] [vel] [voices]
    --rate 44100 --block 1024      # match a host
    --set dec=0.01                 # override any published control
    --seq                          # ten overlapping notes, note-offs off block boundaries
    --plan X.plan --ref X.ref.f32  # the null test

./scripts/build.sh install         # signs, installs; Faust ON by default
```

The shell footer shows `N voices · on X / off Y`. `notesOn` ahead of `notesOff`
means the host is not delivering them; equal counts with voices still active
means the engine is holding them. Those need different fixes and sound identical.

## Standing rules

- **Run the null test after touching the engine.** Three regressions this
  session were green everywhere else: idle voices silenced all 33; waking on
  `setParameter` fixed 5 and left 28; auto-idle keyed off a flag only note-on
  ever set, so parameter-driven voices idled on their first quiet block.
- **A green check is not evidence until you know which path it ran.** §O3. The
  listing/loading split in 365574b is the same shape from the other side: two
  checks that had to agree and were never made to.
- **Measure the measurement.** The articulation table was wrong first time —
  it classified percussive one-shots as drones by dividing noise by noise — and
  the wrong version was convincing.
- **Compile or run to confirm an API exists.** `m3_GetMemory` takes `size_t*`,
  not `uint32_t*`. Fourth this way across these sessions.
- `git:git_commit` via MCP behaves like `git commit -a`. `kromosynth-projects`
  has pre-staged WIP — use `git add -- <paths>` then `git commit -F msg --
  <paths>` from the terminal.
- `local-terminal` intermittently returns `null` for multi-line commands that
  mix variable assignment with `~`. Absolute paths on one line work. `echo
  alive` first if it recurs.
- `rm` is blocked by the tool sandbox; ask Björn for deletions.
