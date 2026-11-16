// Factory presets: see common/Presets.hpp for how they work.
#pragma once

#include "../../common/Presets.hpp"
#include "Params.hpp"

namespace presets {

using hnl::Preset;
using hnl::PresetValue;

// Which heads play: every preset that changes them names all three.
#define HE_HEADS(a, b, c) {kHead1, a}, {kHead2, b}, {kHead3, c}

static const PresetValue kSlapback[] = {
    HE_HEADS(1, 0, 0), {kTime, 95}, {kIntensity, 0}, {kEcho, 70}, {kWobble, 15}, {kWidth, 0},
};
static const PresetValue kEchoAndSpring[] = {
    HE_HEADS(0, 1, 0), {kTime, 140}, {kIntensity, 40}, {kReverb, 40},
};
static const PresetValue kThreeHeads[] = {
    HE_HEADS(1, 1, 1), {kTime, 120}, {kIntensity, 22}, {kEcho, 55}, {kWidth, 70},
};
static const PresetValue kGallop[] = {
    HE_HEADS(1, 0, 1), {kTime, 110}, {kIntensity, 30}, {kWidth, 80},
};
static const PresetValue kDubThrow[] = {
    HE_HEADS(0, 0, 1), {kTime, 150}, {kIntensity, 58}, {kEcho, 70}, {kReverb, 20}, {kBass, 3}, {kTreble, -4}, {kWobble, 35},
};
static const PresetValue kDarkWash[] = {
    HE_HEADS(1, 1, 1), {kTime, 330}, {kIntensity, 26}, {kReverb, 45}, {kTreble, -7}, {kWobble, 40}, {kWidth, 100},
};
static const PresetValue kWornTape[] = {
    HE_HEADS(0, 1, 0), {kTime, 170}, {kIntensity, 45}, {kInput, 6}, {kTreble, -4}, {kWobble, 75}, {kNoise, 55},
};
static const PresetValue kOnTheEdge[] = {
    HE_HEADS(0, 1, 0), {kTime, 180}, {kIntensity, 62}, {kEcho, 50}, {kBass, -3},
};
static const PresetValue kSpringOnly[] = {
    HE_HEADS(0, 0, 0), {kEcho, 0}, {kReverb, 65}, {kWidth, 100},
};
static const PresetValue kSyncedDotted[] = {
    HE_HEADS(0, 0, 1), {kSync, 1}, {kDivision, 2}, {kIntensity, 40},
};
static const PresetValue kSyncedTriplets[] = {
    HE_HEADS(1, 1, 0), {kSync, 1}, {kDivision, 3}, {kIntensity, 30}, {kWidth, 60},
};
static const PresetValue kWetSend[] = {
    HE_HEADS(0, 1, 0), {kDry, 0}, {kEcho, 100}, {kReverb, 30},
};

#undef HE_HEADS

static const Preset kAll[] = {
    {"Default", nullptr, 0},
    HNL_PRESET("Slapback", kSlapback),
    HNL_PRESET("Echo and Spring", kEchoAndSpring),
    HNL_PRESET("Three Heads", kThreeHeads),
    HNL_PRESET("Gallop", kGallop),
    HNL_PRESET("Dub Throw", kDubThrow),
    HNL_PRESET("Dark Wash", kDarkWash),
    HNL_PRESET("Worn Tape", kWornTape),
    HNL_PRESET("On the Edge", kOnTheEdge),
    HNL_PRESET("Spring Only", kSpringOnly),
    HNL_PRESET("Synced: Dotted Eighth", kSyncedDotted),
    HNL_PRESET("Synced: Triplets", kSyncedTriplets),
    HNL_PRESET("Wet Only (for a send)", kWetSend),
};

static const int kCount = static_cast<int>(sizeof(kAll) / sizeof(kAll[0]));

/** Every control belongs to the preset. */
inline bool inPreset(uint32_t) { return true; }

/** The value a preset gives to an input parameter. */
inline float value(int preset, uint32_t id) { return hnl::presetValue(kAll[preset], id, kParams[id].def); }

} // namespace presets
