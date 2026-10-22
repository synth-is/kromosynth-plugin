# Self-hosting WCLAP bundles on synth.is: licence checklist and packaging plan (synthis-aa4.92)

Desk check, 2026-10-20. **Not legal advice.** Every point that needs counsel is tagged **[COUNSEL]**; the list of
questions is in section 11. No code, bundle or script was written or committed by this task.

Question from Björn (2026-10-20): "couldn't we just host the binaries at our own site?" Answer: yes, if we meet the
obligations below. Doing so removes the dependency on openDAW's hosting (`assets.opendaw.studio`, which per the task
statement does not allow cross-origin fetch from synth.is; **not re-tested here**).

## 0. How to read the evidence tags

- **[READ]** read in a file on this machine in this run. The path is given. The open-webclap clone is
  `~/wclap-native/open-webclap` at `0833f5b` (`docs/OPEN_WEBCLAP_SIX_SINES_2026-10-20.md` s.1). Paths below written
  `ow:` are relative to it.
- **[DOC]** stated in an earlier repo doc (named). Not re-read upstream here.
- **[UNVERIFIED]** from the task description or general knowledge; no network access in this run, so no upstream
  page was fetched. Re-check before relying on it.

## 1. What the licences require, in one page

Premises, each with its source:

1. Every JUCE-built bundle links JUCE under its AGPL-3.0 option, so `module.wasm`/`ui/ui.wasm` ship as **AGPL-3.0**,
   on top of the plugin's own GPL-3.0(-or-later). [READ] `ow:README.md` s. "Licenses" (l.158-165) and each
   `ow:ports/<port>/NOTICES.md.in` "License" section. GPLv3 s.13 / AGPLv3 s.13 allow the combination
   [READ, quoted in those notices; the statutes themselves are **[UNVERIFIED]** here, `ow:licenses/AGPL-3.0.txt` has the
   AGPL text].
2. Serving a GPL/AGPL binary to a browser is *conveying* a copy. Position taken in the task description and
   `docs/WCLAP_DESK_CHECK.md` s.2 "Web" [DOC]. **[COUNSEL]** whether a browser download is "conveying" under GPLv3
   rather than only AGPLv3 s.13 "remote network interaction" is not needed to decide: we plan to comply as if it is.
3. Conveying object code requires GPLv3 s.6: the licence text, and the Corresponding Source by one of the listed
   means (accompanying medium, written offer valid 3 years, or a network server offering equivalent access next to
   the binary). The "Corresponding Source" includes build scripts and the pinned patches. [UNVERIFIED: statute not
   re-read; consistent with how `ow:scripts/bundle-license.sh` is designed.] **[COUNSEL]** which s.6 variant to rely on
   (see s.4: we choose 6(d), a server next to the binary).
4. Open-webclap's own practice is the reference: each bundle contains `LICENSE` (the AGPL-3.0 text), `NOTICES.md`
   (components, copyrights, and the repository commit the bundle was built from) and `licenses/` (longer texts),
   made by `bundle_license` in `ow:scripts/bundle-license.sh`. It records the commit as
   `<origin>/tree/<HEAD>` and appends "(plus uncommitted changes: not a release build)" when the tree is dirty.
   README l.160: "Build hosted bundles from a clean, pushed commit, so the source link matches." [READ]
5. Patches to a plugin are under that plugin's licence; `patches/juce-8-wasm.patch` is under JUCE's licence.
   [READ] `ow:README.md` l.155-156.

Consequence: we may host these bundles, but each served bundle is a GPL/AGPL distribution made by **us**, so the
source offer is ours to keep up, for as long as we serve it (and 3 years after if we rely on a written offer).

## 2. Per-bundle licence stack and what must ship

Common to all five JUCE bundles (everything in `bundle_license` output): `LICENSE` (AGPL-3.0), `NOTICES.md`,
`licenses/GPL-3.0.txt` (named by every notice), and the JUCE-bundled texts `licenses/JUCE-zlib.txt`,
`JUCE-libpng.txt`, `JUCE-libjpeg.txt`, `JUCE-HarfBuzz.txt`, `JUCE-SheenBidi-Apache-2.0.txt`, plus
`JUCE-FLAC.txt` where the bundle decodes FLAC (`ow:licenses/juce-bundled.md`, `ow:scripts/bundle-license.sh` l.8-17).
The JPEG notice "based in part on the work of the Independent JPEG Group" is required text (same file).

Pins in the table are from the notices [READ]; Six Sines' full pin list is `docs/OPEN_WEBCLAP_SIX_SINES_2026-10-20.md`
s.1 [DOC]; the others' dependency commits are in `ow:scripts/fetch-deps.py` (OB-Xf l.47, RipplerX l.109-111,
Odin 2 l.125-126, Surge `4f425b0` per its notice) [READ].

| Bundle | Engine licence (as built) | Pinned engine commit | Third-party items needing their own text or notice (from its `NOTICES.md.in`) | Per-bundle flags |
|---|---|---|---|---|
| **Surge XT** | GPL-3.0-or-later, "except its fonts"; bundle AGPL-3.0 | `4f425b0` (1.4.0) | sst-* libs (GPL-3.0-or-later or MIT per library); Airwindows MIT; Mutable Instruments Plaits/Clouds MIT; tuning-library MIT; fmt (MIT with binary exception); PEGTL MIT; r8brain MIT; PFFFT/FFTPACK (UCAR terms: no-endorsement + reproduce notice); Zstandard BSD-3; binn Apache-2.0; SQLite public domain; ghc::filesystem MIT; SIMDe MIT; CLAP MIT; fonts Lato, Fira Mono, Indie Flower (OFL-1.1) and DejaVu (Bitstream Vera) | **Modified version of Surge XT**: the notice says so (changes 2026-10-10, `patches/surge-*.patch`: no undo history, no thread on patch load, no Lua/MTS-ESP/OSC). Modified-version notice must survive. Factory patches, wavetables and skins are listed inside "Surge XT … GPL-3.0-or-later": **do not assume they are CC0** (see s.7). OFL fonts: must not be sold on their own and reserved font names apply **[UNVERIFIED]** |
| **OB-Xf** | GPL-3.0-or-later; bundle AGPL-3.0 | `b08ffb6` | sst-basic-blocks GPL-3.0; MIT, zlib (strnatcmp), 0BSD (MTS-ESP client) texts; Jersey 20 font (`ow:ports/obxf/OFL-Jersey20.txt`); Trek font | Factory patches and themes are inside the GPL-3.0-or-later line. Trek font's licence is **not stated in the lines read** (notice l.25 lumps it with the plugin): **[UNVERIFIED]**, check `ow:ports/obxf/NOTICES.md.in` and the upstream font file before shipping |
| **Odin 2** | GPL-3.0-or-later; bundle AGPL-3.0 | `265e9e2` (2.4.1) | reverb adapted from zita-rev1 (GPL, noted in its notice l.21); Aldrich font OFL-1.1 (`licenses/OFL-1.1-Aldrich.txt`); tuning-library MIT; MIT/other texts | Wavetables, presets and graphics are under the plugin's GPL line (notice l.27). Odin 2 ships the no-threads and async-dialog patches (`ow:patches/odin2-*.patch`) so it too is a *modified* version |
| **Six Sines** | Own source MIT; dependencies sst-basic-blocks and sst-filters GPL-3.0, so `module.wasm` is **GPL-3.0**; `ui/ui.wasm` also contains JUCE so it is **AGPL-3.0** (the only bundle where the two modules differ) | `03f3b9af…` (+ pins in the Six Sines doc) | BSD-2 (libsamplerate), FFTPACK (pffft), zlib (TinyXML, strnatcmp), 0BSD (MTS-ESP), MIT texts | Row in `ow:README.md` l.27 calls it "MIT (GPL-3.0 as built)": never describe our served copy as MIT. Patch `six-sines-no-threads.patch` |
| **RipplerX** | GPL-3.0 (not "or-later"); bundle AGPL-3.0 | `11683a6` (1.5.18) | Ubuntu Medium font under **Ubuntu Font Licence 1.0** (`licenses/UFL-1.0-Ubuntu.txt`, present in `ow:licenses/`); BSD limiter text; 0BSD (MTS-ESP) | Presets, images and **mallet samples** are listed under the GPL-3.0 row (notice l.26). UFL-1.0 has renaming and "no sale of the font alone" conditions **[UNVERIFIED]**. Patch `ripplerx-headless.patch` |
| **Our DX7 core** | Apache-2.0 (per the task description; not found in this repo: **[UNVERIFIED]**, locate the repo and its `LICENSE`) | pin = our own tag | Apache-2.0 s.4(d): ship the `NOTICE` file if the core has one, keep copyright notices, state changes | Needs only the NOTICE and the licence text. If the DX7 core is *built into a JUCE bundle* (route 1 or 2 of `docs/WCLAP_GUEST_HOSTING_ROUTE.md`), that bundle is AGPL as a whole, and the DX7 core inside it is still Apache-2.0 for anyone who takes it from the core's own repo. If it is built as a **JUCE-free** WCLAP (like SC Dark, `ow:README.md` l.146-153) the bundle can stay Apache-2.0 + NOTICE **[COUNSEL]** (does Apache-2.0 plus dependencies force a different bundle licence?). Voice data: any DX7 sysex / patch bank licence is separate, see `docs/` DX7 sources note (Bobby Blues collection: public-domain claim only, internal use, per the workspace memory **[UNVERIFIED here]**), so **no sysex banks ship in the DX7 bundle** |

Also confirm for every bundle **[UNVERIFIED]**: `ow:ports/<port>/LICENSE` is the plugin's own licence file, "when it
is not MIT" (`ow:README.md` l.133). For Six Sines there is no `ports/six-sines/LICENSE`, so `licenses/GPL-3.0.txt`
for that bundle must come from the fetched sources and be checked to be present in the output.

The kit itself (`modules/juce_webclap`, scripts, site) is MIT, Copyright (c) 2026 André Michelle (`ow:LICENSE`,
`ow:README.md` l.152). MIT notices for the kit are in each `NOTICES.md` ("WebCLAP kit … MIT").

**Checklist per served bundle (all must be true before the URL goes live):**

- [ ] `LICENSE`, `NOTICES.md`, `licenses/*` present inside the bundle archive, produced by `bundle_license`.
- [ ] `NOTICES.md` source link points at a **pushed, clean** commit of our build repo (no "uncommitted changes" suffix).
- [ ] The commit is tagged (s.4) and the `/source` page (s.4) lists every pin.
- [ ] Modified-version statement present (Surge XT, Odin 2, Six Sines, RipplerX are patched).
- [ ] Every row in the bundle's component table has its licence text file in `licenses/` (Surge/Odin notices cite
      the file names; verify each exists in the built bundle, not just in the template).
- [ ] The page that loads the bundle shows the notice (s.6).

## 3. Not the same thing: patches (presets) versus engines

Engine licence does not set patch licence. The rule in force (task description, and `docs/ARTEFACT_LICENCE_DECISION.md`
s.1 [READ]): Synth.is claims **no copyright** in evolved renders or in the `.dsp`/`.wasm` evolved for a user, and the
AGPL is not claimed over them. Third-party terms still apply underneath. Two separate cases:

1. **Patch state we generate** (a Surge/Odin/Six Sines state blob found by the search). Our own copyright claim: none
   by the decision above. Whether the *output sound* of a GPL synth is covered by the synth's licence: widely held
   not (`docs/WCLAP_DESK_CHECK.md` s.2 iOS, "audio output of a GPL synth is not covered" [DOC, flagged "confirm per
   engine"]). **[COUNSEL]** confirm, particularly for patches that embed or reference factory content (wavetables,
   RipplerX mallet samples, Odin wavetables, Surge factory patches) since a state blob can *contain* them.
2. **Public audio.** The task's rule: only patches declared **CC0** may have public audio (previews, shared links,
   embed.synth.is). A bundle being freely served says nothing about its factory presets being CC0. Therefore: factory
   presets that ship inside a served bundle stay behind the bundle's GPL/AGPL notice and are **not** added to the
   public-audio set unless an upstream grant is found (**[UNVERIFIED]**, none read here). Evolved-state patches
   become public audio only through the existing CC0 declaration flow, independent of this document.

Do not generalise `ARTEFACT_LICENCE_DECISION.md` to bundles; it says so itself ("covers *our evolved Faust*
artefacts", `docs/WCLAP_DESK_CHECK.md` s.2 intro [DOC]).

## 4. Source-offer design

Goal: for every served bundle hash, a stable place that yields the Corresponding Source for exactly that binary.

**4.1 Where it lives (choice):** a `/source/<bundle>/<version>` page on `synth.is` (and mirrored under
`embed.synth.is/source/…`), plus a downloadable tarball, plus a git tag. We rely on GPLv3 s.6(d) (a network server
offering equivalent access next to the binary) rather than a 3-year written offer, which would bind us after we stop
serving. **[COUNSEL]** (6(d) when the binary is served from the same origin as the source page vs when it is loaded
inside a third-party page via embed).

**4.2 Each `/source/` page states:**

- bundle name, version, **content hash** (the one in the URL), build date, and "built on: Mac Studio, macOS arm64,
  Emscripten X".
- Engine upstream URL and exact commit (from `NOTICES.md`); every dependency pin from `fetch-deps.py`
  (Six Sines list: `docs/OPEN_WEBCLAP_SIX_SINES_2026-10-20.md` s.1 is the template).
- The open-webclap commit (`0833f5b` today) and **our** patch set applied on top: `open-webclap-exnref.patch`
  (`scripts/wclap-exnref/`, 7 files +17/-5 [DOC]) and any bundle-specific patch.
- Toolchain: emsdk legacy `3.1.51` or exnref `4.0.20` (`6913738e`), and the exact command line (s.8).
- Links: the git tag, the tarball (`<bundle>-<hash>-source.tar.gz`) and its SHA-256, the licence texts, a
  "report a licence problem" contact, and the same offer text GPLv3 s.6(d) needs ("the Corresponding Source is
  available from this same place").
- A statement that the served build is **modified** where it is (Surge XT, Odin 2, Six Sines, RipplerX).

**4.3 The tarball must contain** (so a user can rebuild without us): our fork/mirror snapshot of open-webclap at the
pin **including `external/` as fetched** (source is not checked in upstream, `ow:README.md` l.134-135 area, so the
tarball is the only complete source; it is built by running `fetch-deps.py` then `git archive`/`tar`), the patch files, the pin file
(s.8), the build scripts, the `bundle_license` outputs, and a `BUILD.md`. Size: unknown, estimate tens of MB
(JUCE + Surge). **[UNVERIFIED]** size.

**4.4 Retention.** Keep every tarball for as long as the matching binary is reachable at any URL, and for at least
3 years after the last hash was withdrawn **[COUNSEL]** (needed only if a written offer is ever used). Never delete a
source tarball while the immutable binary URL remains cached by a CDN or embedded by a third party.

**4.5 Mirrors, not links to GitHub alone.** Upstream repos can disappear; the tarball is the authoritative source.
GitHub URLs are listed as convenience.

**4.6 Site-wide AGPL offer.** The AGPL network clause covers modified AGPL code we run as a service
(`docs/WCLAP_DESK_CHECK.md` s.2 "Web" [DOC]); the existing site-wide source offer must link to `/source/` so it is
one list. The loader/glue in the web app is published AGPL-3.0 (desk check recommendation (b), kept here).

## 5. Versioned immutable URLs, cache headers, CORS

**5.1 URL shape.** `https://synth.is/wclap/<id>/<version>-<sha256-first-12>/…` for the unpacked bundle directory, or
`…/<id>-<version>-<hash>.wclap.tar.gz` for the archive (`pack-wclap.py` output, `<name>.wclap.tar.gz`
[DOC, Six Sines doc s.2]). The hash is over the **tarball bytes**, recorded in the pin file and printed on the `/source`
page. A rebuilt bundle with different bytes gets a new URL; never overwrite. Legacy-exception and exnref builds are
different bytes, so separate URLs (they cannot share a build dir either: Six Sines doc s.2).
Choose the exception form per client: Wasmtime requires exnref, browsers/Node accept the legacy form
(`docs/WCLAP_GUEST_HOSTING_ROUTE.md` s.1 "Not shown" [DOC]); for browsers we serve what the browser supports and
**feature-detect** (not tested here **[UNVERIFIED]**: which browsers accept exnref `try_table`).

**5.2 Serving files.** Deploy through the existing synth.is web deploy (hub serves `packages/web/dist`, swap-build
recipe; memory "Deploying synth.is web + burst", **[UNVERIFIED]** whether `dist/wclap/` survives the swap: it must be
written into dist or be a separate directory outside the swap; a `dist` wipe would 404 immutable URLs and break the
source offer). Recommendation: a **separate static root** `/srv/synthis-wclap/` outside `dist`, served by the hub, so
web deploys cannot delete it.

**5.3 Headers (immutable content):**

| Header | Value | Why |
|---|---|---|
| `Cache-Control` | `public, max-age=31536000, immutable` on hashed bundle files | name contains the hash, so safe |
| `Content-Type` | `application/wasm` for `module.wasm`/`ui.wasm` | required for `WebAssembly.instantiateStreaming` |
| `Content-Encoding` | brotli or gzip pre-compressed variants for `.wasm`, `Vary: Accept-Encoding` | 7-14 MB modules |
| `Access-Control-Allow-Origin` | see 5.4 | fetch from other origins |
| `Cross-Origin-Resource-Policy` | `cross-origin` | needed if a page uses COEP `require-corp` (shared memory / threads; **[UNVERIFIED]** whether our pages set COEP) |
| `ETag` / `Accept-Ranges` | default | |
| `/source/*` pages | `Cache-Control: no-cache` (revalidate) | the offer text may be corrected |

Mutable pointer (`/wclap/<id>/latest.json` naming the current hash) is `no-cache`, never `immutable`.

**5.4 CORS.** Allow-list, not `*`: `https://synth.is`, `https://www.synth.is` (if used), `https://embed.synth.is`,
and the dev origins. Echo the matched `Origin` and send `Vary: Origin`. Do **not** open to `*`: a wildcard makes our
copy a free third-party CDN for GPL binaries, which is allowed by the licence but means we have to carry the source
offer for hosts we cannot see. **[COUNSEL]** (are we "conveying" to a third-party page that fetches us; does
CORS-blocked access change anything). Third-party embedding we *do* want (openDAW "open in" via our URL, see
`docs/WCLAP_DESK_CHECK.md` s.2): can use `Access-Control-Allow-Origin: *` on the **bundle** files only if we accept the
above, and open the licence files too.

**5.5 `embed.synth.is`.** Cross-origin from the main site; needs the allow-listed CORS above, and a **notice visible in
the embed frame** (s.6), not only on the host page. If the embed page is cross-origin-isolated, keep
`Cross-Origin-Embedder-Policy` compatible (CORP header in 5.3).

## 6. What changes in the web app that loads the bundles

Hosting route is chosen in `docs/WCLAP_GUEST_HOSTING_ROUTE.md`: route 1 (`wasm-merge`) first for Surge, route 2 as the
Safari answer, route 3 for Faust only, route 4 only if upstream builds it. Whichever is used, the web app change is:

1. **Loader.** `wclap-host-js` (licence **[UNVERIFIED]**; desk check s.2 "Cross-cutting" lists it as unread) or our own
   loader (`packages/wclap-host-node` is the headless equivalent in kromosynth-burst, Six Sines doc s.3 [DOC]) fetches
   `https://synth.is/wclap/<id>/<ver>-<hash>/…` instead of `assets.opendaw.studio`. The URL comes from a **manifest**
   (`wclap-manifest.json`: id, version, hash, tarball URL, source URL, licence SPDX, exception form), baked into the
   web build; no hard-coded host strings elsewhere.
2. **Notice on the loading page.** The page must not hide the licence: a visible "Open-source synth engine: Surge XT
   (GPL-3.0-or-later, AGPL-3.0 as built) · licence · source" with links to `/source/…` when a bundle is loaded, in
   the app and in the embed frame. A footer link alone is the minimum; a link inside the instrument panel is the plan.
   The bundle's own `ui/index.html` page (the kit's editor page) should be left unmodified so its notices remain.
   **[COUNSEL]** whether our host page needs to be AGPL as a whole (desk check s.2 "Web" option (a) vs (b); we do (b)).
3. **Integrity.** Check `sha256` of the fetched bytes against the manifest (`crypto.subtle.digest`), refuse on mismatch.
4. **Cache.** Service worker or `Cache API` keyed by the hashed URL; the immutable header handles the HTTP cache.
5. **Terms of service.** The ToS must not forbid redistribution or add restrictions on the bundles (GPLv3 s.10
   "no further restrictions"). The extraction quota is a metering rule on *our service*, not a licence term
   (`docs/WCLAP_DESK_CHECK.md` s.4 [DOC]); ToS says so.
6. **Export to openDAW.** The exported bundle URL or archive carries the same `LICENSE`/`NOTICES.md` and a source link.
7. **Remove** the openDAW CDN URL from any config; keep a fallback only as a manual override, since a hosted
   fallback we do not control cannot carry our source offer.

## 7. iOS exception

Decision 2026-10-12 (and recorded 2026-10-12 in `docs/WCLAP_DESK_CHECK.md` s.2 "iOS" and s.5 item 3 [READ]): **no
GPL/AGPL engine WCLAP in the App Store binary**; iOS uses SFZ (sfizz, BSD-2, `src/SamplerEngine.cpp`, primary iOS
engine per `docs/AUV3_IOS_FINDINGS.md`) rendered by kromosynth-vi. Reasons recorded: Apple's terms vs GPLv3 s.10
(**[UNVERIFIED]**, FSF/VLC position), guideline 2.5.2 on downloading executable code, no JIT.

Implications for self-hosting:

- The iOS app **must not fetch** `https://synth.is/wclap/...` or embed it: not in the app, not in the AUv3. The
  manifest carries a per-bundle `platforms` field (`web`, `desktop`) and iOS builds ignore it.
- The Safari web app (not App Store) takes the web route (s.6). **Safari multi-memory support**, needed by route 1, was
  not re-checked (`docs/WCLAP_GUEST_HOSTING_ROUTE.md` s.3) **[UNVERIFIED]**. A WKWebView *inside* the App Store app
  that loads the same bundles is the same problem as embedding them: do not.
- SFZ renditions of engine patches: the render happens server-side from the bundle; sample content from a GPL synth's
  output is not claimed covered by the engine licence (s.3 above, **[COUNSEL]**). Any engine-provided sample set
  (RipplerX mallets, Odin wavetables) rendered into SFZ should be reviewed per engine.
- AGPL-3.0 JUCE is **already** in the iOS app (`docs/WCLAP_DESK_CHECK.md` s.2 iOS [DOC]); the App Store question is open
  for the whole app and unaffected by this document. **[COUNSEL]**.

## 8. Reproducible build, pin file, ownership

**Pin file** (one per bundle, committed in a build repo, e.g. `kromosynth-plugin/wclap/pins/<id>.json`; not written
here): `open_webclap` commit; `emsdk` version and commit; `exception_form` (`legacy`/`exnref`); every `fetch-deps.py`
entry (repo, commit) as the script computes it; patch file SHA-256s; `cmake` flags (`-DWEBCLAP_EXNREF=ON`); the
`SOURCE_DATE_EPOCH` used; the output tarball SHA-256. The Six Sines table
(`docs/OPEN_WEBCLAP_SIX_SINES_2026-10-20.md` s.1) is the model.

**Build command shape** (from that doc s.2): `source emsdk_env.sh; WEBCLAP_BUILD_DIR=… bash scripts/build-<port>.sh
[-DWEBCLAP_EXNREF=ON]`. Build from a clean, pushed commit (`bundle_license` marks a dirty tree "not a release
build", `ow:scripts/bundle-license.sh` l.30-33); **release gate: refuse to publish a bundle whose `NOTICES.md` contains
that marker**.

**Reproducibility is NOT yet shown.** Two builds producing identical bytes has not been tested. Emscripten embeds
paths/timestamps in some outputs **[UNVERIFIED]**, `pack-wclap.py` tar metadata (mtime, owner) likely varies
**[UNVERIFIED]**. Until byte-reproducibility is proven, what the licence needs is *the same source that built that
binary*, not rebuilt bytes: so the hash identifies the served binary and the pin file identifies its source. Plan: do
a double-build comparison on the Studio for one bundle and record the result; if bytes differ, normalise tar
metadata (`--mtime`, `--sort=name`, `--owner=0`) and `SOURCE_DATE_EPOCH`, and report remaining diffs.

**Ownership of each build:**

| Bundle | Build owner | Notes |
|---|---|---|
| Surge XT | Mac Studio, `~/wclap-native/juce-webclap-plugins` (aa4.52, exnref) now; moves to the open-webclap pin for the served copy | open-webclap's Surge port differs by three commits (editor meters, NOTICES, test host audio inputs; Six Sines doc s.5 [DOC]). **Decision needed:** serve open-webclap's port (carries the AGPL/GPL `NOTICES.md` and `LICENSE`) rather than ecab895, which does not label licences the same way. Recommended: open-webclap pin |
| Six Sines | Studio, open-webclap `0833f5b` (aa4.90) | Both legacy and exnref built; not committed |
| OB-Xf, Odin 2, RipplerX | Studio, open-webclap `0833f5b` | **Not built by us yet**; exnref hunk not tested for them (Six Sines doc s.5 [DOC]) |
| DX7 core | our repo, our CI | Apache-2.0; build owner = the DX7 port owner, **TBD** |

"Owns the build" = keeps the pin file current, runs the double build, and publishes the bundle and tarball together
(atomically: a bundle URL must never exist without its `/source` page). Suggest the existing bdloop harness on the
Studio runs it; Studio-detached-process LAN block applies (use tailnet names for any upload step).

## 9. Upstream contributions and how they interact with obligations

- **Our patches are derivative of GPL code** and distributed by us, so they must be offered under the same licence
  anyway; contributing them upstream costs nothing extra. For open-webclap (MIT kit): the exnref opt-in
  (`scripts/wclap-exnref/open-webclap-exnref.patch`, prepared; "No PR opened", Six Sines doc s.4 [DOC]) is a
  build-flag change in MIT files, contributable as MIT. Patches touching a plugin go under that plugin's licence
  (`ow:README.md` l.155).
- **Upstream wclap-bridge fixes** (aa4.51; `docs/WCLAP_BRIDGE_UPSTREAM_ISSUE.md`): the bridge is BSL-1.0
  (`docs/WCLAP_DESK_CHECK.md` s.6 [DOC, read via summariser]), so contributions stay BSL-1.0. No effect on bundles.
- **DX7 port.** Our core is Apache-2.0 [UNVERIFIED location]. Offering it to open-webclap as a port: `ow:README.md`
  l.120-125 says a JUCE port's licence must allow combination with JUCE AGPL; Apache-2.0 allows it (GPLv3 side:
  **[COUNSEL]**; Apache-2.0 and GPL-**2**-only are incompatible, and GPL-3.0 is compatible, which is the
  case here, **[UNVERIFIED]** statute not re-read). If the port is JUCE-free (like SC Dark's independent path)
  it can stay Apache-2.0.
- **What contribution does NOT do.** It does not release us from our own source offer: even if upstream carries the
  same bytes, *we* still serve a binary and must offer source for it. Upstream acceptance is not a substitute for
  s.4.
- **Copyright assignment.** None expected; contributions keep our copyright under the receiving project's licence.
  **[COUNSEL]** for the employer / funding angle (Icelandic Technology Development Fund grant terms may place
  conditions on IP; **[UNVERIFIED]**, not read).
- **Naming.** The Surge XT notice already says "This is not an official Surge XT release." Keep this statement and do
  not use project trademarks beyond nominative reference **[COUNSEL]** (Surge, Odin, "openDAW", "WebCLAP").

## 10. Cross-cutting items, ordered

1. Decide the Surge XT source (open-webclap pin vs juce-webclap-plugins ecab895) and rebuild with `bundle_license` (s.8).
2. Verify the missing facts marked **[UNVERIFIED]** that gate going live: Trek font and Jersey 20 licence (OB-Xf),
   UFL-1.0 conditions (RipplerX), OFL font reserved names, `wclap-host-js` licence, DX7 core location and `NOTICE`.
3. Build all five on the Studio; double-build one for reproducibility (s.8).
4. Create the static root, headers and CORS (s.5), the `/source/` pages and the tarballs (s.4).
5. Web app: manifest, loader URL change, visible notice, integrity check (s.6).
6. Ensure the iOS target excludes the manifest (s.7).
7. Counsel review (s.11) **before** the first public URL.
8. Third-party notices in this repo: `THIRD_PARTY_NOTICES.md` should list the hosted bundles only if the plugin
   distributes them; the web hosting is documented on the `/source` pages instead.

## 11. Questions for counsel

1. Is serving a GPL-3.0 / AGPL-3.0 WASM module to a browser "conveying" (GPLv3) and/or "remote interaction" (AGPLv3 s.13)?
   Which of GPLv3 s.6(a)-(e) should we rely on, and does a `/source` page next to the binary satisfy 6(d)? (s.1, s.4.1)
2. Is the web page (our app, which loads the module over the WCLAP/CLAP ABI) a "combined work" with the module, or
   a separate program? Does our decision to publish the loader/glue under AGPL close the question? (s.6.2)
3. What exactly is "Corresponding Source" for a wasm bundle built with a patched toolchain stack (open-webclap, patches,
   JUCE, emsdk, `fetch-deps.py` downloads)? Do we have to include emsdk? Do we need to ship `external/` as fetched? (s.4.3)
4. For a bundle that is built from MIT + GPL + AGPL + Apache + OFL/UFL parts: is it enough to include the licence texts
   and a component table (the `NOTICES.md` approach), and are the font licence conditions (renaming, no standalone sale) met? (s.2)
5. Is the output sound of a GPL/AGPL synth, and a patch/state blob that contains or references the synth's factory wavetables,
   samples or presets, free of the engine's licence? Which factory content (Surge factory patches, RipplerX mallet samples,
   Odin wavetables) is safe to use in public audio or must be treated as GPL data? (s.3)
6. May we allow `Access-Control-Allow-Origin: *` on the bundles, i.e. let third-party pages fetch GPL binaries from
   us? What are our obligations towards them and towards their users? (s.5.4)
7. Does the iOS exclusion decision suffice, and is anything about the App Store terms affected by the AGPL JUCE already
   in the iOS app? Does an SFZ rendition made *on our server by a GPL engine* carry any obligation? (s.7)
8. Our DX7 core under Apache-2.0: may it live inside an AGPL bundle and still be offered separately as Apache-2.0?
   Do we owe anything beyond the NOTICE? Any trademark or patent considerations around DX7/Yamaha naming and the
   third-party sysex banks? (s.2, s.9)
9. Do the funding agreement (Icelandic Technology Development Fund) or any employer terms restrict how we may license
   or contribute code upstream? (s.9)
10. How long must we keep source after a bundle is withdrawn, and does the immutable-URL policy create obligations
    for old versions that remain cached by third parties? (s.4.4)
11. Terms of service: does the extraction quota or a ToS clause on bundle use create a "further restriction" under GPLv3 s.10? (s.6.5)
12. Do we need a registered DMCA/licence-contact process for third-party licence complaints about hosted copies? (s.4.2)

## 12. What was NOT verified in this run

- Nothing was fetched from the network; no upstream licence page or GPL statute was re-read.
- No bundle was built, hashed, served or loaded; the byte-reproducibility, header and CORS rules are design, untested.
- Section numbers of GPLv3 are cited from memory; check them against `ow:licenses/AGPL-3.0.txt` and the GPL-3.0 text before the lawyer sees this.
- Whether assets.opendaw.studio blocks CORS from synth.is is taken from the task statement.

## Update 2026-10-22 (synthis-aa4.95): source offer target

The corresponding source for the engine bundles is the open-webclap pin recorded in `scripts/wclap-pins/open-webclap.json`
(upstream commit `0833f5b`, plus our three-commit patch queue on branch `synthis/main` of the fork
`https://github.com/synth-is/open-webclap`; the public commit URL is a placeholder until the first push). Workflow and
comparisons: `docs/OPEN_WEBCLAP_UPSTREAM_2026-10-22.md`.
