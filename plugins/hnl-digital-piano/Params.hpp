// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// Parameter table shared by the plugin and its UI.
#pragma once

#include <cmath>
#include <cstdint>

enum ParamId : uint32_t {
    kVolume, kTone, kVelocity, kDecay, kRelease,
    kDetune, kWidth, kReverb, kTune, kOctave,
    kVoice, // added in 0.4.0
    // New parameters go here, at the end of the inputs: hosts store automation by index.
    kInputParamCount,

    // Meters (plugin -> UI/host).
    kMeterOutL = kInputParamCount, kMeterOutR, kMeterVoices,
    kParamCount
};

enum ParamKind { kLinear, kLog, kToggle, kChoice, kMeter };

static const char* const kOctaveNames[] = {"-2", "-1", "0", "+1", "+2"};
static const char* const kVoiceNames[] = {"Workstation", "FM Electric", "Stage 80s", "Dance 90s", "Grand"};
constexpr int kVoiceChoices = sizeof(kVoiceNames) / sizeof(kVoiceNames[0]);

struct ParamInfo {
    const char* name;
    const char* symbol;
    const char* unit;
    float min, max, def;
    ParamKind kind;
    const char* const* choices; // for kChoice: max + 1 names
};

static const ParamInfo kParams[kParamCount] = {
    {"Volume", "volume", "dB", -24.f, 12.f, 0.f, kLinear, nullptr},
    {"Tone", "tone", "%", -100.f, 100.f, 0.f, kLinear, nullptr},
    {"Velocity", "velocity", "%", 0.f, 100.f, 70.f, kLinear, nullptr},
    {"Decay", "decay", "%", 10.f, 100.f, 100.f, kLinear, nullptr},
    {"Release", "release", "ms", 50.f, 5000.f, 600.f, kLog, nullptr},
    {"Detune", "detune", "cents", 0.f, 30.f, 0.f, kLinear, nullptr},
    {"Width", "width", "%", 0.f, 100.f, 100.f, kLinear, nullptr},
    {"Reverb", "reverb", "%", 0.f, 100.f, 15.f, kLinear, nullptr},
    {"Tune", "tune", "cents", -100.f, 100.f, 0.f, kLinear, nullptr},
    {"Octave", "octave", "", 0.f, 4.f, 2.f, kChoice, kOctaveNames},
    {"Voice", "voice", "", 0.f, kVoiceChoices - 1.f, 0.f, kChoice, kVoiceNames},

    {"Output Peak Left", "meter_out_l", "dB", -100.f, 24.f, -100.f, kMeter, nullptr},
    {"Output Peak Right", "meter_out_r", "dB", -100.f, 24.f, -100.f, kMeter, nullptr},
    {"Voices", "meter_voices", "", 0.f, 64.f, 0.f, kMeter, nullptr},
};

/** Knob position 0..1 for a parameter value. */
inline float paramToNorm(uint32_t id, float value) {
    const ParamInfo& p = kParams[id];
    if (p.kind == kLog) return std::log(value / p.min) / std::log(p.max / p.min);
    return (value - p.min) / (p.max - p.min);
}

/** Parameter value for a knob position 0..1. */
inline float normToParam(uint32_t id, float norm) {
    const ParamInfo& p = kParams[id];
    norm = norm < 0.f ? 0.f : (norm > 1.f ? 1.f : norm);
    if (p.kind == kLog) return p.min * std::pow(p.max / p.min, norm);
    return p.min + norm * (p.max - p.min);
}
