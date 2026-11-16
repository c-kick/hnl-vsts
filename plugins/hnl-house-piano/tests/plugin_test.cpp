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
    for (int block = 0; block < 60; ++block) plugin.run(nullptr, out, 512, nullptr, 0);
    check(plugin.getParameterValue(kMeterVoices) == 0.0f, "note off ends the voice", plugin.getParameterValue(kMeterVoices));

    // Note on with velocity 0 counts as note off; running status of several events in one block.
    MidiEvent many[4] = {midi(0, 0x90, 48, 100), midi(10, 0x90, 52, 100), midi(20, 0x90, 55, 100), midi(400, 0x90, 52, 0)};
    plugin.run(nullptr, out, 512, many, 4);
    for (int block = 0; block < 60; ++block) plugin.run(nullptr, out, 512, nullptr, 0);
    check(plugin.getParameterValue(kMeterVoices) == 2.0f, "three notes on, one off by velocity 0", plugin.getParameterValue(kMeterVoices));
    MidiEvent stop = midi(0, 0xb0, 123, 0);
    plugin.run(nullptr, out, 512, &stop, 1);

    // Every preset plays a chord, finite and under full scale.
    bool finite = true;
    peak = 0;
    for (int preset = 0; preset < presets::kCount; ++preset) {
        for (uint32_t id = 0; id < kInputParamCount; ++id) plugin.setParameterValue(id, presets::value(preset, id));
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

    plugin.deactivate();
    plugin.setSampleRate(96000.0, true);
    plugin.activate();
    plugin.run(nullptr, out, 512, &on, 1);
    for (int block = 0; block < 20; ++block) {
        plugin.run(nullptr, out, 512, nullptr, 0);
        for (float v : l) finite = finite && std::isfinite(v);
    }
    check(finite, "runs after a sample rate change", finite);

    std::printf("%s\n", failures ? "FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
