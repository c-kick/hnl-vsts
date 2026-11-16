// Drives the plugin through DPF's own host-side wrapper, natively on Linux.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
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

// What a host must never send. Made from bits and tested by bits, so that no compiler setting can reason them away.
static float fromBits(uint32_t bits) {
    float v;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}
static bool isNumber(float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    return (bits & 0x7f800000u) != 0x7f800000u;
}

/** Every input parameter: far too high, far too low, and not a number. Leaves all of them at their default. */
static bool parametersAreGuarded(PluginExporter& plugin) {
    bool ok = true;
    for (uint32_t id = 0; id < kInputParamCount; ++id) {
        const ParameterRanges& range = plugin.getParameterRanges(id);
        plugin.setParameterValue(id, 1e9f);
        ok = ok && plugin.getParameterValue(id) == range.max;
        plugin.setParameterValue(id, -1e9f);
        ok = ok && plugin.getParameterValue(id) == range.min;
        plugin.setParameterValue(id, fromBits(0x7fc00000u));
        ok = ok && plugin.getParameterValue(id) == range.def;
    }
    return ok;
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


    { // Guards: bad parameter values, and samples that are not numbers.
        check(parametersAreGuarded(plugin), "parameters: clamped, not-a-number gives the default", 1);
        std::vector<float> gl(512), gr(512), gs(512, 0.0f);
        const float* gin[2] = {gl.data(), gr.data()};
        float* gout[2] = {gl.data(), gr.data()};
        bool numbers = true;
        float last = 0;
        long gn = 0;
        for (int block = 0; block < 200; ++block) {
            for (int i = 0; i < 512; ++i, ++gn) gl[i] = gr[i] = static_cast<float>(0.25 * std::sin(6.283185307 * 1000 * gn / 48000.0));
            if (block == 2) {
                gl[10] = fromBits(0x7fc00000u); // NaN
                gr[20] = fromBits(0x7f800000u); // +infinity
                gl[30] = fromBits(0xff800000u); // -infinity
            }
            plugin.run(gin, gout, 512);
            last = 0;
            for (int i = 0; i < 512; ++i) {
                numbers = numbers && isNumber(gl[i]) && isNumber(gr[i]);
                last = std::max(last, std::max(std::fabs(gl[i]), std::fabs(gr[i])));
            }
        }
        check(numbers, "NaN and infinity in: every output sample is a number", numbers);
        check(last > 0.01f && last < 4.0f, "and the signal is back afterwards (peak of the last block)", last);
    }

    plugin.deactivate();
    plugin.setSampleRate(96000.0, true);
    plugin.activate();
    for (int i = 0; i < 512; ++i) l[i] = r[i] = 0.0f;
    plugin.run(in, out, 512);
    check(l[511] == 0.0f, "runs at 96 kHz", l[511]);
    check(plugin.getLatency() == 96, "latency follows the sample rate", plugin.getLatency());

    { // The commonest sample rate of all.
        plugin.deactivate();
        plugin.setSampleRate(44100.0, true);
        plugin.activate();
        std::vector<float> ql(512), qr(512), qs(512, 0.0f);
        const float* qin[2] = {ql.data(), qr.data()};
        float* qout[2] = {ql.data(), qr.data()};
        bool numbers = true;
        float last = 0;
        long qn = 0;
        for (int block = 0; block < 100; ++block) {
            for (int i = 0; i < 512; ++i, ++qn) ql[i] = qr[i] = static_cast<float>(0.25 * std::sin(6.283185307 * 1000 * qn / 44100.0));
            plugin.run(qin, qout, 512);
            last = 0;
            for (int i = 0; i < 512; ++i) {
                numbers = numbers && isNumber(ql[i]) && isNumber(qr[i]);
                last = std::max(last, std::max(std::fabs(ql[i]), std::fabs(qr[i])));
            }
        }
        check(numbers && last > 0.01f && last < 4.0f, "runs at 44.1 kHz (peak of the last block)", last);
    }

    std::printf("%s\n", failures ? "FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
