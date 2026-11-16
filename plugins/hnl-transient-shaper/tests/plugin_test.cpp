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
    std::printf("%s  %-46s %.3f\n", ok ? "PASS" : "FAIL", what, value);
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
    check(kAttack == 0 && kMix == 5 && kInputParamCount == 6, "0.1.0 parameter indices unchanged", kMix);
    check(plugin.getLatency() == 48, "reported latency at 48 kHz (samples)", plugin.getLatency());

    // Bursts of a 1 kHz sine, 50 ms on and 450 ms off, with Attack at +100%.
    plugin.setParameterValue(kAttack, 100.0f);
    plugin.activate();

    std::vector<float> l(512), r(512);
    const float* in[2] = {l.data(), r.data()};
    float* out[2] = {l.data(), r.data()};
    float peak = 0, boost = 0;
    long n = 0;
    for (int block = 0; block < 300; ++block) {
        for (int i = 0; i < 512; ++i, ++n) l[i] = r[i] = n % 24000 < 2400 ? static_cast<float>(0.25 * std::sin(6.283185307 * 1000 * n / 48000.0)) : 0.0f;
        plugin.run(in, out, 512);
        for (float v : l) peak = std::max(peak, std::fabs(v));
        boost = std::max(boost, plugin.getParameterValue(kMeterBoost));
    }
    const float peakDb = 20 * std::log10(peak / 0.25f);
    check(boost > 11.0f && boost < 12.01f, "boost meter on bursts (dB)", boost);
    check(std::fabs(peakDb - boost) < 0.5f, "the bursts come out that much louder (dB)", peakDb);
    check(plugin.getParameterValue(kMeterCut) == 0.0f, "cut meter stays at zero", plugin.getParameterValue(kMeterCut));
    check(std::fabs(plugin.getParameterValue(kMeterInL) - 20 * std::log10(0.25f)) < 3.0f, "input peak meter (dBFS)", plugin.getParameterValue(kMeterInL));

    // Every preset, and every combination of the detector with the knobs at their extremes, must stay finite.
    bool finite = true;
    const auto burst = [&](double level) {
        for (int block = 0; block < 4; ++block) {
            for (int i = 0; i < 512; ++i, ++n) l[i] = r[i] = n % 1500 < 300 ? static_cast<float>(level * std::sin(6.283185307 * 220 * n / 48000.0)) : 0.0f;
            plugin.run(in, out, 512);
            for (float v : l) finite = finite && std::isfinite(v) && std::fabs(v) < 200.0f;
        }
    };
    for (int i = 0; i < presets::kCount; ++i) {
        for (uint32_t id = 0; id < kInputParamCount; ++id) plugin.setParameterValue(id, presets::value(i, id));
        burst(1.0);
    }
    check(finite, "all presets stay finite", presets::kCount);

    int combinations = 0;
    for (int focus = 0; focus < 4; ++focus)
        for (int corner = 0; corner < 16; ++corner, ++combinations) {
            plugin.setParameterValue(kFocus, focus);
            plugin.setParameterValue(kAttack, corner & 1 ? 100.0f : -100.0f);
            plugin.setParameterValue(kSustain, corner & 2 ? 100.0f : -100.0f);
            plugin.setParameterValue(kLength, corner & 4 ? 60.0f : 5.0f);
            plugin.setParameterValue(kOutput, corner & 8 ? 12.0f : -12.0f);
            plugin.setParameterValue(kMix, corner & 1 ? 100.0f : 30.0f);
            burst(2.0);
        }
    check(finite, "all 64 corner settings stay finite", combinations);

    plugin.deactivate();
    plugin.setSampleRate(96000.0, true);
    plugin.activate();
    for (int i = 0; i < 512; ++i) l[i] = r[i] = 0.0f;
    plugin.run(in, out, 512);
    check(l[511] == 0.0f, "runs at 96 kHz", l[511]);
    check(plugin.getLatency() == 96, "latency follows the sample rate", plugin.getLatency());

    std::printf("%s\n", failures ? "FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
