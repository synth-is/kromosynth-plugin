# AUv3 on iOS — findings

Companion to `STAGE0_FINDINGS.md`, which is the macOS host log (GarageBand,
Reaper, Live, in-process and XPC). This one is iOS and AUv3 only. Same
convention: dated entries, newest at the top, wrong claims left standing with
the correction next to them rather than edited away.

**Environment:** iPhone 13 Pro Max (A15, iOS 18.7, UDID
`00008110-000120D9228A801E`), Xcode-beta 27.0 (27A5252f), iPhoneOS 27.0 SDK,
deployment target 16.0, JUCE 9.0.1, team ATA8MQ4L7K. Build dir
`build/ios-auv3`, Xcode generator, Release, wasm3 backend.

**Hosts tested:** GarageBand for iOS, AUM. **Not tested:** Cubasis, Loopy,
Drambo, Logic for iPad.

---

# C0. STAGE A VERDICT — the App Group entitlement is live end to end (2026-09-08)

Commit `58629e9` added `AUv3` to `FORMATS` and declared the App Group. Every
link in the chain is now measured rather than assumed:

| Link | Evidence |
|---|---|
| portal | two explicit App IDs, group assigned to both |
| profile | `iOS Team Provisioning Profile: is.synth.plugin` and `…pluginAUv3`, both carrying `application-groups` |
| signature | `codesign -d --entitlements` on BOTH bundles shows `group.is.synth.plugin` |
| integrity | `codesign --verify --deep --strict` — valid, satisfies its Designated Requirement |
| **runtime** | **iOS created the container on device at install: `Library/`, `Library/Caches`, `Library/Preferences`, writable** |

The runtime row is the one that matters. Every row above it could have passed
with the container still unreachable. Static checks prove the entitlement was
*requested and signed*; only the container on disk proves it is *effective*.

**The extension bundle ID was predicted from source before being measured.**
`juceaide/Main.cpp:324-334` appends `"." + <last dotted segment> + "AUv3"`;
`JUCEUtils.cmake:834-841` regex-derives the same string. Both said
`is.synth.plugin.pluginAUv3`. The built appex `Info.plist` agrees. Recorded
because a guessed identifier fails at install with an error that points at
provisioning rather than at the string.

**Install is non-destructive.** Same bundle ID and team, only the profile
changed, so iOS treats it as an update. Verified: the app's
`Documents` listing is 37 entries identical before and after
(`/tmp/auv3-stage-a/container-{before,after}.txt` at the time; re-derive rather
than trust that path). Worth keeping because a wiped container would have cost
re-downloads against extraction quota, not just time.

---

# C28. ELIMINATED — the AU icon is not `AudioComponentBundle` either (2026-09-10)

C24's surviving hypothesis is dead. **There is no shortlist left.**

The run C24 asked for, finally done: `AudioComponentBundle` stripped from the
built appex's `NSExtensionAttributes`, appex then app re-signed inside out with
entitlements preserved, installed 12:09:27, AUM force-quit beforehand and opened
after. **AUM still draws its no-icon placeholder.**

**The install is proven, which is what makes this an elimination rather than
another inconclusive run.** C0's trap is that a stale binary looks exactly like
a fix that did not work, and `processor_constructed` does not distinguish them.
Here the log does: pid 9029 at 12:14 writes `block_over_budget` lines, a string
that did not exist in any binary before 2026-09-10. So the stripped build ran.

Also confirmed before installing, by measurement rather than argument: the
re-signed copy's `__TEXT,__text` section is byte-identical to the plain build's
(`4b64b922744299ff24b90189b412c9ab` both). Only the signature and the removed
plist key differ, so nothing measured on it is affected by the re-sign.

**What this leaves.** The key was mechanically consistent with the symptom — it
named `is.synth.plugin.internal`, and no bundle by that name exists anywhere in
the shipped app. It was still not the cause. Mechanically consistent is not
demonstrated, and this is the entry that proves the distinction was worth
keeping.

Repeatable with `./scripts/ios-icon-experiment.sh`, which refuses to run if the
premise is gone. **A reboot has not been tried since the strip** — C24's reboot
eliminated the registry cache as a cause of absence, which is not the same as
establishing that a corrected plist would be picked up without one. That is the
one loose thread.

---

# C27. C22 ANSWERED — the overruns are the genome, and the denominator was wrong too (2026-09-10)

Measured in AUM on the device, 19 over-budget blocks captured across three
loads.

**The pre-registered question was whether an overrun could be told from a load
transient. It can, and it is not one.**

    block=7400  voices=0      block=26890  voices=0     block=103645 voices=2
    block=8994  voices=3      block=28579  voices=1
    block=9312  voices=2      block=28655  voices=0
    block=11679 voices=1

Nothing near `block=3`. These run from block 7,400 to block 103,645 — deep in
the session, nowhere near the suspend barrier. **The transient hypothesis is
dead.**

The 12:14 load is unambiguous: **eight overruns between block 2454 and 2727,
about 1.5 seconds, every one at `voices=3`**, load 1.13 to 1.40, on an
instrument whose declared cap is 3. At its own cap, in a real host, it misses
the deadline repeatedly.

**NOT a verdict on the cap.** No bar was pre-registered, and converting this
into one now is how the 4x bar acquired its authority. What the data supports
without a bar is narrower and still worth stating: *the cap permits sustained
deadline misses at the cap.*

## And the denominator was wrong the whole time

The footer read `load 35% of 42.67 ms` while the log read `budget_ms=5.33
frames=256`, in the same second, from the same class.

`engineState` computed `budgetMs` from `getBlockSize()` — the host's **declared
maximum**. `recordBlockLoad` computes its fraction from frames **actually
delivered**. AUM declares 2048 and delivers 256, so the editor overstated the
available deadline eightfold, beside a correct percentage. Fixed in `704bd1e`,
in both places that had it.

**C22 named two reasons its numbers could not be read and this was a third it
did not know about.** The capture answered *which blocks*; this answers *out of
how long*. Every "of 42.67 ms" reading so far, C22's own included, was labelled
against a deadline the host never used. The percentages stand — they were always
computed against real frames — only the labels were wrong.

**The real regime is 5.33 ms, not 42.67 ms.** Any cap reasoning starts there.

---

# C26. THE VOICE-CAP LADDER HAD THREE IMPLEMENTATIONS, AND TWO OF THEM CHECKED EACH OTHER (2026-09-09)

C20 closed with "one policy has two disagreeing implementations". It was three,
and the third is the reason the disagreement was invisible.

| where | state before | now |
|---|---|---|
| `FaustEngine::capForRtf` | arithmetic | unchanged (`b1e9cfb`) |
| `kromosynth-render/.../faust-artefact.js:274` | ladder | arithmetic (`94e7471`) |
| **`tools/faust-probe/main.cpp:416`** | **ladder** | arithmetic (`884e0c3`) |

**`b1e9cfb` fixed the engine and missed the probe in the same repo.** Not found
by reasoning about it -- found by grepping for `v *= 2` across every repo while
looking for the render server's copy, which is the C25 lesson applied on
purpose rather than in hindsight.

**Why the third one matters more than the second.** `faust-probe` produces the
reference JSON that `kromosynth-render/render-socket/src/test-faust-artefact.js`
compares its own numbers against. Both sides carried the identical ladder, so
both were wrong in the same direction and therefore **agreed** -- and an
agreeing cross-check reads as confirmation. A comparison between two copies of
one mistake cannot detect it. That test had been passing on this field the whole
time.

**Two defects in each ladder, not one.**

    ladder    v stepped 1, 2, 4, 8, 16, 32, keeping the largest value clearing
              the bar -- the largest TESTED value, not the largest value.
              rtf 14.7 permits 3 and gave 2. rtf 100 permits 23 and gave 16.

    cap = 0   the initialiser. Below rtf 4.2 the loop fell through untouched and
              the cap was reported as ZERO voices -- an unplayable instrument.
              The measurement did not say that. It said the genome is too
              expensive for 4x headroom, and one voice is still the answer.

The second is the one that would have been hardest to attribute later, because
`applyFaustTrim` does `jlimit(1, 32, faustDeclared_.capAt4x)` and silently
clamps a declared 0 up to 1. The wrong number was corrected downstream and
therefore never surfaced as a disagreement between the two sides.

**Verified rather than asserted.** `rtf / (v * 1.05) >= headroom` is exactly
`v <= rtf / (1.05 * headroom)`. At headroom 4.0 that reduces to
`floor(rtf / 4.2)` clamped to 1..32, which is `capForRtf`'s body character for
character -- checked against it at 500001 points over rtf 0..5000, identical at
every one. That check is what to rerun if either side moves again.

**Written as `1.05 * headroom`, not folded to 4.2**, in both copies. `capAt2x`
comes from the same lambda, and a folded constant would put two policies back
inside one file.

**STILL TWO IMPLEMENTATIONS, and deliberately.** `capForRtf`'s own
documentation claims "one formula, one place" and that is not true yet: it
hardcodes 4.2 and answers headroom 4.0 only, while the probe also reports 2x.
Collapsing them means giving `capForRtf` a headroom parameter, which touches
`FaustEngine.cpp` and sits behind the 120/120 null test. **The numbers agree
across all three copies now; the structure is one commit behind.** Left for a
session that can run the null test rather than done half-way.

**Not deployed.** `94e7471` is committed and not pushed or restarted. Artefacts
already on disk keep whatever cap they were minted with, and the plugin
re-derives from stored `rtf` rather than reading `capAt4x` back (`b1e9cfb`), so
installed instruments are unaffected either way.

---

# C25. MY OWN RECURRING ERROR — inferring a missing mechanism from a missing log line

Five times across two days I claimed a capability did not exist because I could
not see its output, and five times the codebase had already built it:

| claim | reality |
|---|---|
| acquisition is not instrumented | `acquisition_sentinel` logs on every path (C11) |
| the `show-login-modal` listener is unmounted in the plugin | `App.jsx:1450` keeps `NavigationBar` mounted precisely so it survives |
| the indicator cannot render a code-only `AUTH_REQUIRED` | it handles exactly that case |
| there is no live Faust render peak | `FaustEngine::peakSinceReset()` since §4.2, called from nowhere |
| the three surfaces offer different VI formats | one shared `InstrumentDownloadButton` with a dynamic format list |

Plus two tooling errors of the same shape: `find -maxdepth` after `-name`, which
BSD find ignores, and trusting devicectl's frozen container view over the
running system (C16).

The fix is procedural and cheap: **grep the source before asserting a thing does
not exist.** Absence from a log means the layer does not report; absence from
`grep` on one spelling means nothing at all. This entry exists because catching
it in oneself is the only version that generalises, and five times is a pattern
rather than a slip.

---

# C24. FINDING — branding ships, and the AU icon still does not appear

`3e67906` added `ICON_BIG`. Both bundles carry a compiled `AppIcon`, 24 entries
in each `Assets.car`, `CFBundleIconName` set in both plists.

**The app icon works: it is on the home screen.** **The AU icon does not: AUM
still draws its no-icon placeholder**, alongside two older Nemur builds that
never had icons. A device reboot changed nothing, which eliminates the
AudioComponent registry cache.

So the app-icon pipeline is fine and the AU's icon RESOLUTION is not. The
surviving hypothesis is the one flagged in `58629e9` and wrongly retired in C7:
`AudioComponentBundle = is.synth.plugin.internal` in the appex's
`NSExtensionAttributes`, naming a bundle JUCE only creates on Darwin
(`juce_PlistOptions.cpp:377` unguarded, `JUCEUtils.cmake:1459-1471`). C7 retired
it because the AU instantiated; instantiation and icon lookup are different code
paths, and the crash in C15 was in `requestIcon`. **Over-generalising that
retirement was a mistake.**

Not a `juce_add_plugin` keyword, so fixing it means a post-build plist edit plus
re-signing, or patching vendored JUCE. **Untested.** The experiment is one
throwaway run: strip the key from the built appex plist, re-sign, install, look.
Do that before building either fix.

Submission item, unrelated: `juceaide` puts an alpha channel back into every
generated slot even from an RGB source. Fully opaque (extrema 255..255), but
Apple has historically rejected any alpha on an app icon. Whether `actool`
strips it is untested.

---

# C23. FINDING — the per-note gain can clip the quiet end of the keyboard

Reported as "a bit distorted in the upper half" on `#DDJ9Y`. Its measurement:

    c3=1.1160  a4=0.6847  a5=0.2743  spread_db=12.2  trim=1.0000

`trim=1.0` means **no attenuation was taken at all**, while the per-note gain
equalises every key to the a4 reference. So notes near a5 are boosted by
0.6847/0.2743 = **2.5x** with no headroom in hand, and any moment where the true
signal exceeds the short measured peak clips.

Mechanism only. The fix is a real decision -- clamp the boost, or take trim
headroom when the spread is large -- and is deliberately not made here. Note
that a 12.2 dB spread is mild for this substrate: `FaustEngine.h` records 29.1 dB
on one archive genome.

---

# C22. THE DEADLINE METER — built, and its first numbers are NOT yet interpretable

`dc5cb17` measures wall time in `processBlock` as a fraction of the block
period, which is the quantity `capForRtf` has always been a proxy for. Prompted
by Björn asking, of a reasoned claim that a 2-voice cap was right, "could we
know?"

First readings, AUM, 2026-09-09:

    #FJF8V  3-voice cap (RTF 15x)  peak 142%  22 OVER BUDGET of 4065 blocks
    #RPGW9  2-voice cap (RTF 12x)  peak 115%  12 OVER BUDGET of 4804 blocks
    both:   load 0%  of 42.67 ms

**AUM gives 2048-frame blocks.** With GarageBand's 256 and the standalone app's
64, block sizes across our surfaces span **64 to 2048, a 32x range**. A cap
derived once at 512 free-running was never going to fit that.

**But the question is not answered, and the design is why:**

- `load 0%` in both, because both were read with **0 voices** sounding. The
  rolling meter clears on read, so a poll between notes reads zero. The reading
  needed is with notes held.
- 142% of 42.67 ms is a 60 ms block. At RTF 15 a block should cost ~2.8 ms per
  voice, about 7%. So 60 ms is ~20x steady state and smells like a TRANSIENT --
  the first block after `suspendProcessing(false)`, a page fault, a thermal
  event. 22 in 4065 is 0.54%, consistent with a few one-offs.

**So the counters cannot yet separate "this genome is too heavy" from "loading
costs one late block", which is the whole question.** Next: log the first few
over-budget blocks with timestamps so they can be correlated against
`instrument_load` in the same file. Until then these numbers should not be used
to argue either way about the cap.

---

# C21. FINDING — "you have to tap a new instrument to activate it" is a 9-second audio suspension

Not autoload, which works. `#RPG29`, 2026-09-09:

    11:12:47.406  acquisition_queued 2ac089a6
    11:12:47.894  acquisition_autoload            <- 488 ms
    11:12:47.916  instrument_load ok=1 detail="loaded"   <- 22 ms later
    11:12:56.841  faust_measure rtf=11.7 measure_ms=8918.5   <- NINE SECONDS

`PluginProcessor.cpp:712` suspends processing and, in its own words,
"suspendProcessing(false) MOVES WITH THE MEASUREMENT" -- resuming earlier would
open the barrier while the measuring render still owns the engine. Sound
reasoning. But the comment was written against "the one to three seconds a
105-second measurement takes", and on an A15 under wasm3 it is **8.9 seconds**.

So: acquire, load in half a second, silence for nine seconds, user taps, the
measurement is now cached (`measure_ms=0.0`) and it works instantly. The tap
looks causal and is not.

**And the shell already knows.** `measuring_` is an atomic "read by the editor"
and the load outcome carries `measuring=true`. Nothing displays it. Cheapest
honest fix is to say so while it happens; not suspending at all is
architectural and risks the race the barrier exists to prevent.

---

# C20. THE BADGES SAID THINGS THEY DID NOT MEAN

Two, same failure: an internal value printed as a user-facing fact. `28f0c12`.

**`silent` is the BEST-behaved articulation, not a dead sound.** From
`faust-artefact.js:282` the classification is tail level after note-off:
`silent` < 0.001 stops cleanly, `quiet` < 0.032 decays quickly, else `drone`
sustains. The badge expanded only `drone` and printed the other two raw. Three
working instruments were reported as "not responding to notes" on 2026-09-09 for
this reason; their measurements show healthy output at all three probe pitches.

**"8 voices" was another machine's number.** `capAt4x` in the sidecar derives
from an rtf measured on the render server's V8, and `faustArtefact.js` is
explicit that "the V8/wasmtime ratio ranges 0.48-0.99 depending on the genome,
so there is no factor to convert it". Its own text summary says "Indicative
polyphony" and "Your machine will differ". The badge said `8 voices` for a genome
the phone capped at 2. Now `est. 8 voices`.

**Related, still open:** `faust-artefact.js:274-279` computes `capAt4x` with the
same power-of-two ladder this repo removed in `b1e9cfb`, so new artefacts still
carry ladder estimates and one policy has two disagreeing implementations.
Different repo, deployed service.

---

# C19. THE VOICE CAP — a ladder, and a persisted conclusion

Two commits, and the second is the interesting one.

**`b1e9cfb`: the ladder.** `capAt4x` stepped `v *= 2`, testing only 1, 2, 4, 8,
16, 32, so the cap was the largest TESTED value clearing the 4x bar rather than
the largest value clearing it. rtf 14.7 and 15.8 both permit 3 and got 2; 24.5
permits 5 and got 4. `rtf / (v * 1.05) >= 4.0` is exactly `v <= rtf / 4.2`, so
no loop was needed. `NEXT_SESSION_PROMPT.2026-09-01.md` had already specified
"a CONTINUOUS answer" and "derive the per-platform cap arithmetically" -- the
ladder contradicted the recorded design.

**`6e13c93`: and on the device the cap stayed at 2.** Because `capAt4x` was
PERSISTED. The measurement cache validates "this runtime, this rate, these exact
module bytes -- nothing the measurement depends on has changed", and a change to
the FORMULA depends on none of those. Every check passed and the cache kept
serving the previous build's conclusions.

**The generalisable mistake is storing a conclusion beside the observation that
produced it.** rtf is measured and worth trusting from disk; the cap is what
this build concludes from rtf, and storing a conclusion freezes it. One formula
now lives in `FaustEngine::capForRtf`, called by `measure()` and by every read
path.

Verified on the same cached measurement -- identical rtf, identical peaks,
`measure_ms=0.0` both times:

    10:30:52  faust_measure rtf=15.0 voices=2   (old build)
    10:47:27  faust_measure rtf=15.0 voices=3   (new build)

**The 4x bar is UNCHANGED and that is deliberate.** The rtf is free-running, and
`d3321de` measured a real block cadence costing 2.3-2.9x more, so 4x
free-running is nearer 1.4-1.7x in a host. Whether the bar is right is what C22
exists to answer.

---

# C18. STAGE C, AND A HARNESS THAT COULD NOT FAIL

**`0e8b93e`: concurrent log appends LOSE lines, not merely interleave them.**
C12 understated it. `File::appendText` builds a `FileOutputStream`, and
`FileOutputStream::openHandle` on POSIX opens `O_CREAT | O_RDWR` -- **no
O_APPEND** (`juce_SharedCode_posix.h:581`) -- then seeks to the end and writes at
the offset it just remembered. Two processes each seek to EOF and write THERE.

Fixed: one `write(2)` on an `O_APPEND` descriptor, `stage0-<host>-<wrapper>.log`
so Standalone and AUv3 no longer share a name, and `pid=` per line because the
filename separates hosts and formats but not instances. Also fixes a latent
desktop collision nobody had hit: our AU and VST3 in one host were one process
writing one filename. Does NOT fix `STAGE0_FINDINGS.md:444` -- GarageBand and
Logic both host AU in `AUHostingServiceXPC_arrow` -- but their lines now survive
and are attributable.

**`a7bd1bc`: and the null test reported IDENTICAL for code it had never
compiled.** The probe build FAILED (`build/wasm3` cannot compile faust_probe --
`main.cpp` wants `wasmtime.h` and that configuration lacks it) and the script ran
the months-old binary beside it, reporting PASS 120 FAIL 0. `find | head -1`
picked whichever of three probes find reached first, and nothing compared the
binary's age to the source.

Now: newest by mtime, overridable, **refuses** when `FaustEngine.cpp` is newer,
and prints which probe ran. Negative control run before trusting it. The
staleness guard is the same one `scripts/build.sh` already applies to a running
host -- applied to the harness rather than the product.

**A harness that cannot fail is worse than no harness, because it is believed.**

---

# C17. STAGE B VERIFIED — an instrument acquired in the AUv3 appears in the standalone app

Acquired inside AUM, then Refresh in the standalone app revealed it there.

The test is unambiguous because of what the standalone app is NOT doing: it has
acquired nothing today, and after Stage B it no longer reads its own private
`Documents`. Its own four instruments are invisible to it. So anything it lists
can only have come out of the shared container, written by a DIFFERENT PROCESS.

    19:12:03  wrapper=AUv3        library=appGroup   <- wrote it
    19:12:40  wrapper=Standalone  library=appGroup   <- reads it

This is the requirement §3a is actually about. Refresh was needed because
`scan()` runs on demand rather than watching the directory — a list rendered
before the download landed is stale, not empty.

**Not migrating was the right call.** Because both surfaces started at 0, a
single acquisition appearing in both proves SHARING. Had the four been migrated
first, the same observation would only have proved that a copy happened.

---

# C16. RETRACTION of C8 — devicectl's App Group container view is frozen at install time, and lies plausibly

C8 claimed `devicectl --domain-type appGroupDataContainer` can read the shared
container. **It cannot be trusted.** Across the entire session it reported the
same three entries, all dated 15:20 — the install moment — and never changed:
not when both processes began resolving `library=appGroup`, not after six
acquisitions, not while the instrument was visibly listed in the app.
`--username mobile` returns the identical frozen answer.

**A plausible-looking wrong answer is worse than an error**, and this is the
second-order version of the pattern already catalogued in C6 and C11: not a
layer that fails to report, but an OBSERVER that reports confidently about
something it is not seeing. It cost real damage:

- I asserted that instrument `70f23e40` existed in none of the three
  containers, and treated that as a contradiction with `acquisition_autoload`
  and `instrument_load ok=1`.
- I proposed instrumenting `fetchAndInstall` as the way to resolve it. The
  gap is real (C6) but the urgency was manufactured to explain a phantom.
- I trusted the tool over the running system, having spent the session
  insisting on the opposite.

**Ground truth for the shared container is the plugin itself**: the count and
the path the shell displays via the `getLibraryRoot` native function, plus
`library=` in the log. Those agreed with each other and with reality throughout.

**REOPENED as a result:** the Stage B trade-off C8 claimed to close. Moving the
library into the group container does lose Finder and Files.app visibility
(`FILE_SHARING_ENABLED` covers only the app's own `Documents`), and there is now
NO established way to read it from a Mac. `InstrumentLibrary.cpp`'s comment
currently cites the devicectl command as the answer; that citation is wrong.

---

# C15. FINDING — AUM died in AudioComponentCopyIcon after a reinstall under a live host

    exception   EXC_BAD_ACCESS / SIGSEGV
    subtype     KERN_INVALID_ADDRESS at 0x3eebc86a03a8eeec
                (possible pointer authentication failure)
    thread 0    com.apple.main-thread
      0  AudioToolboxCore  AudioComponentMgr_NSExtension::requestIcon(...)
      1  AudioToolboxCore  AudioComponentCopyIcon
      2  AUM               ...
      6  CoreFoundation    __CFNOTIFICATIONCENTER_IS_CALLING_OUT_TO_AN_OBSERVER__
     10  Foundation        -[NSNotificationCenter postNotificationName:...]

Report `AUM-2026-09-08-191128.ips`, two seconds after `acquisition_autoload`.
Not our code, and nothing to do with the library path — but caused by our
procedure: a new build was installed at 19:00 while AUM had been running since
18:37, so it held `AudioComponent` references into a bundle that was replaced
underneath it, and asked the system for the icon of a stale component on the
next registration-changed notification.

The appex log stopping at `measuring=true` was a CONSEQUENCE, not a cause: the
extension was torn down with its host before the measurement could log.

**Rule:** do not install under a live AU host on iOS. Quit the host first.
`scripts/build.sh` already guards the desktop equivalent by warning when a
running host predates the installed binary; iOS has no such check, and adding
one is cheap.

Also worth keeping: the PAC-failure signature reads like a use-after-free in
our own code, and the first instinct was to suspect the change made minutes
earlier. The backtrace cost one command and pointed somewhere else entirely.

---

# C14. RESOLVED — C11's cause was auth, and the whole acquisition chain works inside an app extension

Signed in inside the AUv3's site view, the same button worked. Full trace from
the appex log:

    18:40:28.393  acquisition_sentinel synthis://acquire artifact=a3abb066-...
                    name="Virtual Instrument #MBDZW" ticket=present
    18:40:28.394  acquisition_queued a3abb066-...
    18:40:28.561  acquisition_autoload id=a3abb066-...
    18:40:28.566  instrument_load kind=faust ok=1 dsp="faustgenome_mtt0lnag_4"
                    voices=1 gate=1 detail="loaded"
    18:41:02.081  instrument_load kind=faust ok=1 dsp="faustgenome_mtt0lnag_4"
                    voices=4 gate=1 detail="loaded"

168 ms from sentinel to autoload. The container holds the full bundle:
`instrument.json`, `instrument.wasm`, `measured.local.json`, `metadata.json`,
`synthis.json`.

**What this establishes, all of it inside an extension process:** ticket
minting, sentinel navigation, the intercept, `fetchAndInstall`, directory
creation in an appex container, the sidecar merge, wasm3 compilation, voice
allocation, and autoload. None of it needed changing for AUv3.

**The ticket redaction works live.** `ticket=present`, not the value. That is
the property the intercept's ordering was built for — logged before the origin
check and with the ticket stripped — and it is now confirmed on a device rather
than argued from the code.

**Noted, not chased:** the reload at 18:41:02 reports `voices=4` where the
first load reported `voices=1`. Unexplained.

## What remains, and it is much smaller than C11 implied

Acquisition is not broken. The **signed-out path produces no visible
feedback**. `acquireIntoPlugin` toasts on the 401 from the download-recording
POST, and no toast was seen in AUM's tall view, so clipping is unlikely.

One hypothesis excluded, and worth recording because the code had already
guarded it: I expected the `show-login-modal` listener to be unmounted in the
plugin, since the plugin suppresses the web app's chrome. It is not.
`App.jsx:1450` is `if (hostDrawsChrome()) return <NavigationBar hideNav />;`
with a comment stating that NavigationBar still mounts without its `<nav>`
SO THAT those listeners survive. Someone anticipated this exact failure.
`RegisterPromptModal`, `useRequireRegistered` and the event wiring all exist.

So the fault is inside `SoundCard`'s handler, or in toast visibility. The
desktop signed-out test (C11) remains the discriminator and still costs nothing.
Priority drops: this is legibility on an error path, not a broken feature.

## Stage B's acceptance improves because of this

The AUv3 is now signed in, and its session persists in the appex's own
WKWebView store. So after Stage B the test can be the **product requirement
itself** rather than a proxy: acquire one instrument in the AUv3, and it must
appear in the standalone app with no second sign-in and no acquisition of its
own. Planting a directory with `devicectl device copy to` stays as the fallback
if acquisition regresses.

Revised container counts for that test: the app holds 4 in its private
`Documents`, the extension now holds 1 in its own. Both are private, so both
surfaces should read 0 immediately after Stage B, then 1 after a single
acquisition. Nothing is deleted either way.

---

# C13. FINDING — the view size is host-specific, and the shell already shows the library root

AUM gives the AUv3 a **tall, near-full-width view**. C4's `736x242` is a
GarageBand constraint, not an AUv3 one. Both matter, but they are different
problems: GarageBand needs a letterbox layout, AUM does not.

Two things visible in AUM's shell surface that are worth more than the null
result they came with:

    /var/mobile/Containers/Data/PluginKitPlugin/24971780-.../Documents/Instruments
    Note sent (a4, 0.8 s) — 3697 audio blocks rendered so far.

**`PluginKitPlugin`, not `Containers/Data/Application`.** That is the extension
container, so this is direct confirmation of the MECHANISM behind C1 rather
than its symptom: `InstrumentLibrary::root()` is resolving to the appex's own
private `Documents`.

**The shell already displays that path** through the `getLibraryRoot` native
function (`PluginEditor.cpp:406`). So Stage B has a free on-device acceptance
check: the displayed path must change to the shared container. No devicectl
needed, and the check is visible to a user rather than only to a debugger.

And the audio path is live in an extension — 3697 blocks rendered, silence
because nothing is loaded, but `prepareToPlay` and `processBlock` are running.

---

# C12. C3 ESCALATED — the appex log is shared across HOSTS, so the collision is live now

C3 said the filename collision was harmless today because the containers
differ, and would bite once the log moved to the shared container. That
understated it. The GarageBand session (16:56–17:03) and the AUM session
(18:37–18:39) are in the **same** `stage0-Synth.log`, because the container is
per-extension, not per-host.

Each AUv3 instance runs in its own process. Two instances in two hosts, or two
in one host, are already two processes appending to one file with no locking.
The interleaving risk is **present now**, not after Stage B.

---

# C11. CORRECTION to C6 — the attempt IS instrumented, and the fault is not in the plugin

C6 says "the log cannot say why". That over-corrected, and the sequence is worth
recording as it happened:

1. I inferred from the absence of an acquisition log line that nothing was
   handed over. Right in substance, but I had not established the premise.
2. I retracted it on finding zero `logEvent` calls in `ArtifactAcquisition.cpp`
   and `AcquisitionRunner.cpp`, and on §3a saying outcomes are not logged.
3. Reading `PluginEditor.cpp:923-952` settled it. The retraction was wrong in
   SCOPE. The *outcome* is uninstrumented; the *attempt* is not.

The sentinel branch logs on every path — `acquisition_sentinel` with the ticket
redacted, then `acquisition_queued`, `acquisition_already_running`, or
`acquisition_sentinel_malformed`. §3a's "acquisition outcomes are still not
logged" was precise; I collapsed "outcome" into "attempt".

**So the silence is positive evidence.** Across two hosts and three site
sessions there is no `acquisition_sentinel`, no `page_about_to_load synthis://`
and no `navigation_blocked`. Nothing was ever offered to the intercept.

The plugin side is correct and can be excluded:

- `site_->interceptsSentinels = true` (`PluginEditor.cpp:582`) — the site
  surface does intercept.
- The intercept runs BEFORE the `page_about_to_load` log line and before the
  origin check, both deliberately: an unconditional log would have written the
  ticket to disk in plaintext, and the origin check would have handed the
  sentinel to `launchInDefaultBrowser`.
- `InstrumentLibrary::root()` is passed at the intercept site, so Stage B flows
  through with no change here.

The fault is web-app side. `SoundCard.jsx` — the component on screen — does use
`useObtainInstrument` → `watchAndAcquire` → `acquireIntoPlugin`, and that
function toasts on the signed-out 401 from the download-recording POST. So
"nothing at all" means either the purple piano is not the obtain affordance
(`TakeoutView.jsx:1005` is another call site, and the package icon in the top
nav looks like Takeout), or the handler dies before reaching any toast.

**NOT established which.** Pre-registered next test, which needs no phone: press
the same button in the DESKTOP plugin while signed out.

- Desktop also silent → the signed-out path is broken everywhere and this is
  not an AUv3 issue at all. That is the bigger finding, because it would mean
  the amber state the `FeedRenderJobIndicator` work was meant to produce is
  unreachable.
- Desktop toasts → AUv3-specific, and the fixed-position toast or the host
  viewport becomes the suspect.

---

# C1. FINDING — the sandbox boundary is real, and now has a before-state

The AUv3 instantiates in GarageBand, renders our UI, and receives MIDI
(`midi_in events=74 on=22 off=22`, notes 66–80). It reports:

    Instruments   0 instruments
    No instruments in the local library yet.

against four instruments in the container app — three Faust
(`5253ba00…`, `6d5f3028…`, `e140008d…`) and one sfz (`cabd7ca5…`), each with
its `measured.local.json` sidecar.

This is the Stage B before-state and it was captured deliberately, before
touching `InstrumentLibrary::root()`. After the change, an empty list would
otherwise be ambiguous between "the sandbox" and "the group ID does not match".

---

# C2. FINDING — the WKWebView session does not cross the process boundary

`persist` markers from the PROBE lines:

| | shell origin | site origin |
|---|---|---|
| standalone app | `SURVIVED age=98075s` | — |
| AUv3 extension | `created` | `created` |

The extension's web storage is fresh for both origins while the app's has
survived 27 hours. `sessionStorage` likewise `fresh`.

**CORRECTION to `NEXT_SESSION_PROMPT_IOS.md` §3a.** It says of sign-in: *"The
App Group container is where that lands."* That is wrong. `WKWebsiteDataStore`
appears **nowhere** in JUCE (`grep -rn WKWebsiteDataStore modules/` returns
nothing), so the webview uses `defaultDataStore`, which is container-scoped,
and Apple ships no API to point a `WKWebsiteDataStore` at an App Group. The
library files can move to the shared container. The cookies cannot.

Consequence: **Stage D is a ticket-passing problem, not a container problem.**
What crosses the boundary is the HMAC ticket as data, not the session.

---

# C3. CORRECTION — §3a's log claim is false on iOS, and the filename is worse

§3a says: *"The per-host log naming already keys on `hostApplicationPath`, so
this should work."*

`hostApplicationPath` on Apple platforms is `_NSGetExecutablePath()`
(`juce_Files_mac.mm:238-245`) — the **current process's** executable. On desktop
the plugin lives in the host's process, so that resolves to GarageBand or
Reaper and the naming works. An AUv3 runs in its **own** process, so it
resolves to the appex's own executable.

Measured, and sharper than predicted:

    stage0-Synth.log                                        (both surfaces)
    processor_constructed host=Synth wrapper=Standalone     (app)
    processor_constructed host=Synth wrapper=AUv3           (extension)

`getFileNameWithoutExtension()` strips `.is` from `Synth.is` as if it were a
file extension, so the name collapses to `Synth`. The naming therefore fails
twice: it identifies no host, and it mangles the product.

Both surfaces produce the **same filename**. Harmless today because the
containers differ. A two-writer, no-locking collision the moment the log moves
into the shared container — which is a natural thing to want, since the group
container is the only place both processes can write.

**The fix is small, and smaller than first proposed.** `wrapper=` is already in
the log *content*; only the filename collides. Put the wrapper type in the
filename and drop the claim of host discrimination on iOS. No new plumbing.

---

# C4. FINDING — GarageBand gives the AUv3 a 736x242 view

Resize sequence: `320x400` → `428x926` → `926x428` → `242x736` → settles at
**`736x242`**.

A 3:1 letterbox. The synth.is web UI is being asked to lay itself out in it.
This is a hard constraint on the unified-chrome work (§3), not a detail, and it
is a live candidate cause for C6.

---

# C5. FINDING — GarageBand runs at 44100 / 256 frames. The voice caps were measured at 512.

    prepareToPlay sampleRate=44100 blockSize=512
    prepareToPlay sampleRate=44100 blockSize=256

Every Gate 0 voice cap — 1–2 voices for the slowest ranks, up to 8 for the
fastest, at a >=4x RTF margin — was established at **512 frames**. GarageBand
settles at 256. Smaller blocks mean more per-block overhead per unit of useful
work, so the caps are likely optimistic in the host that actually matters.

**NOT established:** the size of the effect. Do not assume it halves, and do not
tune anything until it is measured at 256 under a pre-registered rule. This is
a re-measurement task, not a known regression.

Found only by loading the plugin in a real host. `faust-probe` was never going
to surface it.

---

# C6. NOT A FINDING — acquisition produced no visible result, and the log cannot say why

Observed: on the site surface inside the AUv3, pressing the acquire affordance
did nothing visible. No instrument appeared, no error was noticed.

**CORRECTION, mine, made and retracted inside the same session.** I read the
absence of any acquisition line in the appex log as evidence that nothing was
handed over. That inference is void. Verified from disk:

    logEvent calls in ArtifactAcquisition.cpp     0
    logEvent calls in AcquisitionRunner.cpp       0
    logEvent calls in PluginEditor.cpp           21
    logEvent calls in PluginProcessor.cpp        32

The acquisition path is entirely uninstrumented — §3a already said so
("acquisition outcomes are still not logged"). Absence of a log line from a
layer that emits none is not evidence of anything. This is the same failure the
`--clicks` reporting and the acquisition card were caught doing: asserting an
outcome the layer does not observe. It is recorded here because catching it in
oneself is the only version that generalises.

Three candidate causes, **none eliminated**:

1. Signed out (proven by C2) — the web app may correctly refuse and show the
   amber "Sign in to add this to the plugin", which could go unnoticed in a
   736x242 strip.
2. The `synthis://acquire` interception may not be wired on the site surface in
   an appex. The namespace is referenced from `PluginEditor.cpp`, so the path
   exists; where it is triggered was not located this session.
3. C4's viewport putting the affordance out of reach.

**Prerequisite before diagnosis: instrument the acquisition path.** Diagnosing
it now would mean three live hypotheses, a known-broken precondition (auth) and
a destination about to change in Stage B.

---

# C7. RESOLVED — AudioComponentBundle names a nonexistent bundle on iOS, and it does not matter

Flagged unverified in `58629e9`: `juce_PlistOptions.cpp:377` emits
`AudioComponentBundle = is.synth.plugin.internal` unguarded, but JUCE creates
the `_Framework` target only on Darwin (`JUCEUtils.cmake:1459-1471`), so on iOS
that key names a bundle that does not exist. It was named as the first suspect
for a listed-but-uninstantiable AU.

Retired. The appex instantiated, rendered and received MIDI in GarageBand. The
key is inert on iOS.

---

# C8. FINDING — the Mac can read the App Group container

    xcrun devicectl device info files --device <udid> \
      --domain-type appGroupDataContainer --domain-identifier group.is.synth.plugin

`appGroupDataContainer` is a first-class domain type alongside
`appDataContainer`. This **resolves a Stage B trade-off that was open**:
`UIFileSharingEnabled` exposes only the app's own `Documents`, so moving the
library into the shared container loses Finder and Files.app visibility — which
is exactly what `InstrumentLibrary.cpp`'s comment chose `Documents` for. It does
**not** lose inspectability, because devicectl reaches the group container
directly. Stage B costs less than estimated.

Mechanical note: `devicectl device copy from --destination` needs a full **file**
path. Given a directory it fails, and with stderr suppressed it fails silently.

---

# C9. FINDING — the iOS toolchain had no Apple ID; every prior device build rode a cached wildcard profile

    defaults read com.apple.dt.Xcode IDEProvisioningTeamByIdentifier      {}
    defaults read com.apple.dt.Xcode DVTDeveloperAccountManagerAppleIDLists  {} {}

Five profiles cached on disk, all team ATA8MQ4L7K, for other apps plus one
wildcard `ATA8MQ4L7K.*` expiring **2027-09-06**. None for `is.synth.plugin`.

The first App Group build failed with eight errors. Seven described the
symptom — the wildcard profile lacking `application-groups`. One, first and
quiet, named the cause:

    error: No Accounts: Add a new account in Accounts settings.

`-allowProvisioningUpdates` had nothing to authenticate with, so it fell back to
the cached wildcard. **The condition was latent for every prior iOS build**,
because a wildcard profile is sufficient right up to the first entitlement. The
flag in `probe/gate0-ios/run-on-device.sh` had never had to do anything.

Fixed by signing in to Xcode-beta. Durable fix outstanding: an App Store Connect
API key already exists at `~/Downloads/dnl/AuthKey_95F348PRU8.p8` (key ID
`95F348PRU8`); it needs the Issuer ID from the Keys page, and either an explicit
`-authenticationKeyPath` or a move to `~/.appstoreconnect/private_keys/`. Worth
doing before 2027-09-06, when the wildcard expires and the failure recurs
looking like an entitlement problem.

---

# C10. LATENT TRAP — Signing.cmake would destroy iOS provisioning if a sign identity were ever set

`_kromosynth_sign` does `if(NOT APPLE) return()` and then returns early only
because `KROMOSYNTH_SIGN_IDENTITY` is empty. There is no `if(IOS) return()`.

So iOS is safe **by accident**. Set a sign identity on an iOS build and the
post-build step would re-sign the `.app` with `--options runtime` and a
Developer ID identity, replacing the automatic signature and its provisioning.
Not fixed; recorded so the next person does not find it the hard way.

---

# What Stage B now has

- a measured before-state (C1): 0 in the extension, 4 in the app
- a confirmed-live shared container (C0)
- a way to read that container from the Mac (C8)
- `File::getContainerForSecurityApplicationGroupIdentifier` in plain
  `juce_core` — no `.mm` translation unit needed, contrary to what §3a implies
- the failure mode to design against: that function returns a **non-existent**
  `File`, `scan()` returns early on `! base.isDirectory()`, and the result is an
  empty list indistinguishable from a user who has downloaded nothing. Hence
  `rootOrigin()` reported once at construction.

# Open, in order

1. **The `load@cap` measurement** (C27 answered the transient question; this is
   the cap question). `loadByVoices_[0..32]` with a block count beside each
   entry, so one note, two, three gives the intercept and the per-voice slope
   directly. **Needs a bar registered before the run** — and note we are no
   longer naive, since C27 shows the shape. The bar is for the next
   measurement, not retrofitted to this one.
2. **Re-read the footer after `704bd1e`.** It should now say roughly 5.33 ms in
   AUM, not 42.67. One glance, no run needed. If it still says 42.67 the fix
   did not reach the device.
3. **The icon is now unexplained** (C28). The `AudioComponentBundle` strip was
   run on device and changed nothing, so C24's last hypothesis is eliminated and
   there is no shortlist left. The one loose thread is that **no reboot has been
   tried since the strip** — C24's reboot ruled out the registry cache as a
   cause of absence, which is not the same as showing a corrected plist gets
   picked up without one. Cheapest next step, and it needs eyes on AUM.
4. **The unified chrome and the merged library.** Designed and committed as
   `daw-plugin/WP_UNIFIED_CHROME.md`. §7.2 is the important part and outranks the
   chrome it sits in: instruments bought on one machine are invisible on
   another. **NOT blocked, correcting an earlier overstatement**: §7.2 already
   re-download exemption keys on the takeout entry, so the merged list must
   re-download the same entry and never re-render. **OQ7 is still open, unchanged
   since 2026-09-07, and it is one decision: entry or sound.** Highest leverage
   item on this list, and not code. §11's acceptance list already includes the
   merged-library criterion, so building the chrome first means shipping against
   a list known to be incomplete.
5. **The envelope panel** (`WP_UNIFIED_CHROME.md` §9). Parameters already exist
   as host parameters; the gap is presentational. Release is the polyphony
   lever, so it belongs beside the voice count.
6. **The per-note gain clipping decision** (C23). Clamp the boost, or take trim
   headroom on a wide spread. A real choice, not a number to pick.
7. **Finish consolidating `capForRtf`** (C26). All three copies now agree
   numerically; there are still two of them. Giving `capForRtf` a headroom
   parameter touches `FaustEngine.cpp` and so sits behind the 120/120 null test.
8. **Deploy `94e7471`** (C26), whenever the hub is next restarted. Committed in
   `kromosynth-render`, not pushed. Nothing breaks until it ships; new artefacts
   keep being minted with ladder caps until it does.
9. **The desktop signed-out test** (C11, C14). Still costs nothing.
10. **Stage D** — the ticket across the process boundary (C2).
11. **A reliable way to read the group container from a Mac** (C16).
12. **Migration**, if wanted: the app's four and the extension's several are
    still in their private containers, untouched and unlisted.

# Closed 2026-09-09, after the prompt was rotated

- **C22's counters are now interpretable** (`844223d`). Item 1 above is the run,
  not the build.
- **"Say measuring while measuring"** (`2f40a20`, C21). Both surfaces — and both
  turned out to be making a positive false claim rather than staying silent:
  `SiteBar` said "Installed - ready in Library" and the shell said "Loaded",
  throughout the eight to nine seconds the audio path is suspended.
- **The render server's ladder** (`94e7471`, C26), and the probe's, which was not
  on this list because nobody knew it existed (`884e0c3`).
