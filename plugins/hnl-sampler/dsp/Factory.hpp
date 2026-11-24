// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// The sounds and instruments that come with the sampler. They are files
// under factory/, compiled into the plugin by FactoryData.cpp: sounds as
// FLAC, instruments as the text of Model.hpp. A document names such a sound
// "factory:<set>/<sound>.flac" where it names any other by its place on
// disk, so a song that uses one opens on every computer.
//
// FactoryData.cpp hands its list over when the plugin is loaded. A program
// built without that file (most tests) simply has none.
// Header-only, no dependencies.
#pragma once

#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

namespace hnl {
namespace sampler {
namespace factory {

struct Item {
    const char* name; // "kit/kick_1a.flac", "Jazz Kit.hnlsampler"
    const unsigned char* data;
    size_t size;
};

struct List {
    const Item* items = nullptr;
    size_t count = 0;
};

inline List& list() {
    static List all;
    return all;
}

/** One of these in FactoryData.cpp, made before anything else runs, is what fills the list. */
struct Install {
    Install(const Item* items, size_t count) {
        list().items = items;
        list().count = count;
    }
};

constexpr const char* kPrefix = "factory:";

/** Whether a document's name for a sound means one of these. */
inline bool isFactory(const std::string& path) { return path.compare(0, std::strlen(kPrefix), kPrefix) == 0; }

/** The file of this name ("kit/kick_1a.flac"), or null. */
inline const Item* find(const std::string& name) {
    const List& all = list();
    for (size_t i = 0; i < all.count; ++i)
        if (name == all.items[i].name) return &all.items[i];
    return nullptr;
}

constexpr const char* kInstrumentExt = ".hnlsampler";

/** The names of the instruments, in the order they were made. */
inline std::vector<std::string> instruments() {
    std::vector<std::string> names;
    const List& all = list();
    const size_t ext = std::strlen(kInstrumentExt);
    for (size_t i = 0; i < all.count; ++i) {
        const std::string name = all.items[i].name;
        if (name.size() > ext && name.compare(name.size() - ext, ext, kInstrumentExt) == 0) names.push_back(name.substr(0, name.size() - ext));
    }
    return names;
}

/** The text of an instrument by its name, empty when there is none. */
inline std::string instrument(const std::string& name) {
    const Item* item = find(name + kInstrumentExt);
    return item != nullptr ? std::string(reinterpret_cast<const char*>(item->data), item->size) : std::string();
}

} // namespace factory
} // namespace sampler
} // namespace hnl
