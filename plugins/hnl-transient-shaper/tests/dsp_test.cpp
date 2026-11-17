// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// Offline measurements of the transient shaper. Build and run with tests/run.sh.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../dsp/TransientShaper.hpp"

#if defined(__SSE2__) || defined(_M_X64)
#include <immintrin.h>
#endif

using namespace hnl;

static int failures = 0;

static void check(bool ok, const std::string& name, const char* fmt, double a = 0, double b = 0, double c = 0) {
    std::printf("%s  %-46s ", ok ? "PASS" : "FAIL", name.c_str());
    std::printf(fmt, a, b, c);
    std::printf("\n");
    if (!ok) ++failures;
}

static void run(TransientShaper& m, std::vector<float>& l, std::vector<float>& r) {
    const int block = 256;
    for (size_t i = 0; i < l.size(); i += block)
        m.process(&l[i], &r[i], static_cast<int>(std::min<size_t>(block, l.size() - i)));
}

/** A shaper that is set up and at rest. */
struct Shaper : TransientShaper {
    Shaper(double fs, const ShaperParams& p) {
        prepare(fs);
        setParams(p);
        reset();
    }
};

static ShaperParams settings(double attack, double sustain, double length = 20.0, int focus = kFocusFull) {
    ShaperParams p;
    p.attack = attack;
    p.sustain = sustain;
    p.length = length;
    p.focus = focus;
    return p;
}

/**
 * The gain in dB over time that the shaper gives to `in` (which must not
 * cross zero: the tests use steady and decaying levels, so the gain can be
 * read off sample by sample). Index i is the gain on input sample i.
 */
static std::vector<double> gainOf(double fs, const ShaperParams& p, const std::vector<float>& in, const std::vector<float>* rightIn = nullptr,
                                  std::vector<double>* rightGain = nullptr) {
    Shaper m(fs, p);
    std::vector<float> l = in, r = rightIn ? *rightIn : in;
    run(m, l, r);
    const size_t lat = static_cast<size_t>(m.latency());
    std::vector<double> g(in.size() - lat);
    for (size_t i = 0; i < g.size(); ++i) g[i] = linToDb(l[i + lat] / in[i]);
    if (rightGain) {
        rightGain->resize(g.size());
        for (size_t i = 0; i < g.size(); ++i) (*rightGain)[i] = linToDb(r[i + lat] / (*rightIn)[i]);
    }
    return g;
}

/** A quiet bed (dBFS) with a hit that jumps to `peak` at 0.2 s and, when decayMs > 0, dies away from there. */
static std::vector<float> hit(double fs, double bed, double peak, double seconds, double decayMs = 0.0) {
    std::vector<float> x(static_cast<size_t>(seconds * fs));
    const size_t at = static_cast<size_t>(0.2 * fs);
    for (size_t i = 0; i < x.size(); ++i) {
        double level = dbToLin(bed);
        if (i >= at) {
            const double t = (i - at) / fs * 1000.0;
            level = std::max(level, dbToLin(peak) * (decayMs > 0.0 ? std::exp(-t / decayMs) : 1.0));
        }
        x[i] = static_cast<float>(level);
    }
    return x;
}

static double maxOf(const std::vector<double>& g, double fs, double fromS, double toS) {
    double m = -1e9;
    for (size_t i = static_cast<size_t>(fromS * fs); i < static_cast<size_t>(toS * fs) && i < g.size(); ++i) m = std::max(m, g[i]);
    return m;
}
static double minOf(const std::vector<double>& g, double fs, double fromS, double toS) {
    double m = 1e9;
    for (size_t i = static_cast<size_t>(fromS * fs); i < static_cast<size_t>(toS * fs) && i < g.size(); ++i) m = std::min(m, g[i]);
    return m;
}
static double at(const std::vector<double>& g, double fs, double s) { return g[static_cast<size_t>(s * fs)]; }

static std::vector<float> sine(double f, double amp, double fs, double seconds, double startS = 0.0) {
    std::vector<float> x(static_cast<size_t>(seconds * fs), 0.0f);
    for (size_t i = static_cast<size_t>(startS * fs); i < x.size(); ++i) x[i] = static_cast<float>(amp * std::sin(2 * kPi * f * i / fs));
    return x;
}

static double peakOf(const std::vector<float>& x, double fs, double fromS, double toS) {
    double m = 0;
    for (size_t i = static_cast<size_t>(fromS * fs); i < static_cast<size_t>(toS * fs) && i < x.size(); ++i) m = std::max(m, static_cast<double>(std::fabs(x[i])));
    return m;
}

static void testNeutral(double fs) {
    Shaper m(fs, ShaperParams{});
    std::vector<float> l(4000), r(4000);
    for (size_t i = 0; i < l.size(); ++i) {
        l[i] = static_cast<float>(0.5 * std::sin(0.05 * i) * (i % 700 < 60 ? 1.0 : 0.2));
        r[i] = static_cast<float>(0.3 * std::cos(0.031 * i));
    }
    const auto inL = l, inR = r;
    run(m, l, r);
    const size_t lat = static_cast<size_t>(m.latency());
    double diff = 0;
    for (size_t i = lat; i < l.size(); ++i) diff = std::max({diff, static_cast<double>(std::fabs(l[i] - inL[i - lat])), static_cast<double>(std::fabs(r[i] - inR[i - lat]))});
    char name[64];
    std::snprintf(name, sizeof(name), "neutral at %.0f Hz: only delayed", fs);
    check(diff == 0.0 && std::fabs(lat * 1000.0 / fs - kLookaheadMs) < 0.02, name, "latency %.0f samples, largest difference %.1e", static_cast<double>(lat), diff);
}

static void testAttack(double fs) {
    char name[64];
    // A hit of 30 dB over the bed, held. Attack up: a boost at the hit that is gone when the level stays.
    const auto x = hit(fs, -40, -10, 1.0);
    const auto up = gainOf(fs, settings(100, 0), x), down = gainOf(fs, settings(-100, 0), x), half = gainOf(fs, settings(50, 0), x);
    std::snprintf(name, sizeof(name), "attack +100%% at %.0f Hz: boost at the hit", fs);
    check(std::fabs(at(up, fs, 0.2) - 11.5) < 0.6 && std::fabs(maxOf(up, fs, 0.19, 0.3) - 11.8) < 0.3, name, "%.2f dB on the first sample, %.2f dB at most", at(up, fs, 0.2), maxOf(up, fs, 0.19, 0.3));
    std::snprintf(name, sizeof(name), "attack +100%% at %.0f Hz: 63%% left after Length", fs);
    check(std::fabs(at(up, fs, 0.22) - 0.632 * kAttackRangeDb) < 0.5, name, "%.2f dB after 20 ms, expected %.2f", at(up, fs, 0.22), 0.632 * kAttackRangeDb);
    std::snprintf(name, sizeof(name), "attack +100%% at %.0f Hz: gone on a held level", fs);
    check(std::fabs(at(up, fs, 0.4)) < 0.05 && std::fabs(at(up, fs, 0.15)) < 0.05, name, "%.3f dB after 200 ms, %.3f dB before the hit", at(up, fs, 0.4), at(up, fs, 0.15));
    std::snprintf(name, sizeof(name), "attack -100%% and +50%% at %.0f Hz", fs);
    check(std::fabs(minOf(down, fs, 0.19, 0.3) + maxOf(up, fs, 0.19, 0.3)) < 0.01 && std::fabs(maxOf(half, fs, 0.19, 0.3) - 0.5 * maxOf(up, fs, 0.19, 0.3)) < 0.01, name,
          "%.2f dB and %.2f dB", minOf(down, fs, 0.19, 0.3), maxOf(half, fs, 0.19, 0.3));
}

static void testLength() {
    const double fs = 48000;
    const auto x = hit(fs, -40, -10, 1.0);
    for (double length : {5.0, 60.0}) {
        const auto g = gainOf(fs, settings(100, 0, length), x);
        char name[64];
        std::snprintf(name, sizeof(name), "length %.0f ms", length);
        check(std::fabs(at(g, fs, 0.2 + length * 0.001) - 0.632 * kAttackRangeDb) < 0.6, name, "%.2f dB left after %.0f ms, expected %.2f", at(g, fs, 0.2 + length * 0.001), length, 0.632 * kAttackRangeDb);
    }
}

static void testLevelIndependence() {
    const double fs = 48000;
    // The same 20 dB hit at two levels 30 dB apart gets the same boost.
    const auto loud = gainOf(fs, settings(100, 100), hit(fs, -26, -6, 1.0, 80.0)), quiet = gainOf(fs, settings(100, 100), hit(fs, -56, -36, 1.0, 80.0));
    double diff = 0;
    for (size_t i = static_cast<size_t>(0.15 * fs); i < loud.size(); ++i) diff = std::max(diff, std::fabs(loud[i] - quiet[i])); // after the start of the file has settled
    check(diff < 0.01, "no threshold: same shape at -6 and -36 dBFS", "largest difference %.4f dB", diff);

    // A small hit gets a smaller boost than a large one.
    const auto small = gainOf(fs, settings(100, 0), hit(fs, -16, -10, 1.0)), large = gainOf(fs, settings(100, 0), hit(fs, -40, -10, 1.0));
    const double s = maxOf(small, fs, 0.19, 0.3), l = maxOf(large, fs, 0.19, 0.3);
    check(s > 5.5 && s < 8.5 && l > 11.0, "a 6 dB hit is boosted less than a 30 dB hit", "%.2f dB against %.2f dB", s, l);
}

static void testSustain() {
    const double fs = 48000;
    // A hit that dies away in 60 ms (about 145 dB per second).
    const auto x = hit(fs, -70, -6, 1.2, 60.0);
    const auto up = gainOf(fs, settings(0, 100), x), down = gainOf(fs, settings(0, -100), x), half = gainOf(fs, settings(0, 50), x);
    check(std::fabs(maxOf(up, fs, 0.2, 0.206)) < 0.3, "sustain +100%: the hit itself is left alone", "%.2f dB in the first 6 ms", maxOf(up, fs, 0.2, 0.206));
    check(at(up, fs, 0.35) > 13.0 && at(up, fs, 0.35) < 18.01, "sustain +100%: the tail comes up", "%.2f dB after 150 ms", at(up, fs, 0.35));
    check(std::fabs(at(down, fs, 0.35) + at(up, fs, 0.35)) < 0.01 && std::fabs(at(half, fs, 0.35) - 0.5 * at(up, fs, 0.35)) < 0.01, "sustain -100% and +50%", "%.2f dB and %.2f dB", at(down, fs, 0.35), at(half, fs, 0.35));

    // A level that is held is not sustain: nothing happens.
    const auto held = gainOf(fs, settings(0, 100), hit(fs, -40, -10, 1.0));
    check(std::fabs(maxOf(held, fs, 0.25, 0.9)) < 0.05, "sustain +100%: a held level is left alone", "%.3f dB at most", maxOf(held, fs, 0.25, 0.9));

    // Once the sound has gone, the gain comes back to zero.
    check(std::fabs(at(up, fs, 1.15)) < 2.5, "sustain +100%: back near zero in silence", "%.2f dB, 0.95 s after the hit", at(up, fs, 1.15));
}

static void testLowNotes() {
    const double fs = 48000;
    // A steady low tone must not be taken for a row of hits.
    for (double f : {40.0, 100.0}) {
        Shaper m(fs, settings(100, 100));
        auto l = sine(f, 0.5, fs, 2.0), r = l;
        run(m, l, r);
        m.meters.boost.store(0.0f);
        m.meters.cut.store(0.0f);
        auto l2 = sine(f, 0.5, fs, 1.0), r2 = l2; // continues in phase: f is a whole number of Hz
        run(m, l2, r2);
        char name[64];
        std::snprintf(name, sizeof(name), "steady %.0f Hz tone, attack and sustain at 100%%", f);
        check(m.meters.boost.load() < 0.1f && m.meters.cut.load() < 0.1f, name, "gain moves by %.3f dB", std::max(m.meters.boost.load(), m.meters.cut.load()));
    }
}

static void testFocus() {
    const double fs = 48000;
    const auto measure = [&](int focus, double f) {
        Shaper m(fs, settings(100, 0, 20, focus));
        auto l = sine(f, 0.25, fs, 0.6, 0.2), r = l;
        const auto in = l;
        run(m, l, r);
        return linToDb(peakOf(l, fs, 0.2, 0.23) / peakOf(in, fs, 0.2, 0.23));
    };
    const double lowLow = measure(kFocusLow, 60), lowHigh = measure(kFocusLow, 6000), highLow = measure(kFocusHigh, 60), highHigh = measure(kFocusHigh, 6000);
    const double midMid = measure(kFocusMid, 600), midLow = measure(kFocusMid, 40), full = measure(kFocusFull, 600);
    check(lowLow > 8.0 && lowHigh > 8.0, "detector Low: both a 60 Hz and a 6 kHz burst start from silence", "%.2f and %.2f dB", lowLow, lowHigh);
    check(highHigh > 8.0 && midMid > 8.0 && full > 8.0 && midLow > 5.0 && highLow > 5.0, "every range responds to a burst out of silence", "%.2f, %.2f, %.2f dB", highHigh, midMid, full);

    // A hi-hat over a steady low note: only the High detector hears it as a hit.
    const auto boostAt = [&](int focus) {
        Shaper m(fs, settings(100, 0, 20, focus));
        auto l = sine(60, 0.5, fs, 1.0), r = l;
        const auto hat = sine(8000, 0.05, fs, 1.0, 0.6);
        for (size_t i = 0; i < l.size(); ++i) l[i] += hat[i];
        r = l;
        std::vector<float> a(l.begin(), l.begin() + 24000), ar = a, b(l.begin() + 24000, l.end()), br = b;
        run(m, a, ar);
        m.meters.boost.store(0.0f);
        run(m, b, br);
        return static_cast<double>(m.meters.boost.load());
    };
    const double heardHigh = boostAt(kFocusHigh), heardFull = boostAt(kFocusFull), heardLow = boostAt(kFocusLow);
    check(heardHigh > 9.0 && heardFull < 1.5 && heardLow < 0.3, "a quiet hat over a loud bass note", "boost %.2f dB with High, %.2f with Full, %.2f with Low", heardHigh, heardFull, heardLow);
}

static void testRest() {
    const double fs = 48000;
    {
        // Left has the hit, right a steady quiet level: both get the same gain.
        const auto left = hit(fs, -40, -10, 1.0);
        const std::vector<float> right(left.size(), static_cast<float>(dbToLin(-45)));
        std::vector<double> gr;
        const auto gl = gainOf(fs, settings(100, -50), left, &right, &gr);
        double diff = 0;
        for (size_t i = 0; i < gl.size(); ++i) diff = std::max(diff, std::fabs(gl[i] - gr[i]));
        check(diff < 0.001 && maxOf(gr, fs, 0.19, 0.3) > 11.0, "stereo link: both channels get the same gain", "largest difference %.5f dB", diff);
    }
    {
        ShaperParams p = settings(100, 100); // mix at 0%: the input, only delayed
        p.output = 6.0;
        p.mix = 0.0;
        const auto x = hit(fs, -40, -10, 1.0, 60.0);
        // Compared sample for sample: a gain worked out by dividing is only exact to the last bit
        // under strict floating point, and the plugin is not built that way.
        Shaper m(fs, p);
        std::vector<float> l = x, r = x;
        run(m, l, r);
        const size_t lat = static_cast<size_t>(m.latency());
        size_t different = 0;
        for (size_t i = 0; i + lat < x.size(); ++i) different += (l[i + lat] != x[i] || r[i + lat] != x[i]) ? 1 : 0;
        check(different == 0, "mix 0%: untouched", "%.0f samples differ from the input", static_cast<double>(different));
        p.mix = 100.0;
        p.attack = p.sustain = 0.0;
        const auto out = gainOf(fs, p, x);
        check(std::fabs(at(out, fs, 0.5) - 6.0) < 1e-6, "output +6 dB", "%.3f dB", at(out, fs, 0.5));
        p.mix = 50.0;
        p.output = 0.0;
        p.attack = 100.0;
        const auto half = gainOf(fs, p, hit(fs, -40, -10, 1.0));
        const double want = linToDb(0.5 + 0.5 * dbToLin(maxOf(gainOf(fs, settings(100, 0), hit(fs, -40, -10, 1.0)), fs, 0.19, 0.3)));
        check(std::fabs(maxOf(half, fs, 0.19, 0.3) - want) < 0.01, "mix 50%: half dry, half shaped", "%.2f dB at the hit, expected %.2f", maxOf(half, fs, 0.19, 0.3), want);
    }
    {
        Shaper m(fs, settings(100, 100)); // comes to rest, and runs fast enough
        auto l = sine(100, 0.5, fs, 1.0), r = l;
        run(m, l, r);
        std::vector<float> zl(static_cast<size_t>(30 * fs), 0.0f), zr = zl;
        run(m, zl, zr);
        m.meters.boost.store(0.0f);
        m.meters.cut.store(0.0f);
        std::vector<float> tl(256, 0.0f), tr(256, 0.0f);
        m.process(tl.data(), tr.data(), 256);
        check(m.meters.boost.load() < 1e-6f && m.meters.cut.load() == 0.0f && tl[255] == 0.0f, "comes to rest after silence", "gain %.1e dB, output %.1e", m.meters.boost.load(), tl[255]);

        auto nl = sine(220, 0.5, fs, 20.0), nr = sine(331, 0.4, fs, 20.0);
        const auto start = std::chrono::steady_clock::now();
        run(m, nl, nr);
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        check(seconds < 2.0, "speed", "20 s of audio in %.3f s (%.0fx real time)", seconds, 20.0 / seconds);
    }
}

// Made from bits and tested by bits, so that no compiler setting can reason them away.
static float fromBits(uint32_t bits) {
    float v;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}
[[maybe_unused]] static bool isNumber(float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    return (bits & 0x7f800000u) != 0x7f800000u;
}

/**
 * Numbers too small to matter must count as zero inside the engine, whatever the host has set:
 * otherwise filters ringing out into silence make the processor many times slower.
 */
static void testDenormals() {
#if defined(__SSE2__) || defined(_M_X64)
    const unsigned before = _mm_getcsr();
    _mm_setcsr(before & ~0x8040u); // as a host that sets nothing
    TransientShaper m;
    m.prepare(48000.0);
    m.reset();
    const int n = 4800;
    std::vector<float> l(n, fromBits(0x00000100u)), r(n, fromBits(0x00000100u)); // about 3.6e-43
    m.process(l.data(), r.data(), n);
    int nonzero = 0;
    for (int i = 0; i < n; ++i) nonzero += (l[i] != 0.0f || r[i] != 0.0f) ? 1 : 0;
    check(nonzero == 0, "denormal input counts as silence", "%.0f samples not zero", static_cast<double>(nonzero));
    check((_mm_getcsr() & 0x8040u) == 0u, "and the host's own setting is left as it was", "%.0f", static_cast<double>(_mm_getcsr() & 0x8040u));
    _mm_setcsr(before);
#endif
}

int main() {
    for (double fs : {44100.0, 48000.0, 96000.0}) {
        testNeutral(fs);
        testAttack(fs);
    }
    testLength();
    testLevelIndependence();
    testSustain();
    testLowNotes();
    testFocus();
    testRest();
    testDenormals();
    std::printf("%s\n", failures ? "FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
