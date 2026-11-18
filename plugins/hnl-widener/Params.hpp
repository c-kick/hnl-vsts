// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// Parameter table shared by the plugin and its UI.
#pragma once

#include <cmath>
#include <cstdint>

enum ParamId : uint32_t {
    kAmount, kHaas, kHaasTime, kHaasMode, kDiffuse, kSize, kDetune, kCents,
    kWidth, kLow, kHigh, kBass, kFocus, kDrift, kListen, kOutput, kMatch,
    // New parameters go here, at the end of the inputs: hosts store automation by index.
    kInputParamCount,

    // Meters (plugin -> UI/host).
    kMeterInL = kInputParamCount, kMeterInR, kMeterOutL, kMeterOutR, kMeterCorrelation, kMeterSideIn, kMeterSideOut, kMeterBalance, kMeterMatch,
    kParamCount
};

// kLog: a knob that moves in equal steps of pitch (the host still sees plain Hz).
// kSquare: a knob with finer steps at the low end (the host still sees the plain value).
enum ParamKind { kLinear, kLog, kSquare, kToggle, kChoice, kMeter };

static const char* const kHaasModeNames[] = {"Mono Safe", "Left Late", "Right Late"};
static const char* const kBassNames[] = {"As Is", "Mono"};
static const char* const kListenNames[] = {"Stereo", "Mono", "Sides"};
static const char* const kOffOnNames[] = {"Off", "On"};

struct ParamInfo {
    const char* name;
    const char* symbol;
    const char* unit;
    float min, max, def;
    ParamKind kind;
    const char* const* choices; // for kChoice and kToggle: max + 1 names
};

static const ParamInfo kParams[kParamCount] = {
    {"Amount", "amount", "%", 0.f, 100.f, 100.f, kLinear, nullptr},
    {"Haas", "haas", "%", 0.f, 100.f, 0.f, kLinear, nullptr},
    {"Haas Time", "haas_time", "ms", 1.f, 40.f, 12.f, kLinear, nullptr},
    {"Haas Mode", "haas_mode", "", 0.f, 2.f, 0.f, kChoice, kHaasModeNames},
    {"Diffuse", "diffuse", "%", 0.f, 100.f, 50.f, kLinear, nullptr},
    {"Diffuse Size", "size", "%", 0.f, 100.f, 50.f, kLinear, nullptr},
    {"Detune", "detune", "%", 0.f, 100.f, 0.f, kLinear, nullptr},
    {"Detune Cents", "cents", "ct", 1.f, 50.f, 9.f, kLinear, nullptr},
    {"Stereo Width", "width", "%", 0.f, 200.f, 100.f, kLinear, nullptr},
    {"Widen Above", "low", "Hz", 20.f, 2000.f, 120.f, kLog, nullptr},
    {"Widen Below", "high", "Hz", 1000.f, 20000.f, 20000.f, kLog, nullptr},
    {"Bass", "bass", "", 0.f, 1.f, 0.f, kToggle, kBassNames},
    {"Focus", "focus", "%", 0.f, 100.f, 0.f, kLinear, nullptr},
    {"Drift", "drift", "Hz", 0.f, 4.f, 0.f, kSquare, nullptr},
    {"Listen", "listen", "", 0.f, 2.f, 0.f, kChoice, kListenNames},
    {"Output", "output", "dB", -12.f, 12.f, 0.f, kLinear, nullptr},
    {"Level Match", "match", "", 0.f, 1.f, 0.f, kToggle, kOffOnNames},

    {"Input Peak Left", "meter_in_l", "dB", -100.f, 24.f, -100.f, kMeter, nullptr},
    {"Input Peak Right", "meter_in_r", "dB", -100.f, 24.f, -100.f, kMeter, nullptr},
    {"Output Peak Left", "meter_out_l", "dB", -100.f, 24.f, -100.f, kMeter, nullptr},
    {"Output Peak Right", "meter_out_r", "dB", -100.f, 24.f, -100.f, kMeter, nullptr},
    {"Left/Right Correlation", "meter_correlation", "", -1.f, 1.f, 1.f, kMeter, nullptr},
    {"Input Side Against Mid", "meter_side_in", "dB", -60.f, 24.f, -60.f, kMeter, nullptr},
    {"Output Side Against Mid", "meter_side_out", "dB", -60.f, 24.f, -60.f, kMeter, nullptr},
    {"Output Right Against Left", "meter_balance", "dB", -60.f, 24.f, 0.f, kMeter, nullptr},
    {"Level Match Gain", "meter_match", "dB", -24.f, 12.f, 0.f, kMeter, nullptr},
};

/** Knob position 0..1 for a parameter value. */
inline float paramToNorm(uint32_t id, float value) {
    const ParamInfo& p = kParams[id];
    if (p.kind == kLog) return std::log(value / p.min) / std::log(p.max / p.min);
    const float t = (value - p.min) / (p.max - p.min);
    return p.kind == kSquare ? std::sqrt(t < 0.f ? 0.f : t) : t;
}

/** Parameter value for a knob position 0..1. */
inline float normToParam(uint32_t id, float norm) {
    const ParamInfo& p = kParams[id];
    norm = norm < 0.f ? 0.f : (norm > 1.f ? 1.f : norm);
    if (norm <= 0.f) return p.min; // the ends exactly: Widen Below and Drift are off there
    if (norm >= 1.f) return p.max;
    if (p.kind == kLog) return p.min * std::pow(p.max / p.min, norm);
    if (p.kind == kSquare) return p.min + norm * norm * (p.max - p.min);
    return p.min + norm * (p.max - p.min);
}
