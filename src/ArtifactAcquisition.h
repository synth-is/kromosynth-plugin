// ArtifactAcquisition — turning a sentinel URL into an installed instrument.
//
// Stage 1, WP_SYNTHIS_PLUGIN SS12 and SS13. KEEPABLE.
//
// ── What this is for ─────────────────────────────────────────────────────────
//
// The site component (native integration OFF) cannot call native functions, so
// acquisition works by navigation interception: the site navigates to a
// sentinel URL, pageAboutToLoad recognises it, returns false to block the
// navigation, and hands the parsed request here.
//
// This file deliberately knows nothing about WebBrowserComponent, the editor or
// the processor. It takes a struct and produces files on disk, which is what
// makes it testable by tools/acquire-probe without a DAW, a host or a webview.
//
// ── Why a custom scheme and not an https path ────────────────────────────────
//
// The sentinel carries a ticket. If the plugin were NOT listening -- an older
// build, a browser, a host whose webview behaves unexpectedly -- an https
// sentinel would be a real network request, and the ticket would land in
// Cloudflare's edge logs. A custom scheme cannot reach the network at all, so
// the failure mode is "nothing happens", which is visible in testing, rather
// than "credential leaked", which is not.
//
// UNVERIFIED ON DEVICE: JUCE forwards WKWebView's navigation policy decision to
// pageAboutToLoad (juce_WebBrowserComponent_mac.mm:432-442, read rather than
// assumed), and WKWebView is documented to consult that delegate for custom
// schemes -- that is how OAuth redirect capture works. But nothing here has
// been run in a host yet. If the scheme turns out not to fire, kSentinelScheme
// is the only thing that changes, and parseSentinel already accepts an https
// form for exactly that fallback.
//
// ── The archive is a ZIP, and its shape varies ───────────────────────────────
//
// kromosynth-vi does not serve a directory. artifactManager.js builds a ZIP,
// and which of three shapes you get depends on the requested format:
//
//   single-format sfz   instrument.sfz, samples/, metadata.json, README.txt
//   multi-format        sfz/instrument.sfz, sfz/samples/, plus a dir per format
//   non-sfz primary     the conversion root's contents; NO instrument.sfz
//
// InstrumentLibrary::scan() recognises only the first. So this normalises:
// find the shallowest instrument.sfz, treat its parent as the instrument root,
// extract that subtree only, and refuse an archive that has no SFZ at all --
// sfizz has nothing to open without one, and installing a directory the scan
// silently skips would look like a download that vanished.
//
// ── Atomicity ────────────────────────────────────────────────────────────────
//
// InstrumentLibrary.cpp's own comment asks that the SFZ be written last,
// because scan() skips a directory without one and a half-finished download
// would simply not appear. A directory rename is stronger than that: the
// instrument is absent, then present, with nothing in between. The staging area
// lives INSIDE the library root rather than in /tmp so the rename stays on one
// volume, where it is a rename rather than a copy.

#pragma once

#include "InstrumentLibrary.h"

#include <juce_core/juce_core.h>

#include <functional>
#include <optional>

namespace kromosynth::acquisition
{

/** The scheme the site navigates to. Nothing else in the plugin should
    hard-code this string.
*/
inline constexpr const char* kSentinelScheme = "synthis";

/** Host part of the sentinel, so other future verbs can share the scheme. */
inline constexpr const char* kSentinelAcquireHost = "acquire";

/** A parsed acquisition request. Everything needed to fetch exactly one file.

    Note what is NOT here: no user id, no account token, no quota state. The
    plugin is told "fetch this, with this" and can do nothing else with it
    (WP SS13). Keeping that true is what lets the plugin later gain its own
    credential without this struct or the install path changing.
*/
struct Request
{
    juce::String artifactId;
    juce::String fileName;
    juce::String ticket;

    bool isValid() const noexcept
    {
        return artifactId.isNotEmpty() && fileName.isNotEmpty() && ticket.isNotEmpty();
    }
};

struct Result
{
    bool ok = false;
    juce::String installedId;   // directory name under the library root
    juce::String message;       // human-readable; safe to log and to display
};

/** Recognises a sentinel without parsing it. Cheap enough for the hot path in
    pageAboutToLoad, which sees every navigation.
*/
bool isSentinel (const juce::String& url);

/** Parses a sentinel URL.

    Accepts either

        synthis://acquire?artifact=<id>&file=<name>&ticket=<t>

    or, as the documented fallback if the custom scheme turns out not to reach
    the navigation delegate,

        https://<anything>/__synthis_plugin/acquire?artifact=...&file=...&ticket=...

    Returns nullopt for anything that is not a sentinel or is missing a field.
    A partially-formed sentinel is a bug on the site side, and treating it as
    "not a sentinel" would let the navigation proceed and put the ticket on the
    wire; returning nullopt from a URL that isSentinel() accepted must therefore
    still be blocked by the caller. The two functions are deliberately separate
    so that decision is the caller's and is visible.
*/
std::optional<Request> parseSentinel (const juce::String& url);

/** A form of the URL that is safe to write to a log file.

    WP SS13 flags this specifically: pageAboutToLoad logs every URL it sees, and
    a sentinel carrying a ticket would otherwise land in plaintext in
    ~/Library/Logs/Synth.is/. This keeps the artefact id and the fact of a
    ticket, which is what an operator needs, and drops the value.

    Safe to call on any URL, sentinel or not.
*/
juce::String redactForLog (const juce::String& url);

/** Fetches and installs, synchronously. Blocks; call it off the message thread.

    @param request     what to fetch
    @param baseUrl     e.g. "https://vi.synth.is" -- injected rather than baked
                       in so the probe can point at a local fixture server
    @param libraryRoot where instruments live; InstrumentLibrary::root() in the
                       plugin, a scratch directory under test
    @param onProgress  optional; called with a 0..1 fraction, or -1 when the
                       total size is unknown. Called from the calling thread.
*/
Result fetchAndInstall (const Request& request,
                        const juce::String& baseUrl,
                        const juce::File& libraryRoot,
                        std::function<void (double)> onProgress = {});

/** Installs from an already-downloaded archive. Split out from
    fetchAndInstall so the ZIP-shape normalisation can be tested against
    fixtures without a network, which is the part with three cases and
    therefore the part most likely to be wrong.
*/
Result installFromArchive (const juce::File& archive,
                           const juce::String& artifactId,
                           const juce::File& libraryRoot);

} // namespace kromosynth::acquisition
