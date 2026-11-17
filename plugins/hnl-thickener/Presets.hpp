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

// Delayed side: 0 = left, 1 = right, 2 = both. Room: 0 = off, 1 = on.
// "Default" is the rack patch this plugin started from: everything added since is off in it.

static const PresetValue kSubtle[] = {
    {kAmount, 30},
};
static const PresetValue kHeavy[] = {
    {kAmount, 100}, {kDelayMix, 75}, {kOutput, -4},
};
static const PresetValue kNoRoom[] = {
    {kRoom, 0},
};
static const PresetValue kCentred[] = {
    {kSide, 2}, {kGuard, 120},
};
static const PresetValue kSolidKick[] = {
    {kGuard, 180}, {kClean, 40},
};
static const PresetValue kBodyOnly[] = {
    {kSide, 2}, {kLowCut, 150}, {kHighCut, 3000}, {kAmount, 75},
};
static const PresetValue kParallelCrush[] = {
    {kSide, 2}, {kGuard, 100}, {kCrush, 60}, {kAmount, 45}, {kRoomMix, 20}, {kDecay, 0.3f},
};
static const PresetValue kCrushedRoom[] = {
    {kSide, 2}, {kRoomMix, 60}, {kSize, 65}, {kDecay, 0.4f}, {kDamp, 60}, {kCrush, 80}, {kClean, 50}, {kAmount, 40},
};
static const PresetValue kDirt[] = {
    {kSide, 2}, {kGuard, 100}, {kDrive, 55}, {kHighCut, 6000}, {kAmount, 40},
};
static const PresetValue kSmashAndDrive[] = {
    {kSide, 2}, {kGuard, 120}, {kCrush, 70}, {kDrive, 35}, {kLowCut, 80}, {kAmount, 40},
};
static const PresetValue kTightComb[] = {
    {kDelay, 1}, {kDelayMix, 70},
};
static const PresetValue kWide[] = {
    {kSide, 2}, {kDelay, 12}, {kDelayMix, 100}, {kGuard, 200}, {kAmount, 45},
};
static const PresetValue kBiggerRoom[] = {
    {kRoomMix, 30}, {kSize, 75}, {kDecay, 0.5f}, {kDamp, 60},
};
static const PresetValue kLeftSide[] = {
    {kSide, 0},
};

static const Preset kAll[] = {
    {"Default", nullptr, 0},
    HNL_PRESET("Subtle", kSubtle),
    HNL_PRESET("Heavy", kHeavy),
    HNL_PRESET("No Room", kNoRoom),
    HNL_PRESET("Centred", kCentred),
    HNL_PRESET("Solid Kick", kSolidKick),
    HNL_PRESET("Body Only", kBodyOnly),
    HNL_PRESET("Parallel Crush", kParallelCrush),
    HNL_PRESET("Crushed Room", kCrushedRoom),
    HNL_PRESET("Dirt", kDirt),
    HNL_PRESET("Smash And Drive", kSmashAndDrive),
    HNL_PRESET("Tight Comb", kTightComb),
    HNL_PRESET("Wide", kWide),
    HNL_PRESET("Bigger Room", kBiggerRoom),
    HNL_PRESET("Left Side", kLeftSide),
};

static const int kCount = static_cast<int>(sizeof(kAll) / sizeof(kAll[0]));

/** Level Match and Listen are for comparing and checking: a preset leaves them as they are. */
inline bool inPreset(uint32_t id) { return id != kMatch && id != kListen; }

/** The value a preset gives to an input parameter. */
inline float value(int preset, uint32_t id) { return hnl::presetValue(kAll[preset], id, kParams[id].def); }

/** The factory presets followed by the ones the user saved (common/UserPresets.hpp). */
inline hnl::PresetList list() {
    return hnl::PresetList("HNL Thickener", kAll, kCount, kInputParamCount, [](uint32_t id) {
        const ParamInfo& p = kParams[id];
        return hnl::PresetParam{p.symbol, p.min, p.max, p.def};
    }, inPreset);
}

} // namespace presets
