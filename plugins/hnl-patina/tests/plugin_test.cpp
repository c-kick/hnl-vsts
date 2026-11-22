// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// Drives the plugin through DPF's own host-side wrapper, natively on Linux.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "src/DistrhoPluginInternal.hpp"

#include "../Params.hpp"
#include "../Presets.hpp"

USE_NAMESPACE_DISTRHO

static int failures = 0;
static void check(bool ok, const char* what, double value) {
    std::printf("%s  %-58s %.3f\n", ok ? "PASS" : "FAIL", what, value);
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
    std::set<std::string> symbols;
    for (uint32_t i = 0; i < kParamCount; ++i) {
        const ParameterRanges& r = plugin.getParameterRanges(i);
        rangesOk = rangesOk && r.min < r.max && r.def >= r.min && r.def <= r.max;
        rangesOk = rangesOk && plugin.isParameterOutput(i) == (i >= kInputParamCount);
        if (kParams[i].kind == kChoice) rangesOk = rangesOk && plugin.getParameterEnumValues(i).count == r.max + 1;
        symbols.insert(kParams[i].symbol);
    }
    check(rangesOk, "parameter ranges, defaults, choices", rangesOk);
    check(symbols.size() == kParamCount, "every parameter has a symbol of its own", static_cast<double>(symbols.size()));

    // The two scenes hold the same controls, in the same order, with the same ranges.
    bool twins = true;
    for (uint32_t s = 0; s < kSceneSize; ++s) {
        const ParamInfo& a = kParams[kSceneA + s];
        const ParamInfo& b = kParams[kSceneB + s];
        twins = twins && a.min == b.min && a.max == b.max && a.kind == b.kind && a.choices == b.choices;
        twins = twins && a.symbol[0] == 'a' && b.symbol[0] == 'b' && std::strcmp(a.symbol + 1, b.symbol + 1) == 0;
    }
    for (int m = 0; m < kModuleCount; ++m) {
        twins = twins && kParams[moduleParam(0, m, mOn)].kind == kToggle && kParams[moduleParam(0, m, mType)].kind == kChoice;
        twins = twins && static_cast<int>(kParams[moduleParam(0, m, mType)].max) + 1 == kModuleTypeCount[m];
    }
    check(twins, "scene B is scene A again; a module is on, type, amount, react, own knob", twins);

    bool presetsOk = true;
    for (int i = 0; i < presets::kCount; ++i)
        for (uint32_t j = 0; j < presets::kAll[i].count; ++j) {
            const hnl::PresetValue& v = presets::kAll[i].values[j];
            presetsOk = presetsOk && v.id < kInputParamCount && presets::inPreset(v.id) && v.value >= kParams[v.id].min && v.value <= kParams[v.id].max;
            if (kParams[v.id].kind != kLinear) presetsOk = presetsOk && v.value == std::floor(v.value);
        }
    check(presetsOk, "preset values are within range", presets::kCount);
    check(plugin.getStateCount() == 2 && plugin.getStateKey(0) == "preset" && plugin.getStateKey(1) == "edit", "preset name and the scene being edited are saved as state", plugin.getStateCount());
    check(kMorph == 0 && kMix == 1 && kOutput == 2 && kMains == 3 && kIdle == 4 && kSceneA == 5 && kSceneB == 36 && kInputParamCount == 67,
          "0.1.0 parameter indices unchanged", kInputParamCount);
    check(kSceneA + sNoiseMode == 25 && kSceneA + sSpaceDecay == 35 && kParamCount == 80, "  and within a scene; 13 outputs", kParamCount);
    check(plugin.getLatency() == 36 + 576, "latency reported at 48 kHz: 36 samples + 12 ms", plugin.getLatency());

    plugin.activate();
    std::vector<float> l(512), r(512);
    const float* in[2] = {l.data(), r.data()};
    float* out[2] = {l.data(), r.data()};
    long n = 0;

    // The defaults: a tone comes out about as loud as it went in, and the window is told what happens.
    float peak = 0, hits = 0, angleMoved = 0, firstAngle = -1;
    for (int block = 0; block < 300; ++block) {
        for (int i = 0; i < 512; ++i, ++n) {
            const double burst = std::fmod(n / 48000.0, 0.5) < 0.05 ? 0.5 : 0.1;
            l[i] = r[i] = static_cast<float>(burst * std::sin(6.283185307 * 330 * n / 48000.0));
        }
        plugin.run(in, out, 512);
        if (block > 100) for (float v : l) peak = std::max(peak, std::fabs(v));
        hits = std::max(hits, plugin.getParameterValue(kMeterHits));
        const float angle = plugin.getParameterValue(kMeterAngle);
        if (firstAngle < 0) firstAngle = angle;
        angleMoved = std::max(angleMoved, std::fabs(angle - firstAngle));
    }
    check(peak > 0.25f && peak < 1.0f, "defaults: bursts of 0.5 come out at (peak)", peak);
    check(hits > 0.3f && angleMoved > 0.3f, "the window sees the hits and the record turning (hits)", hits);
    check(std::fabs(plugin.getParameterValue(kMeterOutL) - 20 * std::log10(peak)) < 6.0f, "output peak meter (dBFS)", plugin.getParameterValue(kMeterOutL));

    // Every preset, and every type of every module at the extremes of the knobs, must stay finite.
    bool finite = true;
    const auto burst = [&](double level) {
        for (int block = 0; block < 4; ++block) {
            for (int i = 0; i < 512; ++i, ++n) l[i] = r[i] = static_cast<float>(level * std::sin(6.283185307 * 220 * n / 48000.0));
            plugin.run(in, out, 512);
            for (float v : l) finite = finite && std::isfinite(v) && std::fabs(v) < 40.0f;
        }
    };
    for (int i = 0; i < presets::kCount; ++i) {
        for (uint32_t id = 0; id < kInputParamCount; ++id) plugin.setParameterValue(id, presets::value(i, id));
        for (float morph : {0.0f, 50.0f, 100.0f}) {
            plugin.setParameterValue(kMorph, morph);
            burst(1.0);
        }
    }
    check(finite, "all presets stay finite, at Morph 0, 50 and 100%", presets::kCount);

    int combinations = 0;
    for (int type = 0; type < 4; ++type)
        for (int corner = 0; corner < 8; ++corner, ++combinations) {
            for (int s = 0; s < 2; ++s)
                for (int m = 0; m < kModuleCount; ++m) {
                    const uint32_t third = moduleParam(s, m, mThird);
                    plugin.setParameterValue(moduleParam(s, m, mOn), 1.0f);
                    plugin.setParameterValue(moduleParam(s, m, mType), static_cast<float>(std::min(type, kModuleTypeCount[m] - 1)));
                    plugin.setParameterValue(moduleParam(s, m, mAmount), (corner & 1) != s ? 100.0f : 0.0f);
                    plugin.setParameterValue(moduleParam(s, m, mReact), corner & 2 ? 100.0f : 0.0f);
                    plugin.setParameterValue(third, corner & 4 ? kParams[third].max : kParams[third].min);
                }
            plugin.setParameterValue(kMorph, corner & 2 ? 100.0f : 40.0f);
            plugin.setParameterValue(kIdle, corner & 4 ? 1.0f : 0.0f);
            plugin.setParameterValue(kOutput, corner & 1 ? 12.0f : -24.0f);
            burst(2.0);
        }
    check(finite, "every type at the corners of the knobs stays finite", combinations);

    { // Guards: bad parameter values, and samples that are not numbers.
        check(parametersAreGuarded(plugin), "parameters: clamped, not-a-number gives the default", 1);
        std::vector<float> gl(512), gr(512);
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
    check(plugin.getLatency() == 36 + 1152, "latency reported at 96 kHz", plugin.getLatency());
    plugin.activate();
    for (int i = 0; i < 512; ++i) l[i] = r[i] = 0.0f;
    plugin.run(in, out, 512);
    check(std::fabs(l[511]) < 0.05f, "runs at 96 kHz", l[511]);

    { // The commonest sample rate of all.
        plugin.deactivate();
        plugin.setSampleRate(44100.0, true);
        plugin.activate();
        std::vector<float> ql(512), qr(512);
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
