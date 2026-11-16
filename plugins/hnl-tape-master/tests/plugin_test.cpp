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
    std::printf("%s  %-42s %.3f\n", ok ? "PASS" : "FAIL", what, value);
    if (!ok) ++failures;
}

int main() {
    d_nextBufferSize = 512;
    d_nextSampleRate = 48000.0;
    PluginExporter plugin(nullptr, nullptr, nullptr, nullptr);

    check(plugin.getParameterCount() == kParamCount, "parameter count", plugin.getParameterCount());
    check(plugin.getLatency() == 84, "reported latency at 48 kHz (samples)", plugin.getLatency());
    bool rangesOk = true;
    for (uint32_t i = 0; i < kParamCount; ++i) {
        const ParameterRanges& r = plugin.getParameterRanges(i);
        rangesOk = rangesOk && r.min < r.max && r.def >= r.min && r.def <= r.max;
        rangesOk = rangesOk && plugin.isParameterOutput(i) == (i >= kInputParamCount);
        if (kParams[i].kind == kChoice) rangesOk = rangesOk && plugin.getParameterEnumValues(i).count == r.max + 1;
    }
    check(rangesOk, "parameter ranges, defaults, choices", rangesOk);

    bool presetsOk = true;
    for (int i = 0; i < presets::kCount; ++i)
        for (uint32_t j = 0; j < presets::kAll[i].count; ++j) {
            const hnl::PresetValue& v = presets::kAll[i].values[j];
            presetsOk = presetsOk && v.id < kInputParamCount && presets::inPreset(v.id) && v.value >= kParams[v.id].min && v.value <= kParams[v.id].max;
        }
    check(presetsOk, "preset values are within range", presets::kCount);
    check(plugin.getStateCount() == 1 && plugin.getStateKey(0) == "preset", "preset name is saved as state", plugin.getStateCount());
    check(kNoise == 10 && kCalibration == 11, "0.1.0 parameter indices unchanged", kNoise);

    plugin.setParameterValue(kRecord, 6.0f);
    plugin.activate();

    std::vector<float> l(512), r(512);
    const float* in[2] = {l.data(), r.data()};
    float* out[2] = {l.data(), r.data()};
    const double amp = std::pow(10.0, kParams[kCalibration].def / 20.0); // a 0 VU sine
    float peak = 0;
    long n = 0;
    for (int block = 0; block < 300; ++block) {
        for (int i = 0; i < 512; ++i, ++n) l[i] = r[i] = static_cast<float>(amp * std::sin(6.283185307 * 1000 * n / 48000.0));
        plugin.run(in, out, 512);
        if (block > 50) for (float v : l) peak = std::max(peak, std::fabs(v));
    }
    check(std::fabs(plugin.getParameterValue(kMeterVuL) - 6.0f) < 0.3f, "VU meter: 0 VU sine, record +6 (VU)", plugin.getParameterValue(kMeterVuL));
    const float peakDb = 20 * std::log10(peak);
    check(peakDb < -12.0f && peakDb > -13.5f, "linked output, slightly compressed (dBFS)", peakDb);
    check(std::fabs(plugin.getParameterValue(kMeterOutL) - peakDb) < 0.3f, "output peak meter (dBFS)", plugin.getParameterValue(kMeterOutL));

    // Every combination of the selectors must run and stay finite.
    bool finite = true;
    for (int speed = 0; speed < 3; ++speed)
        for (int formula = 0; formula < 4; ++formula)
            for (int head = 0; head < 3; ++head)
                for (int path = 0; path < 3; ++path) {
                    plugin.setParameterValue(kSpeed, speed);
                    plugin.setParameterValue(kFormula, formula);
                    plugin.setParameterValue(kHead, head);
                    plugin.setParameterValue(kPath, path);
                    plugin.setParameterValue(kNoise, 100.0f);
                    plugin.setParameterValue(kWow, 100.0f);
                    plugin.setParameterValue(kFlutter, 100.0f);
                    plugin.setParameterValue(kBias, path == 0 ? -100.0f : 100.0f);
                    for (int block = 0; block < 4; ++block) {
                        for (int i = 0; i < 512; ++i, ++n) l[i] = r[i] = static_cast<float>(std::sin(6.283185307 * 220 * n / 48000.0));
                        plugin.run(in, out, 512);
                        for (float v : l) finite = finite && std::isfinite(v) && std::fabs(v) < 4.0f;
                    }
                }
    check(finite, "all 108 selector combinations stay finite", finite);

    plugin.deactivate();
    plugin.setSampleRate(96000.0, true);
    check(plugin.getLatency() == 132, "latency follows sample rate", plugin.getLatency());

    std::printf("%s\n", failures ? "FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
