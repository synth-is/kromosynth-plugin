// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Björn Þór Jónsson
// wclap-host-probe only (synthis-et4.4): take a picture of the WKWebView inside a JUCE peer's NSView, so a
// run can LOOK at what the engine's page drew instead of inferring it from counters. Never shipped.
#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>

#include <atomic>
#include <cstdio>

static std::atomic<int> gState { 0 };   // 0 idle, 1 waiting, 2 done ok, 3 failed

// The nth WKWebView in depth-first order (the editor holds the shell, the site and the engine's page).
static WKWebView* findWebView (NSView* v, int& n)
{
    if ([v isKindOfClass: [WKWebView class]]) { if (n == 0) return (WKWebView*) v; --n; }
    for (NSView* s in v.subviews)
        if (auto* w = findWebView (s, n)) return w;
    return nil;
}

extern "C" bool wclapProbeSnapshotStart (void* nsView, int nth, const char* pngPath, int* widthOut, int* heightOut)
{
    @autoreleasepool
    {
        int n = nth;
        auto* wv = findWebView ((__bridge NSView*) nsView, n);
        if (wv == nil) return false;
        *widthOut = (int) wv.bounds.size.width;
        *heightOut = (int) wv.bounds.size.height;
        NSString* path = [NSString stringWithUTF8String: pngPath];
        gState = 1;
        [wv takeSnapshotWithConfiguration: nil completionHandler: ^(NSImage* image, NSError* error)
        {
            if (image == nil) { std::printf ("SNAPSHOT_ERROR=%s\n", error.localizedDescription.UTF8String); gState = 3; return; }
            NSBitmapImageRep* rep = [[NSBitmapImageRep alloc] initWithData: [image TIFFRepresentation]];
            NSData* png = [rep representationUsingType: NSBitmapImageFileTypePNG properties: @{}];
            gState = [png writeToFile: path atomically: YES] ? 2 : 3;
        }];
        return true;
    }
}

extern "C" int wclapProbeSnapshotState() { return gState.load(); }
