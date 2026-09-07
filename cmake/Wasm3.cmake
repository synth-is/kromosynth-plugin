# Wasm3.cmake — the interpreter backend, for platforms that forbid JIT.
#
# WHY A SECOND RUNTIME EXISTS AT ALL
#
# wasmtime's C API offers exactly two strategies, Cranelift and Winch, and both
# are JIT compilers. iOS forbids third-party JIT: the dynamic-codesigning
# entitlement is Apple's own, and an interoperability request to extend the EU
# browser-engine exception beyond browsers was refused. So the same
# instrument.wasm has to be INTERPRETED there. See
# faust-genome-substrate/GATE0_IOS_RESULT_2026-09-07.md.
#
# wasm3 was chosen by measurement rather than by availability, against three
# alternatives, all on this machine and these genomes:
#
#   wasm3                    1.00x   chosen
#   WAMR fast-interp        ~0.77x   slower on every row (EXPERIMENT_01_3)
#   Faust interpreter (D')   0.49x   INTERP_BACKEND_RESULT_2026-09-07
#   wasmtime/Pulley             --   its own docs expect ~10x vs Cranelift,
#                                    worse than wasm3's measured 5.56x
#
# It is also the only candidate that preserves bit-identity by construction: it
# executes the same module bytes the platform emitted, so ARTEFACT_CONTRACT
# §3.2's trust in peak, trim, articulation and hasGate carries over unchanged.
#
# VENDORING, AND WHY IT DIFFERS FROM Wasmtime.cmake
#
# VENDORING.md asks for pinned submodules built from source. Wasmtime.cmake had
# to override that -- a Rust toolchain for a C++ project is a large ask, so it
# fetches a hash-pinned prebuilt tarball. wasm3 needs no override: it is
# dependency-free C99 with no build system worth invoking, so the sources are
# compiled directly into the plugin the way probe/gate0/build.sh already does.
#
# That is also why iOS costs nothing here. The same twelve files cross-compile
# clean for arm64-apple-ios, and the resulting build configuration resolves
# IDENTICALLY to the macOS build the reference numbers came from -- verified by
# preprocessing both targets: M3_ARCH "arm64-v8a", M3_GUARANTEED_TAIL_CALL 1,
# M3_HAS_TAIL_CALL 1, d_m3Use32BitSlots 1, d_m3RecordBacktraces 0.
#
# The tail-call setting is the one that matters. Without musttail wasm3
# degrades badly and silently, so a build that quietly lost it would look like
# a slow interpreter rather than a broken flag.

if(NOT KROMOSYNTH_WASM3)
    return()
endif()

# Located rather than fetched. The clone already exists beside the probe that
# produced the reference numbers, and pointing at a second copy would invite
# the two to drift on a wasm3 version bump.
set(KROMOSYNTH_WASM3_SOURCE_DIR "" CACHE PATH
    "Path to the wasm3 'source' directory. Defaults to the probe's clone.")

if(NOT KROMOSYNTH_WASM3_SOURCE_DIR)
    set(_kw3_default
        "${CMAKE_CURRENT_SOURCE_DIR}/../kromosynth-projects/Evolutionary Sound Discovery Platform/faust-genome-substrate/probe/out/wasm-runtime/wasm3/source")
    if(EXISTS "${_kw3_default}/wasm3.h")
        set(KROMOSYNTH_WASM3_SOURCE_DIR "${_kw3_default}" CACHE PATH "" FORCE)
    endif()
endif()

if(NOT EXISTS "${KROMOSYNTH_WASM3_SOURCE_DIR}/wasm3.h")
    message(FATAL_ERROR
        "KROMOSYNTH_WASM3=ON but wasm3 sources were not found.\n"
        "Looked in: ${KROMOSYNTH_WASM3_SOURCE_DIR}\n"
        "Clone it (see probe/gate0/build.sh) and pass "
        "-DKROMOSYNTH_WASM3_SOURCE_DIR=/path/to/wasm3/source")
endif()

# Excluded BY NAME rather than by glob-and-hope, matching probe/gate0/build.sh.
# These four are WASI shims: the modules import nothing but libm (§4.1), so
# WASI is dead weight, and m3_api_tracer.c pulls in tracing this build has no
# use for.
file(GLOB _kw3_all "${KROMOSYNTH_WASM3_SOURCE_DIR}/*.c")
set(_kw3_sources "")
foreach(_f ${_kw3_all})
    get_filename_component(_n "${_f}" NAME)
    if(NOT _n MATCHES "^m3_api_(uvwasi|meta_wasi|tracer|wasi)\\.c$")
        list(APPEND _kw3_sources "${_f}")
    endif()
endforeach()

list(LENGTH _kw3_sources _kw3_count)
if(_kw3_count LESS 10)
    message(FATAL_ERROR
        "Found only ${_kw3_count} wasm3 sources in ${KROMOSYNTH_WASM3_SOURCE_DIR}. "
        "Expected 12. The clone looks incomplete.")
endif()

add_library(kromosynth_wasm3 STATIC ${_kw3_sources})

target_include_directories(kromosynth_wasm3
    PUBLIC "${KROMOSYNTH_WASM3_SOURCE_DIR}")

# d_m3HasWASI=0 to match the excluded sources. Without it the headers still
# declare the WASI API and the link fails on symbols that were deliberately
# left out.
target_compile_definitions(kromosynth_wasm3 PUBLIC d_m3HasWASI=0)

# wasm3 is upstream C and its warnings are not this project's to fix. Silenced
# on the wasm3 target ONLY, so the plugin's own warnings stay loud.
target_compile_options(kromosynth_wasm3 PRIVATE
    -Wno-unused-function -Wno-unused-variable -Wno-strict-prototypes)

set_target_properties(kromosynth_wasm3 PROPERTIES
    C_STANDARD 11
    POSITION_INDEPENDENT_CODE ON)

add_library(wasm3::wasm3 ALIAS kromosynth_wasm3)

message(STATUS "wasm3: ${_kw3_count} sources from ${KROMOSYNTH_WASM3_SOURCE_DIR}")
