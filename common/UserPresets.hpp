// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// Presets the user saves, kept as small text files so that they are there in
// every song. One file per preset:
//
//   Windows:  %APPDATA%\HNL\<plugin>\Presets\<name>.hnlpreset
//   elsewhere: $XDG_CONFIG_HOME (or ~/.config)/HNL/<plugin>/Presets/
//   HNL_PRESET_DIR, when set, replaces all of that (the tests use it).
//
// A file holds "symbol = value" lines. A parameter that is not in the file
// gets its default, so a preset saved today still loads after a parameter
// was added. Used from the UI thread only. No DPF or drawing code here.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "Presets.hpp"
#include "dsp/Safety.hpp"

namespace hnl {

/** What the preset code needs to know of a parameter. */
struct PresetParam {
    const char* symbol;
    float min, max, def;
};

namespace presetfile {

namespace fs = std::filesystem;

constexpr const char* kExtension = ".hnlpreset";
constexpr size_t kMaxNameLength = 20; // characters, not bytes: what fits in the preset box
constexpr size_t kMaxFileSize = 64 * 1024;

inline char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c; }

/** Compares without regard to case (ASCII only), as the Windows file system does. */
inline bool sameName(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (lower(a[i]) != lower(b[i])) return false;
    return true;
}

/** The number of characters in a UTF-8 string. */
inline size_t length(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s) n += (c & 0xC0) != 0x80 ? 1 : 0;
    return n;
}

/** True for a character that may be part of a preset name (and so of a file name). */
inline bool allowed(unsigned char c) {
    if (c < 0x20 || c == 0x7F) return false;
    for (const char* bad = "\\/:*?\"<>|"; *bad; ++bad)
        if (c == static_cast<unsigned char>(*bad)) return false;
    return true;
}

/** True when the bytes are well-formed UTF-8. */
inline bool validUtf8(const std::string& s) {
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        const size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
        if (n == 0 || i + n > s.size()) return false;
        for (size_t k = 1; k < n; ++k)
            if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return false;
        i += n;
    }
    return true;
}

/**
 * A name as it can be stored: no characters a file name cannot hold, no space or dot at either
 * end, not longer than fits, never one of the names Windows keeps for devices. Empty when
 * nothing is left.
 */
inline std::string cleanName(const std::string& raw) {
    if (!validUtf8(raw)) return "";
    std::string out;
    size_t characters = 0;
    for (unsigned char c : raw) {
        if (!allowed(c)) continue;
        if ((c & 0xC0) != 0x80 && ++characters > kMaxNameLength) break; // a new character, and no room for it
        out += static_cast<char>(c);
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
    size_t first = 0;
    while (first < out.size() && (out[first] == ' ' || out[first] == '.')) ++first;
    out.erase(0, first);

    std::string stem = out.substr(0, out.find('.'));
    for (const char* device : {"con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "com5", "com6", "com7", "com8", "com9",
                               "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"})
        if (sameName(stem, device)) return out + "_";
    return out;
}

/**
 * Reads a number written with a point or a comma, whatever the host has set the C locale to.
 * False when the text is not a number.
 */
inline bool parseNumber(const std::string& text, float& out) {
    size_t i = 0;
    const size_t n = text.size();
    while (i < n && (text[i] == ' ' || text[i] == '\t')) ++i;
    double sign = 1.0;
    if (i < n && (text[i] == '-' || text[i] == '+')) sign = text[i++] == '-' ? -1.0 : 1.0;
    double value = 0.0, scale = 1.0;
    int digits = 0;
    for (; i < n && text[i] >= '0' && text[i] <= '9'; ++i, ++digits) value = value * 10.0 + (text[i] - '0');
    if (i < n && (text[i] == '.' || text[i] == ',')) {
        ++i;
        for (; i < n && text[i] >= '0' && text[i] <= '9'; ++i, ++digits) {
            scale *= 0.1;
            value += (text[i] - '0') * scale;
        }
    }
    if (digits == 0) return false;
    if (i < n && (text[i] == 'e' || text[i] == 'E')) {
        ++i;
        int esign = 1, exponent = 0, edigits = 0;
        if (i < n && (text[i] == '-' || text[i] == '+')) esign = text[i++] == '-' ? -1 : 1;
        for (; i < n && text[i] >= '0' && text[i] <= '9' && exponent < 1000; ++i, ++edigits) exponent = exponent * 10 + (text[i] - '0');
        if (edigits == 0) return false;
        value *= std::pow(10.0, esign * exponent);
    }
    while (i < n && (text[i] == ' ' || text[i] == '\t' || text[i] == '\r')) ++i;
    if (i != n) return false;
    const float result = static_cast<float>(sign * value);
    if (!isFinite(result)) return false;
    out = result;
    return true;
}

/** Writes a number with a point, whatever the C locale. */
inline std::string formatNumber(float value) {
    char text[48];
    std::snprintf(text, sizeof(text), "%.9g", static_cast<double>(value));
    for (char* c = text; *c; ++c)
        if (*c == ',') *c = '.';
    return text;
}

inline std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return s.substr(a, b - a);
}

inline std::FILE* open(const fs::path& file, bool write) {
#ifdef _WIN32
    return _wfopen(file.c_str(), write ? L"wb" : L"rb");
#else
    return std::fopen(file.c_str(), write ? "wb" : "rb");
#endif
}

/** The folder of a plugin's presets. Empty when the system does not say where settings go. */
inline fs::path folder(const char* plugin) {
#ifdef _WIN32
    if (const wchar_t* dir = _wgetenv(L"HNL_PRESET_DIR"))
        if (dir[0] != L'\0') return fs::path(dir);
    const wchar_t* base = _wgetenv(L"APPDATA");
    if (base == nullptr || base[0] == L'\0') return fs::path();
    return fs::path(base) / "HNL" / plugin / "Presets";
#else
    if (const char* dir = std::getenv("HNL_PRESET_DIR"))
        if (dir[0] != '\0') return fs::path(dir);
    if (const char* base = std::getenv("XDG_CONFIG_HOME"))
        if (base[0] != '\0') return fs::path(base) / "HNL" / plugin / "Presets";
    const char* home = std::getenv("HOME");
    if (home == nullptr || home[0] == '\0') return fs::path();
    return fs::path(home) / ".config" / "HNL" / plugin / "Presets";
#endif
}

} // namespace presetfile

/**
 * A plugin's presets as the UI shows them: the factory presets first, then the ones the user
 * saved, by name. An index counts through both.
 */
class PresetList {
public:
    typedef PresetParam (*InfoFn)(uint32_t id);
    typedef bool (*InPresetFn)(uint32_t id);

    /** The most presets the list in the UI has room for, together with its "Save as..." row. */
    static constexpr int kMaxShown = 47;

    PresetList(const char* plugin, const Preset* factory, int factoryCount, uint32_t paramCount, InfoFn info, InPresetFn inPreset)
        : plugin_(plugin), factory_(factory), factoryCount_(factoryCount), paramCount_(paramCount), info_(info), inPreset_(inPreset),
          dir_(presetfile::folder(plugin)) {
        rescan();
    }

    int count() const { return factoryCount_ + static_cast<int>(user_.size()); }
    int factoryCount() const { return factoryCount_; }
    bool isUser(int index) const { return index >= factoryCount_ && index < count(); }
    /** False when the list is full or there is nowhere to keep a preset. */
    bool canSave() const { return !dir_.empty(); }
    bool full() const { return count() >= kMaxShown; }

    const char* name(int index) const {
        if (index < 0 || index >= count()) return "";
        return index < factoryCount_ ? factory_[index].name : user_[index - factoryCount_].name.c_str();
    }

    /** The preset of this name, or -1. A factory preset goes before a saved one. */
    int find(const std::string& name) const {
        for (int i = 0; i < count(); ++i)
            if (name == this->name(i)) return i;
        return -1;
    }

    /** The value a preset gives to a parameter: within the parameter's range, its default when the preset does not name it. */
    float value(int index, uint32_t id) const {
        const PresetParam p = info_(id);
        if (index < 0 || index >= count()) return p.def;
        if (index < factoryCount_) return presetValue(factory_[index], id, p.def);
        for (const auto& v : user_[index - factoryCount_].values)
            if (v.first == p.symbol) return cleanParam(v.second, p.min, p.max, p.def);
        return p.def;
    }

    /** Reads the folder again: another plugin window may have saved or deleted a preset. */
    void rescan() {
        namespace fs = presetfile::fs;
        user_.clear();
        if (dir_.empty()) return;
        std::error_code ec;
        fs::directory_iterator it(dir_, ec), end;
        for (int seen = 0; !ec && it != end && seen < 2000; it.increment(ec), ++seen) {
            const fs::path file = it->path();
            if (file.extension().u8string() != presetfile::kExtension) continue;
            User u;
            u.name = file.stem().u8string();
            // Only names this code could have written: anything else it could not show or save again.
            if (u.name.empty() || presetfile::cleanName(u.name) != u.name || isFactoryName(u.name)) continue;
            if (!read(file, u)) continue;
            u.file = file;
            user_.push_back(std::move(u));
        }
        std::sort(user_.begin(), user_.end(), [](const User& a, const User& b) {
            const size_t n = std::min(a.name.size(), b.name.size());
            for (size_t i = 0; i < n; ++i) {
                const char x = presetfile::lower(a.name[i]), y = presetfile::lower(b.name[i]);
                if (x != y) return static_cast<unsigned char>(x) < static_cast<unsigned char>(y);
            }
            return a.name.size() < b.name.size();
        });
        const size_t room = factoryCount_ < kMaxShown ? static_cast<size_t>(kMaxShown - factoryCount_) : 0;
        if (user_.size() > room) user_.resize(room);
    }

    /** The name the "Save as" box starts with: that of the user's preset in use, or a free "My preset". */
    std::string suggestName(int current) const {
        if (isUser(current)) return name(current);
        std::string base = "My preset", name = base;
        for (int n = 2; n < 100 && taken(name); ++n) name = base + " " + std::to_string(n);
        return name;
    }

    /**
     * Saves values[0..paramCount) under a name. A saved preset of that name is replaced; a
     * factory preset is not, the name then gets "(mine)" added. Returns the index of the
     * preset, or -1 when it could not be written.
     */
    int save(const std::string& rawName, const float* values) {
        namespace fs = presetfile::fs;
        std::string name = presetfile::cleanName(rawName);
        if (name.empty() || dir_.empty()) return -1;
        if (isFactoryName(name)) {
            std::string marked = presetfile::cleanName(name + " (mine)");
            if (marked.size() < name.size() + 7) return -1; // the name was too long to mark
            name = marked;
        }
        rescan();
        const fs::path* old = nullptr;
        for (const User& u : user_)
            if (presetfile::sameName(u.name, name)) old = &u.file;
        if (old == nullptr && full()) return -1;

        std::string text = "HNL preset\nplugin: " + plugin_ + "\n";
        for (uint32_t id = 0; id < paramCount_; ++id) {
            if (!inPreset_(id)) continue;
            const PresetParam p = info_(id);
            text += std::string(p.symbol) + " = " + presetfile::formatNumber(cleanParam(values[id], p.min, p.max, p.def)) + "\n";
        }

        std::error_code ec;
        fs::create_directories(dir_, ec);
        // Written next to the preset and then moved over it, so that a full disk never leaves half a preset.
        const fs::path file = dir_ / fs::u8path(name + presetfile::kExtension), temp = dir_ / fs::u8path(name + ".tmp");
        std::FILE* f = presetfile::open(temp, true);
        if (f == nullptr) return -1;
        const bool written = std::fwrite(text.data(), 1, text.size(), f) == text.size();
        const bool closed = std::fclose(f) == 0;
        if (!written || !closed) {
            fs::remove(temp, ec);
            return -1;
        }
        if (old != nullptr && *old != file) fs::remove(*old, ec); // the same name in other capitals
        fs::rename(temp, file, ec);
        if (ec) {
            fs::remove(temp, ec);
            return -1;
        }
        rescan();
        return find(name);
    }

    /** Deletes a saved preset. False for a factory preset or when the file could not be removed. */
    bool remove(int index) {
        if (!isUser(index)) return false;
        std::error_code ec;
        const bool gone = presetfile::fs::remove(user_[index - factoryCount_].file, ec) && !ec;
        rescan();
        return gone;
    }

private:
    struct User {
        std::string name;
        std::vector<std::pair<std::string, float>> values;
        presetfile::fs::path file;
    };

    bool isFactoryName(const std::string& name) const {
        for (int i = 0; i < factoryCount_; ++i)
            if (presetfile::sameName(name, factory_[i].name)) return true;
        return false;
    }
    bool taken(const std::string& name) const {
        if (isFactoryName(name)) return true;
        for (const User& u : user_)
            if (presetfile::sameName(u.name, name)) return true;
        return false;
    }

    /** False for a file that is not a preset of this code: too large, unreadable, or without its first line. */
    static bool read(const presetfile::fs::path& file, User& out) {
        std::FILE* f = presetfile::open(file, false);
        if (f == nullptr) return false;
        std::string text(presetfile::kMaxFileSize + 1, '\0');
        text.resize(std::fread(&text[0], 1, text.size(), f));
        std::fclose(f);
        if (text.size() > presetfile::kMaxFileSize) return false;

        bool first = true;
        for (size_t start = 0; start < text.size();) {
            size_t stop = text.find('\n', start);
            if (stop == std::string::npos) stop = text.size();
            const std::string line = presetfile::trim(text.substr(start, stop - start));
            start = stop + 1;
            if (first) {
                if (line != "HNL preset") return false;
                first = false;
                continue;
            }
            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            float value = 0.0f;
            if (!presetfile::parseNumber(line.substr(eq + 1), value)) continue;
            out.values.emplace_back(presetfile::trim(line.substr(0, eq)), value);
        }
        return !first;
    }

    std::string plugin_;
    const Preset* factory_;
    int factoryCount_;
    uint32_t paramCount_;
    InfoFn info_;
    InPresetFn inPreset_;
    presetfile::fs::path dir_;
    std::vector<User> user_;
};

} // namespace hnl
