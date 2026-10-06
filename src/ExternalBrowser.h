/*  ExternalBrowser.h — handing a URL to the system browser, for every build
    target this plugin ships as.

    ── Why this exists instead of a bare launchInDefaultBrowser() call ─────────

    juce::URL::launchInDefaultBrowser() is NOT format-neutral on iOS, even
    though it looks like it is from the call site. It bottoms out in
    Process::openDocument, which on iOS calls
    [[UIApplication sharedApplication] openURL:options:completionHandler:].
    UIApplication.sharedApplication is unavailable to an app extension process
    -- there is no UIApplication instance there -- so inside the AUv3 appex
    that call is sent to nil, which Objective-C treats as a silent no-op.
    launchInDefaultBrowser() still returns having "succeeded": nothing in
    JUCE's own code detects the failure. The symptom at the call site
    (PluginEditor.cpp's off-origin fallback, and the checkout sentinel) is
    indistinguishable from a working browser launch: the modal closes, nothing
    opens, nothing logs an error.

    ── CORRECTION, first attempt ────────────────────────────────────────────────

    The first version of this file routed the AUv3 case through
    NSExtensionContext.open(_:completionHandler:) -- the Apple-documented way
    for an app extension to ask the system to open a URL. It compiled, linked,
    found a real non-nil extensionContext on a physical device (iPhone 13 Pro
    Max, iOS 18.7) under a real host (AUM), and called it. MEASURED on that
    device: the completion handler fires with success=NO roughly 1ms after the
    call -- too fast to be the host asking the user anything, which reads as
    the extension point itself rejecting the request rather than any
    particular host declining to cooperate. Recorded here rather than silently
    replaced, because the next person reaching for NSExtensionContext.open()
    for an AU extension should not have to re-discover this.

    ── The fix that actually measured as working ────────────────────────────────

    SFSafariViewController, presented IN-PROCESS on the extension's own
    principal view controller. This asks nothing of the host or the system:
    it is an ordinary presentViewController: call on a view controller that is
    already on screen (because the AU's own view is), and SFSafariViewController
    is a real Safari-backed browser surface -- cookies, Apple Pay JS, the
    works -- running inside our process rather than handing off to a separate
    one. It is also the closest available match to "how this works on the
    plain web", which is the point: no app-switch, no manual step, a checkout
    surface that looks and behaves like a real browser because it is one.

    Every other wrapper type -- macOS Standalone/AU/VST3, iOS Standalone -- is
    a real app process with a working UIApplication/NSWorkspace, so
    launchInDefaultBrowser() already does the right thing there and nothing
    about this header changes that path.

    ExternalBrowser_iOS.mm carries the Objective-C++ half of this (iOS build
    only); this header is the cross-platform entry point everything else calls.
*/

#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace kromosynth::browser
{

/** Which mechanism openExternal actually used. Logged by every call site
    rather than left implicit -- see the header comment above on why a silent
    success here is exactly the bug this file exists to end. A log line
    reading openedViaSystemBrowser on a real AUv3 appex means the Safari view
    could not be presented and the old silent-no-op is still live; that is the
    one outcome worth watching for on a physical-device test.
*/
enum class Outcome
{
    openedViaSafariView,     ///< iOS AUv3: SFSafariViewController presented in-process.
    openedViaSystemBrowser   ///< Everything else, and AUv3 as a last-resort fallback.
};

/** Opens `url` in the system browser, choosing the mechanism that actually
    works for `wrapperType` on this platform.

    `owner` only needs to be A component that is already on screen and has a
    live peer -- on iOS AUv3 it is walked up to find the extension's principal
    view controller, which is where the Safari view gets presented from (see
    presentSafariView). Any visible component in the plugin's own hierarchy
    does; PluginEditor.cpp passes the browser component itself since that is
    what is calling this from pageAboutToLoad.

    `onPresented`, if given, is called with whether the modal presentation's
    own animation actually completed -- confirmation that something real
    landed on screen, not just that this function attempted it. ONLY called on
    the openedViaSafariView path, and asynchronously (UIKit's own
    presentation animation). Never called on openedViaSystemBrowser --
    launchInDefaultBrowser() has no completion signal to relay, which is the
    whole problem this header opens with.
*/
Outcome openExternal (juce::Component& owner,
                       const juce::URL& url,
                       juce::AudioProcessor::WrapperType wrapperType,
                       std::function<void (bool)> onPresented = {});

#if JUCE_IOS

/** iOS only. Walks the UIKit responder chain up from `owner`'s native peer
    view to find a live view controller, and presents an SFSafariViewController
    on `url` modally from it.

    Returns false (and does nothing else) when no live peer/view controller is
    found, or the URL could not be turned into an NSURL -- callers should fall
    back to launchInDefaultBrowser() on false rather than treating it as an
    error.
*/
bool presentSafariView (juce::Component& owner,
                         const juce::URL& url,
                         std::function<void (bool)> onPresented = {});

#endif

} // namespace kromosynth::browser
