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
    std::printf("%s  %-52s %.3f\n", ok ? "PASS" : "FAIL", what, value);
    if (!ok) ++failures;
}

struct Rig {
    std::vector<float> l, r, sl, sr;
    long n = 0;
    Rig() : l(512), r(512), sl(512), sr(512) {}

    /** Runs blocks of a 1 kHz tone as main and (optionally) a 1 kHz ducker; returns the last block's main peak. */
    float run(PluginExporter& plugin, int blocks, float duck, bool* finite = nullptr) {
        const float* in[4] = {l.data(), r.data(), sl.data(), sr.data()};
        float* out[2] = {l.data(), r.data()};
        float top = 0;
        for (int block = 0; block < blocks; ++block) {
            for (int i = 0; i < 512; ++i, ++n) {
                const float s = static_cast<float>(std::sin(6.283185307 * 1000 * n / 48000.0));
                l[i] = r[i] = 0.25f * s;
                sl[i] = sr[i] = duck * s;
            }
            plugin.run(in, out, 512);
            top = 0;
            for (float v : l) {
                if (finite) *finite = *finite && std::isfinite(v);
                top = std::max(top, std::fabs(v));
            }
        }
        return top;
    }
};

int main() {
    d_nextBufferSize = 512;
    d_nextSampleRate = 48000.0;
    PluginExporter plugin(nullptr, nullptr, nullptr, nullptr);

    check(plugin.getParameterCount() == kParamCount, "parameter count", plugin.getParameterCount());
    bool rangesOk = true;
    for (uint32_t i = 0; i < kParamCount; ++i) {
        const ParameterRanges& r = plugin.getParameterRanges(i);
        const ParamInfo& p = paramInfo(i);
        rangesOk = rangesOk && r.min < r.max && r.def >= r.min && r.def <= r.max;
        rangesOk = rangesOk && plugin.isParameterOutput(i) == (i >= kInputParamCount);
        if (p.kind == kChoice) rangesOk = rangesOk && plugin.getParameterEnumValues(i).count == r.max + 1;
        if (i < kInputParamCount && p.kind != kChoice && p.kind != kToggle)
            rangesOk = rangesOk && std::fabs(normToParam(i, paramToNorm(i, r.def)) - r.def) < 1e-3f * (r.max - r.min);
    }
    check(rangesOk, "parameter ranges, defaults, choices, knob scale", rangesOk);

    bool namesOk = true;
    for (uint32_t i = 0; i < kParamCount; ++i)
        for (uint32_t j = i + 1; j < kParamCount; ++j)
            namesOk = namesOk && plugin.getParameterSymbol(i) != plugin.getParameterSymbol(j);
    check(namesOk, "every parameter has its own symbol", namesOk);
    check((plugin.getParameterHints(kMeterCut) & kParameterIsHidden) && !(plugin.getParameterHints(kMeterOutL) & kParameterIsHidden),
          "band meters are hidden from the host's lists", kBands * 2);
    check((plugin.getAudioPort(true, 2).hints & kAudioPortIsSidechain) && (plugin.getAudioPort(true, 3).hints & kAudioPortIsSidechain)
              && !(plugin.getAudioPort(true, 0).hints & kAudioPortIsSidechain),
          "inputs 3 and 4 are the sidechain", 1);

    bool presetsOk = true;
    for (int i = 0; i < presets::kCount; ++i)
        for (uint32_t j = 0; j < presets::kAll[i].count; ++j) {
            const hnl::PresetValue& v = presets::kAll[i].values[j];
            presetsOk = presetsOk && v.id < kInputParamCount && v.value >= paramInfo(v.id).min && v.value <= paramInfo(v.id).max;
        }
    check(presetsOk, "preset values are within range", presets::kCount);
    check(plugin.getStateCount() == 1 && plugin.getStateKey(0) == "preset", "preset name is saved as state", plugin.getStateCount());

    plugin.activate();
    Rig rig;
    float top = rig.run(plugin, 100, 0.0f);
    check(std::fabs(top - 0.25f) < 1e-6f, "silent sidechain: main passes untouched (peak)", top);
    top = rig.run(plugin, 100, 0.25f);
    check(top < 0.15f && top > 0.07f, "sidechain playing: main is turned down (peak)", top);
    int deepest = 0;
    for (int k = 1; k < kBands; ++k)
        if (plugin.getParameterValue(kMeterCut + k) > plugin.getParameterValue(kMeterCut + deepest)) deepest = k;
    check(std::fabs(std::log2(bandFreq(deepest) / 1000.0)) < 0.2, "the deepest cut is in the band at 1 kHz (Hz)", bandFreq(deepest));
    check(plugin.getParameterValue(kMeterLevel + deepest) > -20.0f && plugin.getParameterValue(kMeterLevel + 2) < -60.0f,
          "level meters show the ducker (dB at 1 kHz)", plugin.getParameterValue(kMeterLevel + deepest));
    check(std::fabs(plugin.getParameterValue(kMeterOutL) - 20.0f * std::log10(top)) < 1.0f, "output meter (dB)", plugin.getParameterValue(kMeterOutL));

    plugin.setParameterValue(kListen, kListenRemoved);
    const float removed = rig.run(plugin, 50, 0.25f);
    check(std::fabs(removed + top - 0.25f) < 0.01f, "listen removed: the part taken out (peak)", removed);
    plugin.setParameterValue(kListen, kListenOutput);

    // Every preset runs and never makes the signal louder.
    bool finite = true;
    float loudest = 0;
    for (int preset = 0; preset < presets::kCount; ++preset) {
        for (uint32_t id = 0; id < kInputParamCount; ++id) plugin.setParameterValue(id, presets::value(preset, id));
        loudest = std::max(loudest, rig.run(plugin, 200, 0.5f, &finite));
    }
    check(finite && loudest <= 0.2501f, "all presets stay finite and never add level (peak)", loudest);

    plugin.deactivate();
    plugin.setSampleRate(96000.0, true);
    plugin.activate();
    rig.run(plugin, 20, 0.5f, &finite);
    check(finite, "runs after a sample rate change", finite);

    std::printf("%s\n", failures ? "FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
