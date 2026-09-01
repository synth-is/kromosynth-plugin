# Wasmtime.cmake — the Faust engine's runtime.
#
# ── Why this does not follow VENDORING.md ────────────────────────────────────
#
# VENDORING.md permits exactly one mechanism: pinned git submodules consumed by
# add_subdirectory. Wasmtime cannot be had that way. Its C API ships as a
# prebuilt release tarball, and building it from source requires a Rust
# toolchain, which is a far larger imposition on the build than a download.
#
# The rule is overridden deliberately, and the REASON behind it is kept. That
# reason is stated in VENDORING.md: the reference repo it was written against
# declared a submodule, never initialised it, and produced a build that worked
# on one machine and nowhere else. The failure mode is an unpinned or
# unverifiable dependency, not a download as such.
#
# So this pins a specific release and verifies its SHA256. A tarball that does
# not hash correctly fails the configure rather than producing a plugin built
# against something nobody chose.
#
# ── Why this version ─────────────────────────────────────────────────────────
#
# v48.0.1 is not a guess. EXPERIMENT_01_3_DESKTOP_RESULT measured "wasmtime v48"
# from a tarball downloaded 2026-08-24, which is the day v48.0.1 was published,
# and the libwasmtime.a inside this release is BYTE-IDENTICAL to the copy that
# measurement used. Checked rather than inferred from the date. Every RTF, voice
# cap and null test in that document therefore describes this artefact.
#
# ── Why wasmtime rather than the interpreter ─────────────────────────────────
#
# Measured over the 120-elite archive, wasmtime/Cranelift against wasm3 on
# single-voice RTF: min 2.02x, p10 3.26x, median 5.56x, p90 9.41x, max 24.49x.
# In voice caps at the >=4x bar that is a median of 16 voices against 4.
#
# iOS cannot run a JIT and will need an interpreter, so a second backend is
# coming. It is not written yet, and it is not abstracted for yet: FaustEngine
# is the seam, and the abstraction gets built when there is a second real
# consumer rather than an imagined one.

set(KROMOSYNTH_WASMTIME_VERSION "48.0.1")

set(KROMOSYNTH_WASMTIME_SHA256_aarch64
    "9e3c636ed487a41026ff76388c5fa6f3a48ea0968408d033ed4b5e8082c20d69")
set(KROMOSYNTH_WASMTIME_SHA256_x86_64
    "a5d92170718d41e4bd08173049019f0cedb318d0156365a52667d4a35ea3ca69")

if(NOT APPLE)
    message(FATAL_ERROR
        "Wasmtime.cmake currently resolves macOS tarballs only. Linux and "
        "Windows use the same release page and the same layout; add their "
        "hashes here when a build needs them.")
endif()

# The release tarballs are single-architecture. A universal binary would need
# both downloaded and lipo'd together, which nothing needs yet -- but failing
# loudly is better than silently building arm64-only and discovering it on an
# Intel Mac.
list(LENGTH CMAKE_OSX_ARCHITECTURES _kw_arch_count)
if(_kw_arch_count GREATER 1)
    message(FATAL_ERROR
        "CMAKE_OSX_ARCHITECTURES requests ${CMAKE_OSX_ARCHITECTURES}, but the "
        "wasmtime C API tarballs are per-architecture. A universal build needs "
        "both fetched and combined with lipo; that is not implemented.")
endif()

if(CMAKE_OSX_ARCHITECTURES)
    set(_kw_arch "${CMAKE_OSX_ARCHITECTURES}")
else()
    set(_kw_arch "${CMAKE_HOST_SYSTEM_PROCESSOR}")
endif()

if(_kw_arch STREQUAL "arm64" OR _kw_arch STREQUAL "aarch64")
    set(_kw_slug "aarch64")
elseif(_kw_arch STREQUAL "x86_64")
    set(_kw_slug "x86_64")
else()
    message(FATAL_ERROR "unsupported architecture for wasmtime: ${_kw_arch}")
endif()

set(_kw_sha "${KROMOSYNTH_WASMTIME_SHA256_${_kw_slug}}")
set(_kw_name "wasmtime-v${KROMOSYNTH_WASMTIME_VERSION}-${_kw_slug}-macos-c-api")
set(_kw_root "${CMAKE_BINARY_DIR}/_wasmtime")
set(_kw_dir  "${_kw_root}/${_kw_name}")
set(_kw_tar  "${_kw_root}/${_kw_name}.tar.xz")

if(NOT EXISTS "${_kw_dir}/lib/libwasmtime.a")
    message(STATUS "Fetching wasmtime ${KROMOSYNTH_WASMTIME_VERSION} (${_kw_slug})…")
    file(MAKE_DIRECTORY "${_kw_root}")
    file(DOWNLOAD
        "https://github.com/bytecodealliance/wasmtime/releases/download/v${KROMOSYNTH_WASMTIME_VERSION}/${_kw_name}.tar.xz"
        "${_kw_tar}"
        EXPECTED_HASH SHA256=${_kw_sha}
        SHOW_PROGRESS
        STATUS _kw_status)

    list(GET _kw_status 0 _kw_code)
    if(NOT _kw_code EQUAL 0)
        list(GET _kw_status 1 _kw_msg)
        message(FATAL_ERROR "wasmtime download failed: ${_kw_msg}")
    endif()

    file(ARCHIVE_EXTRACT INPUT "${_kw_tar}" DESTINATION "${_kw_root}")
endif()

if(NOT EXISTS "${_kw_dir}/lib/libwasmtime.a")
    message(FATAL_ERROR "wasmtime extracted but libwasmtime.a is not at ${_kw_dir}/lib")
endif()

add_library(wasmtime::wasmtime STATIC IMPORTED GLOBAL)
set_target_properties(wasmtime::wasmtime PROPERTIES
    IMPORTED_LOCATION "${_kw_dir}/lib/libwasmtime.a"
    INTERFACE_INCLUDE_DIRECTORIES "${_kw_dir}/include")

# Security and CoreFoundation are wasmtime's own dependencies, not ours.
target_link_libraries(wasmtime::wasmtime INTERFACE
    "-framework Security" "-framework CoreFoundation")

message(STATUS "wasmtime ${KROMOSYNTH_WASMTIME_VERSION} (${_kw_slug}) at ${_kw_dir}")
