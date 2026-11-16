// Offline measurements of the mastering chain. Build and run with tests/run.sh.
#include <chrono>
#include <cstdio>
#include <random>
#include <vector>

#include "../dsp/MasterChain.hpp"

using namespace hm;

static int failures = 0;

static void check(bool ok, const char* name, const char* fmt, double a = 0, double b = 0) {
    std::printf("%s  %-34s ", ok ? "PASS" : "FAIL", name);
    std::printf(fmt, a, b);
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

static void testExciter(double fs) {
    Params p = allOff();
    p.exciterOn = true;
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
        check(h2 > -60 && h3 > -60, "exciter adds harmonics", "2nd %.1f dBFS, 3rd %.1f dBFS", h2, h3);
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
        check(worst < -80, "exciter aliasing suppressed", "strongest folded harmonic %.1f dBFS", worst);
    }
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

static double runLimiter(double fs, bool whiteNoise, float* deepestGr = nullptr) {
    MasterChain c;
    c.prepare(fs);
    Params p = allOff();
    p.limiterOn = true;
    p.limiterGain = 12;
    p.limiterCeiling = -1.0;
    p.limiterRelease = 100;
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
    c.setParams(p);
    std::mt19937 rng(3);
    std::uniform_real_distribution<float> d(-0.5f, 0.5f);
    std::vector<float> l(static_cast<size_t>(20 * fs)), r(l.size());
    for (size_t i = 0; i < l.size(); ++i) { l[i] = d(rng); r[i] = d(rng); }
    const auto t0 = std::chrono::steady_clock::now();
    run(c, l, r);
    const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    check(sec < 4.0, "speed (full chain, stereo)", "%.0fx realtime (%.1f%% of one core)", 20.0 / sec, 100.0 * sec / 20.0);
}

int main() {
    for (double fs : {44100.0, 48000.0, 96000.0}) {
        std::printf("--- %.0f Hz ---\n", fs);
        testCrossover(fs);
        testBypassNull(fs);
        testCompressor(fs);
        testExciter(fs);
        testLimiter(fs);
        testLoudness(fs);
        testSpeed(fs);
    }
    std::printf("%s\n", failures ? "FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
