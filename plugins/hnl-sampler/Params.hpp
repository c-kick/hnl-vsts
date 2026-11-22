// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// The parameters the host sees, shared by the plugin and its UI. They are few:
// the instrument itself (zones, filters, envelopes and the rest) is a document
// the plugin keeps as state (dsp/Model.hpp). The eight macros are how a host
// automates it: each can move any setting through the modulation matrix.
#pragma once

#include <cstdint>

enum ParamId : uint32_t {
    kVolume,
    kMacro1, kMacro2, kMacro3, kMacro4, kMacro5, kMacro6, kMacro7, kMacro8,
    // New parameters go here, at the end of the inputs: hosts store automation by index.
    kInputParamCount,

    // Meters (plugin -> UI/host).
    kMeterOutL = kInputParamCount, kMeterOutR, kMeterVoices,
    kParamCount
};

/** All of them run evenly from min to max. `meter` marks the ones the plugin reports. */
struct ParamInfo {
    const char* name;
    const char* symbol;
    const char* unit;
    float min, max, def;
    bool meter;
};

static const ParamInfo kParams[kParamCount] = {
    {"Volume", "volume", "dB", -48.f, 12.f, 0.f, false},
    {"Macro 1", "macro1", "%", 0.f, 100.f, 0.f, false},
    {"Macro 2", "macro2", "%", 0.f, 100.f, 0.f, false},
    {"Macro 3", "macro3", "%", 0.f, 100.f, 0.f, false},
    {"Macro 4", "macro4", "%", 0.f, 100.f, 0.f, false},
    {"Macro 5", "macro5", "%", 0.f, 100.f, 0.f, false},
    {"Macro 6", "macro6", "%", 0.f, 100.f, 0.f, false},
    {"Macro 7", "macro7", "%", 0.f, 100.f, 0.f, false},
    {"Macro 8", "macro8", "%", 0.f, 100.f, 0.f, false},

    {"Output Peak Left", "meter_out_l", "dB", -100.f, 24.f, -100.f, true},
    {"Output Peak Right", "meter_out_r", "dB", -100.f, 24.f, -100.f, true},
    {"Voices", "meter_voices", "", 0.f, 64.f, 0.f, true},
};

/** Knob position 0..1 for a parameter value. */
inline float paramToNorm(uint32_t id, float value) { return (value - kParams[id].min) / (kParams[id].max - kParams[id].min); }

/** Parameter value for a knob position 0..1. */
inline float normToParam(uint32_t id, float norm) {
    norm = norm < 0.f ? 0.f : (norm > 1.f ? 1.f : norm);
    return kParams[id].min + norm * (kParams[id].max - kParams[id].min);
}
