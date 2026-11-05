// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Björn Þór Jónsson
// ShellPage — the local UI the plugin serves to itself.
//
// Stage 1. Small on purpose, and the smallness is the design, not a shortcut.
//
// ── Why this exists at all ───────────────────────────────────────────────────
//
// WP_SYNTHIS_PLUGIN.md section 4 originally said "one UI codebase, not three"
// and served everything from the platform. That was superseded on 2026-08-28
// (WP section 12) for two reasons neither of which is a preference:
//
//   1. The web app's service worker handles `push` and `notificationclick`
//      only -- 36 lines, no `fetch` handler. Nothing is cached, so a
//      platform-served UI is BLANK with no network. For a plugin expected to
//      work during a live performance that is disqualifying.
//
//   2. The list of locally downloaded instruments is plugin-side state. The
//      web app cannot render it without the native bridge regardless of
//      caching.
//
// So: this page, served from C++ over the resource provider, is the offline
// surface and the only place `window.__JUCE__.backend` is exposed. The live
// site keeps doing everything discovery-shaped, in a second component with
// native integration OFF.
//
// ── Why it is one hand-written string and not a build step ───────────────────
//
// A bundler, a package.json and a node_modules for a page with one list in it
// would be a second frontend toolchain to maintain for no gain, and it would
// make the plugin's build depend on npm. When this page needs a framework it
// will be obvious, and the honest move then is a real frontend directory --
// not to have pre-emptively built one now and pretended the decision was made
// on evidence.
//
// Kept deliberately dependency-free: no fonts, no CDN, no images. Every
// external reference would be a network request on a surface whose entire
// justification is working without a network.

#pragma once

namespace kromosynth::shell
{
/** THE BAR'S OWN HEIGHT, in logical pixels. NOT the strip's.

    This said "the height the shell collapses to when the site is up" and that
    was the wrong claim by the time it was written. The shell collapses to
    `stripHeight_`, which is what the PAGE reports: this number plus whatever
    `env(safe-area-inset-top)` turns out to be. Measured 2026-09-11 at 51 in an
    AUv3 (this plus the border, no inset) and 113 in the standalone (plus 62 of
    status bar). Neither is here, and neither could be.

    What this number still is, and why it stays:

      - the CSS's own bar height, substituted at the STRIPH placeholder the
        resource provider fills next to the palette's -- 34 for the tabs plus
        16 for the status line. One source, so the rule and this constant
        cannot become two numbers obliged to agree, which is the drift
        Palette.h's C26 note is about.
      - the seed for `stripHeight_`, so a page that never reports degrades to a
        strip that is merely too short rather than to one of zero height.
      - the floor of the clamp in `chromeHeight`, with six times it as the
        ceiling.

    The GarageBand arithmetic that justified 50: 736x242 leaves 191 for content
    at the measured 51, which is four rows at 44 against WP section 11's
    criterion of three. The original note credited that measurement to
    ChromeBar, which no longer exists; the budget it established does.
*/
inline constexpr int kStripHeight = 50;

/** The logo, served by the resource provider rather than fetched.

    The no-images rule below is about EXTERNAL references, because those are
    network requests on the one surface justified by working without a network.
    This is compiled into the binary and handed over by the same provider that
    serves the page, so the rule holds.

    Byte-for-byte `packages/web/src/assets/synthis_S_logo.svg` minus its XML
    prolog, so the plugin and the web app cannot show different marks. The
    `fill:#ffffff` is the file's own -- the web app uses the white logo on its
    dark chrome too, and this chrome is darker still.
*/
inline constexpr const char* kLogoSvg = R"SVG(<svg
   version="1.1"
   width="174.02478"
   height="246.08557"
   viewBox="0 0 174.02478 246.08557"
   xmlns="http://www.w3.org/2000/svg">
  <defs>
    <clipPath clipPathUnits="userSpaceOnUse" id="clipPath4">
      <path d="M 0,0 H 1920 V 1080 H 0 Z" transform="translate(-994.1677,-454.81691)" />
    </clipPath>
  </defs>
  <g transform="translate(-1180.6406,-596.95721)">
    <path d="m 0,0 v 0 c 0,-3.92 -3.178,-7.099 -7.099,-7.099 h -83.42 c -3.921,0 -7.099,3.179 -7.099,7.099 0,3.92 3.178,7.099 7.099,7.099 h 83.42 C -3.178,7.099 0,3.92 0,0"
       style="fill:#ffffff;fill-opacity:1;fill-rule:nonzero;stroke:none"
       transform="matrix(1.3333333,0,0,-1.3333333,1325.5569,833.57747)"
       clip-path="url(#clipPath4)" />
    <path d="m 0,0 v 0 c 0,-3.921 -3.178,-7.099 -7.099,-7.099 h -40.069 c -3.921,0 -7.099,3.179 -7.099,7.099 0,3.92 3.178,7.099 7.099,7.099 H -7.099 C -3.178,7.099 0,3.92 0,0"
       style="fill:#ffffff;fill-opacity:1;fill-rule:nonzero;stroke:none"
       transform="matrix(1.3333333,0,0,-1.3333333,1339.9089,757.85907)" />
    <path d="m 0,0 v 0 c 0,-3.921 -3.178,-7.099 -7.099,-7.099 h -18.363 c -3.92,0 -7.098,3.178 -7.098,7.099 0,3.92 3.178,7.099 7.098,7.099 H -7.099 C -3.178,7.099 0,3.92 0,0"
       style="fill:#ffffff;fill-opacity:1;fill-rule:nonzero;stroke:none"
       transform="matrix(1.3333333,0,0,-1.3333333,1289.3669,719.99987)" />
    <path d="m 0,0 v 0 c 0,-3.921 -3.178,-7.099 -7.099,-7.099 h -72.755 c -3.921,0 -7.099,3.178 -7.099,7.099 0,3.92 3.178,7.099 7.099,7.099 H -7.099 C -3.178,7.099 0,3.92 0,0"
       style="fill:#ffffff;fill-opacity:1;fill-rule:nonzero;stroke:none"
       transform="matrix(1.3333333,0,0,-1.3333333,1354.6667,795.71827)" />
    <path d="m 0,0 v 0 c 0,3.92 3.178,7.099 7.099,7.099 h 83.42 c 3.921,0 7.099,-3.179 7.099,-7.099 0,-3.92 -3.178,-7.099 -7.099,-7.099 H 7.099 C 3.178,-7.099 0,-3.92 0,0"
       style="fill:#ffffff;fill-opacity:1;fill-rule:nonzero;stroke:none"
       transform="matrix(1.3333333,0,0,-1.3333333,1209.7516,606.42253)" />
    <path d="m 0,0 v 0 c 0,3.921 3.178,7.099 7.099,7.099 h 39.975 c 3.921,0 7.099,-3.178 7.099,-7.099 0,-3.92 -3.178,-7.099 -7.099,-7.099 H 7.099 C 3.178,-7.099 0,-3.92 0,0"
       style="fill:#ffffff;fill-opacity:1;fill-rule:nonzero;stroke:none"
       transform="matrix(1.3333333,0,0,-1.3333333,1195.4059,682.14093)" />
    <path d="m 0,0 v 0 c 0,3.92 3.178,7.099 7.099,7.099 h 72.755 c 3.92,0 7.099,-3.179 7.099,-7.099 v 0 c 0,-3.92 -3.179,-7.099 -7.099,-7.099 H 7.099 C 3.178,-7.099 0,-3.92 0,0"
       style="fill:#ffffff;fill-opacity:1;fill-rule:nonzero;stroke:none"
       transform="matrix(1.3333333,0,0,-1.3333333,1180.642,644.2816)" />
  </g>
</svg>
)SVG";

// The page. `%%STATUS%%` is not templated -- everything is fetched through the
// bridge after load, so the HTML is static and can be served from a constant.
inline constexpr const char* kIndexHtml = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<title>Synth.is</title>
<style>
/*PALETTE*/
  * { box-sizing: border-box; }
  body {
    margin: 0; padding: 0;
    background: var(--surface-deep); color: var(--text-primary);
    font: 14px/1.5 var(--font-stack);
    height: 100vh; display: flex; flex-direction: column;
    /* MEASURED 2026-09-11: with viewport-fit=cover the layout viewport is the
       full webview (874pt, not the 812 the default gave), so the bottom inset
       is now ours to respect as well -- 34pt on this device. box-sizing:
       border-box above keeps the total at 100vh.

       Left and right are NOT handled. In landscape on a notched phone they are
       non-zero and the list will run under the rounded corner. Named rather
       than silently left out. */
    padding-bottom: env(safe-area-inset-bottom, 0px);
  }
  header {
    padding: 14px 18px; border-bottom: 1px solid var(--border-subtle);
    display: flex; align-items: baseline; gap: 12px; flex: 0 0 auto;
    /* Wraps, because four controls plus a title do not fit across a phone
       even once the editor is the right width. Without this the row simply
       overflowed and the last button was unreachable -- which is how "Test
       note" was visible only in landscape. */
    flex-wrap: wrap;
  }
  header h1 { margin: 0; font-size: 15px; font-weight: 600; letter-spacing: .01em; }
  header .count { color: var(--text-muted); font-size: 12px; }
  header button {
    background: var(--border-subtle); color: var(--text-primary);
    border: 1px solid var(--border); border-radius: 6px;
    padding: 5px 12px; font-size: 12px; cursor: pointer;
  }
  /* Only the FIRST button takes the free space, so the group still sits right
     on a desktop width. It was on every button, which happened to work only
     because nothing wrapped. */
  header button:first-of-type { margin-left: auto; }
  header button:hover { background: var(--border); }
  main { flex: 1 1 auto; overflow-y: auto; padding: 8px; position: relative; }

  /* The taken-sounds section. Sits in the same scroll region as the instrument
     list and above it, so the two read as siblings rather than as one page with
     something bolted on.

     z-index because #spectro is absolutely positioned behind the list at
     z-index 0: without a stacking value of its own this section would be drawn
     under the spectrogram and look like it had failed to render. */
  #rendersec { position: relative; z-index: 1; margin-bottom: 10px; }
  #rendersec .subhead {
    display: flex; align-items: baseline; gap: 10px;
    padding: 2px 4px 6px;
  }
  #rendersec h2 {
    margin: 0; font-size: 12px; font-weight: 600;
    text-transform: uppercase; letter-spacing: .06em;
    color: var(--text-muted);
  }
  #rendersec .count { color: var(--text-muted); font-size: 12px; }
  #rendersec .subhead button {
    margin-left: auto;
    background: var(--border-subtle); color: var(--text-primary);
    border: 1px solid var(--border); border-radius: 6px;
    padding: 3px 9px; font-size: 11px; cursor: pointer;
  }
  #rendersec .subhead button:hover { background: var(--border); }

  .rrow {
    display: flex; align-items: center; gap: 8px;
    padding: 6px 8px; border-radius: 7px;
    background: var(--surface-panel); border: 1px solid var(--border-subtle);
    margin-bottom: 4px;
  }
  .rrow .rname {
    flex: 1 1 auto; min-width: 0;
    font-size: 13px; color: var(--text-primary);
    overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
  }
  .rrow .rmeta { flex: 0 0 auto; font-size: 11px; color: var(--text-muted); }
  .rrow button {
    flex: 0 0 auto;
    background: transparent; color: var(--text-body);
    border: 1px solid var(--border); border-radius: 5px;
    padding: 2px 8px; font-size: 11px; cursor: pointer;
  }
  .rrow button:hover { background: var(--border-subtle); color: var(--text-primary); }
  /* Delete reads as destructive only on hover, so a row of them does not look
     like a row of warnings. It removes a file the user spent extraction quota
     on, which is why it confirms before acting. */
  .rrow button.rdel:hover { color: var(--danger); border-color: var(--danger); }
  .rrow button.rplay.on { color: var(--accent); border-color: var(--accent); }

  /* No longer needed, and removed rather than left inert: sounds and
     instruments are separate sub-tabs now, so at letterbox height the Sounds
     tab shows sounds and nothing competes with them. The rule that collapsed
     the rows existed because the section sat ABOVE the instrument list and
     pushed it off a 242pt window entirely -- measured in tools/shell-preview,
     and the reason the sub-tabs are a better answer than the media query was. */
  /* The spectrogram sits BEHIND the list, not beside it. It is context for the
     loaded instrument, not a control, and anything that took vertical space
     away from the list would be paying for decoration with the thing the page
     exists to show. */
  #spectro {
    position: absolute; inset: 0; width: 100%; height: 100%;
    z-index: 0; pointer-events: none; opacity: .34;
    transition: opacity .4s ease;
    /* Explicit rather than relying on the default. A 192x128 image stretched
       across the whole panel needs interpolation, and a browser that chose
       nearest-neighbour would draw visible blocks. */
    image-rendering: auto;
    /* Fades out at the bottom instead of ending on a hard edge, which read as a
       misplaced image rather than as backdrop. This said "fades into the
       footer"; the footers were deleted 2026-09-14 and it now fades into the
       bottom of the panel. The soft edge is still the point. */
    -webkit-mask-image: linear-gradient(to bottom, rgba(0,0,0,1) 55%, rgba(0,0,0,0) 100%);
    mask-image: linear-gradient(to bottom, rgba(0,0,0,1) 55%, rgba(0,0,0,0) 100%);
  }
  #spectro.empty { opacity: 0; }
  #list { position: relative; z-index: 1; }
  .toolbar {
    flex: 0 0 auto; display: flex; gap: 8px; align-items: center;
    padding: 8px 18px; border-bottom: 1px solid var(--border-subtle);
  }
  .toolbar input, .toolbar select {
    background: var(--surface-deep); color: var(--text-primary); border: 1px solid var(--border);
    border-radius: 6px; padding: 4px 8px; font-size: 12px;
    font-family: inherit;
  }
  .toolbar input { flex: 1 1 auto; min-width: 0; }
  .row {
    padding: 9px 12px; border-radius: 6px; cursor: pointer;
    border: 1px solid transparent;
  }
  .row:hover { background: var(--surface-base); border-color: var(--border-subtle); }
  .row.active { background: var(--accent-surface); border-color: var(--success-edge); }
  .row .top { display: flex; align-items: center; gap: 12px; }
  .row .name {
    flex: 1 1 auto; font-variant-numeric: tabular-nums;
    overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
  }
  .row .meta { color: var(--text-muted); font-size: 12px; flex: 0 0 auto; }
  .row .sub {
    margin: 2px 0 0 19px; color: var(--text-muted); font-size: 11.5px;
    overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
  }
  .row .sub .sep { opacity: .4; padding: 0 5px; }
  .row .sub .warn { color: var(--warning); }
  .row.fresh { border-color: var(--success-glow); }
  .row .new {
    flex: 0 0 auto; font-size: 10px; text-transform: uppercase;
    letter-spacing: .04em; color: var(--success);
    border: 1px solid var(--success-glow); border-radius: 4px; padding: 1px 5px;
  }
  .row .treebtn {
    flex: 0 0 auto; font: inherit; font-size: 11px; cursor: pointer;
    background: var(--border-subtle); color: var(--text-muted);
    border: 1px solid var(--border); border-radius: 4px; padding: 1px 7px;
  }
  .row .treebtn:hover { background: var(--border); color: var(--text-primary); }

  /* The list goes quiet while an instrument loads. Loading BLOCKS the message
     thread for one to three seconds -- measure() renders 105 s of audio to
     level the keyboard -- so further clicks would queue up behind it and
     arrive as a burst afterwards. */
  #list.busy { opacity: .55; pointer-events: none; }
  #list.busy .row.pending { opacity: 1; }

  /* CSS animation, deliberately. WKWebView runs the page in its own process,
     so this keeps turning while our message thread is blocked -- which is the
     whole point: the plugin is busy, not hung, and a static "Loading..." is
     indistinguishable from a freeze. */
  .spinner {
    display: inline-block; width: 11px; height: 11px; flex: 0 0 auto;
    border: 2px solid var(--border); border-top-color: var(--accent); border-radius: 50%;
    animation: spin .7s linear infinite; vertical-align: -1px;
  }
  @keyframes spin { to { transform: rotate(360deg); } }
  .row .rmbtn {
    flex: 0 0 auto; font: inherit; font-size: 11px; cursor: pointer;
    background: transparent; color: var(--text-muted);
    border: 1px solid transparent; border-radius: 4px; padding: 1px 7px;
  }
  .row:hover .rmbtn { border-color: var(--border); }
  .row .rmbtn:hover { background: var(--danger-surface-dim); border-color: var(--danger-surface); color: var(--danger); }
  .row .confirm { flex: 0 0 auto; font-size: 11px; color: var(--warning); }
  .row .confirm button {
    font: inherit; font-size: 11px; cursor: pointer; margin-left: 6px;
    border-radius: 4px; padding: 1px 7px;
  }
  .row .confirm .yes {
    background: var(--danger-surface); color: var(--danger-text); border: 1px solid var(--danger-border);
  }
  .row .confirm .no {
    background: transparent; color: var(--text-muted); border: 1px solid var(--border);
  }
  .row .dot {
    width: 7px; height: 7px; border-radius: 50%;
    background: var(--border); flex: 0 0 auto;
  }
  .row.active .dot { background: var(--success); }
  .empty { padding: 40px 18px; color: var(--text-muted); }
  .empty code {
    display: block; margin-top: 10px; padding: 10px;
    background: var(--surface-base); border-radius: 6px; font-size: 12px;
    color: var(--text-secondary); user-select: text; word-break: break-all;
  }
  /* The <footer> pair that used to live here is GONE, 2026-09-14.

     One carried the status and one the engine readout. Both sat below the
     instrument list at 37pt each, and the status one rendered the SAME string
     the strip was already showing -- "Nothing loaded yet -- pick an
     instrument." twice on screen at once. Measured in GarageBand's 736x242
     (C39): 74pt of the two of them against 20pt for the list itself.

     Both moved into the strip's existing 16px row, which was already there and
     usually idle. The duplication was mine, from the setStatus mirroring added
     for strip mode and never looked at on a short viewport. */

  /* ── The chrome strip ──────────────────────────────────────────────────────

     This replaces ChromeBar, which was hand-rolled JUCE buttons and looked it.

     It is WP_UNIFIED_CHROME section 4's unconsidered third option: not
     injected into synth.is, and not a bar maintained in two documents. The
     nav lives in THIS document, which never navigates, and the site fills the
     rectangle beneath it. The A3.2 probe measured that two WKWebViews do
     composite simultaneously in an AUv3, so the risk was already retired.

     The BAR is kStripHeight tall. The ELEMENT is taller than that by the
     status bar, which is why C++ cannot hold the number alone -- see below. */
  nav#chrome {
    /* MEASURED 2026-09-11. Under the default viewport-fit the layout viewport
       came back 62pt shorter than the webview -- innerHeight 812 against
       editor_resized h=874 -- with navTop 0 and scrollTop 0. So the VIEWPORT
       was inset; the content was not scrolled. The bar sat 62pt down inside a
       50pt strip and was clipped out of it entirely.

       viewport-fit=cover hands back the full viewport and makes the inset
       readable here instead. The bar keeps kStripHeight and the status bar
       becomes padding above it -- which is what the shrunken viewport was doing
       for free in library mode, and what it could not do in strip mode.

       So this element's height varies with the device AND with the host.
       BOTH ENDS ARE NOW MEASURED, 2026-09-11: 62pt of inset in the standalone
       on an iPhone 17 Pro simulator, and 0 in the AUv3 under AUM on an iPhone
       13 Pro Max -- `navPadTop "0px"`, `navCssH "51px"`, `envTop 0`, against a
       page report of 51. A host-framed view does not touch the top of the
       screen, so there is nothing to inset, which is what the reasoning said
       and is no longer only what it said. The page reports offsetHeight
       through chromeHeight() and the editor sizes the strip to what it is
       told.

       51 rather than 50 because of the border-bottom. Worth knowing before
       someone reads 51 as an off-by-one. */
    flex: 0 0 auto;
    /* AUTO HEIGHT, MEASURED AFTER LAYOUT rather than computed up front.

       CORRECTION, 2026-09-11. This said "no env() inside calc(), and that is
       measured rather than stylistic", on the evidence that the page reported
       its nav as 50 tall while an inline env() read 62 in the same document. I
       blamed calc. That was WRONG: the next reading added getComputedStyle and
       showed the stylesheet resolving env perfectly -- paddingTop 62px, height
       113px. The real cause was timing. env(safe-area-inset-top) is 0 at first
       layout and becomes 62 shortly after, so every report fired before the
       inset existed, and the inset arriving does not raise a window resize.
       See the ResizeObserver comment in the script below.

       The auto height stays, because it is the better shape for a different
       reason than the one I gave: the rows below carry their own heights, so
       the total falls out of layout and the number the page reports is what it
       actually LAID OUT rather than what it was told to be. But calc was never
       at fault and this comment claimed it was. */
    height: auto;
    padding-top: env(safe-area-inset-top, 0px);
    display: flex; flex-direction: column;
    background: var(--surface-base);
    border-bottom: 1px solid var(--border-subtle);

    /* NO DOUBLE-TAP ZOOM ON THE CHROME.

       Reproduced 2026-09-14 in AUM, on device, by double-tapping the LOGO --
       and only there, after several attempts elsewhere. That fits: double-tap
       zoom targets the tapped element's block and scales it to the viewport
       width, so a ~13pt image asks for an enormous factor, while the text
       buttons around it barely move. Once zoomed, the visual viewport pans
       independently of CSS overflow and the bar can be dragged out of its own
       strip.

       `manipulation` kills double-tap zoom and keeps pinch. Deliberately NOT
       `user-scalable=no` on the viewport meta: that would disable zoom for the
       whole document, and in library mode this document is the instrument
       list, which is content someone may legitimately want to magnify -- more
       so in a 242pt letterbox, not less. Scoped to the chrome, which is the
       only part that was being zoomed by accident.

       Pinch-zooming the bar is still possible. That is a deliberate gesture,
       not a stray tap, and left alone. UNVERIFIED HERE: a double-tap cannot be
       synthesised through the simulator tooling, so this was fixed from a
       reproduction on Björn's device and not watched working. */
    touch-action: manipulation;
    user-select: none;
    -webkit-user-select: none;
  }
  /* 34, which with the 16px status line below is kStripHeight. Explicit rather
     than `flex: 1 1 auto`: the nav's height is auto now, so a row that sized
     itself from its parent and a parent that sized itself from its rows would
     have nothing to stand on. */
  nav#chrome .tabs {
    flex: 0 0 auto; min-width: 0; height: calc(/*STRIPH*/px - 16px);
    display: flex; align-items: center; gap: 2px; padding: 0 6px;
  }
  /* The real mark, which is the visible half of why this moved into HTML.

     Ruled off from the tabs rather than sitting flush against them: the S is
     four horizontal bars and lucide's `library` is four vertical ones, and at
     18px side by side they read as two icons in a row instead of a mark and a
     tab. */
  nav#chrome .brand {
    flex: 0 0 auto; display: flex; align-items: center;
    height: 100%; padding: 0 6px 0 3px; margin-right: 7px;
    border-right: 1px solid var(--border-subtle);
  }
  nav#chrome .brand img { height: 18px; width: auto; display: block; }

  nav#chrome button {
    flex: 0 1 auto; min-width: 0;
    display: flex; align-items: center; gap: 6px;
    background: transparent; color: var(--text-secondary);
    border: 1px solid transparent; border-radius: 6px;
    padding: 4px 9px; font: inherit; font-size: 12.5px; font-weight: 500;
    cursor: pointer; white-space: nowrap;
  }
  nav#chrome button svg { flex: 0 0 auto; width: 15px; height: 15px; }
  nav#chrome button:hover {
    background: var(--surface-raised); color: var(--text-primary);
  }
  /* The web app's own current-tab treatment: `bg-blue-600 text-white`, which
     Palette.h records is teal rather than blue. */
  nav#chrome button.current { background: var(--accent); color: var(--on-accent); }

  /* Quiet until it is wanted. This was --success green, which made the one
     button you should rarely press the loudest thing in the bar. It stays
     because it is the only way to stop a drone that ignores its own gate. */
  nav#chrome button.panic {
    margin-left: auto; flex: 0 0 auto;
    color: var(--text-muted); border-color: var(--border-subtle);
  }
  nav#chrome button.panic:hover {
    background: var(--danger-surface-dim); color: var(--danger);
    border-color: var(--danger-border);
  }

  /* The account control. Sits to the right of Panic, after the auto margin, so
     the two ride the right edge together. */
  nav#chrome button.acct {
    flex: 0 0 auto; margin-left: 6px;
    border-color: var(--border-subtle);
  }
  nav#chrome button.acct[hidden] { display: none; }
  /* ANONYMOUS LOOKS DIFFERENT FROM REGISTERED, and that is the whole point.
     Signing out creates an anonymous identity silently and likes attach to it;
     it was indistinguishable from a real account until this rule existed. */
  nav#chrome button.acct.anon { color: var(--warning); border-color: var(--warning); }
  nav#chrome button.acct.none { color: var(--accent); border-color: var(--accent); }
  /* A DOT, NOT A COUNT. The number changes server-side with no navigation to
     carry a sentinel, so it would be stale far more often than right. */
  nav#chrome #acctdot {
    width: 6px; height: 6px; border-radius: 50%;
    background: var(--danger); display: none; flex: 0 0 auto;
  }
  nav#chrome button.acct.unread #acctdot { display: block; }

  /* THE ACTIVITY CAPSULE (synthis-7o3.3). One capsule for everything in flight,
     fed by the activityChanged push plus the getActivity pull. It lives in
     .tabs, so chromeHeight() measures it with the rest of the bar and the
     34px row is unchanged -- the capsule is 22px tall inside it. */
  nav#chrome #capsule {
    flex: 0 1 auto; min-width: 0; display: flex; align-items: center; gap: 5px;
    height: 22px; padding: 0 8px; margin-left: 5px;
    background: var(--surface-panel); border: 1px solid var(--border-subtle);
    border-radius: 11px; color: var(--text-secondary);
    font-size: 11.5px; white-space: nowrap; overflow: hidden;
    max-width: 34%;
    transition: max-width .3s ease, opacity .3s ease, padding .3s ease, margin .3s ease;
  }
  /* display:flex above beats the UA [hidden] rule, so the capsule needs its own */
  nav#chrome #capsule[hidden] { display: none; }
  /* The fold: when a landing has been on screen long enough the capsule
     collapses into the Library tab it sits beside, not vanishes. */
  nav#chrome #capsule.fold {
    max-width: 0; opacity: 0; padding-left: 0; padding-right: 0;
    margin-left: 0; border-width: 0;
  }
  nav#chrome #capsule .ring { flex: 0 0 auto; width: 14px; height: 14px; }
  nav#chrome #capsule .ringbg { fill: none; stroke: var(--border); stroke-width: 2.5; }
  nav#chrome #capsule .ringfg {
    fill: none; stroke: var(--accent); stroke-width: 2.5; stroke-linecap: round;
    stroke-dasharray: 50.27; stroke-dashoffset: 50.27;
  }
  /* Indeterminate (pct unknown, or several jobs at once): a spinning arc. */
  nav#chrome #capsule .ring.indet { animation: capspin .9s linear infinite; }
  nav#chrome #capsule .ring.indet .ringfg { stroke-dasharray: 14 36.27; }
  @keyframes capspin { to { transform: rotate(360deg); } }
  /* Terminal states: the ring stops and the tick or the warning colour is
     the whole of it. */
  nav#chrome #capsule .tick {
    display: none; fill: none; stroke: var(--success);
    stroke-width: 2.5; stroke-linecap: round; stroke-linejoin: round;
  }
  nav#chrome #capsule.landed .tick { display: block; }
  nav#chrome #capsule.landed .ringbg,
  nav#chrome #capsule.landed .ringfg { display: none; }
  nav#chrome #capsule.warn { color: var(--warning); }
  nav#chrome #capsule.warn .ringfg { stroke: var(--warning); }
  nav#chrome #capsule #capsuletext {
    min-width: 0; overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
  }
  nav#chrome #capsule #capsulecount { display: none; }

  /* THE LIBRARY BADGE (synthis-7o3.3). Landed-and-unseen instruments only --
     while jobs are merely in flight the tab carries a ring dot with no digit,
     so one number never means two things. */
  nav#chrome .tab .libbadge {
    flex: 0 0 auto; min-width: 15px; height: 15px; padding: 0 4px;
    border-radius: 8px; background: var(--accent); color: var(--on-accent);
    font-size: 10px; line-height: 15px; text-align: center; font-weight: 600;
  }
  nav#chrome .tab .libbadge[hidden] { display: none; }
  nav#chrome .tab .libbadge.dot {
    min-width: 0; width: 8px; height: 8px; padding: 0;
    background: transparent; border: 2px solid var(--accent);
  }
  /* The landing pulse: one short scale on the tab the capsule folds into. */
  nav#chrome .tab.pulse { animation: tabpulse .6s ease; }
  @keyframes tabpulse {
    0% { transform: scale(1); } 40% { transform: scale(1.14); } 100% { transform: scale(1); }
  }

  /* REDUCED MOTION (synthis-7o3.3): no travel and no pulse -- the static tick
     and the badge are the whole of the landing. */
  @media (prefers-reduced-motion: reduce) {
    nav#chrome #capsule .ring.indet { animation: none; }
    nav#chrome .tab.pulse { animation: none; }
    nav#chrome #capsule { transition: none; }
    nav#chrome #capsule.fold { transition: none; }
  }

  /* NARROW BAR (the same 639px breakpoint that drops the tab labels): the
     capsule shows the ring and the count only. */
  @media (max-width: 639px) {
    nav#chrome #capsule { max-width: none; padding: 0 6px; margin-left: 3px; }
    nav#chrome #capsule #capsuletext { display: none; }
    nav#chrome #capsule #capsulecount { display: inline; }
  }

  /* LABELS DROP AT THE FIRST WIDTH THAT CANNOT HOLD THEM, which is the fix for
     "Instrume…". The native bar had no way to do this: it measured a string
     against a rectangle and truncated, so the longest label decided what every
     tab looked like.

     639px because Tailwind's `sm` is 640 and the web app's rule is
     `hidden sm:inline`. This said 560 when it was written and claimed to mirror
     that rule, which the number did not support. At 402pt both give icon-only,
     so no screenshot could have caught it. */
  @media (max-width: 639px) {
    nav#chrome button .lbl { display: none; }
    nav#chrome button { padding: 4px 8px; }
    nav#chrome .brand { padding: 0 5px 0 2px; margin-right: 5px; }
  }

  /* TWO READOUTS, ONE 16px ROW. Status on the left, engine on the right.

     The 16px was ChromeBar's, reserved because a download STARTS on the site,
     so nothing in the library is on screen when there is something to report.
     That still holds; what changed is that this row is now the only status
     surface rather than a mirror of a footer.

     Status grows and the engine line gives way, because status is event-driven
     and worth reading while the engine readout is continuous and diagnostic.
     Both ellipsise rather than wrap -- the row is a fixed 16px and a second
     line would silently cost the list another row of height. */
  nav#chrome .line {
    flex: 0 0 auto; height: 16px; line-height: 16px; padding: 0 11px;
    display: flex; align-items: center; gap: 10px;
    font-size: 11px; color: var(--text-muted);
  }
  nav#chrome .line span {
    min-width: 0; overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
  }
  nav#chrome #navline { flex: 1 1 auto; }
  /* Shrink factor 8, not the default 1. At the default they gave way in
     proportion to their content and the engine readout is the longer string,
     so the STATUS was truncated just as hard.

     MEASURED, and honestly: at 402pt the visible difference is almost nil,
     because the overflow is only two or three characters and there is nothing
     much to reallocate. It matters as the disparity grows -- a long
     acquisition message against a long engine line -- and at 736pt in the
     letterbox both fit outright. Kept because it is right, not because it was
     seen to work.

     Deliberately NOT hidden below some width: the engine readout is how the
     load figure gets read on the phone, which is the device it is most needed
     on. */
  nav#chrome #navload { flex: 0 8 auto; margin-left: auto; opacity: 0.72; }
  nav#chrome #navline.warn { color: var(--warning); }
  nav#chrome #navline.err { color: var(--danger); }

  /* STRIP MODE — the site is up and owns everything below the bar.

     The rest of the document is REMOVED, not clipped. A 50px viewport showing
     the top of a taller document is still scrollable, and one stray gesture
     would drag the instrument list up into the chrome. */
  body.strip { height: auto; overflow: hidden; }
  body.strip > :not(nav#chrome) { display: none; }

  /* ── Library sub-tabs ──────────────────────────────────────────────── */

  /* STRIP MODE HIDES THEM, and it takes an explicit rule to do it.
     body.strip hides `> :not(nav#chrome)`, and these live INSIDE nav#chrome --
     the one element strip mode keeps, because the chrome is in that document.
     So the sub-tabs stayed on screen while the site owned the body, offering to
     switch between two halves of a Library that was not showing. Björn saw them
     over the Feed.
     The comment beside the markup claimed they were "hidden in strip mode with
     everything else". They were not. Fixed here rather than by editing the
     claim, because the claim was the right intent.
     nav#chrome is observed on its BORDER box, so losing this row changes the
     reported chrome height on its own and C++ resizes the strip to match. */
  body.strip nav#chrome .subtabs { display: none; }

  nav#chrome .subtabs {
    display: flex; gap: 4px; padding: 4px 8px 6px;
    border-top: 1px solid var(--border-subtle);
  }
  nav#chrome .subtabs .sub {
    display: flex; align-items: center; gap: 5px;
    background: transparent; color: var(--text-muted);
    border: 1px solid transparent; border-radius: 6px;
    padding: 3px 10px; font-size: 12px; cursor: pointer;
  }
  nav#chrome .subtabs .sub:hover { color: var(--text-body); }
  nav#chrome .subtabs .sub.current {
    color: var(--text-primary);
    background: var(--accent-surface);
    border-color: var(--accent);
  }
  nav#chrome .subtabs .badge {
    font-size: 10px; color: var(--text-muted);
    background: var(--border-subtle); border-radius: 8px;
    padding: 0 5px; min-width: 14px; text-align: center;
  }
  nav#chrome .subtabs .sub.current .badge { color: var(--text-body); }
  /* Empty badge takes no room, so "Sounds" does not sit beside a stray pill
     before the first take. */
  nav#chrome .subtabs .badge:empty { display: none; }

  /* Which half of the Library is on screen. One class on <body> rather than
     toggling four elements, so there is one place the state lives. */
  body.sounds header,
  body.sounds .toolbar,
  body.sounds #list,
  body.sounds #spectro { display: none; }
  body:not(.sounds) #rendersec { display: none; }

  /* THE CONTROLS VIEW, the third sub-tab (synthis-cda.3). Same treatment as
     Sounds: the library halves hide and the controls section takes the scroll
     region. #controlsec is a direct child of <body>, so strip mode's
     `> :not(nav#chrome)` hides it along with everything else, and the
     sub-tab row stays inside nav#chrome where chromeHeight() measures it. */
  body.controls header,
  body.controls .toolbar,
  body.controls #list,
  body.controls #spectro,
  body.controls #rendersec { display: none; }
  body:not(.controls) #controlsec { display: none; }

  /* MAPPING CONTROLS ARE STANDALONE-ONLY. In a DAW the host owns MIDI input,
     so the chip, Learn, clear, the range editor and the input monitor have
     nothing to act on; the sliders stay and read the values the host moves.
     The class comes from getOscState().receiverExists, which the native
     bridge reports true only in the Standalone wrapper. */
  body:not(.standalone) .sa-only { display: none; }

  #controlsec { flex: 1 1 auto; overflow-y: auto; padding: 8px 18px 12px; }
  #controlsec h2 {
    margin: 14px 4px 6px; font-size: 12px; font-weight: 600;
    text-transform: uppercase; letter-spacing: .06em; color: var(--text-muted);
  }
  .crow {
    padding: 7px 10px; border-radius: 7px; margin-bottom: 4px;
    background: var(--surface-panel); border: 1px solid var(--border-subtle);
  }
  .crow .ctop { display: flex; align-items: center; gap: 8px; }
  .crow .cname {
    flex: 0 0 88px; min-width: 0; font-size: 12.5px;
    overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
  }
  .crow .cslider { flex: 1 1 auto; min-width: 48px; accent-color: var(--accent); }
  .crow .cslider:disabled { opacity: 1; cursor: default; }
  .crow .cval {
    flex: 0 0 42px; text-align: right; font-size: 11.5px;
    font-variant-numeric: tabular-nums; color: var(--text-muted);
  }
  .crow .cchip {
    flex: 0 1 auto; min-width: 0; max-width: 132px;
    overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
    font-size: 10.5px; padding: 1px 7px; border-radius: 8px;
    background: var(--accent-surface); border: 1px solid var(--accent);
    color: var(--text-body);
  }
  .crow .cchip.unmapped {
    background: transparent; border-color: var(--border-subtle); color: var(--text-muted);
  }
  .crow button {
    flex: 0 0 auto; font: inherit; font-size: 11px; cursor: pointer;
    background: transparent; color: var(--text-muted);
    border: 1px solid var(--border); border-radius: 5px; padding: 2px 8px;
  }
  .crow button:hover { background: var(--border-subtle); color: var(--text-primary); }
  .crow button.learn.on { color: var(--accent); border-color: var(--accent); }
  .crow .cedit {
    display: flex; flex-wrap: wrap; align-items: center; gap: 8px;
    padding: 7px 2px 2px;
  }
  /* the author display:flex above beats the UA [hidden] rule, so the
     collapsed editor needs its own rule */
  .crow .cedit[hidden] { display: none; }
  .crow .cedit label {
    display: flex; align-items: center; gap: 4px;
    font-size: 11px; color: var(--text-muted);
  }
  .crow .cedit input[type="number"] {
    width: 58px; background: var(--surface-deep); color: var(--text-primary);
    border: 1px solid var(--border); border-radius: 5px; padding: 2px 6px;
    font: inherit; font-size: 11px;
  }
  .crow .cedit input[type="range"] { width: 90px; accent-color: var(--accent); }
  .crow .cedit .apply { background: var(--border-subtle); color: var(--text-primary); }
  .monline {
    margin-top: 10px; padding: 7px 10px; border-radius: 7px;
    border: 1px solid var(--border-subtle); background: var(--surface-panel);
    font-size: 11.5px; color: var(--text-muted);
    overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
  }
</style>
</head>
<body>

<!-- The chrome. First child of <body> and the only element strip mode keeps.

     `data-path` empty means the PLUGIN'S OWN LIBRARY, which is not a site page
     -- the same reason ChromeBar called it "Library" and not "Instruments":
     the web app's /instruments is the account's rendered instruments, this is
     what is on the device. Labels and order are ChromeBar's, unchanged, so this
     is a like-for-like replacement and can be compared as one.

     /instruments over /instrument even though the router redirects: it is what
     NavigationBar.jsx:93 lists, and matching that file is the invariant. The
     redirect costs one navigation.

     Icons are lucide 0.454.0's own path data, lifted from the same package the
     web app imports (ISC), with lucide's default attributes. Nothing is
     fetched -- see kLogoSvg on why that matters here. -->
<nav id="chrome">
  <div class="tabs">
    <span class="brand"><img src="logo.svg" alt="Synth.is"></span>

    <button class="tab current" data-dest="0" id="libtab" data-path="" title="Instruments downloaded to this device">
      <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="m16 6 4 14"/><path d="M12 6v14"/><path d="M8 8v12"/><path d="M4 4v16"/></svg>
      <span class="lbl">Library</span>
      <!-- Landed-and-unseen count (synthis-7o3.3): the number while something
           has landed unseen, a ring dot while jobs are merely in flight,
           cleared when this tab is opened. -->
      <span class="libbadge" id="libbadge" hidden></span>
    </button>

    <button class="tab" data-dest="1" data-path="/network" title="Discover sounds from the community">
      <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="m16.24 7.76-1.804 5.411a2 2 0 0 1-1.265 1.265L7.76 16.24l1.804-5.411a2 2 0 0 1 1.265-1.265z"/><circle cx="12" cy="12" r="10"/></svg>
      <span class="lbl">Feed</span>
    </button>

    <button class="tab" data-dest="2" data-path="/collection" title="Your liked sounds collection">
      <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M19 14c1.49-1.46 3-3.21 3-5.5A5.5 5.5 0 0 0 16.5 3c-1.76 0-3 .5-4.5 2-1.5-1.5-2.74-2-4.5-2A5.5 5.5 0 0 0 2 8.5c0 2.3 1.5 4.05 3 5.5l7 7Z"/></svg>
      <span class="lbl">Garden</span>
    </button>

    <!-- Grow sits where NavigationBar.jsx lists it, between Garden and
         Instruments. data-dest 5 rather than renumbering: dest is only the
         identity setStrip marks current, not a position. -->
    <button class="tab" data-dest="5" data-path="/grow" title="Grow new sounds from a prompt or a place on the sound map">
      <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M7 20h10"/><path d="M10 20c5.5-2.5.8-6.4 3-10"/><path d="M9.5 9.4c1.1.8 1.8 2.2 2.3 3.7-2 .4-3.5.4-4.8-.3-1.2-.6-2.3-1.9-3-4.2 2.8-.5 4.4 0 5.5.8z"/><path d="M14.1 6a7 7 0 0 0-1.1 4c1.9-.1 3.3-.6 4.3-1.4 1-1 1.6-2.3 1.7-4.6-2.7.1-4 1-4.9 2z"/></svg>
      <span class="lbl">Grow</span>
    </button>

    <button class="tab" data-dest="3" data-path="/instruments" title="Download rendered virtual instruments">
      <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M11 21.73a2 2 0 0 0 2 0l7-4A2 2 0 0 0 21 16V8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73z"/><path d="M12 22V12"/><path d="m3.3 7 7.703 4.734a2 2 0 0 0 1.994 0L20.7 7"/><path d="m7.5 4.27 9 5.15"/></svg>
      <span class="lbl">Instruments</span>
    </button>

    <button class="tab" data-dest="4" data-path="/tree" title="Explore evolutionary sound lineages">
      <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="m17 14 3 3.3a1 1 0 0 1-.7 1.7H4.7a1 1 0 0 1-.7-1.7L7 14h-.3a1 1 0 0 1-.7-1.7L9 9h-.2A1 1 0 0 1 8 7.3L12 3l4 4.3a1 1 0 0 1-.8 1.7H15l3 3.3a1 1 0 0 1-.7 1.7H17Z"/><path d="M12 22v-3"/></svg>
      <span class="lbl">Biomes</span>
    </button>

    <!-- THE ACTIVITY CAPSULE (synthis-7o3.3). One capsule for everything in
         flight, beside the tabs and before Panic, inside .tabs so
         chromeHeight() measures it with the rest of the bar. Ring plus text
         while work is active ("Rendering 2"), "Added" briefly on a landing,
         hidden when idle; in a narrow bar the ring and the count only. The
         tooltip lists the jobs oldest first. -->
    <div id="capsule" hidden>
      <svg class="ring" id="capring" viewBox="0 0 20 20" aria-hidden="true">
        <circle class="ringbg" cx="10" cy="10" r="8"></circle>
        <circle class="ringfg" id="ringfg" cx="10" cy="10" r="8" transform="rotate(-90 10 10)"></circle>
        <path class="tick" d="M6 10.6l2.6 2.6 5.4-5.8"></path>
      </svg>
      <span id="capsuletext"></span>
      <span id="capsulecount"></span>
    </div>

    <!-- id="panic" moved HERE from the header rather than added alongside it.
         The header's copy was unreachable the moment the site filled the view,
         which is why ChromeBar drew a second one -- and two Panic buttons 30px
         apart is the duplication that made the old bar look hand-rolled. One
         button, and the existing handler finds it unchanged. -->
    <button class="panic" id="panic" title="Reinitialise all voices">Panic</button>

    <!-- WHO YOU ARE, in the bar rather than in a band.

         The site's own user section cost 74pt of VERTICAL for two controls,
         measured on device -- more than this entire bar, and comparable to the
         whole instrument list in GarageBand's 242pt letterbox. Horizontal is
         the abundant axis here and vertical is the scarce one, so this moves
         across axes rather than adding.

         Hidden until the first auth sentinel arrives. An account control that
         appears only once the site has spoken is honest about not knowing;
         one that renders "Sign in" on a cold start is guessing. -->
    <button class="acct" id="acct" hidden><span id="acctdot"></span><span id="acctlbl"></span></button>
  </div>
  <div class="line"><span id="navline"></span><span id="navload"></span></div>

  <!-- SUB-TABS, inside the Library. Instruments are what you play from MIDI;
       sounds are what you drag into a timeline. Different actions, so they are
       siblings here rather than one interleaved list where alternating rows
       behave completely differently.

       INSIDE nav#chrome deliberately: chromeHeight() measures that element's
       bounding rect, so a row added here is accounted for with no change to
       the reporting at either end. Putting it below the nav would have meant
       teaching C++ about a second measured element.

       NAMING: "Instruments" also names a TOP-LEVEL tab, which is the site's
       /instruments -- the account's rendered instruments, not what is on this
       device. The collision is real and the site tab is the one to reconsider;
       these two are named for what they are inside a library.

       Hidden in strip mode by an explicit rule in the stylesheet -- NOT by
       body.strip's `> :not(nav#chrome)`, which keeps this element along with
       the rest of the chrome. See the rule for what that cost. -->
  <div class="subtabs">
    <button class="sub current" data-view="instruments">Instruments</button>
    <button class="sub" data-view="sounds">Sounds<span class="badge" id="subcount"></span></button>
    <!-- Controls (synthis-cda.3): the loaded instrument's sliders and the
         MIDI/OSC mapping. Same row as the other two, so it sits inside
         nav#chrome and chromeHeight() accounts for it unchanged. -->
    <button class="sub" data-view="controls">Controls</button>
  </div>
</nav>

<header>
  <h1>Instruments</h1>
  <span class="count" id="count"></span>
  <button id="refresh">Refresh</button>
  <button id="testnote">Test note</button>
</header>
<div class="toolbar">
  <input id="filter" type="search" autocomplete="off"
         placeholder="Filter by name, type or character&hellip;">
  <select id="sort">
    <option value="added">Recently added</option>
    <option value="discovered">Recently discovered</option>
    <option value="adopted">Recently kept</option>
    <option value="name">Name</option>
    <option value="type">Type</option>
    <option value="bright">Brightest first</option>
  </select>
</div>
<main id="listwrap">
  <canvas id="spectro" class="empty"></canvas>

  <!-- SOUNDS YOU HAVE TAKEN, above the instruments rather than below.

       Björn, after using the native strip: "I also wonder about if this is the
       best UI or UX presentation to have this persistent bottom bar with
       swatches you, for example, cannot preview just with some IDs, maybe not
       too user friendly." Right on both counts -- the chips were labelled with
       an entry-id fragment and there was no way to hear one without dragging it
       into a DAW first.

       Above, because it is the shorter list and the one that changes: fifty-three
       instruments below it would push it off the top of a GarageBand-height
       window, and this is the section that gains a row while you watch.

       Hidden entirely until the first take, so it costs no height for anyone
       who has not used the feature. -->
  <section id="rendersec" hidden>
    <!-- No heading and no count: the sub-tab above reads "Sounds 4", and a
         second "SOUNDS YOU HAVE TAKEN  4 sounds" directly beneath it said the
         same thing twice. The row survives to carry Reveal folder, which is
         the escape hatch that works whatever the list and the drag do. -->
    <div class="subhead">
      <button id="rreveal">Reveal folder</button>
    </div>
    <div id="rlist"></div>
  </section>

  <div id="list"></div>
</main>

<!-- THE CONTROLS PANEL (synthis-cda.3). A direct child of <body> so strip
     mode hides it with everything below the chrome. One row per mapping
     target the native bridge reports: the sixteen macro slots in #cclist,
     then attack / decay / sustain / release / output under their own heading
     in #envlist. The input monitor and the OSC port/IP line are sa-only:
     they show only when the bridge reports the Standalone wrapper. -->
<section id="controlsec">
  <div class="empty sa-only" id="controlsempty">Press <b>Learn</b> on a row, then move a MIDI controller or send an OSC message, and that input will be mapped to the slider.</div>
  <div id="cclist"></div>
  <h2 id="envhead">Envelope &amp; output</h2>
  <div id="envlist"></div>
  <div class="monline sa-only" id="ccmonitor"></div>
  <div class="monline sa-only" id="oscline"></div>
</section>

<script>
// The bridge. JUCE injects window.__JUCE__.backend when native integration is
// enabled; if it is missing we are being viewed somewhere it is not, and
// saying so plainly beats a page that silently does nothing.
const backend = window.__JUCE__ && window.__JUCE__.backend;

// The invoke protocol, implemented against JUCE's own frontend source rather
// than guessed. The first version of this called backend.callNativeFunction(),
// which does not exist -- the shell rendered, the bridge was present, and every
// call threw "callNativeFunction is not a function".
//
// The real contract (juce_gui_extra/native/typescript/webview-interop/src/
// index.ts:81-99) is an event pair:
//
//   emit  '__juce__invoke'   { name, params, resultId }
//   listen '__juce__complete' { promiseId, result }
//
// JUCE ships a helper module that wraps this, but importing it would mean a
// bundler and a node_modules for one function. Reimplemented here, in about
// fifteen lines, with the source reference above so the next person can check
// it against upstream rather than trusting this comment.
let nextPromiseId = 0;
const pending = new Map();

if (backend) {
  backend.addEventListener('__juce__complete', function (payload) {
    const entry = pending.get(payload.promiseId);
    if (entry) {
      entry(payload.result);
      pending.delete(payload.promiseId);
    }
  });
}

function setStatus (text, kind) {
  // THE STRIP IS THE ONLY STATUS SURFACE.
  //
  // This also wrote to a <footer> until 2026-09-14, and both were visible in
  // library mode -- the same string rendered twice, the footer copy costing
  // 37pt of a 242pt letterbox. The mirroring was added for strip mode, where
  // the footer is off-screen and the bar is the only place a download can be
  // reported, and it was never looked at on a short viewport.
  const line = document.getElementById('navline');
  if (!line) return;

  line.textContent = text;
  line.className = kind || '';
}

function call (name, ...args) {
  if (!backend) return Promise.reject(new Error('native bridge unavailable'));

  return new Promise(function (resolve) {
    const id = nextPromiseId++;
    pending.set(id, resolve);
    backend.emitEvent('__juce__invoke', {
      name: name, params: args, resultId: id
    });
  });
}

let entries = [];
let currentId = null;
let sortBy = 'added';
let filterText = '';

// Ids that appeared in the list since the last poll, and the one to scroll to.
//
// A sound acquired from the site lands wherever the CURRENT sort puts it, which
// is rarely the top: sorted by type or by name it can arrive in the middle of
// twenty entries with nothing to distinguish it, and the user who just added it
// has to hunt. Changing the sort for them would be worse -- it throws away the
// order they chose.
//
// So the new arrival is marked and scrolled to instead. That works under every
// ordering, including whatever was restored from a previous session.
let knownIds = null;      // null until the first successful list, so a cold
                          // start does not mark all 21 instruments as new
let newIds = new Set();
let scrollToId = null;

// ── ordering and description ────────────────────────────────────────────────
//
// listInstruments hands over synthis.json's own shape -- source, character,
// measured -- so everything below is a read. Before this the list was whatever
// order the filesystem returned scan()'s directories in, which is why it looked
// random.
//
// THE HARD PART IS ABSENCE. Instruments that predate the artefact contract,
// ones placed by hand, and renders of unadopted feed candidates carry almost
// none of this; and of the Faust-substrate sounds in the archive only 73 of 231
// have descriptors at all. So every field here is optional and unset sorts
// LAST under every ordering -- a missing discovery date is not the epoch, and
// must not lead a list sorted by discovery.

const DESCENDING = { added: 1, discovered: 1, adopted: 1, bright: 1 };

function timeOf (iso) {
  const t = iso ? Date.parse(iso) : NaN;
  return isNaN(t) ? null : t;
}

function sortKey (e) {
  const src = e.source || {}, chr = e.character || {};

  switch (sortBy) {
    case 'name': return (e.name || '').toLowerCase();
    case 'discovered': return timeOf(src.discoveredAt);
    case 'adopted': return timeOf(src.adoptedAt);
    case 'type': return chr.soundType || null;
    // Ranked against THIS LIBRARY rather than against a corpus. The labels are
    // P33/P67 across the whole archive and shift when it is relabelled; the raw
    // vector does not, and "the brightest of the forty I have" is the question
    // actually being asked here. Index 0 is brightness in DESCRIPTOR_DIMENSIONS
    // order.
    case 'bright': {
      const d = chr.descriptors;
      return (d && d.length === 7) ? d[0] : null;
    }
    default: return timeOf(src.acquiredAt);
  }
}

function compareEntries (a, b) {
  const ka = sortKey(a), kb = sortKey(b);
  const missingA = (ka === null || ka === undefined);
  const missingB = (kb === null || kb === undefined);

  if (missingA && missingB) return (a.name || '').localeCompare(b.name || '');
  if (missingA) return 1;
  if (missingB) return -1;
  if (ka === kb) return (a.name || '').localeCompare(b.name || '');

  const ascending = ka < kb ? -1 : 1;
  return DESCENDING[sortBy] ? -ascending : ascending;
}

// Everything worth typing at the box, including the labels -- which are not
// shown on the row (seven of them would not fit) but are exactly what someone
// means when they type "bright" or "noisy".
function searchText (e) {
  const chr = e.character || {}, src = e.source || {}, msd = e.measured || {};
  const labels = chr.labels ? Object.keys(chr.labels).map(function (k) {
    return chr.labels[k];
  }) : [];

  return [e.name, e.kind, chr.soundType, msd.articulation, src.class]
    .concat(labels).filter(Boolean).join(' ').toLowerCase();
}

function ago (iso) {
  const t = timeOf(iso);
  if (t === null) return null;

  const days = Math.floor((Date.now() - t) / 86400000);
  if (days <= 0) return 'today';
  if (days === 1) return 'yesterday';
  if (days < 30) return days + 'd ago';
  if (days < 365) return Math.round(days / 30) + 'mo ago';
  return Math.round(days / 365) + 'y ago';
}

// The date shown follows the SORT, so the column being ordered by is the one
// that can be read. Sorting by discovery and showing the acquisition date would
// make the order look arbitrary all over again.
function datePart (e) {
  const src = e.source || {};

  if (sortBy === 'discovered') { const a = ago(src.discoveredAt); return a && 'found ' + a; }
  if (sortBy === 'adopted')    { const a = ago(src.adoptedAt);    return a && 'kept ' + a; }

  const a = ago(src.acquiredAt);
  return a && 'added ' + a;
}

function describe (e) {
  const chr = e.character || {}, msd = e.measured || {};
  const bits = [];

  if (chr.soundType) bits.push({ text: chr.soundType });

  // THREE of the seven descriptor labels, chosen by measurement rather than by
  // taste. Over 889 labelled sounds the seven dimensions are far from
  // independent -- brightness, noisiness, rolloff and width correlate at 0.81
  // to 0.92, so they are one spectral axis read four ways. Printing all four
  // says the same thing four times and crowds out everything else.
  //
  // energy (-0.27 against brightness) and richness (-0.09) are the two that
  // carry information the first axis does not. Percussive sits in between,
  // correlating -0.61 with energy, so it is left to the filter rather than the
  // row.
  //
  // All seven remain searchable -- see searchText -- so nothing is lost, only
  // unprinted.
  //
  // These are CORPUS-RELATIVE: DescriptorLabelingService cuts at P33/P67 over
  // the whole archive, so "bright" is a claim about where this sound sits among
  // the others, and it moves when the archive grows. soundType above is the
  // only absolute claim in this row, which is also why it is nearly useless on
  // its own: 76% of the corpus is "textural".
  if (chr.labels) {
    ['brightness', 'energy', 'richness'].forEach(function (dim) {
      if (chr.labels[dim]) bits.push({ text: chr.labels[dim] });
    });
  }

  // A drone is a fine pad and a poor MIDI instrument, and it is worth knowing
  // before playing one rather than after.
  //
  // ALL THREE spelled out, not just drone. This used to expand 'drone' and
  // print the other two as raw enum values, and 'silent' reads to a user as
  // "this instrument makes no sound" when it means very nearly the opposite:
  // the voice stops CLEANLY when the key is released, which is the
  // best-behaved of the three. On 2026-09-09 three perfectly good instruments
  // were reported as not responding to notes for exactly that reason, and
  // their measurements showed healthy output at all three probe pitches.
  //
  // The wording is faustArtefact.js's, verbatim. That file's text summary
  // already said all three correctly; only this badge did not. Two vocabularies
  // for one classification is how they drift apart.
  //
  // The values are set in kromosynth-render/render-socket/src/faust-artefact.js
  // from the tail level after note-off: tailRel < 0.001 is silent, < 0.032 is
  // quiet, anything more is a drone. The final fallback prints an unrecognised
  // value as-is rather than guessing a meaning for it.
  if (msd.articulation) {
    bits.push(
      msd.articulation === 'drone'  ? { text: 'drone \u2014 sustains after note-off', warn: true }
    : msd.articulation === 'silent' ? { text: 'stops cleanly on note-off' }
    : msd.articulation === 'quiet'  ? { text: 'decays quickly after note-off' }
    : { text: msd.articulation });
  }

  // Only when the artefact says so. hasGate is a tri-state here: true, false,
  // and "the artefact never said", and the third must not read as the second.
  if (msd.hasGate === false) bits.push({ text: 'no gate', warn: true });

  // "est." because this is NOT this device's number and must not read as one.
  // capAt4x here comes from the artefact's sidecar, derived from an rtf measured
  // on the render server's V8. faustArtefact.js states the problem exactly: "the
  // V8/wasmtime ratio ranges 0.48-0.99 depending on the genome, so there is no
  // factor to convert it", and its own text summary calls the figure
  // "Indicative polyphony" and adds "Your machine will differ; the plugin
  // measures its own."
  //
  // This badge said "8 voices" for a genome the phone measured at 2, with
  // nothing to say the two numbers came from different machines. The live cap
  // is shown in the status line under the list, where it belongs -- it exists
  // only for the loaded instrument, so it cannot be shown per row.
  if (msd.capAt4x > 0) bits.push({ text: 'est. ' + msd.capAt4x + ' voices' });

  // Regions, for sampled instruments only. It used to sit in the kind column,
  // which is now the one place both engines say something comparable, so the
  // count belongs here with the other measured facts. It is also the honest
  // analogue of a voice cap: how many rendered samples the instrument is
  // actually made of.
  if (e.kind !== 'faust' && e.regions > 0) bits.push({ text: e.regions + ' regions' });

  const when = datePart(e);
  if (when) bits.push({ text: when });

  // Which of its siblings this is. Two live instruments from one sound are
  // identical and the useful action is removing one; two sample sets from one
  // sound differ by how they were rendered and the useful action is choosing
  // between them. Either way the first thing to know is that there IS another.
  if (e._sibling) {
    const r = (e.source && e.source.render) || null;

    // Named by what DIFFERS where the artefact says so, and by position where
    // it does not. A live instrument extracted twice has nothing to tell apart
    // -- same genome, same everything -- so "1 of 2" is the whole truth there.
    const how = r
      ? [r.duration != null ? r.duration + 's' : null,
         r.pitchRange ? r.pitchRange.low + '\u2013' + r.pitchRange.high : null,
         r.velocityLayers ? r.velocityLayers + ' vel' : null].filter(Boolean).join(' \u00b7 ')
      : '';

    bits.push({ text: e._sibling.index + ' of ' + e._sibling.of + ' from this sound'
                      + (how ? ' \u2014 ' + how : '') });
  }

  return bits;
}

function soundIdOf (e) {
  // SINGULAR. The sidecar stores `source.soundIds` as an array, but
  // InstrumentLibrary::readSidecar takes its first element into Metadata as
  // `soundId` and that is what reaches this page. Reading the array name here
  // returned undefined for every instrument, so nothing ever grouped -- and
  // silently, because "no siblings" looks exactly like "no duplicates".
  //
  // The array form is still accepted in case a future payload passes the
  // sidecar through unflattened.
  const src = e.source || {};
  if (src.soundId) return src.soundId;
  return (src.soundIds && src.soundIds.length) ? src.soundIds[0] : null;
}

/**
 * Keeps instruments made from the SAME SOUND next to each other, without
 * changing where the group as a whole lands.
 *
 * Two things produce siblings. Extracting a sound twice mints a fresh
 * artifactId each time, so the library ends up with two identical live
 * instruments under unrelated names. And a sample set can be rendered from one
 * sound at different durations, pitch ranges or velocity layers, which are
 * genuinely different instruments worth keeping side by side.
 *
 * Deliberately NOT a nested tree. The chosen sort still decides the order, and
 * a group simply follows its first member -- so sorting by name or by type
 * behaves as before and siblings merely stop being scattered. A real grouped
 * view belongs with the larger interface change, not ahead of it.
 *
 * Instruments with no soundId pass through untouched. Every sample instrument
 * installed before the sidecar fix is in that position: the run id and sound id
 * were never written into the artefact, so they cannot be grouped at all.
 */
function groupBySound (list) {
  const groups = new Map();
  const out = [];

  list.forEach(function (e) {
    const sid = soundIdOf(e);

    if (!sid) { out.push([e]); return; }

    if (groups.has(sid)) { groups.get(sid).push(e); return; }

    const g = [e];
    groups.set(sid, g);
    out.push(g);
  });

  // Sibling position stamped here, where the group is still intact. The rows
  // are rendered from a flat list and would otherwise have to rediscover it.
  const flat = [];
  out.forEach(function (g) {
    g.forEach(function (e, i) {
      e._sibling = g.length > 1 ? { index: i + 1, of: g.length } : null;
      flat.push(e);
    });
  });

  return flat;
}

function render () {
  const list = document.getElementById('list');

  if (entries.length === 0) {
    document.getElementById('count').textContent = '0 instruments';
    list.innerHTML =
      '<div class="empty">No instruments in the local library yet.' +
      '<code id="root"></code></div>';
    call('getLibraryRoot').then(function (p) {
      const el = document.getElementById('root');
      if (el) el.textContent = p;
    }).catch(function () {});
    return;
  }

  const needle = filterText.trim().toLowerCase();
  const matched = needle
    ? entries.filter(function (e) { return searchText(e).indexOf(needle) >= 0; })
    : entries.slice();

  // New arrivals are PINNED ABOVE the chosen ordering rather than sorted into
  // it. Sorted by type or by name a just-acquired instrument can land in the
  // middle of twenty with nothing to distinguish it, and a badge alone still
  // leaves the user scanning for it. Pinning puts it where they look; the rest
  // of the list keeps exactly the order they chose, which is why this is a
  // partition and not a change of sort.
  const fresh = matched.filter(function (e) { return newIds.has(e.id); }).sort(compareEntries);
  const rest = matched.filter(function (e) { return !newIds.has(e.id); }).sort(compareEntries);
  const shown = groupBySound(fresh).concat(groupBySound(rest));

  document.getElementById('count').textContent =
    (needle ? shown.length + ' of ' + entries.length : entries.length)
    + (entries.length === 1 && !needle ? ' instrument' : ' instruments');

  list.innerHTML = '';

  if (shown.length === 0) {
    // Escapes rather than literal curly quotes. Everything user-visible in this
    // page is ASCII plus entities or \u escapes: a literal multi-byte character
    // in the source came back as mojibake in the rendered placeholder, because
    // the page is served from a C++ string over the resource provider and the
    // decoding is not ours to assume.
    list.innerHTML = '<div class="empty">Nothing matches \u201c'
      + needle.replace(/&/g, '&amp;').replace(/</g, '&lt;') + '\u201d.</div>';
    return;
  }

  shown.forEach(function (e) {
    const row = document.createElement('div');
    const fresh = newIds.has(e.id);
    row.className = 'row' + (e.id === currentId ? ' active' : '') + (fresh ? ' fresh' : '');
    row.dataset.id = e.id;

    const top = document.createElement('div');
    top.className = 'top';

    const dot = document.createElement('span');
    dot.className = 'dot';

    const name = document.createElement('span');
    name.className = 'name';
    name.textContent = e.name;

    const meta = document.createElement('span');
    meta.className = 'meta';
    // SYMMETRIC, and generic. This said 'live \u00b7 Faust' for one engine and
    // nothing at all for the other, so a sample instrument's row looked like a
    // failed load rather than a different kind of thing.
    //
    // 'Faust' is dropped deliberately: it names the substrate, which matters to
    // the platform and not to someone choosing an instrument to play. What a
    // player needs to know is whether the sound is synthesised on the fly or
    // read from rendered audio -- that is what decides whether it answers
    // across the whole keyboard, and what the controls will do.
    //
    // The region count moves to the detail line below, with the other measured
    // facts.
    meta.textContent = e.kind === 'faust' ? 'live' : 'sampled';

    top.appendChild(dot);
    top.appendChild(name);

    if (fresh) {
      const badge = document.createElement('span');
      badge.className = 'new';
      badge.textContent = 'new';
      top.appendChild(badge);
    }

    top.appendChild(meta);

    // Where this sound sits in its family tree. Only when the artefact knows
    // both ids -- the tree view is addressed by run, and highlighting needs the
    // sound -- so it is absent for instruments that predate the sidecar rather
    // than present and broken.
    const src = e.source || {};

    if (src.soundId && src.evolutionRunId) {
      const tree = document.createElement('button');
      tree.className = 'treebtn';
      tree.textContent = 'tree';
      tree.title = 'See this sound\u2019s relatives on synth.is';
      tree.addEventListener('click', function (ev) {
        // Or the row's own handler would also fire and load the instrument,
        // which is not what a navigation button asked for.
        ev.stopPropagation();

        const params = 'run=' + encodeURIComponent(src.evolutionRunId)
          + '&view=tree&highlight=' + encodeURIComponent(src.soundId);

        // Biomes' tab lights, because /tree is where this goes. A deep link
        // that left Library current would be the bar lying about the surface.
        setStrip(true, 4);

        call('openSitePath', '/tree?' + params).catch(function (err) {
          setStrip(false, 0);
          setStatus(String(err), 'err');
        });
      });
      top.appendChild(tree);
    }

    // Remove. Inline confirmation rather than a modal: the whole page is one
    // list, and a dialog over it to ask about one row is more interruption than
    // the action deserves -- especially since it goes to the Trash and is
    // recoverable, so the confirm is politeness rather than a safety net.
    const rm = document.createElement('button');
    rm.className = 'rmbtn';
    rm.textContent = '\u00d7';
    rm.title = 'Move this instrument to the Trash';
    rm.addEventListener('click', function (ev) {
      ev.stopPropagation();

      const confirm = document.createElement('span');
      confirm.className = 'confirm';
      confirm.appendChild(document.createTextNode('Move to Trash?'));

      const yes = document.createElement('button');
      yes.className = 'yes';
      yes.textContent = 'remove';
      yes.addEventListener('click', async function (e2) {
        e2.stopPropagation();
        try {
          const ok = await call('removeInstrument', e.id);
          setStatus(ok ? 'Moved \u201c' + e.name + '\u201d to the Trash.'
                       : 'Could not remove that instrument.', ok ? undefined : 'err');
          // Dropped from the new set too, or a re-added copy would inherit the
          // badge from the one that was just removed.
          newIds.delete(e.id);
          refresh();
        } catch (err) {
          setStatus(String(err), 'err');
        }
      });

      const no = document.createElement('button');
      no.className = 'no';
      no.textContent = 'cancel';
      no.addEventListener('click', function (e2) {
        e2.stopPropagation();
        confirm.replaceWith(rm);
      });

      confirm.appendChild(yes);
      confirm.appendChild(no);
      rm.replaceWith(confirm);
    });

    top.appendChild(rm);

    row.appendChild(top);

    // Omitted entirely when there is nothing to say, rather than rendered as an
    // empty line. An instrument with no metadata should look plain, not broken.
    const bits = describe(e);

    if (bits.length) {
      const sub = document.createElement('div');
      sub.className = 'sub';

      bits.forEach(function (bit, i) {
        if (i > 0) {
          const sep = document.createElement('span');
          sep.className = 'sep';
          sep.textContent = '\u00b7';
          sub.appendChild(sep);
        }
        const span = document.createElement('span');
        if (bit.warn) span.className = 'warn';
        span.textContent = bit.text;
        sub.appendChild(span);
      });

      row.appendChild(sub);
    }

    row.addEventListener('click', function () { load(e.id); });
    list.appendChild(row);
  });

  // After the rows exist, not before. Deferred a frame because the canvas and
  // the rows land in the same paint and scrollIntoView on an element the
  // browser has not laid out yet does nothing.
  if (scrollToId) {
    const target = list.querySelector('[data-id="' + scrollToId + '"]');
    scrollToId = null;
    if (target) requestAnimationFrame(function () {
      target.scrollIntoView({ block: 'center', behavior: 'smooth' });
    });
  }
}

async function load (id) {
  // Marked BEFORE the native call, because that call blocks the message thread
  // and nothing after it will run until the load finishes. Everything the user
  // needs to see has to be in the DOM by this point.
  const row = list.querySelector('[data-id="' + id + '"]');
  list.classList.add('busy');

  let spinner = null;

  if (row) {
    row.classList.add('pending');
    spinner = document.createElement('span');
    spinner.className = 'spinner';
    const top = row.firstChild;
    if (top) top.insertBefore(spinner, top.firstChild);
  }

  // Says WHY it takes a moment. "Loading" invites the reading that something is
  // stuck; naming the work makes a two-second wait legible.
  setStatus('Measuring levels across the keyboard\u2026');

  try {
    let r = await call('loadInstrument', id);

    // Traced, because the processor's log said ok=1 and the shell said failed,
    // twice, and reconciling those by reasoning produced two wrong answers.
    call('logFromShell', 'load start id=' + id
      + ' ok=' + JSON.stringify(r && r.ok)
      + ' measuring=' + JSON.stringify(r && r.measuring)
      + ' detail=' + JSON.stringify(r && r.detail));

    // The Faust path hands the measurement to a background thread and returns
    // as soon as the module is in, so this first reply means "started", not
    // "ready". Polling rather than waiting for a push: the editor can be
    // destroyed while a measurement runs, and a push into a destroyed editor
    // is the problem AcquisitionRunner already avoided the same way.

    // Loop on MEASURING ALONE, never on ok.
    //
    // getLoadState reports the last COMPLETED load, and on the first load of a
    // session there isn't one -- the outcome is default-constructed, so ok is
    // false. Testing `r.ok && r.measuring` therefore exited on the first poll
    // and reported "Load failed" for a load that was measuring perfectly well
    // and went on to succeed. Reopening the window showed the instrument
    // active, because it had loaded all along.
    //
    // Capped so a measurement that never clears cannot hang the list forever.
    // 20 s is comfortably past the slowest measurement observed, 4.8 s.
    let waited = 0;

    while (r.measuring && waited < 20000) {
      await new Promise(function (res) { setTimeout(res, 120); });
      waited += 120;
      r = await call('getLoadState');
    }

    if (r.measuring) {
      setStatus('Still measuring after 20 s \u2014 see the log', 'err');
      return;
    }

    call('logFromShell', 'load settled id=' + id
      + ' polls=' + (waited / 120)
      + ' ok=' + JSON.stringify(r && r.ok)
      + ' rid=' + JSON.stringify(r && r.id)
      + ' regions=' + JSON.stringify(r && r.regions)
      + ' tail=' + JSON.stringify(r && r.tailSeconds)
      + ' detail=' + JSON.stringify(r && r.detail));

    // ok and regions are reported separately on purpose. F11: sfizz's
    // loadSfzFile returns true when every region was culled for an
    // unresolvable sample, so "it loaded" and "there is an instrument" are
    // different claims. A UI that collapses them would show a green tick on a
    // silent instrument.
    if (r.ok) {
      currentId = id;
      setStatus('Loaded \u00b7 ' + r.regions + ' regions \u00b7 '
                + r.tailSeconds.toFixed(1) + ' s tail');
    } else {
      setStatus(r.detail || 'Load failed', 'err');
    }
    render();
    drawSpectrogram();
  } catch (err) {
    call('logFromShell', 'load threw id=' + id + ' err=' + String(err));
    setStatus(String(err), 'err');
  } finally {
    // In a finally, not after the try: a failed load must not leave the list
    // permanently unclickable. render() rebuilds the rows and takes the
    // spinner with them, but the busy class lives on the list itself and
    // would survive.
    list.classList.remove('busy');
    if (spinner && spinner.parentNode) spinner.parentNode.removeChild(spinner);
    if (row) row.classList.remove('pending');
  }
}

// ── the loaded instrument's own spectrogram ─────────────────────────────────
//
// Rendered by the plugin during the load measurement, not fetched. The
// measurement already produces five seconds of audio at a4 and keeps only the
// peak, so the picture costs nothing extra -- and it is this instrument as this
// machine plays it, at the host's sample rate, rather than a render made
// elsewhere. A network fetch would also be the one thing this page must never
// do: it is the offline surface.
async function drawSpectrogram () {
  const canvas = document.getElementById('spectro');
  if (!canvas) return;

  let spec = null;
  try { spec = await call('getSpectrogram'); } catch (e) { /* stays empty */ }

  if (!spec || !spec.data) { canvas.className = 'empty'; return; }

  const bins = spec.bins, frames = spec.frames;

  // One byte per cell, base64 over the bridge. atob gives a binary string, so
  // charCodeAt is the decode.
  let raw;
  try { raw = atob(spec.data); } catch (e) { canvas.className = 'empty'; return; }
  if (raw.length < bins * frames) { canvas.className = 'empty'; return; }

  // Drawn at data resolution and stretched by CSS. Sizing the backing store to
  // the element instead would mean re-rendering on every resize for an image
  // that is deliberately blurred to 16% opacity anyway.
  canvas.width = frames;
  canvas.height = bins;

  const ctx = canvas.getContext('2d');
  const image = ctx.createImageData(frames, bins);

  for (let f = 0; f < frames; f++) {
    for (let b = 0; b < bins; b++) {
      const v = raw.charCodeAt(f * bins + b);
      // Low frequencies at the BOTTOM, which is how every spectrogram on the
      // site is drawn; the data runs the other way.
      const px = ((bins - 1 - b) * frames + f) * 4;
      // Blue-green ramp, matching the site's spectrograms closely enough to
      // read as the same kind of picture without importing a palette.
      //
      // Alpha is CURVED rather than linear in the magnitude. A linear ramp put
      // most of the image in the 20-60 range, where it was too faint to read
      // at any panel opacity that stayed subdued; the square-root lifts the
      // quiet detail without letting the loud partials dominate.
      const a = Math.sqrt(v / 255);
      image.data[px]     = Math.round(v * 0.18);
      image.data[px + 1] = Math.round(v * 0.85);
      image.data[px + 2] = Math.round(60 + v * 0.55);
      image.data[px + 3] = Math.round(a * 255);
    }
  }

  ctx.putImageData(image, 0, 0);
  canvas.className = '';
}

async function refresh () {
  if (!backend) {
    setStatus('Native bridge unavailable \u2014 this page is not being served '
              + 'by the plugin.', 'err');
    return;
  }
  try {
    entries = await call('listInstruments');

    // Anything that was not here last time. On the FIRST list knownIds is
    // null, so nothing is marked -- otherwise opening the editor would badge
    // every instrument in the library as new.
    const ids = new Set(entries.map(function (e) { return e.id; }));

    if (knownIds !== null) {
      const arrived = entries.filter(function (e) { return !knownIds.has(e.id); });

      if (arrived.length) {
        arrived.forEach(function (e) { newIds.add(e.id); });
        // The last one, because acquisitions finish in submission order and the
        // most recent is the one the user was waiting for.
        scrollToId = arrived[arrived.length - 1].id;
      }
    }

    knownIds = ids;
    // Ids that have since been deleted should not stay marked forever.
    newIds.forEach(function (id) { if (!ids.has(id)) newIds.delete(id); });

    const cur = await call('getStatus');
    currentId = cur && cur.ok ? cur.id : null;
    render();
    drawSpectrogram();
    setStatus(currentId
      ? 'Loaded \u00b7 ' + cur.regions + ' regions \u00b7 '
        + cur.tailSeconds.toFixed(1) + ' s tail'
      : 'Nothing loaded yet \u2014 pick an instrument.');
  } catch (err) {
    setStatus(String(err), 'err');
  }
}

// ── Sounds you have taken ───────────────────────────────────────────────────
//
// Reads getRenders, which reports what is actually ON DISK. That is the whole
// reason this list is worth having over the markers in the web app: those are
// guesses. The live one assumes any live artefact on screen is installed; the
// single one remembers only the current session. Neither can see a filesystem.
//
// Durations come from each file's own header, so this cannot repeat C44's
// mistake of showing the length that was REQUESTED rather than the one that
// arrived.

let renderRows = [];
let audio = null;          // the one <audio> element, reused
let playingFile = null;

function fmtSeconds (s) {
  if (!s || s <= 0) return '';
  return (s < 10 ? s.toFixed(1) : Math.round(s)) + 's';
}

function fmtBytes (b) {
  if (!b || b <= 0) return '';
  return b >= 1048576 ? (b / 1048576).toFixed(1) + ' MB'
                      : Math.max(1, Math.round(b / 1024)) + ' kB';
}

function stopPlayback () {
  if (audio) { audio.pause(); audio.currentTime = 0; }
  playingFile = null;

  if (backend) call('renderPlaying', '').catch(function () {});
  Array.prototype.forEach.call(
    document.querySelectorAll('.rrow button.rplay'),
    function (b) { b.classList.remove('on'); b.textContent = 'Play'; }
  );
}

function playRender (file, button) {
  // button is optional: a play request arriving from the NATIVE list has no
  // HTML button behind it, and the row it lights up lives on the other side of
  // the bridge.
  // Toggling the row that is already sounding stops it, which is what a second
  // press of a play button means to everyone.
  if (playingFile === file) { stopPlayback(); return; }

  stopPlayback();

  if (!audio) {
    audio = new Audio();
    // Reset the row when the file runs out on its own, so the button does not
    // sit lit with nothing playing -- a control claiming a state it is not in.
    audio.addEventListener('ended', stopPlayback);
    audio.addEventListener('error', stopPlayback);
  }

  // OUR OWN ORIGIN. The bytes come through the resource provider that serves
  // /logo.svg, so WebAudio plays them to the system output exactly as the
  // site's feed play button already does inside the plugin -- nothing enters
  // the host's signal chain. A file:// URL is the one thing that cannot work
  // here: C43 measured that WebKit will not load a local file from a page on a
  // non-file origin.
  audio.src = '/render/' + encodeURIComponent(file);
  playingFile = file;

  if (button) {
    button.classList.add('on');
    button.textContent = 'Stop';
  }

  // Reported rather than assumed, so a file that fails to load leaves the
  // native row unlit instead of showing a Stop button for silence.
  call('renderPlaying', file).catch(function () {});

  audio.play().catch(function () { stopPlayback(); });
}

function renderRenders () {
  const sec = document.getElementById('rendersec');
  const list = document.getElementById('rlist');

  // Hidden entirely rather than shown empty: before the first take there is
  // nothing to say, and a heading over a blank box is worse than no heading.
  if (renderRows.length === 0) {
    sec.hidden = true;
    list.innerHTML = '';
    return;
  }

  sec.hidden = false;

  // The count lives in the sub-tab badge, set in refreshRenders. This section
  // used to carry its own heading and count because it sat above the
  // instrument list and had to name itself; a tab labelled "Sounds 4" does
  // both, so repeating it here was saying the same thing twice.

  list.innerHTML = '';

  renderRows.forEach(function (r) {
    const row = document.createElement('div');
    row.className = 'rrow';

    const name = document.createElement('span');
    name.className = 'rname';
    name.textContent = r.name;
    name.title = r.file;

    const meta = document.createElement('span');
    meta.className = 'rmeta';
    meta.textContent = [fmtSeconds(r.seconds), fmtBytes(r.bytes)]
      .filter(Boolean).join(' \u00b7 ');

    const play = document.createElement('button');
    play.className = 'rplay';
    play.textContent = playingFile === r.file ? 'Stop' : 'Play';
    if (playingFile === r.file) play.classList.add('on');
    play.addEventListener('click', function () { playRender(r.file, play); });

    const del = document.createElement('button');
    del.className = 'rdel';
    del.textContent = 'Delete';
    del.addEventListener('click', function () {
      // CONFIRMS, because this removes a file the user spent extraction quota
      // on. The native end deliberately does not prompt -- a modal over a
      // webview inside a DAW is its own problem -- so the asking belongs here.
      if (!window.confirm('Delete \u201c' + r.name + '\u201d from your renders?\n\n'
                          + 'The sound stays yours on synth.is and can be taken '
                          + 'again for free.')) {
        return;
      }

      if (playingFile === r.file) stopPlayback();

      call('deleteRender', r.file).then(function (ok) {
        if (!ok) setStatus('Could not delete that render', 'err');
        refreshRenders();
      }).catch(function () {
        setStatus('Could not delete that render', 'err');
      });
    });

    row.appendChild(name);
    row.appendChild(meta);
    row.appendChild(play);
    row.appendChild(del);
    list.appendChild(row);
  });
}

function refreshRenders () {
  if (!backend) return;

  call('getRenders').then(function (rows) {
    renderRows = Array.isArray(rows) ? rows : [];

    // A file that vanished under us -- deleted in Finder, or by the drain on
    // iOS -- must not leave its button lit.
    if (playingFile && !renderRows.some(function (r) { return r.file === playingFile; })) {
      stopPlayback();
    }

    // The badge on the Sounds sub-tab, which is the only place the count shows
    // while the Instruments half is on screen.
    const badge = document.getElementById('subcount');
    if (badge) badge.textContent = renderRows.length ? String(renderRows.length) : '';

    renderRenders();
  }).catch(function () {
    // An older binary has no getRenders. Leaving the section hidden is the
    // right outcome: a visible error about a missing bridge function helps
    // nobody using it.
    renderRows = [];
    renderRenders();
  });
}

// ── Library sub-tabs ─────────────────────────────────────────────────────
//
// One class on <body>, so the CSS decides what is on screen and this only
// decides which class. Reporting the chrome height afterwards matters: the
// sub-tab row lives inside nav#chrome, and C++ sizes the strip from what the
// page reports -- a switch that changed the nav's height without saying so
// would leave the strip the wrong size until the next resize.

function setLibraryView (view) {
  const sounds = view === 'sounds';
  const controls = view === 'controls';

  document.body.classList.toggle('sounds', sounds);
  document.body.classList.toggle('controls', controls);

  const subs = document.querySelectorAll('nav#chrome .subtabs .sub');
  for (let i = 0; i < subs.length; i++)
    subs[i].classList.toggle('current', subs[i].dataset.view === view);

  // Playback is scoped to the view it started in. Leaving a sound running
  // while its list is off screen means a stop button nobody can reach.
  if (!sounds) stopPlayback();

  if (sounds) refreshRenders();
  if (controls) refreshControls();

  // TELLS C++, which is what makes the native list appear. On desktop the
  // editor shrinks the shell to its strip and gives the body to RenderList,
  // whose rows can be dragged into a DAW -- an HTML row cannot be (C43). On
  // iOS this is a no-op and the section below stays on screen instead, which
  // is right: there is no drag on iOS to be had.
  call('soundsView', sounds).catch(function () { /* older binary */ });

  reportChromeHeight();
}

Array.prototype.forEach.call(
  document.querySelectorAll('nav#chrome .subtabs .sub'),
  function (btn) {
    btn.addEventListener('click', function () {
      setLibraryView(btn.dataset.view);
    });
  }
);

// ── The Controls panel (synthis-cda.3) ─────────────────────────────────────
//
// One row per mapping target the native bridge reports (getCcMappings): the
// sixteen macro slots first, then attack / decay / sustain / release / output
// under their own heading. The values are POLLED, not pushed, so a change
// made by the host or by OSC shows up here -- CC_POLL_MS is ~13 polls a
// second, inside the 10..20/s the panel is specified to run at.
//
// Mapping (the chip, Learn, clear, the range editor) and the input monitor
// are STANDALONE-ONLY (.sa-only): in a DAW the host owns MIDI input, so those
// controls have nothing to act on. The sliders themselves stay and read the
// values the host moves.

const CC_POLL_MS = 75;    // ~13x/s, inside the specified 10..20 times a second
const ENV_TARGET_NAMES = ['attack', 'decay', 'sustain', 'release', 'output'];

let ccRows = [];          // the last getCcMappings payload
let ccRowEls = new Map(); // target -> the row's elements
let learnTarget = -1;
let standaloneBridge = false;

// The native bridge resolves a target from a NAME (ccTargetFromName,
// PluginProcessor.cpp): "attack".."output" or a slot number 1..16. "macro1"
// is NOT resolvable -- its int value is 0, which resolves to -1 -- so macro
// slots are addressed by slot number (target + 1) and the envelope/output
// targets by name.
function ccTargetArg (m) {
  return ENV_TARGET_NAMES.indexOf(String(m.name).toLowerCase()) >= 0
    ? m.name
    : String(m.target + 1);
}

// "cc:21:1" -> "CC 21 ch1"; "osc:/mocap/hand/y" -> "/mocap/hand/y".
function chipText (m) {
  if (!m || !m.mapped || !m.source) return '';
  const s = String(m.source);
  if (s.indexOf('cc:') === 0) {
    const p = s.split(':');
    return 'CC ' + p[1] + ' ch' + p[2];
  }
  if (s.indexOf('osc:') === 0) return s.slice(4);
  return s;
}

function monitorText (e) {
  if (!e || !e.source) return 'No MIDI or OSC input yet.';
  const s = String(e.source);
  if (s.indexOf('cc:') === 0) {
    const p = s.split(':');
    return 'Last input: CC ' + p[1] + ' ch' + p[2] + ' = ' + e.value;
  }
  if (s.indexOf('osc:') === 0) return 'Last input: OSC ' + s.slice(4);
  if (s.indexOf('note:') === 0) {
    const p = s.split(':');
    return 'Last input: note ' + p[1] + ' ch' + p[2] + ' vel ' + e.value;
  }
  return 'Last input: ' + s;
}

function updateLearnButtons () {
  ccRowEls.forEach(function (els, t) {
    const on = learnTarget === t;
    els.learn.classList.toggle('on', on);
    els.learn.textContent = on ? 'Learning\u2026' : 'Learn';
  });
}

function buildControlRow (m) {
  const row = document.createElement('div');
  row.className = 'crow';
  row.dataset.target = m.target;

  const top = document.createElement('div');
  top.className = 'ctop';

  const name = document.createElement('span');
  name.className = 'cname';
  name.textContent = m.name;
  name.title = m.name;

  // The slider is a live read-out of the value in the target's own
  // (normalised) units, not a control: no native setter exists for it, so
  // it is disabled and the poll below always keeps it following host or
  // OSC changes.
  const slider = document.createElement('input');
  slider.type = 'range';
  slider.className = 'cslider';
  slider.min = '0';
  slider.max = '1';
  slider.step = '0.001';
  slider.disabled = true;
  slider.title = 'Live value; move it with MIDI, OSC or the host';

  const val = document.createElement('span');
  val.className = 'cval';

  const chip = document.createElement('span');
  chip.className = 'cchip sa-only';

  const learn = document.createElement('button');
  learn.className = 'learn sa-only';
  learn.textContent = 'Learn';
  learn.title = 'Arm learn for this slider: the next incoming MIDI CC or OSC message is mapped to it';
  learn.addEventListener('click', function () {
    const arm = learnTarget !== m.target;
    call('setCcLearnTarget', arm ? ccTargetArg(m) : -1).then(function (r) {
      learnTarget = (r && r.learnTarget !== undefined && r.learnTarget !== null)
        ? r.learnTarget : (arm ? m.target : -1);
      updateLearnButtons();
    }).catch(function () {});
  });

  const clear = document.createElement('button');
  clear.className = 'cclear sa-only';
  clear.textContent = '\u00d7';
  clear.title = 'Clear this mapping';
  clear.addEventListener('click', function () {
    call('clearCcMapping', ccTargetArg(m)).then(function () {
      if (learnTarget === m.target) learnTarget = -1;
      refreshControls();
    }).catch(function () {});
  });

  const expand = document.createElement('button');
  expand.className = 'cexpand sa-only';
  expand.textContent = '\u25b8';
  expand.title = 'Range / smoothing / invert';

  top.appendChild(name);
  top.appendChild(slider);
  top.appendChild(val);
  top.appendChild(chip);
  top.appendChild(learn);
  top.appendChild(clear);
  top.appendChild(expand);
  row.appendChild(top);

  // The expandable range / smoothing / invert editor.
  const edit = document.createElement('div');
  edit.className = 'cedit sa-only';
  edit.hidden = true;

  expand.addEventListener('click', function () {
    edit.hidden = !edit.hidden;
    expand.textContent = edit.hidden ? '\u25b8' : '\u25be';
  });

  const mkField = function (labelText, input) {
    const lab = document.createElement('label');
    lab.appendChild(document.createTextNode(labelText));
    lab.appendChild(input);
    return lab;
  };

  const minIn = document.createElement('input');
  minIn.type = 'number'; minIn.min = '0'; minIn.max = '1'; minIn.step = '0.01';
  const maxIn = document.createElement('input');
  maxIn.type = 'number'; maxIn.min = '0'; maxIn.max = '1'; maxIn.step = '0.01';
  const smoothIn = document.createElement('input');
  smoothIn.type = 'range'; smoothIn.min = '0'; smoothIn.max = '1'; smoothIn.step = '0.01';
  const invIn = document.createElement('input');
  invIn.type = 'checkbox';
  const apply = document.createElement('button');
  apply.className = 'apply';
  apply.textContent = 'Apply';

  apply.addEventListener('click', function () {
    const cur = ccRows.find(function (r) { return r && r.target === m.target; }) || m;
    call('setCcMapping', ccTargetArg(m), cur.cc, cur.channel,
         parseFloat(minIn.value), parseFloat(maxIn.value),
         invIn.checked, parseFloat(smoothIn.value))
      .then(function () { refreshControls(); })
      .catch(function () {});
  });

  edit.appendChild(mkField('min', minIn));
  edit.appendChild(mkField('max', maxIn));
  edit.appendChild(mkField('smoothing', smoothIn));
  edit.appendChild(mkField('invert', invIn));
  edit.appendChild(apply);
  row.appendChild(edit);

  return { row: row, slider: slider, val: val, chip: chip, learn: learn,
           clear: clear, expand: expand, edit: edit,
           minIn: minIn, maxIn: maxIn, smoothIn: smoothIn, invIn: invIn, apply: apply };
}

function updateControls (rows) {
  ccRows = rows;

  const list = document.getElementById('cclist');
  const envList = document.getElementById('envlist');
  if (!list || !envList) return;

  const seen = new Set();

  rows.forEach(function (m) {
    if (!m || m.target === undefined || m.target === null) return;
    seen.add(m.target);

    let els = ccRowEls.get(m.target);
    if (!els) {
      els = buildControlRow(m);
      ccRowEls.set(m.target, els);
      const isEnv = ENV_TARGET_NAMES.indexOf(String(m.name).toLowerCase()) >= 0;
      (isEnv ? envList : list).appendChild(els.row);
    }

    // The live value: slider and readout always follow the polled native
    // value (the slider is disabled, so nothing can hold it back).
    const v = typeof m.value === 'number' ? m.value : 0;
    els.slider.value = String(v);
    els.val.textContent = v.toFixed(3);

    const chip = chipText(m);
    els.chip.textContent = chip || 'unmapped';
    els.chip.classList.toggle('unmapped', !chip);

    els.minIn.value = String(m.rangeMin !== undefined ? m.rangeMin : 0);
    els.maxIn.value = String(m.rangeMax !== undefined ? m.rangeMax : 1);
    els.smoothIn.value = String(m.smoothing || 0);
    els.invIn.checked = !!m.invert;

    // setCcMapping writes CC mappings, so an OSC-mapped row cannot be
    // re-applied through it; its range is changed by learning again.
    const ccMapped = m.mapped && String(m.source || '').indexOf('cc:') === 0;
    els.apply.disabled = !ccMapped;
    els.apply.title = ccMapped ? 'Apply range / smoothing / invert'
                               : 'Learn maps the source first; OSC mappings are re-learned';

    // Learn clears itself natively once a mapping lands; mirror that here.
    if (learnTarget === m.target && m.mapped) learnTarget = -1;
  });

  ccRowEls.forEach(function (els, t) {
    if (!seen.has(t) && els.row.parentNode) els.row.parentNode.removeChild(els.row);
  });

  updateLearnButtons();

  // The empty state: nothing mapped yet (or no bridge at all) is exactly when
  // a first-time user needs the one sentence about what Learn does.
  const empty = document.getElementById('controlsempty');
  if (empty) {
    const anyMapped = rows.some(function (r) { return r && r.mapped; });
    empty.style.display = (rows.length && anyMapped) ? 'none' : '';
  }
}

async function refreshControls () {
  if (!backend) return;
  try {
    const rows = await call('getCcMappings');
    if (Array.isArray(rows)) updateControls(rows);
  } catch (e) { /* editor closing */ }
}

async function pollControls () {
  if (!backend) return;
  // Only while the Controls view is on screen: the poll exists so host or
  // OSC changes show, and nobody is reading another view.
  if (!document.body.classList.contains('controls')) return;
  try {
    const rows = await call('getCcMappings');
    if (Array.isArray(rows)) updateControls(rows);

    const mon = await call('getCcMonitor');
    const line = document.getElementById('ccmonitor');
    if (line) line.textContent = monitorText(Array.isArray(mon) && mon.length ? mon[0] : null);
  } catch (e) { /* editor closing */ }
}

setInterval(pollControls, CC_POLL_MS);

async function pollOscLine () {
  if (!backend) return;
  try {
    const st = await call('getOscState');
    // receiverExists is true only in the Standalone wrapper: this is the
    // native bridge reporting where it is running.
    standaloneBridge = !!(st && st.receiverExists);
    document.body.classList.toggle('standalone', standaloneBridge);
    if (!standaloneBridge) return;

    const ips = await call('getLocalIpAddresses');
    const line = document.getElementById('oscline');
    if (!line) return;
    const addr = Array.isArray(ips) && ips.length ? ips.join(', ') : 'no address found';
    line.textContent = 'OSC port ' + (st.port !== undefined ? st.port : '?')
      + (st.listening ? ' \u00b7 listening' : ' \u00b7 not listening'
         + (st.error ? ' (' + st.error + ')' : ''))
      + ' \u00b7 send to ' + addr;
  } catch (e) { /* editor closing */ }
}

setInterval(pollOscLine, 1000);
pollOscLine();

// ── What C++ asks the page for ──────────────────────────────────────────────
//
// The native Sounds list cannot play a file itself without its own
// AudioDeviceManager, and routing a preview through the processor would drop it
// into the host's signal chain and get it recorded if the transport is rolling.
// So it asks here, and WebAudio reaches the system output exactly as the site's
// feed play button already does inside the plugin.
if (backend) {
  backend.addEventListener('playRender', function (file) {
    const name = typeof file === 'string' ? file : '';

    if (!name) { stopPlayback(); return; }

    // No button: the row that will light up is the native one, and it learns
    // about it from the renderPlaying call inside playRender.
    playRender(name, null);
  });

  // The folder changed under the native list -- a delete there, or a take
  // landing while it is open. Only this page owns the Sounds badge.
  backend.addEventListener('rendersChanged', function () {
    refreshRenders();
  });
}

document.getElementById('rreveal').addEventListener('click', function () {
  call('revealRenders').catch(function () {});
});

document.getElementById('refresh').addEventListener('click', function () {
  // An explicit refresh is the user saying "show me the current state", so the
  // new-arrival marks are cleared: they exist to point out something that
  // appeared while attention was elsewhere.
  newIds.clear();
  refresh();
  refreshRenders();
});

document.getElementById('filter').addEventListener('input', function (ev) {
  filterText = ev.target.value;
  render();
});

document.getElementById('sort').addEventListener('change', function (ev) {
  sortBy = ev.target.value;
  // Remembered across editor opens, which happen constantly -- a DAW creates
  // and destroys the view whenever the window is closed. try/catch because the
  // shell is served from a custom scheme and storage may be unavailable on an
  // opaque origin; losing the preference is not worth an exception.
  try { localStorage.setItem('synthis.sort', sortBy); } catch (e) { /* fine */ }
  render();
});

try {
  const saved = localStorage.getItem('synthis.sort');
  if (saved) { sortBy = saved; document.getElementById('sort').value = saved; }
} catch (e) { /* fine */ }

// A DAW's panic is not guaranteed to reach a plugin and GarageBand shows none.
// 16 of 33 measured archive genomes ignore their own gate, so without this the
// only way to stop a stuck drone is to delete the track.
document.getElementById('panic').addEventListener('click', async () => {
  await call('panic');
  setStatus('Panicked \u2014 all voices reinitialised.');
});

// Diagnostic, and the status line is the whole point of it.
//
// Silence has three causes and they need telling apart: the OS never called
// processBlock, it did but no instrument is loaded, or both happened and the
// engine rendered nothing. `blocks` answers the first, and it is reported here
// rather than logged because on a phone the log is not where anyone is looking.
document.getElementById('testnote').addEventListener('click', async () => {
  const r = await call('testNote', 69, 1.0, 800);
  const blocks = (r && r.blocks) || 0;
  setStatus(blocks > 0
    ? 'Note sent (a4, 0.8 s) \u2014 ' + blocks + ' audio blocks rendered so far.'
    : 'Note sent, but the audio callback has NEVER run (0 blocks). '
      + 'Nothing can be audible until it does.');
});

// Polled only while a Faust instrument is loaded. notesOn far ahead of
// notesOff means the host is not delivering note-offs, which is a completely
// different problem from the engine holding a voice open, and the two are
// indistinguishable by ear.
//
// Owns the status line WHILE MEASURING, and hands it back on the falling edge.
// See the measuring block below.
let wasMeasuring = false;

setInterval(async () => {
  try {
    const st = await call('engineState');
    if (!st) return;

    // NINE SECONDS OF SILENCE, said out loud. C21.
    //
    // An instrument acquired from the site autoloads in about half a second and
    // the audio path is then suspended for as long as the keyboard measurement
    // takes -- 8.9 s on an A15 under wasm3. A load started from THIS page
    // already says so, in load(); an autoload never goes through that function,
    // because the user was on the site when they pressed "Add to plugin". So
    // the shell woke up, called refresh(), and wrote "Loaded" over a plugin
    // that could not yet make a sound. The user then taps the instrument, hits
    // the cached measurement, and learns that tapping is what activates it.
    //
    // Owning the line unconditionally while measuring is right rather than
    // rude: there is no other true thing to say about the plugin during those
    // seconds.
    if (st.measuring) {
      setStatus('Measuring levels across the keyboard\u2026 not playable yet');
      wasMeasuring = true;
    } else if (wasMeasuring) {
      wasMeasuring = false;
      // Handed back rather than left saying "Measuring" forever. Skipped when
      // the list is busy: that means load() is mid-flight and does its own
      // refresh and its own status write, and a second one here would
      // duplicate a listInstruments call and race the message.
      const listEl = document.getElementById('list');
      if (!listEl || !listEl.classList.contains('busy')) refresh();
    }

    // DEADLINE LOAD, built first and shown whether or not an engine is loaded,
    // because processBlock is timed regardless and "nothing loaded and still at
    // 40%" is a reading worth being able to see.
    //
    // Percent of the block period: 100% means the deadline was exactly
    // consumed. This is the number every voice-cap decision has been a PROXY
    // for, and the only one measured in the host at the block size the host
    // actually chose -- capForRtf works from a free-running RTF whose own
    // comment says it "is not a headroom claim about a busy DAW".
    //
    // OVER BUDGET counts blocks that used the whole period. It does not claim a
    // dropout was heard: whether one is audible depends on the host's own
    // buffering. It does mean we were the reason it could be.
    const load = st.budgetMs > 0
      ? 'load ' + Math.round(st.loadRecent * 100) + '% of ' + st.budgetMs.toFixed(2) + ' ms'
        + (st.loadMax > 0 ? ' (peak ' + Math.round(st.loadMax * 100) + '%)' : '')
        + (st.blocksOver > 0 ? ' \u00b7 ' + st.blocksOver + ' OVER BUDGET' : '')
      : '';

    if (!st.faust) {
      document.getElementById('navload').textContent = load;
      return;
    }

    const stuck = st.notesOn - st.notesOff;
    document.getElementById('navload').textContent =
      st.activeVoices + ' voices \u00b7 on ' + st.notesOn + ' / off ' + st.notesOff
      + (st.rawPeak > 0 ? ' \u00b7 trim ' + st.trimDb.toFixed(1) + ' dB (raw peak '
                          + st.rawPeak.toFixed(3) + ')' : '')
      + (st.voiceCap > 0 ? ' \u00b7 ' + st.voiceCap + '-voice cap (RTF '
                           + st.rtf.toFixed(0) + 'x)' : '')
      // LIVE output, and the reason it is worth a line of its own. "rendered
      // 0.000" after thousands of blocks is an instrument producing nothing
      // audible; "rendered 0 blocks" is a host that never asked. Those look
      // identical from the keyboard and the offline raw peak above cannot tell
      // them apart, because it was measured before any of this.
      //
      // Shown only once the host has asked for at least one block, so an
      // editor open before playback does not read as a failure.
      + (st.renderBlocks > 0
           ? ' \u00b7 rendered ' + st.renderPeak.toFixed(3)
             + ' peak over ' + st.renderBlocks + ' blocks'
           : ' \u00b7 not yet rendered')
      + (load ? ' \u00b7 ' + load : '')
      + (stuck > 0 ? ' \u00b7 ' + stuck + ' UNRELEASED' : '')
      + (st.forcedStops > 0 ? ' \u00b7 ' + st.forcedStops + ' force-stopped' : '');
  } catch (e) { /* editor closing */ }
}, 500);

// ── The chrome strip ──────────────────────────────────────────────────────
//
// THERE IS NO C++ -> JS CHANNEL HERE, and that is a consequence rather than an
// omission. showSite is reachable only from this page's own native calls --
// openSite, openSitePath, showLibrary -- so the shell is always the initiator
// and already knows which surface is up. The editor is reconstructed with the
// library showing on every window open, so there is no state to restore on
// load either. If a future caller raises the site from C++, this stops being
// true and the bar starts lying; that caller has to tell the page.
function setStrip (on, dest) {
  document.body.classList.toggle('strip', on);

  const tabs = document.querySelectorAll('nav#chrome .tab');
  for (let i = 0; i < tabs.length; i++)
    tabs[i].classList.toggle('current', tabs[i].dataset.dest === String(dest));
}

// ── Who you are ─────────────────────────────────────────────────────────────
//
// Polled rather than pushed, on the loop that already runs. A push would need
// a C++ -> JS channel that does not otherwise exist, and the note above
// explains why there isn't one.
//
// THE BAR RENDERS ONLY WHAT IT HAS BEEN TOLD. `unknown` hides the control
// entirely rather than defaulting to signed-out, because the plugin cannot see
// the site's session -- different origins, no shared storage -- and a bar that
// guessed would be wrong on every cold start until the first navigation.
let lastAuthWire = '';

function renderAuth (a) {
  const btn = document.getElementById('acct');
  const lbl = document.getElementById('acctlbl');
  if (!btn || !lbl) return;

  const wire = a.kind + '|' + (a.label || '') + '|' + (a.unread ? 1 : 0);
  if (wire === lastAuthWire) return;
  lastAuthWire = wire;

  if (a.kind === 'unknown') { btn.hidden = true; return; }

  btn.hidden = false;
  btn.classList.toggle('anon', a.kind === 'anonymous');
  btn.classList.toggle('none', a.kind === 'none');
  btn.classList.toggle('unread', !!a.unread);

  // Three states, three strings. "Guest" is NOT cosmetic: signing out on the
  // web app creates an anonymous identity silently and likes attach to it, and
  // it was indistinguishable from a real account in the plugin until this said
  // so out loud.
  lbl.textContent = a.kind === 'registered' ? (a.label || 'Account')
                  : a.kind === 'anonymous'  ? 'Guest'
                  :                           'Sign in';

  btn.title = a.kind === 'registered' ? 'Signed in' + (a.label ? ' as ' + a.label : '')
            : a.kind === 'anonymous'  ? 'Browsing as a guest \u2014 likes are not on your account'
            :                           'Not signed in';
}

function pollAuth () {
  call('getAuthState').then(renderAuth).catch(function () { /* editor closing */ });
}

document.getElementById('acct').addEventListener('click', function () {
  // Navigates the SITE to its own account surface. The plugin does not open a
  // sign-in of its own and holds no credential -- the site is the only thing
  // that knows the session, so it is the only thing that can act on it.
  //
  // dest -1 marks no tab current, because /account is not one of the five.
  setStrip(true, -1);

  call('openSitePath', '/account').then(function (ok) {
    if (ok === false) {
      setStrip(false, 0);
      setStatus('Refused: /account', 'err');
    }
  }).catch(function (e) {
    setStrip(false, 0);
    setStatus(String(e), 'err');
  });
});

pollAuth();

// THE STRIP'S HEIGHT IS MEASURED, NOT ASSUMED.
//
// kStripHeight is the bar; the element is that plus env(safe-area-inset-top),
// which is MEASURED at 62pt in the standalone and 0 in the AUv3 under AUM
// (2026-09-11, both ends). C++ cannot compute either one, and the version that
// assumed 50pt clipped the bar out of its own strip entirely.
//
// Reported on load and on resize, because the inset moves with orientation and
// with whatever rectangle a host hands us. The editor defaults to kStripHeight
// until the first report, so a page that never answers degrades to the old
// wrong-but-bounded behaviour rather than to a zero-height strip.
let lastReportedChromeHeight = 0;

function reportChromeHeight () {
  const nav = document.getElementById('chrome');
  if (!nav || !backend) return;

  const h = Math.ceil(nav.getBoundingClientRect().height);
  if (h <= 0 || h === lastReportedChromeHeight) return;

  lastReportedChromeHeight = h;
  call('chromeHeight', h).catch(function () { /* editor closing */ });
}

// A ResizeObserver, because ON LOAD IS TOO EARLY and that is measured.
//
// The version before this called on script end, on requestAnimationFrame and
// on window resize, and all three reported 51 -- the bar with no inset above
// it. The probe, three seconds later, read the same element at 113 with
// paddingTop 62px. So env(safe-area-inset-top) is 0 at first layout and
// becomes 62 shortly after, and the inset arriving does NOT fire a window
// resize. Three observations inside the wrong window are still one
// observation.
//
// I had blamed env() inside calc() for the earlier reading and restructured
// this rule accordingly. That was wrong -- getComputedStyle showed the
// stylesheet resolving env perfectly. The restructure stands because an auto
// height measured after layout is the better shape anyway, not because the
// reason given for it held.
//
// An observer on the element itself cannot miss the change whatever causes it:
// the inset landing, an orientation change, the 639px label breakpoint, or a
// host handing over a different rectangle.
if (window.ResizeObserver) {
  const ro = new ResizeObserver(reportChromeHeight);
  const navEl = document.getElementById('chrome');

  // BORDER-BOX, NOT THE DEFAULT.
  //
  // ResizeObserver observes the CONTENT box unless told otherwise, and the
  // safe-area inset arrives as padding-top -- which does not change the content
  // box at all. Tabs 34 plus line 16 is 50 whether the padding is 0 or 62, so a
  // content-box observer is structurally blind to the one event it exists to
  // catch.
  //
  // Measured 2026-09-11: with the default box the report stayed at 51 through
  // load even though the bar was visibly sitting at 62pt, and 113 only arrived
  // 585ms AFTER the first tab tap -- fired by the webview's WIDTH changing when
  // C++ resized it to the (wrong, too short) strip, not by the inset. That left
  // a window where the strip was 51pt and clipped the bar out of itself.
  //
  // This was hidden for several builds by the strip inset diagnostic in
  // kProbeScript, which read getComputedStyle three seconds after load. Whether
  // that read was what forced the earlier runs to report 113 in time is NOT
  // established -- I have no timestamps from those runs. What is established is
  // the box, and the box is enough to explain it.
  if (navEl) ro.observe(navEl, { box: 'border-box' });
}

window.addEventListener('resize', reportChromeHeight);

// Still called eagerly, as a floor rather than as the answer. Without
// ResizeObserver this is all there is, and a strip one inset too short beats a
// strip of nothing.
reportChromeHeight();

const navTabs = document.querySelectorAll('nav#chrome .tab');
for (let i = 0; i < navTabs.length; i++) {
  navTabs[i].addEventListener('click', function () {
    const path = this.dataset.path;

    // Empty path is the plugin's own library: no navigation, just give the
    // shell the whole rectangle back. Opening the Library is what the landed
    // badge counts as "seen", so it clears here (showLibrary clears it
    // natively and pushes activityChanged, which re-renders from getActivity).
    if (!path) {
      setStrip(false, 0);
      clearLibraryBadge();
      call('showLibrary').catch(function (e) { setStatus(String(e), 'err'); });
      return;
    }

    // Class flip FIRST, navigation second. The flip is local and instant where
    // the load is neither, and the other order leaves the tap looking like it
    // never registered for as long as the site takes to answer.
    //
    // This said "undone if the call is refused, which openSitePath does for
    // anything not site-relative". IT WAS NOT UNDONE -- see below. Corrected
    // where the claim was made rather than only where it was fixed.
    setStrip(true, this.dataset.dest);

    call('openSitePath', path).then(function (ok) {
      // A RESOLVED `false` IS A REFUSAL, not a success.
      //
      // openSitePath refuses anything that is not site-relative by calling
      // `done (juce::var (false))`, which RESOLVES the promise. A `.catch()`
      // alone therefore never ran on the one failure it was written for, and
      // the strip was left marked current with no site behind it -- the bar
      // reporting an outcome it did not observe, which is the failure this
      // codebase keeps rediscovering.
      if (ok === false) {
        setStrip(false, 0);
        setStatus('Refused: ' + path, 'err');
      }
    }).catch(function (e) {
      setStrip(false, 0);
      setStatus(String(e), 'err');
    });
  });
}

// There used to be a "Browse synth.is" button here. It was the one way into the
// site surface that did NOT navigate (openSite resumes wherever the user had
// got to, including a half-completed sign-in), and it cost a row of vertical
// space that the nav bar's own tabs make redundant: Feed opens the site root,
// which is the same surface. Removed 2026-10-26 on the owner's call; a
// half-finished sign-in is now resumed by the Feed tab, not a dedicated button.

// ── The activity capsule and the Library badge (synthis-7o3.3) ─────────────
//
// The page is fed by the activityChanged push plus a getActivity pull: the
// event carries no data, so a lost or throttled push costs one stale frame,
// never wrong state. While anything is active the page also pulls every 2 s
// -- WKWebView throttles timers in a hidden view and the push is "if the
// browser is visible", so the pull is the safety net. The page never reads
// site state; getActivity is the ground truth (design §4).
const ACTIVITY_PULL_MS = 2000;
const LANDING_COALESCE_MS = 1000;   // landings within 1 s show as one "Added N"
const LANDING_HOLD_MS = 1200;       // a landing is held on screen at least 1.2 s
const RING_LEN = 50.27;             // 2*pi*8, the ring circle's circumference

let activityActive = 0;
let landedFlash = null;             // { count, firstMs, lastMs } while a landing shows

function reasonText (r) {
  return ({ quota: 'quota reached', signed_out: 'signed out',
            render_failed: 'render failed', timeout: 'timed out',
            handover_failed: 'hand-over failed', network: 'network' })[r] || 'failed';
}

function pulseLibraryTab () {
  const libtab = document.getElementById('libtab');
  if (!libtab) return;
  libtab.classList.add('pulse');
  setTimeout(function () { libtab.classList.remove('pulse'); }, 700);
}

function clearLibraryBadge () {
  const b = document.getElementById('libbadge');
  if (b) b.hidden = true;
}

function renderActivity (a) {
  if (!a) return;
  const now = typeof a.now === 'number' ? a.now : Date.now();
  activityActive = a.active || 0;
  const entries = a.entries || [];

  const capsule = document.getElementById('capsule');
  if (!capsule) return;
  const captext = document.getElementById('capsuletext');
  const capcount = document.getElementById('capsulecount');
  const capring = document.getElementById('capring');
  const ringfg = document.getElementById('ringfg');
  const badge = document.getElementById('libbadge');

  const terminal = function (p) { return p === 'landed' || p === 'failed' || p === 'lost'; };
  const active = entries.filter(function (e) { return !terminal(e.phase); });
  const landed = entries.filter(function (e) { return e.phase === 'landed'; });
  const failed = entries.filter(function (e) { return e.phase === 'failed'; });
  const lost = entries.filter(function (e) { return e.phase === 'lost'; });

  // Landings within 1 s of each other coalesce into one "Added N"; the flash
  // is held for at least 1.2 s even if the entries expire sooner. A new flash
  // only starts once the current one has been on screen for the full hold --
  // a landing that arrives sooner merges into it, so the hold is never
  // truncated (landings 1.0-1.2 s apart would otherwise restart the clock).
  if (landed.length) {
    const newest = Math.max.apply(null, landed.map(function (e) { return e.updatedMs || 0; }));
    const recent = landed.filter(function (e) { return newest - (e.updatedMs || 0) <= LANDING_COALESCE_MS; });
    const fresh = !landedFlash
      || ((now - landedFlash.firstMs >= LANDING_HOLD_MS)
          && newest - landedFlash.lastMs > LANDING_COALESCE_MS);
    if (fresh) {
      landedFlash = { count: recent.length, firstMs: now, lastMs: newest };
      pulseLibraryTab();
    } else {
      landedFlash.count = recent.length;
      landedFlash.lastMs = newest;
    }
  } else if (landedFlash && now - landedFlash.firstMs >= LANDING_HOLD_MS) {
    landedFlash = null;
  }

  // Tooltip: the jobs, oldest first.
  capsule.title = entries.slice()
    .sort(function (x, y) { return (x.startedMs || 0) - (y.startedMs || 0); })
    .map(function (e) { return (e.title || e.job || '?') + ' \u2014 ' + e.phase; })
    .join('\n');

  // What the capsule says. One job in flight: "Rendering" plus the title, the
  // ring determinate when pct >= 0. Two or more: "Rendering N", indeterminate.
  // A landing: "Added" plus the title, or the coalesced count. lost and failed
  // show in the warning colour for the 8 s the entry lives.
  let text = '';
  let count = '';
  let cls = '';
  let indet = false;
  let pct = -1;

  if (lost.length) {
    cls = 'warn';
    text = 'Lost track of ' + (lost[0].title || lost[0].job || '');
  } else if (failed.length) {
    cls = 'warn';
    text = 'Failed \u00b7 ' + reasonText(failed[0].reason);
  } else if (landedFlash) {
    cls = 'landed';
    const t = landed.length
      ? (landed[landed.length - 1].title || landed[landed.length - 1].job || '') : '';
    text = landedFlash.count > 1 ? 'Added ' + landedFlash.count : 'Added ' + t;
  } else if (active.length >= 2) {
    text = 'Rendering ' + active.length;
    indet = true;
  } else if (active.length === 1) {
    text = 'Rendering ' + (active[0].title || active[0].job || '');
    pct = active[0].pct;
    if (!(pct >= 0)) indet = true;   // pct -1: indeterminate, never a frozen number
  }

  if (active.length > 0) count = String(active.length);
  else if (landedFlash) count = String(landedFlash.count);

  const show = entries.length > 0 || landedFlash;
  const wasLanded = capsule.classList.contains('landed');

  if (show) {
    capsule.hidden = false;
    // A landing or job that arrived while the capsule was folding removes
    // the fold here, so the pending fold timeout must not hide it.
    capsule.classList.remove('fold');
    capsule.classList.toggle('landed', cls === 'landed');
    capsule.classList.toggle('warn', cls === 'warn');
    if (captext) captext.textContent = text;
    if (capcount) capcount.textContent = count;
    if (capring) capring.classList.toggle('indet', indet);
    if (ringfg) {
      if (cls === 'warn') ringfg.setAttribute('stroke-dashoffset', '0');
      else if (!indet && pct >= 0) ringfg.setAttribute('stroke-dashoffset', String(RING_LEN * (1 - pct / 100)));
      else ringfg.setAttribute('stroke-dashoffset', String(RING_LEN));
    }
  } else if (wasLanded) {
    // The landing has been on screen long enough: fold the capsule into the
    // Library tab rather than dropping it. The timeout only hides when the
    // fold is still current -- a frame that arrived during the 320 ms removed
    // 'fold' in the show branch above and must stay visible.
    capsule.classList.add('fold');
    setTimeout(function () {
      if (capsule.classList.contains('fold')) {
        capsule.hidden = true;
        capsule.classList.remove('fold');
      }
    }, 320);
  } else {
    capsule.hidden = true;
  }

  // The badge counts landed-and-unseen instruments only. While jobs are in
  // flight the tab carries a ring dot with no digit.
  if (badge) {
    const unseen = a.unseenLanded || 0;
    if (unseen > 0) {
      badge.hidden = false;
      badge.textContent = String(unseen);
      badge.classList.remove('dot');
    } else if (activityActive > 0) {
      badge.hidden = false;
      badge.textContent = '';
      badge.classList.add('dot');
    } else {
      badge.hidden = true;
      badge.classList.remove('dot');
    }
  }
}

async function pullActivity () {
  if (!backend) return;
  try {
    renderActivity(await call('getActivity'));
  } catch (e) { /* editor closing */ }
}

function pollActivityTimer () {
  if (activityActive > 0) pullActivity();
}

if (backend) backend.addEventListener('activityChanged', pullActivity);
setInterval(pollActivityTimer, ACTIVITY_PULL_MS);
pullActivity();

// Downloads are polled, not pushed. They run on a pool thread owned by the
// processor and outlive this editor, so there is nothing for them to push to.
// One second is well inside human patience and nowhere near often enough to
// matter for a call that reads a vector under a lock.
//
// This poll will usually MISS the running state entirely. A download starts
// while the user is on the site, so the shell is hidden, and WKWebView throttles
// timers in a hidden view. The shell typically wakes up to find the job already
// finished. That is normal, and the reason the refresh below is unconditional:
// an earlier version only refreshed if it had seen the job while active, which
// meant the list silently failed to update and the user had to press Refresh.
//
// Refreshing on every finished batch is safe because this is the only clearer.
// The SiteBar deliberately does not clear, so each batch is processed once.

async function pollAcquisitions () {
  if (!backend) return;
  try {
    const jobs = await call('getAcquisitions');
    if (!jobs || !jobs.length) return;

    // In flight: the capsule in the bar owns progress now (synthis-7o3.3), so
    // there is no status line for it here.
    if (jobs.some(function (j) { return !j.finished; })) return;

    // Everything finished. Failures keep their message; arrivals no longer
    // get a status line -- the capsule and the Library badge report them, and
    // the top status line is not part of the success path (design §1). Clear
    // them so the list does not grow for the life of the session.
    const failed = jobs.filter(function (j) { return !j.ok; });
    if (failed.length) setStatus(failed[0].message, 'err');

    await call('clearFinishedAcquisitions');

    refresh();

    // A take that just finished put a file in the outbox, so the list is now
    // out of date. Refreshed HERE rather than on a timer: this is the one
    // moment the page knows the folder changed.
    if (jobs.some(function (j) { return j.kind === 'render'; })) refreshRenders();
  } catch (e) { /* a poll that fails is not worth a visible error */ }
}

// Auth rides the acquisition poll rather than the engine one: it changes at
// human speed, not per block, and 1s is already generous for something that
// only moves when the user signs in or out.
setInterval(pollAcquisitions, 1000);
setInterval(pollAuth, 1000);
refresh();

// Once at startup, and again whenever an acquisition or a take finishes rather
// than on a timer of its own. The outbox only changes when the plugin puts
// something there, so polling it would be asking a question whose answer this
// page already knows is unchanged -- and the native strip is watching the
// folder anyway for the cases the page cannot see, like a deletion in Finder.
refreshRenders();

// ── Computer-keyboard play (standalone only, synthis-o2a.5) ────────────────
//
// On macOS a focused WKWebView takes the key presses, so JUCE key handling on
// the native on-screen keyboard never sees them while this page has focus. The
// page forwards keydown / keyup to native instead, and native plays the notes
// on the same MidiKeyboardState the on-screen keyboard uses.
//
// The keyEvent native function is registered only in the Standalone wrapper,
// and __juce__functions lists exactly the registered names, so in AU / VST3 /
// AUv3 this block never fires and the DAW host keeps the keys.
const KEY_PLAY_KEYS = 'awsedftgyhujkolp;zxcv';

function keyPlayAvailable () {
  return !!(backend
    && window.__JUCE__.initialisationData
    && window.__JUCE__.initialisationData.__juce__functions
    && window.__JUCE__.initialisationData.__juce__functions.indexOf('keyEvent') >= 0);
}

// A text field owns the keyboard while it has focus -- the Library filter box
// here, and on the site the search and sign-in fields (which carry no bridge
// at all). Typing there must not play notes, so key-DOWN is never forwarded
// from one.
function textInputFocused () {
  const el = document.activeElement;
  if (!el) return false;
  const tag = el.tagName;
  return tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT'
      || el.isContentEditable === true;
}

function forwardKey (ev) {
  if (!keyPlayAvailable()) return;
  const k = (ev.key || '').toLowerCase();
  if (k.length !== 1 || KEY_PLAY_KEYS.indexOf(k) < 0) return;

  const down = ev.type === 'keydown';
  if (down && textInputFocused()) return;
  if (down) ev.preventDefault();

  // Key-UP is always forwarded: a key released while a text field has focus
  // must still end the note it started, and native ignores note-offs for keys
  // it does not hold, so this can never start a note from a text field.
  // `repeat` is forwarded too; native drops repeated key-downs.
  call('keyEvent', down, k, ev.repeat === true).catch(function () {});
}

window.addEventListener('keydown', forwardKey);
window.addEventListener('keyup', forwardKey);

// Losing window focus must release every held key: no keyup will ever arrive
// for them.
window.addEventListener('blur', function () {
  if (keyPlayAvailable()) call('keyEvent', 'blur').catch(function () {});
});
</script>
</body>
</html>
)HTML";
} // namespace kromosynth::shell
