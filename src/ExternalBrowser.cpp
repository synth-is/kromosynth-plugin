// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Björn Þór Jónsson
// See ExternalBrowser.h for why this dispatch exists at all, and for the
// measured history of what was tried before presentSafariView.

#include "ExternalBrowser.h"

namespace kromosynth::browser
{

Outcome openExternal (juce::Component& owner,
                       const juce::URL& url,
                       juce::AudioProcessor::WrapperType wrapperType,
                       std::function<void (bool)> onPresented)
{
   #if JUCE_IOS
    if (wrapperType == juce::AudioProcessor::wrapperType_AudioUnitv3)
    {
        if (presentSafariView (owner, url, std::move (onPresented)))
            return Outcome::openedViaSafariView;

        // No live view controller to present from, or a malformed URL.
        // Falling through costs nothing: on a real sandboxed extension
        // process this repeats the existing silent no-op rather than
        // introducing a new failure mode.
    }
   #else
    juce::ignoreUnused (owner, wrapperType, onPresented);
   #endif

    url.launchInDefaultBrowser();
    return Outcome::openedViaSystemBrowser;
}

} // namespace kromosynth::browser
