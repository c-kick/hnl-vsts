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
    check(kAmount == 0 && kOutput == 17 && kInputParamCount == 18, "0.1.0 parameter indices unchanged", kOutput);

    // A 1 kHz sine on both channels at the default settings, room switched off: 1 kHz is a
    // whole number of waves in 3 ms, so both channels come out 1 + 0.6 times as loud.
    plugin.setParameterValue(kRoom, 0.0f);
    plugin.activate();

    std::vector<float> l(512), r(512);
    const float* in[2] = {l.data(), r.data()};
    float* out[2] = {l.data(), r.data()};
    float peakL = 0, peakR = 0;
    long n = 0;
    for (int block = 0; block < 100; ++block) {
        for (int i = 0; i < 512; ++i, ++n) l[i] = r[i] = static_cast<float>(0.25 * std::sin(6.283185307 * 1000 * n / 48000.0));
        plugin.run(in, out, 512);
        if (block < 50) continue;
        for (float v : l) peakL = std::max(peakL, std::fabs(v));
        for (float v : r) peakR = std::max(peakR, std::fabs(v));
    }
    check(std::fabs(peakL / 0.25f - 1.6f) < 0.01f, "1 kHz, left: 1.6 times as loud", peakL / 0.25f);
    check(std::fabs(peakR / 0.25f - 1.6f) < 0.01f, "1 kHz, right: the same (a rise of the comb)", peakR / 0.25f);
    check(std::fabs(plugin.getParameterValue(kMeterInL) - 20 * std::log10(0.25f)) < 0.5f, "input peak meter (dBFS)", plugin.getParameterValue(kMeterInL));
    check(std::fabs(plugin.getParameterValue(kMeterOutR) - 20 * std::log10(0.4f)) < 0.5f, "output peak meter (dBFS)", plugin.getParameterValue(kMeterOutR));

    // Every preset, and every combination of the switches with the knobs at their extremes, must stay finite.
    bool finite = true;
    const auto burst = [&](double level) {
        for (int block = 0; block < 4; ++block) {
            for (int i = 0; i < 512; ++i, ++n) l[i] = r[i] = n % 1500 < 300 ? static_cast<float>(level * std::sin(6.283185307 * 220 * n / 48000.0)) : 0.0f;
            plugin.run(in, out, 512);
            for (float v : l) finite = finite && std::isfinite(v) && std::fabs(v) < 200.0f;
            for (float v : r) finite = finite && std::isfinite(v) && std::fabs(v) < 200.0f;
        }
    };
    for (int i = 0; i < presets::kCount; ++i) {
        for (uint32_t id = 0; id < kInputParamCount; ++id) plugin.setParameterValue(id, presets::value(i, id));
        burst(1.0);
    }
    check(finite, "all presets stay finite", presets::kCount);

    int combinations = 0;
    for (int switches = 0; switches < 24; ++switches)
        for (int corner = 0; corner < 32; ++corner, ++combinations) {
            plugin.setParameterValue(kSide, switches % 3);
            plugin.setParameterValue(kRoom, (switches / 3) & 1);
            plugin.setParameterValue(kMatch, (switches / 6) & 1);
            plugin.setParameterValue(kListen, (switches / 12) & 1);
            for (uint32_t id : {kAmount, kDelayMix, kRoomMix}) plugin.setParameterValue(id, corner & 1 ? 100.0f : 0.0f);
            plugin.setParameterValue(kDelay, corner & 2 ? 20.0f : 0.1f);
            for (uint32_t id : {kSize, kDamp}) plugin.setParameterValue(id, corner & 4 ? 100.0f : 0.0f);
            plugin.setParameterValue(kDecay, corner & 4 ? 1.5f : 0.1f);
            plugin.setParameterValue(kOutput, corner & 8 ? 12.0f : -12.0f);
            for (uint32_t id : {kCrush, kDrive, kClean}) plugin.setParameterValue(id, corner & 16 ? 100.0f : 0.0f);
            plugin.setParameterValue(kGuard, corner & 2 ? 400.0f : 0.0f);
            plugin.setParameterValue(kLowCut, corner & 16 ? 2000.0f : 20.0f);
            plugin.setParameterValue(kHighCut, corner & 4 ? 500.0f : 20000.0f);
            burst(2.0);
        }
    check(finite, "all 768 corner settings stay finite", combinations);

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
