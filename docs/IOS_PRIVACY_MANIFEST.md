# iOS privacy manifest (synthis-59r.17)

One file, `resources/ios/PrivacyInfo.xcprivacy`, attached to both bundles in
`src/CMakeLists.txt` (`KromosynthPlugin_Standalone` and `KromosynthPlugin_AUv3`).
`scripts/ios-build.sh` fails if either `Synth.is.app` or
`PlugIns/Synth.is.appex` lacks `PrivacyInfo.xcprivacy` at its root. Both targets
link the same shared-code target, so the declarations are identical.

Not verified on a build: this worktree has no Xcode generator, so the CMake
attachment (`MACOSX_PACKAGE_LOCATION Resources`, which CMake flattens to the
bundle root on iOS) is untested until `./scripts/ios-build.sh` runs on a machine
with Xcode. The script's check is what will say.

## Tracking and data collected

- `NSPrivacyTracking` false, no tracking domains.
- `NSPrivacyCollectedDataTypes` empty. The native code collects nothing: no
  account, purchase or analytics code exists in `src/`. Sign-in and checkout
  happen on synth.is, in a WebBrowserComponent / SFSafariViewController
  (`ExternalBrowser_iOS.mm`), and `synthis://auth` / checkout sentinels carry only
  a URL or ticket. The plugin has no StoreKit use. If native sign-in or in-app
  purchase is added, revisit this.
- Log files (`PluginProcessor.cpp` logFile_) stay on device; URLs are redacted.

## Required-reason APIs declared

| Category | Reason | Traced to |
|---|---|---|
| FileTimestamp | C617.1 (files inside the app / app-group container) | `RenderList.cpp:32` and `RenderOutbox.cpp:26-27,332` call `File::getLastModificationTime`; JUCE's `juce_SharedCode_posix.h` `juce_stat` / `getattrlist` (line ~186, ~676) back every `File` query. Paths are the app group container (`InstrumentLibrary.cpp:55`) and Documents (`RenderOutbox.cpp:58`). |
| SystemBootTime | 35F9.1 (elapsed time between in-app events) | `Time::getHighResolutionTicks` / `getMillisecondCounterHiRes` (`mach_absolute_time`, `juce_SystemStats_mac.mm:337-356`), used in `PluginProcessor.cpp` (block-load and note timing), `FaustEngine.cpp:1050`, `SamplerEngine.cpp:165`; `[NSProcessInfo systemUptime]` in `juce_UIViewComponentPeer_ios.mm:2241`. |
| UserDefaults | CA92.1 (app's own data) | Not called from `src/`. JUCE's `WebBrowserComponent::clearCookies` (`juce_WebBrowserComponent_mac.mm:1181`) references `[NSUserDefaults standardUserDefaults]` and is compiled into both bundles (`JUCE_WEB_BROWSER=1`). Declared because the symbol is linked, not because we call it. |

## Deliberately not declared

- DiskSpace: JUCE's `statfs` wrapper (`juce_doStatFS`) backs
  `File::getBytesFreeOnVolume` and friends; nothing in `src/` calls them.
  If a future feature checks free space, add category DiskSpace (E174.1 or
  85F4.1).
- ActiveKeyboards: no use of `UITextInputMode.activeInputModes` found in `src/`.

Apple's upload-time scan is the arbiter; if it reports an undeclared category,
find the use, add the entry here and in the file.
