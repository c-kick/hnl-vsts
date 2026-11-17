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

// Sets threshold, ratio, attack and release of one compressor band.
#define HM_BAND(base, thr, ratio, att, rel) {base, thr}, {base + 1, ratio}, {base + 2, att}, {base + 3, rel}

static const PresetValue kGentleGlue[] = {
    HM_BAND(kLowThreshold, -18, 1.5f, 40, 300), HM_BAND(kMidThreshold, -18, 1.5f, 25, 200),
    HM_BAND(kHighThreshold, -18, 1.5f, 10, 120),
    {kExcMix, 8}, {kLimGain, 2},
};

static const PresetValue kLoudPunchy[] = {
    HM_BAND(kLowThreshold, -24, 3, 40, 180), HM_BAND(kMidThreshold, -24, 3, 20, 120),
    HM_BAND(kHighThreshold, -24, 3, 8, 60),
    {kLowMakeup, 1}, {kHighMakeup, 1},
    {kExcDrive, 12}, {kExcMix, 20},
    {kLimGain, 6}, {kLimRelease, 80},
};

static const PresetValue kWarmTape[] = {
    {kExcMode, 0}, {kExcFreq, 2500}, {kExcDrive, 14}, {kExcColour, 20}, {kExcMix, 25},
    {kEqOn, 1}, {kEqLowFreq, 100}, {kEqLowGain, 1.5f}, {kEqHighFreq, 9000}, {kEqHighGain, -1},
    {kLimGain, 2},
};

static const PresetValue kBrightAiry[] = {
    {kExcMode, 2}, {kExcFreq, 7000}, {kExcDrive, 10}, {kExcColour, 50}, {kExcMix, 18},
    {kEqOn, 1}, {kEqMidFreq, 300}, {kEqMidGain, -1}, {kEqMidQ, 0.8f}, {kEqHighFreq, 11000}, {kEqHighGain, 2},
    {kWideOn, 1}, {kWideWidth, 120}, {kWideFreq, 300},
};

static const PresetValue kWideStereo[] = {
    {kWideOn, 1}, {kWideWidth, 150}, {kWideFreq, 180},
    {kExcMix, 10},
};

static const PresetValue kTightLowEnd[] = {
    {kXoverLow, 150},
    HM_BAND(kLowThreshold, -26, 4, 25, 150), {kLowMakeup, 1.5f},
    {kEqOn, 1}, {kEqLowFreq, 60}, {kEqLowGain, 1}, {kEqMidFreq, 250}, {kEqMidGain, -1.5f}, {kEqMidQ, 1.2f},
};

static const PresetValue kVocalPresence[] = {
    HM_BAND(kMidThreshold, -22, 2.5f, 15, 150),
    {kEqOn, 1}, {kEqMidFreq, 3000}, {kEqMidGain, 2}, {kEqMidQ, 0.9f}, {kEqHighFreq, 12000}, {kEqHighGain, 1},
    {kExcFreq, 6000}, {kExcMix, 12},
};

static const PresetValue kRoomAmbience[] = {
    {kRevOn, 1}, {kRevDecay, 0.9f}, {kRevDamping, 5000}, {kRevMix, 10},
    {kExcMix, 10},
};

static const PresetValue kLoFiRetro[] = {
    {kExcMode, 2}, {kExcFreq, 2000}, {kExcDrive, 18}, {kExcColour, 80}, {kExcMix, 35},
    {kEqOn, 1}, {kEqLowFreq, 120}, {kEqLowGain, -3}, {kEqMidFreq, 1200}, {kEqMidGain, 2}, {kEqMidQ, 0.7f},
    {kEqHighFreq, 6000}, {kEqHighGain, -4},
    {kWideOn, 1}, {kWideWidth, 70},
    {kRevOn, 1}, {kRevDecay, 1.6f}, {kRevDamping, 3000}, {kRevMix, 14},
};

static const PresetValue kClubMaster[] = {
    HM_BAND(kLowThreshold, -26, 4, 30, 150), HM_BAND(kMidThreshold, -26, 4, 15, 100),
    HM_BAND(kHighThreshold, -26, 4, 5, 60),
    {kLowMakeup, 2},
    {kExcMix, 20},
    {kWideOn, 1}, {kWideWidth, 115}, {kWideFreq, 250},
    {kLimGain, 8}, {kLimRelease, 60},
};

static const PresetValue kLimiterOnly[] = {
    {kCompOn, 0}, {kExcOn, 0},
};

#undef HM_BAND

static const Preset kAll[] = {
    {"Default", nullptr, 0},
    HNL_PRESET("Gentle Glue", kGentleGlue),
    HNL_PRESET("Loud & Punchy", kLoudPunchy),
    HNL_PRESET("Warm Tape", kWarmTape),
    HNL_PRESET("Bright & Airy", kBrightAiry),
    HNL_PRESET("Wide Stereo", kWideStereo),
    HNL_PRESET("Tight Low End", kTightLowEnd),
    HNL_PRESET("Vocal Presence", kVocalPresence),
    HNL_PRESET("Room Ambience", kRoomAmbience),
    HNL_PRESET("Lo-Fi Retro", kLoFiRetro),
    HNL_PRESET("Club Master", kClubMaster),
    HNL_PRESET("Limiter Only", kLimiterOnly),
};

static const int kCount = static_cast<int>(sizeof(kAll) / sizeof(kAll[0]));

/** The value a preset gives to an input parameter. */
inline float value(int preset, uint32_t id) { return hnl::presetValue(kAll[preset], id, kParams[id].def); }

/** Every control belongs to a preset. */
inline bool inPreset(uint32_t) { return true; }

/** The factory presets followed by the ones the user saved (common/UserPresets.hpp). */
inline hnl::PresetList list() {
    return hnl::PresetList("HNL Master", kAll, kCount, kInputParamCount, [](uint32_t id) {
        const ParamInfo& p = kParams[id];
        return hnl::PresetParam{p.symbol, p.min, p.max, p.def};
    }, inPreset);
}

} // namespace presets
