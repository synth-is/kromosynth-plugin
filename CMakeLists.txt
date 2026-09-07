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
option(KROMOSYNTH_BUILD_FAUST    "Build the wasmtime Faust engine (Stage 2)" OFF)

# The interpreter backend. OFF on macOS, where Cranelift is legal and 5.56x
# faster; REQUIRED on iOS, where JIT is not permitted at all. Selecting it swaps
# the runtime inside FaustEngine.cpp and changes nothing else -- voice logic,
# levelling, trim and envelope are shared, deliberately: duplicating the
# note-start path is how pitch correction and the envelope came to disagree once
# already (FaustEngine.h).
#
# Acceptance is scripts/null-test.sh. Both runtimes execute the same module
# bytes and share the math expressions, so bit-identical output is the
# expectation rather than the hope.
option(KROMOSYNTH_WASM3          "Use the wasm3 interpreter for the Faust engine" OFF)
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

# ── Whether a build installs itself ──────────────────────────────────────────
#
# ON reproduces the historical behaviour exactly: JUCE copies each built bundle
# to ~/Library/Audio/Plug-Ins, and cmake/Signing.cmake re-copies over it.
#
# OFF exists for compile verification. A plugin build otherwise overwrites the
# installed bundle and, with KROMOSYNTH_SIGN_IDENTITY empty, replaces a
# Developer ID signed and notarised copy with an ad-hoc one -- silently, as a
# side effect of checking whether the code compiles. That is a destructive
# default for a question as small as "does it build".
#
# Turning this OFF also suppresses the signing re-copy, because that step is
# guarded on JUCE_PLUGIN_COPY_DIR, which JUCE only sets when the copy is on.
option(KROMOSYNTH_COPY_AFTER_BUILD
       "Copy built plugin bundles into ~/Library/Audio/Plug-Ins" ON)

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

# Where the plugin fetches artefacts from, using the ticket the site hands it
# over the sentinel URL (WP_SYNTHIS_PLUGIN SS13). A separate host from the web
# app, and read from nginx.conf's vi.synth.is server block rather than guessed.
#
# A cache variable so acquire-probe and a local kromosynth-vi can be pointed at
# each other without editing code.
set(KROMOSYNTH_VI_SERVICE_URL "https://vi.synth.is" CACHE STRING
    "kromosynth-vi origin the plugin downloads instrument archives from")

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

# ── Platform: macOS, and iOS as a second backend ─────────────────────────────
#
# iOS is not a port of the macOS build. Three measured facts shape it, all from
# GATE0_IOS_RESULT_2026-09-07 and INTERP_BACKEND_RESULT_2026-09-07 in
# faust-genome-substrate:
#
#   1. wasmtime cannot ship. Its C API exposes Cranelift and Winch, both JIT,
#      and iOS forbids JIT for third-party code. KROMOSYNTH_BUILD_FAUST is
#      therefore refused on iOS until FaustEngine.cpp is re-backed onto wasm3 —
#      which FaustEngine.h is already shaped for, since it names no runtime
#      type anywhere in its 496 lines.
#
#   2. The sampler is the PRIMARY iOS engine, not a fallback. wasm3 on an A15
#      gives one to two voices at the slow end of the catalogue, and the Faust
#      interpreter measured half of wasm3's throughput, so there is no faster
#      legal runtime to wait for. A sampled rendition has no realtime factor at
#      all.
#
#   3. sfizz needs no new porting work. Building it for arm64-ios required
#      exactly the two things cmake/Sfizz.cmake already does for macOS arm64:
#      the PROJECT_SYSTEM_PROCESSOR override and the atomic_queue warning
#      demotion.
#
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(KROMOSYNTH_IOS ON)
else()
    set(KROMOSYNTH_IOS OFF)
endif()

# Feature probes have to stop trying to link executables.
#
# try_compile builds an EXECUTABLE by default, and an iOS executable cannot be
# linked without a bundle and a signing identity. So every probe fails for a
# reason that has nothing to do with what it was probing. FindThreads is the
# first casualty and the most misleading one: it reports "Could NOT find
# Threads" on a platform where pthreads is not optional and is in the SDK.
#
# Probing with a static library removes the link step, and the probes then
# answer the question they were actually asked. Set after project() on purpose:
# compiler detection has already run by here and does need to link.
if(KROMOSYNTH_IOS)
    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
endif()

# FindThreads has to be told the answer, and this is an ASSERTION rather than a
# probe — which is why it is written down.
#
# sfizz does find_package(Threads REQUIRED) in cmake/SfizzDeps.cmake:4. Under
# the Xcode generator targeting iOS, FindThreads' CMAKE_HAVE_LIBC_PTHREAD check
# fails, and it then goes looking for a libpthread to link, which does not
# exist — on iOS, exactly as on macOS, pthreads lives in libSystem and there is
# no separate library. The result is "Could NOT find Threads" on a platform
# where threads are not optional. CMAKE_TRY_COMPILE_TARGET_TYPE does not help;
# the check fails inside xcodebuild for reasons unrelated to pthreads.
#
# CMAKE_HAVE_LIBC_PTHREAD is the ONE variable to set. Seeding the downstream
# results instead does not work: FindThreads recomputes Threads_FOUND from
# CMAKE_THREAD_LIBS_INIT or CMAKE_HAVE_LIBC_PTHREAD and overwrites anything
# preset. This is the cached result of the check, so setting it both skips the
# check and states the true fact — libc does have pthread here — rather than
# faking a conclusion further down the chain.
#
# CMAKE_THREAD_LIBS_INIT stays EMPTY on purpose: there is genuinely nothing to
# link, which is the same answer the probe reaches on macOS.
#
# If a future SDK makes the real probe pass, delete this block rather than
# leaving it — a stale assertion is worse than none.
if(KROMOSYNTH_IOS)
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    set(CMAKE_HAVE_LIBC_PTHREAD 1 CACHE INTERNAL "" FORCE)
    set(CMAKE_THREAD_LIBS_INIT "" CACHE STRING "" FORCE)
endif()

# Apple platforms only. Fail loudly rather than producing a half-configured
# build on another host.
if(NOT APPLE)
    message(FATAL_ERROR
        "kromosynth-plugin targets Apple platforms only: macOS (AU + VST3) and "
        "iOS (Standalone + AUv3). Windows and Linux are Stage 3 — see "
        "WP_SYNTHIS_PLUGIN.md §7.")
endif()

# The Faust engine needs an interpreter on iOS. wasmtime's C API offers only
# Cranelift and Winch, both JIT, which iOS forbids -- so the engine is available
# there only with KROMOSYNTH_WASM3. A hard error rather than a silent downgrade:
# a build that quietly dropped the engine would produce an app whose instrument
# library shows Faust artefacts it cannot play.
#
# Worth the wasm3 build: measured across all 120 archive elites on an A15, 60%
# carry four voices or more and 43% carry eight
# (IOS_VOICE_CAP_DISTRIBUTION_2026-09-07.md).
if(KROMOSYNTH_IOS AND KROMOSYNTH_BUILD_FAUST AND NOT KROMOSYNTH_WASM3)
    message(FATAL_ERROR
        "On iOS the Faust engine requires -DKROMOSYNTH_WASM3=ON. wasmtime's C "
        "API offers only Cranelift and Winch and both are JIT, which iOS does "
        "not permit. wasm3 interprets the same module bytes and is legal there.")
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
    if(KROMOSYNTH_IOS)
        message(FATAL_ERROR
            "KROMOSYNTH_UNIVERSAL_BINARY is meaningless on iOS: every supported "
            "device is arm64. Configure without it.")
    endif()
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()

# Deployment target is per-platform, and the two number spaces are unrelated:
# "11.0" means macOS Big Sur and iOS 11. The iOS floor is 16.0 — AUv3 audio
# units and the App Group container work the extension will need are both
# well-trodden there, and an iOS 11 device would not be worth aiming an audio
# interpreter at.
if(KROMOSYNTH_IOS)
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "16.0" CACHE STRING "" FORCE)
else()
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
endif()

# There is no ~/Library/Audio/Plug-Ins on a phone. JUCE's copy step and the
# signing re-copy that is guarded on it are both nonsense on iOS, where
# installation happens through devicectl or Xcode.
if(KROMOSYNTH_IOS)
    set(KROMOSYNTH_COPY_AFTER_BUILD OFF CACHE BOOL "" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(KromosynthOptions)
include(JuceLicenceCheck)
include(Signing)

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

# Before src/, so wasmtime::wasmtime exists when the plugin target links it.
if(KROMOSYNTH_BUILD_FAUST)
    include(Wasmtime)
    include(Wasm3)
endif()

add_subdirectory(src)

# ── Developer tools (never shipped) ──────────────────────────────────────────
# sfz-probe answers the Task 2 format questions without a plugin, a host or an
# audio device. It is built with the sampler because it links sfizz; it is not
# part of any deliverable. See tools/sfz-probe/main.cpp.
#
# All three probes are console executables. On iOS there is no console to run
# them from, so they are skipped rather than turned into app bundles nobody
# would launch. They are host-side tools and the host is still a Mac.
if(KROMOSYNTH_BUILD_SAMPLER AND NOT KROMOSYNTH_IOS)
    add_subdirectory(tools/sfz-probe)
    add_subdirectory(tools/engine-probe)
endif()

# ── wasmtime, the Faust engine's runtime (Stage 2, off by default) ───────────
#
# See cmake/Wasmtime.cmake for the pin, the SHA256 verification, and why this
# deliberately does not follow VENDORING.md.
if(KROMOSYNTH_BUILD_FAUST)
    add_subdirectory(tools/faust-probe)
endif()

# acquire-probe links juce_core only -- no sfizz, no webview, no audio device --
# so it is not gated behind the sampler. It exists to exercise the acquisition
# path without building the plugin, because a plugin rebuild re-signs and
# un-notarises the installed bundle (STAGE0_FINDINGS L5, L7). Host-side only,
# for the same reason as the probes above.
if(NOT KROMOSYNTH_IOS)
    add_subdirectory(tools/acquire-probe)
endif()
