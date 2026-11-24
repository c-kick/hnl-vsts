// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

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
    // Added in 0.2.0. New parameters go at the end of this block so the
    // indices above, which hosts store with automation, never change.
    kEqOn, kEqLowFreq, kEqLowGain, kEqMidFreq, kEqMidGain, kEqMidQ, kEqHighFreq, kEqHighGain,
    kExcMode,
    kWideOn, kWideWidth, kWideFreq,
    kRevOn, kRevDecay, kRevDamping, kRevMix,
    kEqFirst, // added in 0.5.0
    // Added in 0.7.0.
    kLimSoft, kLimAttack,
    kEqLowCut, kEqMode,
    kLowSolo, kLowMute, kMidSolo, kMidMute, kHighSolo, kHighMute,
    kDither, kBypass, kMatch,
    kInputParamCount,

    // Meters (plugin -> UI/host), in dB or LUFS.
    kMeterInL = kInputParamCount, kMeterInR, kMeterOutL, kMeterOutR, kMeterTruePeak,
    kMeterGrLow, kMeterGrMid, kMeterGrHigh, kMeterGrLim,
    kMeterLufsM, kMeterLufsS, kMeterLufsI,
    kMeterMatch, kMeterCorr,
    kSpectrum, // the first of kSpectrumBands, low to high (hnl::Spectrum)
    kParamCount = kSpectrum + 32
};

enum ParamKind { kLinear, kLog, kToggle, kChoice, kMeter };

static const char* const kExciterModes[3] = {"Tape", "Tube", "Retro"};
static const char* const kEqModes[3] = {"Stereo", "Mid", "Side"};
static const char* const kDitherModes[3] = {"Off", "16 bit", "24 bit"};
constexpr int kSpectrumBands = 32;
static_assert(kParamCount - kSpectrum == kSpectrumBands, "one output parameter for each band of the spectrum");

/** The three names of a parameter that is a choice. */
inline const char* const* choiceNames(uint32_t id) { return id == kExcMode ? kExciterModes : (id == kEqMode ? kEqModes : kDitherModes); }

/** The low cut of the equaliser is off at the lowest setting of its knob. */
constexpr float kLowCutOff = 15.f;
inline bool lowCutIsOff(float hz) { return hz < kLowCutOff + 0.5f; }

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

    {"Equaliser", "eq_on", "", 0.f, 1.f, 0.f, kToggle},
    {"EQ Low Shelf Frequency", "eq_low_freq", "Hz", 20.f, 500.f, 80.f, kLog},
    {"EQ Low Shelf Gain", "eq_low_gain", "dB", -12.f, 12.f, 0.f, kLinear},
    {"EQ Peak Frequency", "eq_mid_freq", "Hz", 100.f, 10000.f, 1000.f, kLog},
    {"EQ Peak Gain", "eq_mid_gain", "dB", -12.f, 12.f, 0.f, kLinear},
    {"EQ Peak Q", "eq_mid_q", "Q", 0.3f, 8.f, 1.f, kLog},
    {"EQ High Shelf Frequency", "eq_high_freq", "Hz", 1000.f, 20000.f, 10000.f, kLog},
    {"EQ High Shelf Gain", "eq_high_gain", "dB", -12.f, 12.f, 0.f, kLinear},

    {"Exciter Mode", "exc_mode", "", 0.f, 2.f, 1.f, kChoice},

    {"Widener", "wide_on", "", 0.f, 1.f, 0.f, kToggle},
    {"Stereo Width", "wide_width", "%", 0.f, 200.f, 130.f, kLinear},
    {"Widen Above", "wide_freq", "Hz", 20.f, 1000.f, 200.f, kLog},

    {"Reverb", "rev_on", "", 0.f, 1.f, 0.f, kToggle},
    {"Reverb Decay", "rev_decay", "s", 0.2f, 6.f, 1.4f, kLog},
    {"Reverb Damping", "rev_damping", "Hz", 1000.f, 16000.f, 6000.f, kLog},
    {"Reverb Amount", "rev_mix", "%", 0.f, 50.f, 8.f, kLinear},

    {"EQ Before Compressor", "eq_first", "", 0.f, 1.f, 0.f, kToggle},

    {"Limiter Soft", "lim_soft", "", 0.f, 1.f, 0.f, kToggle},
    {"Limiter Attack", "lim_attack", "ms", 0.5f, 50.f, 3.f, kLog},

    {"EQ Low Cut", "eq_low_cut", "Hz", kLowCutOff, 150.f, kLowCutOff, kLog},
    {"EQ Works On", "eq_mode", "", 0.f, 2.f, 0.f, kChoice},

    {"Low Solo", "low_solo", "", 0.f, 1.f, 0.f, kToggle},
    {"Low Mute", "low_mute", "", 0.f, 1.f, 0.f, kToggle},
    {"Mid Solo", "mid_solo", "", 0.f, 1.f, 0.f, kToggle},
    {"Mid Mute", "mid_mute", "", 0.f, 1.f, 0.f, kToggle},
    {"High Solo", "high_solo", "", 0.f, 1.f, 0.f, kToggle},
    {"High Mute", "high_mute", "", 0.f, 1.f, 0.f, kToggle},

    {"Dither", "dither", "", 0.f, 2.f, 0.f, kChoice},
    {"Bypass", "bypass", "", 0.f, 1.f, 0.f, kToggle},
    {"Match Loudness", "match", "", 0.f, 1.f, 0.f, kToggle},

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
    {"Meter Match Gain", "meter_match", "dB", -24.f, 12.f, 0.f, kMeter},
    {"Meter Correlation", "meter_corr", "", -1.f, 1.f, 0.f, kMeter},
#define HM_SPEC(n) {"Spectrum " #n, "spectrum_" #n, "dB", -100.f, 24.f, -100.f, kMeter}
    HM_SPEC(01), HM_SPEC(02), HM_SPEC(03), HM_SPEC(04), HM_SPEC(05), HM_SPEC(06), HM_SPEC(07), HM_SPEC(08),
    HM_SPEC(09), HM_SPEC(10), HM_SPEC(11), HM_SPEC(12), HM_SPEC(13), HM_SPEC(14), HM_SPEC(15), HM_SPEC(16),
    HM_SPEC(17), HM_SPEC(18), HM_SPEC(19), HM_SPEC(20), HM_SPEC(21), HM_SPEC(22), HM_SPEC(23), HM_SPEC(24),
    HM_SPEC(25), HM_SPEC(26), HM_SPEC(27), HM_SPEC(28), HM_SPEC(29), HM_SPEC(30), HM_SPEC(31), HM_SPEC(32),
#undef HM_SPEC
};

/** The longest attack of the limiter as a brickwall, in ms (hnl::Limiter::kMaxLookMs); soft it uses the whole range. */
constexpr float kBrickwallMaxAttack = 5.f;

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
