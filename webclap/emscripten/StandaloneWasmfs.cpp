// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Björn Þór Jónsson
//
// synthis-et4.18: the sampler spike's replacement for the kit's juce_webclap_standalone.cpp.
//
// The kit's file keeps module.wasm free of "env" imports by defining the memory growth hook, getentropy() and every
// file syscall; the syscalls describe an empty read-only file system (lookups ENOENT, changes EROFS). The sampler
// needs a file system it can write to and read from (sfizz and juce::File open files by path), so it links WasmFS
// (-sWASMFS), which defines the same __syscall_* functions over an in-memory backend: with the kit's file both are
// strong definitions and wasm-ld stops on "duplicate symbol". This file keeps the kit's other two definitions and
// leaves the syscalls to WasmFS. Same code as the kit's (open-webclap a2e8984, juce_webclap_standalone.cpp).

#include <cerrno>
#include <cstddef>
#include <cstdint>

#include <wasi/api.h>

extern "C"
{
void emscripten_notify_memory_growth (size_t) {}

// Emscripten's standalone getentropy() aborts (its weak default), which kills std::random_device; WASI random_get.
int getentropy (void* buffer, size_t length)
{
    if (length > 256)
    {
        errno = EIO;
        return -1;
    }

    if (__wasi_random_get (static_cast<uint8_t*> (buffer), length) != 0)
    {
        errno = EIO;
        return -1;
    }

    return 0;
}
}
