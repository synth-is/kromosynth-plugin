# Next session — kromosynth-plugin

Previous prompt preserved as `NEXT_SESSION_PROMPT.2026-08-31.md`.

## Where things stand

**Stage 1 acquisition is complete and verified end to end on a live host.** The
site hands the plugin an artefact; the plugin fetches, unpacks, installs and
names it. Every layer was proven against the real system rather than a
stand-in — HMAC minting through the real quota exemptions, ticket verification
at `/vi/plugin-file`, `synthis://` reaching `pageAboutToLoad` in a live
WKWebView, ZIP shape normalisation, atomic install, and three acquisition paths
(existing entry, re-render, fresh render from a feed).

Read `STAGE0_FINDINGS.md` §O before touching any of it. §O3 in particular: four
bugs this session were checks that passed while the thing they guarded was
broken, and the pattern is worth carrying rather than rediscovering.

**Stage 0** is closed (§A–§N). **Stage 2 — the Faust engine — has not started.**

## A correction to carry forward

Earlier notes narrowed "Gate 0" to "an iOS hardware measurement". That is wrong
and it made the Faust path look more blocked than it is.

WP_SYNTHIS_PLUGIN §Gate 0 runs in `kromosynth-render`, needs no JUCE, and asks
which runtime at what polyphony on which platforms. Its stated failure mode is
*"the Faust engine ships desktop-only in v1 and iOS carries the sampler engine
alone"* — desktop is the fallback, not the thing at risk.

What genuinely needs a device is the narrower iOS question, and only because
Apple forbids runtime code generation: no JIT, so iOS must interpret or AOT, and
interpreter throughput on real iOS silicon inside an AUv3 memory budget cannot
be inferred from a Mac.

**So live Faust in the macOS plugin is not blocked on iOS hardware.** It is
blocked on the desktop half of Gate 0 — protocol already written in
`EXPERIMENT_01_DSP_EXECUTION.md` — plus the JUCE-side engine work.

## Task — pick one

**1. Gate 0, desktop half.** Run the pre-registered protocol in
`kromosynth-render`. Answers which runtime and what polyphony on Apple silicon,
and whether the LLVM and WASM backends agree numerically (§2 says that is cheap
to test and must not be assumed in either direction). Unblocks Stage 2 and needs
no hardware you do not have. **This is the one that unblocks the most.**

**2. Quota visibility.** Cost currently appears only after the click. Grabbing
from a feed is far higher frequency than deliberate takeout, so the remaining
allowance should be visible before, not after. Touches
`kromosynth-desktop/packages/web` and possibly a new read-only endpoint. A
design decision as much as a code one — propose before building.

**3. Better names.** `Virtual Instrument #8NMZJ` beats a UUID and is still not
descriptive. `sounds[0].class` sometimes carries a readable fragment
(`238_buzz_drone-2_0_1`) and sometimes does not (`2_2_2_PQ-4_0_1`) depending on
whether the run used CLAP prompt classes. Making that dependable is QD template
work; the plugin inherits any improvement for free, because the name is composed
on the site and carried in the sentinel.

**4. Close `/vi/file/`.** Still unauthenticated, `Access-Control-Allow-Origin: *`,
no expiry check. Needs the web app's anchor-click download rebuilt as
fetch-plus-blob first, which risks a regression on the main product path —
large ZIPs, and the `Content-Length` line in `vi.js` exists because Cloudflare
otherwise buffers them. Weigh that before starting.

**5. Housekeeping**, none of it urgent, all of it recorded in §O8:
`~/.gitignore_global` line 10 is a bare `render` that swallows source
directories in every repo on this machine; `tests/` is gitignored in
kromosynth-recommend so no test there is version-controlled; and
`pm2/ecosystem.config.js:101` points at an `env-config/` directory that does not
exist.

## Build and test

```
./scripts/build.sh                 # compile only, installs nothing
./scripts/build.sh install         # sign with Developer ID and install
./scripts/build.sh install --backup   # copy the current bundle aside first
```

`verify` mode exists because a plugin build otherwise overwrites
`~/Library/Audio/Plug-Ins` and, with an empty signing identity, replaces a
notarised bundle with an ad-hoc one as a side effect of checking whether the
code compiles. The script reads the identity from the keychain and checks
afterwards that the mode did what it claimed, in both directions.

Acquisition can be exercised with no plugin, host or DAW:

```
./build/acquire-probe/tools/acquire-probe/acquire_probe_artefacts/Release/acquire_probe selftest /tmp/x
```

51 checks. Restart the host after `install` — AU caches component scans.

## Standing rules, unchanged and re-earned this session

- **Read the actual tree before designing.** Two of WP §13's three stated
  premises were wrong, and reading settled all three before any code existed.
- **Verify a test can go red.** Every fix this session was checked by breaking
  the thing it guarded first. Twice that revealed the test had never covered the
  path at all.
- **A green check is not evidence until you know which path it ran.** §O3.
- **Compile or run to confirm an API exists.** Three JUCE APIs guessed wrong this
  session, all caught that way, none by reading.
- **Query the live system for facts about it.** The status-string handling was
  fixed before shipping because live entries were queried rather than reasoned
  about — `artifacts_pruned` was unanticipated, and re-render starting from a
  terminal status would have reported false failures.
- **Check cache headers before believing a single `curl`.** A stale `index.html`
  led to telling Björn to redeploy something he had already deployed.
- **Selective staging.** `git_add` then `git_commit` via MCP commits exactly the
  staged paths and leaves other WIP alone — confirmed. The exception is a repo
  with *pre-staged* unrelated files, where a terminal pathspec commit
  (`git commit -F msg -- path`) is the only safe form.
- **Propose before writing** on anything with a design decision in it.

## Deploy notes

nginx serves synth.is directly from
`<SYNTH_ROOT>/kromosynth-desktop/packages/web/dist`, so deploying the web app is
`git pull && npm run build` on the hub. No upload, no restart.

`EXTRACTION_TICKET_SECRET` must be identical in `kromosynth-recommend/.env` and
`kromosynth-vi/.env` on the hub. A mismatch is externally indistinguishable from
a bad ticket — every download 401s and it looks like a user problem.

Confirm a deploy landed with:

```
curl -s https://synth.is/ -H 'Cache-Control: no-cache' | grep -o '/assets/index-[A-Za-z0-9_-]*\.js'
```
