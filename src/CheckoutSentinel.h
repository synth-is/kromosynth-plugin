/*  CheckoutSentinel.h — "open this URL in a real browser", carried over the
    one channel that exists.

    HEADER-ONLY, so it needs no CMakeLists change.

    ── Why a third verb instead of folding into acquisition ────────────────────

    Billing has nothing in common with acquisition's payload: there is no
    artefact id, no ticket, no file to fetch -- only a destination URL the site
    wants opened in the system browser (BillingService.js's openBillingUrl(),
    the mechanism behind both the pricing page's Subscribe button and the
    upgrade modal's See Plans). Acquisition's Verb enum exists to choose
    between two FETCH destinations (the instrument library vs the render
    outbox); it has no slot for "don't fetch anything, just hand this URL to
    the OS", and bolting one on would make fetchAndInstall/fetchRender need to
    cope with a verb that calls neither.

    ── The greedy-match trap, again ─────────────────────────────────────────────

    Exactly the bug AuthSentinel.h documents for `synthis://auth`:
    acquisition::isSentinel matches ANY `synthis://` URL, and parseSentinel
    returns nullopt for the unrecognised "checkout" host -- so a checkout
    sentinel reaching the acquisition check first is blocked (right) and
    logged as `acquisition_sentinel_malformed` (wrong, and, before this header
    existed, exactly what happened on every build target: Standalone, AU,
    VST3, AUv3 alike. `synthis://checkout?url=...` never reached
    launchInDefaultBrowser; it was swallowed as a malformed download request).
    This check must run BEFORE the acquisition check in pageAboutToLoad, same
    as auth's.

    ── What this does with the URL ──────────────────────────────────────────────

    Nothing but open it. No token, no credential, nothing the plugin acts on --
    the destination is the site's own page (https://synth.is/pricing or
    similar) and the plugin's only job is handing it to the system browser via
    ExternalBrowser.h, which is where the AUv3-vs-everything-else split lives
    (see that header for why launchInDefaultBrowser() alone is not enough
    inside the iOS app extension).
*/

#pragma once

#include "ArtifactAcquisition.h"

#include <juce_core/juce_core.h>

#include <optional>

namespace kromosynth::checkout
{

/** The verb. `synthis://checkout?url=...`, alongside acquisition's
    `synthis://acquire`/`synthis://render` and auth's `synthis://auth`.
*/
inline constexpr const char* kCheckoutHost = "checkout";

/** True for `synthis://checkout`, false for every other sentinel verb.

    Matches the host segment rather than a prefix, for the same reason
    AuthSentinel.h does: `synthis://checkoutsomething` must not slip through.
*/
inline bool isCheckoutSentinel (const juce::String& url)
{
    const juce::String prefix = juce::String (acquisition::kSentinelScheme) + "://";

    if (! url.startsWithIgnoreCase (prefix))
        return false;

    const auto afterScheme = url.fromFirstOccurrenceOf ("://", false, false);
    const auto host = afterScheme.upToFirstOccurrenceOf ("?", false, false)
                                 .upToFirstOccurrenceOf ("/", false, false);

    return host.equalsIgnoreCase (kCheckoutHost);
}

/** Parses the destination out of a checkout sentinel, or nullopt if it is not
    one or is malformed.

    NULLOPT MUST STILL BE BLOCKED by the caller -- same contract as
    acquisition::parseSentinel and auth::parse. Requires `https://`: the
    decoded value reaches launchInDefaultBrowser/NSExtensionContext.open
    directly, and accepting any scheme here would hand a browser-launch call
    whatever the embedded page sent, with no check anywhere downstream.
*/
inline std::optional<juce::String> parse (const juce::String& url)
{
    if (! isCheckoutSentinel (url))
        return std::nullopt;

    const juce::URL parsed (url);

    const auto idx = parsed.getParameterNames().indexOf ("url", true);
    if (idx < 0)
        return std::nullopt;

    const auto destination = parsed.getParameterValues()[idx];

    if (! destination.startsWithIgnoreCase ("https://"))
        return std::nullopt;

    return destination;
}

} // namespace kromosynth::checkout
