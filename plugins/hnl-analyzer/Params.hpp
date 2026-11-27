// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// Parameter table shared by the plugin and its UI. None of them changes the sound: the plugin
// passes it on as it came in.
#pragma once

#include <cmath>
#include <cstdint>

enum ParamId : uint32_t {
    kMode,       // track, continuous
    kAuto,       // a capture follows the transport
    kRmsWindow,  // the length of the RMS windows shown
    kRmsRef,     // 0 dB is a sine or a square wave at full scale
    kTargetLufs, // the advisor's targets: these three are what a preset holds
    kMaxTruePeak,
    kPrefPlr,
    kInputParamCount,

    // What the meter reads now, for the host.
    kMeterLufsM = kInputParamCount, kMeterLufsS, kMeterLufsI, kMeterTruePeak, kMeterPlr, kMeterLra,
    kParamCount
};

enum ParamKind { kLinear, kLog, kToggle, kChoice, kMeter };

struct ParamInfo {
    const char* name;
    const char* symbol;
    const char* unit;
    float min, max, def;
    ParamKind kind;
};

static const ParamInfo kParams[kParamCount] = {
    {"Mode", "mode", "", 0.f, 1.f, 0.f, kChoice},
    {"Follow Transport", "auto", "", 0.f, 1.f, 1.f, kToggle},
    {"RMS Window", "rms_window", "", 0.f, 5.f, 2.f, kChoice},
    {"RMS Reference", "rms_ref", "", 0.f, 1.f, 0.f, kChoice},
    {"Target Loudness", "target_lufs", "LUFS", -30.f, -5.f, -14.f, kLinear},
    {"Maximum True Peak", "max_true_peak", "dBTP", -6.f, 0.f, -1.f, kLinear},
    {"Preferred PLR", "pref_plr", "dB", 4.f, 24.f, 13.f, kLinear},

    {"Loudness Momentary", "meter_lufs_m", "LUFS", -100.f, 24.f, -100.f, kMeter},
    {"Loudness Short-Term", "meter_lufs_s", "LUFS", -100.f, 24.f, -100.f, kMeter},
    {"Loudness Integrated", "meter_lufs_i", "LUFS", -100.f, 24.f, -100.f, kMeter},
    {"True Peak", "meter_true_peak", "dBTP", -100.f, 24.f, -100.f, kMeter},
    {"Peak To Loudness", "meter_plr", "dB", 0.f, 100.f, 0.f, kMeter},
    {"Loudness Range", "meter_lra", "LU", 0.f, 100.f, 0.f, kMeter},
};

/** The names of a choice, and how many there are. */
inline const char* const* choiceNames(uint32_t id, int& count) {
    static const char* const kModes[2] = {"Track", "Continuous"};
    static const char* const kWindows[6] = {"10 ms", "25 ms", "50 ms", "100 ms", "300 ms", "1 s"};
    static const char* const kRefs[2] = {"Sine", "Square"};
    count = id == kRmsWindow ? 6 : 2;
    return id == kMode ? kModes : (id == kRmsWindow ? kWindows : kRefs);
}

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
