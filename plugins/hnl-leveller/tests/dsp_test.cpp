// Offline measurements of the leveller. Build and run with tests/run.sh.
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "../dsp/Leveller.hpp"

using namespace hnl;

static int failures = 0;
static const char* const kNames[2] = {"opto", "FET"};

static void check(bool ok, const std::string& name, const char* fmt, double a = 0, double b = 0, double c = 0) {
    std::printf("%s  %-46s ", ok ? "PASS" : "FAIL", name.c_str());
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

static void run(Leveller& m, std::vector<float>& l, std::vector<float>& r) {
    const int block = 256;
    for (size_t i = 0; i < l.size(); i += block)
        m.process(&l[i], &r[i], static_cast<int>(std::min<size_t>(block, l.size() - i)));
}

/** A leveller that is set up and at rest. */
struct Lev : Leveller {
    Lev(double fs, const LevellerParams& p) {
        prepare(fs);
        setParams(p);
        reset();
    }
};

/** Settings with the colour stage off, so that the gain can be read off exactly. */
static LevellerParams clean(int type, double threshold = -30.0) {
    LevellerParams p;
    p.type = type;
    p.threshold = threshold;
    p.colour = 0.0;
    return p;
}

/**
 * Gain reduction over time for a steady (DC) input whose level steps through
 * `levels` (dBFS), each lasting `seconds[i]`. A steady input makes the
 * detector's level exact, so the curve and the time constants can be read
 * off directly. Returns the reduction in dB per sample.
 */
static std::vector<double> envelope(double fs, const LevellerParams& p, const std::vector<double>& levels, const std::vector<double>& seconds) {
    Lev m(fs, p);
    std::vector<float> l;
    for (size_t i = 0; i < levels.size(); ++i) l.insert(l.end(), static_cast<size_t>(seconds[i] * fs), static_cast<float>(dbToLin(levels[i])));
    const std::vector<float> in = l;
    std::vector<float> r = l;
    run(m, l, r);
    std::vector<double> gr(l.size());
    for (size_t i = 0; i < l.size(); ++i) gr[i] = -linToDb(l[i] / in[i]);
    return gr;
}

static void testStaticCurve() {
    for (int type = 0; type < kLevellerTypeCount; ++type)
        for (int ratio = 0; ratio < kLevRatioCount; ++ratio) {
            LevellerParams p = clean(type);
            p.ratio = ratio;
            const double slope = 1.0 - 1.0 / kLevRatios[ratio], knee = kLevKneesDb[type][ratio];
            const auto far = envelope(48000, p, {-10.0}, {1.0});
            const auto at = envelope(48000, p, {-30.0}, {1.0});
            const auto under = envelope(48000, p, {-30.0 - 0.5 * knee - 0.5}, {1.0});
            char name[64];
            std::snprintf(name, sizeof(name), "%s %.0f:1, 20 dB over the threshold", kNames[type], kLevRatios[ratio]);
            check(std::fabs(far.back() - slope * 20.0) < 0.02, name, "%.2f dB reduction, expected %.2f", far.back(), slope * 20.0);
            std::snprintf(name, sizeof(name), "%s %.0f:1, at the threshold (knee %.0f dB)", kNames[type], kLevRatios[ratio], knee);
            check(std::fabs(at.back() - slope * knee / 8.0) < 0.02, name, "%.2f dB reduction, expected %.2f", at.back(), slope * knee / 8.0);
            std::snprintf(name, sizeof(name), "%s %.0f:1, below the knee", kNames[type], kLevRatios[ratio]);
            check(std::fabs(under.back()) < 1e-6, name, "%.4f dB reduction", under.back());
        }
}

/** Seconds after `from` at which the envelope first crosses `level` (rising or falling). */
static double timeTo(const std::vector<double>& gr, size_t from, double level, bool rising, double fs) {
    for (size_t i = from; i < gr.size(); ++i)
        if (rising ? gr[i] >= level : gr[i] <= level) return (i - from) / fs;
    return -1.0;
}

static void testTiming(double fs) {
    char name[64];
    for (int type = 0; type < kLevellerTypeCount; ++type)
        for (double knob : {0.0, 50.0, 100.0}) {
            LevellerParams p = clean(type);
            p.attack = p.release = knob;
            const LevellerTiming t = levellerTiming(type, knob, knob);

            // Attack: time to reach 63% of the final reduction after the level jumps up.
            const auto up = envelope(fs, p, {-60.0, -10.0}, {0.1, 0.5});
            const size_t step = static_cast<size_t>(0.1 * fs);
            const double a = 1000.0 * timeTo(up, step, up.back() * (1.0 - std::exp(-1.0)), true, fs);
            std::snprintf(name, sizeof(name), "%s attack at %.0f%%, %.0f Hz", kNames[type], knob, fs);
            check(std::fabs(a - t.attackMs) < 0.05 * t.attackMs + 1500.0 / fs, name, "63%% after %.3f ms, expected %.3f", a, t.attackMs);

            // Release after a 50 ms burst, too short for the opto's slow stage to matter: time to fall to 37%.
            const auto down = envelope(fs, p, {-60.0, -10.0, -60.0}, {0.05, 0.05, 3.0});
            const size_t end = static_cast<size_t>(0.1 * fs);
            const double r = 1000.0 * timeTo(down, end, down[end - 1] * std::exp(-1.0), false, fs);
            std::snprintf(name, sizeof(name), "%s release at %.0f%%, %.0f Hz", kNames[type], knob, fs);
            check(std::fabs(r - t.releaseMs) < 0.04 * t.releaseMs, name, "37%% after %.1f ms, expected %.1f", r, t.releaseMs);
        }
}

static void testOptoRelease() {
    const double fs = 48000;
    const LevellerParams p = clean(kOpto);
    const LevellerTiming t = levellerTiming(kOpto, 50, 50); // 60 ms, then a 1.5 s tail
    const auto brief = envelope(fs, p, {-60.0, -10.0, -60.0}, {0.1, 0.03, 1.0});
    const auto longer = envelope(fs, p, {-60.0, -10.0, -60.0}, {0.1, 8.0, 3.0});
    const size_t endBrief = static_cast<size_t>(0.13 * fs), endLong = static_cast<size_t>(8.1 * fs);
    const auto left = [&](const std::vector<double>& gr, size_t end, double seconds) { return gr[end + static_cast<size_t>(seconds * fs)] / gr[end - 1]; };

    check(left(brief, endBrief, 0.5) < 0.03, "opto: a 30 ms peak is let go at once", "%.1f%% left after 0.5 s", 100 * left(brief, endBrief, 0.5));
    const double quick = left(longer, endLong, 4 * t.releaseMs * 0.001), late = left(longer, endLong, 1.5);
    check(quick > 0.42 && quick < 0.55, "opto: after 8 s, about half goes quickly", "%.0f%% left after %.0f ms", 100 * quick, 4 * t.releaseMs);
    check(late > 0.14 && late < 0.22, "opto: the rest fades over the tail time", "%.0f%% left after 1.5 s (expected about 18%%)", 100 * late);
    check(std::fabs(longer[endLong - 1] - 15.0) < 0.02, "opto: steady reduction is not changed by the tail", "%.2f dB", longer[endLong - 1]);

    LevellerParams fet = clean(kFet);
    const auto plain = envelope(fs, fet, {-60.0, -10.0, -60.0}, {0.1, 8.0, 3.0});
    check(left(plain, endLong, 1.5) < 0.01, "FET: no tail after 8 s", "%.2f%% left after 1.5 s", 100 * left(plain, endLong, 1.5));
}

/** Gain in dB applied to a steady sine, measured over the last quarter of two seconds. */
static double sineGain(Leveller& m, double f, double amp, double fs, double* right = nullptr, double ampRight = -1.0) {
    auto l = sine(f, amp, fs, 2.0), r = sine(f * 1.5, ampRight < 0 ? amp : ampRight, fs, 2.0);
    const auto inL = l, inR = r;
    run(m, l, r);
    double a = 0, b = 0, c = 0, d = 0;
    for (size_t i = l.size() * 3 / 4; i < l.size(); ++i) {
        a += l[i] * l[i];
        b += inL[i] * inL[i];
        c += r[i] * r[i];
        d += inR[i] * inR[i];
    }
    if (right) *right = 0.5 * linToDb(c / d);
    return 0.5 * linToDb(a / b);
}

static double largestDifference(const std::vector<float>& a, const std::vector<float>& b) {
    double diff = 0;
    for (size_t i = 0; i < a.size(); ++i) diff = std::max(diff, static_cast<double>(std::fabs(a[i] - b[i])));
    return diff;
}

static void testColour() {
    const double fs = 48000, f = 1000;
    // A sine at -12 dBFS, far below the threshold, so only the colour stage acts.
    for (int type = 0; type < kLevellerTypeCount; ++type) {
        LevellerParams p = clean(type, 0.0);
        p.colour = 100.0;
        Lev m(fs, p);
        auto l = sine(f, dbToLin(-12.0), fs, 2.0), r = l;
        run(m, l, r);
        const size_t from = l.size() / 2, to = l.size();
        const double fund = tone(l, from, to, f, fs), h2 = 100 * tone(l, from, to, 2 * f, fs) / fund, h3 = 100 * tone(l, from, to, 3 * f, fs) / fund;
        const double h4 = 100 * tone(l, from, to, 4 * f, fs) / fund;
        double mean = 0;
        for (size_t i = from; i < to; ++i) mean += l[i];
        mean /= (to - from);
        const double want2 = 100 * kColour2[type] / 2, want3 = 100 * kColour3[type] / 4;
        char name[64];
        std::snprintf(name, sizeof(name), "%s colour 100%%, -12 dBFS: harmonics", kNames[type]);
        check(std::fabs(h2 - want2) < 0.03 && std::fabs(h3 - want3) < 0.03 && h4 < 0.01, name, "2nd %.2f%%, 3rd %.2f%% (3rd expected %.2f)", h2, h3, want3);
        std::snprintf(name, sizeof(name), "%s colour 100%%: level and offset", kNames[type]);
        check(std::fabs(linToDb(fund / dbToLin(-12.0))) < 0.25 && std::fabs(mean) < 1e-5, name, "level %+.3f dB, offset %.1e", linToDb(fund / dbToLin(-12.0)), mean);
    }
    {
        LevellerParams p = clean(kFet, 0.0); // the harmonics grow with the level
        p.colour = 100.0;
        double h3[2];
        int n = 0;
        for (double level : {-24.0, -12.0}) {
            Lev m(fs, p);
            auto l = sine(f, dbToLin(level), fs, 1.0), r = l;
            run(m, l, r);
            h3[n++] = 100 * tone(l, l.size() / 2, l.size(), 3 * f, fs) / tone(l, l.size() / 2, l.size(), f, fs);
        }
        check(h3[1] > 10 * h3[0], "colour grows with the level", "3rd harmonic %.3f%% at -24 dBFS, %.3f%% at -12", h3[0], h3[1]);
    }
    {
        LevellerParams p = clean(kFet, 0.0); // a signal far over full scale stays bounded
        p.colour = 100.0;
        Lev coloured(fs, p);
        p.colour = 0.0;
        Lev plain(fs, p);
        auto l = sine(100, 4.0, fs, 0.5), r = l, l2 = l, r2 = l;
        run(coloured, l, r);
        run(plain, l2, r2);
        check(largestDifference(l, l2) < 0.15, "colour is bounded at +12 dBFS", "adds at most %.3f", largestDifference(l, l2));
    }
}

static void testSignal() {
    const double fs = 48000;
    for (int type = 0; type < kLevellerTypeCount; ++type) {
        Lev m(fs, clean(type, -20.0));
        auto l = sine(1000, dbToLin(-34), fs, 0.5), r = l;
        const auto in = l;
        run(m, l, r);
        check(largestDifference(l, in) == 0.0, std::string(kNames[type]) + ", colour 0, below the threshold: untouched", "largest difference %.2e", largestDifference(l, in));
    }
    {
        Lev m(fs, clean(kFet)); // a sine well over the threshold: between the static value and a few dB less
        const double g = sineGain(m, 1000, dbToLin(-10), fs);
        check(g < -12.5 && g > -15.0, "FET, 1 kHz sine 20 dB over, 4:1", "%.2f dB (steady level would give -15.00)", g);
        check(std::fabs(m.meters.reduction.load() + g) < 0.6, "reduction meter follows", "%.2f dB shown", m.meters.reduction.load());
    }
    {
        Lev m(fs, clean(kOpto)); // left loud, right quiet: both channels get the same gain
        double right = 0;
        const double left = sineGain(m, 1000, dbToLin(-10), fs, &right, dbToLin(-40));
        check(left < -8.0 && std::fabs(left - right) < 0.1, "stereo link: quiet channel follows", "left %.2f dB, right %.2f dB", left, right);
    }
    {
        LevellerParams p = clean(kOpto); // the sidechain filter keeps bass out of the detector, not out of the sound
        Lev open(fs, p), open2(fs, p);
        p.scHp = 300.0;
        Lev filtered(fs, p), filtered2(fs, p);
        const double bassOpen = sineGain(open, 50, dbToLin(-10), fs), bassFiltered = sineGain(filtered, 50, dbToLin(-10), fs);
        check(bassOpen < -10.0 && bassFiltered > -0.5, "sidechain high-pass 300 Hz, 50 Hz sine", "%.2f dB without, %.2f dB with", bassOpen, bassFiltered);
        const double trebleFiltered = sineGain(filtered2, 3000, dbToLin(-10), fs), trebleOpen = sineGain(open2, 3000, dbToLin(-10), fs);
        check(std::fabs(trebleFiltered - trebleOpen) < 0.15, "sidechain high-pass leaves 3 kHz alone", "%.2f dB without, %.2f dB with", trebleOpen, trebleFiltered);
    }
    {
        LevellerParams p = clean(kOpto, -20.0); // make-up gain
        p.makeup = 6.0;
        Lev m(fs, p);
        const double g = sineGain(m, 1000, dbToLin(-40), fs);
        check(std::fabs(g - 6.0) < 0.01, "make-up +6 dB, below the threshold", "%.2f dB", g);
    }
    {
        LevellerParams p = clean(kFet); // mix at 0%: the untouched signal, whatever the other settings
        p.makeup = 6.0;
        p.colour = 100.0;
        p.mix = 0.0;
        Lev dry(fs, p);
        auto l = sine(1000, dbToLin(-10), fs, 0.5), r = l;
        const auto in = l;
        run(dry, l, r);
        check(largestDifference(l, in) == 0.0, "mix 0%: untouched", "largest difference %.2e", largestDifference(l, in));

        p.mix = 50.0;
        p.makeup = p.colour = 0.0;
        const auto half = envelope(fs, p, {-10.0}, {1.0});
        const double want = -linToDb(0.5 + 0.5 * dbToLin(-15.0));
        check(std::fabs(half.back() - want) < 0.02, "mix 50%: half dry, half compressed", "%.2f dB down, expected %.2f", half.back(), want);
    }
    {
        Lev m(fs, LevellerParams{}); // no latency: an impulse comes out where it went in
        std::vector<float> l(1000, 0.0f), r(1000, 0.0f);
        l[100] = r[100] = 0.01f;
        run(m, l, r);
        size_t at = 0;
        for (size_t i = 0; i < l.size(); ++i) if (std::fabs(l[i]) > std::fabs(l[at])) at = i;
        check(at == 100, "no latency", "impulse at sample %.0f, sent at 100", static_cast<double>(at));
    }
    {
        LevellerParams p = clean(kOpto, -20.0); // turning make-up moves smoothly, without a jump
        Lev m(fs, p);
        auto l = std::vector<float>(12000, 0.01f), r = l;
        m.process(l.data(), r.data(), 2400);
        p.makeup = 12.0;
        m.setParams(p);
        m.process(l.data() + 2400, r.data() + 2400, 9600);
        double jump = 0;
        for (size_t i = 1; i < l.size(); ++i) jump = std::max(jump, static_cast<double>(std::fabs(l[i] - l[i - 1])) / 0.01);
        check(jump < 0.01 && std::fabs(l.back() / 0.01 - dbToLin(12.0)) < 0.05, "make-up change is smoothed", "largest step %.4f of the level", jump);
    }
}

static void testSilenceAndSpeed() {
    const double fs = 48000;
    LevellerParams p;
    p.threshold = -30.0;
    p.colour = 100.0;
    Lev m(fs, p);
    auto l = sine(100, 0.5, fs, 1.0), r = l;
    run(m, l, r);
    std::vector<float> zl(static_cast<size_t>(80 * fs), 0.0f), zr = zl;
    run(m, zl, zr);
    m.meters.reduction.store(0.0f);
    std::vector<float> tl(256, 0.0f), tr(256, 0.0f);
    m.process(tl.data(), tr.data(), 256);
    check(m.meters.reduction.load() == 0.0f && tl[255] == 0.0f, "comes to rest after silence", "reduction %.2e dB, output %.2e", m.meters.reduction.load(), tl[255]);

    auto nl = sine(220, 0.5, fs, 20.0), nr = sine(331, 0.4, fs, 20.0);
    const auto start = std::chrono::steady_clock::now();
    run(m, nl, nr);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    check(seconds < 2.0, "speed", "20 s of audio in %.3f s (%.0fx real time)", seconds, 20.0 / seconds);
}

int main() {
    testStaticCurve();
    for (double fs : {44100.0, 48000.0, 96000.0}) testTiming(fs);
    testOptoRelease();
    testColour();
    testSignal();
    testSilenceAndSpeed();
    std::printf("%s\n", failures ? "FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
