// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// Drives the plugin through DPF's own host-side wrapper, natively on Linux.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "src/DistrhoPluginInternal.hpp"

#include "../Params.hpp"
#include "../Presets.hpp"
#include "../dsp/Engine.hpp"

USE_NAMESPACE_DISTRHO
using namespace hnl::analyzer;

static int failures = 0;
static void check(bool ok, const char* what, double value = 0) {
    std::printf("%s  %-72s %.3f\n", ok ? "PASS" : "FAIL", what, value);
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

/** Music of a kind: two tones and noise, different left and right. */
static void signal(std::vector<float>& l, std::vector<float>& r, size_t n) {
    l.resize(n);
    r.resize(n);
    uint32_t seed = 7;
    for (size_t i = 0; i < n; ++i) {
        const double t = i / 48000.0;
        seed = seed * 1664525u + 1013904223u;
        const double noise = static_cast<double>(seed >> 8) / 8388608.0 - 1.0, hit = std::fmod(t, 0.5) < 0.12 ? 1.0 : 0.3;
        l[i] = static_cast<float>(hit * (0.4 * std::sin(6.283185307 * 110 * t) + 0.2 * std::sin(6.283185307 * 1870 * t) + 0.05 * noise));
        r[i] = static_cast<float>(hit * (0.4 * std::sin(6.283185307 * 110 * t + 0.5) + 0.2 * std::sin(6.283185307 * 2210 * t) - 0.05 * noise));
    }
}

/** Plays `l` and `r` through the plugin in blocks, the transport playing from `frame` or standing still. True when what came out is what went in, bit for bit. */
static bool play(PluginExporter& plugin, const std::vector<float>& l, const std::vector<float>& r, bool playing, uint64_t frame, uint32_t block = 512, bool inPlace = false) {
    bool same = true;
    std::vector<float> ol(block), orr(block);
    for (size_t at = 0; at < l.size(); at += block) {
        const uint32_t n = static_cast<uint32_t>(std::min<size_t>(block, l.size() - at));
        TimePosition time;
        time.playing = playing;
        time.frame = playing ? frame + at : frame;
        plugin.setTimePosition(time);
        if (inPlace) {
            std::memcpy(ol.data(), l.data() + at, sizeof(float) * n);
            std::memcpy(orr.data(), r.data() + at, sizeof(float) * n);
            const float* in[2] = {ol.data(), orr.data()};
            float* out[2] = {ol.data(), orr.data()};
            plugin.run(in, out, n);
        } else {
            const float* in[2] = {l.data() + at, r.data() + at};
            float* out[2] = {ol.data(), orr.data()};
            plugin.run(in, out, n);
        }
        same = same && std::memcmp(ol.data(), l.data() + at, sizeof(float) * n) == 0 && std::memcmp(orr.data(), r.data() + at, sizeof(float) * n) == 0;
    }
    return same;
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
        rangesOk = rangesOk && std::fabs(normToParam(i, paramToNorm(i, r.def)) - r.def) < 1e-3f * (r.max - r.min);
    }
    check(rangesOk, "parameter ranges, defaults, directions", rangesOk);
    check(kMode == 0 && kAuto == 1 && kRmsWindow == 2 && kRmsRef == 3 && kTargetLufs == 4 && kMaxTruePeak == 5 && kPrefPlr == 6 && kMeterLufsM == 7, "parameter indices of the first version", kPrefPlr);
    bool presetsOk = true;
    for (int i = 0; i < presets::kCount; ++i)
        for (uint32_t j = 0; j < presets::kAll[i].count; ++j) {
            const hnl::PresetValue& v = presets::kAll[i].values[j];
            presetsOk = presetsOk && v.id < kInputParamCount && presets::inPreset(v.id) && v.value >= kParams[v.id].min && v.value <= kParams[v.id].max;
        }
    for (uint32_t id = 0; id < kInputParamCount; ++id) presetsOk = presetsOk && presets::value(0, id) == kParams[id].def;
    check(presetsOk, "presets hold targets only, within range, and the first holds the defaults", presets::kCount);
    check(plugin.getStateCount() == 3 && plugin.getStateKey(0) == "preset" && plugin.getStateKey(1) == "analysis" && plugin.getStateKey(2) == "reference",
          "the preset name, the analysis and the choice of reference are saved as state", plugin.getStateCount());
    check(parametersAreGuarded(plugin), "parameters are pulled into range, and what is no number becomes the default", 1);

    plugin.activate();
    std::vector<float> l, r;
    signal(l, r, 48000 * 6);

    // The sound is not touched, in any mode, whatever the settings, and there is no delay.
    bool same = true;
    for (int mode = 0; mode < 3; ++mode) {
        plugin.setParameterValue(kMode, static_cast<float>(mode));
        for (uint32_t block : {1u, 17u, 512u, 4096u}) {
            same = same && play(plugin, l, r, true, 0, block, false);
            same = same && play(plugin, l, r, false, 0, block, true);
        }
    }
    for (uint32_t id = 0; id < kInputParamCount; ++id) {
        plugin.setParameterValue(id, plugin.getParameterRanges(id).max);
        same = same && play(plugin, l, r, true, 0, 256, true);
        plugin.setParameterValue(id, plugin.getParameterRanges(id).min);
        same = same && play(plugin, l, r, true, 0, 256, false);
        plugin.setParameterValue(id, plugin.getParameterRanges(id).def);
    }
    check(same, "what comes out is what went in, bit for bit: every mode, every setting, any block, in place or not", same);
    std::vector<float> loud(l), tiny(l);
    for (size_t i = 0; i < l.size(); ++i) { loud[i] = l[i] * 40.0f; tiny[i] = l[i] * 1e-30f; }
    check(play(plugin, loud, loud, true, 0) && play(plugin, tiny, tiny, true, 0), "also far over full scale and far under anything audible", 1);

    // A capture through DPF: the transport starts, runs for six seconds and stops.
    plugin.setParameterValue(kMode, 0.0f);
    std::vector<float> silence(48000, 0.0f);
    play(plugin, silence, silence, false, 0);
    play(plugin, l, r, true, 96000);
    check(plugin.getParameterValue(kMeterLufsM) > -40.0f && plugin.getParameterValue(kMeterLufsI) > -40.0f, "while it runs the host reads the meter (LUFS, momentary)", plugin.getParameterValue(kMeterLufsM));
    play(plugin, silence, silence, false, 96000 + l.size());
    std::unique_ptr<Meter> meter(new Meter());
    meter->prepare(48000.0);
    meter->process(l.data(), r.data(), static_cast<uint32_t>(l.size()));
    meter->flush();
    std::unique_ptr<Result> expect(new Result());
    meter->snapshot(*expect);
    check(std::fabs(plugin.getParameterValue(kMeterLufsI) - expect->integrated) < 1e-4 && std::fabs(plugin.getParameterValue(kMeterTruePeak) - truePeakDb(*expect)) < 1e-4 &&
              std::fabs(plugin.getParameterValue(kMeterPlr) - plr(*expect)) < 1e-4 && std::fabs(plugin.getParameterValue(kMeterLra) - expect->range) < 1e-4,
          "after the transport stops the host reads the result: LUFS, true peak, PLR, LRA", plugin.getParameterValue(kMeterLufsI));
    check(plugin.getParameterValue(kMeterLufsM) == -100.0f, "and no momentary loudness any more", plugin.getParameterValue(kMeterLufsM));
    const std::string stored(plugin.getStateValue("analysis").buffer());
    Analysis a;
    check(fromJson(stored, a) && a.r.frames == l.size() && std::fabs(a.r.integrated - expect->integrated) < 1e-12 && a.capture.status == kComplete && a.capture.startFrame == 96000 &&
              a.capture.ended == kTransportStopped,
          "the analysis is in the plugin's state: six seconds from where the transport stood, complete", a.r.seconds());

    // Another instance takes it over from the song.
    {
        PluginExporter other(nullptr, nullptr, nullptr, nullptr);
        check(std::string(other.getStateValue("analysis").buffer()).empty() && std::string(other.getStateValue("preset").buffer()) == "Streaming -14", "a new instance has no analysis, and the first preset",
              0);
        other.setState("analysis", stored.c_str());
        other.setState("preset", "My album");
        other.setState("reference", "1|Some master");
        Analysis b;
        check(fromJson(other.getStateValue("analysis").buffer(), b) && b.r.frames == a.r.frames && std::fabs(b.r.integrated - a.r.integrated) < 1e-12 && b.r.loudCount == a.r.loudCount,
              "a song gives an instance its analysis back", b.r.integrated);
        check(std::string(other.getStateValue("preset").buffer()) == "My album" && std::string(other.getStateValue("reference").buffer()) == "1|Some master",
              "and the name of its preset and its choice of reference", 1);
        other.setState("analysis", "not an analysis");
        other.setState("analysis", "");
        Analysis c;
        check(fromJson(other.getStateValue("analysis").buffer(), c) && c.r.frames == a.r.frames, "text that is no analysis changes nothing", 1);
        Engine* engine = Engine::of(other.getInstancePointer());
        check(engine != nullptr && engine != Engine::of(plugin.getInstancePointer()) && Engine::of(plugin.getInstancePointer()) != nullptr, "each instance has its own engine, which its window finds", 1);
    }

    // Samples that are not numbers: silence in their place, the rest as it was, and the meter stays sound.
    plugin.setParameterValue(kMode, 1.0f);
    std::vector<float> bad(l), badR(r), out(512), outR(512);
    bad[100] = fromBits(0x7fc00000u);
    bad[101] = fromBits(0x7f800000u);
    badR[300] = fromBits(0xff800000u);
    bool clean = true, kept = true;
    for (size_t at = 0; at + 512 <= 48000; at += 512) {
        const float* in[2] = {bad.data() + at, badR.data() + at};
        float* o[2] = {out.data(), outR.data()};
        plugin.run(in, o, 512);
        for (size_t i = 0; i < 512; ++i) {
            clean = clean && isNumber(out[i]) && isNumber(outR[i]);
            const bool wasBadL = at + i == 100 || at + i == 101, wasBadR = at + i == 300;
            kept = kept && (wasBadL ? out[i] == 0.0f : out[i] == l[at + i]) && (wasBadR ? outR[i] == 0.0f : outR[i] == r[at + i]);
        }
    }
    bool meters = true;
    for (uint32_t id = kInputParamCount; id < kParamCount; ++id) meters = meters && isNumber(plugin.getParameterValue(id));
    check(clean && kept && meters && plugin.getParameterValue(kMeterLufsI) > -60.0f, "samples that are not numbers come out as silence, the rest untouched, and the meter stays sound",
          plugin.getParameterValue(kMeterLufsI));

    // A change of sample rate in the middle of things.
    plugin.setParameterValue(kMode, 0.0f);
    play(plugin, silence, silence, false, 0);
    play(plugin, l, r, true, 0);
    plugin.deactivate();
    plugin.setSampleRate(96000.0, true);
    plugin.activate();
    check(play(plugin, l, r, true, 0) && isNumber(plugin.getParameterValue(kMeterLufsI)), "a change of sample rate during a capture: the sound still passes untouched", 1);
    plugin.deactivate();

    std::printf("%s\n", failures == 0 ? "ALL PASS" : "FAILED");
    return failures == 0 ? 0 : 1;
}
