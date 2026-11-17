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

static const PresetValue kHouseStabs[] = {
    {kTone, 25}, {kVelocity, 45}, {kDecay, 60}, {kRelease, 300}, {kReverb, 28},
};
static const PresetValue kDryAndTight[] = {
    {kReverb, 0}, {kDecay, 70}, {kRelease, 200}, {kWidth, 70},
};
static const PresetValue kDoubled[] = {
    {kDetune, 6}, {kReverb, 22},
};
static const PresetValue kHonkyTonk[] = {
    {kDetune, 18}, {kTone, 15}, {kReverb, 8}, {kVelocity, 55},
};
static const PresetValue kBallad[] = {
    {kTone, -30}, {kVelocity, 90}, {kRelease, 1000}, {kReverb, 40},
};
static const PresetValue kDarkAndLong[] = {
    {kTone, -60}, {kRelease, 1800}, {kReverb, 50}, {kDetune, 4},
};
static const PresetValue kPluck[] = {
    {kDecay, 25}, {kRelease, 150}, {kTone, 30}, {kVelocity, 50}, {kReverb, 20},
};
static const PresetValue kBassPiano[] = {
    {kOctave, 1}, {kTone, -10}, {kDecay, 80}, {kWidth, 40}, {kReverb, 5},
};
static const PresetValue kHighBells[] = {
    {kOctave, 3}, {kRelease, 2500}, {kDetune, 5}, {kReverb, 45},
};
static const PresetValue kMonoClassic[] = {
    {kWidth, 0}, {kReverb, 0}, {kVelocity, 60},
};

// The FM electric piano (voice 1).
static const PresetValue kFmElectricPiano[] = {
    {kVoice, 1}, {kReverb, 20},
};
static const PresetValue kFmBallad[] = {
    {kVoice, 1}, {kTone, -20}, {kVelocity, 85}, {kDetune, 6}, {kRelease, 900}, {kReverb, 45},
};
static const PresetValue kFmBrightTines[] = {
    {kVoice, 1}, {kTone, 40}, {kVelocity, 60}, {kReverb, 25},
};
static const PresetValue kFmSoftPad[] = {
    {kVoice, 1}, {kTone, -60}, {kVelocity, 40}, {kDetune, 12}, {kRelease, 1500}, {kReverb, 55},
};
static const PresetValue kFmDry[] = {
    {kVoice, 1}, {kReverb, 0}, {kWidth, 60}, {kRelease, 300},
};

// The 1980s stage piano (voice 2).
static const PresetValue kStagePiano[] = {
    {kVoice, 2}, {kVelocity, 100}, {kRelease, 400}, {kReverb, 18},
};
static const PresetValue kStageBallad[] = {
    {kVoice, 2}, {kVelocity, 100}, {kDetune, 4}, {kRelease, 900}, {kReverb, 42},
};
static const PresetValue kStageBright[] = {
    {kVoice, 2}, {kVelocity, 60}, {kTone, 30}, {kRelease, 300}, {kReverb, 12},
};

// The 1990s dance piano (voice 3).
static const PresetValue kDancePiano[] = {
    {kVoice, 3}, {kRelease, 350}, {kReverb, 15},
};
static const PresetValue kDanceStabs[] = {
    {kVoice, 3}, {kVelocity, 40}, {kDecay, 55}, {kRelease, 250}, {kReverb, 30},
};
static const PresetValue kDanceWide[] = {
    {kVoice, 3}, {kDetune, 7}, {kRelease, 500}, {kReverb, 25},
};

// The grand (voice 4).
static const PresetValue kGrandPiano[] = {
    {kVoice, 4}, {kVelocity, 100}, {kRelease, 350}, {kReverb, 22},
};
static const PresetValue kGrandHall[] = {
    {kVoice, 4}, {kVelocity, 100}, {kRelease, 450}, {kReverb, 55},
};
static const PresetValue kGrandMellow[] = {
    {kVoice, 4}, {kVelocity, 100}, {kTone, -25}, {kRelease, 500}, {kReverb, 30},
};
static const PresetValue kGrandDry[] = {
    {kVoice, 4}, {kVelocity, 100}, {kRelease, 300}, {kReverb, 0},
};
static const PresetValue kGrandPop[] = {
    {kVoice, 4}, {kVelocity, 65}, {kTone, 20}, {kRelease, 300}, {kReverb, 12},
};

static const Preset kAll[] = {
    {"Default", nullptr, 0},
    HNL_PRESET("House Stabs", kHouseStabs),
    HNL_PRESET("Dry and Tight", kDryAndTight),
    HNL_PRESET("Doubled", kDoubled),
    HNL_PRESET("Honky Tonk", kHonkyTonk),
    HNL_PRESET("Ballad", kBallad),
    HNL_PRESET("Dark and Long", kDarkAndLong),
    HNL_PRESET("Pluck", kPluck),
    HNL_PRESET("Bass Piano", kBassPiano),
    HNL_PRESET("High Bells", kHighBells),
    HNL_PRESET("Mono, No Reverb", kMonoClassic),
    HNL_PRESET("FM Electric Piano", kFmElectricPiano),
    HNL_PRESET("FM Ballad", kFmBallad),
    HNL_PRESET("FM Bright Tines", kFmBrightTines),
    HNL_PRESET("FM Soft and Slow", kFmSoftPad),
    HNL_PRESET("FM Dry", kFmDry),
    HNL_PRESET("Stage Piano", kStagePiano),
    HNL_PRESET("Stage Ballad", kStageBallad),
    HNL_PRESET("Stage Bright", kStageBright),
    HNL_PRESET("Dance Piano", kDancePiano),
    HNL_PRESET("Dance Stabs", kDanceStabs),
    HNL_PRESET("Dance Wide", kDanceWide),
    HNL_PRESET("Grand Piano", kGrandPiano),
    HNL_PRESET("Grand in a Hall", kGrandHall),
    HNL_PRESET("Grand Mellow", kGrandMellow),
    HNL_PRESET("Grand Dry", kGrandDry),
    HNL_PRESET("Grand Pop", kGrandPop),
};

static const int kCount = static_cast<int>(sizeof(kAll) / sizeof(kAll[0]));

/** Volume and tuning belong to the song, not to the sound: presets leave them alone. */
inline bool inPreset(uint32_t id) { return id != kVolume && id != kTune; }

/** The value a preset gives to an input parameter. */
inline float value(int preset, uint32_t id) { return hnl::presetValue(kAll[preset], id, kParams[id].def); }

/** The factory presets followed by the ones the user saved (common/UserPresets.hpp). */
inline hnl::PresetList list() {
    return hnl::PresetList("HNL Digital Piano", kAll, kCount, kInputParamCount, [](uint32_t id) {
        const ParamInfo& p = kParams[id];
        return hnl::PresetParam{p.symbol, p.min, p.max, p.def};
    }, inPreset);
}

} // namespace presets
