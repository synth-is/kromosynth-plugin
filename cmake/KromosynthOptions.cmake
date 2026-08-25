# Per-target compile configuration.
#
# KEEPABLE. Deliberately thin — one function, no policy beyond warnings and
# the C++ standard. Resist adding project-wide compile definitions here; they
# belong on the targets that need them so the dependency is visible.

function(kromosynth_configure_target target_name)
    target_compile_features(${target_name} PUBLIC cxx_std_20)

    set_target_properties(${target_name} PROPERTIES
        CXX_EXTENSIONS OFF
        CXX_STANDARD_REQUIRED ON)

    target_compile_options(${target_name} PRIVATE
        -Wall -Wextra -Wpedantic)
endfunction()
