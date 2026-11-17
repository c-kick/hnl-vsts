// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// Parameter table shared by the plugin and its UI.
#pragma once

#include <cmath>
#include <cstdint>

enum ParamId : uint32_t {
    kAmount, kDelay, kDelayMix, kSide, kGuard, kLowCut, kHighCut, kClean,
    kRoom, kRoomMix, kSize, kDecay, kDamp, kCrush, kDrive, kMatch, kListen, kOutput,
    // New parameters go here, at the end of the inputs: hosts store automation by index.
    kInputParamCount,

    // Meters (plugin -> UI/host).
    kMeterInL = kInputParamCount, kMeterInR, kMeterOutL, kMeterOutR, kMeterCrush, kMeterMatch, kMeterCorrelation,
    kParamCount
};

// kLog: a knob that moves in equal steps of pitch (the host still sees plain Hz).
enum ParamKind { kLinear, kLog, kToggle, kChoice, kMeter };

static const char* const kSideNames[] = {"Left", "Right", "Both"};
static const char* const kOffOnNames[] = {"Off", "On"};
static const char* const kListenNames[] = {"All", "Copy"};

struct ParamInfo {
    const char* name;
    const char* symbol;
    const char* unit;
    float min, max, def;
    ParamKind kind;
    const char* const* choices; // for kChoice and kToggle: max + 1 names
};

static const ParamInfo kParams[kParamCount] = {
    {"Amount", "amount", "%", 0.f, 100.f, 60.f, kLinear, nullptr},
    {"Delay", "delay", "ms", 0.1f, 20.f, 3.f, kLinear, nullptr},
    {"Delay Mix", "delay_mix", "%", 0.f, 100.f, 62.f, kLinear, nullptr},
    {"Delayed Side", "side", "", 0.f, 2.f, 1.f, kChoice, kSideNames},
    {"Low Guard", "guard", "Hz", 0.f, 400.f, 0.f, kLinear, nullptr},
    {"Low Cut", "low_cut", "Hz", 20.f, 2000.f, 20.f, kLog, nullptr},
    {"High Cut", "high_cut", "Hz", 500.f, 20000.f, 20000.f, kLog, nullptr},
    {"Clean Hit", "clean", "%", 0.f, 100.f, 0.f, kLinear, nullptr},
    {"Room", "room", "", 0.f, 1.f, 1.f, kToggle, kOffOnNames},
    {"Room Mix", "room_mix", "%", 0.f, 100.f, 9.f, kLinear, nullptr},
    {"Room Size", "size", "%", 0.f, 100.f, 50.f, kLinear, nullptr},
    {"Room Decay", "decay", "s", 0.1f, 1.5f, 0.2f, kLinear, nullptr},
    {"Room Damping", "damp", "%", 0.f, 100.f, 84.f, kLinear, nullptr},
    {"Crush", "crush", "%", 0.f, 100.f, 0.f, kLinear, nullptr},
    {"Drive", "drive", "%", 0.f, 100.f, 0.f, kLinear, nullptr},
    {"Level Match", "match", "", 0.f, 1.f, 0.f, kToggle, kOffOnNames},
    {"Listen", "listen", "", 0.f, 1.f, 0.f, kChoice, kListenNames},
    {"Output", "output", "dB", -12.f, 12.f, 0.f, kLinear, nullptr},

    {"Input Peak Left", "meter_in_l", "dB", -100.f, 24.f, -100.f, kMeter, nullptr},
    {"Input Peak Right", "meter_in_r", "dB", -100.f, 24.f, -100.f, kMeter, nullptr},
    {"Output Peak Left", "meter_out_l", "dB", -100.f, 24.f, -100.f, kMeter, nullptr},
    {"Output Peak Right", "meter_out_r", "dB", -100.f, 24.f, -100.f, kMeter, nullptr},
    {"Crush Gain Reduction", "meter_crush", "dB", 0.f, 60.f, 0.f, kMeter, nullptr},
    {"Level Match Gain", "meter_match", "dB", -24.f, 12.f, 0.f, kMeter, nullptr},
    {"Left/Right Correlation", "meter_correlation", "", -1.f, 1.f, 1.f, kMeter, nullptr},
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
    if (norm <= 0.f) return p.min; // the ends exactly: a cut is off there
    if (norm >= 1.f) return p.max;
    if (p.kind == kLog) return p.min * std::pow(p.max / p.min, norm);
    return p.min + norm * (p.max - p.min);
}
