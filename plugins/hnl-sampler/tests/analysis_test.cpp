// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// Offline measurements of the sound analysis (dsp/Analysis.hpp) on sounds made here: notes of several kinds,
// drum loops at known tempi with hits at known places, held tones to loop, noise, and input no sound should hold.
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include "../dsp/Analysis.hpp"

using namespace hnl;
using namespace hnl::sampler;

static int failures = 0;

static void check(bool ok, const std::string& name, const char* fmt, double a = 0, double b = 0, double c = 0, double d = 0) {
    std::printf("%s  %-52s ", ok ? "PASS" : "FAIL", name.c_str());
    std::printf(fmt, a, b, c, d);
    std::printf("\n");
    if (!ok) ++failures;
}

typedef std::vector<float> Buf;

static uint32_t seed = 12345u;
/** White noise in -1..1, the same sequence on every run. */
static double noise() {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return static_cast<double>(seed) / 2147483648.0 - 1.0;
}

static double noteHz(int key) { return 440.0 * std::pow(2.0, (key - 69) / 12.0); }
static double cents(double hz, double ref) { return 1200.0 * std::log2(hz / ref); }
static size_t at(double seconds, double fs) { return static_cast<size_t>(seconds * fs + 0.5); }

// ------------------------------------------------------------------ notes

/**
 * A sum of partials: partial k at f * k * sqrt(1 + B k^2) with amplitude amp[k - 1], each decaying at
 * decay * k per second, with a 3 ms rise. Partials above 0.45 fs are left out.
 */
static Buf partials(double fs, double seconds, double f, const std::vector<double>& amp, double B, double decay) {
    const size_t n = at(seconds, fs);
    Buf out(n, 0.0f);
    std::vector<double> acc(n, 0.0);
    for (size_t k = 1; k <= amp.size(); ++k) {
        const double fk = f * k * std::sqrt(1.0 + B * k * k);
        if (fk >= 0.45 * fs || amp[k - 1] == 0.0) continue;
        const double w = 2.0 * kPi * fk / fs, c = std::cos(w), s = std::sin(w), fall = std::exp(-decay * k / fs);
        double re = 0.0, im = amp[k - 1]; // starts at phase 0 of a sine
        for (size_t i = 0; i < n; ++i) {
            acc[i] += re;
            const double r = re * c + im * s, m = im * c - re * s;
            re = r * fall;
            im = m * fall;
        }
    }
    const size_t rise = at(0.003, fs);
    for (size_t i = 0; i < n; ++i) out[i] = static_cast<float>(acc[i] * (i < rise ? static_cast<double>(i) / rise : 1.0));
    return out;
}

static void scale(Buf& x, double peak) {
    double top = 0;
    for (float v : x) top = std::max(top, static_cast<double>(std::fabs(v)));
    if (top > 0)
        for (float& v : x) v = static_cast<float>(v * peak / top);
}

enum Kind { kSine, kSaw, kPiano, kOctaveTrap, kVibrato, kNoisyAttack, kKinds };
static const char* kindName[kKinds] = {"sine", "sawtooth", "piano-like (weak fundamental, stretched)", "strong 2nd harmonic", "vibrato +-20 cents",
                                       "noisy attack"};

/** A test note of the given kind and the frequency a tuner should read for it. */
static Buf note(Kind kind, double fs, int key, double& reference) {
    const double f = noteHz(key);
    reference = f;
    Buf x;
    switch (kind) {
    case kSine: x = partials(fs, 1.0, f, {1.0}, 0.0, 0.0); break;
    case kSaw: {
        std::vector<double> a(60);
        for (size_t k = 0; k < a.size(); ++k) a[k] = 1.0 / (k + 1);
        x = partials(fs, 1.0, f, a, 0.0, 0.0);
        break;
    }
    case kPiano: {
        // A weak fundamental, partials falling slowly, more stretch higher up as on a real piano. The pitch a
        // tuner reads is that of the first partial.
        const double B = 0.00012 * std::pow(2.0, 0.05 * (key - 21));
        std::vector<double> a(30);
        a[0] = 0.15;
        for (size_t k = 1; k < a.size(); ++k) a[k] = 1.0 / std::pow(k + 1.0, 0.8);
        x = partials(fs, 1.2, f, a, B, 0.6);
        reference = f * std::sqrt(1.0 + B);
        break;
    }
    case kOctaveTrap: x = partials(fs, 1.0, f, {0.15, 1.0, 0.05, 0.3}, 0.0, 0.0); break;
    case kVibrato: {
        const size_t n = at(2.0, fs);
        x.assign(n, 0.0f);
        double phase = 0.0;
        for (size_t i = 0; i < n; ++i) {
            const double t = static_cast<double>(i) / fs, fi = f * std::pow(2.0, 20.0 / 1200.0 * std::sin(2.0 * kPi * 5.5 * t));
            phase += 2.0 * kPi * fi / fs;
            double v = 0.0;
            const double amps[3] = {1.0, 0.5, 0.3};
            for (int k = 1; k <= 3; ++k)
                if (k * fi < 0.45 * fs) v += amps[k - 1] * std::sin(k * phase);
            x[i] = static_cast<float>(v * std::min(1.0, t / 0.01));
        }
        break;
    }
    case kNoisyAttack: {
        x = partials(fs, 1.0, f, {1.0, 0.4, 0.2}, 0.0, 0.3);
        scale(x, 0.5);
        const size_t burst = at(0.06, fs);
        for (size_t i = 0; i < burst; ++i) x[i] += static_cast<float>(1.2 * noise() * std::exp(-3.0 * static_cast<double>(i) / burst));
        break;
    }
    default: break;
    }
    if (kind != kNoisyAttack) scale(x, 0.8);
    return x;
}

static void testPitch() {
    const int keys[] = {21, 28, 33, 40, 45, 52, 57, 60, 64, 69, 76, 81, 88, 93, 96};
    for (int kind = 0; kind < kKinds; ++kind)
        for (double fs : {44100.0, 48000.0, 96000.0}) {
            double worst = 0, worstKey = 0, lowestConf = 1;
            int wrongKeys = 0;
            for (int key : keys) {
                double ref;
                const Buf x = note(static_cast<Kind>(kind), fs, key, ref);
                const PitchResult p = detectPitch(x.data(), x.size(), fs);
                const double err = p.hz > 0 ? cents(p.hz, ref) : 1200.0;
                if (std::fabs(err) > std::fabs(worst)) {
                    worst = err;
                    worstKey = key;
                }
                if (p.key != key) ++wrongKeys;
                lowestConf = std::min(lowestConf, p.confidence);
            }
            char name[160];
            std::snprintf(name, sizeof(name), "pitch %s, %.1f kHz", kindName[kind], fs / 1000.0);
            check(std::fabs(worst) <= 3.0 && wrongKeys == 0 && lowestConf > 0.5, name, "worst %+.2f cents (key %.0f), %.0f wrong keys, conf >= %.2f",
                  worst, worstKey, wrongKeys, lowestConf);
        }
    // The detailed result of one: tuned 30 cents sharp.
    double ref;
    Buf x = partials(48000, 1.0, noteHz(57) * std::pow(2.0, 30.0 / 1200.0), {1.0, 0.5}, 0.0, 0.0);
    PitchResult p = detectPitch(x.data(), x.size(), 48000);
    check(p.key == 57 && std::fabs(p.cents - 30.0) < 0.5, "pitch key and cents of A3 +30 cents", "key %.0f, %+.2f cents", p.key, p.cents);
    x = partials(48000, 1.0, noteHz(57) * std::pow(2.0, 70.0 / 1200.0), {1.0, 0.5}, 0.0, 0.0);
    p = detectPitch(x.data(), x.size(), 48000);
    check(p.key == 58 && std::fabs(p.cents + 30.0) < 0.5, "pitch key and cents of A3 +70 cents (= A#3 -30)", "key %.0f, %+.2f cents", p.key, p.cents);
    (void)ref;

    Buf n(48000);
    for (float& v : n) v = static_cast<float>(0.5 * noise());
    p = detectPitch(n.data(), n.size(), 48000);
    check(p.confidence < 0.3, "pitch of white noise: none, or low confidence", "%.1f Hz, conf %.2f", p.hz, p.confidence);
    Buf quiet(48000, 0.0f);
    p = detectPitch(quiet.data(), quiet.size(), 48000);
    check(p.hz == 0 && p.confidence == 0 && p.key == -1, "pitch of silence: none", "%.1f Hz, conf %.2f, key %.0f", p.hz, p.confidence, p.key);
    // A short note: 60 ms of A4.
    x = partials(48000, 0.06, 440.0, {1.0, 0.3}, 0.0, 0.0);
    p = detectPitch(x.data(), x.size(), 48000);
    check(p.key == 69 && std::fabs(p.cents) < 3.0, "pitch of a 60 ms note", "key %.0f, %+.2f cents", p.key, p.cents);
    // A note with dropouts where samples were not numbers.
    x = partials(48000, 1.0, 220.0, {1.0, 0.5}, 0.0, 0.0);
    for (size_t i = 1000; i < x.size(); i += 997) x[i] = std::numeric_limits<float>::quiet_NaN();
    p = detectPitch(x.data(), x.size(), 48000);
    check(p.key == 57 && std::fabs(p.cents) < 3.0, "pitch of a note with NaN samples in it", "key %.0f, %+.2f cents", p.key, p.cents);
}

// ------------------------------------------------------------- drum loops

struct Drum {
    Buf x;
    std::vector<size_t> hits; // where each hit starts, ascending, unique
};

enum Voice { kKick = 1, kSnare = 2, kHat = 4, kOpenHat = 8 };

static void addKick(Buf& x, size_t pos, double fs, double level) {
    double phase = 0;
    for (size_t i = 0; pos + i < x.size() && i < at(0.6, fs); ++i) {
        const double t = i / fs, f = 50.0 + 100.0 * std::exp(-t / 0.03);
        x[pos + i] += static_cast<float>(level * std::sin(phase) * std::exp(-t / 0.15));
        phase += 2.0 * kPi * f / fs;
    }
}

static void addSnare(Buf& x, size_t pos, double fs, double level) {
    double lp = 0, phase = 0;
    for (size_t i = 0; pos + i < x.size() && i < at(0.3, fs); ++i) {
        const double t = i / fs, n = noise();
        lp += 0.3 * (n - lp);
        x[pos + i] += static_cast<float>(level * ((n - lp) * std::exp(-t / 0.06) + 0.5 * std::sin(phase) * std::exp(-t / 0.04)));
        phase += 2.0 * kPi * 190.0 / fs;
    }
}

static void addHat(Buf& x, size_t pos, double fs, double level, double tau) {
    double prev = 0;
    for (size_t i = 0; pos + i < x.size() && i < at(8 * tau, fs); ++i) {
        const double t = i / fs, n = noise();
        x[pos + i] += static_cast<float>(level * 0.5 * (n - prev) * std::exp(-t / tau));
        prev = n;
    }
}

/**
 * A loop of `bars` bars at `bpm`, cut exactly to its length, from a pattern of 16 steps per bar
 * (a bitmask of Voice per step). swing delays the odd eighths by that fraction of an eighth.
 */
static Drum drumLoop(double fs, double bpm, int bars, const std::vector<int>& pattern, double swing = 0.0, int steps = 16) {
    Drum d;
    const double beat = 60.0 / bpm * fs, stepLen = 4.0 * beat / steps;
    const size_t n = static_cast<size_t>(std::floor(bars * 4.0 * beat + 0.5));
    d.x.assign(n, 0.0f);
    for (int bar = 0; bar < bars; ++bar)
        for (int s = 0; s < steps; ++s) {
            const int v = pattern[static_cast<size_t>(s) % pattern.size()];
            if (!v) continue;
            double pos = (bar * steps + s) * stepLen;
            if (steps == 16 && s % 4 == 2) pos += swing * 2.0 * stepLen; // the "and" of each beat
            const size_t p = static_cast<size_t>(std::floor(pos + 0.5));
            if (v & kKick) addKick(d.x, p, fs, 0.8);
            if (v & kSnare) addSnare(d.x, p, fs, 0.5);
            if (v & kHat) addHat(d.x, p, fs, 0.25, 0.012);
            if (v & kOpenHat) addHat(d.x, p, fs, 0.3, 0.06);
            d.hits.push_back(p);
        }
    return d;
}

// Patterns: one bar of sixteenths.
static const std::vector<int> kRock = {kKick | kHat, 0, kHat, 0, kSnare | kHat, 0, kHat, 0, kKick | kHat, 0, kKick | kHat, 0, kSnare | kHat, 0, kHat, 0};
static const std::vector<int> kHouse = {kKick, 0, kOpenHat, 0, kKick | kSnare, 0, kOpenHat, 0, kKick, 0, kOpenHat, 0, kKick | kSnare, 0, kOpenHat, 0};
static const std::vector<int> kSixteenths = {kKick | kHat, kHat, kHat, kHat, kSnare | kHat, kHat, kHat, kKick | kHat,
                                             kHat,         kHat, kKick | kHat, kHat, kSnare | kHat, kHat, kHat, kHat};
static const std::vector<int> kBreak = {kKick | kHat, 0, kHat, 0, kSnare | kHat, 0, kHat, 0, kHat, 0, kKick | kHat, 0, kSnare | kHat, 0, kHat, 0};

static void testTransients() {
    struct Case {
        const char* name;
        double fs, bpm;
        int bars;
        const std::vector<int>* pattern;
    } cases[] = {{"rock 120 bpm, 44.1 kHz", 44100, 120, 2, &kRock},
                 {"house 128 bpm, 48 kHz", 48000, 128, 2, &kHouse},
                 {"sixteenths 100 bpm, 48 kHz", 48000, 100, 2, &kSixteenths},
                 {"break 174 bpm, 96 kHz", 96000, 174, 2, &kBreak}};
    for (const Case& c : cases) {
        const Drum d = drumLoop(c.fs, c.bpm, c.bars, *c.pattern);
        const std::vector<size_t> hits = detectTransients(d.x.data(), d.x.size(), c.fs, 0.5);
        double early = 0, late = 0;
        int missed = 0;
        for (size_t h : d.hits) {
            double bestErr = 1e9;
            for (size_t f : hits) {
                const double e = (static_cast<double>(f) - static_cast<double>(h)) / c.fs * 1000.0;
                if (std::fabs(e) < std::fabs(bestErr)) bestErr = e;
            }
            if (bestErr < -2.0 || bestErr > 5.0) ++missed;
            else {
                early = std::min(early, bestErr);
                late = std::max(late, bestErr);
            }
        }
        const double extra = static_cast<double>(hits.size()) - static_cast<double>(d.hits.size() - missed);
        check(missed == 0 && extra == 0, std::string("transients ") + c.name, "%.0f of %.0f hits within -2..+5 ms (%+.2f..%+.2f ms), %.0f extra",
              static_cast<double>(d.hits.size() - missed), static_cast<double>(d.hits.size()), early, late);
        check(extra == 0 && missed == 0, std::string("  ... count ") + c.name, "%.0f found, %.0f made", static_cast<double>(hits.size()),
              static_cast<double>(d.hits.size()));
    }

    // A held tone: only its start.
    Buf tone = partials(48000, 3.0, 196.0, {1.0, 0.6, 0.4, 0.2, 0.1}, 0.0, 0.2);
    scale(tone, 0.7);
    std::vector<size_t> hits = detectTransients(tone.data(), tone.size(), 48000, 0.5);
    check(hits.size() == 1 && hits[0] < at(0.002, 48000), "transients of a held tone: only its start", "%.0f found, first at %.0f",
          static_cast<double>(hits.size()), hits.empty() ? -1.0 : static_cast<double>(hits[0]));
    hits = detectTransients(tone.data(), tone.size(), 48000, 1.0);
    check(hits.size() == 1, "transients of a held tone at sensitivity 1", "%.0f found", static_cast<double>(hits.size()));

    // The first sound after silence is a hit even when quiet.
    Buf late(48000, 0.0f);
    addSnare(late, 12000, 48000, 0.05);
    hits = detectTransients(late.data(), late.size(), 48000, 0.0);
    check(!hits.empty() && hits[0] >= 12000 - 48 && hits[0] <= 12000 + 240, "transients: first sound after silence is a hit",
          "first at %.0f (made at 12000)", hits.empty() ? -1.0 : static_cast<double>(hits[0]));

    // Sensitivity: a phrase of hits at many levels over a noise floor.
    Buf phrase(at(6.0, 48000), 0.0f);
    for (float& v : phrase) v = static_cast<float>(0.003 * noise());
    for (int i = 0; i < 40; ++i) {
        const double level = 0.05 + 0.9 * std::fabs(noise());
        const size_t pos = at(0.05 + i * 0.147, 48000);
        if (i % 3 == 0) addKick(phrase, pos, 48000, level);
        else if (i % 3 == 1) addSnare(phrase, pos, 48000, level * 0.6);
        else addHat(phrase, pos, 48000, level * 0.4, 0.02);
    }
    size_t prevCount = 0;
    bool monotone = true, ordered = true;
    size_t lowCount = 0, highCount = 0;
    for (int s = 0; s <= 20; ++s) {
        hits = detectTransients(phrase.data(), phrase.size(), 48000, s / 20.0);
        if (s == 0) lowCount = hits.size();
        if (s == 20) highCount = hits.size();
        if (hits.size() < prevCount) monotone = false;
        prevCount = hits.size();
        for (size_t i = 1; i < hits.size(); ++i)
            if (hits[i] <= hits[i - 1]) ordered = false;
        for (size_t h : hits)
            if (h >= phrase.size()) ordered = false;
    }
    check(monotone && highCount > lowCount, "transients: higher sensitivity never finds fewer", "%.0f at 0, %.0f at 1", static_cast<double>(lowCount),
          static_cast<double>(highCount));
    check(ordered, "transients: ascending, unique, inside the sound", "");
    const Drum fast = drumLoop(48000, 100, 2, kSixteenths);
    hits = detectTransients(fast.x.data(), fast.x.size(), 48000, 0.5, 0.2);
    double smallest = 1e9;
    for (size_t i = 1; i < hits.size(); ++i) smallest = std::min(smallest, static_cast<double>(hits[i] - hits[i - 1]) / 48000.0);
    check(smallest >= 0.2 && hits.size() >= 10, "transients: a minimum gap of 0.2 s is kept", "smallest gap %.3f s, %.0f hits", smallest,
          static_cast<double>(hits.size()));
}

// ------------------------------------------------------------------ tempo

static void testTempo() {
    struct Style {
        double bpm;
        const std::vector<int>* pattern;
        double high;
    } styles[] = {{90, &kRock, 180}, {120, &kRock, 180}, {128, &kHouse, 180}, {140, &kSixteenths, 180}, {174, &kBreak, 200}};
    for (const Style& s : styles)
        for (int bars : {1, 2, 4}) {
            const double fs = bars == 2 ? 44100.0 : 48000.0;
            const Drum d = drumLoop(fs, s.bpm, bars, *s.pattern);
            const TempoResult t = detectTempo(d.x.data(), d.x.size(), fs, 70.0, s.high);
            char name[120];
            std::snprintf(name, sizeof(name), "tempo %.0f bpm, %d bar%s cut as a loop (%.0f..%.0f)", s.bpm, bars, bars > 1 ? "s" : "", 70.0, s.high);
            check(std::fabs(t.bpm - s.bpm) <= 0.5 && std::fabs(t.beats - 4.0 * bars) < 0.01, name, "%.3f bpm, %.3f beats, conf %.2f", t.bpm, t.beats,
                  t.confidence);
        }
    {
        const Drum d = drumLoop(48000, 96, 2, kRock, 1.0 / 3.0);
        const TempoResult t = detectTempo(d.x.data(), d.x.size(), 48000);
        check(std::fabs(t.bpm - 96) <= 0.5, "tempo 96 bpm with swung eighths, 2 bars", "%.3f bpm, %.3f beats, conf %.2f", t.bpm, t.beats, t.confidence);
    }
    {
        // Not cut as a loop: four bars and a bit, so only the beats themselves tell.
        Drum d = drumLoop(48000, 123, 4, kRock);
        d.x.resize(d.x.size() + at(0.37, 48000), 0.0f);
        const TempoResult t = detectTempo(d.x.data(), d.x.size(), 48000);
        check(std::fabs(t.bpm - 123) <= 0.5, "tempo 123 bpm, 4 bars and 0.37 s (not a loop)", "%.3f bpm, conf %.2f", t.bpm, t.confidence);
    }
    {
        // Sixteen bars of house, uncut: the grid has many beats to agree on.
        Drum d = drumLoop(44100, 126.3, 16, kHouse);
        d.x.resize(d.x.size() - at(0.2, 44100));
        const TempoResult t = detectTempo(d.x.data(), d.x.size(), 44100);
        check(std::fabs(t.bpm - 126.3) <= 0.5, "tempo 126.3 bpm, 16 bars less 0.2 s (not a loop)", "%.3f bpm, conf %.2f", t.bpm, t.confidence);
    }
    Buf n(at(8.0, 48000));
    for (float& v : n) v = static_cast<float>(0.5 * noise());
    TempoResult t = detectTempo(n.data(), n.size(), 48000);
    check(t.confidence < 0.3, "tempo of white noise: low confidence", "%.2f bpm, conf %.2f", t.bpm, t.confidence);
    Buf tone = partials(48000, 6.0, 220.0, {1.0, 0.5, 0.2}, 0.0, 0.1);
    t = detectTempo(tone.data(), tone.size(), 48000);
    check(t.confidence < 0.3, "tempo of a held tone: low confidence", "%.2f bpm, conf %.2f", t.bpm, t.confidence);
    Buf quiet(48000, 0.0f);
    t = detectTempo(quiet.data(), quiet.size(), 48000);
    check(t.bpm == 0 && t.confidence == 0, "tempo of silence: none", "%.2f bpm, conf %.2f", t.bpm, t.confidence);
}

static void testBeatSlices() {
    std::vector<size_t> s = beatSlices(96000, 4, 1);
    check(s.size() == 4 && s[0] == 0 && s[1] == 24000 && s[2] == 48000 && s[3] == 72000, "beatSlices 4 beats by beats", "%.0f slices, second at %.0f",
          static_cast<double>(s.size()), s.size() > 1 ? static_cast<double>(s[1]) : -1.0);
    s = beatSlices(96000, 4, 0.25);
    check(s.size() == 16 && s[5] == 30000 && s.back() == 90000, "beatSlices 4 beats by sixteenths", "%.0f slices, last at %.0f",
          static_cast<double>(s.size()), static_cast<double>(s.back()));
    s = beatSlices(100001, 8, 0.5);
    bool near = s.size() == 16;
    for (size_t k = 0; near && k < s.size(); ++k) near = std::fabs(static_cast<double>(s[k]) - k * 100001.0 / 16.0) <= 0.5;
    check(near, "beatSlices rounded to the nearest frame", "%.0f slices", static_cast<double>(s.size()));
    s = beatSlices(70000, 3.5, 1);
    check(s.size() == 4 && s[3] == 60000, "beatSlices 3.5 beats: the half beat at the end is a slice", "%.0f slices", static_cast<double>(s.size()));
    s = beatSlices(48000, 4, 0.3);
    check(s.size() == 14 && s.back() == 46800, "beatSlices 4 beats by 0.3 beat", "%.0f slices, last at %.0f", static_cast<double>(s.size()),
          static_cast<double>(s.back()));
    s = beatSlices(10, 4, 0.0001);
    bool ok = s.size() == 10;
    for (size_t k = 1; k < s.size(); ++k) ok = ok && s[k] > s[k - 1] && s[k] < 10;
    check(ok, "beatSlices finer than a frame: each frame once", "%.0f slices", static_cast<double>(s.size()));
}

// ------------------------------------------------------------------- loops

/** The step in value and in slope across a loop's joint, each against the mean step of the sound near it. */
static void jointJumps(const Buf& x, size_t start, size_t end, double& value, double& slope) {
    double mean = 0;
    int n = 0;
    for (size_t i = start; i + 1 < x.size() && i < start + 400; ++i, ++n) mean += std::fabs(x[i + 1] - x[i]);
    mean = std::max(mean / std::max(1, n), 1e-12);
    // After end - 1 comes start: compare with what end - 1 was followed by in the sound.
    value = std::fabs(static_cast<double>(x[start]) - x[end]) / mean;
    slope = std::fabs((static_cast<double>(x[start]) - x[end - 1]) - (static_cast<double>(x[end]) - x[end - 1])) / mean;
    // and the step after the joint, start -> start + 1, against end -> end + 1
    slope = std::max(slope, std::fabs((static_cast<double>(x[start + 1]) - x[start]) - (static_cast<double>(x[end + 1]) - x[end])) / mean);
}

static Buf drifting(double fs, double seconds, double f, double phase, const std::vector<double>& amp, double dbPerSecond) {
    const size_t n = at(seconds, fs);
    Buf x(n);
    for (size_t i = 0; i < n; ++i) {
        const double t = i / fs;
        double v = 0;
        for (size_t k = 0; k < amp.size(); ++k) v += amp[k] * std::sin(2.0 * kPi * f * (k + 1) * t + phase * (k + 1));
        x[i] = static_cast<float>(0.5 * v * std::pow(10.0, dbPerSecond * t / 20.0));
    }
    return x;
}

static void testFindLoop() {
    const double fs = 48000, f = 233.08; // a period of 205.94 frames
    const Buf x = drifting(fs, 4.0, f, 0.3, {1.0, 0.5, 0.25}, -0.5);
    LoopResult r = findLoop(x.data(), nullptr, x.size(), fs, at(0.5, fs), at(3.5, fs), 0.25);
    const double periods = (r.end - r.start) * f / fs;
    double value = 1e9, slope = 1e9;
    if (r.end > r.start && r.end + 1 < x.size()) jointJumps(x, r.start, r.end, value, slope);
    check(r.start >= at(0.5, fs) && r.end <= at(3.5, fs) && r.end - r.start >= at(0.25, fs), "findLoop inside [from, to), at least minSeconds",
          "%.0f..%.0f (%.3f s)", static_cast<double>(r.start), static_cast<double>(r.end), (r.end - r.start) / fs);
    check(std::fabs(periods - std::round(periods)) < 0.01, "findLoop: a whole number of periods", "%.4f periods", periods);
    check(value < 0.1 && slope < 0.1, "findLoop: no step at the joint (mono, -0.5 dB/s)", "value %.3f, slope %.3f of a mean step", value, slope);
    check(r.quality > 0.95, "findLoop quality of a held tone", "%.4f", r.quality);

    // Stereo: the right channel has its own phase and partials; the joint has to fit both.
    const Buf right = drifting(fs, 4.0, f, 1.7, {0.8, 0.2, 0.5, 0.1}, -0.5);
    r = findLoop(x.data(), right.data(), x.size(), fs, at(0.5, fs), at(3.5, fs), 0.25);
    double vl = 1e9, sl = 1e9, vr = 1e9, sr = 1e9;
    if (r.end > r.start && r.end + 1 < x.size()) {
        jointJumps(x, r.start, r.end, vl, sl);
        jointJumps(right, r.start, r.end, vr, sr);
    }
    const double sp = (r.end - r.start) * f / fs;
    check(std::fabs(sp - std::round(sp)) < 0.01 && r.quality > 0.95, "findLoop stereo: whole periods, quality", "%.4f periods, quality %.4f", sp,
          r.quality);
    check(std::max(vl, vr) < 0.15 && std::max(sl, sr) < 0.15, "findLoop stereo: no step in either channel", "value %.3f/%.3f, slope %.3f/%.3f", vl, vr,
          sl, sr);

    // A low note at 44.1 kHz, longer minimum.
    const Buf low = drifting(44100, 5.0, 55.0, 0.0, {1.0, 0.7, 0.4, 0.3}, 0.3);
    r = findLoop(low.data(), nullptr, low.size(), 44100, at(1.0, 44100), at(4.5, 44100), 1.0);
    double v2 = 1e9, s2 = 1e9;
    if (r.end > r.start && r.end + 1 < low.size()) jointJumps(low, r.start, r.end, v2, s2);
    const double lp = (r.end - r.start) * 55.0 / 44100;
    check(r.end - r.start >= 44100 && std::fabs(lp - std::round(lp)) < 0.01 && v2 < 0.1 && s2 < 0.1, "findLoop A1 at 44.1 kHz, minSeconds 1",
          "%.4f periods, value %.3f, slope %.3f", lp, v2, s2);

    Buf n(at(3.0, fs));
    for (float& v : n) v = static_cast<float>(0.5 * noise());
    r = findLoop(n.data(), nullptr, n.size(), fs, 0, n.size(), 0.25);
    check(r.quality < 0.3, "findLoop in white noise: low quality", "%.3f", r.quality);
    r = findLoop(x.data(), nullptr, x.size(), fs, at(1.0, fs), at(1.2, fs), 0.25);
    check(r.start == 0 && r.end == 0, "findLoop with a stretch shorter than minSeconds: none", "%.0f..%.0f", static_cast<double>(r.start),
          static_cast<double>(r.end));
}

// --------------------------------------------------------------- helpers

static void testHelpers() {
    Buf x = {0.1f, -0.5f, 0.25f, std::numeric_limits<float>::infinity(), 0.3f};
    check(std::fabs(peakOf(x.data(), x.size()) - 0.5) < 1e-9, "peakOf skips what is not a number", "%.3f", peakOf(x.data(), x.size()));
    Buf r = {0.0f, 0.25f};
    double g = normaliseGainDb(x.data(), r.data(), 2, 0.0);
    check(std::fabs(g - 6.0206) < 0.001, "normaliseGainDb to 0 dB", "%+.4f dB", g);
    g = normaliseGainDb(x.data(), nullptr, x.size(), -1.0);
    check(std::fabs(g - 5.0206) < 0.001, "normaliseGainDb to -1 dB", "%+.4f dB", g);
    Buf z(100, 0.0f);
    check(normaliseGainDb(z.data(), z.data(), z.size(), -3.0) == 0.0, "normaliseGainDb of silence is 0 dB", "%+.2f",
          normaliseGainDb(z.data(), z.data(), z.size(), -3.0));

    Buf s(1000);
    for (size_t i = 0; i < s.size(); ++i) s[i] = static_cast<float>(std::sin(2 * kPi * (i + 0.3) / 100.0)); // crossings near 0, 50, 100...
    size_t c = nearestZeroCrossing(s.data(), s.size(), 140, 30);
    check(c == 150 || c == 149, "nearestZeroCrossing finds the nearest", "%.0f", static_cast<double>(c));
    c = nearestZeroCrossing(s.data(), s.size(), 160, 30);
    check(c == 150 || c == 149, "nearestZeroCrossing looks back as well", "%.0f", static_cast<double>(c));
    c = nearestZeroCrossing(s.data(), s.size(), 125, 10);
    check(c == 125, "nearestZeroCrossing: `at` when none in reach", "%.0f", static_cast<double>(c));
    c = nearestZeroCrossing(s.data(), s.size(), 5000, 10);
    check(c < s.size(), "nearestZeroCrossing with `at` past the end stays inside", "%.0f", static_cast<double>(c));
    Buf pos(50, 0.5f);
    c = nearestZeroCrossing(pos.data(), pos.size(), 20, 100);
    check(c == 20, "nearestZeroCrossing without a sign change", "%.0f", static_cast<double>(c));

    Buf ramp(1000);
    for (size_t i = 0; i < ramp.size(); ++i) ramp[i] = static_cast<float>(i) / 1000.0f;
    float out[20];
    waveformOverview(ramp.data(), ramp.size(), 0, 1000, 10, out);
    bool ok = true;
    for (int k = 0; k < 10; ++k) ok = ok && std::fabs(out[2 * k] - k * 0.1f) < 1e-6f && std::fabs(out[2 * k + 1] - (k * 100 + 99) / 1000.0f) < 1e-6f;
    check(ok, "waveformOverview min and max per column", "column 3: %.3f..%.3f", out[6], out[7]);
    float few[40];
    waveformOverview(ramp.data(), ramp.size(), 100, 110, 20, few);
    ok = true;
    for (int k = 0; k < 20; ++k) ok = ok && few[2 * k] == few[2 * k + 1] && few[2 * k] == ramp[100 + k / 2];
    check(ok, "waveformOverview with more columns than frames", "column 0: %.3f, column 19: %.3f", few[0], few[38]);
    waveformOverview(ramp.data(), ramp.size(), 2000, 3000, 20, few);
    ok = true;
    for (int k = 0; k < 40; ++k) ok = ok && few[k] == 0.0f;
    check(ok, "waveformOverview outside the sound: zeros", "");
}

// ----------------------------------------------------------- hostile input

static bool inRange(const PitchResult& p) {
    return isFinite(p.hz) && p.hz >= 0 && p.confidence >= 0 && p.confidence <= 1 && p.key >= -1 && p.key <= 127 && p.cents >= -50 && p.cents <= 50;
}

static void testHostile() {
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    std::vector<std::pair<std::string, Buf>> inputs;
    inputs.push_back({"one sample", Buf{0.5f}});
    inputs.push_back({"two samples", Buf{0.5f, -0.5f}});
    inputs.push_back({"zeros", Buf(1000, 0.0f)});
    inputs.push_back({"DC", Buf(48000, 0.5f)});
    inputs.push_back({"all NaN", Buf(48000, nan)});
    Buf mixed(48000);
    for (size_t i = 0; i < mixed.size(); ++i) mixed[i] = i % 7 == 0 ? nan : (i % 11 == 0 ? inf : (i % 13 == 0 ? -inf : static_cast<float>(std::sin(i * 0.05))));
    inputs.push_back({"NaN and inf in a tone", mixed});
    Buf huge(48000);
    for (size_t i = 0; i < huge.size(); ++i) huge[i] = (i / 37) % 2 ? 3.0e38f : -3.0e38f;
    inputs.push_back({"full float range square wave", huge});
    inputs.push_back({"tiny", Buf(48000, 1e-40f)});
    Buf click(48000, 0.0f);
    click[24000] = 1.0f;
    inputs.push_back({"one click", click});

    const double rates[] = {44100.0, 0.0, -48000.0, std::numeric_limits<double>::quiet_NaN(), 1e12, 8000.0, 384000.0};
    int bad = 0;
    std::string firstBad;
    auto fail = [&](const std::string& what) {
        if (!bad) firstBad = what;
        ++bad;
    };
    for (const auto& in : inputs)
        for (double fs : rates) {
            const Buf& x = in.second;
            const size_t n = x.size();
            const std::string tag = in.first + " @ " + std::to_string(fs);
            if (!inRange(detectPitch(x.data(), n, fs))) fail("pitch " + tag);
            for (double sens : {0.0, 0.5, 1.0, -3.0, 9.0, static_cast<double>(nan)})
                for (double gap : {0.03, 0.0, -1.0, 1e300, static_cast<double>(nan)}) {
                    const std::vector<size_t> h = detectTransients(x.data(), n, fs, sens, gap);
                    for (size_t i = 0; i < h.size(); ++i)
                        if (h[i] >= n || (i && h[i] <= h[i - 1])) fail("transients " + tag);
                }
            for (double lo : {70.0, -5.0, static_cast<double>(nan), 300.0})
                for (double hi : {180.0, 0.0, static_cast<double>(inf), 60.0}) {
                    const TempoResult t = detectTempo(x.data(), n, fs, lo, hi);
                    if (!isFinite(t.bpm) || !isFinite(t.beats) || t.bpm < 0 || t.confidence < 0 || t.confidence > 1) fail("tempo " + tag);
                }
            for (size_t from : {size_t(0), n / 2, SIZE_MAX})
                for (size_t to : {n, n / 3, SIZE_MAX})
                    for (double minS : {0.25, -1.0, static_cast<double>(nan), 1e300, 0.001}) {
                        const LoopResult r = findLoop(x.data(), x.data(), n, fs, from, to, minS);
                        if (r.end > n || r.start > r.end || !(r.quality >= 0 && r.quality <= 1)) fail("loop " + tag);
                        const LoopResult m = findLoop(x.data(), nullptr, n, fs, from, to, minS);
                        if (m.end > n || m.start > m.end || !(m.quality >= 0 && m.quality <= 1)) fail("loop mono " + tag);
                    }
        }
    for (const auto& in : inputs) {
        const Buf& x = in.second;
        const size_t n = x.size();
        const double p = peakOf(x.data(), n), g = normaliseGainDb(x.data(), x.data(), n, nan);
        if (!isFinite(p) || p < 0 || !isFinite(g)) fail("peak " + in.first);
        for (size_t a : {size_t(0), n - 1, n, SIZE_MAX})
            for (size_t reach : {size_t(0), size_t(5), SIZE_MAX})
                if (nearestZeroCrossing(x.data(), n, a, reach) >= n) fail("zero crossing " + in.first);
        float out[2 * 300];
        for (int cols : {1, 7, 300}) {
            waveformOverview(x.data(), n, 0, n, cols, out);
            for (int k = 0; k < 2 * cols; ++k)
                if (!isFinite(out[k])) fail("overview " + in.first);
        }
        waveformOverview(x.data(), n, 0, n, 0, out);
        waveformOverview(x.data(), n, 0, n, -4, out);
        waveformOverview(x.data(), n, SIZE_MAX, 3, 5, out);
        waveformOverview(x.data(), n, 0, n, 5, nullptr);
    }
    // Nothing at all.
    {
        if (!inRange(detectPitch(nullptr, 0, 48000)) || detectPitch(nullptr, 0, 48000).hz != 0) fail("pitch null");
        if (!detectTransients(nullptr, 0, 48000, 0.5).empty()) fail("transients null");
        if (detectTempo(nullptr, 0, 48000).bpm != 0) fail("tempo null");
        const LoopResult r = findLoop(nullptr, nullptr, 0, 48000, 0, 0);
        if (r.start || r.end) fail("loop null");
        if (peakOf(nullptr, 0) != 0 || normaliseGainDb(nullptr, nullptr, 0) != 0) fail("peak null");
        if (nearestZeroCrossing(nullptr, 0, 10, 10) != 0) fail("zero crossing null");
        float out[4] = {1, 1, 1, 1};
        waveformOverview(nullptr, 0, 0, 0, 2, out);
        if (out[0] != 0 || out[3] != 0) fail("overview null");
        if (!beatSlices(0, 4, 1).empty()) fail("slices of nothing");
        for (double beats : {0.0, -4.0, static_cast<double>(nan), static_cast<double>(inf), 1e300, 4.0})
            for (double div : {0.0, -1.0, static_cast<double>(nan), static_cast<double>(inf), 1e-300, 1.0}) {
                const std::vector<size_t> s = beatSlices(1000, beats, div);
                if (s.empty() || s[0] != 0 || s.size() > 1000) fail("slices");
                for (size_t i = 1; i < s.size(); ++i)
                    if (s[i] <= s[i - 1] || s[i] >= 1000) fail("slices order");
            }
    }
    check(bad == 0, "every function on hostile input stays in range", "%.0f problems", bad);
    if (bad) std::printf("      first: %s\n", firstBad.c_str());
}

// ------------------------------------------------------------------- speed

static void testSpeed() {
    const double fs = 48000;
    const Drum d = drumLoop(fs, 120, 30, kSixteenths); // one minute
    Buf left = d.x, right = d.x;
    const Buf pad = partials(fs, 60.0, 110.0, {0.3, 0.2, 0.1}, 0.0, 0.0);
    for (size_t i = 0; i < left.size() && i < pad.size(); ++i) {
        left[i] += pad[i];
        right[i] -= 0.5f * pad[i];
    }
    Buf mono(left.size());
    for (size_t i = 0; i < mono.size(); ++i) mono[i] = 0.5f * (left[i] + right[i]);
    auto time = [](auto&& f) {
        const auto t0 = std::chrono::steady_clock::now();
        f();
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    };
    double sink = 0;
    const double tp = time([&] { sink += detectPitch(mono.data(), mono.size(), fs).hz; });
    const double tt = time([&] { sink += static_cast<double>(detectTransients(mono.data(), mono.size(), fs, 0.5).size()); });
    const double tb = time([&] { sink += detectTempo(mono.data(), mono.size(), fs).bpm; });
    const double tl = time([&] { sink += findLoop(left.data(), right.data(), left.size(), fs, 0, left.size()).quality; });
    const double tk = time([&] { sink += normaliseGainDb(left.data(), right.data(), left.size()); });
    float out[2 * 1000];
    const double tw = time([&] {
        waveformOverview(left.data(), left.size(), 0, left.size(), 1000, out);
        sink += out[0];
    });
    std::printf("      one minute, stereo, 48 kHz: pitch %.3f s, transients %.3f s, tempo %.3f s, loop %.3f s, peak %.4f s, overview %.4f s\n", tp, tt, tb,
                tl, tk, tw);
    check(tp < 2 && tt < 5 && tb < 5 && tl < 5 && isFinite(sink), "one minute of sound analysed in a few seconds", "slowest %.2f s",
          std::max({tp, tt, tb, tl}));
}

int main() {
    const auto t0 = std::chrono::steady_clock::now();
    testHelpers();
    testBeatSlices();
    testPitch();
    testTransients();
    testTempo();
    testFindLoop();
    testHostile();
    testSpeed();
    std::printf("      %.1f s in all\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    std::printf("%s\n", failures ? "FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
