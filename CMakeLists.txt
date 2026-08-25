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

# The origin the WebBrowserComponent points at. Sourced from kromosynth-auth's
# .env.example (APP_PUBLIC_URL=https://synth.is), not guessed.
set(KROMOSYNTH_WEB_APP_URL "https://synth.is" CACHE STRING
    "Deployed synth.is web app origin loaded into the plugin webview")

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
