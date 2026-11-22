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

// A preset holds two scenes. A is the mild one and B the heavy one of the same character, so that
// Morph reads as "more". A(x) and B(x) are a scene's parameter x; BOTH(x, v) gives both the same value.
#define A(x) (kSceneA + (x))
#define B(x) (kSceneB + (x))
#define BOTH(x, v) {A(x), v}, {B(x), v}

// Types. Drive: 0 tube, 1 transistor, 2 diode, 3 fuzz. Motion: 0 off-centre, 1 warped, 2 slipping belt,
// 3 tired motor; speed 0 = 33, 1 = 45, 2 = 78. Vinyl: 0 dusty, 1 scratched, 2 worn groove, 3 shellac.
// Noise: 0 hum, 1 buzz, 2 static, 3 room tone; mode 0 follows the music, 1 ducks under it.
// Filter: 0 tilt, 1 band, 2 ladder. Space: 0 room, 1 chamber, 2 spring, 3 early digital.

static const PresetValue kSundayRecord[] = {
    {kMorph, 35},
    BOTH(sVinylType, 1), {A(sVinylAmount), 25}, {B(sVinylAmount), 55},
    {A(sDriveAmount), 12}, {B(sDriveAmount), 35},
    BOTH(sSpaceType, 1), {A(sSpaceAmount), 12}, {B(sSpaceAmount), 25}, BOTH(sSpaceDecay, 1.2f),
};
static const PresetValue kCharityShop45[] = {
    {kMorph, 40},
    BOTH(sMotionType, 1), BOTH(sMotionSpeed, 1), {A(sMotionAmount), 25}, {B(sMotionAmount), 65},
    BOTH(sVinylType, 1), {A(sVinylAmount), 35}, {B(sVinylAmount), 75}, {A(sVinylWear), 35}, {B(sVinylWear), 70},
    BOTH(sDriveType, 1), {A(sDriveAmount), 25}, {B(sDriveAmount), 50},
    {A(sFilterAmount), 25}, {B(sFilterAmount), 55},
};
static const PresetValue kGramophone[] = {
    {kMorph, 50},
    BOTH(sVinylType, 3), {A(sVinylAmount), 40}, {B(sVinylAmount), 80},
    BOTH(sMotionSpeed, 2), BOTH(sMotionType, 3), {A(sMotionAmount), 15}, {B(sMotionAmount), 40},
    BOTH(sDriveType, 2), {A(sDriveAmount), 30}, {B(sDriveAmount), 60},
    BOTH(sFilterType, 1), {A(sFilterAmount), 35}, {B(sFilterAmount), 70}, BOTH(sFilterRes, 20),
    BOTH(sSpaceOn, 0),
};
static const PresetValue kWornOut[] = {
    {kMorph, 45},
    BOTH(sVinylType, 2), {A(sVinylAmount), 35}, {B(sVinylAmount), 75}, {A(sVinylWear), 45}, {B(sVinylWear), 85},
    BOTH(sMotionType, 3), {A(sMotionAmount), 30}, {B(sMotionAmount), 70},
    BOTH(sFilterType, 2), {A(sFilterAmount), 20}, {B(sFilterAmount), 50}, BOTH(sFilterReact, 60), BOTH(sFilterRes, 25),
};
static const PresetValue kBasementAmp[] = {
    {kMorph, 40},
    BOTH(sDriveType, 3), {A(sDriveAmount), 20}, {B(sDriveAmount), 60}, BOTH(sDriveReact, 70), BOTH(sDriveTone, -25),
    BOTH(sMotionOn, 0), BOTH(sVinylOn, 0),
    BOTH(sNoiseOn, 1), BOTH(sNoiseType, 0), {A(sNoiseAmount), 25}, {B(sNoiseAmount), 50},
    BOTH(sFilterAmount, 20),
    BOTH(sSpaceType, 2), {A(sSpaceAmount), 20}, {B(sSpaceAmount), 45}, BOTH(sSpaceDecay, 1.6f),
};
static const PresetValue kDustyKeys[] = {
    {kMorph, 30},
    {A(sDriveAmount), 10}, {B(sDriveAmount), 30},
    BOTH(sMotionType, 1), {A(sMotionAmount), 25}, {B(sMotionAmount), 60}, BOTH(sMotionReact, 15),
    {A(sVinylAmount), 15}, {B(sVinylAmount), 40}, BOTH(sVinylReact, 80),
    {A(sFilterAmount), 20}, {B(sFilterAmount), 45}, BOTH(sFilterReact, 50),
    BOTH(sSpaceType, 1), {A(sSpaceAmount), 18}, {B(sSpaceAmount), 35}, BOTH(sSpaceDecay, 1.8f), BOTH(sSpaceReact, 60),
};
static const PresetValue kDrumCrate[] = {
    {kMorph, 40},
    BOTH(sDriveType, 1), {A(sDriveAmount), 30}, {B(sDriveAmount), 65}, BOTH(sDriveReact, 70),
    {A(sMotionAmount), 8}, {B(sMotionAmount), 20}, BOTH(sMotionReact, 70),
    BOTH(sVinylType, 1), {A(sVinylAmount), 20}, {B(sVinylAmount), 45}, BOTH(sVinylReact, 90),
    BOTH(sFilterType, 1), {A(sFilterAmount), 20}, {B(sFilterAmount), 45},
    {A(sSpaceAmount), 8}, {B(sSpaceAmount), 18}, BOTH(sSpaceDecay, 0.4f), BOTH(sSpaceReact, 70),
};
static const PresetValue kNightRadio[] = {
    {kMorph, 45},
    BOTH(sDriveType, 2), {A(sDriveAmount), 20}, {B(sDriveAmount), 45},
    BOTH(sMotionType, 2), {A(sMotionAmount), 40}, {B(sMotionAmount), 90},
    BOTH(sVinylOn, 0),
    BOTH(sNoiseOn, 1), BOTH(sNoiseType, 2), {A(sNoiseAmount), 30}, {B(sNoiseAmount), 60}, BOTH(sNoiseMode, 1), BOTH(sNoiseReact, 70),
    BOTH(sFilterType, 1), {A(sFilterAmount), 40}, {B(sFilterAmount), 75}, BOTH(sFilterRes, 30),
    BOTH(sSpaceOn, 0),
};
static const PresetValue kFirstDigital[] = {
    {kMorph, 35},
    {A(sDriveAmount), 10}, {B(sDriveAmount), 25},
    BOTH(sMotionOn, 0), BOTH(sVinylOn, 0),
    BOTH(sNoiseOn, 1), BOTH(sNoiseType, 3), {A(sNoiseAmount), 20}, {B(sNoiseAmount), 40},
    BOTH(sFilterType, 2), {A(sFilterAmount), 10}, {B(sFilterAmount), 35},
    BOTH(sSpaceType, 3), {A(sSpaceAmount), 30}, {B(sSpaceAmount), 60}, BOTH(sSpaceDecay, 2.2f),
};
// Two scenes that differ: A is nearly clean, B is a wreck. Morph is the whole effect.
static const PresetValue kCleanToWrecked[] = {
    {kMorph, 0},
    {A(sDriveAmount), 0}, {B(sDriveType), 3}, {B(sDriveAmount), 75},
    {A(sMotionAmount), 0}, {B(sMotionType), 3}, {B(sMotionAmount), 90}, {B(sMotionReact), 80},
    {A(sVinylAmount), 0}, {B(sVinylType), 2}, {B(sVinylAmount), 90}, {B(sVinylWear), 90},
    {B(sNoiseOn), 1}, {B(sNoiseType), 1}, {B(sNoiseAmount), 55},
    {A(sFilterAmount), 0}, {B(sFilterType), 2}, {B(sFilterAmount), 60}, {B(sFilterRes), 40},
    {A(sSpaceAmount), 5}, {B(sSpaceType), 2}, {B(sSpaceAmount), 50},
};

#undef A
#undef B
#undef BOTH

static const Preset kAll[] = {
    {"Default", nullptr, 0},
    HNL_PRESET("Sunday Record", kSundayRecord),
    HNL_PRESET("Charity Shop 45", kCharityShop45),
    HNL_PRESET("Gramophone", kGramophone),
    HNL_PRESET("Worn Out", kWornOut),
    HNL_PRESET("Basement Amp", kBasementAmp),
    HNL_PRESET("Dusty Keys", kDustyKeys),
    HNL_PRESET("Drum Crate", kDrumCrate),
    HNL_PRESET("Night Radio", kNightRadio),
    HNL_PRESET("First Digital", kFirstDigital),
    HNL_PRESET("Clean to Wrecked", kCleanToWrecked),
};

static const int kCount = static_cast<int>(sizeof(kAll) / sizeof(kAll[0]));

/** The output level and the mains frequency belong to the song and the place, not to a preset. */
inline bool inPreset(uint32_t id) { return id != kOutput && id != kMains; }

/** The value a preset gives to an input parameter. */
inline float value(int preset, uint32_t id) { return hnl::presetValue(kAll[preset], id, kParams[id].def); }

/** The factory presets followed by the ones the user saved (common/UserPresets.hpp). */
inline hnl::PresetList list() {
    return hnl::PresetList("HNL Patina", kAll, kCount, kInputParamCount, [](uint32_t id) {
        const ParamInfo& p = kParams[id];
        return hnl::PresetParam{p.symbol, p.min, p.max, p.def};
    }, inPreset);
}

} // namespace presets
