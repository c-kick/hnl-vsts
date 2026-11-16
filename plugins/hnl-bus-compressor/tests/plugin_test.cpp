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
    std::printf("%s  %-44s %.3f\n", ok ? "PASS" : "FAIL", what, value);
    if (!ok) ++failures;
}

int main() {
    d_nextBufferSize = 512;
    d_nextSampleRate = 48000.0;
    PluginExporter plugin(nullptr, nullptr, nullptr, nullptr);

    check(plugin.getParameterCount() == kParamCount, "parameter count", plugin.getParameterCount());
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
            if (kParams[v.id].kind == kChoice) presetsOk = presetsOk && v.value == std::floor(v.value);
        }
    check(presetsOk, "preset values are within range", presets::kCount);
    check(plugin.getStateCount() == 1 && plugin.getStateKey(0) == "preset", "preset name is saved as state", plugin.getStateCount());
    check(kThreshold == 0 && kMix == 6 && kInputParamCount == 7, "0.1.0 parameter indices unchanged", kMix);

    // A 1 kHz sine at -6 dBFS into threshold -26, 4:1, attack 1 ms, release 1.2 s, make-up +3.
    plugin.setParameterValue(kThreshold, -26.0f);
    plugin.setParameterValue(kAttack, 2.0f);
    plugin.setParameterValue(kRelease, 3.0f);
    plugin.setParameterValue(kMakeup, 3.0f);
    plugin.activate();

    std::vector<float> l(512), r(512);
    const float* in[2] = {l.data(), r.data()};
    float* out[2] = {l.data(), r.data()};
    const double amp = std::pow(10.0, -6.0 / 20.0);
    float peak = 0;
    long n = 0;
    for (int block = 0; block < 300; ++block) {
        for (int i = 0; i < 512; ++i, ++n) l[i] = r[i] = static_cast<float>(amp * std::sin(6.283185307 * 1000 * n / 48000.0));
        plugin.run(in, out, 512);
        if (block > 200) for (float v : l) peak = std::max(peak, std::fabs(v));
    }
    const float gr = plugin.getParameterValue(kMeterGr), peakDb = 20 * std::log10(peak);
    check(gr > 12.5f && gr < 15.0f, "reduction meter (dB), steady level gives 15", gr);
    check(std::fabs(peakDb - (-6.0f - gr + 3.0f)) < 0.3f, "output level = input - reduction + make-up", peakDb);
    check(std::fabs(plugin.getParameterValue(kMeterInL) + 6.0f) < 0.1f, "input peak meter (dBFS)", plugin.getParameterValue(kMeterInL));
    check(std::fabs(plugin.getParameterValue(kMeterOutR) - peakDb) < 0.1f, "output peak meter (dBFS)", plugin.getParameterValue(kMeterOutR));

    // Every combination of the selectors must run and stay finite, at the extremes of the knobs.
    bool finite = true;
    int combinations = 0;
    for (int ratio = 0; ratio < 4; ++ratio)
        for (int attack = 0; attack < 6; ++attack)
            for (int release = 0; release < 5; ++release, ++combinations) {
                plugin.setParameterValue(kRatio, ratio);
                plugin.setParameterValue(kAttack, attack);
                plugin.setParameterValue(kRelease, release);
                plugin.setParameterValue(kThreshold, combinations % 2 ? -40.0f : 0.0f);
                plugin.setParameterValue(kMakeup, combinations % 3 ? 20.0f : 0.0f);
                plugin.setParameterValue(kScHp, combinations % 2 ? 300.0f : 20.0f);
                plugin.setParameterValue(kMix, combinations % 4 ? 100.0f : 30.0f);
                for (int block = 0; block < 4; ++block) {
                    for (int i = 0; i < 512; ++i, ++n) l[i] = r[i] = static_cast<float>(std::sin(6.283185307 * 220 * n / 48000.0));
                    plugin.run(in, out, 512);
                    for (float v : l) finite = finite && std::isfinite(v) && std::fabs(v) < 11.0f;
                }
            }
    check(finite, "all 120 selector combinations stay finite", combinations);

    plugin.deactivate();
    plugin.setSampleRate(96000.0, true);
    plugin.activate();
    for (int i = 0; i < 512; ++i) l[i] = r[i] = 0.0f;
    plugin.run(in, out, 512);
    check(l[511] == 0.0f, "runs at 96 kHz", l[511]);

    std::printf("%s\n", failures ? "FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
