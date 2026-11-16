// Factory presets: see common/Presets.hpp for how they work.
#pragma once

#include "../../common/Presets.hpp"
#include "Params.hpp"

namespace presets {

using hnl::Preset;
using hnl::PresetValue;

// Type: 0 = opto, 1 = FET. Ratio: 0 = 3:1, 1 = 4:1, 2 = 8:1, 3 = 20:1.
// Attack and Release: 0 = fastest, 100 = slowest of the type's range.

static const PresetValue kVocalLeveller[] = {
    {kType, 0}, {kThreshold, -24}, {kRatio, 0}, {kMakeup, 4}, {kColour, 25}, {kScHp, 80},
};
static const PresetValue kVocalUpFront[] = {
    {kType, 1}, {kThreshold, -26}, {kRatio, 1}, {kAttack, 30}, {kRelease, 30}, {kMakeup, 6}, {kColour, 30}, {kScHp, 80},
};
static const PresetValue kSmoothBass[] = {
    {kType, 0}, {kThreshold, -26}, {kRatio, 1}, {kMakeup, 5}, {kColour, 35},
};
static const PresetValue kBassGrip[] = {
    {kType, 1}, {kThreshold, -24}, {kRatio, 2}, {kAttack, 80}, {kRelease, 45}, {kMakeup, 5}, {kColour, 40},
};
static const PresetValue kGentleOpto[] = {
    {kType, 0}, {kThreshold, -18}, {kRatio, 0}, {kMakeup, 2}, {kColour, 10},
};
static const PresetValue kOptoLimit[] = {
    {kType, 0}, {kThreshold, -20}, {kRatio, 3}, {kMakeup, 4},
};
static const PresetValue kAcousticGuitar[] = {
    {kType, 0}, {kThreshold, -22}, {kRatio, 0}, {kAttack, 65}, {kMakeup, 3}, {kColour, 15},
};
static const PresetValue kSnappyDrums[] = {
    {kType, 1}, {kThreshold, -22}, {kRatio, 1}, {kAttack, 100}, {kRelease, 10}, {kMakeup, 4},
};
static const PresetValue kSmash[] = {
    {kType, 1}, {kThreshold, -34}, {kRatio, 3}, {kAttack, 20}, {kRelease, 0}, {kMakeup, 12}, {kColour, 80},
};
static const PresetValue kParallelThicken[] = {
    {kType, 1}, {kThreshold, -36}, {kRatio, 3}, {kAttack, 30}, {kRelease, 25}, {kMakeup, 12}, {kColour, 60}, {kMix, 40},
};

static const Preset kAll[] = {
    {"Default", nullptr, 0},
    HNL_PRESET("Vocal Leveller", kVocalLeveller),
    HNL_PRESET("Vocal Up Front", kVocalUpFront),
    HNL_PRESET("Smooth Bass", kSmoothBass),
    HNL_PRESET("Bass Grip", kBassGrip),
    HNL_PRESET("Gentle Opto", kGentleOpto),
    HNL_PRESET("Opto Limit", kOptoLimit),
    HNL_PRESET("Acoustic Guitar", kAcousticGuitar),
    HNL_PRESET("Snappy Drums", kSnappyDrums),
    HNL_PRESET("Smash", kSmash),
    HNL_PRESET("Parallel Thicken", kParallelThicken),
};

static const int kCount = static_cast<int>(sizeof(kAll) / sizeof(kAll[0]));

/** Every control belongs to a preset. */
inline bool inPreset(uint32_t) { return true; }

/** The value a preset gives to an input parameter. */
inline float value(int preset, uint32_t id) { return hnl::presetValue(kAll[preset], id, kParams[id].def); }

} // namespace presets
