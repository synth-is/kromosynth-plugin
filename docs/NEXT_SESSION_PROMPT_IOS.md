# Next session: ship macOS, then Windows, then iOS

Rotated 2026-09-24. Previous: `NEXT_SESSION_PROMPT_IOS.2026-09-17.md`.

Read `docs/AUV3_IOS_FINDINGS.md` first — C0 through C49, newest first. It is the
record; do not re-derive it from commit messages.

**The phase has changed.** The plugin works on macOS and iOS, verified by use
rather than by reading. What remains is mostly not code: packaging, a
certificate, a store listing, and one real feature gap.

---

## 1. Why macOS goes first, and it is not about quality

iOS works well. Björn has taken sounds in AUM, played them back, seen them
arrive in Files. The gap is the **distribution gate**, not the software.

macOS: build an installer, sign it, put it on a page. Publish the day it works.

iOS: App Store Connect record, screenshots at required sizes, privacy manifest,
export compliance, App Review — and a rejection costs another round. Calendar
time, not code time.

So macOS can ship in about a focused week while iOS is a month, most of it
waiting. **Letting desktop wait for iOS is the expensive default.** Choose it
rather than drift into it.

---

## 2. macOS release — the actual list

**No installer exists.** `scripts/build.sh install` copies into the developer's
own `~/Library/Audio/Plug-Ins/`. Shipping needs a signed `.pkg` that puts the
`.component` and `.vst3` in the right places, then notarised and stapled **as
the pkg** — `scripts/notarize.sh` handles the bundles, not the installer around
them. About a day.

**`VERSION 0.0.1`** in `CMakeLists.txt:16` needs to become a number you would
put on a download page.

**A licence and a download page.** Neither exists.

Signing is already working: Developer ID, team `ATA8MQ4L7K`, "Nemur net ehf.",
and `notarize.sh` documents the prerequisites including the fact that the
Developer ID certificate is NOT the Apple Distribution one used for the App
Store.

---

## 3. Windows — a separate project, start the certificate early

VST3 is overwhelmingly a Windows format, so a macOS-only VST3 reaches a small
slice of who the format implies. Björn has access to a Windows machine.
Cross-compiling from macOS is not practical; build and test both happen there.

```
cmake -S . -B build/win -G "Visual Studio 17 2022" -A x64 ^
      -DKROMOSYNTH_BUILD_FAUST=ON -DKROMOSYNTH_BUILD_SAMPLER=ON ^
      -DKROMOSYNTH_WEB_APP_URL=https://synth.is ^
      -DKROMOSYNTH_VI_SERVICE_URL=https://vi.synth.is
cmake --build build/win --config Release --target KromosynthPlugin_VST3
```

VST3 only; AU is macOS-only.

**Three things expected to break, in order.** `cmake/Wasmtime.cmake` pins macOS
tarballs by SHA256 for `aarch64` and `x86_64` only — Windows releases exist, so
it is a slug and a hash, but then Faust genomes run on a runtime nobody has
measured and a fresh voice-count question follows. `sfizz` needs to build there.
And JUCE uses **WebView2** on Windows, a different engine — everything C43
established about drag came from WebKit, so the native `RenderList` should be
unaffected but the page should not be assumed identical.

**Signing is a different world.** Since 2023 all code-signing certificates
require the key on hardware. Azure Trusted Signing (cloud, monthly, no token) or
a traditional OV certificate plus a token. **Identity verification for an
Icelandic ehf. takes days to weeks and is the long pole — start it now, it runs
in parallel with everything else.** Reputation also accrues per certificate on
Windows, so early users may see SmartScreen warnings even when correctly signed.

For TESTERS, skip signing entirely and tell people to click through.

Prices, vendors and eligibility change; verify against current terms rather than
this paragraph.

---

## 4. The one real feature gap: sampled Faust, on every platform

**Not an iOS feature.** A desktop user wants it to freeze a track, drop CPU, get
polyphony beyond what live allows, or hold an artefact that does not depend on a
wasm runtime — the same reasons CPPN+DSP genomes already offer both. iOS is only
where it BLOCKS.

Render Faust genomes to sample-based VIs as CPPN+DSP already are, asking for
duration, octave range and velocity layers. Reuses the existing VI pipeline, SFZ
packaging and the plugin's installer path.

**Scope it with the character tags, already computed per instrument:**

- `stops cleanly on note-off`, `decays quickly after note-off` → sample fine
- `drone — sustains after note-off`, `no gate` → **skip for v1.** These need loop
  points, a real feature. An unloopable sustain sampled to fixed length is a
  worse artefact than none.

Offer both live and sampled. Live is the interactive one — deciding how long to
hold a key is exactly what a sample cannot give you.

C44's R3 (pitch is not transposition) applies, so a multisample is genuinely
different sounds per zone rather than one shifted — but the VI rendition already
copes by sampling at multiple points, so Faust is no worse.

### What the evidence for this actually says, after C48 and C49

**C47 is narrowed, not refuted.** Half of Björn's ACQUIRED LIBRARY is monophonic
on his phone — median 2 voices, 48% at exactly 1 — and C49 argues that is the
better population for a release decision than the 120-elite probe bundle, since
it is what a user holds. What it cannot claim is to describe "the live Faust
archive": the 120-elite sweep says 60% of that carries four or more voices, and
both are true because **the two populations do not overlap at all** and cannot
be joined.

**Do not repeat C48's R1.** It is not executable from the device log: the log
names instruments by takeout artefact UUID and display name, and `dsp=` is a
render-server timestamp, not a genome identity.

**And nothing was measured on the day C47 reported.** The device log is
append-only and its filename is the PULL date; the 25 fresh readings span 09-10
to 09-23 across at least seven binaries, in unknown thermal states, in a log C39
already established cannot tell AUM from GarageBand. C47 aggregated the right
subset and its figures reproduce exactly — but the three "hot peaks" are two
measurements from 09-10 and 09-11 replayed from cache, not a current
observation. "The archive has shifted" is now the weaker reading.

### What is already BUILT, as of C50 (2026-09-26)

**The sampled path runs end to end.** Two `/vi/full` jobs, 12/12 and 3/3, on real
archive Faust genomes, against deployed services. **No routing change was
needed** and none should be written: `startRenderJob`'s Faust guard tests
`config.format === 'faust'`, the artefact FORM, so a Faust genome submitted as
`format: 'sfz'` already passes. Genome fetch, sample matrix, SFZ generation and
packaging all handled the substrate unmodified.

Four defects were fixed first, because each would have shipped a broken
instrument the first time anyone tried. All committed, pushed, deployed on both
machines, and confirmed from the render log on real material rather than argued:

1. **Pitch was nine semitones sharp.** The sample matrix computes `midi - 60`;
   `renderFaust` reads `noteDelta` as absolute from A4. `renderFaustGenome` now
   takes an absolute `midiNote` (render `11c15fa`, vi `927f2fc`). The log states
   the pitch it rendered: `midi 48 / 60 / 72`, matching `pitch_keycenter`.
2. **The note-off was baked in at 70%**, so notes decayed twice and went to the
   float floor for the last fifth. Gate now held for the whole render on the
   sampler path (vi `e8cd360`); log reads `gate off at 100%`.
3. **No edge fades on the Faust branch**, unlike the other two render paths, so
   a held gate ended the buffer at 0.79 full scale. Opt-in `edgeFade`, DEFAULT
   OFF because the QD loop scores this output (render `ce532db`). All 15 samples
   end at exactly 0.00e+00.
4. **`autoLoop` was accepted and ignored** — which is exactly the loop-points
   gap above. Now refused with `AUTOLOOP_NOT_IMPLEMENTED` (vi `3502333`).
   Behaviourally free: every caller hardcodes `false`.

### The finding that changes the PRODUCT, not the code

**Round robins and velocity layers produce nothing but copies for a Faust
genome, and both are priced.**

Round robins come back byte-identical — same SHA256, six pairs — and the SFZ
writes a real `seq_length=2` alternation between the duplicates. A Faust render
is deterministic given the same source and parameters, and with
`layerParamsPerSound` null nothing varies.

Velocity layers differ by 2–3×10⁻⁵ after level-matching, which is the 16-bit
quantisation floor (1/32768 = 3.05×10⁻⁵). Velocity reaches a Faust render only
as `setParam('gain', v)` — a scalar on the output.

Of one run's 12 samples, **3 are distinct audio**. Extraction quota bands by
sample count (4/8/16/32), so a user pays a higher band for copies.

**Fix it server-side**, in `calculateSampleMatrix`: force `velocityLayers` to 1
and `roundRobins` to 0 for `substrate === 'faust'`, and count distinct renders
for quota. Preferred over hiding the controls in the panel, because the panel is
not the only caller and a server that returns copies will return them to whoever
asks. Hide them in the panel too, so the UI does not offer what the server will
collapse.

Making velocity MEANINGFUL for Faust is a separate, real feature: it needs
mapping to something the genome's term tree exposes, not to output gain.

That is now THREE of the VI panel's four sample-shaping controls that do not
survive the substrate change. Only pitch range clearly applies.

### Still open on this path

- **Tuning, against a HARMONIC seed.** Two attempts to verify it from audio
  failed on a scrape and then a bell; see §9. The interval is confirmed — midi
  60 → 72 measured +1198.7 cents, an exact octave within 1.3 cents — so the
  mapping is linear in semitones. Absolute tuning needs a `pm.*` template with a
  known fundamental, not an archive elite chosen by class label.
- **The drone exclusion**, still unbuilt. Enforce it in `startRenderJob`:
  viable because extraction quota is charged at TAKE, not at submit
  (`takeoutRoutes.js:600-636`), so a refused job costs nothing. Articulation is
  NOT available per sound — it is computed in `measureFaustArtefact` at artefact
  build time and nothing persists it — so either measure at job start or
  backfill `measured` onto the sound.
- **Live versus sampled in the UI.** `useObtainInstrument`'s
  `needsConfiguration` is currently a pure function of substrate. That is the
  assumption that has to go.

Full record with every number:
`kromosynth-projects/Evolutionary Sound Discovery Platform/faust-genome-substrate/SAMPLED_FAUST_FIRST_RUN_2026-09-26.md`

---

## 5. Small, and each is one thing

**`KROMOSYNTH_MEASURE_EPOCH` in the measurement cache key.** C49 found the key
does not include the plugin build: `runtimeId()` is the wasm3 version plus OS
and arch — the runtime LIBRARY, not this plugin's embedding of it. So two builds
linking the same wasm3 share cached rtf even if one changed the host-function
bindings or the measurement loop. `PluginProcessor.cpp:1628` already reasons
this way for the cap and fixes that half. **Do this before trusting a voice
count again.**

**The macro rename reading.** `macro_rename_requested` now logs only the intent,
because the old line asked JUCE what it had just been told and agreed 149 times
while proving nothing. The host-side result is not observable from inside the
plugin. The reading: open AUM's parameter list, load a Faust genome declaring a
slider like `att`, and watch whether the name changes. If it does not, renaming
is dead and a fixed pool of `Macro 1..8` with the mapping in our own UI is the
design. Björn has seen a parameter still called "macro" in AUM, which points
that way.

**Verify MIDI learn** on `attack/decay/sustain/release/Output` in AUM. Exposing
them as host parameters IS what makes them MIDI-controllable, so this may
already be true and statable as a feature.

**Host-generic parameter UI for v1.** Custom knobs need bidirectional sync —
host automation reaching the page, page edits reaching the parameters — which is
C40's unsolved resync problem. Generic is ugly and correct.

**The preview level, deferred by Björn.** `usePreviewAudio.js`
`DEFAULT_PREVIEW_VOLUME = 0.25` means the web preview plays 12 dB below unity
while a dragged WAV plays at unity. That is the "volume and depth" difference;
quieter audio reads as thinner. One line when someone decides whether 0.25 is
deliberate. **Level-match before looking for a second cause.**

**The three hot peaks** — `raw_peak` 415.70, 341.74, 27.73 against a guard
saying nothing exceeds 2.4 — are still worth explaining, but see §4: they are
old cached measurements, so re-measure before theorising.

---

## 6. Pricing and payment, as it stands

`kromosynth-recommend/docs/QUOTA_AND_PRICING.md` is the reference. **Its §5 is
already partly stale** — the payment session has since pushed commits that make
parts of it real, including one rejecting §5.2. Re-read the code before trusting
that section.

**Decided: quota is purchased on the WEB ONLY.** The iOS app is a client for an
account you already have. Revisit if something becomes apparent.

**Act on this before the Paddle work ships:**

> In the plugin, and especially the iOS AUv3, the out-of-quota path says you are
> out and when it resets. It does NOT offer a way to buy, link to pricing, or
> explain where to.

`UpgradeModal` currently says "no payment processing yet" and offers a
manual-grant request, so today it is clean. `isPluginHost()` exists; it is a
branch in one component. Cheap now, awkward to retrofit, and it is the one
screen with App Review exposure.

---

## 7. TestFlight

Audiobus and Loopy Pro forums. That audience already owns an AUv3 host, which is
why those forums are right and a general music forum is not. Internal testers
(100) need no review; external (10,000) need Beta App Review.

Go in with one question rather than "try it": **"you take a sound in AUM — how
much does it bother you that you have to open Synth.is before it shows up in
Files?"** That is the judgement no testing session can make. Björn is also
seeking testers on Windows and macOS.

Recruit on iOS; do not conclude iOS is the flagship. The product is currently
stronger on desktop — drag straight to the timeline, one gesture, two hosts.

---

## 8. What shipped, so it is not rebuilt

Delivery is done on both platforms. `synthis://render` verb, `RenderOutbox`, the
cold-launch drain; Library sub-tabs **Instruments | Sounds**; `RenderList` native
and draggable on desktop, the HTML list on iOS; a dismissible one-row drag handle
while browsing; auto-take on render; `Take` labelled with its cost.

Quota: extraction charged **per sound** not per artefact, capped at 4 per entry,
`extractionsCharged` in the response. Live and validated in production.

Verified in use: VST3 in Reaper, AU in GarageBand, AUv3 in AUM.

**C34: the AUv3 icon now shows, cause unknown.** Nothing was changed for it.
Observed-working, not closed — check once per release.

---

## 9. Things I got wrong, so you do not repeat them

Earlier rotations carry the full list. Recent ones:

**I said I had written the previous prompt to disk and had not.** It was printed
in chat, the file was left at the 2026-09-15 version, and a correction header was
then committed onto the stale file. Nothing was lost because the chat copy was
pasted forward — but the claim was false. *Writing a file and saying you wrote it
are two acts.*

**Three defects in `RenderList` were found by use, none by reading.** Delete
never deleted: `NativeMessageBox::showYesNoBox`'s int-returning form only blocks
under `JUCE_MODAL_LOOPS_PERMITTED`, off in plugins, and it compiled because both
overloads take the same five arguments. Written from the docs instead of the
build flag.

**I blamed the plugin's `Output` default for the preview/timeline level
difference.** Neither path goes through the plugin's audio. A plausible number
applied to a quantity nobody checked was in the signal chain — the same mistake
as reading `trim 0.0 dB` as evidence of a stale binary.

**I replaced a poll with `ComponentMovementWatcher` and called the original
reasoning an unnecessary hedge.** The hedge covered host window moves, which the
watcher is blind to. Both are needed.

**Three comments asserted behaviour the code did not have** — sub-tabs "hidden
in strip mode", a `#toolbar` rule against a `class="toolbar"` element, and a
`dryRun` that would have orphaned a doc comment.

**C48 called C47 "the outlier" before checking whether the populations were the
same.** They were not, and could not be joined. *A disagreement between two
measurements is only a disagreement if they measured the same thing.*

**I told Björn "nothing pushed" five or six times and never once checked.** Three
of four repos were already on Bitbucket — something in the environment pushes
after commits, `branch.main.vscode-merge-base` is set and there are no hooks. The
claim was asserted, not observed, which is the exact failure I had spent the
session writing into other people's commit messages. *Saying a thing did not
happen is a measurement.*

**I tried to verify tuning from audio twice, on inharmonic genomes.** A scrape,
where autocorrelation returned ~65 Hz for two pitches an octave apart; then a
bell, where a harmonic-sum estimator locked onto partials. "In tune" is not well
defined for an inharmonic genome, and a pitch word in the class label does not
make an archive elite a tuning subject. The answer was in the render log the
whole time, because I had added it there that morning. *Check whether the
quantity is already being reported before building an instrument to infer it.*

**I restarted one render worker and nearly tested against three stale ones.**
`RENDER_INSTANCES` fans `/vi/full` across four workers on two machines; the job
would have round-robined onto old code and the result would have been
uninterpretable. Deployment is per-fleet, not per-host.

---

## 10. Conventions

Explicit pathspec on every `git add` and `git commit`; the MCP `git_commit` tool
behaves like `git commit -a`. Commit message files under
`/Users/bjornpjo/Developer/`. `filesystem:edit_file` with `dryRun: true` first;
anchor on the comment AND the declaration together. MCP `local-terminal` times
out around four minutes — and **`nohup … &` does NOT get you past it**, which
this file said until 2026-09-26. The runner waits on the whole process group, so
the `/bin/sh -c` parent stays alive and the call times out anyway; use `setsid`,
or redirect stdin from `/dev/null` and disown. Inline sleeps under ~100s.
**A script that calls `renderFaustGenome` never exits** after producing its
output — two were found still burning 35% CPU twenty minutes later. Check for
orphans after running `validate-levelling.js`, `measure-audition-parity.js` or
anything else of that shape. `pm2 env` takes a NUMERIC id, not a name.

**Three machines, three repo roots.** Laptop
`/Users/bjornpjo/Developer/apps/synth.is/`, Studio
`/Users/bthj/Developer/apps/synth.is/`, hub `/Users/bthj/Developer/synth.is/` —
the hub has NO `apps/` segment. Anything scripted against these paths silently
misses on one machine.

**`git pull` over non-interactive SSH cannot reach the keychain.** The helper is
`osxkeychain` and the Bitbucket item exists, but a `BatchMode=yes` session gets
`failed to get: -25308`. Pulls on the Studio and hub have to be run by hand in
Terminal.app — same class of constraint as the PM2 TCC issue. Do NOT work around
it by copying files over SSH; that leaves a production checkout divergent from
its own history.

**Deployment is per-fleet.** `kromosynth-render` runs on BOTH machines (hub
`kromosynth-render-1` and `-preview`, Studio `-2/-3/-4` on 3006/3007/3008), and
`kromosynth-vi`'s `RENDER_INSTANCES` fans across four of them. Restart all of
them or the test lands on stale code. `kromosynth-vi` is hub-only, port 3090.
Scripts run by hand on the hub need `tools/run-with-service-env.js`, because the
hub's `.env` ARCADEDB credentials are stale and silently unused.

**Other sessions run concurrently in these repos.** C48 and C49 arrived as
uncommitted work in the findings file while this session was writing to it.
Check `git log` and `git status` at both ends of a session, and do not commit
another session's half-finished work — unless, as with C48/C49, the chat is lost
and the alternative is discarding it.

**Build targets, two of which are not what you would guess.**
`build/desk-verify` is Ninja with NO Standalone target — use
`--target KromosynthPlugin`. `build/acquire-probe` is Makefiles, target
`acquire_probe`, and its selftest is the fastest real check in the repo:

```
./build/acquire-probe/tools/acquire-probe/acquire_probe_artefacts/Release/acquire_probe selftest /tmp/acq
```

**Page layout is checked in a browser, never from source:**
`node tools/shell-preview/build-preview.cjs`, then open the generated file. Test
at 900x620, 428 wide, and 736x242.

**iOS:** `./scripts/ios-build.sh` then `./scripts/ios-install.sh`. The install
gate refuses while an AU host holds a live process — **open the host, then swipe
it away**; swiping alone often does not end it. A new source file needs TWO
passes under the Xcode generator.
