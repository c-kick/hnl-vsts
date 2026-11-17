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

// Ratio: 0 = 1.5:1, 1 = 2:1, 2 = 4:1, 3 = 10:1.
// Attack: 0 = 0.1, 1 = 0.3, 2 = 1, 3 = 3, 4 = 10, 5 = 30 ms.
// Release: 0 = 0.1, 1 = 0.3, 2 = 0.6, 3 = 1.2 s, 4 = auto.

static const PresetValue kMixGlue[] = {
    {kThreshold, -14}, {kRatio, 1}, {kAttack, 5}, {kRelease, 4}, {kScHp, 60}, {kMakeup, 1},
};
static const PresetValue kGentleMaster[] = {
    {kThreshold, -18}, {kRatio, 0}, {kAttack, 5}, {kRelease, 4}, {kScHp, 80}, {kMakeup, 1},
};
static const PresetValue kDrumPunch[] = {
    {kThreshold, -20}, {kRatio, 2}, {kAttack, 5}, {kRelease, 0}, {kMakeup, 3},
};
static const PresetValue kDrumSmash[] = {
    {kThreshold, -28}, {kRatio, 3}, {kAttack, 2}, {kRelease, 0}, {kMakeup, 9},
};
static const PresetValue kParallelCrush[] = {
    {kThreshold, -32}, {kRatio, 3}, {kAttack, 1}, {kRelease, 1}, {kMakeup, 12}, {kMix, 35},
};
static const PresetValue kBassHeavyMix[] = {
    {kThreshold, -16}, {kRatio, 2}, {kAttack, 4}, {kRelease, 4}, {kScHp, 140}, {kMakeup, 2},
};
static const PresetValue kPumping[] = {
    {kThreshold, -24}, {kRatio, 2}, {kAttack, 3}, {kRelease, 1}, {kMakeup, 5},
};
static const PresetValue kKeysBus[] = {
    {kThreshold, -20}, {kRatio, 1}, {kAttack, 4}, {kRelease, 2}, {kMakeup, 2},
};
static const PresetValue kVocalBus[] = {
    {kThreshold, -22}, {kRatio, 2}, {kAttack, 3}, {kRelease, 1}, {kScHp, 100}, {kMakeup, 4},
};

static const Preset kAll[] = {
    {"Default", nullptr, 0},
    HNL_PRESET("Mix Glue", kMixGlue),
    HNL_PRESET("Gentle Master", kGentleMaster),
    HNL_PRESET("Drum Bus Punch", kDrumPunch),
    HNL_PRESET("Drum Bus Smash", kDrumSmash),
    HNL_PRESET("Parallel Crush", kParallelCrush),
    HNL_PRESET("Bass-heavy Mix", kBassHeavyMix),
    HNL_PRESET("Pumping", kPumping),
    HNL_PRESET("Keys Bus", kKeysBus),
    HNL_PRESET("Vocal Bus", kVocalBus),
};

static const int kCount = static_cast<int>(sizeof(kAll) / sizeof(kAll[0]));

/** Every control belongs to a preset. */
inline bool inPreset(uint32_t) { return true; }

/** The value a preset gives to an input parameter. */
inline float value(int preset, uint32_t id) { return hnl::presetValue(kAll[preset], id, kParams[id].def); }

/** The factory presets followed by the ones the user saved (common/UserPresets.hpp). */
inline hnl::PresetList list() {
    return hnl::PresetList("HNL Bus Compressor", kAll, kCount, kInputParamCount, [](uint32_t id) {
        const ParamInfo& p = kParams[id];
        return hnl::PresetParam{p.symbol, p.min, p.max, p.def};
    }, inPreset);
}

} // namespace presets
