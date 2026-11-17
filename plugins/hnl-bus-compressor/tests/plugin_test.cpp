// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

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
    std::printf("%s  %-44s %.3f\n", ok ? "PASS" : "FAIL", what, value);
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
    check(plugin.getStateCount() == 2 && plugin.getStateKey(0) == "preset" && plugin.getStateKey(1) == "meter", "preset name and meter design are saved as state", plugin.getStateCount());
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
