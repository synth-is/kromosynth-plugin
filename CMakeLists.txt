cmake_minimum_required(VERSION 3.24)

# ── Kromosynth Plugin ────────────────────────────────────────────────────────
#
# Stage 0 of WP_SYNTHIS_PLUGIN.md §7. The repository is `kromosynth-plugin`
# from the start because probe names stick; but only the BUILD LAYER here is
# written to be kept:
#
#   KEEPABLE   CMakeLists.txt, cmake/, JUCE + sfizz vendoring, target config, CI
#   DISPOSABLE everything under src/ — see the header comment in each file
#
# Do not grow an abstraction layer in src/ before there is a second engine to
# abstract over. §3's "two engines, one shell" is a document-level idea; it is
# not a C++ class hierarchy yet.

project(KromosynthPlugin VERSION 0.0.1 LANGUAGES C CXX)

# ── Options ──────────────────────────────────────────────────────────────────
option(KROMOSYNTH_BUILD_SAMPLER  "Build the sfizz sampler engine (Task 2)" OFF)
option(KROMOSYNTH_WEBVIEW_PROBE  "Inject the Stage 0 environment probe JS"  ON)

# How long after page load the probe fires. Default 3000 ms is enough for React
# to mount.
#
# Raise it to run the editor-lifetime test. The probe schedules a timer against
# the editor; the bug it guards against is the editor being destroyed while
# that timer is pending. With a 3 s delay, reproducing that means closing the
# plugin window within 3 seconds by hand, which is a flaky test that a human
# will usually fail to hit. With -DKROMOSYNTH_PROBE_DELAY_MS=30000, any normal
# open-then-close reliably lands inside the window and the guard is exercised
# every time.
set(KROMOSYNTH_PROBE_DELAY_MS "3000" CACHE STRING
    "Milliseconds after page load before the Stage 0 probe fires")

# §A3.1c discriminator. Replaces the WebBrowserComponent with a plain
# juce::Component, leaving resize configuration identical, to determine whether
# the AU editor's refusal to resize is caused by the native NSView embedding
# (upstream JUCE) or by our own editor configuration (ours). See
# docs/STAGE0_FINDINGS.md §A3.1c.
option(KROMOSYNTH_EDITOR_STUB "Build editor with a plain component instead of the webview" OFF)

# §A3.1c occlusion test. Pixels trimmed from the bottom and right of the
# browser's bounds so JUCE's ResizableCornerComponent (18x18) is not covered by
# the native WKWebView NSView. 0 = off. 24 exposes the grip with margin.
# Diagnostic only — not a workaround being adopted.
set(KROMOSYNTH_BROWSER_INSET "0" CACHE STRING
    "Pixels to reserve at bottom/right so JUCE's resize grip is not occluded")

# The origin the WebBrowserComponent points at. Sourced from kromosynth-auth's
# .env.example (APP_PUBLIC_URL=https://synth.is), not guessed.
set(KROMOSYNTH_WEB_APP_URL "https://synth.is" CACHE STRING
    "Deployed synth.is web app origin loaded into the plugin webview")

# The instrument the sampler engine loads, compiled in as a define.
#
# A cache variable rather than a code edit, so switching artefacts is a
# reconfigure. Deliberately NOT unified with the sfz-probe harness, which takes
# argv: the whole point of the dump is comparing two artefacts, and a define
# would make that two reconfigures. See STAGE0_FINDINGS.md F1.
#
# Empty is a valid configuration and means "build the engine, load nothing".
# The plugin logs that and plays silence rather than failing.
set(KROMOSYNTH_SFZ_PATH "" CACHE STRING
    "Absolute path to the .sfz instrument the sampler engine loads")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# macOS only for Stage 0 (WP constraint: AU + VST3, no Windows/Linux/iOS/CLAP).
# Fail loudly rather than producing a half-configured build on another host.
if(NOT APPLE)
    message(FATAL_ERROR
        "kromosynth-plugin is macOS-only at Stage 0 (AU + VST3). "
        "Windows and Linux are Stage 3 — see WP_SYNTHIS_PLUGIN.md §7.")
endif()

# Architecture. Default is NATIVE ONLY, deliberately.
#
# A universal arm64+x86_64 build roughly doubles the JUCE compile, and JUCE's
# first build is already the slow part. Every host in the Stage 0 matrix
# (Logic, Live 12, Reaper, Bitwig) runs arm64-native on Apple silicon, so a
# universal binary buys nothing for local host testing and costs several
# minutes on every clean configure.
#
# Turn it on for anything that leaves this machine.
option(KROMOSYNTH_UNIVERSAL_BINARY "Build arm64 + x86_64 universal" OFF)

if(KROMOSYNTH_UNIVERSAL_BINARY)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(KromosynthOptions)
include(JuceLicenceCheck)

# ── JUCE ─────────────────────────────────────────────────────────────────────
# Vendored as a pinned git submodule. One dependency mechanism only: no
# FetchContent, no CPM, no find_package. See docs/VENDORING.md for why.
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/JUCE/CMakeLists.txt")
    message(FATAL_ERROR
        "third_party/JUCE is empty. Run:\n"
        "    git submodule update --init --recursive\n"
        "(The agentic-synth reference repo has this exact submodule declared "
        "and never initialised, which is how a build stops being reproducible.)")
endif()

# Assert the licence BEFORE configuring any target against it. WP §0 asks for
# this to be verified "before the first public build rather than after"; making
# it a configure-time failure is cheaper than remembering.
kromosynth_assert_juce_licence("${CMAKE_CURRENT_SOURCE_DIR}/third_party/JUCE")

add_subdirectory(third_party/JUCE)

# ── sfizz (Task 2, off by default) ───────────────────────────────────────────
if(KROMOSYNTH_BUILD_SAMPLER)
    include(Sfizz)
endif()

add_subdirectory(src)

# ── Developer tools (never shipped) ──────────────────────────────────────────
# sfz-probe answers the Task 2 format questions without a plugin, a host or an
# audio device. It is built with the sampler because it links sfizz; it is not
# part of any deliverable. See tools/sfz-probe/main.cpp.
if(KROMOSYNTH_BUILD_SAMPLER)
    add_subdirectory(tools/sfz-probe)
endif()
