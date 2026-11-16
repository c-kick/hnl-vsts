// Factory presets: see common/Presets.hpp for how they work.
#pragma once

#include "../../common/Presets.hpp"
#include "Params.hpp"

namespace presets {

using hnl::Preset;
using hnl::PresetValue;

static const PresetValue kHouseStabs[] = {
    {kTone, 25}, {kVelocity, 45}, {kDecay, 60}, {kRelease, 90}, {kReverb, 28},
};
static const PresetValue kDryAndTight[] = {
    {kReverb, 0}, {kDecay, 70}, {kRelease, 80}, {kWidth, 70},
};
static const PresetValue kDoubled[] = {
    {kDetune, 6}, {kReverb, 22},
};
static const PresetValue kHonkyTonk[] = {
    {kDetune, 18}, {kTone, 15}, {kReverb, 8}, {kVelocity, 55},
};
static const PresetValue kBallad[] = {
    {kTone, -30}, {kVelocity, 90}, {kDecay, 160}, {kRelease, 350}, {kReverb, 40},
};
static const PresetValue kDarkAndLong[] = {
    {kTone, -60}, {kDecay, 300}, {kRelease, 600}, {kReverb, 50}, {kDetune, 4},
};
static const PresetValue kPluck[] = {
    {kDecay, 25}, {kRelease, 60}, {kTone, 30}, {kVelocity, 50}, {kReverb, 20},
};
static const PresetValue kBassPiano[] = {
    {kOctave, 1}, {kTone, -10}, {kDecay, 80}, {kWidth, 40}, {kReverb, 5},
};
static const PresetValue kHighBells[] = {
    {kOctave, 3}, {kDecay, 250}, {kRelease, 900}, {kDetune, 5}, {kReverb, 45},
};
static const PresetValue kMonoClassic[] = {
    {kWidth, 0}, {kReverb, 0}, {kVelocity, 60},
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
};

static const int kCount = static_cast<int>(sizeof(kAll) / sizeof(kAll[0]));

/** Volume and tuning belong to the song, not to the sound: presets leave them alone. */
inline bool inPreset(uint32_t id) { return id != kVolume && id != kTune; }

/** The value a preset gives to an input parameter. */
inline float value(int preset, uint32_t id) { return hnl::presetValue(kAll[preset], id, kParams[id].def); }

} // namespace presets
