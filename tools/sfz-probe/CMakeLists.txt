# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Björn Þór Jónsson
# sfz-probe — developer tool, never shipped.
#
# KEEPABLE.
#
# Links sfizz and nothing else. No JUCE, no plugin wrapper, no audio device.
# Gated on KROMOSYNTH_BUILD_SAMPLER by the caller, so the stage0 presets do not
# pay for it.
#
# Deliberately NOT given COPY_AFTER_BUILD or an install rule: this binary is a
# measuring instrument for the build tree it was built in, and a copy of it
# sitting somewhere on $PATH would eventually be run against a different pin.
#
# Compiles at the top-level CMAKE_CXX_STANDARD (20) while sfizz itself is at 14.
# Safe for the same reason recorded in cmake/Sfizz.cmake: sfizz.hpp's public API
# is std::string, std::vector and std::unique_ptr only, so no type crossing the
# boundary has a standard-dependent definition.

add_executable(sfz_probe main.cpp)

set_target_properties(sfz_probe PROPERTIES OUTPUT_NAME "sfz-probe")

target_link_libraries(sfz_probe PRIVATE sfizz::sfizz)

target_compile_options(sfz_probe PRIVATE -Wall -Wextra)
