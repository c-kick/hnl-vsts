// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// Parameter table shared by the plugin and its UI.
#pragma once

#include <cmath>
#include <cstdint>

enum ParamId : uint32_t {
    kType, kThreshold, kRatio, kAttack, kRelease,
    kMakeup, kColour, kScHp, kMix,
    // New parameters go here, at the end of the inputs: hosts store automation by index.
    kInputParamCount,

    // Meters (plugin -> UI/host).
    kMeterGr = kInputParamCount, kMeterInL, kMeterInR, kMeterOutL, kMeterOutR,
    kParamCount
};

enum ParamKind { kLinear, kToggle, kChoice, kMeter };

static const char* const kTypeNames[] = {"Opto", "FET"};
static const char* const kRatioNames[] = {"3:1", "4:1", "8:1", "20:1"};

struct ParamInfo {
    const char* name;
    const char* symbol;
    const char* unit;
    float min, max, def;
    ParamKind kind;
    const char* const* choices; // for kChoice: max + 1 names
};

static const ParamInfo kParams[kParamCount] = {
    {"Type", "type", "", 0.f, 1.f, 0.f, kChoice, kTypeNames},
    {"Threshold", "threshold", "dB", -50.f, 0.f, -20.f, kLinear, nullptr},
    {"Ratio", "ratio", "", 0.f, 3.f, 1.f, kChoice, kRatioNames},
    {"Attack", "attack", "%", 0.f, 100.f, 50.f, kLinear, nullptr},
    {"Release", "release", "%", 0.f, 100.f, 50.f, kLinear, nullptr},
    {"Make-up", "makeup", "dB", 0.f, 24.f, 0.f, kLinear, nullptr},
    {"Colour", "colour", "%", 0.f, 100.f, 20.f, kLinear, nullptr},
    {"Sidechain High-pass", "sc_hp", "Hz", 20.f, 300.f, 20.f, kLinear, nullptr},
    {"Mix", "mix", "%", 0.f, 100.f, 100.f, kLinear, nullptr},

    {"Gain Reduction", "meter_gr", "dB", 0.f, 50.f, 0.f, kMeter, nullptr},
    {"Input Peak Left", "meter_in_l", "dB", -100.f, 24.f, -100.f, kMeter, nullptr},
    {"Input Peak Right", "meter_in_r", "dB", -100.f, 24.f, -100.f, kMeter, nullptr},
    {"Output Peak Left", "meter_out_l", "dB", -100.f, 24.f, -100.f, kMeter, nullptr},
    {"Output Peak Right", "meter_out_r", "dB", -100.f, 24.f, -100.f, kMeter, nullptr},
};

/** Knob position 0..1 for a parameter value. */
inline float paramToNorm(uint32_t id, float value) {
    const ParamInfo& p = kParams[id];
    return (value - p.min) / (p.max - p.min);
}

/** Parameter value for a knob position 0..1. */
inline float normToParam(uint32_t id, float norm) {
    const ParamInfo& p = kParams[id];
    norm = norm < 0.f ? 0.f : (norm > 1.f ? 1.f : norm);
    return p.min + norm * (p.max - p.min);
}
