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

/** Runs a click through the plugin and returns when the loudest echo arrives, in ms after it. */
static double echoTime(PluginExporter& plugin, double seconds = 1.5) {
    std::vector<float> l(512), r(512);
    const float* in[2] = {l.data(), r.data()};
    float* out[2] = {l.data(), r.data()};
    const int blocks = static_cast<int>(seconds * 48000 / 512);
    float best = 0;
    long at = 0, n = 0;
    for (int block = 0; block < blocks; ++block) {
        for (int i = 0; i < 512; ++i) l[i] = r[i] = 0.0f;
        if (block == 0) for (int i = 0; i < 48; ++i) l[i] = r[i] = 0.1f * std::sin(6.283185307f * i / 48.0f);
        plugin.run(in, out, 512);
        for (int i = 0; i < 512; ++i, ++n)
            if (n > 400 && std::fabs(l[i]) > best) { best = std::fabs(l[i]); at = n; }
    }
    return (at - 24) / 48.0;
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
        if (i < kInputParamCount && kParams[i].kind != kChoice && kParams[i].kind != kToggle)
            rangesOk = rangesOk && std::fabs(normToParam(i, paramToNorm(i, r.def)) - r.def) < 1e-3f * (r.max - r.min);
    }
    check(rangesOk, "parameter ranges, defaults, choices, knob scale", rangesOk);

    bool presetsOk = true;
    for (int i = 0; i < presets::kCount; ++i)
        for (uint32_t j = 0; j < presets::kAll[i].count; ++j) {
            const hnl::PresetValue& v = presets::kAll[i].values[j];
            presetsOk = presetsOk && v.id < kInputParamCount && v.value >= kParams[v.id].min && v.value <= kParams[v.id].max;
        }
    check(presetsOk, "preset values are within range", presets::kCount);
    check(plugin.getStateCount() == 1 && plugin.getStateKey(0) == "preset", "preset name is saved as state", plugin.getStateCount());

    // Defaults: head 2 at twice 125 ms.
    plugin.setParameterValue(kWobble, 0.0f);
    plugin.setParameterValue(kDry, 0.0f);
    plugin.setParameterValue(kIntensity, 0.0f);
    plugin.activate();
    double t = echoTime(plugin);
    check(std::fabs(t - 250.0) < 2.0, "default: head 2 echoes after 250 ms", t);
    check(std::fabs(plugin.getParameterValue(kMeterTime) - 125.0f) < 0.5f, "head time meter (ms)", plugin.getParameterValue(kMeterTime));

    // Sync without a host tempo falls back to 120 bpm: 1/16 is 125 ms, 1/8 is 250 ms.
    plugin.setParameterValue(kSync, 1.0f);
    plugin.setParameterValue(kDivision, 4.0f);
    plugin.setParameterValue(kHead1, 1.0f);
    plugin.setParameterValue(kHead2, 0.0f);
    echoTime(plugin, 3.0); // let the motor settle
    t = echoTime(plugin);
    check(std::fabs(t - 250.0) < 3.0, "sync 1/8 at 120 bpm: head 1 after 250 ms", t);
    check(std::fabs(syncedTime(2, 140.0) - 0.107143) < 1e-5 && syncedTime(4, 50.0) <= 0.5 && syncedTime(0, 300.0) >= 0.02,
          "synced times: 1/16 at 140 bpm, limits", syncedTime(2, 140.0) * 1000.0);

    // Every preset runs, stays finite and bounded, with a loud input.
    std::vector<float> l(512), r(512);
    const float* in[2] = {l.data(), r.data()};
    float* out[2] = {l.data(), r.data()};
    bool finite = true;
    float top = 0;
    long n = 0;
    for (int preset = 0; preset < presets::kCount; ++preset) {
        for (uint32_t id = 0; id < kInputParamCount; ++id) plugin.setParameterValue(id, presets::value(preset, id));
        for (int block = 0; block < 400; ++block) {
            for (int i = 0; i < 512; ++i, ++n) l[i] = r[i] = static_cast<float>(0.5 * std::sin(6.283185307 * 220 * n / 48000.0));
            plugin.run(in, out, 512);
            for (float v : l) { finite = finite && std::isfinite(v); top = std::max(top, std::fabs(v)); }
        }
    }
    check(finite && top < 2.0f, "all presets stay finite and bounded (peak)", top);
    check(plugin.getParameterValue(kMeterOutL) > -20.0f && plugin.getParameterValue(kMeterTape) > -20.0f, "meters move (tape, dB)", plugin.getParameterValue(kMeterTape));

    plugin.deactivate();
    plugin.setSampleRate(96000.0, true);
    plugin.activate();
    for (int block = 0; block < 20; ++block) {
        for (int i = 0; i < 512; ++i) l[i] = r[i] = 0.1f;
        plugin.run(in, out, 512);
        for (float v : l) finite = finite && std::isfinite(v);
    }
    check(finite, "runs after a sample rate change", finite);

    std::printf("%s\n", failures ? "FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
