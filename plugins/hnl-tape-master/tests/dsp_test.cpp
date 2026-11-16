// Offline measurements of the tape machine. Build and run with tests/run.sh.
#include <chrono>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include "../dsp/TapeMachine.hpp"

using namespace hnl;

static int failures = 0;

static void check(bool ok, const std::string& name, const char* fmt, double a = 0, double b = 0, double c = 0) {
    std::printf("%s  %-38s ", ok ? "PASS" : "FAIL", name.c_str());
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

static std::vector<float> sine(double f, double amp, double fs, double seconds) {
    std::vector<float> x(static_cast<size_t>(seconds * fs));
    for (size_t i = 0; i < x.size(); ++i) x[i] = static_cast<float>(amp * std::sin(2 * kPi * f * i / fs));
    return x;
}

static void run(TapeMachine& m, std::vector<float>& l, std::vector<float>& r) {
    const int block = 256;
    for (size_t i = 0; i < l.size(); i += block)
        m.process(&l[i], &r[i], static_cast<int>(std::min<size_t>(block, l.size() - i)));
}

/** A steady machine: no wow, flutter or hiss. */
static TapeParams steady() {
    TapeParams p;
    p.wow = p.flutter = p.noise = 0.0;
    return p;
}

/** Level in dB VU (0 VU = kReferenceDb dBFS sine) to peak amplitude. */
static double vu(double db) { return dbToLin(kReferenceDb + db); }

/** Output of a sine at `level` VU: fundamental gain in dB, third harmonic in percent. */
static void measure(double fs, const TapeParams& p, double f, double level, double* gainDb, double* thd3 = nullptr, double* h2 = nullptr) {
    TapeMachine m;
    m.prepare(fs);
    m.setParams(p);
    auto l = sine(f, vu(level), fs, 1.0), r = l;
    run(m, l, r);
    const double fund = tone(l, l.size() / 2, l.size(), f, fs);
    if (gainDb) *gainDb = linToDb(fund / vu(level));
    if (thd3) *thd3 = 100.0 * tone(l, l.size() / 2, l.size(), 3 * f, fs) / fund;
    if (h2) *h2 = 100.0 * tone(l, l.size() / 2, l.size(), 2 * f, fs) / fund;
}

static void testLatency(double fs) {
    for (int path : {kInput, kRepro}) {
        TapeMachine m;
        m.prepare(fs);
        TapeParams p = steady();
        p.path = path;
        m.setParams(p);
        std::vector<float> l(2000, 0.0f), r(2000, 0.0f);
        l[100] = r[100] = 0.01f;
        run(m, l, r);
        int at = 0;
        for (size_t i = 0; i < l.size(); ++i) if (std::fabs(l[i]) > std::fabs(l[at])) at = static_cast<int>(i);
        check(at - 100 == m.latency(), path == kInput ? "latency, input path" : "latency, tape path", "%.0f samples, reported %.0f", at - 100, m.latency());
    }
}

static void testInputPath(double fs) {
    TapeParams p = steady();
    p.path = kInput;
    double worst = 0, g, thd;
    for (double f : {20.0, 100.0, 1000.0, 10000.0, 18000.0}) {
        measure(fs, p, f, 0, &g);
        worst = std::max(worst, std::fabs(g));
    }
    measure(fs, p, 1000, 0, &g, &thd);
    check(worst < 0.1 && thd < 0.5, "input path: flat, nearly clean", "worst %.3f dB, third harmonic %.2f%% at 0 VU", worst, thd);
    measure(fs, p, 1000, 22, &g, &thd);
    check(thd > 3.0 && g < -1.0, "input path: amplifier clips when hot", "at +22 VU: %.1f%% third harmonic, %.1f dB", thd, g);
}

static void testSaturation(double fs) {
    static const char* const names[kFormulaCount] = {"vintage", "classic", "modern", "high output"};
    TapeParams p = steady();
    p.speed = kSpeed15;
    for (int f = 0; f < kFormulaCount; ++f) {
        p.formula = f;
        double thd, h2, g;
        measure(fs, p, 315, formulaInfo(f).mol, &g, &thd, &h2);
        check(thd > 2.4 && thd < 3.8 && h2 < 0.05, std::string(names[f]) + ": 3% third harmonic at MOL",
              "%.2f%% at +%.0f VU, second harmonic %.3f%%", thd, formulaInfo(f).mol, h2);
    }
    p.formula = kClassic;
    double lo, mid, hi, thd;
    measure(fs, p, 315, -20, &lo, &thd);
    measure(fs, p, 315, 9, &mid);
    measure(fs, p, 315, 18, &hi);
    check(std::fabs(lo) < 0.3 && thd < 0.05, "quiet signals pass unchanged", "%.2f dB, %.3f%% third harmonic at -20 VU", lo, thd);
    check(mid < -0.5 && hi < mid - 3.0, "tape compression grows with level", "%.1f dB at +9 VU, %.1f dB at +18 VU", mid, hi);

    // The top compresses before the middle, more so on slow tape.
    double slowTop, slowMid, fastTop, fastMid;
    p.speed = kSpeed7;
    measure(fs, p, 10000, 6, &slowTop);
    measure(fs, p, 10000, -20, &lo);
    slowTop -= lo;
    measure(fs, p, 315, 6, &slowMid);
    p.speed = kSpeed30;
    measure(fs, p, 10000, 6, &fastTop);
    measure(fs, p, 10000, -20, &lo);
    fastTop -= lo;
    measure(fs, p, 315, 6, &fastMid);
    check(slowTop < slowMid - 1.0 && slowTop < fastTop - 1.0, "highs saturate first on slow tape",
          "10 kHz at +6 VU: %.1f dB at 7.5 ips, %.1f dB at 30 ips (315 Hz: %.1f dB)", slowTop, fastTop, fastMid);
}

static void testResponse(double fs) {
    TapeParams p = steady();
    static const double ips[3] = {7.5, 15, 30};
    double top[3];
    for (int s = 0; s < kSpeedCount; ++s) {
        p.speed = s;
        const double bumpFreq = 3.6 * ips[s];
        double bump, mid, sub;
        measure(fs, p, bumpFreq, -20, &bump);
        measure(fs, p, 1000, -20, &mid);
        measure(fs, p, 0.15 * bumpFreq, -20, &sub);
        measure(fs, p, 15000, -20, &top[s]);
        char name[48];
        std::snprintf(name, sizeof(name), "%.1f ips: head bump and low roll-off", ips[s]);
        check(bump > 0.8 && bump < 3.0 && std::fabs(mid) < 0.3 && sub < -6.0, name, "%+.2f dB at %.0f Hz, %+.1f dB at %.0f Hz", bump,
              bumpFreq, sub);
    }
    check(top[0] < -3.0 && top[1] > top[0] + 2.0 && top[2] > top[1] && top[2] > -1.0, "faster tape keeps more top",
          "15 kHz: %+.1f / %+.1f / %+.1f dB at 7.5 / 15 / 30 ips", top[0], top[1], top[2]);

    p.speed = kSpeed15;
    double narrow, wide, repro, sync, t;
    p.head = kQuarterInch;
    measure(fs, p, 54, -20, &narrow);
    p.head = kOneInch;
    measure(fs, p, 54, -20, &wide);
    check(narrow > wide + 0.5, "narrow head has the bigger bump", "quarter inch %+.2f dB, one inch %+.2f dB", narrow, wide);
    p.head = kHalfInch;
    measure(fs, p, 12000, -20, &repro);
    p.path = kSync;
    measure(fs, p, 12000, -20, &sync);
    check(sync < repro - 1.0, "sync head is duller", "12 kHz: repro %+.1f dB, sync %+.1f dB", repro, sync);

    p.path = kRepro;
    double under, over, thdNominal, thdOver;
    measure(fs, p, 315, 6, &t, &thdNominal);
    p.bias = -100;
    measure(fs, p, 10000, -20, &under);
    p.bias = 100;
    measure(fs, p, 10000, -20, &over);
    measure(fs, p, 315, 6, &t, &thdOver);
    check(under > over + 1.5 && thdNominal > thdOver * 1.2, "more bias: duller top, cleaner lows",
          "under to over: %+.1f dB at 10 kHz; third harmonic at +6 VU %.2f%% nominal, %.2f%% over", over - under, thdNominal, thdOver);
    double quietUnder, quietNominal;
    p.bias = -100;
    measure(fs, p, 315, -10, &t, &quietUnder);
    p.bias = 0;
    measure(fs, p, 315, -10, &t, &quietNominal);
    check(quietUnder > 0.5 && quietNominal < 0.1, "too little bias roughens quiet signals", "third harmonic at -10 VU: %.2f%% under-biased, %.3f%% nominal", quietUnder, quietNominal);
}

static void testLink(double fs) {
    TapeParams p = steady();
    p.record = 9;
    double linked, plain;
    measure(fs, p, 1000, -30, &linked);
    p.link = false;
    measure(fs, p, 1000, -30, &plain);
    check(std::fabs(linked) < 0.3 && std::fabs(plain - 9) < 0.3, "link takes the record level off again", "linked %+.2f dB, unlinked %+.2f dB", linked, plain);
}

static void testTransport(double fs) {
    auto deviation = [&](double wow, double flutter) {
        TapeMachine m;
        m.prepare(fs);
        TapeParams p = steady();
        p.wow = wow;
        p.flutter = flutter;
        m.setParams(p);
        auto l = sine(fs / 40, 0.1, fs, 6.0), r = l; // a whole number of samples per cycle
        run(m, l, r);
        // Instantaneous frequency from the spacing of rising zero crossings.
        double last = -1, lo = 1e9, hi = 0;
        for (size_t i = static_cast<size_t>(fs); i + 1 < l.size(); ++i) {
            if (l[i] < 0 && l[i + 1] >= 0) {
                const double t = static_cast<double>(i) + static_cast<double>(l[i]) / (static_cast<double>(l[i]) - l[i + 1]);
                if (last >= 0) { lo = std::min(lo, t - last); hi = std::max(hi, t - last); }
                last = t;
            }
        }
        return 100.0 * (hi - lo) / (hi + lo); // peak deviation in percent
    };
    const double none = deviation(0, 0), kept = deviation(10, 10), wow = deviation(100, 0), flutter = deviation(0, 100);
    check(none < 0.002, "steady transport holds pitch", "%.4f%% peak deviation", none);
    check(kept > 0.01 && kept < 0.08, "well-kept machine (10%)", "%.3f%% peak deviation", kept);
    check(wow > 0.15 && wow < 0.45 && flutter > 0.08 && flutter < 0.4, "wow and flutter at full", "wow %.2f%%, flutter %.2f%%", wow, flutter);
}

static void testNoise(double fs) {
    static double corr_;
    auto level = [&](double amount, int head, int path = kRepro) {
        TapeMachine m;
        m.prepare(fs);
        TapeParams p = steady();
        p.speed = kSpeed15;
        p.noise = amount;
        p.head = head;
        p.path = path;
        m.setParams(p);
        std::vector<float> l(static_cast<size_t>(2 * fs), 0.0f), r(l.size(), 0.0f);
        run(m, l, r);
        double sum = 0, cross = 0;
        for (size_t i = l.size() / 2; i < l.size(); ++i) { sum += static_cast<double>(l[i]) * l[i]; cross += static_cast<double>(l[i]) * r[i]; }
        corr_ = sum > 0 ? cross / sum : 0;
        // Relative to a 0 VU sine.
        return 10 * std::log10(sum / (l.size() / 2) + 1e-30) - (kReferenceDb - 3.01);
    };
    const double off = level(0, kHalfInch), real = level(50, kHalfInch), loud = level(100, kHalfInch);
    check(off < -250, "noise off is silent", "%.0f dB", off);
    check(std::fabs(real - (-62.0)) < 1.5 && std::fabs(corr_) < 0.1, "hiss at 50% is the formula's level", "%.1f dB below 0 VU, left/right correlation %.2f", real, corr_);
    check(std::fabs(loud - real - 20) < 1.0, "hiss at 100% is 20 dB more", "%.1f dB", loud - real);
    const double narrow = level(50, kQuarterInch), wide = level(50, kOneInch);
    check(std::fabs(narrow - wide - 6) < 1.5, "wider tracks are quieter", "quarter inch %.1f dB, one inch %.1f dB", narrow, wide);
    check(level(50, kHalfInch, kInput) < -250, "no hiss without tape", "input path silent", 0);
}

static void testAliasingAndSpeed(double fs) {
    {
        TapeMachine m;
        m.prepare(fs);
        TapeParams p = steady();
        p.record = 12;
        p.formula = kVintage;
        m.setParams(p);
        const double f = 0.21 * fs; // harmonics 3 and 5 fold to 0.37 and 0.05 of the rate without oversampling
        auto l = sine(f, vu(6), fs, 2.0), r = l;
        run(m, l, r);
        double worst = -200;
        for (double a : {0.37, 0.05}) worst = std::max(worst, linToDb(tone(l, l.size() / 2, l.size(), a * fs, fs)));
        check(worst < -80, "aliasing suppressed", "strongest folded harmonic %.1f dBFS (tape at +18 VU)", worst);
    }
    TapeMachine m;
    m.prepare(fs);
    TapeParams p;
    p.noise = 50;
    p.wow = p.flutter = 50;
    p.record = 6;
    m.setParams(p);
    std::mt19937 rng(3);
    std::uniform_real_distribution<float> d(-0.5f, 0.5f);
    std::vector<float> l(static_cast<size_t>(20 * fs)), r(l.size());
    for (size_t i = 0; i < l.size(); ++i) { l[i] = d(rng); r[i] = d(rng); }
    const auto t0 = std::chrono::steady_clock::now();
    run(m, l, r);
    const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    bool finite = true;
    for (float v : l) finite = finite && std::isfinite(v);
    check(finite && sec < 4.0, "speed (everything on, stereo)", "%.0fx realtime (%.1f%% of one core)", 20.0 / sec, 100.0 * sec / 20.0);
}

static void testMeters(double fs) {
    TapeMachine m;
    m.prepare(fs);
    TapeParams p = steady();
    p.record = 3;
    m.setParams(p);
    auto l = sine(1000, vu(0), fs, 2.0), r = sine(1000, vu(-6), fs, 2.0);
    run(m, l, r);
    const double a = m.meters.vu[0].load(), b = m.meters.vu[1].load();
    check(std::fabs(a - 3) < 0.3 && std::fabs(b + 3) < 0.3, "VU meters read the level on tape", "left %+.2f VU, right %+.2f VU", a, b);
}

int main() {
    for (double fs : {44100.0, 48000.0, 96000.0}) {
        std::printf("--- %.0f Hz ---\n", fs);
        testLatency(fs);
        testInputPath(fs);
        testSaturation(fs);
        testResponse(fs);
        testLink(fs);
        testTransport(fs);
        testNoise(fs);
        testMeters(fs);
        testAliasingAndSpeed(fs);
    }
    std::printf("%s\n", failures ? "FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
