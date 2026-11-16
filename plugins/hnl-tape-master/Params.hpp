// Parameter table shared by the plugin and its UI.
#pragma once

#include <cmath>
#include <cstdint>

enum ParamId : uint32_t {
    kRecord, kReproduce, kLink,
    kSpeed, kFormula, kHead, kPath,
    kBias, kWow, kFlutter, kNoise,
    // New parameters go here, at the end of the inputs: hosts store automation by index.
    kInputParamCount,

    // Meters (plugin -> UI/host).
    kMeterVuL = kInputParamCount, kMeterVuR, kMeterOutL, kMeterOutR,
    kParamCount
};

enum ParamKind { kLinear, kToggle, kChoice, kMeter };

static const char* const kSpeedNames[] = {"7.5", "15", "30"};
static const char* const kFormulaNames[] = {"Vintage", "Classic", "Modern", "High Output"};
static const char* const kHeadNames[] = {"1/4\"", "1/2\"", "1\""};
static const char* const kPathNames[] = {"Repro", "Sync", "Input"};

struct ParamInfo {
    const char* name;
    const char* symbol;
    const char* unit;
    float min, max, def;
    ParamKind kind;
    const char* const* choices; // for kChoice: max + 1 names
};

static const ParamInfo kParams[kParamCount] = {
    {"Record Level", "record", "dB", -12.f, 12.f, 0.f, kLinear, nullptr},
    {"Reproduce Level", "reproduce", "dB", -12.f, 12.f, 0.f, kLinear, nullptr},
    {"Link Levels", "link", "", 0.f, 1.f, 1.f, kToggle, nullptr},

    {"Tape Speed", "speed", "ips", 0.f, 2.f, 2.f, kChoice, kSpeedNames},
    {"Tape Formula", "formula", "", 0.f, 3.f, 1.f, kChoice, kFormulaNames},
    {"Head Width", "head", "", 0.f, 2.f, 1.f, kChoice, kHeadNames},
    {"Path", "path", "", 0.f, 2.f, 0.f, kChoice, kPathNames},

    {"Bias", "bias", "%", -100.f, 100.f, 0.f, kLinear, nullptr},
    {"Wow", "wow", "%", 0.f, 100.f, 10.f, kLinear, nullptr},
    {"Flutter", "flutter", "%", 0.f, 100.f, 10.f, kLinear, nullptr},
    {"Noise", "noise", "%", 0.f, 100.f, 0.f, kLinear, nullptr},

    {"VU Left", "meter_vu_l", "dB", -60.f, 12.f, -60.f, kMeter, nullptr},
    {"VU Right", "meter_vu_r", "dB", -60.f, 12.f, -60.f, kMeter, nullptr},
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
