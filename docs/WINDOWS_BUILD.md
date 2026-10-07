# Windows build steps

Prerequisites for building the Synth.is plugin on Windows.  All paths refer to
a 64-bit Windows 10 or later machine (x86_64).

---

## 0. Toolchain — 5 minutes

Install (or verify) the following:

| Item | Minimum version | Notes |
|---|---|---|
| Visual Studio 2022 Build Tools | 17.x | The "Desktop development with C++" workload. |
| CMake | 3.24+ | 3.19+ is required for `file(ARCHIVE_EXTRACT)` on `.zip`. |
| Git | latest | For submodule checkout. |

Confirm:

```bat
cmake --version
cl /?
git --version
```

---

## 1. Initialise the repository — 1 minute

```bat
cd C:\Users\<you>\Developer\apps\synth.is\kromosynth-plugin
git init
git add .
git commit -m "Stage 0: build layer for the Synth.is plugin shell"
```

Do not push.  (Session constraint.)

---

## 2. Vendor JUCE — 3–10 minutes

Use the latest 9.x (9.0.1 at time of writing; check for newer).

```bat
cd C:\Users\<you>\Developer\apps\synth.is\kromosynth-plugin
git clone --depth 1 --branch 9.0.1 ^
  https://github.com/juce-framework/JUCE.git third_party\JUCE
git submodule add https://github.com/juce-framework/JUCE.git third_party\JUCE
git add .gitmodules third_party\JUCE
git commit -m "Vendor JUCE 9.0.1 as a pinned submodule"
```

---

## 3. WebView2 — 2 minutes

The plugin uses JUCE's WebView2 integration (`NEEDS_WEBVIEW2 TRUE` and
`JUCE_USE_WIN_WEBVIEW2=1`).  On Windows the Microsoft.Web.WebView2 NuGet
package must be available to CMake.

**Install the NuGet package** (one of these methods):

```bat
:: Option A: via NuGet CLI (if installed)
nuget install Microsoft.Web.WebView2 -OutputDirectory third_party\WebView2

:: Option B: via PowerShell / dotnet
dotnet add package Microsoft.Web.WebView2 --version 1.0.2643.38
```

**Or install the runtime** (for testing, not just building):

```bat
:: Download the standalone installer from:
:: https://developer.microsoft.com/en-us/microsoft-edge/webview2/
:: Run WebView2OfflineInstaller.exe as Administrator.
```

JUCE's CMake integration looks for the package in the standard NuGet layout:
`third_party\WebView2\lib\native\include` for headers and
`third_party\WebView2\runtimes\win-x64\Microsoft.Web.WebView2.Core.dll` for
the runtime DLL.

---

## 4. Configure — and this answers the licence question

```bat
cmake --preset stage0
```

**Read the output.** Two lines matter:

```
-- JUCE 8.x.x — module declares license: <whatever it says>
-- JUCE licence assertion passed: module header mentions AGPL
```

If instead you get a `FATAL_ERROR` from `kromosynth_assert_juce_licence`, that
is the answer to `WP_SYNTHIS_PLUGIN.md` §0's open question, and it is a finding
to record — **not a check to delete.**

---

## 5. Build — 15–40 minutes the first time

```bat
cmake --build --preset stage0
```

JUCE compiles once and then caches.  Subsequent builds of just the plugin
sources are seconds.

`COPY_PLUGIN_AFTER_BUILD TRUE` installs to:

- `%ProgramFiles%\Synth.is\Synth.is.vst3` (VST3)
- `%ProgramFiles%\Synth.is\Synth.is.dll` (AU, on Windows AU is not available)

---

## 6. Runtime: wasmtime.dll

The wasmtime C-API ships as a dynamic library (`wasmtime.dll`) alongside the
import library (`wasmtime.lib`) that we link against.  At runtime the host
process must be able to find `wasmtime.dll`.

**Ship the DLL alongside your VST3 bundle:**

```
Synth.is.vst3/
  Contents/
    x86_64/
      Synth.is
    wasmtime.dll        ← copy here
```

Or add the wasmtime extract directory to the system `PATH` before launching
your DAW.

---

## 7. If a host crashes

Capture the crash dump.  On Windows, the Windows Error Reporting dialog can
save a dump; alternatively use ProcDump:

```bat
procdump -ma <pid> crash.dmp
```

What matters is whether the crashing thread is in `WebView2` /
`com.microsoft.WebKit.WebContent`, or in JUCE, or in the host.

---

## What NOT to do at this stage

- Do not add native integration, a resource provider, or a JS bridge.  Both
  change the page's origin semantics and would invalidate the probe.
- Do not sign or notarise.  Stage 1.
- Do not touch `third_party/sfizz` — Task 2 is gated on Task 1 passing.
- Do not "fix" anything you find.  Record it.
