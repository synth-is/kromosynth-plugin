# Render progress: design record and event contract

Bead `synthis-7o3.1` (epic `synthis-7o3`). Written 2026-11-04. Read the epic's
design field first; this record settles what that field left open, and **where
the two differ, this record wins** (the dependent beads say the same).

Status of the evidence: everything below marked *measured* was measured on
2026-11-04 on the laptop; everything marked *read* was read from the code at the
commits named. Anything that needs eyes on a real screen is in
[What is not verified](#what-is-not-verified).

---

## 1. The story, in one paragraph

A user clicks the piano on a sound card. The card's piano icon becomes a ring
that follows the job (queued, rendering, handing over). The bar above the site
shows one capsule for everything in flight and, when the instrument has
**actually been installed by the plugin**, the capsule folds into the Library
tab, which shows a badge until the user opens it, where the new row carries the
existing NEW marker. The bottom toast and the top status line are no longer part
of the success path. Errors keep a message.

Two layers draw it, and they cannot talk directly (the site's web view has
native integration off on purpose). So:

```
 SITE (https://synth.is)                     PLUGIN (native)                 BAR (shell page)
 ───────────────────────                     ───────────────                 ────────────────
 card ring  <- vi job poll (own state)
                │
                │ synthis://progress?...     pageAboutToLoad                 
                ├──────────────────────────► parse, block, log
                │  (best effort, one way)       │ ActivityList (editor)
                │                               │◄── AcquisitionRunner finished
                │ synthis://acquire?...         │     (the ONLY source of "landed")
                └──────────────────────────► start download+install
                                                │ emitEvent "activityChanged"  ─►  capsule, Library badge
                                                │ native fn  getActivity       ◄─  (pull = ground truth)
```

## 2. State machine

One entry per **job** (the takeout entry id the site already has for every
render, sampled and live alike).

```
                 ┌────────────────────────────── failed(reason) ◄── any non-terminal state
                 │                               lost            ◄── any non-terminal state
  (start) ─► queued ─► rendering ─► delivering ─► landed
                          │  ▲                      ▲
                          └──┘ pct updates          └── native only: AcquisitionRunner finished ok
```

* **Forward only.** Each phase has a rank (queued 1, rendering 2, delivering 3,
  landed 4). An event with a lower rank than the entry's current phase is
  ignored (sentinels are delivered in order, but the site can fire two in one
  tick and the application must not go backwards because of that).
* **`landed` is native-only.** It is set by the plugin when AcquisitionRunner
  finishes ok. The site is never trusted to say it, and the parser rejects
  `phase=landed` in a sentinel. This is the "landing confirmed by the native
  side" rule from the epic, made structural.
* **`failed` has a reason from a fixed list** (section 3.3). Free text from the
  site is never shown.
* **`lost`** is set by the plugin when it stops hearing from a job it was
  following (section 6, Q5). It is not an error the user caused and it is not
  final in fact: if the instrument later lands, the entry is revived as
  `landed`.
* An acquisition that arrives with **no job** (the Instruments page's plain
  download button, an old site that sends no progress) still produces a
  `landed` entry, titled from the acquisition's `name`. The capsule and the
  badge therefore work for every route into the library, not only for renders.

Terminal entries (`landed`, `failed`, `lost`) stay for 8 s so the capsule can
show them, then expire.

## 3. The event contract (site → plugin)

### 3.1 Verb and URL

```
synthis://progress?job=<id>&phase=<phase>[&pct=<0-100>][&title=<text>][&artifact=<id>][&reason=<code>]
```

* **Verb `progress`.** Matched on the host segment, like `isAuthSentinel`
  (`synthis://progressive` does not match).
* **Parsed BEFORE the acquisition check in `pageAboutToLoad`** (the greedy-match
  trap documented in `AuthSentinel.h`: `acquisition::isSentinel` matches every
  `synthis://` URL and `parseSentinel` then answers nullopt for an unknown verb,
  so a progress URL reaching it is blocked correctly and logged
  `acquisition_sentinel_malformed` wrongly). Put the new branch directly after
  the auth branch and before the checkout branch.
* **No credential, ever.** No ticket, no token, no user id. The whole URL is
  therefore safe to log, unlike acquisition's (see `redactForLog`). The only
  free text is `title`, which is a sound name the user is looking at.

### 3.2 Fields

| field | required | rule |
|---|---|---|
| `job` | yes | `[A-Za-z0-9_-]{1,64}`. The takeout entry id. Anything else: malformed. |
| `phase` | yes | `queued`, `rendering`, `delivering`, `failed`. Unknown value: **ignored but blocked and logged** (`progress_sentinel_unknown_phase`), never "let through". A newer site may add phases; an older plugin must not misread them. `landed` and `lost` are rejected here: native-only. |
| `pct` | no | integer 0..100, **only meaningful with `phase=rendering`**. Clamped. Absent means indeterminate. Ignored with any other phase. |
| `title` | first event for a job | sanitised like `auth::sanitiseLabel` (control characters out, length clamped to 40). Later events may omit it; the plugin keeps the first. |
| `artifact` | with `delivering` | the artefact id that the acquisition sentinel is about to carry. This is the **join key** between the job (known to the site) and the install (known to native): `AcquisitionRunner` reports by artefact id and nothing else. Without it a landing cannot be attached to its job. |
| `reason` | with `failed` | `quota`, `signed_out`, `render_failed`, `timeout`, `handover_failed`, `network`, `other`. The plugin supplies the wording. |

Unknown extra parameters are ignored. That is the forward-compatibility rule:
fields can be added, never repurposed.

### 3.3 Why `delivering` is one phase, not "downloading" and "installing"

The epic listed downloading and installing separately. *Measured* on the
laptop's plugin logs (32 instrument acquisitions and 22 render saves, Aug 31 to
Nov 4): the native leg from `acquisition_queued` to `acquisition_autoload` (or
`render_saved`) takes a **median of 0.15 s** for instruments (p90 0.56 s, max
1.50 s) and 0.16 s for render saves (max 0.72 s). Two phases that last a fifth
of a second cannot be seen and would only produce flicker. The site sends one
`delivering`; the plugin adds nothing visible between it and `landed` except a
minimum visible time (section 6, Q4).

### 3.4 Rate: what the site may send

* The **first** event for a job (`queued`, with `title`) and **every phase
  change** go out immediately.
* A same-phase progress update (`rendering` with a new `pct`) goes out when
  `pct` has risen by 10 points since the last one sent **or** 3 s have passed
  since the last one sent and `pct` changed, and never closer than 1 s to the
  previous event for that job.
* A **heartbeat** (same phase, same pct) every 15 s while the job is alive, so
  the plugin can tell a long job from a lost one (section 6, Q5).
* The end (`delivering`, `failed`) always goes out.
* **One choke point for every sentinel the site sends** (acquire, render, auth,
  checkout, progress): a function in `pluginHost.js` that guarantees a **minimum
  gap of 300 ms between two navigations, before and after each one**. Setting
  `window.location.href` twice in one task keeps only the last navigation (the
  burst measurement in section 5: two per tick lost half, five lost four
  fifths), so without a gap a progress event sent in the same tick as the
  acquisition sentinel would **replace the acquisition**, which is the one that
  matters. 250 ms measured clean (240 of 240); 300 ms is that plus margin.
  Acquire, render, auth and checkout are never dropped; if the gap forces a wait
  they wait. Progress events are coalesced (the newest per job wins) if the
  queue backs up.
* `delivering` must be sent **after step 1 of `handOverToPlugin`** (the
  record-download POST that returns `artifactId`) and before the resolve fetch,
  so a network round trip separates it from the acquisition sentinel.

### 3.5 Compatibility (site first, plugin second)

* An **older plugin** blocks and logs an unknown verb (`acquisition_sentinel_malformed`)
  and nothing else happens. The site keeps its own card ring regardless and
  never waits for the plugin to understand anything.
* An **older site** simply never sends the verb. The plugin still shows
  `landed` for every acquisition (section 2), so the capsule and badge work
  without the site's help, just without the render phases.
* Nothing the installed plugin depends on is removed or renamed.

## 4. Native → bar

**A content-free push plus a pull, the same shape as `rendersChanged`.**

* The editor keeps the activity list (like `authState_`: stored by the editor,
  nothing pushed from a worker thread). Whenever it changes it calls
  `browser_->emitEventIfBrowserIsVisible ("activityChanged", juce::var (true))`.
  This is the mechanism already used for `rendersChanged` and `playRender`
  (`PluginEditor.cpp`, `renderList_->onContentsChanged`), and the shell already
  registers listeners with `backend.addEventListener` (`ShellPage.h`).
* The page answers by calling a new native function **`getActivity`**, which
  returns the whole list. The event carries no data, so a lost or throttled
  event costs one stale frame, never wrong state.
* While any entry is non-terminal the page also calls `getActivity` every 2 s
  (a native read, not a network call). WKWebView throttles timers in a hidden
  view, and the push is "if the browser is visible", so the pull is the safety
  net and `listInstruments` stays the ground truth for the Library list itself.
* **Landing is pushed the moment `AcquisitionRunner` finishes**, not discovered
  by the page's 1 s `pollAcquisitions` (which can miss a hidden-view window
  entirely and is the only clearer of finished runner entries; it is left
  exactly as it is). The runner's completion handler is set by the processor to
  load the instrument; add a second, editor-set notification beside it (message
  thread, cleared in the editor's destructor, guarded by `lifetimeToken()` /
  SafePointer like the other asynchronous callbacks) that marks the entry
  `landed` or `failed` and emits `activityChanged`.

`getActivity` returns:

```json
{ "now": 123456789,
  "active": 2,
  "unseenLanded": 1,
  "entries": [
    { "job": "e1f2…", "title": "Pluck 14", "phase": "rendering", "pct": 42,
      "startedMs": 120000, "updatedMs": 123000 },
    { "job": "9a8b…", "title": "Pad 3", "phase": "landed", "artifact": "…",
      "startedMs": 118000, "updatedMs": 122500 }
  ] }
```

`pct` is `-1` when indeterminate. Times are the plugin's own monotonic
milliseconds (`now` is included so the page can compute ages without trusting
its own clock after a throttled wake-up). `reason` appears on `failed`.

## 5. What the sentinel does to the page it is sent from (Q1 prototype)

The question: the site will set `window.location.href` to a `synthis://` URL
every few seconds while the user may be typing or scrolling. Does a cancelled
navigation at that rate disturb the page: focus, scroll position, frame
cadence, a reload?

**Prototype.** `KROMOSYNTH_SENTINEL_PROBE=<hz>:<seconds>[:<burst>[:<drive>]]`,
read by the Standalone only, inert otherwise (`PluginEditor.cpp`,
`kSentinelProbeScript`, inside the existing `KROMOSYNTH_WEBVIEW_PROBE` block).
It opens the site, injects a script that records focus and blur events, scroll
events (capture phase, so any element), `visibilitychange`, `pagehide`,
`beforeunload`, requestAnimationFrame gaps (over 50 and 100 ms), whether the
page could scroll at all and whether a field held focus, then fires
`synthis://progress?...` at the chosen rate. `burst` assigns that many URLs back
to back in one task; `drive=native` lets the plugin's own timer call the page's
`fire()` so a hidden page's throttled timers cannot distort the rate. The
native side counts how many reached `pageAboutToLoad`.

**Where it ran.** The Mac standalone (`build/verify-nocopy`, Release), on the
laptop, **with the screen locked**, so the site's document was hidden
(`document.hidden` true, no frames, no focus, nothing to scroll). That makes
the delivery counts below sound and the focus, scroll and frame-cadence counters
**empty** (they recorded zero because nothing could happen to them, which proves
nothing). Those three need a person at an unlocked screen; see
[What is not verified](#what-is-not-verified) for the one-line command.

**Delivery** (native clock, so a hidden page's throttled timers cannot bend the
rate; "received" is the number of lines `pageAboutToLoad` logged):

| rate | per tick | sent | received | lost |
|---|---|---|---|---|
| 0.33 Hz (the contract's 3 s), 15 s | 1 | 5 | 5 | 0 |
| 1 Hz, 15 s | 1 | 15 | 15 | 0 |
| 4 Hz (250 ms), 60 s | 1 | 240 | 240 | **0** |
| 10 Hz (100 ms), 10 s | 1 | 100 | 99 | 1 |
| 100 Hz (10 ms), 10 s | 1 | 1000 | 986 | 14 (1.4 %) |
| 1000 Hz, 10 s | 1 | not countable: the evaluation queue saturates and the page's counter lags the backlog | | |
| 1 Hz, 10 s | **2 in one task** | 20 | 10 | **10 (50 %)** |
| 1 Hz, 15 s | **5 in one task** | 75 | 15 | **60 (80 %)** |
| 10 Hz, 15 s | **5 in one task** | about 710 | 149 | one per tick |

Every run: `page_finished_loading` stayed at 2 (the shell and the site's first
load, so no sentinel reloaded anything), `pagehide` and `beforeunload` stayed at
0, and `location.href` was unchanged at the end.

**What this says.**

1. **A sentinel does not reload, navigate or unload the page.** At every rate up
   to 100 per second the document stayed put.
2. **Several `location.href` assignments in one task deliver only the last.**
   Two in a tick lost half, five lost four fifths, exactly one arrived per tick.
   This is the finding that matters: a progress event set in the same tick as
   the acquisition sentinel would **replace** it, and the instrument would never
   be requested. Today nothing sends two in the same tick (the acquisition
   sentinel follows two awaited fetches), but nothing prevents it either, and
   the auth sentinel is sent on every site page load.
3. **Spacing is what saves them, and 250 ms is clean.** Zero loss over 240
   events at 250 ms and at every slower rate tested; about 1 % loss at 100 ms
   and 10 ms (cause not investigated; the likely reading is that the busy main
   thread bunched two evaluation calls into one WebKit task). **The contract's
   minimum gap is 300 ms**, the measured clean figure plus margin, applied
   *before and after* every sentinel, so a progress event can neither precede
   nor follow the acquisition closely enough to clobber it.
4. At the contract's real rate (one event per 3 s, with a floor of 1 s per job)
   the load is three orders of magnitude under anything that lost an event.

### 5.2 What a hidden site does to its own timers

Found by accident (the first page-driven runs delivered a fraction of what they
sent) and then measured: an independent 1 s timer in the site's document, run
with the screen locked, ticked **3 times in the first 10 s and then once more
60 s later** (4 ticks in 60 s, longest gap 59.6 s, `document.hidden` true at
every tick). The page-driven sentinel timers behaved the same way: 4 of 20 at
1 Hz, 2 of 6 at 0.33 Hz, 4 of 60 over a minute. That is WebKit throttling a
hidden or occluded page to about one timer tick a minute.

What was **not** measured is the same thing in an unlocked, visible window with
the site view put behind the shell (the user reading the Library tab while a
render runs, which is the normal thing to do). `scripts/sentinel-probe.sh
hidden` measures exactly that; its numbers replace this paragraph. Until then
the design assumes the worst case: **the site's follower loop and its
heartbeats can stall for about a minute whenever the site view is hidden or
the window is behind another app.** Section 6 Q5 is built on that.

It also means the existing `watchAndAcquire` poll (4 s) has the same weakness
today: a render that finishes while the site is hidden is handed over up to a
minute late. That is current behaviour and not made worse here, but it is the
reason a native-side follower (the plugin watching the takeout entry itself) is
worth a bead of its own and is **not** part of this epic.

## 6. The six questions

### Q1. The sentinel channel
Section 3 and section 5.

### Q2. Native to bar: event or poll?
Section 4: push `activityChanged` on every change, page pulls `getActivity`;
the 2 s pull while anything is active is the net under it.

### Q3. What does the vi job report?
*Read* in `kromosynth-vi/src/routes/vi.js` (`GET /status/:jobId`) and
`src/services/renderPipeline.js`, and `useViJobProgress.js` on the site.

A sampled-instrument job moves through `currentPhase`: `queued`,
`fetching_genomes`, `initializing`, `calculating_matrix`, `rendering`,
`generating_sfz`, `packaging`, then `complete` or `failed`. The response carries
`progress.samplesRendered`, `progress.samplesTotal` and a `percentComplete`.
**The percentage is real only in `rendering`**: `samples_rendered` is updated
there and nowhere else. Outside it `samplesTotal` may be known but nothing is
counting, so the number is 0. `useViJobProgress` already respects that (it
computes `progress` only in `rendering`), but **a consumer must treat any other
phase as indeterminate, not as 0 %**. A ring that sits at 0 % through
`fetching_genomes` and `calculating_matrix` and then jumps looks broken.

While `queued`, the status also returns a queue position.

A **live (Faust) instrument has no sampled render**. `useObtainInstrument`
submits it as a takeout entry (`format: 'faust'`), registers it as `kind: 'live'`
and watches the entry with `watchAndAcquire`. The ring there is indeterminate
by construction: queue, hand-over, install. The first-load measurement stays
where it is and is not part of the ring.

### Q4. How long does it take?
*Measured.*

**Server side, sampled instruments.** The local copy of the vi job database
(`kromosynth-vi/data/vi.db`: 335 jobs, 2026-02-02 to 2026-03-17, so older than
today's hub and a caveat on every number here). For 280 completed full jobs:

| | p50 | p90 | p99 |
|---|---|---|---|
| waiting in the queue | 2.5 s | 4.7 s | 696 s |
| rendering | 6.1 s | 91.6 s | 867 s |

Failed jobs: 40 of 335 (12 %), median 27 s before failing. **13 jobs (3.9 %)
sit in `rendering` forever** with no completion time. Both numbers matter for Q5.

**Plugin side.** Median 0.15 s from "queued" to "installed" (section 3.3).

**Site side, a floor nobody chose.** `watchAndAcquire` waits `pollMs` (4 s)
*before* its first poll, so even an instantly finished render takes at least
4 s from click to hand-over. A ring on a live instrument will therefore be
visible for about 4 s regardless. That is long enough to read; it is also
an easy 3.5 s to give back (poll once after 0.5 s), noted for the site task but
not a requirement of this design.

What this decides:

* **The ring is visible for most jobs** (median render 6 s plus the 4 s poll
  floor) and for a long time for some (p90 over a minute), so a determinate ring
  in `rendering` is worth having and the capsule must handle minutes.
* **The native phases need a minimum visible time, not their own states.**
  `landed` is held on screen for at least 1.2 s even if it arrives 0.15 s after
  `delivering`, or the tick would flash and be missed. `delivering` is shown for
  at least 0.4 s for the same reason.

### Q5. Timeouts and stuck jobs
*Measured* (the 3.9 % that never complete, the failures that took thousands of
seconds because the server restarted) and *read* (`watchAndAcquire` gives up at
20 min; `useViJobProgress` has no deadline of its own).

The server cannot be trusted to finish a job, and a user navigating away must
not leave a spinner forever. So the plugin treats every non-terminal entry as a
**lease**:

* Each event renews it. The site sends a heartbeat every 15 s (section 3.4), which
  a throttled site delivers late, not never.
* **Two levels of silence, sized for a throttled site** (section 5.2: a hidden
  site view can go a minute between ticks, so a 15 s heartbeat can arrive a
  minute apart). **After 60 s without an event the ring drops to indeterminate**
  and stops showing a percentage, because a number frozen for minutes is a claim
  nobody is checking. **After 180 s** (three of the worst-case gaps) in `queued`,
  `rendering` or `delivering` the entry becomes `lost`. The capsule shows "Lost
  track of *title*" in the warning colour for 8 s and the ring stops. Nothing is
  claimed about the render itself: the plugin only knows it stopped hearing.
* **No silence clock while the plugin itself has the site view hidden**
  (`showSite (false)`, the Library tab in front). The plugin knows that state, and
  it is the one in which silence is expected. The clock restarts, from zero, the
  moment the site is shown again. The 25 min cap below still applies.
* **A full load of the site view** (`pageFinishedLoading` on the site browser,
  which means the document was replaced and every follower on it died) marks all
  non-terminal entries `lost` immediately, rather than waiting out the 60 s.
* **Hard cap of 25 min** from the first event regardless of heartbeats, five
  minutes past the site's own 20 min deadline, as a backstop against a heartbeat
  that never stops.
* The site sends `failed&reason=timeout` itself when its own 20 min deadline
  expires, so in the normal case the plugin never has to guess.
* If an instrument lands for a `lost` job, the entry is revived as `landed`
  (the join is the `artifact` id from `delivering`). Losing track is not the same
  as the instrument not coming.

### Q6. Several renders at once
* **Capsule text.** One job in flight: `Rendering` plus the title, truncated to
  fit (ring determinate if `pct` is known). Two or more: `Rendering 3`, the ring
  indeterminate (an average of unlike jobs is a number nobody can check). In a
  narrow bar (the iPhone): ring and count only.
* **Order.** Oldest first in the tooltip (`title` attribute) listing each job's
  title and phase. A popover list is not part of the first version.
* **Landings coalesce.** Landings that arrive within 1 s of each other show as
  one "Added 3" rather than three flashes. A single landing shows "Added" and
  the title for at least 1.2 s.
* **The badge counts landed-and-unseen instruments only.** Not jobs in flight:
  one number meaning two things is how it becomes unreadable. While jobs are in
  flight the Library tab carries a small ring dot (no digit); when something has
  landed the dot becomes the number. Opening the Library clears it (the list has
  already rendered the NEW markers by then).
* **Failures do not count towards the badge** and stay in the capsule for 8 s;
  the amber ring and the reason are the whole of it.

## 7. The exact edits

Order: this record, then R4 (running), R2, R7 can run in parallel, R3 after R2
and the Controls panel (both edit `ShellPage.h`), then the human check.

### R2 `synthis-7o3.2`: native (kromosynth-plugin)

1. **New `src/ProgressSentinel.h`**, header-only, namespace `kromosynth::progress`,
   modelled on `AuthSentinel.h`:
   `kProgressHost = "progress"`; `enum class Phase { queued, rendering, delivering,
   failed, landed, lost }` (the last two native-only); `Reason` enum with
   `toWire`/`fromWire`; `struct Event { juce::String job, title, artifact;
   Phase phase; int pct = -1; Reason reason; }`; `isProgressSentinel (url)`
   (host match); `std::optional<Event> parse (url)`; limits `kMaxJob = 64`,
   `kMaxTitle = 40`. `nullopt` means "could not read", **and the caller must
   still block** (the contract `AuthSentinel.h` states). `phase=landed` and
   `phase=lost` parse to nullopt.
2. **`src/PluginEditor.cpp`, `InstrumentedBrowser::pageAboutToLoad`:** add the
   branch directly after the auth branch and before the checkout branch, same
   shape (`interceptsSentinels && isProgressSentinel (url)`; parse; log
   `progress_sentinel <job> <phase> <pct>` or `progress_sentinel_malformed` or
   `progress_sentinel_unknown_phase`; `return false`). Add `std::function<void
   (const kromosynth::progress::Event&)> onProgress` beside `onAuthState` in
   `PluginEditor.h` and set it where `onAuthState` is set.
3. **Activity list in the editor** (`PluginEditor.h/.cpp`): one entry per job
   with the fields in section 4; the transition rules of section 2 (forward
   only, `landed` native-only, revive on landing); expiry in the existing
   `timerCallback`: ring indeterminate after 60 s without an event, `lost` after
   180 s, no silence clock while `showSite (false)` has the site hidden (restart
   it from zero when the site is shown), a 25 min cap regardless, terminal
   entries removed after 8 s.
   Pure logic in a small header (`ActivityList.h`) so the acquire-probe can test
   it without a window.
4. **Push and pull:** `emitEventIfBrowserIsVisible ("activityChanged", juce::var
   (true))` on every change; `.withNativeFunction ("getActivity", ...)` returning
   the JSON of section 4.
5. **Landing:** an editor-set notification beside the processor's completion
   handler in `AcquisitionRunner` (see section 4), joining on `artifact`; an
   acquisition with no known job creates a `landed` entry titled from the
   request's `name`.
6. **Site reload:** in the site browser's `pageFinishedLoading`, mark every
   non-terminal entry `lost`.
7. **Self-test:** extend `tools/acquire-probe` (gate step 2) with: a valid
   progress URL; each phase; unknown phase (blocked, not parsed); missing `job`;
   missing `phase`; over-long `job` and `title`; `pct` out of range and with a
   non-rendering phase; `phase=landed` rejected; and that **none of the other
   verbs match** (`synthis://progressive`, `synthis://acquire`, `synthis://auth`).
   Plus the `ActivityList` rules: out-of-order events ignored, revival on
   landing, lease expiry with an injected clock.
8. Do **not** change what the other verbs do, `scripts/gate.sh`, the protected
   files, or AU/VST3 audio.

### R3 `synthis-7o3.3`: bar (src/ShellPage.h)

1. **Capsule inside `nav#chrome`**, in `.tabs` between the Biomes tab and
   `#panic`, so `chromeHeight()` still measures it. Ring (SVG, determinate when
   `pct >= 0`), text per section 6 Q6, hidden when there are no entries.
   Narrow bar: ring and count only.
2. **Library badge** on the first tab (`data-dest="0"`): ring dot while jobs are
   in flight, the `unseenLanded` number when something landed, cleared when the
   Library is opened.
3. **Wiring:** `backend.addEventListener ('activityChanged', pullActivity)`,
   `pullActivity` calls `getActivity` and renders; a 2 s `setInterval` that pulls
   only while `active > 0`.
4. **On landing:** a short pulse on the Library tab and the capsule folding into
   it; the landed row gets the existing NEW marker through the existing
   `newIds` mechanism (a landing triggers `refresh()` already via
   `pollAcquisitions`; do not duplicate it).
5. **Remove the arrival status line** in `pollAcquisitions` (`ShellPage.h`, the
   `Added N instrument(s)` / `Saved N render(s)` `setStatus` call, and the
   in-flight `Downloading <id> <pct>%` line). Keep the failure message
   (`failed[0].message`), keep `clearFinishedAcquisitions`, keep `refresh()` and
   `refreshRenders()`, and keep the `Loaded · N regions` line that `refresh()`
   writes for a manual load.
6. **`prefers-reduced-motion`:** no travel, no pulse; a static tick and the badge.
7. **Check script** in the style of `scripts/check-standalone-keys.sh` (not in
   the gate) asserting the elements and handlers exist, and extend the shell
   preview's stub bridge so the capsule can be driven with fake `activityChanged`
   events and a fake `getActivity`.

### R4 `synthis-7o3.4`: site card ring (kromosynth-desktop, built, awaiting review)

No sentinel here. Reviewed 2026-11-04 against this record (branch `bdloop/agents`
at `7620a73`; vitest passes 347 tests, the `node --test` files pass 61, and the
build succeeds; one unrelated vitest file, `ForceDirectedGraph.test.jsx`, cannot
load on the Studio's Node 26 because `localStorage` is undefined there, and the
branch does not touch it):

* **Matches the record:** outside `rendering` the ring is indeterminate, never
  0 % (`jobRingState.js`: a percentage only when `progress > 0`, in `rendering`);
  the tick only on `acquire.ok === true`; amber with the reason and Retry on
  failure; `prefers-reduced-motion` respected; the ring only inside
  `isPluginHost()`; no sentinel emitted; no verb or route removed.
* **The card's `downloading` and `installing` states are card-local** and do not
  need to match the contract's single `delivering`. `installing` is set
  synchronously just before the sentinel and `landed` follows at once, so it is
  effectively never visible. Harmless.
* **The card's tick means "handed over", not "installed".** `acquire.ok` is the
  site's own report that the ticket was obtained and the sentinel sent; only the
  plugin knows the instrument landed (typically 0.15 s later). The bar's capsule
  is the confirmed one.
* **Two loose ends**, worth a small follow-up rather than blocking the merge: the persistent
  `Rendering… Ns, queue #n` toast in `watchAndAcquire` is still there and
  now duplicates the ring; and the removed "Sent to plugin — free, this
  sound is already yours" toast was the only place a user learned a take cost
  nothing (`extractionsCharged === 0`), so that fact is now shown nowhere.
* The ring polls `useViJobProgress` itself, so a card with both the ring and the
  inline `FeedRenderJobIndicator` polls the job twice. Cheap, but one poll is
  tidier.

### R7 `synthis-7o3.7`: site emits the sentinel (kromosynth-desktop)

1. **One helper in `services/pluginHost.js`** wrapping the `synthis://progress`
   URL of section 3, `isPluginHost()` only, with the rate rules of 3.4.
2. **The choke point of 3.4** for every sentinel the site sends, so acquire,
   render, auth and checkout are never dropped and are spaced from progress
   events by the measured gap.
3. **Where to emit:** `watchAndAcquire` (module-level, survives route changes)
   for `queued`/`rendering`/heartbeats/`failed`/`timeout`; `handOverToPlugin`
   for `delivering` after step 1, with `artifact`. Use the same takeout entry id
   as `job` everywhere. Do not emit from a component's effect (an unmounted card
   would stop heartbeating and the plugin would mark a healthy job lost).
4. `failed` reasons map from what the code already knows: `429` +
   `extraction_quota_exceeded` is `quota`, `401` is `signed_out`, the
   `Render failed` branch is `render_failed`, the 20 min deadline is `timeout`,
   steps 2 and 3 failing is `handover_failed`.
5. **vitest:** payload shape, the rate rules, the gap, heartbeat, and that nothing
   is emitted outside the plugin host.

## What is not verified

**Needs a person at an unlocked screen** (the probe refuses to run locked):

```bash
cd ~/Developer/apps/synth.is/kromosynth-plugin && scripts/sentinel-probe.sh
```

It opens the Standalone three times (about 4 minutes in all) and prints, for
each run, whether the page lost focus, scrolled, dropped frames, reloaded, or
had its timers throttled. In the last run (`flash`, 10 sentinels a second for
20 s) **watch the window and say whether anything flashed or jumped**: no
counter can see a flash. The first two runs want the mouse left alone.

Everything else that is open:

* **Focus, scroll and frame cadence under sentinels, and the throttle with the
  site hidden in a visible window** (above). The delivery counts and the
  "no reload" result do not depend on this and stand.
* **The iPhone.** The probe is Standalone-only and the phone has no way to set
  the variable; the same navigation runs through `WKWebView` there, but nothing
  here was measured on the device. The check task (`synthis-7o3.6`) covers it.
* **That an old plugin ignores `synthis://progress` harmlessly.** Read from the
  code (unknown verbs are blocked and logged malformed), and the probe shows the
  malformed path at 100 events a second with no effect on the page, but no
  installed older build was run against it.
* **The hub's real job timings.** The vi numbers come from the laptop's copy of
  the job database (February to March), not today's hub.
* **Live-instrument timings** end to end: no instrument-kind job appears in that
  database, so the live path's duration is known only from the 4 s poll floor and
  the 0.15 s native leg.
* **The iOS build of the probe.** It compiles from shared code, but only the Mac
  Standalone was built here.
