// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// What a host may do to a plugin without being wrong, and a little beyond: unusual sample rates,
// blocks of any length, parameters jumping about, garbage MIDI and tempo. One file for all plugins:
// plugins/<name>/tests/abuse_test.cpp includes its Params.hpp and then this.
//
// Each check has a name starting with HA (host abuse) and a number.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "src/DistrhoPluginInternal.hpp"

#include "../dsp/Safety.hpp"

USE_NAMESPACE_DISTRHO

namespace abuse {

static int failures = 0;
inline void check(bool ok, const char* what, double a = 0, double b = 0) {
    std::printf("%s  %-58s %.6g  %.6g\n", ok ? "PASS" : "FAIL", what, a, b);
    if (!ok) ++failures;
}

inline bool isNumber(float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    return (bits & 0x7f800000u) != 0x7f800000u;
}

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float unit() { return static_cast<float>(next() >> 8) * (1.0f / 16777216.0f); } // 0..1
    float bipolar() { return 2.0f * unit() - 1.0f; }
    int below(int n) { return static_cast<int>(next() % static_cast<uint32_t>(n)); }
};

constexpr int kIns = DISTRHO_PLUGIN_NUM_INPUTS;
constexpr int kOuts = DISTRHO_PLUGIN_NUM_OUTPUTS; // the checks look at the first two; a plugin with more gets room for all
#if DISTRHO_PLUGIN_WANT_MIDI_INPUT
constexpr bool kSynth = true;
#else
constexpr bool kSynth = false;
#endif

/** Runs one block of any length; inputs are copied so the caller's signal is kept. */
struct Host {
    PluginExporter plugin;
    std::vector<float> buf[4], out[kOuts];
    Host() : plugin(nullptr, nullptr, nullptr, nullptr) {
#ifdef HNL_ABUSE_SETUP
        HNL_ABUSE_SETUP(plugin); // a plugin that is silent as it starts (a sampler with nothing loaded) is given something to play
#endif
    }

    void run(const float* const* in, uint32_t frames, const MidiEvent* events = nullptr, uint32_t eventCount = 0) {
        for (int c = 0; c < 4; ++c) {
            buf[c].assign(frames + 1, 0.0f);
            if (c < kIns && in != nullptr) std::memcpy(buf[c].data(), in[c], sizeof(float) * frames);
        }
        for (auto& o : out) o.assign(frames + 1, 0.0f);
        const float* ins[4] = {buf[0].data(), buf[1].data(), buf[2].data(), buf[3].data()};
        float* outs[kOuts];
        for (int c = 0; c < kOuts; ++c) outs[c] = out[c].data();
#if DISTRHO_PLUGIN_WANT_MIDI_INPUT
        plugin.run(kIns > 0 ? ins : nullptr, outs, frames, events, eventCount);
#else
        (void)events; (void)eventCount;
        plugin.run(ins, outs, frames);
#endif
    }
};

inline MidiEvent midi(uint32_t frame, uint8_t status, uint8_t a, uint8_t b, uint32_t size = 3) {
    MidiEvent ev;
    ev.frame = frame;
    ev.size = size;
    ev.data[0] = status; ev.data[1] = a; ev.data[2] = b; ev.data[3] = 0;
    ev.dataExt = nullptr;
    return ev;
}

/** A second of test signal at any rate: two tones and noise, the same on every call. */
inline void signal(std::vector<float>& l, std::vector<float>& r, std::vector<float>& side, double fs, size_t n, float level) {
    Rng rng(0x1234567u);
    l.resize(n); r.resize(n); side.resize(n);
    for (size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / fs;
        const double burst = std::fmod(t, 0.25) < 0.1 ? 1.0 : 0.15; // hits, for the dynamics
        l[i] = static_cast<float>(level * burst * (0.5 * std::sin(6.283185307 * 110.0 * t) + 0.3 * std::sin(6.283185307 * 1870.0 * t) + 0.2 * rng.bipolar()));
        r[i] = static_cast<float>(level * burst * (0.5 * std::sin(6.283185307 * 110.0 * t + 0.4) + 0.3 * std::sin(6.283185307 * 2210.0 * t) + 0.2 * rng.bipolar()));
        side[i] = static_cast<float>(level * 0.5 * std::sin(6.283185307 * 440.0 * t));
    }
}

/** Plays the signal (a synth: a chord on the way) in blocks of `block` and collects the output. */
inline void play(Host& h, const std::vector<float>& l, const std::vector<float>& r, const std::vector<float>& side, uint32_t block,
                 std::vector<float>& outL, std::vector<float>& outR) {
    outL.clear(); outR.clear();
    const size_t n = l.size();
    // The chord's notes at fixed places in time, whatever the block length.
    const size_t noteAt[4] = {0, 1000, 2500, n / 2};
    const uint8_t notes[4] = {48, 60, 67, 72};
    for (size_t done = 0; done < n; done += block) {
        const uint32_t frames = static_cast<uint32_t>(std::min<size_t>(block, n - done));
        const float* in[4] = {l.data() + done, r.data() + done, side.data() + done, side.data() + done};
        MidiEvent events[8];
        uint32_t count = 0;
        for (int k = 0; k < 4; ++k)
            if (noteAt[k] >= done && noteAt[k] < done + frames) events[count++] = midi(static_cast<uint32_t>(noteAt[k] - done), 0x90, notes[k], 100);
        h.run(in, frames, events, count);
        outL.insert(outL.end(), h.out[0].begin(), h.out[0].begin() + frames);
        outR.insert(outR.end(), h.out[1].begin(), h.out[1].begin() + frames);
    }
}

inline double peakOf(const std::vector<float>& x, bool* numbers) {
    double peak = 0;
    for (float v : x) {
        if (!isNumber(v)) { *numbers = false; continue; }
        peak = std::max(peak, static_cast<double>(std::fabs(v)));
    }
    return peak;
}

// ------------------------------------------------------------------ checks

/** HA-1: every sample rate a host may choose, at default, lowest and highest settings. */
inline void sampleRates() {
    for (double fs : {8000.0, 11025.0, 22050.0, 32000.0, 88200.0, 192000.0, 384000.0}) {
        d_nextSampleRate = fs;
        d_nextBufferSize = 512;
        bool numbers = true;
        double loudest = 0, quietestDefault = 0;
        for (int setting = 0; setting < 3; ++setting) { // defaults, all lowest, all highest
            Host h;
            if (setting > 0)
                for (uint32_t id = 0; id < kInputParamCount; ++id) {
                    const ParameterRanges& range = h.plugin.getParameterRanges(id);
                    h.plugin.setParameterValue(id, setting == 1 ? range.min : range.max);
                }
            h.plugin.activate();
            std::vector<float> l, r, side, ol, or_;
            signal(l, r, side, fs, static_cast<size_t>(0.5 * fs), 0.5f);
            play(h, l, r, side, 512, ol, or_);
            const double peak = std::max(peakOf(ol, &numbers), peakOf(or_, &numbers));
            loudest = std::max(loudest, peak);
            if (setting == 0) quietestDefault = peak;
            h.plugin.deactivate();
        }
        char name[80];
        std::snprintf(name, sizeof(name), "HA-1 %.0f Hz: numbers, bounded, sounds (peak, default peak)", fs);
        check(numbers && loudest < 64.0 && quietestDefault > 1e-4, name, loudest, quietestDefault);
    }
    check(hnl::guardTrips().load() == 0u, "HA-1 no engine had to be restarted (times)", hnl::guardTrips().load());
    d_nextSampleRate = 48000.0;
}

/** HA-2: the same signal in blocks of any length gives the same sound. */
inline void blockSizes() {
    d_nextSampleRate = 48000.0;
    d_nextBufferSize = 4096;
    std::vector<float> l, r, side, refL, refR;
    signal(l, r, side, 48000.0, 48000, 0.5f);
    {
        Host h;
        h.plugin.activate();
        play(h, l, r, side, 512, refL, refR);
    }
    for (uint32_t block : {1u, 37u, 64u, 2048u, 4096u}) {
        Host h;
        h.plugin.activate();
        std::vector<float> ol, or_;
        play(h, l, r, side, block, ol, or_);
        double worst = 0;
        bool numbers = true;
        peakOf(ol, &numbers);
        for (size_t i = 0; i < refL.size(); ++i) worst = std::max(worst, static_cast<double>(std::max(std::fabs(ol[i] - refL[i]), std::fabs(or_[i] - refR[i]))));
        char name[80];
        std::snprintf(name, sizeof(name), "HA-2 blocks of %u: same output as blocks of 512 (largest difference)", block);
        check(numbers && worst < 1e-4, name, worst);
    }
    // And a block of nothing at all.
    Host h;
    h.plugin.activate();
    h.run(nullptr, 0);
    std::vector<float> ol, or_;
    play(h, l, r, side, 512, ol, or_);
    double worst = 0;
    for (size_t i = 0; i < refL.size(); ++i) worst = std::max(worst, static_cast<double>(std::fabs(ol[i] - refL[i])));
    check(worst < 1e-4, "HA-2 an empty block changes nothing (largest difference)", worst);
    d_nextBufferSize = 512;
}

/** HA-3: parameters jumping to random values in their range while loud signal runs, tempo and MIDI garbage included. */
inline void fuzz(uint32_t seed, int blocks) {
    d_nextSampleRate = 48000.0;
    d_nextBufferSize = 512;
    Host h;
    h.plugin.activate();
    Rng rng(seed);
    bool numbers = true;
    double loudest = 0;
    std::vector<float> l(512), r(512), side(512);
    long n = 0;
    for (int block = 0; block < blocks; ++block) {
        const int changes = rng.below(10) < 3 ? 1 + rng.below(3) : 0;
        for (int c = 0; c < changes; ++c) {
            const uint32_t id = static_cast<uint32_t>(rng.below(static_cast<int>(kInputParamCount)));
            const ParameterRanges& range = h.plugin.getParameterRanges(id);
            const int kind = rng.below(4);
            float value = kind == 0 ? range.min : kind == 1 ? range.max : range.min + rng.unit() * (range.max - range.min);
            if (h.plugin.getParameterHints(id) & kParameterIsInteger) value = std::round(value);
            h.plugin.setParameterValue(id, value);
        }
        if (rng.below(40) == 0) { // the host stops and starts
            h.plugin.deactivate();
            h.plugin.activate();
        }
#if DISTRHO_PLUGIN_WANT_TIMEPOS
        TimePosition time;
        time.playing = rng.below(2) == 0;
        time.bbt.valid = rng.below(4) != 0;
        static const double kTempos[8] = {120.0, 0.0, -30.0, 20.0001, 999.0, 1e12, 61.5, 174.0};
        time.bbt.beatsPerMinute = kTempos[rng.below(8)];
        h.plugin.setTimePosition(time);
#endif

        const float level = rng.below(6) == 0 ? 4.0f : rng.below(3) == 0 ? 0.0f : 0.7f; // up to 12 dB over full scale, and silence
        for (int i = 0; i < 512; ++i, ++n) {
            l[i] = level * (0.6f * static_cast<float>(std::sin(6.283185307 * 220.0 * n / 48000.0)) + 0.4f * rng.bipolar());
            r[i] = level * (0.6f * static_cast<float>(std::sin(6.283185307 * 331.0 * n / 48000.0)) + 0.4f * rng.bipolar());
            side[i] = level * rng.bipolar();
        }
        MidiEvent events[6];
        uint32_t count = 0;
        if (kSynth) {
            count = static_cast<uint32_t>(rng.below(7));
            uint32_t frame = 0;
            for (uint32_t e = 0; e < count; ++e) {
                frame += static_cast<uint32_t>(rng.below(300)); // may run past the block
                static const uint8_t kStatus[8] = {0x90, 0x90, 0x90, 0x80, 0xb0, 0xe0, 0xc0, 0xf8};
                events[e] = midi(frame, static_cast<uint8_t>(kStatus[rng.below(8)] | rng.below(16)), static_cast<uint8_t>(rng.below(256)), static_cast<uint8_t>(rng.below(256)),
                                 static_cast<uint32_t>(1 + rng.below(3)));
            }
        }
        const uint32_t frames = static_cast<uint32_t>(1 + rng.below(512));
        const float* in[4] = {l.data(), r.data(), side.data(), side.data()};
        h.run(in, frames, events, count);
        for (uint32_t i = 0; i < frames; ++i) {
            numbers = numbers && isNumber(h.out[0][i]) && isNumber(h.out[1][i]);
            loudest = std::max(loudest, static_cast<double>(std::max(std::fabs(h.out[0][i]), std::fabs(h.out[1][i]))));
        }
        // The sentinel after the block: a plugin that writes past the end of what it was given.
        numbers = numbers && h.out[0][frames] == 0.0f && h.out[1][frames] == 0.0f;
        for (uint32_t id = kInputParamCount; id < kParamCount; ++id) numbers = numbers && isNumber(h.plugin.getParameterValue(id));
    }
    char name[80];
    std::snprintf(name, sizeof(name), "HA-3 fuzz %08x: numbers and bounded (peak)", seed);
    check(numbers && loudest < 8192.0, name, loudest); // HNL Master alone can add 60 dB with its limiter off
    std::snprintf(name, sizeof(name), "HA-3 fuzz %08x: no engine restart (times)", seed);
    check(hnl::guardTrips().load() == 0u, name, hnl::guardTrips().load());

    // Afterwards, back at the default settings and with silence in, the plugin comes to rest.
    for (uint32_t id = 0; id < kInputParamCount; ++id) h.plugin.setParameterValue(id, h.plugin.getParameterRanges(id).def);
    std::fill(l.begin(), l.end(), 0.0f);
    std::fill(r.begin(), r.end(), 0.0f);
    std::fill(side.begin(), side.end(), 0.0f);
    MidiEvent quiet = midi(0, 0xb0, 120, 0);
    double tail = 0;
    for (int block = 0; block < 1900; ++block) { // 20 seconds
        const float* in[4] = {l.data(), r.data(), side.data(), side.data()};
        h.run(in, 512, &quiet, block == 0 ? 1 : 0);
        if (block >= 1800) for (int i = 0; i < 512; ++i) tail = std::max(tail, static_cast<double>(std::max(std::fabs(h.out[0][i]), std::fabs(h.out[1][i]))));
    }
    std::snprintf(name, sizeof(name), "HA-3 fuzz %08x: at rest 20 s later (peak)", seed);
    check(tail < 1e-3, name, tail);
}

inline int runAll() {
    sampleRates();
    blockSizes();
    for (uint32_t seed : {0x9e3779b9u, 0x7f4a7c15u, 0x2545f491u, 0xdeadbeefu}) fuzz(seed, 1500);
    std::printf("%s\n", failures ? "FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}

} // namespace abuse
