// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// Offline measurements of the mastering chain. Build and run with tests/run.sh.
#include <chrono>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include "../dsp/MasterChain.hpp"

using namespace hnl;

static int failures = 0;

static void check(bool ok, const char* name, const char* fmt, double a = 0, double b = 0, double c = 0) {
    std::printf("%s  %-34s ", ok ? "PASS" : "FAIL", name);
    std::printf(fmt, a, b, c);
    std::printf("\n");
    if (!ok) ++failures;
}

/** Amplitude of one frequency component over x[from, to). */
static double tone(const std::vector<float>& x, size_t from, size_t to, double f, double fs) {
    double re = 0, im = 0;
    for (size_t i = from; i < to; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2 * kPi * (i - from) / (to - from)); // Hann
        re += w * x[i] * std::cos(2 * kPi * f * i / fs);
        im += w * x[i] * std::sin(2 * kPi * f * i / fs);
    }
    return 2.0 * std::sqrt(re * re + im * im) / (0.5 * (to - from));
}

/** Independent true-peak measurement: 16x windowed-sinc interpolation. */
static double truePeak(const std::vector<float>& x, size_t from = 0, size_t to = ~size_t(0)) {
    const int half = 48, os = 16;
    std::vector<double> k((2 * half * os) + 1);
    for (size_t i = 0; i < k.size(); ++i) {
        const double t = (static_cast<double>(i) - half * os) / os;
        k[i] = (t == 0 ? 1.0 : std::sin(kPi * t) / (kPi * t)) * kaiser(static_cast<int>(i), static_cast<int>(k.size()), 10.0);
    }
    double peak = 0;
    from = std::max<size_t>(from, half);
    to = std::min(to, x.size() - half);
    for (size_t n = from; n < to; ++n)
        for (int p = 0; p < os; ++p) {
            double s = 0;
            for (int j = -half + 1; j <= half; ++j) s += x[n + j] * k[(half - j) * os + p];
            peak = std::max(peak, std::fabs(s));
        }
    return peak;
}

static Params allOff() {
    Params p;
    p.compOn = p.exciterOn = p.limiterOn = false;
    p.eqOn = p.widenerOn = p.reverbOn = false;
    return p;
}

static void run(MasterChain& c, std::vector<float>& l, std::vector<float>& r) {
    const int block = 256;
    for (size_t i = 0; i < l.size(); i += block)
        c.process(&l[i], &r[i], static_cast<int>(std::min<size_t>(block, l.size() - i)));
}

static std::vector<float> sine(double f, double amp, double fs, double seconds, double phase = 0) {
    std::vector<float> x(static_cast<size_t>(seconds * fs));
    for (size_t i = 0; i < x.size(); ++i) x[i] = static_cast<float>(amp * std::sin(2 * kPi * f * i / fs + phase));
    return x;
}

static void testCrossover(double fs) {
    double worst = 0;
    for (double f : {30.0, 80.0, 120.0, 200.0, 700.0, 1800.0, 2500.0, 4000.0, 9000.0, 16000.0}) {
        Crossover3 xo;
        xo.setFreqs(fs, 120, 2500);
        auto x = sine(f, 0.5, fs, 1.0);
        for (auto& v : x) { double a, b, c; xo.split(v, a, b, c); v = static_cast<float>(a + b + c); }
        worst = std::max(worst, std::fabs(linToDb(tone(x, x.size() / 2, x.size(), f, fs) / 0.5)));
    }
    check(worst < 0.01, "crossover sums flat", "worst deviation %.4f dB", worst);
}

static void testBypassNull(double fs) {
    MasterChain c;
    c.prepare(fs);
    c.setParams(allOff());
    std::mt19937 rng(1);
    std::uniform_real_distribution<float> d(-0.5f, 0.5f);
    std::vector<float> l(20000), r(20000);
    for (size_t i = 0; i < l.size(); ++i) { l[i] = d(rng); r[i] = d(rng); }
    const auto il = l, ir = r;
    run(c, l, r);
    const int lat = c.latency();
    double err = 0;
    for (size_t i = 0; i + lat < l.size(); ++i)
        err = std::max<double>(err, std::max(std::fabs(l[i + lat] - il[i]), std::fabs(r[i + lat] - ir[i])));
    check(err < 1e-6, "all-off chain is a pure delay", "latency %.0f samples, max error %.2g", lat, err);

    // Limiter on but never reached: still a pure delay.
    MasterChain c2;
    c2.prepare(fs);
    Params p = allOff();
    p.limiterOn = true;
    c2.setParams(p);
    l = il; r = ir;
    run(c2, l, r);
    err = 0;
    for (size_t i = 0; i + lat < l.size(); ++i) err = std::max<double>(err, std::fabs(l[i + lat] - il[i]));
    check(err < 1e-6, "idle limiter is transparent", "max error %.2g", err);
}

static void testCompressor(double fs) {
    MasterChain c;
    c.prepare(fs);
    Params p = allOff();
    p.compOn = true;
    p.band[1] = {-20.0, 4.0, 5.0, 500.0, 0.0};
    c.setParams(p);
    auto l = sine(1000, dbToLin(-8), fs, 3.0), r = l;
    run(c, l, r);
    const double out = linToDb(tone(l, l.size() / 2, l.size(), 1000, fs));
    check(std::fabs(out - (-17.0)) < 1.0, "compressor 4:1 static gain", "out %.2f dBFS (ideal %.1f)", out, -17.0);
    check(c.meters.compGr[1].load() < -7.0, "gain reduction meter", "mid band GR %.2f dB", c.meters.compGr[1].load());
}

static void testExciterMode(double fs, int mode, const char* name) {
    Params p = allOff();
    p.exciterOn = true;
    p.exciterMode = mode;
    p.exciterFreq = 3000;
    p.exciterDrive = 18;
    p.exciterColour = 0.5;
    p.exciterMix = 100;
    {
        MasterChain c;
        c.prepare(fs);
        c.setParams(p);
        auto l = sine(4000, dbToLin(-12), fs, 2.0), r = l;
        run(c, l, r);
        const double h2 = linToDb(tone(l, l.size() / 2, l.size(), 8000, fs));
        const double h3 = linToDb(tone(l, l.size() / 2, l.size(), 12000, fs));
        check(h2 > -60 && h3 > -60, (std::string(name) + ": adds harmonics").c_str(), "2nd %.1f dBFS, 3rd %.1f dBFS", h2, h3);
    }
    {
        MasterChain c;
        c.prepare(fs);
        c.setParams(p);
        // Harmonics 3, 4 and 5 lie above Nyquist; without oversampling they
        // would fold back to 0.37, 0.16 and 0.05 times the sample rate.
        const double f = 0.21 * fs;
        auto l = sine(f, dbToLin(-6), fs, 2.0), r = l;
        run(c, l, r);
        double worst = -200;
        for (double a : {0.37, 0.16, 0.05})
            worst = std::max(worst, linToDb(tone(l, l.size() / 2, l.size(), a * fs, fs)));
        check(worst < -80, (std::string(name) + ": aliasing suppressed").c_str(), "strongest folded harmonic %.1f dBFS", worst);
    }
}

static void testExciter(double fs) {
    testExciterMode(fs, Exciter::kTape, "exciter tape");
    testExciterMode(fs, Exciter::kTube, "exciter tube");
    testExciterMode(fs, Exciter::kRetro, "exciter retro");
    {
        // The generated signal must line up with the delayed dry signal.
        Exciter e;
        e.setFreq(fs, 10.0);
        e.setDrive(0.0, 0.0);
        int at = -1;
        double best = 0;
        for (int i = 0; i < 200; ++i) {
            const double x = i == 0 ? 1e-3 : 0.0;
            const double wet = e.process(0, x, 1.0, true) - (i == Exciter::latency() ? 1e-3 : 0.0);
            if (std::fabs(wet) > best) { best = std::fabs(wet); at = i; }
        }
        check(std::abs(at - Exciter::latency()) <= 1, "exciter path aligned with dry", "wet peak at %.0f, dry delay %.0f", at, Exciter::latency());
    }
}

/** Gain in dB of a full chain with only `p` applied, for a sine at f. */
static double gainAt(double fs, const Params& p, double f) {
    MasterChain c;
    c.prepare(fs);
    c.setParams(p);
    auto l = sine(f, 0.25, fs, 1.0), r = l;
    run(c, l, r);
    return linToDb(tone(l, l.size() / 2, l.size(), f, fs) / 0.25);
}

static void testEq(double fs) {
    Params p = allOff();
    p.eqOn = true;
    {
        MasterChain c;
        c.prepare(fs);
        c.setParams(p);
        std::mt19937 rng(5);
        std::uniform_real_distribution<float> d(-0.5f, 0.5f);
        std::vector<float> l(20000), r(20000);
        for (size_t i = 0; i < l.size(); ++i) { l[i] = d(rng); r[i] = d(rng); }
        const auto il = l;
        run(c, l, r);
        double err = 0;
        for (size_t i = 0; i + c.latency() < l.size(); ++i) err = std::max<double>(err, std::fabs(l[i + c.latency()] - il[i]));
        check(err < 1e-6, "flat equaliser is transparent", "max error %.2g", err);
    }
    Params lo = p;
    lo.eqLowFreq = 100; lo.eqLowGain = 6;
    const double l1 = gainAt(fs, lo, 25), l2 = gainAt(fs, lo, 100), l3 = gainAt(fs, lo, 4000);
    check(std::fabs(l1 - 6) < 0.4 && std::fabs(l2 - 3) < 0.4 && std::fabs(l3) < 0.1, "low shelf +6 dB at 100 Hz",
          "25 Hz %+.2f, 4 kHz %+.2f dB", l1, l3);
    Params mid = p;
    mid.eqMidFreq = 1000; mid.eqMidGain = -6; mid.eqMidQ = 2;
    const double m1 = gainAt(fs, mid, 1000), m2 = gainAt(fs, mid, 100), m3 = gainAt(fs, mid, 10000);
    check(std::fabs(m1 + 6) < 0.1 && std::fabs(m2) < 0.2 && std::fabs(m3) < 0.2, "peak -6 dB at 1 kHz",
          "1 kHz %+.2f, 100 Hz %+.2f dB", m1, m2);
    Params hi = p;
    hi.eqHighFreq = 6000; hi.eqHighGain = 4;
    const double h1 = gainAt(fs, hi, 19000), h2 = gainAt(fs, hi, 300);
    check(std::fabs(h1 - 4) < 0.4 && std::fabs(h2) < 0.1, "high shelf +4 dB at 6 kHz", "19 kHz %+.2f, 300 Hz %+.2f dB", h1, h2);
}

static void testWidener(double fs) {
    auto sideGain = [&](double width, double f, double* monoErr) {
        MasterChain c;
        c.prepare(fs);
        Params p = allOff();
        p.widenerOn = true;
        p.width = width;
        p.widenerFreq = 200;
        c.setParams(p);
        auto l = sine(f, 0.4, fs, 1.0);
        std::vector<float> r(l.size(), 0.0f); // hard left: mid and side are both 0.2
        run(c, l, r);
        std::vector<float> m(l.size()), s(l.size());
        for (size_t i = 0; i < l.size(); ++i) { m[i] = 0.5f * (l[i] + r[i]); s[i] = 0.5f * (l[i] - r[i]); }
        if (monoErr) *monoErr = std::fabs(linToDb(tone(m, m.size() / 2, m.size(), f, fs) / 0.2));
        return linToDb(tone(s, s.size() / 2, s.size(), f, fs) / 0.2);
    };
    double monoErr = 0;
    const double wide = sideGain(200, 2000, &monoErr), bass = sideGain(200, 40, nullptr);
    check(std::fabs(wide - 6) < 0.3 && std::fabs(bass) < 0.5, "widener 200%", "side %+.2f dB at 2 kHz, %+.2f dB at 40 Hz", wide, bass);
    check(monoErr < 0.001, "widener leaves mono sum alone", "mid changed by %.4f dB", monoErr);
    const double narrow = sideGain(50, 2000, nullptr), mono = sideGain(0, 2000, nullptr);
    check(std::fabs(narrow + 6) < 0.1 && mono < -100, "widener narrows to mono", "50%%: %+.2f dB, 0%%: %.0f dB", narrow, mono);
}

static void testReverb(double fs) {
    Params p = allOff();
    p.reverbOn = true;
    p.reverbDecay = 1.5;
    p.reverbDamping = 16000;
    p.reverbMix = 100;
    auto energyDb = [&](const std::vector<float>& x, double from, double len) {
        double e = 0;
        const size_t a = static_cast<size_t>(from * fs), n = static_cast<size_t>(len * fs);
        for (size_t i = a; i < a + n; ++i) e += static_cast<double>(x[i]) * x[i];
        return 10 * std::log10(e / n + 1e-30);
    };
    {
        // Noise burst of 0.5 s, then silence: the tail must decay at the set rate.
        MasterChain c;
        c.prepare(fs);
        c.setParams(p);
        std::mt19937 rng(11);
        std::uniform_real_distribution<float> d(-0.5f, 0.5f);
        std::vector<float> l(static_cast<size_t>(20 * fs), 0.0f), r(l.size(), 0.0f);
        for (size_t i = 0; i < static_cast<size_t>(0.5 * fs); ++i) { l[i] = d(rng); r[i] = d(rng); }
        const double dry = energyDb(l, 0.1, 0.3);
        auto il = l;
        run(c, l, r);
        std::vector<float> wet(l.size(), 0.0f);
        for (size_t i = c.latency(); i < l.size(); ++i) wet[i] = l[i] - il[i - c.latency()];
        const double level = energyDb(wet, 0.25, 0.25) - dry;
        const double drop = energyDb(l, 0.7, 0.1) - energyDb(l, 1.45, 0.1);
        check(std::fabs(drop - 30) < 4, "reverb decay time", "%.1f dB in 0.75 s (ideal 30 for 1.5 s)", drop);
        check(level > -9 && level < 0, "reverb level at 100%", "%.1f dB relative to the dry signal", level);
        const double tail = energyDb(l, 18, 1);
        bool finite = true;
        for (float v : l) finite = finite && std::isfinite(v);
        check(finite && tail < -150, "reverb tail dies out", "%.0f dB after 18 s", tail);
        double corr = 0, el = 0, er = 0;
        for (size_t i = static_cast<size_t>(0.6 * fs); i < static_cast<size_t>(1.2 * fs); ++i) {
            corr += static_cast<double>(l[i]) * r[i]; el += static_cast<double>(l[i]) * l[i]; er += static_cast<double>(r[i]) * r[i];
        }
        check(std::fabs(corr / std::sqrt(el * er)) < 0.3, "reverb tail is stereo", "left/right correlation %.2f", corr / std::sqrt(el * er));
    }
    {
        // Damping: the top of the tail must fall faster than the middle.
        Params q = p;
        q.reverbDamping = 2000;
        auto tailDb = [&](double f) {
            MasterChain c;
            c.prepare(fs);
            c.setParams(q);
            auto l = sine(f, 0.25, fs, 2.0), r = l;
            for (size_t i = static_cast<size_t>(0.5 * fs); i < l.size(); ++i) l[i] = r[i] = 0.0f;
            run(c, l, r);
            return energyDb(l, 0.6, 0.1) - energyDb(l, 1.0, 0.1);
        };
        const double mid = tailDb(700), top = tailDb(9000);
        check(top > mid + 6, "reverb damping", "0.4 s drop: %.1f dB at 700 Hz, %.1f dB at 9 kHz", mid, top);
    }
}

static double maxCurve(const std::vector<float>& x, size_t from, size_t to) {
    double m = 0;
    for (size_t i = std::max<size_t>(from, 2); i < to; ++i) m = std::max(m, std::fabs(static_cast<double>(x[i]) - 2.0 * x[i - 1] + x[i - 2]));
    return m;
}

static double runLimiter(double fs, bool whiteNoise, float* deepestGr = nullptr, double attack = 3.0, bool soft = false) {
    MasterChain c;
    c.prepare(fs);
    Params p = allOff();
    p.limiterOn = true;
    p.limiterGain = 12;
    p.limiterCeiling = -1.0;
    p.limiterRelease = 100;
    p.limiterAttack = attack;
    p.limiterSoft = soft;
    c.setParams(p);

    const size_t n = static_cast<size_t>(4 * fs);
    std::vector<float> l(n), r(n);
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> d(-1.f, 1.f);
    Biquad lp[2];
    for (auto& f : lp) f.set(designLowpass(fs, 0.3 * fs, 0.7071));
    for (size_t i = 0; i < n; ++i) {
        const double t = i / fs;
        double v = 0.4 * std::sin(2 * kPi * 55 * t) + 0.3 * std::sin(2 * kPi * 440 * t) + 0.2 * std::sin(2 * kPi * 3100 * t);
        if (t > 1.0 && t < 1.5) v = 0.9 * std::sin(2 * kPi * (fs / 4) * t + kPi / 4); // worst case inter-sample overs
        if (t > 2.0 && t < 2.5) v = std::sin(2 * kPi * 110 * t) > 0 ? 0.9 : -0.9;      // square wave
        if (t > 3.0 && t < 3.5) v = 0.9 * d(rng);                                      // noise burst
        if (std::fmod(t, 0.25) < 0.002) v += 0.8;                                      // transients
        if (whiteNoise) v = 0.9 * d(rng);
        else if (t > 3.0 && t < 3.5) v = 1.5 * lp[1].process(lp[0].process(v));
        l[i] = static_cast<float>(v);
        r[i] = static_cast<float>(0.7 * v);
    }
    run(c, l, r);
    if (deepestGr) *deepestGr = c.meters.limGr.load();
    return linToDb(truePeak(l));
}

static void testLimiter(double fs) {
    float gr = 0;
    const double tp = runLimiter(fs, false, &gr);
    check(tp <= -1.0 + 0.1, "limiter holds -1 dBTP ceiling", "true peak %.3f dBTP (16x reference meter)", tp);
    check(gr < -6.0, "limiter GR meter", "deepest %.2f dB", gr);
    // Full-scale noise up to Nyquist is beyond what any finite detector can
    // predict exactly; it must still stay close.
    const double wn = runLimiter(fs, true);
    check(wn <= -1.0 + 1.0, "limiter worst case (white noise)", "true peak %.3f dBTP", wn);
    // Every attack of the brickwall holds the ceiling, the shortest and the longest too. What is asked
    // beyond the longest look-ahead counts as the longest.
    double worst = -100;
    for (double attack : {0.5, 1.0, 5.0, 50.0}) worst = std::max(worst, runLimiter(fs, false, nullptr, attack));
    check(worst <= -1.0 + 0.1, "brickwall holds at every attack", "worst true peak %.3f dBTP", worst);
}

/** Level of a 1 kHz tone 12 dB over the ceiling: [0] the highest peak of its first 3 ms, [1] once settled. */
static void toneBurst(double fs, bool soft, double attack, double* out) {
    MasterChain c;
    c.prepare(fs);
    Params p = allOff();
    p.limiterOn = true;
    p.limiterCeiling = -12.0;
    p.limiterRelease = 200;
    p.limiterAttack = attack;
    p.limiterSoft = soft;
    c.setParams(p);
    const size_t n = static_cast<size_t>(fs), at = n / 4;
    std::vector<float> l(n, 0.0f), r(n, 0.0f);
    for (size_t i = at; i < n; ++i) l[i] = r[i] = static_cast<float>(std::sin(2 * kPi * 1000 * (i - at) / fs));
    run(c, l, r);
    const size_t start = at + c.latency();
    out[0] = out[1] = 0;
    for (size_t i = start; i < start + static_cast<size_t>(0.003 * fs); ++i) out[0] = std::max(out[0], std::fabs(static_cast<double>(l[i])));
    for (size_t i = n - n / 4; i < n; ++i) out[1] = std::max(out[1], std::fabs(static_cast<double>(l[i])));
    out[0] = linToDb(out[0]);
    out[1] = linToDb(out[1]);
}

static void testLimiterSoft(double fs) {
    double hard[2], fast[2], slow[2];
    toneBurst(fs, false, 3.0, hard);
    toneBurst(fs, true, 1.0, fast);
    toneBurst(fs, true, 20.0, slow);
    check(hard[0] <= -12.0 + 0.01, "brickwall: the start of a tone is held", "%.2f dB at a ceiling of -12", hard[0]);
    check(fast[0] > -6.0 && slow[0] > fast[0] - 0.01 && slow[0] > -1.0, "soft: the start of a tone passes",
          "%.2f dB at 1 ms attack, %.2f dB at 20 ms", fast[0], slow[0]);
    check(std::fabs(fast[1] + 12.0) < 0.3 && std::fabs(slow[1] + 12.0) < 0.3, "soft: a held tone comes down to the ceiling",
          "%.2f dB and %.2f dB", fast[1], slow[1]);

    {
        // However hard it is driven, soft lets nothing through at more than 12 dB over the ceiling.
        float gr = 0;
        const double tp = runLimiter(fs, false, &gr, 50.0, true);
        MasterChain c;
        c.prepare(fs);
        Params p = allOff();
        p.limiterOn = p.limiterSoft = true;
        p.limiterGain = 24;
        p.limiterAttack = 50;
        p.inputGain = 12;
        c.setParams(p);
        std::vector<float> l = sine(1000, 1.0, fs, 0.5), r = l;
        run(c, l, r);
        double peak = 0;
        for (float v : l) peak = std::max(peak, std::fabs(static_cast<double>(v)));
        check(tp > -1.0 + 0.5 && linToDb(peak) <= -1.0 + 12.01, "soft passes the ceiling, by 12 dB at most",
              "%.2f dBTP on the test signal, %.2f dB driven 36 dB over", tp, linToDb(peak));
    }

    // The latency is the same in both modes and at every attack: what is under the ceiling passes as a pure delay.
    for (int soft = 0; soft < 2; ++soft) {
        MasterChain c;
        c.prepare(fs);
        Params p = allOff();
        p.limiterOn = true;
        p.limiterSoft = soft == 1;
        p.limiterAttack = soft ? 30.0 : 1.0;
        c.setParams(p);
        std::mt19937 rng(9);
        std::uniform_real_distribution<float> d(-0.2f, 0.2f); // true peaks well under the ceiling
        std::vector<float> l(20000), r(20000);
        for (size_t i = 0; i < l.size(); ++i) { l[i] = d(rng); r[i] = d(rng); }
        const auto il = l;
        run(c, l, r);
        double err = 0;
        for (size_t i = 0; i + c.latency() < l.size(); ++i) err = std::max<double>(err, std::fabs(l[i + c.latency()] - il[i]));
        check(err < 1e-6, soft ? "soft, idle: a pure delay of the same length" : "brickwall at 1 ms, idle: the same delay", "max error %.2g", err);
    }

    // Turning the attack and changing the mode under a loud tone: the brickwall holds its ceiling all the
    // while, and nothing clicks (no bend in the wave far sharper than a limited tone has of itself).
    {
        MasterChain c;
        c.prepare(fs);
        Params p = allOff();
        p.limiterOn = true;
        p.limiterCeiling = -6.0;
        p.limiterGain = 6.0;
        c.setParams(p);
        const size_t n = static_cast<size_t>(3 * fs);
        std::vector<float> l = sine(220, 0.9, fs, 3.0), r = l;
        double peakBrick = 0, bend = 0, bend2 = 0, own = 0;
        size_t block = 0;
        for (size_t i = 0; i < n; i += 256, ++block) {
            const bool turning = i > n / 3 && i < 2 * n / 3, softNow = i >= 2 * n / 3 && block % 40 < 20;
            if (turning) p.limiterAttack = 0.5 * std::pow(10.0, std::fmod(block * 0.037, 1.0)); // 0.5 to 5 ms, again and again
            p.limiterSoft = softNow;
            c.setParams(p);
            const size_t len = std::min<size_t>(256, n - i);
            c.process(&l[i], &r[i], static_cast<int>(len));
            if (i < n / 3) own = std::max(own, maxCurve(l, i, i + len));
            else if (i < 2 * n / 3) bend = std::max(bend, maxCurve(l, i, i + len));
            else bend2 = std::max(bend2, maxCurve(l, i, i + len));
            if (!softNow && i < 2 * n / 3) for (size_t k = i; k < i + len; ++k) peakBrick = std::max(peakBrick, std::fabs(static_cast<double>(l[k])));
        }
        check(linToDb(peakBrick) <= -6.0 + 0.01, "brickwall holds while the attack is turned", "peak %.3f dB at a ceiling of -6", linToDb(peakBrick));
        check(bend < 4.0 * own, "the attack is turned without a click", "bend %.2e (the limited tone itself: %.2e)", bend, own);
        check(bend2 < 4.0 * own, "the mode changes without a click", "bend %.2e (the limited tone itself: %.2e)", bend2, own);
    }
}

static void testLoudness(double fs) {
    MasterChain c;
    c.prepare(fs);
    c.setParams(allOff());
    auto l = sine(997, dbToLin(-23), fs, 6.0), r = l;
    run(c, l, r);
    const double m = c.meters.lufsMomentary.load(), s = c.meters.lufsShort.load(), in = c.meters.lufsIntegrated.load();
    check(std::fabs(m + 23) < 0.1 && std::fabs(s + 23) < 0.1, "loudness: -23 dBFS sine", "M %.2f, S %.2f LUFS", m, s);
    check(std::fabs(in + 23) < 0.1, "integrated loudness", "I %.2f LUFS", in);
}

static void testSpeed(double fs) {
    MasterChain c;
    c.prepare(fs);
    Params p;
    p.limiterGain = 6;
    p.eqOn = p.widenerOn = p.reverbOn = true;
    p.eqLowGain = 2; p.eqMidGain = -2; p.eqHighGain = 2;
    c.setParams(p);
    std::mt19937 rng(3);
    std::uniform_real_distribution<float> d(-0.5f, 0.5f);
    std::vector<float> l(static_cast<size_t>(20 * fs)), r(l.size());
    for (size_t i = 0; i < l.size(); ++i) { l[i] = d(rng); r[i] = d(rng); }
    const auto t0 = std::chrono::steady_clock::now();
    run(c, l, r);
    const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    check(sec < 4.0, "speed (everything on, stereo)", "%.0fx realtime (%.1f%% of one core)", 20.0 / sec, 100.0 * sec / 20.0);
}

/** A stage switched on again must not let out what was in its filters when it was switched off. */
static void testStagesStartClean(double fs) {
    static const char* const kNames[2] = {"compressor back on: nothing left in its filters", "exciter back on: nothing left in its filters"};
    for (int stage = 0; stage < 2; ++stage) {
        MasterChain c;
        c.prepare(fs);
        Params p = allOff();
        bool& on = stage == 0 ? p.compOn : p.exciterOn;
        on = true;
        p.exciterMix = 100.0;
        c.setParams(p);
        c.reset();
        const size_t n = static_cast<size_t>(fs);
        std::mt19937 rng(7);
        std::uniform_real_distribution<float> dist(-0.5f, 0.5f);
        std::vector<float> l(n / 2), r(n / 2);
        for (size_t i = 0; i < l.size(); ++i) { l[i] = dist(rng); r[i] = dist(rng); }
        run(c, l, r);
        on = false; // off in the middle of the noise, then a second of silence
        c.setParams(p);
        l.assign(n, 0.0f);
        r.assign(n, 0.0f);
        run(c, l, r);
        double before = 0;
        for (size_t i = n / 2; i < n; ++i) before = std::max(before, static_cast<double>(std::max(std::fabs(l[i]), std::fabs(r[i]))));
        on = true;
        c.setParams(p);
        l.assign(n / 4, 0.0f);
        r.assign(n / 4, 0.0f);
        run(c, l, r);
        double peak = 0;
        for (size_t i = 0; i < l.size(); ++i) peak = std::max(peak, static_cast<double>(std::max(std::fabs(l[i]), std::fabs(r[i]))));
        check(before < 1e-6 && peak < 1e-6, kNames[stage], "peak %.2e (silent before: %.2e)", peak, before);
    }
}

static void testEqPosition(double fs) {
    Params p = allOff();
    p.compOn = p.eqOn = true;
    p.eqLowFreq = 200.0;
    p.eqLowGain = 12.0;
    for (auto& b : p.band) { b.threshold = -30.0; b.ratio = 4.0; }
    const size_t n = static_cast<size_t>(fs);

    // The order is heard: a boost before the compressor is held back by it.
    double level[2];
    for (int first = 0; first < 2; ++first) {
        MasterChain c;
        c.prepare(fs);
        p.eqFirst = first == 1;
        c.setParams(p);
        c.reset();
        std::vector<float> l = sine(60, 0.1, fs, 2.0), r = l;
        run(c, l, r);
        level[first] = 20.0 * std::log10(tone(l, n, 2 * n, 60, fs));
    }
    check(level[0] - level[1] > 4.0, "eq before compressor: boost held", "%.1f dB after it, %.1f dB before it", level[0], level[1]);

    // Changing place under a tone: a short dip, no click. A click shows as a bend in the wave far
    // sharper than the tone's own (the second difference of the samples).
    {
        MasterChain c;
        c.prepare(fs);
        p.eqFirst = false;
        c.setParams(p);
        c.reset();
        std::vector<float> l = sine(200, 0.1, fs, 3.0), r = l;
        for (size_t i = 0; i < l.size(); i += 256) {
            if (i == 4 * 256 * (n / 1024)) { p.eqFirst = true; c.setParams(p); } // about a second in
            c.process(&l[i], &r[i], static_cast<int>(std::min<size_t>(256, l.size() - i)));
        }
        const size_t at = 4 * 256 * (n / 1024), ms = n / 1000;
        const double steady = std::max(maxCurve(l, at - 100 * ms, at), maxCurve(l, 2 * n, 3 * n));
        const double change = maxCurve(l, at, at + 100 * ms);
        double floor = 1.0, before = 0.0;
        for (size_t i = at; i < at + 20 * ms; ++i) floor = std::min(floor, static_cast<double>(std::fabs(l[i]) + std::fabs(l[i + ms / 2 + 1])));
        for (size_t i = at - 20 * ms; i < at; ++i) before = std::max(before, static_cast<double>(std::fabs(l[i])));
        check(change <= 1.5 * steady, "eq changes place without a click", "bend %.2e (the tone itself: %.2e)", change, steady);
        check(floor < 0.05 * before, "eq changes place in a dip", "down to %.4f of %.4f", floor, before);
        const double after = tone(l, 2 * n, 3 * n, 200, fs);
        check(after > 0.5 * before && after < 2.0 * before, "the level comes back after it", "%.4f (before: %.4f)", after, before);
    }

    // With the compressor off the place makes no difference, and nothing is heard of the change.
    {
        p.compOn = false;
        std::vector<float> out[2];
        for (int flip = 0; flip < 2; ++flip) {
            MasterChain c;
            c.prepare(fs);
            p.eqFirst = false;
            c.setParams(p);
            c.reset();
            std::vector<float> l = sine(200, 0.1, fs, 1.0), r = l;
            for (size_t i = 0; i < l.size(); i += 256) {
                if (flip && i == 256 * 40) { p.eqFirst = true; c.setParams(p); }
                c.process(&l[i], &r[i], static_cast<int>(std::min<size_t>(256, l.size() - i)));
            }
            out[flip] = l;
        }
        double diff = 0;
        for (size_t i = 0; i < out[0].size(); ++i) diff = std::max(diff, static_cast<double>(std::fabs(out[0][i] - out[1][i])));
        check(diff < 1e-9, "compressor off: no dip for it", "difference %.2e", diff);
    }
}

static std::vector<float> noise(size_t n, float amp, unsigned seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> d(-amp, amp);
    std::vector<float> x(n);
    for (auto& v : x) v = d(rng);
    return x;
}

static double rmsDb(const std::vector<float>& x, size_t from, size_t to) {
    double e = 0;
    for (size_t i = from; i < to; ++i) e += static_cast<double>(x[i]) * x[i];
    return 10 * std::log10(e / (to - from) + 1e-30);
}

static void testSpectrum(double fs) {
    MasterChain c;
    c.prepare(fs);
    c.setParams(allOff());
    auto l = sine(1000, dbToLin(-12), fs, 1.0), r = l;
    run(c, l, r);
    for (auto& b : c.meters.spectrum) b.store(-100.0f); // the meter holds its highest: without the moment the tone began
    l = sine(1000, dbToLin(-12), fs, 1.0); r = l; // a whole number of periods, so it goes on where it stopped
    run(c, l, r);
    int at = 0;
    while (Spectrum::edge(at + 1) <= 1000.0) ++at;
    const double tone = c.meters.spectrum[at].load(), far = c.meters.spectrum[at + 8].load(), low = c.meters.spectrum[2].load();
    check(std::fabs(tone + 12.0) < 1.0 && far < -60.0 && low < -60.0, "spectrum: a tone reads its level in its band",
          "%.2f dB at 1 kHz, %.1f dB two octaves up", tone, far);
    // A tone at the very bottom, where a band is narrower than what the transform tells apart, is still found.
    MasterChain c2;
    c2.prepare(fs);
    c2.setParams(allOff());
    l = sine(31, dbToLin(-12), fs, 1.0); r = l;
    run(c2, l, r);
    at = 0;
    while (Spectrum::edge(at + 1) <= 31.0) ++at;
    check(std::fabs(c2.meters.spectrum[at].load() + 12.0) < 2.0, "spectrum: and at the bottom too", "%.2f dB at 31 Hz", c2.meters.spectrum[at].load());
}

static void testLowCutAndMidSide(double fs) {
    Params p = allOff();
    p.eqOn = true;
    p.eqLowCut = 40;
    const double a = gainAt(fs, p, 40), b = gainAt(fs, p, 20), c = gainAt(fs, p, 1000);
    check(std::fabs(a + 3) < 0.3 && std::fabs(b + 12.3) < 0.7 && std::fabs(c) < 0.05, "low cut at 40 Hz, 12 dB per octave",
          "40 Hz %+.2f, 20 Hz %+.2f dB", a, b);

    // A peak of +6 dB at 1 kHz on the mid part, then on the side part, of a sound that is hard left (mid and side both 0.2).
    double gain[3][2];
    for (int mode = 0; mode < 3; ++mode) {
        MasterChain ch;
        ch.prepare(fs);
        Params q = allOff();
        q.eqOn = true;
        q.eqMode = mode;
        q.eqMidFreq = 1000; q.eqMidGain = 6; q.eqMidQ = 1;
        ch.setParams(q);
        ch.reset();
        auto l = sine(1000, 0.4, fs, 1.0);
        std::vector<float> r(l.size(), 0.0f), m(l.size()), s(l.size());
        run(ch, l, r);
        for (size_t i = 0; i < l.size(); ++i) { m[i] = 0.5f * (l[i] + r[i]); s[i] = 0.5f * (l[i] - r[i]); }
        gain[mode][0] = linToDb(tone(m, m.size() / 2, m.size(), 1000, fs) / 0.2);
        gain[mode][1] = linToDb(tone(s, s.size() / 2, s.size(), 1000, fs) / 0.2);
    }
    check(std::fabs(gain[0][0] - 6) < 0.1 && std::fabs(gain[0][1] - 6) < 0.1, "equaliser on left and right", "mid %+.2f, side %+.2f dB", gain[0][0], gain[0][1]);
    check(std::fabs(gain[1][0] - 6) < 0.1 && std::fabs(gain[1][1]) < 0.01, "equaliser on the mid part only", "mid %+.2f, side %+.2f dB", gain[1][0], gain[1][1]);
    check(std::fabs(gain[2][0]) < 0.01 && std::fabs(gain[2][1] - 6) < 0.1, "equaliser on the side part only", "mid %+.2f, side %+.2f dB", gain[2][0], gain[2][1]);

    // Changing what it works on under a tone: the same dip as for its place, no click.
    {
        MasterChain ch;
        ch.prepare(fs);
        Params q = allOff();
        q.eqOn = true;
        q.eqLowFreq = 400; q.eqLowGain = 12;
        ch.setParams(q);
        ch.reset();
        const size_t n = static_cast<size_t>(fs), at = 256 * (n / 256);
        std::vector<float> l = sine(200, 0.1, fs, 2.0), r(l.size(), 0.0f);
        for (size_t i = 0; i < l.size(); i += 256) {
            if (i == at) { q.eqMode = 2; ch.setParams(q); }
            ch.process(&l[i], &r[i], static_cast<int>(std::min<size_t>(256, l.size() - i)));
        }
        const size_t ms = n / 1000;
        const double steady = std::max(maxCurve(l, at - 100 * ms, at), maxCurve(l, at + 500 * ms, 2 * n));
        check(maxCurve(l, at, at + 100 * ms) <= 1.5 * steady, "equaliser changes to mid or side without a click", "bend %.2e (the tone itself: %.2e)",
              maxCurve(l, at, at + 100 * ms), steady);
    }
}

static void testSoloMute(double fs) {
    auto level = [&](int solo, int mute, double f) {
        MasterChain c;
        c.prepare(fs);
        Params p = allOff();
        p.compOn = true;
        for (auto& b : p.band) { b.threshold = 0; b.ratio = 1; }
        if (solo >= 0) p.bandSolo[solo] = true;
        if (mute >= 0) p.bandMute[mute] = true;
        c.setParams(p);
        c.reset();
        auto l = sine(f, 0.25, fs, 1.0), r = l;
        run(c, l, r);
        return linToDb(tone(l, l.size() / 2, l.size(), f, fs) / 0.25);
    };
    const double in = level(1, -1, 600), out = level(1, -1, 30), high = level(1, -1, 12000);
    check(std::fabs(in) < 0.5 && out < -40 && high < -40, "solo: only that band is heard", "mid soloed: 600 Hz %+.1f, 30 Hz %+.1f, 12 kHz %+.1f dB", in, out, high);
    const double gone = level(-1, 1, 600), kept = level(-1, 1, 30);
    check(gone < -30 && std::fabs(kept) < 0.5, "mute: that band is silent", "mid muted: 600 Hz %+.1f, 30 Hz %+.1f dB", gone, kept);
    check(level(1, 1, 600) < -100, "a muted band is silent, soloed or not", "%.0f dB", level(1, 1, 600));
}

static void testDither(double fs) {
    for (int bits : {16, 24}) {
        MasterChain c;
        c.prepare(fs);
        Params p = allOff();
        p.dither = bits;
        c.setParams(p);
        const double step = bits == 16 ? 1.0 / 32768 : 1.0 / 8388608, amp = 0.3 * step; // a tone smaller than one step
        std::vector<float> l(static_cast<size_t>(4 * fs)), r(l.size());
        for (size_t i = 0; i < l.size(); ++i) l[i] = r[i] = static_cast<float>(amp * std::sin(2 * kPi * 1000 * i / fs));
        run(c, l, r);
        bool whole = true, same = true;
        for (size_t i = 0; i < l.size(); ++i) {
            whole = whole && std::fabs(l[i] / step - std::round(l[i] / step)) < 1e-3;
            same = same && l[i] == r[i];
        }
        const double found = tone(l, l.size() / 4, l.size(), 1000, fs) / amp;
        const double hiss = rmsDb(l, l.size() / 4, l.size()) - linToDb(step);
        check(whole, bits == 16 ? "dither 16 bit: every sample is a whole step" : "dither 24 bit: every sample is a whole step", "%.0f bits", bits);
        check(found > 0.8 && found < 1.25 && !same, "and a tone under one step is still there", "at %.2f of its level; hiss %.1f dB re one step", found, hiss);
    }
}

static void testBypassAndMatch(double fs) {
    const size_t n = static_cast<size_t>(8 * fs);
    Params p; // the chain as it comes, driven: 12 dB into the limiter
    p.eqOn = p.widenerOn = p.reverbOn = true;
    p.limiterGain = 12;
    p.dither = 16;
    const auto il = noise(n, 0.05f, 21), ir = noise(n, 0.05f, 22);
    {
        MasterChain c;
        c.prepare(fs);
        p.bypass = true;
        c.setParams(p);
        c.reset();
        auto l = il, r = ir;
        run(c, l, r);
        double err = 0;
        for (size_t i = 0; i + c.latency() < n; ++i) err = std::max<double>(err, std::max(std::fabs(l[i + c.latency()] - il[i]), std::fabs(r[i + c.latency()] - ir[i])));
        check(err == 0.0, "bypass: the sound as it came, as late as the processed one", "max error %.2g", err);
    }
    double loud[2];
    for (int match = 0; match < 2; ++match) {
        MasterChain c;
        c.prepare(fs);
        p.bypass = false;
        p.match = match == 1;
        p.dither = 0;
        c.setParams(p);
        c.reset();
        auto l = il, r = ir;
        run(c, l, r);
        loud[match] = c.meters.lufsShort.load();
        if (match) check(std::fabs(c.meters.matchDb.load() - (loud[1] - loud[0])) < 0.3 && c.meters.matchDb.load() < -6.0, "match: the gain it shows is the one it gives", "%.2f dB", c.meters.matchDb.load());
    }
    // The loudness of the unprocessed sound, by the same meter.
    MasterChain c;
    c.prepare(fs);
    c.setParams(allOff());
    auto l = il, r = ir;
    run(c, l, r);
    const double dry = c.meters.lufsShort.load();
    check(loud[0] - dry > 6.0 && std::fabs(loud[1] - dry) < 0.3, "match: the processed sound is as loud as the unprocessed", "%.2f LUFS against %.2f (unmatched %.2f)", loud[1], dry, loud[0]);

    // Switching bypass under a tone: over in a few ms, no click.
    {
        MasterChain b;
        b.prepare(fs);
        Params q = allOff();
        q.inputGain = -6;
        b.setParams(q);
        b.reset();
        const size_t m = static_cast<size_t>(fs), at = 256 * (m / 512);
        std::vector<float> tl = sine(200, 0.4, fs, 1.0), tr = tl;
        for (size_t i = 0; i < tl.size(); i += 256) {
            if (i == at) { q.bypass = true; b.setParams(q); }
            b.process(&tl[i], &tr[i], static_cast<int>(std::min<size_t>(256, tl.size() - i)));
        }
        const double own = maxCurve(tl, m - m / 4, m);
        check(maxCurve(tl, at, at + m / 10) <= 1.5 * own, "bypass switches without a click", "bend %.2e (the tone itself: %.2e)", maxCurve(tl, at, at + m / 10), own);
    }
}

static void testCorrelation(double fs) {
    auto measure = [&](int kind) {
        MasterChain c;
        c.prepare(fs);
        c.setParams(allOff());
        const size_t n = static_cast<size_t>(2 * fs);
        auto l = noise(n, 0.3f, 31), r = kind == 0 ? l : noise(n, 0.3f, 32);
        if (kind == 1) for (size_t i = 0; i < n; ++i) r[i] = -l[i];
        run(c, l, r);
        return static_cast<double>(c.meters.correlation.load());
    };
    const double mono = measure(0), opposite = measure(1), apart = measure(2);
    check(mono > 0.99 && opposite < -0.99 && std::fabs(apart) < 0.15, "correlation of left and right", "mono %+.2f, opposite %+.2f, unrelated %+.2f", mono, opposite, apart);
}

int main() {
    for (double fs : {44100.0, 48000.0, 96000.0}) {
        std::printf("--- %.0f Hz ---\n", fs);
        testCrossover(fs);
        testBypassNull(fs);
        testCompressor(fs);
        testEq(fs);
        testExciter(fs);
        testWidener(fs);
        testReverb(fs);
        testLimiter(fs);
        testLimiterSoft(fs);
        testLoudness(fs);
        testSpeed(fs);
        testStagesStartClean(fs);
        testEqPosition(fs);
        testSpectrum(fs);
        testLowCutAndMidSide(fs);
        testSoloMute(fs);
        testDither(fs);
        testBypassAndMatch(fs);
        testCorrelation(fs);
    }
    std::printf("%s\n", failures ? "FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
