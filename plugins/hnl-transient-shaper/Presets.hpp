// Factory presets: see common/Presets.hpp for how they work.
#pragma once

#include "../../common/Presets.hpp"
#include "Params.hpp"

namespace presets {

using hnl::Preset;
using hnl::PresetValue;

// Detector: 0 = full, 1 = low, 2 = mid, 3 = high.

static const PresetValue kPunchyKick[] = {
    {kAttack, 50}, {kSustain, -20}, {kLength, 25}, {kFocus, 1}, {kOutput, -2},
};
static const PresetValue kSnareCrack[] = {
    {kAttack, 60}, {kLength, 10}, {kFocus, 2}, {kOutput, -3},
};
static const PresetValue kDrumBusPunch[] = {
    {kAttack, 35}, {kLength, 25}, {kOutput, -2},
};
static const PresetValue kTightDrums[] = {
    {kAttack, 20}, {kSustain, -60}, {kOutput, -1},
};
static const PresetValue kDryUpRoom[] = {
    {kSustain, -85},
};
static const PresetValue kRoomUp[] = {
    {kSustain, 60}, {kOutput, -3},
};
static const PresetValue kFatSustain[] = {
    {kAttack, -25}, {kSustain, 50}, {kOutput, -2},
};
static const PresetValue kSoften[] = {
    {kAttack, -55}, {kLength, 15},
};
static const PresetValue kPushBack[] = {
    {kAttack, -75}, {kSustain, 25}, {kLength, 30},
};
static const PresetValue kHatTamer[] = {
    {kAttack, -45}, {kLength, 8}, {kFocus, 3},
};
static const PresetValue kParallelSnap[] = {
    {kAttack, 100}, {kLength, 15}, {kOutput, -4}, {kMix, 50},
};

static const Preset kAll[] = {
    {"Default", nullptr, 0},
    HNL_PRESET("Punchy Kick", kPunchyKick),
    HNL_PRESET("Snare Crack", kSnareCrack),
    HNL_PRESET("Drum Bus Punch", kDrumBusPunch),
    HNL_PRESET("Tight Drums", kTightDrums),
    HNL_PRESET("Dry Up Room", kDryUpRoom),
    HNL_PRESET("Room Up", kRoomUp),
    HNL_PRESET("Fat Sustain", kFatSustain),
    HNL_PRESET("Soften", kSoften),
    HNL_PRESET("Push Back", kPushBack),
    HNL_PRESET("Hat Tamer", kHatTamer),
    HNL_PRESET("Parallel Snap", kParallelSnap),
};

static const int kCount = static_cast<int>(sizeof(kAll) / sizeof(kAll[0]));

/** Every control belongs to a preset. */
inline bool inPreset(uint32_t) { return true; }

/** The value a preset gives to an input parameter. */
inline float value(int preset, uint32_t id) { return hnl::presetValue(kAll[preset], id, kParams[id].def); }

} // namespace presets
