// Drives the plugin through DPF's own host-side wrapper, natively on Linux.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "src/DistrhoPluginInternal.hpp"

#include "../Params.hpp"
#include "../Presets.hpp"

USE_NAMESPACE_DISTRHO

static int failures = 0;
static void check(bool ok, const char* what, double value) {
    std::printf("%s  %-40s %.3f\n", ok ? "PASS" : "FAIL", what, value);
    if (!ok) ++failures;
}

int main() {
    d_nextBufferSize = 512;
    d_nextSampleRate = 48000.0;
    PluginExporter plugin(nullptr, nullptr, nullptr, nullptr);

    check(plugin.getParameterCount() == kParamCount, "parameter count", plugin.getParameterCount());
    check(plugin.getLatency() == 196, "reported latency at 48 kHz (samples)", plugin.getLatency());
    bool rangesOk = true;
    for (uint32_t i = 0; i < kParamCount; ++i) {
        const ParameterRanges& r = plugin.getParameterRanges(i);
        rangesOk = rangesOk && r.min < r.max && r.def >= r.min && r.def <= r.max;
        rangesOk = rangesOk && plugin.isParameterOutput(i) == (i >= kInputParamCount);
        rangesOk = rangesOk && std::fabs(normToParam(i, paramToNorm(i, r.def)) - r.def) < 1e-3f * (r.max - r.min);
    }
    check(rangesOk, "parameter ranges, defaults, directions", rangesOk);

    bool presetsOk = true;
    for (int i = 0; i < presets::kCount; ++i)
        for (uint32_t j = 0; j < presets::kAll[i].count; ++j) {
            const PresetValue& v = presets::kAll[i].values[j];
            presetsOk = presetsOk && v.id < kInputParamCount && v.value >= kParams[v.id].min && v.value <= kParams[v.id].max;
        }
    check(presetsOk, "preset values are within range", presets::kCount);
    check(plugin.getStateCount() == 1 && plugin.getStateKey(0) == "preset", "preset name is saved as state", plugin.getStateCount());
    check(kLimRelease == 27 && kMeterInL == 44, "0.1.0 parameter indices unchanged", kLimRelease);

    plugin.setParameterValue(kLimGain, 12.0f);
    plugin.setParameterValue(kLimCeiling, -1.0f);
    plugin.activate();

    std::vector<float> l(512), r(512);
    const float* in[2] = {l.data(), r.data()};
    float* out[2] = {l.data(), r.data()};
    float peak = 0;
    long n = 0;
    for (int block = 0; block < 600; ++block) { // 6.4 s
        for (int i = 0; i < 512; ++i, ++n) {
            const double t = n / 48000.0;
            l[i] = r[i] = static_cast<float>(0.5 * std::sin(6.283185307 * 80 * t) + 0.3 * std::sin(6.283185307 * 1000 * t)
                                             + 0.2 * std::sin(6.283185307 * 7000 * t));
        }
        plugin.run(in, out, 512);
        if (block > 10) for (float v : l) peak = std::max(peak, std::fabs(v));
    }
    const float peakDb = 20 * std::log10(peak);
    check(peakDb <= -0.99f && peakDb > -1.5f, "output peak held at ceiling (dBFS)", peakDb);
    check(plugin.getParameterValue(kMeterGrLim) < -3.0f, "limiter GR meter (dB)", plugin.getParameterValue(kMeterGrLim));
    check(plugin.getParameterValue(kMeterGrLow) < -0.5f, "low band GR meter (dB)", plugin.getParameterValue(kMeterGrLow));
    check(std::fabs(plugin.getParameterValue(kMeterOutL) - peakDb) < 0.5f, "output level meter (dB)", plugin.getParameterValue(kMeterOutL));
    const float lufs = plugin.getParameterValue(kMeterLufsI);
    check(lufs > -12.0f && lufs < -3.0f, "integrated loudness (LUFS)", lufs);
    check(plugin.getParameterValue(kMeterTruePeak) <= -0.9f, "true-peak hold (dBTP)", plugin.getParameterValue(kMeterTruePeak));

    for (int block = 0; block < 400; ++block) {
        if (block == 20) plugin.setState("reset", ""); // after the tail has left the look-ahead delay
        std::fill(l.begin(), l.end(), 0.0f); // in and out share buffers
        std::fill(r.begin(), r.end(), 0.0f);
        plugin.run(in, out, 512);
    }
    check(plugin.getParameterValue(kMeterTruePeak) < -90.0f, "reset clears true-peak hold", plugin.getParameterValue(kMeterTruePeak));
    check(plugin.getParameterValue(kMeterGrLim) > -0.01f, "GR meter recovers in silence", plugin.getParameterValue(kMeterGrLim));

    // Every preset, driven hard: the limiter must still hold its ceiling.
    float worst = -100.0f;
    for (int i = 0; i < presets::kCount; ++i) {
        for (uint32_t id = 0; id < kInputParamCount; ++id) plugin.setParameterValue(id, presets::value(i, id));
        plugin.setParameterValue(kLimOn, 1.0f);
        plugin.setParameterValue(kInputGain, 12.0f);
        float pk = 0;
        n = 0;
        for (int block = 0; block < 150; ++block) {
            for (int k = 0; k < 512; ++k, ++n) {
                const double t = n / 48000.0;
                l[k] = static_cast<float>(0.5 * std::sin(6.283185307 * 80 * t) + 0.3 * std::sin(6.283185307 * 1000 * t));
                r[k] = static_cast<float>(0.4 * std::sin(6.283185307 * 130 * t) + 0.3 * std::sin(6.283185307 * 7000 * t));
            }
            plugin.run(in, out, 512);
            if (block > 10) for (int k = 0; k < 512; ++k) pk = std::max(pk, std::max(std::fabs(l[k]), std::fabs(r[k])));
        }
        worst = std::max(worst, 20 * std::log10(pk) - plugin.getParameterValue(kLimCeiling));
    }
    check(worst <= 0.01f && worst > -3.0f, "all presets hold the ceiling (dB over)", worst);

    plugin.deactivate();
    plugin.setSampleRate(96000.0, true);
    check(plugin.getLatency() == 340, "latency follows sample rate", plugin.getLatency());

    std::printf("%s\n", failures ? "FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
