# Next session: deploy the web app, then the letterbox

Rotated 2026-09-15 (second rotation that day). Previous:
`NEXT_SESSION_PROMPT_IOS.2026-09-15.md`.

Read `docs/AUV3_IOS_FINDINGS.md` first — C0 through C42, newest first. That file
is the record. Do not re-derive it from commit messages.

---

## 1. The single most useful thing to know

**Deploying `kromosynth-desktop` fixes a live defect on Björn's phone, and it is
already committed.** `main` carries `978e432` (the account view) and `56afa5a`
(the render verb's web half). Neither is deployed. Until they are, tapping
`Sign in` on `/account` inside the plugin does nothing — see §2.

Single-sound delivery (C41 item 1, C42) is **built and committed**: the
`synthis://render` verb, the `<container>/Renders` outbox, and a drain into
`Documents/Renders` at container-app launch. 105/105 in `acquire-probe`,
`build/desk-verify` clean, `vite build` clean.

**The drain has never run.** It is gated on `wrapperType_Standalone` and needs
the phone. And the thing to judge when it runs is not whether it works but
whether the staleness is tolerable — a render made in AUM does not reach Files
until Synth.is is opened COLD. That was pre-registered as the question; if it
feels wrong, the answer is a document picker from the appex, not more sync.

Still not done: the letterbox (C39 items 3 and 4), the desktop VST3/AU rebuild,
and `unread`.

## 2. The Sign in button, and why "unable to tap" was the wrong description

Björn reported he could not tap `Sign in` on the plugin's account surface. The
tap lands. The handler runs. **It dispatches `show-login-modal` into a room with
no listener.**

The deployed `/account` is the OLD `AccountRoute` (`App.jsx` at `2bfa02f`):

```jsx
<div className="p-8 text-center text-gray-300">
  <p className="mb-4">Sign in to keep your sounds on your own account.</p>
  <button className="px-4 py-2 rounded-lg bg-blue-600 text-white"
          onClick={() => window.dispatchEvent(new Event('show-login-modal'))}>
```

That markup IS the screenshot, down to the colours — `text-gray-300` is light
green and `bg-blue-600` is teal `#35aaa3` in this palette (`Palette.h`: the
class names lie). `NavigationBar.jsx:113` is the sole listener for that event,
and the old route did not render `NavigationChrome`, so `NavigationBar` was
never mounted on it.

The comment sitting directly above that button asserted the opposite: *"Navigation
Bar owns the modal and stays mounted even under hideNav, so the dispatch above is
enough to open it."* Same shape as the spinner that claimed an attempt had
started and the card that claimed success for a failed handover. **It was broken
in a plain browser too**, and had simply never had a caller until the plugin got
an account button.

Fixed by `978e432`, which makes `AccountRoute` render `NavigationChrome` plus
`AccountView`. Committed, NOT deployed. The screenshot is the evidence for
"deployed is old" — nobody has fetched the live bundle to confirm it, and
nobody needs to.

**Sign-in itself should then work in the appex**: `authService.js` posts
email/password over `fetch` to `/api/auth/login`. No OAuth, no
`window.location.href`, so `pageAboutToLoad`'s origin block never sees it. If it
still fails after the deploy, that is a NEW finding, not this one.

## 3. Open, in order

1. **Deploy `kromosynth-desktop`.** Two commits ride on it: the account view
   (§2) and the render verb's web half. Then, on the phone, in one sitting:
   - tap `Sign in` and get a modal
   - tap `Download WAV` on a single render and watch the strip say `Saved …`
   - open Synth.is cold and look in Files → *On My iPhone → Synth.is → Renders*
   - double-tap the LOGO in AUM (§3b — thirty seconds, still unverified)

   The log lines to grep for are `render_queued`, `render_saved` and
   `renders_drained moved=`.

2. **The letterbox, C39 items 3 and 4.** Untouched this session. Compact the
   header under `@media (max-height: 320px)` (56 -> ~36pt) and hide the filter
   row at short heights (42pt). Worth about 62pt, landing near 155 against WP
   §11's 132.

   **736x242 exists only on the phone, in GarageBand.** The standalone simulator
   fills the screen and cannot be letterboxed. Cheapest check: serve
   `kIndexHtml` to a desktop browser sized to 242pt tall — CSS only, no plugin,
   no build, no device.

3. **Rebuild the desktop VST3/AU.** Now ~40 commits behind and still drawing the
   deleted ChromeBar. `scripts/build.sh install --backup`, then
   `auval -v aumu Ksyn Bthj` before opening a host. Expect two audible or
   visible changes that are not regressions: the Output default is -9 dB
   (`6fc3159`), and single renders now land in `~/Music/Synth.is/Renders`.

4. **Wire `unread`.** Unchanged from the last rotation: the auth sentinel
   carries a dot, not a count, and the parameter is not sent at all — the count
   lives in `NotificationBell`, not `NavigationBar`, so sending one from there
   would mean inventing a number. The dot stays dark until this is done.

## 3b. Small, and each is one thing

- **The double-tap fix has never been verified.** `touch-action: manipulation`
  on `nav#chrome`, shipped 2026-09-14, reproduced by Björn only by
  double-tapping the LOGO. A double-tap cannot be synthesised through the
  simulator tooling. Thirty seconds on the phone.
- **The `getSurface` resync (C40)** — the page holds view state C++ owns, with
  no way to resync after a page reload. Specified, not built.
- **`ios-log.sh`'s header still says its retrieval path is unverified.** It
  works, and has for days. Update the status line but KEEP the probe — C16 was a
  retraction of exactly the confidence that deleting the fallbacks would restore.
- **A probe `README.txt` sits in the device's `Documents`.** Disposable — and
  now actually disposable, because `Documents/Renders/` will keep the folder
  non-empty once a render has been drained.
- **`fcbe75c` in `kromosynth-desktop` swept unrelated units-panel WIP** into a
  commit about auth. Already pushed; splitting means a force-push.

## 3c. Outside this repo, and one of these is stale in the OTHER direction

**`kromosynth-recommend` on the hub is FINE.** The previous rotation warned that
the backtick fix was "still uncommitted there" and would be clobbered by the
next `git pull`. It is committed, it came from `origin/main` (`c5932c0`, which
reworded the comment rather than swapping to single quotes), the working tree is
clean and the service has been up for hours. Nothing on the hub to lose.

**The laptop is the loose end instead.** `kromosynth-recommend` here carries an
unpushed `969bf05` fixing the same defect a different way, on the same parent,
so the two have diverged and `git pull` will conflict in that comment block.
Diffed: the only difference in `RecommendationService.js` is comment wording, no
behaviour change, so dropping `969bf05` is safe. Björn's call.

**The hub path is `/Users/bthj/Developer/synth.is/…`**, not
`…/Developer/apps/synth.is/…` as several notes claimed.

## 4. The loop, and how to drive it

Unchanged from the last rotation and still correct. In brief, with the
corrections that cost time:

- **AUM hands the plugin at least five different rectangles in one session** —
  428×926, 428×879, 428×280, 409×524, 428×574. GarageBand adds 926×428, 242×736
  and 736×242. Anything that sizes against the editor cannot read one
  `editor_resized` line and call it the layout.
- **Poll the log for `page_finished_loading`; do not sleep.** It lands ~6.8s
  after launch. A screenshot at 10s caught the LAUNCH SCREEN and nearly became a
  finding about a broken stylesheet.
- **Re-derive the data container after every `install`.** Each one mints a fresh
  UUID.
- **`xcode-27-headless` `DeviceInteractionSynthesize` does drive the
  simulator**, despite `skillToTrigger` insisting on a subagent and despite
  reporting `applicationState: "NotRun"` beside a hierarchy dump showing the pid
  running. Web content is a `RemotePlaceholder`, so compute tap coordinates from
  the CSS box. Sessions expire after a couple of calls; restart rather than
  debug.
- **Plugin<->site is solved and worth reusing.** Point a simulator build at the
  dev server with `-DKROMOSYNTH_WEB_APP_URL=http://localhost:5173`; ATS does not
  block localhost. **Set it back to `https://synth.is` afterwards.** This is the
  cheapest way to test §2 and the render verb without deploying.

```
cmake -S . -B build/ios-sim -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphonesimulator \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=16.0 \
  -DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM=ATA8MQ4L7K \
  -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO \
  -DKROMOSYNTH_BUILD_FAUST=ON -DKROMOSYNTH_BUILD_SAMPLER=ON \
  -DKROMOSYNTH_WASM3=ON \
  -DKROMOSYNTH_WASM3_SOURCE_DIR="<repo>/../kromosynth-projects/Evolutionary Sound Discovery Platform/faust-genome-substrate/probe/out/wasm-runtime/wasm3/source" \
  -DKROMOSYNTH_WEB_APP_URL=https://synth.is \
  -DKROMOSYNTH_VI_SERVICE_URL=https://vi.synth.is \
  -DKROMOSYNTH_WEBVIEW_PROBE=ON -DKROMOSYNTH_BROWSER_INSET=0
```

```
cmake --build build/ios-sim --config Release --target KromosynthPlugin_Standalone
# iPhone 17 Pro, 27.0, booted: AC0AA08E-1F64-4E5D-931D-9D505753118B
# Literal UDID, NOT a shell variable -- see §6 on local-terminal and bare null.
xcrun simctl install <udid> build/ios-sim/src/KromosynthPlugin_artefacts/Release/Standalone/Synth.is.app
xcrun simctl launch <udid> is.synth.plugin
xcrun simctl io <udid> screenshot /Users/bjornpjo/Developer/shot.png
xcrun simctl get_app_container <udid> is.synth.plugin data   # re-derive EVERY install
```

**Compile-check targets, because two of them are not what you would guess.**
`build/desk-verify` is Ninja and has NO Standalone target — use
`--target KromosynthPlugin`, which builds the shared code and is enough to catch
every compile error in `src/`. `build/acquire-probe` is Unix Makefiles, target
`acquire_probe`, and its selftest is the fastest real check in the repo:

```
cmake --build build/desk-verify --target KromosynthPlugin
cmake --build build/acquire-probe --target acquire_probe
./build/acquire-probe/tools/acquire-probe/acquire_probe_artefacts/Release/acquire_probe \
    selftest /tmp/acq-scratch
```

**Seeing a screenshot economically.** Crop to the region and JPEG it:

```
magick /tmp/shot.png -crop 1206x230+0+0 +repage -resize 560x -strip -quality 60 /tmp/bar.jpg
```

~5,000 base64 characters for the chrome strip. ImageMagick and PIL 10.1.0 are
both installed. 35 simulators available, including an iPhone 13 matching
Björn's device.

**What the simulator cannot cover, so do not try:** AUM does not exist there.
The icon, AUM's 256-frame block delivery, the AU parameter list, GarageBand's
736x242 letterbox and the container-app drain all need the phone.

## 5. What landed on 2026-09-15 (second session)

**`3ff9629` — the container, not the instrument root.** `InstrumentLibrary`'s
resolver answers with the container; `root()` appends `Instruments`;
`containerRoot()` is public. Same three paths as before. The point is that the
outbox resolves through the SAME branch — an appex writing renders into its
private Documents while reading instruments from the App Group would be two
halves of the plugin disagreeing about which sandbox they occupy.

**`e8f2dac` — C41 item 1.** `synthis://render` as a third verb; `RenderOutbox`
with the outbox, the naming rule and the drain; `download()` extracted so ONE
copy of the network contract serves both verbs; both paths refusing each other's
work before touching disk; the verb carried through `AcquisitionRunner` into
`Status` and the completion callback; the processor returning early for a render
rather than asking the library for a directory that does not exist; the shell's
finished line worded per kind. Seventeen new probe checks.

**`9cce564` — C42**, including the correction written next to C41's wrong claim.

**`56afa5a` in `kromosynth-desktop`** — `handOverToPlugin(verb)` with
`acquireIntoPlugin` and `saveRenderToPlugin` as wrappers, the `'wav'` branch in
`downloadTakeoutEntry`, and the one-line fix that stopped single renders being
labelled `sfz`.

**Not this session, but on `main` and undeployed:** `4e21415`, `978e432`,
`789a618`, `81d3c9c` — Björn's, committed in another terminal while the above
was in progress.

## 6. Things I got wrong, so you do not repeat them

Recorded because every one cost real time and each has a recognisable shape.
Dated, because some are older than the last rotation.

**2026-09-15: I nearly went looking for a hit-testing bug.** "I am unable to tap
the Sign in button" names a mechanism — touch not landing — and that mechanism
was wrong. The tap landed; the event had no listener. What settled it was
matching the rendered markup to source (`text-center text-gray-300`,
`bg-blue-600`) rather than reasoning about webview z-order. *A reported symptom
names a mechanism, and the mechanism is a hypothesis, not part of the report.*

**2026-09-15: the repo moved under me.** Four commits landed in
`kromosynth-desktop` from another terminal mid-session. No overlap and nothing
lost, but a `vite build` run against a tree containing someone else's
in-progress edits is weaker evidence than it looks. *Check `git log` at both
ends of a session, not only the start.*

**2026-09-12: arithmetic stood in for looking, three times in one day.** C35's
`calc()` — a real reading with an obvious interpretation taken instead of
tested. C38's cache magnitude — a real mechanism with an invented severity,
disproved by one phone with different history. C39's 191 — a correct subtraction
of the wrong quantity, treating everything below the strip as content when 172pt
of it is page furniture. *The common shape is a real measurement applied to a
quantity nobody checked was the one that mattered.*

**2026-09-11: I asserted layout from source twice and was wrong both times.**
`2db74a9` claimed `justify-between` would leave the user section where it sat;
it puts a lone child at the START. eslint passed and vite HMR'd cleanly both
times. *Those checks say nothing about where flexbox puts things.*

**2026-09-11: I skipped the `dryRun` pass twice** and the second closed
`showSite` with `}` while two statements still followed. The convention exists so
it does not get that far.

**2026-09-11: I blamed `env()` inside `calc()` on one obvious reading.** The page
reported its nav as 50 tall under `height: calc(50px + env(…))`, exactly the
first term, so the `env()` term looked unsupported. The next reading added
`getComputedStyle` and showed `paddingTop: "62px"` — the stylesheet had been
resolving it correctly all along and the real cause was timing. **0 is what an
absent inset and a broken function both look like.** *Report the state of the
thing, not just the number it produced.*

**2026-09-11: two of my own instruments were wrong before the code was.**
`chrome_height` logged only when the value changed, making "the page never
called" and "the page reported the seed value" identically silent. And a pure
white screenshot nearly became a finding when it was the launch screen. *Fix the
instrument before reasoning about the reading.*

**2026-09-11: I reported a landed commit as having a visible effect it did not
have.** *A commit in one repo is not a behaviour in production.* This session's
§2 is the same lesson pointing the other way: `978e432` fixes the Sign in
button, and will fix nothing until somebody deploys it.

**2026-09-10: I asserted `chrome=host` was already working**, several times,
because the code existed and WP §2 said so. Nothing failed loudly, because a nav
appearing is what a nav does. *Check that the trigger fires, not just that the
handler exists.*

**I over-tightened the install gate on my own inference**, and a gate the
operator cannot clear is a stop, not a safety measure. `3a26581` splits it.

**Three separate times a truncated `grep | head` nearly became a finding.** C25
is about exactly this. *Write the guard that refuses rather than the check you
read.*

**I handed Björn decisions that were mine to make.** He said so: "It is not clear
to me what you need from me." *Arrive with a proposal and a reason.* When he then
says he is not an expert, that is not an invitation to list options again — it is
a request for the recommendation and what it trades away.

**A confirmed prediction from an invalid test is worth nothing.** C29 predicted
the cap would fail on the override clause and it did — and C30 showed the bar was
reading the operating system. *Registering a bar in advance is only useful if you
will also throw it away.*

## 7. The icon: do not pick this up casually

C34 records five dead hypotheses. **Do not propose a sixth bundle hypothesis.**
Everything checkable is checked: 30 catalog renditions at scales 1, 2 and 3, both
idioms, all four plist icon keys present in both bundles with names that resolve,
loose PNGs at correct pixel sizes, `AudioComponentBundle` eliminated in BOTH
directions, and registry staleness eliminated by a full delete, reboot and single
install.

The control test that settled it — *do other vendors' AUv3s show icons in this
AUM?* — was one glance and came four sessions late. They do.

**The reframe that matters:** the `NEMUR NET EHF.` and `NEMUR NET EHF` entries
survived a full delete and reboot, so they are separate older builds under their
own bundle ids, not stale registrations. Three JUCE-built AUv3s of ours across
years, none with an icon, while Moog's and Space Mushrooms' work. That points at
JUCE's iOS plist generation, not at this build.

If picked up: read the system log while AUM draws the row, via Console.app with
the device attached — `log stream --device` is unsupported by this macOS `log`,
and libimobiledevice is not installed. That would OBSERVE the failure.

**The device currently has a modified build** with `AudioComponentBundle`
pointing at `is.synth.plugin`. Byte-identical in code, harmless, but not the
plain build. Overwrite it on the next install.

## 8. Conventions that held

- Explicit pathspec on every `git add` and `git commit`. `kromosynth-desktop`
  carries unrelated WIP more often than not; never stage it. The MCP
  `git_commit` tool behaves like `git commit -a` — use the terminal.
- Commit message files under `/Users/bjornpjo/Developer/`, not `/tmp`.
- `filesystem:edit_file` with `dryRun: true` first. Batches are atomic: one bad
  `oldText` rejects all of them. Box-drawing characters and em dashes must match
  byte for byte.
- **`filesystem:edit_file` will happily orphan a doc comment** by inserting
  between it and its declaration. The dry run showed it; a blind apply would
  not have. Anchor on the comment AND the declaration together.
- MCP `local-terminal` calls time out around four minutes. `nohup … &` plus a
  polling second call for anything longer; keep inline sleeps under ~100s.
- `local-terminal` returns a bare `null` when a command hangs, AND for
  multi-line commands carrying a shell variable assignment. The two look
  identical and are not the same thing. Single-line literals work.
- **`timeout` and `gtimeout` DO exist on the laptop** (`/opt/homebrew/bin`),
  unlike the Mac Studio. `grep -c` returning 0 matches exits non-zero, which the
  MCP surfaces as an error; it is not a failure.
- Corrections go where the wrong claim was written, not only where it was
  noticed. `ShellPage.h`'s `nav#chrome` comment and C41's delivery paragraph
  both carry their own retractions.
