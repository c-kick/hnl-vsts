// Parameter table shared by the plugin and its UI.
#pragma once

#include <cmath>
#include <cstdint>

/** Number of frequency bands. Each has two meters, so this also sets the parameter count. */
constexpr int kBands = 32;
constexpr double kBandLow = 25.0, kBandHigh = 19000.0;

enum ParamId : uint32_t {
    kSensitivity, kDepth, kAttack, kRelease,
    kLow, kLowMid, kHighMid, kHigh,
    kLink, kSource, kListen,
    // New parameters go here, at the end of the inputs: hosts store automation by index.
    kInputParamCount,

    // Meters (plugin -> UI/host).
    kMeterOutL = kInputParamCount, kMeterOutR,
    kMeterCut,                       // first of kBands: reduction per band, dB
    kMeterLevel = kMeterCut + kBands, // first of kBands: level of the ducker per band, dB
    kParamCount = kMeterLevel + kBands
};

enum ParamKind { kLinear, kLog, kToggle, kChoice, kMeter, kBandMeter };

static const char* const kSourceNames[] = {"Sidechain", "Self"};
static const char* const kListenNames[] = {"Output", "Sidechain", "Removed"};
enum { kListenOutput, kListenSidechain, kListenRemoved };

struct ParamInfo {
    const char* name;
    const char* symbol;
    const char* unit;
    float min, max, def;
    ParamKind kind;
    const char* const* choices; // for kChoice: max + 1 names
};

static const ParamInfo kInputParams[kInputParamCount + 2] = {
    {"Sensitivity", "sensitivity", "%", 0.f, 100.f, 50.f, kLinear, nullptr},
    {"Depth", "depth", "dB", 0.f, 24.f, 9.f, kLinear, nullptr},
    {"Attack", "attack", "ms", 0.5f, 200.f, 10.f, kLog, nullptr},
    {"Release", "release", "ms", 20.f, 2000.f, 150.f, kLog, nullptr},

    {"Low", "low", "%", 0.f, 200.f, 100.f, kLinear, nullptr},
    {"Low Mid", "low_mid", "%", 0.f, 200.f, 100.f, kLinear, nullptr},
    {"High Mid", "high_mid", "%", 0.f, 200.f, 100.f, kLinear, nullptr},
    {"High", "high", "%", 0.f, 200.f, 100.f, kLinear, nullptr},

    {"Link", "link", "%", 0.f, 100.f, 0.f, kLinear, nullptr},
    {"Source", "source", "", 0.f, 1.f, 0.f, kChoice, kSourceNames},
    {"Listen", "listen", "", 0.f, 2.f, 0.f, kChoice, kListenNames},

    {"Output Peak Left", "meter_out_l", "dB", -100.f, 24.f, -100.f, kMeter, nullptr},
    {"Output Peak Right", "meter_out_r", "dB", -100.f, 24.f, -100.f, kMeter, nullptr},
};

static const ParamInfo kCutMeter = {"Cut", "meter_cut", "dB", 0.f, 30.f, 0.f, kBandMeter, nullptr};
static const ParamInfo kLevelMeter = {"Level", "meter_level", "dB", -120.f, 24.f, -120.f, kBandMeter, nullptr};

/** The description of any parameter; the band meters share one each. */
inline const ParamInfo& paramInfo(uint32_t id) {
    if (id >= kMeterLevel) return kLevelMeter;
    if (id >= kMeterCut) return kCutMeter;
    return kInputParams[id];
}

/** Knob position 0..1 for a parameter value. */
inline float paramToNorm(uint32_t id, float value) {
    const ParamInfo& p = paramInfo(id);
    if (p.kind == kLog) return std::log(value / p.min) / std::log(p.max / p.min);
    return (value - p.min) / (p.max - p.min);
}

/** Parameter value for a knob position 0..1. */
inline float normToParam(uint32_t id, float norm) {
    const ParamInfo& p = paramInfo(id);
    norm = norm < 0.f ? 0.f : (norm > 1.f ? 1.f : norm);
    if (p.kind == kLog) return p.min * std::pow(p.max / p.min, norm);
    return p.min + norm * (p.max - p.min);
}

// ------------------------------------------------------------------ bands
// Shared by the engine and the display, so the picture shows what the engine does.

/** Centre frequency of a band: evenly spaced in pitch. */
inline double bandFreq(int band) { return kBandLow * std::pow(kBandHigh / kBandLow, band / double(kBands - 1)); }

/** Level of the ducker (dB, a full-scale sine reads 0) where ducking starts. */
inline double thresholdDb(double sensitivity) { return -12.0 - 0.6 * sensitivity; }

/** The four ranges are centred here; in between, the amount glides from one to the next. */
static const double kRangeFreqs[4] = {80.0, 400.0, 2000.0, 10000.0};

/** Amount (1 = 100%) for a frequency, from the four range controls (in %). */
inline double rangeAmount(double freq, const double* percent) {
    if (freq <= kRangeFreqs[0]) return 0.01 * percent[0];
    for (int i = 0; i < 3; ++i) {
        if (freq > kRangeFreqs[i + 1]) continue;
        double t = std::log(freq / kRangeFreqs[i]) / std::log(kRangeFreqs[i + 1] / kRangeFreqs[i]);
        t = t * t * (3.0 - 2.0 * t);
        return 0.01 * (percent[i] + t * (percent[i + 1] - percent[i]));
    }
    return 0.01 * percent[3];
}

/** Deepest cut of any band, whatever Depth and the ranges ask for. */
constexpr double kMaxCutDb = 30.0;

/** Cut (dB) for a band whose ducker level is `over` dB above the threshold: half a dB per dB, easing into `depth`. */
inline double cutFor(double over, double depth) {
    if (over <= 0.0 || depth < 0.01) return 0.0;
    if (depth > kMaxCutDb) depth = kMaxCutDb;
    return depth * std::tanh(0.5 * over / depth);
}

/** The carving filters overlap: this much wider than the band spacing (half width, in bands). */
constexpr double kCarveSpread = 2.5 / 3.14159265358979;

/** What the listener gets at each band centre (dB, negative = cut) from the cuts of all bands. */
inline void effectiveCut(const float* cutDb, float* outDb) {
    for (int j = 0; j < kBands; ++j) {
        double removed = 0.0;
        for (int k = 0; k < kBands; ++k) {
            const double m = (j - k) / kCarveSpread;
            removed += (1.0 - std::pow(10.0, -0.05 * cutDb[k])) / (1.0 + m * m);
        }
        removed /= 2.5; // the sum of all filters
        outDb[j] = static_cast<float>(20.0 * std::log10(removed < 0.97 ? 1.0 - removed : 0.03));
    }
}
