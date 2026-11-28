// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// Measurements of the meter against signals whose figures are known: the test signals of
// EBU Tech 3341 (loudness and true peak) and Tech 3342 (loudness range) that can be made from
// their description, and plain signals for everything else. Then the advisor, the comparison
// with a reference, and the JSON and the reports.
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "../dsp/Report.hpp"
#include "Demo.hpp"

using namespace hnl::analyzer;

static int failures = 0;
static void check(bool ok, const char* name, const char* fmt = "", ...) {
    char text[200];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    std::printf("%s  %-66s %s\n", ok ? "PASS" : "FAIL", name, text);
    if (!ok) ++failures;
}

static const double kTwoPi = 6.283185307179586;
static double lin(double db) { return std::pow(10.0, db / 20.0); }

struct Part {
    double seconds, db, hz;
};

/** Sines one after the other, the same in both channels, with the phase running on. */
static void tones(double fs, const std::vector<Part>& parts, std::vector<float>& l, std::vector<float>& r) {
    l.clear();
    double phase = 0.0;
    for (const Part& p : parts) {
        const size_t n = static_cast<size_t>(std::llround(p.seconds * fs));
        for (size_t i = 0; i < n; ++i) {
            l.push_back(static_cast<float>(lin(p.db) * std::sin(phase)));
            phase += kTwoPi * p.hz / fs;
        }
    }
    r = l;
}

struct Measured {
    std::unique_ptr<Result> r{new Result()};
    Result* operator->() { return r.get(); }
};

/** Measures in blocks of `block` frames. */
static Measured measure(double fs, const std::vector<float>& l, const std::vector<float>& r, uint32_t block = 4096, int channels = 2) {
    std::unique_ptr<Meter> m(new Meter());
    m->prepare(fs, channels);
    for (size_t at = 0; at < l.size(); at += block) {
        const uint32_t n = static_cast<uint32_t>(std::min<size_t>(block, l.size() - at));
        m->process(l.data() + at, channels > 1 ? r.data() + at : nullptr, n);
    }
    m->flush();
    Measured out;
    m->snapshot(*out.r);
    return out;
}

static void loudness() {
    std::vector<float> l, r;
    // EBU Tech 3341, table 1. Each must read within 0.1 LU.
    tones(48000, {{20, -23, 1000}}, l, r);
    Measured a = measure(48000, l, r);
    check(std::fabs(a->integrated + 23.0) < 0.1 && std::fabs(a->momentary + 23.0) < 0.1 && std::fabs(a->shortTerm + 23.0) < 0.1, "3341 case 1: 1 kHz at -23 dBFS reads -23.0 LUFS (M, S, I)",
          "I %.2f M %.2f S %.2f", a->integrated, a->momentary, a->shortTerm);
    tones(48000, {{20, -33, 1000}}, l, r);
    a = measure(48000, l, r);
    check(std::fabs(a->integrated + 33.0) < 0.1 && std::fabs(a->momentary + 33.0) < 0.1 && std::fabs(a->shortTerm + 33.0) < 0.1, "3341 case 2: 1 kHz at -33 dBFS reads -33.0 LUFS", "I %.2f",
          a->integrated);
    tones(48000, {{10, -36, 1000}, {60, -23, 1000}, {10, -36, 1000}}, l, r);
    a = measure(48000, l, r);
    check(std::fabs(a->integrated + 23.0) < 0.1, "3341 case 3: the relative gate leaves out the quiet ends", "I %.2f", a->integrated);
    tones(48000, {{10, -72, 1000}, {10, -36, 1000}, {60, -23, 1000}, {10, -36, 1000}, {10, -72, 1000}}, l, r);
    a = measure(48000, l, r);
    check(std::fabs(a->integrated + 23.0) < 0.1, "3341 case 4: the absolute gate leaves out what is below -70", "I %.2f", a->integrated);
    tones(48000, {{20, -26, 1000}, {20.1, -20, 1000}, {20, -26, 1000}}, l, r);
    a = measure(48000, l, r);
    check(std::fabs(a->integrated + 23.0) < 0.1, "3341 case 5: -26, -20, -26 dBFS reads -23.0 LUFS", "I %.2f", a->integrated);
    // Case 9: short-term loudness holds at -23.0 while 1.34 s at -20 and 1.66 s at -30 take turns.
    std::vector<Part> parts;
    for (int i = 0; i < 5; ++i) {
        parts.push_back({1.34, -20, 1000});
        parts.push_back({1.66, -30, 1000});
    }
    tones(48000, parts, l, r);
    a = measure(48000, l, r);
    float lo = 0, hi = -100;
    for (int i = 0; i < a->loudCount; ++i)
        if ((i + 1) * a->loudStep > 3.05) { lo = std::min(lo, a->loudS[i]); hi = std::max(hi, a->loudS[i]); }
    check(std::fabs(lo + 23.0) < 0.1 && std::fabs(hi + 23.0) < 0.1 && std::fabs(a->shortTermMax + 23.0) < 0.1, "3341 case 9: short-term loudness stays at -23.0 after 3 s",
          "%.2f to %.2f, max %.2f", lo, hi, a->shortTermMax);
    // Case 12: the same for momentary, with 0.18 s at -20 and 0.22 s at -30.
    parts.clear();
    for (int i = 0; i < 25; ++i) {
        parts.push_back({0.18, -20, 1000});
        parts.push_back({0.22, -30, 1000});
    }
    tones(48000, parts, l, r);
    a = measure(48000, l, r);
    lo = 0;
    hi = -100;
    for (int i = 0; i < a->loudCount; ++i)
        if ((i + 1) * a->loudStep > 1.0) { lo = std::min(lo, a->loudM[i]); hi = std::max(hi, a->loudM[i]); }
    check(std::fabs(lo + 23.0) < 0.1 && std::fabs(hi + 23.0) < 0.1 && std::fabs(a->momentaryMax + 23.0) < 0.1, "3341 case 12: momentary loudness stays at -23.0",
          "%.2f to %.2f, max %.2f", lo, hi, a->momentaryMax);

    // The weighting: a channel alone is 3 dB down, and the curve is the one of BS.1770 at other rates too.
    tones(48000, {{10, -23, 1000}}, l, r);
    std::vector<float> none(l.size(), 0.0f);
    a = measure(48000, l, none);
    check(std::fabs(a->integrated + 26.01) < 0.1, "one channel alone reads 3 dB lower", "I %.2f", a->integrated);
    a = measure(48000, l, r, 4096, 1);
    check(std::fabs(a->integrated + 26.01) < 0.1 && a->channels == 1, "a mono sound is measured as one channel", "I %.2f", a->integrated);
    for (double fs : {44100.0, 88200.0, 96000.0, 192000.0}) {
        tones(fs, {{6, -23, 1000}}, l, r);
        a = measure(fs, l, r);
        check(std::fabs(a->integrated + 23.0) < 0.1, "1 kHz at -23 dBFS reads -23.0 LUFS at this rate", "%.0f Hz: I %.2f", fs, a->integrated);
    }
    // BS.1770, against 1 kHz: at 100 Hz the weighting takes 1.8 dB away, at 10 kHz it adds 3.3.
    tones(48000, {{6, -23, 100}}, l, r);
    const double low = measure(48000, l, r)->integrated;
    tones(48000, {{6, -23, 10000}}, l, r);
    const double high = measure(48000, l, r)->integrated;
    check(std::fabs(low + 24.8) < 0.15 && std::fabs(high + 19.65) < 0.15, "the K-weighting at 100 Hz and at 10 kHz", "%.2f and %.2f LUFS", low, high);

    // Silence, and something too short to have a loudness.
    std::vector<float> silent(48000 * 5, 0.0f);
    a = measure(48000, silent, silent);
    check(a->integrated == kSilence && a->range == 0.0 && !hasLoudness(*a.r) && plr(*a.r) == 0.0, "silence has no loudness, and no figure is made up for it", "I %.1f", a->integrated);
    tones(48000, {{0.2, -20, 1000}}, l, r);
    a = measure(48000, l, r);
    check(a->integrated == kSilence && a->frames == 9600, "0.2 s is too short for an integrated loudness", "I %.1f", a->integrated);
}

static void range() {
    std::vector<float> l, r;
    // EBU Tech 3342, table 1. Each within 1 LU.
    struct Case { std::vector<Part> parts; double lra; const char* name; };
    const Case cases[] = {
        {{{20, -20, 1000}, {20, -30, 1000}}, 10, "3342 case 1: -20 then -30 dBFS has a range of 10 LU"},
        {{{20, -20, 1000}, {20, -15, 1000}}, 5, "3342 case 2: -20 then -15 dBFS has a range of 5 LU"},
        {{{20, -40, 1000}, {20, -20, 1000}}, 20, "3342 case 3: -40 then -20 dBFS has a range of 20 LU"},
        {{{20, -50, 1000}, {20, -35, 1000}, {20, -20, 1000}, {20, -35, 1000}, {20, -50, 1000}}, 15, "3342 case 4: the relative gate leaves out the -50 dBFS ends"},
    };
    for (const Case& c : cases) {
        tones(48000, c.parts, l, r);
        Measured a = measure(48000, l, r);
        check(std::fabs(a->range - c.lra) < 1.0 && a->rangeHigh - a->rangeLow == a->range, c.name, "LRA %.2f (%.1f to %.1f)", a->range, a->rangeLow, a->rangeHigh);
    }
    tones(48000, {{30, -18, 1000}}, l, r);
    Measured a = measure(48000, l, r);
    check(a->range < 0.05, "a steady tone has no range", "LRA %.2f", a->range);
}

static void peaks() {
    std::vector<float> l, r;
    // EBU Tech 3341, cases 15 to 19: sines whose samples miss the crest. The true peak must read
    // the level of the sine within +0.2 and -0.4 dB, whatever the samples show.
    struct Case { double div, phase, db, expect; const char* name; };
    const Case cases[] = {
        {4, 0, -6, -6, "3341 case 15: fs/4 at 0 degrees, -6 dBFS"},
        {4, 45, -6, -6, "3341 case 16: fs/4 at 45 degrees: the samples read 3 dB low"},
        {6, 60, -6, -6, "3341 case 17: fs/6 at 60 degrees"},
        {8, 67.5, -6, -6, "3341 case 18: fs/8 at 67.5 degrees"},
        {4, 45, 0, 0, "3341 case 19: fs/4 at 45 degrees and full scale: the peak is at 0 dBTP"},
    };
    for (const Case& c : cases) {
        const double fs = 48000;
        l.assign(48000, 0.0f);
        // Faded in and out over 50 ms: a sine that starts in mid swing has a true peak above its own level.
        for (size_t i = 0; i < l.size(); ++i) {
            const double edge = std::min(1.0, static_cast<double>(std::min(i, l.size() - 1 - i)) / 2400.0), fade = 0.5 - 0.5 * std::cos(3.141592653589793 * edge);
            l[i] = static_cast<float>(fade * lin(c.db) * std::sin(kTwoPi * i / c.div + c.phase * kTwoPi / 360.0));
        }
        r = l;
        Measured a = measure(fs, l, r);
        const double tp = truePeakDb(*a.r), sp = samplePeakDb(*a.r);
        check(tp - c.expect < 0.2 && tp - c.expect > -0.4 && tp >= sp - 1e-9, c.name, "true peak %.2f dBTP, sample peak %.2f dB", tp, sp);
    }
    // The last samples of a sound count too: the peak sits in the final sixteen.
    l.assign(4800, 0.0f);
    l[4795] = 0.5f;
    r.assign(4800, 0.0f);
    Measured a = measure(48000, l, r);
    check(std::fabs(truePeakDb(*a.r, 0) + 6.02) < 0.05 && truePeakDb(*a.r, 1) == kSilence, "a peak in the last samples is found, and each channel has its own",
          "L %.2f dBTP, R %.1f", truePeakDb(*a.r, 0), truePeakDb(*a.r, 1));
    check(std::fabs(a->ch[0].maxSample - 0.5) < 1e-7 && a->ch[0].minSample == 0.0, "the largest and smallest sample", "%.3f and %.3f", a->ch[0].maxSample, a->ch[0].minSample);
}

static void amplitude() {
    std::vector<float> l, r;
    tones(44100, {{10, -12, 440}}, l, r);
    for (float& v : r) v *= 0.5f; // the right 6.02 dB lower
    Measured a = measure(44100, l, r);
    check(std::fabs(totalRmsDb(*a.r, 0, true) + 12.0) < 0.01 && std::fabs(totalRmsDb(*a.r, 0, false) + 15.01) < 0.01, "total RMS of a sine: its peak level, or 3.01 dB less against a square wave",
          "%.3f and %.3f dB", totalRmsDb(*a.r, 0, true), totalRmsDb(*a.r, 0, false));
    check(std::fabs(balanceDb(*a.r) - 6.02) < 0.01 && std::fabs(totalRmsDb(*a.r, 1, true) + 18.02) < 0.01, "each channel has its own RMS, and the balance is their difference", "%.2f dB",
          balanceDb(*a.r));
    check(std::fabs(crestDb(*a.r, 0) - 3.01) < 0.02, "the crest factor of a sine is 3.01 dB", "%.3f dB", crestDb(*a.r, 0));
    bool windows = true;
    for (int w = 0; w < kRmsWindows; ++w) {
        const double expect = std::floor(10.0 / (kRmsWindowMs[w] * 0.001) + 1e-6);
        windows = windows && std::fabs(static_cast<double>(a->rms[w].count) - expect) <= 1.0;
        windows = windows && std::fabs(maxRmsDb(*a.r, w, 0, true) + 12.0) < 0.35 && std::fabs(minRmsDb(*a.r, w, 0, true) + 12.0) < 0.35;
        windows = windows && std::fabs(averageRmsDb(*a.r, w, 0, true) + 12.0) < 0.05;
        uint64_t counted = 0;
        for (int b = 0; b < kHistBins; ++b) counted += a->rms[w].hist[0][b];
        windows = windows && counted == a->rms[w].count;
    }
    check(windows, "every window length: the right number of windows, all at the level of the sine", "%llu windows of 50 ms", static_cast<unsigned long long>(a->rms[2].count));
    const int bin = histBin(lin(-12.0) * lin(-12.0) / 2.0);
    check(a->rms[2].hist[0][bin] + a->rms[2].hist[0][bin - 1] + a->rms[2].hist[0][bin + 1] == a->rms[2].count && std::fabs(kHistFloorDb + 0.5 * bin + 15.01) <= 0.5,
          "the histogram has them all at that level", "bin %d holds %u", bin, a->rms[2].hist[0][bin]);

    // Loud then quiet: the windows tell them apart, the total does not.
    tones(48000, {{5, -6, 440}, {5, -30, 440}}, l, r);
    a = measure(48000, l, r);
    check(std::fabs(maxRmsDb(*a.r, 3, 0, true) + 6.0) < 0.1 && std::fabs(minRmsDb(*a.r, 3, 0, true) + 30.0) < 0.1, "maximum and minimum RMS are the loud and the quiet part",
          "%.2f and %.2f dB", maxRmsDb(*a.r, 3, 0, true), minRmsDb(*a.r, 3, 0, true));
    const double mean = 20.0 * std::log10((lin(-6) + lin(-30)) / 2.0);
    check(std::fabs(averageRmsDb(*a.r, 3, 0, true) - mean) < 0.05 && std::fabs(totalRmsDb(*a.r, 0, true) + 8.98) < 0.05, "average RMS is the mean of the windows, total RMS of everything",
          "%.2f and %.2f dB", averageRmsDb(*a.r, 3, 0, true), totalRmsDb(*a.r, 0, true));

    // DC offset.
    l.assign(48000, 0.25f);
    r.assign(48000, -0.1f);
    a = measure(48000, l, r);
    check(std::fabs(dcOffset(*a.r, 0) - 0.25) < 1e-6 && std::fabs(dcOffset(*a.r, 1) + 0.1) < 1e-6, "DC offset per channel", "%.4f and %.4f", dcOffset(*a.r, 0), dcOffset(*a.r, 1));

    // Clipping: flat tops of two samples or more at full scale, and anything over it.
    l.assign(48000, 0.0f);
    r.assign(48000, 0.0f);
    for (int i = 100; i < 105; ++i) l[i] = 1.0f;       // five in a row
    l[200] = 1.0f;                                     // one alone at full scale: a peak, not a flat top
    l[300] = -1.0f;
    l[301] = -1.0f;                                    // two below
    l[400] = 1.0f;
    l[401] = -1.0f;                                    // up and down: no flat top
    r[500] = 1.2f;                                     // over full scale: counts alone
    a = measure(48000, l, r);
    check(a->ch[0].clipped == 7 && a->ch[1].clipped == 1 && clipped(*a.r) == 8, "clipped samples: flat tops at full scale, and samples over it", "L %llu, R %llu",
          static_cast<unsigned long long>(a->ch[0].clipped), static_cast<unsigned long long>(a->ch[1].clipped));
    tones(48000, {{2, -0.1, 997}}, l, r);
    a = measure(48000, l, r);
    check(clipped(*a.r) == 0, "a sine just under full scale is not clipped", "%llu", static_cast<unsigned long long>(clipped(*a.r)));
}

static void stereo() {
    std::vector<float> l, r;
    tones(48000, {{4, -12, 440}}, l, r);
    Measured a = measure(48000, l, r);
    check(std::fabs(correlation(*a.r) - 1.0) < 1e-6 && sideDb(*a.r, true) == kSilence && std::fabs(midDb(*a.r, true) + 12.0) < 0.01, "the same left and right: correlation +1, no side",
          "%.3f, mid %.2f dB", correlation(*a.r), midDb(*a.r, true));
    for (float& v : r) v = -v;
    a = measure(48000, l, r);
    check(std::fabs(correlation(*a.r) + 1.0) < 1e-6 && midDb(*a.r, true) < -90.0 && std::fabs(sideDb(*a.r, true) + 12.0) < 0.01 && std::fabs(a->correlationNow + 1.0) < 1e-3,
          "opposite: correlation -1, no mid", "%.3f, side %.2f dB", correlation(*a.r), sideDb(*a.r, true));
    for (size_t i = 0; i < r.size(); ++i) r[i] = static_cast<float>(lin(-12) * std::cos(kTwoPi * 440.0 * i / 48000.0));
    a = measure(48000, l, r);
    check(std::fabs(correlation(*a.r)) < 0.01 && std::fabs(midDb(*a.r, true) - sideDb(*a.r, true)) < 0.05, "a quarter turn apart: correlation 0, mid and side alike", "%.3f", correlation(*a.r));
}

static void spectrum() {
    std::vector<float> l, r;
    bool ok = true;
    double worst = 0;
    for (double hz : {31.5, 63.0, 250.0, 1000.0, 4000.0, 16000.0}) {
        tones(48000, {{4, -20, hz}}, l, r);
        Measured a = measure(48000, l, r);
        double sum = 0.0;
        int loudest = 0;
        for (int b = 0; b < kSpecBands; ++b) {
            sum += a->spectrum[b];
            if (a->spectrum[b] > a->spectrum[loudest]) loudest = b;
        }
        const double total = 10.0 * std::log10(2.0 * sum);
        ok = ok && std::fabs(total + 20.0) < 0.3 && hz >= specEdge(loudest) * 0.97 && hz <= specEdge(loudest + 1) * 1.03;
        worst = std::max(worst, std::fabs(total + 20.0));
    }
    check(ok, "a sine at -20 dBFS reads -20 dB in the spectrum, in the band it lies in", "off by at most %.2f dB", worst);
    check(std::fabs(specEdge(0) - 20.0) < 1e-9 && std::fabs(specEdge(kSpecBands) - 20000.0) < 1e-6, "the bands reach from 20 Hz to 20 kHz", "%d bands", kSpecBands);
    // White noise: every third octave 1 dB above the one below it.
    l.resize(48000 * 20);
    r.resize(l.size());
    uint32_t seed = 12345;
    for (size_t i = 0; i < l.size(); ++i) {
        seed = seed * 1664525u + 1013904223u;
        l[i] = r[i] = 0.25f * (static_cast<float>(seed >> 8) / 8388608.0f - 1.0f);
    }
    Measured a = measure(48000, l, r);
    const double slope = (spectrumDb(*a.r, 28) - spectrumDb(*a.r, 8)) / 20.0;
    check(std::fabs(slope - 1.0) < 0.08, "white noise rises 1 dB per third octave", "%.3f dB per band over %llu transforms", slope, static_cast<unsigned long long>(a->spectra));
}

static void series() {
    std::vector<float> l, r;
    // The outline and the loudness history never outgrow their places, however long the sound.
    tones(8000, {{700, -20, 100}, {700, -6, 100}}, l, r);
    Measured a = measure(8000, l, r);
    check(a->waveCount > kSeries / 2 && a->waveCount <= kSeries && std::fabs(a->waveCount * a->waveStep - 1400.0) < 2.0 * a->waveStep, "23 minutes: the outline is halved until it fits",
          "%d points of %.2f s", a->waveCount, a->waveStep);
    check(a->loudCount > kSeries / 2 && a->loudCount <= kSeries && std::fabs(a->loudCount * a->loudStep - 1400.0) < 2.0 * a->loudStep, "and so is the loudness over time", "%d points of %.1f s",
          a->loudCount, a->loudStep);
    const int quarter = a->waveCount / 4, three = 3 * a->waveCount / 4;
    check(std::fabs(a->waveMax[0][quarter] - lin(-20)) < 0.01 && std::fabs(a->waveMin[1][three] + lin(-6)) < 0.01, "what is left still shows the levels where they were", "%.3f and %.3f",
          a->waveMax[0][quarter], a->waveMin[1][three]);
    check(std::fabs(a->loudS[a->loudCount / 4] + 21.1) < 1.5 && std::fabs(a->loudS[3 * a->loudCount / 4] + 7.1) < 1.5, "and the loudness of each half", "%.1f and %.1f LUFS",
          a->loudS[a->loudCount / 4], a->loudS[3 * a->loudCount / 4]);

    // Rolling: the newest stays, the oldest goes.
    std::unique_ptr<Meter> m(new Meter());
    m->prepare(8000);
    m->reset(true);
    m->process(l.data(), r.data(), static_cast<uint32_t>(l.size()));
    std::unique_ptr<Result> res(new Result());
    m->snapshot(*res);
    check(res->waveCount == kSeries && std::fabs(res->waveStep - 0.1) < 1e-12 && std::fabs(res->waveMax[0][0] - lin(-6)) < 0.01, "rolling: the outline holds the last 3.4 minutes", "%d points of %.3f s",
          res->waveCount, res->waveStep);
    check(res->loudCount == kSeries && res->loudStep == 0.1 && std::fabs(res->loudS[0] + 7.1) < 1.5, "rolling: and so does the loudness, point for point", "%d points", res->loudCount);
    check(std::fabs(res->seconds() - 1400.0) < 1e-6 && res->integrated < -7.0 && res->integrated > -9.0, "while the figures are still of everything since the reset", "I %.2f", res->integrated);
}

static void consistency() {
    // The same sound gives the same figures whatever the block length, and at any place in a buffer.
    std::vector<float> l, r;
    demo::song(44100.0, 24.0, l, r);
    Measured whole = measure(44100, l, r, static_cast<uint32_t>(l.size()));
    bool same = true;
    for (uint32_t block : {1u, 7u, 64u, 441u, 1024u, 4410u}) {
        Measured a = measure(44100, l, r, block);
        same = same && a->integrated == whole->integrated && a->range == whole->range && a->ch[0].truePeak == whole->ch[0].truePeak && a->ch[1].sumSquares == whole->ch[1].sumSquares;
        same = same && a->rms[2].maxMs[0] == whole->rms[2].maxMs[0] && a->spectrum[12] == whole->spectrum[12] && a->waveCount == whole->waveCount && a->loudCount == whole->loudCount;
        same = same && std::memcmp(a->loudS, whole->loudS, sizeof(float) * static_cast<size_t>(a->loudCount)) == 0;
    }
    check(same, "blocks of 1 to 4410 frames give the very same figures", "I %.4f LUFS, LRA %.2f", whole->integrated, whole->range);
    check(whole->integrated > -30.0 && whole->range > 1.0 && plr(*whole.r) > 4.0 && correlation(*whole.r) > 0.5 && correlation(*whole.r) < 1.0, "the demo song measures as a song would",
          "I %.2f, PLR %.2f, LRA %.1f, correlation %.2f", whole->integrated, plr(*whole.r), whole->range, correlation(*whole.r));
    // A gain of 6.02 dB moves every level by that much and nothing else.
    std::vector<float> l2 = l, r2 = r;
    for (size_t i = 0; i < l2.size(); ++i) { l2[i] *= 0.5f; r2[i] *= 0.5f; }
    Measured half = measure(44100, l2, r2);
    check(std::fabs(whole->integrated - half->integrated - 6.0206) < 0.001 && std::fabs(truePeakDb(*whole.r) - truePeakDb(*half.r) - 6.0206) < 0.001 &&
              std::fabs(plr(*whole.r) - plr(*half.r)) < 0.001 && std::fabs(whole->range - half->range) < 0.011 && std::fabs(crestDb(*whole.r, 0) - crestDb(*half.r, 0)) < 0.001,
          "half the level: 6.02 dB less loudness and peak, the same PLR, range and crest", "%.3f LU", whole->integrated - half->integrated);
    // Odd sample rates and a very low one do not upset it.
    bool rates = true;
    for (double fs : {100.0, 8000.0, 11025.0, 22050.0, 32000.0, 384000.0}) {
        tones(fs, {{3, -20, fs / 16}}, l, r);
        Measured a = measure(fs, l, r, 333);
        rates = rates && a->frames == l.size() && a->ch[0].samplePeak > 0.09 && a->ch[0].samplePeak < 0.11 && a->waveCount > 0 && a->waveCount <= kSeries;
    }
    check(rates, "sample rates from 100 Hz to 384 kHz are measured", "");
}

static void advisor() {
    // The example the plugin was written from.
    Targets t = {-12.5, -2.0, 10.5};
    Advice a = advise(true, -13.54, -1.58, t);
    check(a.kind == Advice::kDynamics && std::fabs(a.gain - 1.04) < 1e-9 && std::fabs(a.peakReduction - 1.46) < 1e-9 && std::fabs(a.plr - 11.96) < 1e-9 && std::fabs(a.maxGain + 0.42) < 1e-9,
          "-13.54 LUFS and -1.58 dBTP for -12.5 and -2.0: +1.04 dB of gain, 1.46 dB off the peaks", "gain %+.2f, reduction %.2f, PLR %.2f", a.gain, a.peakReduction, a.plr);
    check(std::fabs(a.targetPlr - 10.5) < 1e-9 && std::string(adviceText(a, t)).find("1.46 dB") != std::string::npos && std::string(adviceTitle(a)) == "DYNAMIC PROCESSING",
          "and it says so: dynamic processing, not gain", "%s", adviceTitle(a));
    a = advise(true, -18.0, -8.0, t);
    check(a.kind == Advice::kGain && std::fabs(a.gain - 5.5) < 1e-9 && a.peakReduction == 0.0 && std::string(adviceText(a, t)).find("Raise the level by 5.50 dB") != std::string::npos,
          "room under the ceiling: gain alone", "gain %+.2f, the peak ends at %.2f", a.gain, a.truePeak + a.gain);
    a = advise(true, -9.0, -3.0, t);
    check(a.kind == Advice::kGain && std::fabs(a.gain + 3.5) < 1e-9 && std::string(adviceText(a, t)).find("Lower the level by 3.50 dB") != std::string::npos, "too loud: gain alone, downwards", "gain %+.2f",
          a.gain);
    a = advise(true, -12.56, -2.07, t);
    check(a.kind == Advice::kAchieved, "-12.56 LUFS and -2.07 dBTP: target achieved", "gain %+.2f", a.gain);
    a = advise(true, -12.5, -0.5, t);
    check(a.kind == Advice::kPeaks && std::fabs(a.maxGain + 1.5) < 1e-9 && std::fabs(a.peakReduction - 1.5) < 1e-9, "loud enough but over the ceiling: the peaks must come down", "by %.2f dB",
          a.peakReduction);
    a = advise(false, 0, 0, t);
    check(a.kind == Advice::kNoMeasurement && std::string(adviceTitle(a)) == "NO MEASUREMENT", "nothing measured: no advice", "");
    bool same = true;
    for (int i = 0; i < 50; ++i) same = same && adviceText(advise(true, -13.54, -1.58, t), t) == adviceText(advise(true, -13.54, -1.58, t), t);
    check(same, "the same figures always give the same advice", "");
    const Advice wide = advise(true, -20.0, -3.0, t), tight = advise(true, -8.0, -2.0, t);
    check(std::string(adviceText(wide, t)).find("more dynamic") != std::string::npos && std::string(adviceText(tight, t)).find("more limited") != std::string::npos,
          "a PLR far from the preferred one is remarked on, either way", "%+.1f and %+.1f dB", wide.plrFromPreferred, tight.plrFromPreferred);

    // What APPLY TO MASTER sets the limiter of HNL Master to.
    const Targets m = {-14.0, -1.0, 13.0};
    LimiterChange c = changeFor(advise(true, -16.57, -3.0, m), m, {3.4, -2.1, true});
    check(c.any && c.reaches && std::fabs(c.gain - 6.0) < 1e-9 && std::fabs(c.ceiling + 1.0) < 1e-9, "too quiet: the limiter's gain up by the required gain, the ceiling to the true-peak target", "gain %.2f, ceiling %.2f",
          c.gain, c.ceiling);
    c = changeFor(advise(true, -13.32, -2.11, m), m, {3.4, -2.1, true});
    check(c.any && c.reaches && std::fabs(c.gain - 2.7) < 1e-9 && std::fabs(c.ceiling + 1.0) < 1e-9, "too loud: the gain down", "gain %.2f", c.gain);
    c = changeFor(advise(true, -13.32, -2.11, m), m, {0.3, -1.0, true});
    check(c.any && !c.reaches && c.gain == 0.0, "further down than the knob goes: as far as it goes, and said not to reach", "gain %.2f", c.gain);
    c = changeFor(advise(true, -40.0, -30.0, m), m, {20.0, -1.0, true});
    check(c.any && !c.reaches && c.gain == 24.0, "and the same upwards", "gain %.2f", c.gain);
    c = changeFor(advise(true, -40.0, -30.0, m), m, {24.0, -1.0, true});
    check(!c.any && !c.reaches, "with the knob at its end already there is nothing to set", "gain %.2f", c.gain);
    c = changeFor(advise(true, -16.0, -3.0, m), m, {9.0, -0.5, false});
    check(c.any && std::fabs(c.gain - 2.0) < 1e-9, "a limiter that is off added nothing: its gain becomes the required gain alone", "gain %.2f", c.gain);
    c = changeFor(advise(true, -14.0, 0.2, m), m, {5.0, 0.0, true});
    check(c.any && std::fabs(c.gain - 5.0) < 1e-9 && std::fabs(c.ceiling + 1.0) < 1e-9, "loud enough but over the ceiling: the ceiling alone", "ceiling %.2f", c.ceiling);
    c = changeFor(advise(true, -14.02, -1.3, m), m, {5.0, -0.2, true});
    check(!c.any && c.gain == 5.0 && c.ceiling == -0.2, "on target: nothing to set, whatever the knobs stand at", "gain %.2f", c.gain);
    c = changeFor(advise(false, 0, 0, m), m, {5.0, -0.2, true});
    check(!c.any, "nothing measured: nothing to set", "");
}

static void referenceRows() {
    // The figures of two real masters, typed in.
    Analysis track, ref;
    check(fromJson(demo::currentJson(), track) && fromJson(demo::referenceJson(), ref), "figures typed in by hand are read", "%s, %s", track.name.c_str(), ref.name.c_str());
    const View view;
    std::vector<Row> rows = compare(track, &ref, 0.0, view);
    const auto row = [&](const char* name) -> const Row& {
        for (const Row& r : rows)
            if (std::strcmp(r.name, name) == 0) return r;
        return rows[0];
    };
    check(std::fabs(row("LUFS-I").track + 12.56) < 1e-9 && std::fabs(row("LUFS-I").delta() + 0.09) < 1e-9, "LUFS-I -12.56 against -12.47: -0.09", "%+.2f", row("LUFS-I").delta());
    check(std::fabs(row("True peak").delta() + 0.03) < 1e-6 && std::fabs(row("PLR").track - 10.49) < 1e-6 && std::fabs(row("PLR").reference - 10.43) < 1e-6, "true peak -0.03, PLR 10.49 against 10.43",
          "%.2f and %.2f", row("PLR").track, row("PLR").reference);
    check(std::fabs(row("RMS L").track + 15.56) < 1e-6 && std::fabs(row("RMS L").delta() + 0.43) < 1e-6 && std::fabs(row("RMS R").delta() + 0.32) < 1e-6, "RMS left -0.43, right -0.32", "%+.2f and %+.2f",
          row("RMS L").delta(), row("RMS R").delta());
    check(std::fabs(row("Sample peak").delta() + 0.01) < 1e-6 && row("Clipped samples").track == 0 && row("Clipped samples").haveReference, "sample peak -0.01, no clipped samples", "%+.2f",
          row("Sample peak").delta());
    check(!row("LRA").haveTrack && !row("Correlation").haveReference && !row("Short-term max").haveTrack, "what was not noted down is not made up", "");
    // Matched in loudness, the reference moves by the difference, and what is no level stays.
    double gain = matchGain(track, ref, kMatchLoudness);
    rows = compare(track, &ref, gain, view);
    check(std::fabs(gain + 0.09) < 1e-9 && std::fabs(row("LUFS-I").delta()) < 1e-9 && std::fabs(row("True peak").delta() - 0.06) < 1e-6 && std::fabs(row("PLR").delta() - 0.06) < 1e-6,
          "matched in loudness: the same LUFS, and the true peak 0.06 dB higher", "reference %+.2f dB", gain);
    gain = matchGain(track, ref, kMatchPeak);
    rows = compare(track, &ref, gain, view);
    check(std::fabs(gain + 0.03) < 1e-6 && std::fabs(row("True peak").delta()) < 1e-6 && std::fabs(row("LUFS-I").delta() + 0.06) < 1e-6, "matched in true peak: the same peak, and 0.06 LU quieter",
          "reference %+.2f dB", gain);
    Analysis none;
    none.capture.status = kEmpty;
    check(matchGain(track, none, kMatchLoudness) == 0.0 && compare(none, nullptr, 0.0, view).size() == rows.size() && !compare(none, nullptr, 0.0, view)[0].haveTrack,
          "no reference, or nothing measured: no figures and no gain", "");
}

static void jsonText() {
    Analysis a = demo::measured("D\xC3\xA9j\xC3\xA0 \"des\" souvenirs\\", 1.0, 30.0);
    a.source = "WAV, 24 bit";
    a.capture.startFrame = 441000;
    a.capture.seeks = 2;
    const View view;
    const std::string text = toJson(a, view);
    Analysis b;
    check(fromJson(text, b), "an analysis written as JSON is read back", "%zu bytes", text.size());
    bool same = b.name == a.name && b.source == a.source && b.r.frames == a.r.frames && b.r.sampleRate == a.r.sampleRate && b.capture.startFrame == 441000 && b.capture.seeks == 2;
    same = same && b.capture.status == kComplete && b.capture.ended == kTransportStopped && b.capture.positionKnown && b.capture.automatic && b.have == kHaveAll;
    same = same && std::fabs(b.r.integrated - a.r.integrated) < 1e-12 && std::fabs(b.r.range - a.r.range) < 1e-12 && std::fabs(b.r.momentaryMax - a.r.momentaryMax) < 1e-12;
    for (int c = 0; c < 2; ++c) {
        same = same && std::fabs(b.r.ch[c].truePeak / a.r.ch[c].truePeak - 1.0) < 1e-14 && std::fabs(b.r.ch[c].sumSquares / a.r.ch[c].sumSquares - 1.0) < 1e-14;
        same = same && std::fabs(b.r.ch[c].sum - a.r.ch[c].sum) < 1e-9 && b.r.ch[c].clipped == a.r.ch[c].clipped && std::fabs(b.r.ch[c].minSample - a.r.ch[c].minSample) < 1e-15;
    }
    same = same && std::fabs(b.r.sumLR / a.r.sumLR - 1.0) < 1e-14;
    for (int w = 0; w < kRmsWindows; ++w) {
        same = same && b.r.rms[w].count == a.r.rms[w].count && std::fabs(b.r.rms[w].maxMs[1] / a.r.rms[w].maxMs[1] - 1.0) < 1e-14 && std::fabs(b.r.rms[w].sumRms[0] / a.r.rms[w].sumRms[0] - 1.0) < 1e-14;
        same = same && std::memcmp(b.r.rms[w].hist, a.r.rms[w].hist, sizeof(a.r.rms[w].hist)) == 0;
    }
    for (int k = 0; k < kSpecBands; ++k) same = same && std::fabs(b.r.spectrum[k] - a.r.spectrum[k]) <= 1e-8 * a.r.spectrum[k];
    same = same && b.r.spectra == a.r.spectra && b.r.waveCount == a.r.waveCount && b.r.loudCount == a.r.loudCount && b.r.waveStep == a.r.waveStep && b.r.loudStep == a.r.loudStep;
    for (int i = 0; i < a.r.waveCount; ++i) same = same && std::fabs(b.r.waveMax[0][i] - a.r.waveMax[0][i]) < 1e-4 && std::fabs(b.r.waveMin[1][i] - a.r.waveMin[1][i]) < 1e-4;
    for (int i = 0; i < a.r.loudCount; ++i) same = same && std::fabs(b.r.loudS[i] - a.r.loudS[i]) < 0.006 && std::fabs(b.r.loudM[i] - a.r.loudM[i]) < 0.006;
    check(same, "and is the analysis that was written: every figure, the histograms, the spectrum, the outline", "");
    Analysis again;
    check(fromJson(toJson(b, view), again) && summaryJson(again, view) == summaryJson(a, view), "written and read once more, every figure a person reads is still the same", "");
    check(text.find("\"integrated_lufs\":") != std::string::npos && text.find("\"true_peak_dbtp\":") != std::string::npos && text.find("D\xC3\xA9j\xC3\xA0 \\\"des\\\" souvenirs\\\\") != std::string::npos,
          "the summary is there for a person to read, and the name is as it was typed", "");

    // What is not an analysis is refused, and nothing in a file can upset what reads it.
    Analysis c;
    bool refused = !fromJson("", c) && !fromJson("{}", c) && !fromJson("[1,2,3]", c) && !fromJson("{\"name\":\"x\"}", c) && !fromJson("not json", c) && !fromJson("{\"integrated_lufs\":", c);
    refused = refused && !fromJson(std::string(40, '[') + std::string(40, ']'), c) && !fromJson("{\"integrated_lufs\":-12.5} trailing", c);
    check(refused, "what is not an analysis is refused", "");
    check(fromJson("{\"integrated_lufs\":1e999,\"true_peak_dbtp\":-1e999,\"rms_left_db\":null}", c) && c.r.integrated <= 20.0 && c.r.ch[0].truePeak >= 0.0 && !c.has(kHaveRms),
          "figures out of all proportion are brought within reason", "I %.1f", c.r.integrated);
    uint32_t seed = 0xc0ffee;
    bool survived = true;
    for (int round = 0; round < 400; ++round) {
        std::string damaged = text;
        for (int k = 0; k < 1 + round % 6; ++k) {
            seed = seed * 1664525u + 1013904223u;
            const size_t at = (seed >> 4) % damaged.size();
            seed = seed * 1664525u + 1013904223u;
            if (round % 3 == 0) damaged.erase(at, 1 + (seed >> 9) % 40);
            else if (round % 3 == 1) damaged[at] = static_cast<char>(seed >> 13);
            else damaged.insert(at, std::string(1 + (seed >> 9) % 5, "{[\"-9e,:}]"[(seed >> 20) % 10]));
        }
        Analysis d;
        if (!fromJson(damaged, d)) continue;
        survived = survived && d.r.waveCount >= 0 && d.r.waveCount <= kSeries && d.r.loudCount >= 0 && d.r.loudCount <= kSeries && d.r.integrated >= kSilence && d.r.integrated <= 20.0;
        survived = survived && d.r.ch[0].truePeak >= 0.0 && d.r.ch[0].truePeak <= 100.0 && std::fabs(correlation(d.r)) <= 1.0 && d.have <= kHaveAll;
        ReportInput in;
        in.track = &d;
        survived = survived && !textReport(in).empty() && !toJson(d, view).empty();
    }
    check(survived, "400 damaged copies: each is refused or read into figures that stay within bounds", "");
}

static void reports() {
    Analysis track, ref;
    fromJson(demo::currentJson(), track);
    fromJson(demo::referenceJson(), ref);
    Analysis full = demo::measured("Demo", 1.0, 20.0);
    ReportInput in;
    in.track = &full;
    in.version = "0.1.0";
    in.targets = {-12.5, -2.0, 10.5};
    const std::string text = textReport(in), md = markdownReport(in), js = jsonReport(in);
    bool ok = text.find("Integrated") != std::string::npos && text.find("Total RMS amplitude") != std::string::npos && text.find("0 dB = FS sine wave") != std::string::npos;
    ok = ok && text.find("Mastering advisor") != std::string::npos && text.find("Required gain") != std::string::npos && text.find("Reference") == std::string::npos;
    check(ok, "the text report holds the loudness, the amplitude statistics and the advisor", "%zu characters", text.size());
    check(md.find("# Demo") == 0 && md.find("| Integrated | ") != std::string::npos && md.find("|---|---:|---:|---|") != std::string::npos && md.find("## Mastering advisor") != std::string::npos,
          "the Markdown report has them as tables", "%zu characters", md.size());
    Analysis back;
    json::Value root;
    check(json::Parser::parse(js, root) && root.get("advisor") != nullptr && std::fabs(root.get("advisor")->num("target_lufs", 0) + 12.5) < 1e-9 && fromJson(js, back) && back.r.frames == full.r.frames,
          "the JSON report is JSON, has the advisor in it, and can be read back as an analysis", "%zu bytes", js.size());
    in.track = &track;
    in.reference = &ref;
    const std::string cmp = textReport(in), cmpMd = markdownReport(in), cmpJs = jsonReport(in);
    check(cmp.find("Reference: \xC3\x80 toute allure") != std::string::npos && cmp.find("-12.56") != std::string::npos && cmp.find("-12.47") != std::string::npos && cmp.find("-0.09") != std::string::npos &&
              cmp.find("TARGET ACHIEVED") != std::string::npos,
          "with a reference: both sets of figures and the differences", "");
    check(cmpMd.find("| LUFS-I | -12.56 | -12.47 | -0.09 | LUFS |") != std::string::npos && cmpJs.find("\"difference\":-0.09") != std::string::npos, "in Markdown and in JSON too", "");
    in.view.sine = false;
    in.view.window = 4;
    in.track = &full;
    in.reference = nullptr;
    const std::string square = textReport(in);
    check(square.find("RMS window 300 ms, 0 dB = FS square wave") != std::string::npos && square != text, "the report follows the window and the reference level chosen", "");
    Analysis none;
    none.capture.status = kEmpty;
    in.track = &none;
    check(textReport(in).find("nothing measured") != std::string::npos && !markdownReport(in).empty(), "nothing measured: a report that says so", "");
    check(clock(225.24) == "3:45.2" && clock(59.96) == "1:00.0" && clock(3599.0, false) == "59:59" && clock(-1.0) == "0:00.0", "times read as minutes and seconds", "%s", clock(225.24).c_str());
}

int main() {
    loudness();
    range();
    peaks();
    amplitude();
    stereo();
    spectrum();
    series();
    consistency();
    advisor();
    referenceRows();
    jsonText();
    reports();
    std::printf("%s\n", failures == 0 ? "ALL PASS" : "FAILED");
    return failures == 0 ? 0 : 1;
}
