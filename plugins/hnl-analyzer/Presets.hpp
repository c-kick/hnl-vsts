// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// Factory presets: see common/Presets.hpp for how they work. A preset here is a set of targets
// for the advisor: the integrated loudness to aim for, the true-peak ceiling, and the peak to
// loudness ratio that is usual for that kind of music. The figures for services and broadcast
// are the ones those publish; the ones per kind of music are where commercial masters of that
// kind tend to sit, not rules.
#pragma once

#include "../../common/Presets.hpp"
#include "../../common/UserPresets.hpp"
#include "Params.hpp"

namespace presets {

using hnl::Preset;
using hnl::PresetValue;

#define HA_TARGETS(lufs, peak, plr) {kTargetLufs, lufs}, {kMaxTruePeak, peak}, {kPrefPlr, plr}

static const PresetValue kStreaming[] = {HA_TARGETS(-14.0f, -1.0f, 13.0f)};
static const PresetValue kStreamingLoud[] = {HA_TARGETS(-12.5f, -2.0f, 10.5f)};
static const PresetValue kAppleMusic[] = {HA_TARGETS(-16.0f, -1.0f, 15.0f)};
static const PresetValue kPopRock[] = {HA_TARGETS(-10.0f, -1.0f, 9.0f)};
static const PresetValue kElectronic[] = {HA_TARGETS(-8.0f, -1.0f, 7.5f)};
static const PresetValue kHipHop[] = {HA_TARGETS(-9.0f, -1.0f, 8.5f)};
static const PresetValue kAcoustic[] = {HA_TARGETS(-16.0f, -1.0f, 15.0f)};
static const PresetValue kClassical[] = {HA_TARGETS(-20.0f, -1.0f, 19.0f)};
static const PresetValue kBroadcast[] = {HA_TARGETS(-23.0f, -1.0f, 20.0f)};
static const PresetValue kPodcast[] = {HA_TARGETS(-16.0f, -1.0f, 14.0f)};
static const PresetValue kCompactDisc[] = {HA_TARGETS(-9.0f, -0.3f, 8.5f)};

#undef HA_TARGETS

static const Preset kAll[] = {
    HNL_PRESET("Streaming -14", kStreaming), // the first one holds the defaults
    HNL_PRESET("Streaming Loud -12.5", kStreamingLoud),
    HNL_PRESET("Apple Music -16", kAppleMusic),
    HNL_PRESET("Pop and Rock", kPopRock),
    HNL_PRESET("Electronic and Club", kElectronic),
    HNL_PRESET("Hip-Hop", kHipHop),
    HNL_PRESET("Acoustic and Jazz", kAcoustic),
    HNL_PRESET("Classical", kClassical),
    HNL_PRESET("Broadcast EBU R128", kBroadcast),
    HNL_PRESET("Podcast and Speech", kPodcast),
    HNL_PRESET("Compact Disc", kCompactDisc),
};

static const int kCount = static_cast<int>(sizeof(kAll) / sizeof(kAll[0]));

/** The value a preset gives to an input parameter. */
inline float value(int preset, uint32_t id) { return hnl::presetValue(kAll[preset], id, kParams[id].def); }

/** A preset holds the targets. The mode and how the figures are shown stay as they are when one is chosen. */
inline bool inPreset(uint32_t id) { return id >= kTargetLufs && id <= kPrefPlr; }

/** The factory presets followed by the ones the user saved (common/UserPresets.hpp). */
inline hnl::PresetList list() {
    return hnl::PresetList("HNL Analyzer", kAll, kCount, kInputParamCount, [](uint32_t id) {
        const ParamInfo& p = kParams[id];
        return hnl::PresetParam{p.symbol, p.min, p.max, p.def};
    }, inPreset);
}

} // namespace presets
