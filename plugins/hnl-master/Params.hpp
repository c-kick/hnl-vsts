// Parameter table shared by the plugin and its UI.
#pragma once

#include <cmath>
#include <cstdint>

enum ParamId : uint32_t {
    kInputGain,
    kCompOn, kXoverLow, kXoverHigh,
    kLowThreshold, kLowRatio, kLowAttack, kLowRelease, kLowMakeup,
    kMidThreshold, kMidRatio, kMidAttack, kMidRelease, kMidMakeup,
    kHighThreshold, kHighRatio, kHighAttack, kHighRelease, kHighMakeup,
    kExcOn, kExcFreq, kExcDrive, kExcColour, kExcMix,
    kLimOn, kLimGain, kLimCeiling, kLimRelease,
    kInputParamCount,

    // Meters (plugin -> UI/host), in dB or LUFS.
    kMeterInL = kInputParamCount, kMeterInR, kMeterOutL, kMeterOutR, kMeterTruePeak,
    kMeterGrLow, kMeterGrMid, kMeterGrHigh, kMeterGrLim,
    kMeterLufsM, kMeterLufsS, kMeterLufsI,
    kParamCount
};

enum ParamKind { kLinear, kLog, kToggle, kMeter };

struct ParamInfo {
    const char* name;
    const char* symbol;
    const char* unit;
    float min, max, def;
    ParamKind kind;
};

static const ParamInfo kParams[kParamCount] = {
    {"Input Gain", "input_gain", "dB", -12.f, 12.f, 0.f, kLinear},

    {"Compressor", "comp_on", "", 0.f, 1.f, 1.f, kToggle},
    {"Crossover Low", "xover_low", "Hz", 40.f, 800.f, 120.f, kLog},
    {"Crossover High", "xover_high", "Hz", 800.f, 12000.f, 2500.f, kLog},

    {"Low Threshold", "low_threshold", "dB", -60.f, 0.f, -20.f, kLinear},
    {"Low Ratio", "low_ratio", ":1", 1.f, 20.f, 2.f, kLog},
    {"Low Attack", "low_attack", "ms", 0.1f, 200.f, 30.f, kLog},
    {"Low Release", "low_release", "ms", 10.f, 2000.f, 250.f, kLog},
    {"Low Gain", "low_gain", "dB", -12.f, 12.f, 0.f, kLinear},

    {"Mid Threshold", "mid_threshold", "dB", -60.f, 0.f, -20.f, kLinear},
    {"Mid Ratio", "mid_ratio", ":1", 1.f, 20.f, 2.f, kLog},
    {"Mid Attack", "mid_attack", "ms", 0.1f, 200.f, 15.f, kLog},
    {"Mid Release", "mid_release", "ms", 10.f, 2000.f, 150.f, kLog},
    {"Mid Gain", "mid_gain", "dB", -12.f, 12.f, 0.f, kLinear},

    {"High Threshold", "high_threshold", "dB", -60.f, 0.f, -20.f, kLinear},
    {"High Ratio", "high_ratio", ":1", 1.f, 20.f, 2.f, kLog},
    {"High Attack", "high_attack", "ms", 0.1f, 200.f, 5.f, kLog},
    {"High Release", "high_release", "ms", 10.f, 2000.f, 80.f, kLog},
    {"High Gain", "high_gain", "dB", -12.f, 12.f, 0.f, kLinear},

    {"Exciter", "exc_on", "", 0.f, 1.f, 1.f, kToggle},
    {"Exciter Frequency", "exc_freq", "Hz", 1000.f, 12000.f, 5000.f, kLog},
    {"Exciter Drive", "exc_drive", "dB", 0.f, 24.f, 9.f, kLinear},
    {"Exciter Colour", "exc_colour", "%", 0.f, 100.f, 30.f, kLinear},
    {"Exciter Mix", "exc_mix", "%", 0.f, 100.f, 15.f, kLinear},

    {"Limiter", "lim_on", "", 0.f, 1.f, 1.f, kToggle},
    {"Limiter Gain", "lim_gain", "dB", 0.f, 24.f, 0.f, kLinear},
    {"Limiter Ceiling", "lim_ceiling", "dB", -6.f, 0.f, -1.f, kLinear},
    {"Limiter Release", "lim_release", "ms", 10.f, 1000.f, 150.f, kLog},

    {"Meter In L", "meter_in_l", "dB", -100.f, 24.f, -100.f, kMeter},
    {"Meter In R", "meter_in_r", "dB", -100.f, 24.f, -100.f, kMeter},
    {"Meter Out L", "meter_out_l", "dB", -100.f, 24.f, -100.f, kMeter},
    {"Meter Out R", "meter_out_r", "dB", -100.f, 24.f, -100.f, kMeter},
    {"Meter True Peak", "meter_true_peak", "dB", -100.f, 24.f, -100.f, kMeter},
    {"Meter GR Low", "meter_gr_low", "dB", -60.f, 0.f, 0.f, kMeter},
    {"Meter GR Mid", "meter_gr_mid", "dB", -60.f, 0.f, 0.f, kMeter},
    {"Meter GR High", "meter_gr_high", "dB", -60.f, 0.f, 0.f, kMeter},
    {"Meter GR Limiter", "meter_gr_lim", "dB", -60.f, 0.f, 0.f, kMeter},
    {"Loudness Momentary", "meter_lufs_m", "LUFS", -100.f, 24.f, -100.f, kMeter},
    {"Loudness Short-Term", "meter_lufs_s", "LUFS", -100.f, 24.f, -100.f, kMeter},
    {"Loudness Integrated", "meter_lufs_i", "LUFS", -100.f, 24.f, -100.f, kMeter},
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
