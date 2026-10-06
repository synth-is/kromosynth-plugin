/*  ExternalBrowser_iOS.mm — the Objective-C++ half of ExternalBrowser.h.
    iOS only; see that header for why it exists and what was tried before this.

    ── Finding a view controller to present from, without a JUCE API for it ────

    Same technique the abandoned NSExtensionContext attempt used, documented
    in that header's CORRECTION note: the editor's own native peer view
    (Component::getPeer()->getNativeHandle()) sits inside a UIView hierarchy
    that UIKit's responder chain connects, for free, to whichever
    UIViewController actually owns it. Walking responder.nextResponder up from
    that view until the first UIViewController reaches a real, on-screen
    controller regardless of wrapper type -- the AUv3's own principal
    AUViewController, or the Standalone app's root controller. Unlike the
    extensionContext property, there is no further runtime check needed here:
    ANY on-screen view controller can call presentViewController: on itself,
    and UIKit walks up to whichever ancestor is actually presenting right now.

    ── Why a static delegate rather than one held by the caller ─────────────────

    SFSafariViewController.delegate is `weak`. Something has to keep the
    delegate alive for the lifetime of the presented sheet, and the call site
    in PluginEditor.cpp returns immediately -- it does not keep the plugin's
    editor hierarchy pinned for however long the user spends on a checkout
    page. A single process-lifetime static is enough: only one checkout sheet
    is ever meaningfully open at a time, and safariViewControllerDidFinish:
    dismissing it on the SAME object the next presentation reuses is simpler
    than threading ownership through the call chain for no behavioural gain.
*/

#include "ExternalBrowser.h"

#import <SafariServices/SafariServices.h>
#import <UIKit/UIKit.h>

@interface KromosynthSafariDelegate : NSObject <SFSafariViewControllerDelegate>
@end

@implementation KromosynthSafariDelegate
- (void) safariViewControllerDidFinish: (SFSafariViewController*) controller
{
    [controller dismissViewControllerAnimated: YES completion: nil];
}
@end

namespace kromosynth::browser
{

bool presentSafariView (juce::Component& owner,
                         const juce::URL& url,
                         std::function<void (bool)> onPresented)
{
    auto* peer = owner.getPeer();
    if (peer == nullptr)
        return false;

    UIView* view = (UIView*) peer->getNativeHandle();
    if (view == nil)
        return false;

    UIResponder* responder = view;
    UIViewController* found = nil;

    while (responder != nil)
    {
        if ([responder isKindOfClass: [UIViewController class]])
        {
            found = (UIViewController*) responder;
            break;
        }

        responder = [responder nextResponder];
    }

    if (found == nil)
        return false;

    NSString* nsUrlString = [NSString stringWithUTF8String: url.toString (true).toRawUTF8()];
    NSURL* nsUrl = nsUrlString != nil ? [NSURL URLWithString: nsUrlString] : nil;

    if (nsUrl == nil)
        return false;

    // See the file header on why this is a static rather than owned by the
    // caller.
    static KromosynthSafariDelegate* delegate = nil;
    if (delegate == nil)
        delegate = [[KromosynthSafariDelegate alloc] init];

    SFSafariViewController* safari = [[SFSafariViewController alloc] initWithURL: nsUrl];
    safari.delegate = delegate;

    [found presentViewController: safari animated: YES completion: ^
    {
        if (onPresented)
            onPresented (true);
    }];

    return true;
}

} // namespace kromosynth::browser
