// Factory presets: see common/Presets.hpp for how they work.
#pragma once

#include "../../common/Presets.hpp"
#include "../../common/UserPresets.hpp"
#include "Params.hpp"

namespace presets {

using hnl::Preset;
using hnl::PresetValue;

// Speed: 0 = 7.5, 1 = 15, 2 = 30 ips. Formula: 0 vintage .. 3 high output.
// Head: 0 = 1/4", 1 = 1/2", 2 = 1". Path: 0 repro, 1 sync, 2 input.

static const PresetValue kClean30[] = {
    {kSpeed, 2}, {kFormula, 3}, {kHead, 2}, {kWow, 5}, {kFlutter, 5},
};
static const PresetValue kClassicMaster[] = {
    {kSpeed, 2}, {kFormula, 1}, {kHead, 1}, {kRecord, 3},
};
static const PresetValue kWarm15[] = {
    {kSpeed, 1}, {kFormula, 1}, {kHead, 1}, {kRecord, 3}, {kBias, 15},
};
static const PresetValue kFatLowEnd[] = {
    {kSpeed, 1}, {kFormula, 2}, {kHead, 0}, {kRecord, 2},
};
static const PresetValue kVintageGlue[] = {
    {kSpeed, 1}, {kFormula, 0}, {kHead, 1}, {kRecord, 2}, {kBias, 10},
};
static const PresetValue kDrivenTape[] = {
    {kSpeed, 1}, {kFormula, 1}, {kHead, 1}, {kRecord, 8}, {kBias, -10},
};
static const PresetValue kLoFi[] = {
    {kSpeed, 0}, {kFormula, 0}, {kHead, 0}, {kRecord, 6}, {kBias, -30}, {kWow, 60}, {kFlutter, 50}, {kNoise, 60},
};
static const PresetValue kTiredMachine[] = {
    {kSpeed, 1}, {kFormula, 0}, {kHead, 1}, {kWow, 90}, {kFlutter, 80}, {kNoise, 50},
};
static const PresetValue kSyncHead[] = {
    {kSpeed, 1}, {kFormula, 2}, {kPath, 1},
};
static const PresetValue kElectronicsOnly[] = {
    {kPath, 2},
};

static const Preset kAll[] = {
    {"Default", nullptr, 0},
    HNL_PRESET("Clean 30 ips", kClean30),
    HNL_PRESET("Classic Master", kClassicMaster),
    HNL_PRESET("Warm 15 ips", kWarm15),
    HNL_PRESET("Fat Low End", kFatLowEnd),
    HNL_PRESET("Vintage Glue", kVintageGlue),
    HNL_PRESET("Driven Tape", kDrivenTape),
    HNL_PRESET("Lo-Fi 7.5 ips", kLoFi),
    HNL_PRESET("Tired Machine", kTiredMachine),
    HNL_PRESET("Sync Head", kSyncHead),
    HNL_PRESET("Electronics Only", kElectronicsOnly),
};

static const int kCount = static_cast<int>(sizeof(kAll) / sizeof(kAll[0]));

/** Calibration matches the machine to the mix, so presets leave it alone. */
inline bool inPreset(uint32_t id) { return id != kCalibration; }

/** The value a preset gives to an input parameter. */
inline float value(int preset, uint32_t id) { return hnl::presetValue(kAll[preset], id, kParams[id].def); }

/** The factory presets followed by the ones the user saved (common/UserPresets.hpp). */
inline hnl::PresetList list() {
    return hnl::PresetList("HNL Tape Master", kAll, kCount, kInputParamCount, [](uint32_t id) {
        const ParamInfo& p = kParams[id];
        return hnl::PresetParam{p.symbol, p.min, p.max, p.def};
    }, inPreset);
}

} // namespace presets
