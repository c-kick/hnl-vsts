// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// Drives the plugin through DPF's own host-side wrapper, natively on Linux.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "src/DistrhoPluginInternal.hpp"

#include "../Params.hpp"
#include "../dsp/Sampler.hpp"

USE_NAMESPACE_DISTRHO

static int failures = 0;
static void check(bool ok, const char* what, double value = 0) {
    std::printf("%s  %-62s %.3f\n", ok ? "PASS" : "FAIL", what, value);
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

static MidiEvent midi(uint32_t frame, uint8_t status, uint8_t a, uint8_t b) {
    MidiEvent ev;
    ev.frame = frame;
    ev.size = 3;
    ev.data[0] = status; ev.data[1] = a; ev.data[2] = b; ev.data[3] = 0;
    ev.dataExt = nullptr;
    return ev;
}

constexpr int kOuts = DISTRHO_PLUGIN_NUM_OUTPUTS;
static std::vector<float> buf[kOuts];
static float* out[kOuts];

static float peakOf(int channel) {
    float p = 0;
    for (float v : buf[channel]) p = std::max(p, std::fabs(v));
    return p;
}

int main() {
    d_nextBufferSize = 512;
    d_nextSampleRate = 48000.0;
    PluginExporter plugin(nullptr, nullptr, nullptr, nullptr);
    for (int c = 0; c < kOuts; ++c) { buf[c].assign(512, 0.0f); out[c] = buf[c].data(); }

    check(plugin.getParameterCount() == kParamCount, "parameter count", plugin.getParameterCount());
    bool rangesOk = true;
    for (uint32_t i = 0; i < kParamCount; ++i) {
        const ParameterRanges& r = plugin.getParameterRanges(i);
        rangesOk = rangesOk && r.min < r.max && r.def >= r.min && r.def <= r.max && plugin.isParameterOutput(i) == (i >= kInputParamCount);
        if (i < kInputParamCount) rangesOk = rangesOk && std::fabs(normToParam(i, paramToNorm(i, r.def)) - r.def) < 1e-3f * (r.max - r.min);
    }
    check(rangesOk, "parameter ranges, defaults, knob scale");
    check(kVolume == 0 && kMacro1 == 1 && kMacro8 == 8, "0.1.0 parameter indices unchanged", kMacro8);
    check(kOuts == 2 * hnl::sampler::kOutputs, "one output pair for each the engine has", kOuts);

    bool guarded = true;
    for (uint32_t id = 0; id < kInputParamCount; ++id) {
        const ParameterRanges& range = plugin.getParameterRanges(id);
        plugin.setParameterValue(id, 1e9f);
        guarded = guarded && plugin.getParameterValue(id) == range.max;
        plugin.setParameterValue(id, -1e9f);
        guarded = guarded && plugin.getParameterValue(id) == range.min;
        plugin.setParameterValue(id, fromBits(0x7fc00000u));
        guarded = guarded && plugin.getParameterValue(id) == range.def;
    }
    check(guarded, "parameters: clamped, not-a-number gives the default");

    plugin.activate();
    plugin.run(nullptr, out, 512, nullptr, 0);
    check(peakOf(0) == 0.0f, "no notes: silence", peakOf(0));

    MidiEvent on = midi(300, 0x90, 60, 110);
    plugin.run(nullptr, out, 512, &on, 1);
    MidiEvent end = midi(0, 0x80, 60, 0);
    plugin.run(nullptr, out, 512, &end, 1);
    check(peakOf(0) == 0.0f && plugin.getParameterValue(kMeterVoices) == 0.0f, "the starting instrument is empty: a note makes no sound", peakOf(0));
    plugin.setState("instrument", "HNL Sampler instrument\nname = Saw\npart 1\np.osc_level = 100\n"); // one that sounds: a sawtooth
    plugin.run(nullptr, out, 512, &on, 1);
    int first = -1;
    for (int i = 0; i < 512 && first < 0; ++i) if (buf[0][i] != 0.0f) first = i;
    check(first >= 300 && first <= 302, "with an oscillator turned up, a note starts at its own sample (300)", first);
    for (int block = 0; block < 20; ++block) plugin.run(nullptr, out, 512, nullptr, 0);
    bool others = true;
    for (int c = 2; c < kOuts; ++c) others = others && peakOf(c) == 0.0f;
    check(peakOf(0) > 0.05f && peakOf(1) > 0.05f && others, "on the first output pair only", peakOf(0));
    check(plugin.getParameterValue(kMeterVoices) == 1.0f && plugin.getParameterValue(kMeterOutL) > -40.0f, "meters: one voice, output moving", plugin.getParameterValue(kMeterOutL));

    const float loud = peakOf(0);
    plugin.setParameterValue(kVolume, -20.0f);
    for (int block = 0; block < 20; ++block) plugin.run(nullptr, out, 512, nullptr, 0);
    check(std::fabs(20.0f * std::log10(peakOf(0) / loud) + 20.0f) < 0.5f, "volume turns it down (dB)", 20.0f * std::log10(peakOf(0) / loud));
    plugin.setParameterValue(kVolume, 0.0f);
    MidiEvent off = midi(0, 0x80, 60, 0);
    plugin.run(nullptr, out, 512, &off, 1);
    for (int block = 0; block < 100; ++block) plugin.run(nullptr, out, 512, nullptr, 0);
    check(plugin.getParameterValue(kMeterVoices) == 0.0f && peakOf(0) == 0.0f, "note off ends the voice");

    // The instrument is the plugin's state: what a host saves and gives back.
    check(plugin.getStateCount() == 1 && plugin.getStateKey(0) == "instrument", "the instrument is saved as state", plugin.getStateCount());
    const std::string init(plugin.getStateValue("instrument").buffer());
    check(init.find("HNL Sampler instrument") == 0 && init.find("p.osc_level = 100") != std::string::npos, "state: the instrument as text", init.size());
    const std::string other = "HNL Sampler instrument\nname = Square\npart 1\np.osc_level = 100\np.osc_wave = 3\np.output = 1\n";
    plugin.setState("instrument", other.c_str());
    const std::string back(plugin.getStateValue("instrument").buffer());
    check(back.find("name = Square") != std::string::npos && back.find("p.osc_wave = 3") != std::string::npos, "state: a saved instrument comes back");
    plugin.run(nullptr, out, 512, &on, 1);
    for (int block = 0; block < 10; ++block) plugin.run(nullptr, out, 512, nullptr, 0);
    check(peakOf(2) > 0.05f && peakOf(0) == 0.0f, "and plays: this one on the second output pair", peakOf(2));
    plugin.setState("instrument", "not an instrument");
    plugin.setState("instrument", "");
    check(std::string(plugin.getStateValue("instrument").buffer()) == back, "state: text that is no instrument changes nothing");

    // A macro moves what the instrument routes it to.
    plugin.setState("instrument", (other + "p.mod1_source = 15\np.mod1_dest = 2\np.mod1_amount = -100\n").c_str());
    plugin.setParameterValue(kMacro1, 100.0f);
    MidiEvent stop = midi(0, 0xb0, 120, 0);
    plugin.run(nullptr, out, 512, &stop, 1);
    on.frame = 0;
    plugin.run(nullptr, out, 512, &on, 1);
    for (int block = 0; block < 10; ++block) plugin.run(nullptr, out, 512, nullptr, 0);
    check(peakOf(2) < 1e-4f, "macro 1 routed to level at -100% silences the part", peakOf(2));
    plugin.setParameterValue(kMacro1, 0.0f);
    for (int block = 0; block < 10; ++block) plugin.run(nullptr, out, 512, nullptr, 0);
    check(peakOf(2) > 0.05f, "and back at 0 it sounds again", peakOf(2));
    plugin.run(nullptr, out, 512, &stop, 1);

    for (double fs : {96000.0, 44100.0}) {
        plugin.deactivate();
        plugin.setSampleRate(fs, true);
        plugin.activate();
        bool numbers = true;
        float top = 0;
        plugin.run(nullptr, out, 512, &on, 1);
        for (int block = 0; block < 40; ++block) {
            plugin.run(nullptr, out, 512, nullptr, 0);
            for (int i = 0; i < 512; ++i) {
                numbers = numbers && isNumber(buf[2][i]) && isNumber(buf[3][i]);
                top = std::max(top, std::max(std::fabs(buf[2][i]), std::fabs(buf[3][i])));
            }
        }
        check(numbers && top > 0.01f && top < 1.0f, fs > 50000 ? "runs after a sample rate change (peak)" : "runs at 44.1 kHz (peak)", top);
        plugin.run(nullptr, out, 512, &stop, 1);
    }
    check(hnl::guardTrips().load() == 0u, "the engine never had to be restarted", hnl::guardTrips().load());

    std::printf("%s\n", failures ? "FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
