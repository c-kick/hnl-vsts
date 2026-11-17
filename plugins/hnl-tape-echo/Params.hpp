// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// Parameter table shared by the plugin and its UI.
#pragma once

#include <cmath>
#include <cstdint>

#include "../../common/dsp/Safety.hpp"

enum ParamId : uint32_t {
    kInput, kTime, kSync, kDivision,
    kHead1, kHead2, kHead3,
    kIntensity, kEcho, kReverb,
    kBass, kTreble,
    kWobble, kNoise, kWidth, kDry,
    // New parameters go here, at the end of the inputs: hosts store automation by index.
    kInputParamCount,

    // Meters (plugin -> UI/host).
    kMeterTape = kInputParamCount, kMeterOutL, kMeterOutR, kMeterTime,
    kParamCount
};

enum ParamKind { kLinear, kLog, kToggle, kChoice, kMeter };

static const char* const kDivisionNames[] = {"1/32", "1/16T", "1/16", "1/8T", "1/8"};
/** Length of each division in quarter notes: the time from the record head to head 1. */
static const double kDivisionBeats[] = {0.125, 1.0 / 6.0, 0.25, 1.0 / 3.0, 0.5};

struct ParamInfo {
    const char* name;
    const char* symbol;
    const char* unit;
    float min, max, def;
    ParamKind kind;
    const char* const* choices; // for kChoice: max + 1 names
};

static const ParamInfo kParams[kParamCount] = {
    {"Input", "input", "dB", -12.f, 24.f, 0.f, kLinear, nullptr},
    {"Time", "time", "ms", 40.f, 400.f, 125.f, kLog, nullptr},
    {"Tempo Sync", "sync", "", 0.f, 1.f, 0.f, kToggle, nullptr},
    {"Sync Division", "division", "", 0.f, 4.f, 2.f, kChoice, kDivisionNames},

    {"Head 1", "head1", "", 0.f, 1.f, 0.f, kToggle, nullptr},
    {"Head 2", "head2", "", 0.f, 1.f, 1.f, kToggle, nullptr},
    {"Head 3", "head3", "", 0.f, 1.f, 0.f, kToggle, nullptr},

    {"Intensity", "intensity", "%", 0.f, 100.f, 35.f, kLinear, nullptr},
    {"Echo Level", "echo", "%", 0.f, 100.f, 60.f, kLinear, nullptr},
    {"Reverb Level", "reverb", "%", 0.f, 100.f, 0.f, kLinear, nullptr},

    {"Bass", "bass", "dB", -10.f, 10.f, 0.f, kLinear, nullptr},
    {"Treble", "treble", "dB", -10.f, 10.f, 0.f, kLinear, nullptr},

    {"Wobble", "wobble", "%", 0.f, 100.f, 25.f, kLinear, nullptr},
    {"Noise", "noise", "%", 0.f, 100.f, 0.f, kLinear, nullptr},
    {"Width", "width", "%", 0.f, 100.f, 50.f, kLinear, nullptr},
    {"Dry Signal", "dry", "", 0.f, 1.f, 1.f, kToggle, nullptr},

    {"Tape Level", "meter_tape", "dB", -60.f, 24.f, -60.f, kMeter, nullptr},
    {"Output Peak Left", "meter_out_l", "dB", -100.f, 24.f, -100.f, kMeter, nullptr},
    {"Output Peak Right", "meter_out_r", "dB", -100.f, 24.f, -100.f, kMeter, nullptr},
    {"Head 1 Time", "meter_time", "ms", 20.f, 500.f, 125.f, kMeter, nullptr},
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

/** Seconds from the record head to head 1 for a division at a tempo, kept within what the tape can do. */
inline double syncedTime(int division, double bpm) {
    // A tempo that is not a number, or not one music is played at, counts as 120.
    if (!hnl::isFinite(bpm) || !(bpm > 20.0 && bpm < 1000.0)) bpm = 120.0;
    double t = kDivisionBeats[division < 0 ? 0 : (division > 4 ? 4 : division)] * 60.0 / bpm;
    while (t > 0.5) t *= 0.5;
    while (t < 0.02) t *= 2.0;
    return t;
}
