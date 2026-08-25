# Vendoring

KEEPABLE. This is the layer the session brief says must be built properly.

## One mechanism only

Dependencies are **pinned git submodules under `third_party/`**, consumed via
`add_subdirectory`. No `FetchContent`, no CPM, no `find_package`, no Homebrew.

The reason is not taste. The MIT reference repo (`vendor/agentic-synth`)
declares `third_party/JUCE` in `.gitmodules` and its working copy is **empty** —
the submodule was never initialised. That build worked on the machine that made
it and is not reproducible anywhere else. A single mechanism plus a
configure-time existence check (see the root `CMakeLists.txt`) prevents that
specific failure, which is common enough to be worth designing against.

## Adding JUCE

```sh
cd kromosynth-plugin
git submodule add https://github.com/juce-framework/JUCE.git third_party/JUCE
git -C third_party/JUCE checkout <TAG>     # a TAG, never a branch
git add third_party/JUCE .gitmodules
```

Pin to a tag. `agentic-synth`'s `.gitmodules` has no branch or tag pin, so
`git submodule update --remote` would silently move it.

**The first configure after this will tell you the licence.**
`cmake/JuceLicenceCheck.cmake` reads
`third_party/JUCE/modules/juce_core/juce_core.h`, prints the version and the
`license:` field from the module declaration, and hard-fails if the header text
does not contain `AGPL`.

This exists because `WP_SYNTHIS_PLUGIN.md` §0 asserts JUCE's open-source option
is AGPLv3 and asks for it to be confirmed "before the first public build rather
than after" — and at repo-creation time that assertion was **unverified against
disk**. The only two JUCE copies on this machine at the time were:

| path | version | header says |
|---|---|---|
| `apps/kromosynth-evoruns-plugin/native/juce` | 7.0.5 | `GPL v3` |
| `vendor/srvb/native/juce` | 7.0.5 | `GPL v3` |

Both are JUCE 7, which was genuinely GPLv3, so they neither confirm nor refute
the claim about JUCE 8. They just mean nobody had checked. Now the build checks.

**If the assertion fires, that is a finding for the WP, not a check to delete.**
It would also reopen WP §9's deferred question about pairing JUCE with libfaust
(GPL) if tier 2 ever ships.

## Adding sfizz (Task 2 only)

```sh
git submodule add https://github.com/sfztools/sfizz third_party/sfizz
git -C third_party/sfizz checkout 1.2.3
git submodule update --init --recursive
```

`cmake/Sfizz.cmake` forces `SFIZZ_USE_SNDFILE=OFF` and then **re-checks the
variable after `add_subdirectory`**, failing the build if it came back ON. The
double-check is deliberate: if a future sfizz renames the option, a plain
`set(... FORCE)` would silently fall through to the default, and the default is
libsndfile, which is LGPL. The session constraint is that sfizz's `dr_libs`
default avoids that question entirely, so the build enforces it rather than
documenting it.

Everything else in sfizz — its own LV2/VST/AU wrappers, tools, demos, tests —
is switched off. Its plugin wrappers would collide with ours.

## Not vendored, and not to be added without asking

JUCE and sfizz. That is the whole list. No JSON library, no HTTP client, no
test framework, no format converter.
