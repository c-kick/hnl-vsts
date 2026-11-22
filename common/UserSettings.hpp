// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// Settings that hold for every HNL plugin and every song, such as the look of the
// windows. One small text file of "key = value" lines:
//
//   Windows:  %APPDATA%\HNL\settings.txt
//   macOS:    ~/Library/Application Support/HNL/settings.txt
//   elsewhere: $XDG_CONFIG_HOME (or ~/.config)/HNL/settings.txt
//   HNL_SETTINGS_DIR, when set, is the folder instead (the tests use it).
//
// A missing file or key reads as an empty value, so a plugin always has its default to fall
// back on. Used from the UI thread only. No DPF or drawing code here.
#pragma once

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace hnl {
namespace settings {

namespace fs = std::filesystem;

/** The folder of the settings file. Empty when the system does not say where settings go. */
inline fs::path folder() {
#ifdef _WIN32
    if (const wchar_t* dir = _wgetenv(L"HNL_SETTINGS_DIR"))
        if (dir[0] != L'\0') return fs::path(dir);
    const wchar_t* base = _wgetenv(L"APPDATA");
    if (base == nullptr || base[0] == L'\0') return fs::path();
    return fs::path(base) / "HNL";
#else
    if (const char* dir = std::getenv("HNL_SETTINGS_DIR"))
        if (dir[0] != '\0') return fs::path(dir);
#ifndef __APPLE__
    if (const char* base = std::getenv("XDG_CONFIG_HOME"))
        if (base[0] != '\0') return fs::path(base) / "HNL";
#endif
    const char* home = std::getenv("HOME");
    if (home == nullptr || home[0] == '\0') return fs::path();
#ifdef __APPLE__
    return fs::path(home) / "Library" / "Application Support" / "HNL";
#else
    return fs::path(home) / ".config" / "HNL";
#endif
#endif
}

namespace detail {

inline std::FILE* open(const fs::path& file, bool write) {
#ifdef _WIN32
    return _wfopen(file.c_str(), write ? L"wb" : L"rb");
#else
    return std::fopen(file.c_str(), write ? "wb" : "rb");
#endif
}

inline std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return s.substr(a, b - a);
}

/** The file as (key, value) pairs in the order they stand in it. A file of over 64 kB is not ours. */
inline std::vector<std::pair<std::string, std::string>> read() {
    std::vector<std::pair<std::string, std::string>> out;
    const fs::path dir = folder();
    if (dir.empty()) return out;
    std::FILE* f = open(dir / "settings.txt", false);
    if (f == nullptr) return out;
    std::string all;
    char buffer[1024];
    size_t got;
    while ((got = std::fread(buffer, 1, sizeof(buffer), f)) > 0 && all.size() < 65536) all.append(buffer, got);
    std::fclose(f);
    size_t at = 0;
    while (at < all.size()) {
        size_t end = all.find('\n', at);
        if (end == std::string::npos) end = all.size();
        const std::string line = all.substr(at, end - at);
        at = end + 1;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(line.substr(0, eq));
        if (!key.empty()) out.emplace_back(key, trim(line.substr(eq + 1)));
    }
    return out;
}

} // namespace detail

/** The value stored under `key`, or an empty text. */
inline std::string get(const char* key) {
    for (const auto& entry : detail::read())
        if (entry.first == key) return entry.second;
    return std::string();
}

/**
 * Stores `value` under `key` and leaves the other keys as they are. The file is written
 * beside the old one and then put in its place, so that another plugin window reading it at
 * that moment sees the old file or the new one. False when it could not be written.
 */
inline bool set(const char* key, const std::string& value) {
    const fs::path dir = folder();
    if (dir.empty()) return false;
    std::vector<std::pair<std::string, std::string>> entries = detail::read();
    bool found = false;
    for (auto& entry : entries)
        if (entry.first == key) { entry.second = value; found = true; }
    if (!found) entries.emplace_back(key, value);

    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path file = dir / "settings.txt", temp = dir / "settings.txt.new";
    std::FILE* f = detail::open(temp, true);
    if (f == nullptr) return false;
    bool ok = true;
    for (const auto& entry : entries) ok = std::fprintf(f, "%s = %s\n", entry.first.c_str(), entry.second.c_str()) > 0 && ok;
    ok = std::fclose(f) == 0 && ok;
    if (ok) {
        fs::rename(temp, file, ec);
        ok = !ec;
    }
    if (!ok) fs::remove(temp, ec);
    return ok;
}

} // namespace settings
} // namespace hnl
