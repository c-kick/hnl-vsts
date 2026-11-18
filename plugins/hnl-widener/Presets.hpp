// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// Factory presets: see common/Presets.hpp for how they work.
#pragma once

#include "../../common/Presets.hpp"
#include "../../common/UserPresets.hpp"
#include "Params.hpp"

namespace presets {

using hnl::Preset;
using hnl::PresetValue;

// Haas mode: 0 = mono safe, 1 = left late, 2 = right late. Bass: 0 = as is, 1 = mono.
// "Default" is Diffuse at half and nothing else. The two "Stereo Mix" presets only change the width a
// sound already has: on a mono sound they do nothing.

static const PresetValue kMonoToStereo[] = {
    {kDiffuse, 70}, {kDetune, 35}, {kFocus, 30}, {kLow, 150},
};
static const PresetValue kSubtle[] = {
    {kDiffuse, 25},
};
static const PresetValue kAirOnly[] = {
    {kDiffuse, 60}, {kSize, 30}, {kLow, 2000},
};
static const PresetValue kBassStaysHome[] = {
    {kDiffuse, 60}, {kLow, 300}, {kBass, 1},
};
static const PresetValue kDrumsWide[] = {
    {kDiffuse, 65}, {kSize, 30}, {kFocus, 80}, {kLow, 180},
};
static const PresetValue kHaasSafe[] = {
    {kDiffuse, 0}, {kHaas, 70}, {kHaasTime, 12},
};
static const PresetValue kHaasClassic[] = {
    {kDiffuse, 0}, {kHaas, 100}, {kHaasTime, 15}, {kHaasMode, 2},
};
static const PresetValue kDoubleTrack[] = {
    {kDiffuse, 20}, {kDetune, 80}, {kCents, 12},
};
static const PresetValue kVocalSpread[] = {
    {kDiffuse, 25}, {kDetune, 60}, {kCents, 7}, {kLow, 200}, {kFocus, 20},
};
static const PresetValue kWidePad[] = {
    {kDiffuse, 80}, {kSize, 80}, {kDetune, 50}, {kCents, 14}, {kDrift, 0.15f},
};
static const PresetValue kBarberPole[] = {
    {kDiffuse, 0}, {kHaas, 80}, {kHaasTime, 6}, {kDrift, 0.4f},
};
static const PresetValue kSlowSwirl[] = {
    {kDiffuse, 75}, {kSize, 65}, {kDrift, 0.08f},
};
static const PresetValue kMixWider[] = {
    {kDiffuse, 0}, {kWidth, 140}, {kLow, 200},
};
static const PresetValue kMonoBassOnly[] = {
    {kDiffuse, 0}, {kBass, 1},
};
static const PresetValue kEverything[] = {
    {kHaas, 50}, {kDiffuse, 60}, {kDetune, 50}, {kDrift, 0.2f}, {kFocus, 40}, {kLow, 150},
};

static const Preset kAll[] = {
    {"Default", nullptr, 0},
    HNL_PRESET("Mono To Stereo", kMonoToStereo),
    HNL_PRESET("Subtle", kSubtle),
    HNL_PRESET("Air Only", kAirOnly),
    HNL_PRESET("Bass Stays Home", kBassStaysHome),
    HNL_PRESET("Drums Wide", kDrumsWide),
    HNL_PRESET("Haas Mono Safe", kHaasSafe),
    HNL_PRESET("Haas Classic", kHaasClassic),
    HNL_PRESET("Double Track", kDoubleTrack),
    HNL_PRESET("Vocal Spread", kVocalSpread),
    HNL_PRESET("Wide Pad", kWidePad),
    HNL_PRESET("Barber Pole", kBarberPole),
    HNL_PRESET("Slow Swirl", kSlowSwirl),
    HNL_PRESET("Stereo Mix Wider", kMixWider),
    HNL_PRESET("Stereo Mix Mono Bass", kMonoBassOnly),
    HNL_PRESET("Everything", kEverything),
};

static const int kCount = static_cast<int>(sizeof(kAll) / sizeof(kAll[0]));

/** Listen and Level Match are for checking and comparing: a preset leaves them as they are. */
inline bool inPreset(uint32_t id) { return id != kListen && id != kMatch; }

/** The value a preset gives to an input parameter. */
inline float value(int preset, uint32_t id) { return hnl::presetValue(kAll[preset], id, kParams[id].def); }

/** The factory presets followed by the ones the user saved (common/UserPresets.hpp). */
inline hnl::PresetList list() {
    return hnl::PresetList("HNL Widener", kAll, kCount, kInputParamCount, [](uint32_t id) {
        const ParamInfo& p = kParams[id];
        return hnl::PresetParam{p.symbol, p.min, p.max, p.def};
    }, inPreset);
}

} // namespace presets
