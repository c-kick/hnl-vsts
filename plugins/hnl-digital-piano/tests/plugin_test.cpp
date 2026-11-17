// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// Drives the plugin through DPF's own host-side wrapper, natively on Linux.
#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#include "src/DistrhoPluginInternal.hpp"

#include "../Params.hpp"
#include "../dsp/Piano.hpp"
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

static MidiEvent midi(uint32_t frame, uint8_t status, uint8_t a, uint8_t b) {
    MidiEvent ev;
    ev.frame = frame;
    ev.size = 3;
    ev.data[0] = status; ev.data[1] = a; ev.data[2] = b; ev.data[3] = 0;
    ev.dataExt = nullptr;
    return ev;
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
            presetsOk = presetsOk && v.id < kInputParamCount && presets::inPreset(v.id) && v.value >= kParams[v.id].min && v.value <= kParams[v.id].max;
        }
    check(presetsOk, "preset values are within range", presets::kCount);
    check(plugin.getStateCount() == 1 && plugin.getStateKey(0) == "preset", "preset name is saved as state", plugin.getStateCount());

    plugin.setParameterValue(kReverb, 0.0f);
    plugin.activate();
    std::vector<float> l(512), r(512);
    float* out[2] = {l.data(), r.data()};

    { // A voice chosen while the plugin runs: its waves are computed elsewhere, the audio never waits.
        using Clock = std::chrono::steady_clock;
        plugin.setParameterValue(kVoice, 4.0f); // Grand, not asked for before in this program
        MidiEvent first = midi(0, 0x90, 60, 110);
        const Clock::time_point t0 = Clock::now();
        plugin.run(nullptr, out, 512, &first, 1);
        const double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        check(ms < 100.0, "new voice: the audio block does not wait (ms)", ms);
        check(hnl::Bank::ready(hnl::kGrand) == nullptr, "its waves are not there yet", ms);
        float sounding = 0;
        for (float v : l) sounding = std::max(sounding, std::fabs(v));
        check(sounding > 0.001f, "the voice it had plays meanwhile (peak)", sounding);
        double waited = 0;
        for (; waited < 20000.0 && hnl::Bank::ready(hnl::kGrand) == nullptr; waited += 10.0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            plugin.run(nullptr, out, 512, nullptr, 0);
        }
        check(hnl::Bank::ready(hnl::kGrand) != nullptr, "the waves arrive by themselves (ms waited)", waited);
        MidiEvent quiet = midi(0, 0xb0, 120, 0);
        plugin.run(nullptr, out, 512, &quiet, 1);
        plugin.setParameterValue(kVoice, 0.0f);
        plugin.run(nullptr, out, 512, nullptr, 0);
        {
            // A second piano comes and goes while the first lives on: the shared worker thread stays.
            PluginExporter second(nullptr, nullptr, nullptr, nullptr);
            second.activate();
            second.run(nullptr, out, 512, nullptr, 0);
            second.deactivate();
        }
        plugin.setParameterValue(kVoice, 3.0f); // Dance: still computed after the second piano has gone
        for (waited = 0; waited < 20000.0 && hnl::Bank::ready(hnl::kDance) == nullptr; waited += 10.0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            plugin.run(nullptr, out, 512, nullptr, 0);
        }
        check(hnl::Bank::ready(hnl::kDance) != nullptr, "another voice after a second piano came and went (ms)", waited);
        plugin.setParameterValue(kVoice, 0.0f);
        plugin.run(nullptr, out, 512, nullptr, 0);
    }

    // Silence without notes; a note starts on the sample its event names.
    plugin.run(nullptr, out, 512, nullptr, 0);
    float peak = 0;
    for (float v : l) peak = std::max(peak, std::fabs(v));
    check(peak == 0.0f, "no notes: silence", peak);

    MidiEvent on = midi(300, 0x90, 60, 110);
    plugin.run(nullptr, out, 512, &on, 1);
    int first = -1;
    for (int i = 0; i < 512 && first < 0; ++i) if (l[i] != 0.0f || r[i] != 0.0f) first = i;
    check(first >= 300 && first <= 302, "note starts at its own sample (300)", first);

    float left = 0, right = 0, apart = 0;
    for (int block = 0; block < 40; ++block) {
        plugin.run(nullptr, out, 512, nullptr, 0);
        for (int i = 0; i < 512; ++i) { left += l[i] * l[i]; right += r[i] * r[i]; apart += (l[i] - r[i]) * (l[i] - r[i]); }
    }
    check(left > 0.1f && right > 0.1f && apart > 0.1f * (left + right), "the note sounds, in stereo (L-R share)", apart / (left + right));
    check(plugin.getParameterValue(kMeterVoices) == 1.0f && plugin.getParameterValue(kMeterOutL) > -40.0f, "meters: one voice, output moving", plugin.getParameterValue(kMeterOutL));

    MidiEvent off = midi(0, 0x80, 60, 0);
    plugin.run(nullptr, out, 512, &off, 1);
    for (int block = 0; block < 150; ++block) plugin.run(nullptr, out, 512, nullptr, 0);
    check(plugin.getParameterValue(kMeterVoices) == 0.0f, "note off ends the voice", plugin.getParameterValue(kMeterVoices));

    // Note on with velocity 0 counts as note off; running status of several events in one block.
    MidiEvent many[4] = {midi(0, 0x90, 48, 100), midi(10, 0x90, 52, 100), midi(20, 0x90, 55, 100), midi(400, 0x90, 52, 0)};
    plugin.run(nullptr, out, 512, many, 4);
    for (int block = 0; block < 150; ++block) plugin.run(nullptr, out, 512, nullptr, 0);
    check(plugin.getParameterValue(kMeterVoices) == 2.0f, "three notes on, one off by velocity 0", plugin.getParameterValue(kMeterVoices));
    MidiEvent stop = midi(0, 0xb0, 123, 0);
    plugin.run(nullptr, out, 512, &stop, 1);

    // Every preset plays a chord, finite and under full scale.
    bool finite = true;
    peak = 0;
    for (int preset = 0; preset < presets::kCount; ++preset) {
        for (uint32_t id = 0; id < kInputParamCount; ++id) plugin.setParameterValue(id, presets::value(preset, id));
        plugin.deactivate(); // a host starting the plugin waits for the preset's own voice
        plugin.activate();
        MidiEvent chord[5] = {midi(0, 0x90, 48, 127), midi(0, 0x90, 60, 127), midi(0, 0x90, 64, 127), midi(0, 0x90, 67, 127), midi(0, 0x90, 72, 127)};
        plugin.run(nullptr, out, 512, chord, 5);
        for (int block = 0; block < 100; ++block) {
            plugin.run(nullptr, out, 512, nullptr, 0);
            for (float v : l) { finite = finite && std::isfinite(v); peak = std::max(peak, std::fabs(v)); }
        }
        plugin.run(nullptr, out, 512, &stop, 1);
        for (int block = 0; block < 200; ++block) plugin.run(nullptr, out, 512, nullptr, 0);
    }
    check(finite && peak > 0.1f && peak < 1.0f, "all presets: a hard chord stays under full scale (peak)", peak);
    check(kVoice == 10 && kOctave == 9, "0.3.0 parameter indices unchanged", kVoice);

    { // Guards: bad parameter values must neither stick nor break the sound.
        check(parametersAreGuarded(plugin), "parameters: clamped, not-a-number gives the default", 1);
        bool numbers = true;
        float top = 0;
        MidiEvent note = midi(0, 0x90, 64, 120);
        plugin.run(nullptr, out, 512, &note, 1);
        for (int block = 0; block < 40; ++block) {
            plugin.run(nullptr, out, 512, nullptr, 0);
            for (int i = 0; i < 512; ++i) {
                numbers = numbers && isNumber(l[i]) && isNumber(r[i]);
                top = std::max(top, std::max(std::fabs(l[i]), std::fabs(r[i])));
            }
        }
        check(numbers && top > 0.01f && top < 1.0f, "and a note still sounds as it should (peak)", top);
        plugin.run(nullptr, out, 512, &stop, 1);
    }

    plugin.deactivate();
    plugin.setSampleRate(96000.0, true);
    plugin.activate();
    plugin.run(nullptr, out, 512, &on, 1);
    for (int block = 0; block < 20; ++block) {
        plugin.run(nullptr, out, 512, nullptr, 0);
        for (float v : l) finite = finite && std::isfinite(v);
    }
    check(finite, "runs after a sample rate change", finite);

    { // The commonest sample rate of all.
        plugin.deactivate();
        plugin.setSampleRate(44100.0, true);
        plugin.activate();
        bool numbers = true;
        float top = 0;
        plugin.run(nullptr, out, 512, &on, 1);
        for (int block = 0; block < 40; ++block) {
            plugin.run(nullptr, out, 512, nullptr, 0);
            for (int i = 0; i < 512; ++i) {
                numbers = numbers && isNumber(l[i]) && isNumber(r[i]);
                top = std::max(top, std::max(std::fabs(l[i]), std::fabs(r[i])));
            }
        }
        check(numbers && top > 0.01f && top < 1.0f, "runs at 44.1 kHz (peak)", top);
    }

    std::printf("%s\n", failures ? "FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
