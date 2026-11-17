// Factory presets: see common/Presets.hpp for how they work.
#pragma once

#include "../../common/Presets.hpp"
#include "../../common/UserPresets.hpp"
#include "Params.hpp"

namespace presets {

using hnl::Preset;
using hnl::PresetValue;

#define SP_RANGES(a, b, c, d) {kLow, a}, {kLowMid, b}, {kHighMid, c}, {kHigh, d}

static const PresetValue kVocalGentle[] = {
    {kDepth, 5}, {kSensitivity, 45}, {kAttack, 15}, {kRelease, 200}, SP_RANGES(0, 80, 120, 60),
};
static const PresetValue kVocalClear[] = {
    {kDepth, 9}, {kSensitivity, 55}, {kAttack, 8}, {kRelease, 160}, SP_RANGES(0, 100, 140, 80),
};
static const PresetValue kVocalDeep[] = {
    {kDepth, 15}, {kSensitivity, 65}, {kAttack, 5}, {kRelease, 140}, SP_RANGES(20, 120, 150, 100),
};
static const PresetValue kKickBass[] = {
    {kDepth, 14}, {kSensitivity, 60}, {kAttack, 1}, {kRelease, 110}, SP_RANGES(150, 60, 0, 0),
};
static const PresetValue kSnareSpace[] = {
    {kDepth, 8}, {kSensitivity, 50}, {kAttack, 1}, {kRelease, 90}, SP_RANGES(0, 120, 120, 60),
};
static const PresetValue kLeadPads[] = {
    {kDepth, 10}, {kSensitivity, 60}, {kAttack, 20}, {kRelease, 300}, SP_RANGES(40, 100, 130, 110),
};
static const PresetValue kPump[] = {
    {kDepth, 14}, {kSensitivity, 65}, {kAttack, 1}, {kRelease, 220}, {kLink, 100},
};
static const PresetValue kPumpSoft[] = {
    {kDepth, 6}, {kSensitivity, 55}, {kAttack, 4}, {kRelease, 300}, {kLink, 100},
};
static const PresetValue kHalfLinked[] = {
    {kDepth, 10}, {kSensitivity, 60}, {kAttack, 5}, {kRelease, 180}, {kLink, 50},
};
static const PresetValue kSelfSmooth[] = {
    {kSource, 1}, {kDepth, 6}, {kSensitivity, 40}, {kAttack, 3}, {kRelease, 80}, SP_RANGES(0, 40, 150, 120),
};

#undef SP_RANGES

static const Preset kAll[] = {
    {"Default", nullptr, 0},
    HNL_PRESET("Vocal Space: Gentle", kVocalGentle),
    HNL_PRESET("Vocal Space: Clear", kVocalClear),
    HNL_PRESET("Vocal Space: Deep", kVocalDeep),
    HNL_PRESET("Kick Through the Bass", kKickBass),
    HNL_PRESET("Snare Through the Guitars", kSnareSpace),
    HNL_PRESET("Lead Over the Pads", kLeadPads),
    HNL_PRESET("Sidechain Pump", kPump),
    HNL_PRESET("Sidechain Pump: Soft", kPumpSoft),
    HNL_PRESET("Half Linked", kHalfLinked),
    HNL_PRESET("Self: Tame the Peaks", kSelfSmooth),
};

static const int kCount = static_cast<int>(sizeof(kAll) / sizeof(kAll[0]));

/** Listen is a way of checking, not part of a sound. */
inline bool inPreset(uint32_t id) { return id != kListen; }

/** The value a preset gives to an input parameter. */
inline float value(int preset, uint32_t id) { return hnl::presetValue(kAll[preset], id, paramInfo(id).def); }

/** The factory presets followed by the ones the user saved (common/UserPresets.hpp). */
inline hnl::PresetList list() {
    return hnl::PresetList("HNL Channel Spacer", kAll, kCount, kInputParamCount, [](uint32_t id) {
        const ParamInfo& p = paramInfo(id);
        return hnl::PresetParam{p.symbol, p.min, p.max, p.def};
    }, inPreset);
}

} // namespace presets
