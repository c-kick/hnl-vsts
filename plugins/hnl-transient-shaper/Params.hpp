// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// Parameter table shared by the plugin and its UI.
#pragma once

#include <cmath>
#include <cstdint>

enum ParamId : uint32_t {
    kAttack, kSustain, kLength, kFocus, kOutput, kMix,
    // New parameters go here, at the end of the inputs: hosts store automation by index.
    kInputParamCount,

    // Meters (plugin -> UI/host).
    kMeterBoost = kInputParamCount, kMeterCut, kMeterInL, kMeterInR, kMeterOutL, kMeterOutR,
    kParamCount
};

enum ParamKind { kLinear, kToggle, kChoice, kMeter };

static const char* const kFocusNames[] = {"Full", "Low", "Mid", "High"};

struct ParamInfo {
    const char* name;
    const char* symbol;
    const char* unit;
    float min, max, def;
    ParamKind kind;
    const char* const* choices; // for kChoice: max + 1 names
};

static const ParamInfo kParams[kParamCount] = {
    {"Attack", "attack", "%", -100.f, 100.f, 0.f, kLinear, nullptr},
    {"Sustain", "sustain", "%", -100.f, 100.f, 0.f, kLinear, nullptr},
    {"Length", "length", "ms", 5.f, 60.f, 20.f, kLinear, nullptr},
    {"Detector", "focus", "", 0.f, 3.f, 0.f, kChoice, kFocusNames},
    {"Output", "output", "dB", -12.f, 12.f, 0.f, kLinear, nullptr},
    {"Mix", "mix", "%", 0.f, 100.f, 100.f, kLinear, nullptr},

    {"Gain Boost", "meter_boost", "dB", 0.f, 40.f, 0.f, kMeter, nullptr},
    {"Gain Cut", "meter_cut", "dB", 0.f, 40.f, 0.f, kMeter, nullptr},
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
