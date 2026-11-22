// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// Opening a file for reading, for everything here that reads one. Only a
// plain file is opened. A path may come from a project file or from a click
// in a folder, and may name something that never answers: a named pipe waits
// for a writer when it is opened, a console or a serial port waits when it is
// read. The thread that asked would stand still for good. Header-only, no
// dependencies.
#pragma once

#include <cstdio>
#include <filesystem>

#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#endif

namespace hnl {
namespace sampler {

/** Opens a plain file for reading in binary. Null when it cannot be opened or is anything else: a folder, a pipe, a device. */
inline std::FILE* openPlainFile(const std::filesystem::path& file) {
#ifdef _WIN32
    // Opening a device does not wait on Windows, reading it does: so the look comes after the opening.
    std::FILE* f = _wfopen(file.c_str(), L"rb");
    if (f == nullptr) return nullptr;
    struct _stat64 st;
    if (_fstat64(_fileno(f), &st) != 0 || (st.st_mode & _S_IFMT) != _S_IFREG) {
        std::fclose(f);
        return nullptr;
    }
    return f;
#else
    // Opening a pipe is what waits here: so the look comes first, and once more at what was really opened.
    struct stat st;
    if (::stat(file.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return nullptr;
    std::FILE* f = std::fopen(file.c_str(), "rb");
    if (f == nullptr) return nullptr;
    if (::fstat(fileno(f), &st) != 0 || !S_ISREG(st.st_mode)) {
        std::fclose(f);
        return nullptr;
    }
    return f;
#endif
}

} // namespace sampler
} // namespace hnl
