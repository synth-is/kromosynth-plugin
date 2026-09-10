// Palette.h — the web app's colours, in one place, for both surfaces.
//
// WHY THIS FILE EXISTS
//
// The plugin shell and synth.is looked like two applications, and the missing
// nav bar was the smaller half of it. Measured 2026-09-10:
//
//     shell    #0d1117 bg, #e6edf3 text, #21262d borders, #58a6ff accent
//     web app  #1a3366 bg, #82e399 text, #3b6b96 borders, #35aaa3 accent
//
// No overlap on any value. GitHub-dark against dark-blue-and-green. That is
// why the two surfaces read as unrelated even before a user notices the
// chrome, and WP_UNIFIED_CHROME §4 is explicit that what must match is
// structure and vocabulary rather than a stylesheet — but a shared vocabulary
// has to have shared values or it is not shared.
//
// ── ONE SOURCE, BECAUSE THE ALTERNATIVE IS MEASURED ─────────────────────────
//
// C26: one voice-cap policy existed in three places and two of them
// cross-checked each other, so both were wrong in the same direction and
// agreed. A palette duplicated between C++ and an HTML string literal would
// drift exactly that way, and nothing would report it. So the shell's CSS
// custom properties are GENERATED from these constants rather than written
// beside them.
//
// ── PROVENANCE, AND THE TRAP IN IT ──────────────────────────────────────────
//
// From kromosynth-desktop/packages/web/tailwind.config.js, `paletteOriginal`.
// VITE_PALETTE is `original` in both .env.development and .env.production as
// of 2026-09-10, with `mono` present and commented out.
//
// THE CLASS NAMES LIE, and anyone porting this by reading class names and
// assuming stock Tailwind will get it badly wrong:
//
//     blue-600  is TEAL       #35aaa3, not blue
//     gray-300  is LIGHT GREEN #c8f0d2
//     gray-400  is GREEN      #82e399
//     gray-900  is DARK BLUE  #1a3366
//
// The ramp runs green at the light end and blue at the dark end. It is not a
// grey ramp in any sense except its name.
//
// ── THE COUPLING THIS CREATES, STATED ───────────────────────────────────────
//
// VITE_PALETTE is a BUILD-TIME flag: Vite bakes it in, so the plugin cannot
// read it at runtime. Flipping the site to `mono` would leave the plugin on
// `original` silently — the exact class of failure where two implementations
// of one policy stop agreeing and nothing says so. Fixing that properly means
// the site exposing its resolved palette somewhere the plugin can fetch and
// cache. Until then this file is pinned to `original` deliberately, and this
// paragraph is the record of that choice.

#pragma once

#include <juce_graphics/juce_graphics.h>

namespace kromosynth::palette
{

// ── the ramp, verbatim ──────────────────────────────────────────────────────
//
// Names kept as the web app has them, wrong as they are, so a reader comparing
// the two files can do it line by line. Roles are assigned below; nothing
// should use these directly.

inline constexpr const char* gray100 = "#ffffff";
inline constexpr const char* gray200 = "#e6e6e6";
inline constexpr const char* gray300 = "#c8f0d2";   // light green
inline constexpr const char* gray400 = "#82e399";   // green
inline constexpr const char* gray500 = "#569665";   // darker green
inline constexpr const char* gray600 = "#35aaa3";   // teal
inline constexpr const char* gray700 = "#3b6b96";
inline constexpr const char* gray800 = "#42548b";   // blueish purple
inline constexpr const char* gray850 = "#273e72";
inline constexpr const char* gray900 = "#1a3366";
inline constexpr const char* gray950 = "#0c2859";   // dark blue

inline constexpr const char* blue600 = "#35aaa3";   // teal, primary selection
inline constexpr const char* blue700 = "#2a8882";

// ── roles ───────────────────────────────────────────────────────────────────
//
// Assigned from what the web app ACTUALLY USES, counted across its src/ rather
// than guessed:
//
//     text-gray-400   532     bg-gray-800    281     border-gray-700  281
//     text-gray-500   322     bg-gray-700    276     border-gray-800   48
//     text-gray-300   275     bg-gray-900    176
//     text-gray-200    76     bg-gray-950     64
//
// INFERRED, and flagged as such: the split between gray-300 and gray-400 for
// body against secondary text is read off those frequencies, not off a rule the
// web app states anywhere. gray-400 is the most used text colour by a wide
// margin, which is why it is body here. Worth checking by eye, and worth
// changing here rather than at the call sites if it looks wrong.

inline constexpr const char* surfaceDeep   = gray950;   // deepest ground
inline constexpr const char* surfaceBase   = gray900;   // page and chrome
inline constexpr const char* surfacePanel  = gray800;   // cards, panels
inline constexpr const char* surfaceRaised = gray700;   // hover, controls

inline constexpr const char* borderSubtle  = gray850;
inline constexpr const char* border        = gray700;

inline constexpr const char* textPrimary   = gray200;   // headings
inline constexpr const char* textBody      = gray400;   // the workhorse
inline constexpr const char* textSecondary = gray300;
inline constexpr const char* textMuted     = gray500;

inline constexpr const char* accent        = blue600;
inline constexpr const char* accentHover   = blue700;
inline constexpr const char* onAccent      = gray100;

// STATUS COLOURS HAVE NO EQUIVALENT in the web app's palette, which carries no
// red and no amber at all. Kept from the shell's own set rather than invented,
// because a warning that reads as decoration is worse than one that clashes
// slightly. If the site ever defines them, these are the four lines to change.
inline constexpr const char* success       = gray400;   // the palette's green
inline constexpr const char* warning       = "#d29922";
inline constexpr const char* danger        = "#f85149";
inline constexpr const char* dangerSurface = "#6e2f2f";

// Roles the shell needed that the nav bar never exercised, found by mapping
// every literal in ShellPage.h rather than the ones I had noticed. Four of
// these were missed by a truncated `grep | head` and only caught because the
// mapping script refuses to run with anything unmapped -- C25's error, guarded
// against instead of repeated.

// The SELECTED row. Its old value was a dark green tint, which in this palette
// wants to be a dark blue one with the accent carrying the meaning on the
// border. gray850 rather than an invented tint, so it stays in the ramp.
inline constexpr const char* accentSurface = gray850;

// Alpha variants of the accent edge. Kept as literals with their alpha
// channels intact -- these are 8-digit hex, and matching six digits greedily
// would have emitted `var(--success)26` and silently broken the rule.
inline constexpr const char* successEdge   = "#35aaa326";
inline constexpr const char* successGlow   = "#35aaa355";

// Destructive controls. No red exists in the web app's palette, so these stay
// as the shell had them; the four lines to change if the site ever defines
// one.
inline constexpr const char* dangerSurfaceDim = "#3d1d1d";
inline constexpr const char* dangerBorder     = "#8e3b3b";
inline constexpr const char* dangerText       = "#ffdcd7";

/** The `:root` block the shell's stylesheet is built around.

    Generated rather than written into the HTML literal, so the values above
    are the only place they exist. See the C26 note at the top.
*/
inline juce::String rootCss()
{
    juce::String css;

    const auto var = [&css] (const char* name, const char* value)
    {
        css << "    --" << name << ": " << value << ";\n";
    };

    css << "  :root {\n    color-scheme: dark;\n";

    var ("surface-deep",   surfaceDeep);
    var ("surface-base",   surfaceBase);
    var ("surface-panel",  surfacePanel);
    var ("surface-raised", surfaceRaised);

    var ("border-subtle",  borderSubtle);
    var ("border",         border);

    var ("text-primary",   textPrimary);
    var ("text-body",      textBody);
    var ("text-secondary", textSecondary);
    var ("text-muted",     textMuted);

    var ("accent",         accent);
    var ("accent-hover",   accentHover);
    var ("on-accent",      onAccent);

    var ("success",        success);
    var ("success-edge",   successEdge);
    var ("success-glow",   successGlow);
    var ("accent-surface", accentSurface);
    var ("warning",        warning);
    var ("danger",         danger);
    var ("danger-surface", dangerSurface);
    var ("danger-surface-dim", dangerSurfaceDim);
    var ("danger-border",  dangerBorder);
    var ("danger-text",    dangerText);

    // Funnel Sans is the web app's face and is NOT bundled here. ShellPage.h's
    // own rule is no fonts, no CDN and no images, because every external
    // reference is a network request on the one surface whose justification is
    // working offline. Matching the type means shipping the font as a JUCE
    // binary resource, which is a real addition and is deliberately not
    // smuggled in with a colour change.
    css << "    --font-stack: -apple-system, BlinkMacSystemFont, "
           "\"SF Pro Text\", sans-serif;\n";

    css << "  }\n";

    return css;
}

/** A ramp entry as a juce::Colour, for the native chrome.

    The native bar cannot use CSS, so it reads the same constants through this.
    One source, two consumers — which is the whole point of the file.
*/
inline juce::Colour colour (const char* hex)
{
    return juce::Colour::fromString (juce::String ("ff") + (hex + 1));
}

} // namespace kromosynth::palette
