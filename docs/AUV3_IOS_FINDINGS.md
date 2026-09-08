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

1. **Stage B** — `root()` to the group container, `rootOrigin()` logged.
   Migration of the four existing instruments deliberately NOT bundled with it.
   Acceptance is now visible in the shell itself (C13), not only via devicectl.
2. **The desktop signed-out test** (C11). No phone, no build, and it decides
   whether the acquisition fault is AUv3-specific or global.
3. **Stage C** — the log filename (C3), now escalated by C12 to a live
   multi-writer problem rather than a latent one.
4. **Instrument the acquisition OUTCOME** (C6, C11). The attempt is already
   logged; `fetchAndInstall` is not.
5. **Stage D** — the ticket across the boundary (C2).
6. **Re-measure voice caps at 256 frames** (C5), pre-registered.
7. Unified chrome, against 736x242 in GarageBand and a tall view in AUM (C4,
   C13).
