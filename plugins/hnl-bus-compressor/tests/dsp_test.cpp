// Offline measurements of the bus compressor. Build and run with tests/run.sh.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../dsp/BusCompressor.hpp"

#if defined(__SSE2__) || defined(_M_X64)
#include <immintrin.h>
#endif

using namespace hnl;

static int failures = 0;

static void check(bool ok, const std::string& name, const char* fmt, double a = 0, double b = 0, double c = 0) {
    std::printf("%s  %-44s ", ok ? "PASS" : "FAIL", name.c_str());
    std::printf(fmt, a, b, c);
    std::printf("\n");
    if (!ok) ++failures;
}

static std::vector<float> sine(double f, double amp, double fs, double seconds) {
    std::vector<float> x(static_cast<size_t>(seconds * fs));
    for (size_t i = 0; i < x.size(); ++i) x[i] = static_cast<float>(amp * std::sin(2 * kPi * f * i / fs));
    return x;
}

static void run(BusCompressor& m, std::vector<float>& l, std::vector<float>& r) {
    const int block = 256;
    for (size_t i = 0; i < l.size(); i += block)
        m.process(&l[i], &r[i], static_cast<int>(std::min<size_t>(block, l.size() - i)));
}

/** A compressor that is set up and at rest. */
struct Comp : BusCompressor {
    Comp(double fs, const BusCompParams& p) {
        prepare(fs);
        setParams(p);
        reset();
    }
};

/**
 * Gain reduction over time for a steady (DC) input whose level steps through
 * `levels` (dBFS), each lasting `seconds[i]`. A steady input makes the
 * detector's level exact, so the curve and the time constants can be read
 * off directly. Returns the reduction in dB per sample.
 */
static std::vector<double> envelope(double fs, const BusCompParams& p, const std::vector<double>& levels, const std::vector<double>& seconds) {
    Comp m(fs, p);
    std::vector<float> l, in;
    for (size_t i = 0; i < levels.size(); ++i) l.insert(l.end(), static_cast<size_t>(seconds[i] * fs), static_cast<float>(dbToLin(levels[i])));
    in = l;
    std::vector<float> r = l;
    run(m, l, r);
    std::vector<double> gr(l.size());
    for (size_t i = 0; i < l.size(); ++i) gr[i] = -linToDb(l[i] / in[i]);
    return gr;
}

static void testStaticCurve() {
    for (int ratio = 0; ratio < kRatioCount; ++ratio) {
        BusCompParams p;
        p.threshold = -30.0;
        p.ratio = ratio;
        p.attack = 2;
        p.release = 1;
        const double slope = 1.0 - 1.0 / kRatios[ratio];
        const auto far = envelope(48000, p, {-10.0}, {1.0});
        const auto at = envelope(48000, p, {-30.0}, {1.0});
        const auto under = envelope(48000, p, {-30.0 - 0.5 * kKneesDb[ratio] - 0.5}, {1.0});
        char name[64];
        std::snprintf(name, sizeof(name), "curve %.1f:1, 20 dB over threshold", kRatios[ratio]);
        check(std::fabs(far.back() - slope * 20.0) < 0.02, name, "%.2f dB reduction, expected %.2f", far.back(), slope * 20.0);
        std::snprintf(name, sizeof(name), "curve %.1f:1, at the threshold (knee)", kRatios[ratio]);
        check(std::fabs(at.back() - slope * kKneesDb[ratio] / 8.0) < 0.02, name, "%.2f dB reduction, expected %.2f", at.back(), slope * kKneesDb[ratio] / 8.0);
        std::snprintf(name, sizeof(name), "curve %.1f:1, below the knee", kRatios[ratio]);
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
    // Attack: time to reach 63% of the final reduction after the level jumps up.
    for (int attack = 0; attack < kAttackCount; ++attack) {
        BusCompParams p;
        p.threshold = -30.0;
        p.attack = attack;
        p.release = 3;
        const auto gr = envelope(fs, p, {-60.0, -10.0}, {0.1, 0.5});
        const size_t step = static_cast<size_t>(0.1 * fs);
        const double t = 1000.0 * timeTo(gr, step, gr.back() * (1.0 - std::exp(-1.0)), true, fs), want = kAttacksMs[attack];
        std::snprintf(name, sizeof(name), "attack %g ms at %.0f Hz", want, fs);
        check(std::fabs(t - want) < 0.05 * want + 1500.0 / fs, name, "63%% reached after %.2f ms", t);
    }
    // Release: time to fall to 37% after the level drops away.
    for (int release = 0; release < kReleaseCount - 1; ++release) {
        BusCompParams p;
        p.threshold = -30.0;
        p.attack = 0;
        p.release = release;
        const auto gr = envelope(fs, p, {-10.0, -60.0}, {0.5, 3.0});
        const size_t step = static_cast<size_t>(0.5 * fs);
        const double t = timeTo(gr, step, gr[step - 1] * std::exp(-1.0), false, fs), want = kReleasesS[release];
        std::snprintf(name, sizeof(name), "release %g s at %.0f Hz", want, fs);
        check(std::fabs(t - want) < 0.03 * want, name, "37%% reached after %.3f s", t);
    }
}

static void testAutoRelease() {
    BusCompParams p;
    p.threshold = -30.0;
    p.attack = 0;
    p.release = kAutoRelease;
    const double fs = 48000;
    // What is left of the reduction half a second after the loud part ends.
    const auto brief = envelope(fs, p, {-60.0, -10.0, -60.0}, {0.1, 0.03, 1.0});
    const auto longer = envelope(fs, p, {-60.0, -10.0, -60.0}, {0.1, 5.0, 1.0});
    const size_t endBrief = static_cast<size_t>(0.13 * fs), endLong = static_cast<size_t>(5.1 * fs), half = static_cast<size_t>(0.5 * fs);
    const double leftBrief = brief[endBrief + half] / brief[endBrief - 1], leftLong = longer[endLong + half] / longer[endLong - 1];
    check(leftBrief < 0.12, "auto release after a 30 ms peak", "%.0f%% left after 0.5 s", 100 * leftBrief);
    check(leftLong > 0.75, "auto release after 5 s of compression", "%.0f%% left after 0.5 s", 100 * leftLong);
    // The slow part must not add to the reduction while compressing.
    check(std::fabs(longer[endLong - 1] - 15.0) < 0.02, "auto release: steady reduction unchanged", "%.2f dB, expected 15.00", longer[endLong - 1]);
}

/** Gain in dB applied to a steady sine, measured over the last quarter of two seconds. */
static double sineGain(BusCompressor& m, double f, double amp, double fs, double* right = nullptr, double ampRight = -1.0) {
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

static void testSignal() {
    const double fs = 48000;
    {
        BusCompParams p; // default settings, signal under the knee
        Comp m(fs, p);
        auto l = sine(1000, dbToLin(-30), fs, 0.5), r = l;
        const auto in = l;
        run(m, l, r);
        double diff = 0;
        for (size_t i = 0; i < l.size(); ++i) diff = std::max(diff, static_cast<double>(std::fabs(l[i] - in[i])));
        check(diff == 0.0, "below the threshold: untouched", "largest difference %.2e", diff);
    }
    {
        BusCompParams p; // a sine well over the threshold: between the static value and a few dB less
        p.threshold = -30.0;
        p.attack = 2;
        p.release = 3;
        Comp m(fs, p);
        const double g = sineGain(m, 1000, dbToLin(-10), fs);
        check(g < -12.5 && g > -15.0, "1 kHz sine 20 dB over, 4:1", "%.2f dB (steady level would give -15.00)", g);
        check(std::fabs(m.meters.reduction.load() + g) < 0.6, "reduction meter follows", "%.2f dB shown", m.meters.reduction.load());
    }
    {
        BusCompParams p; // left loud, right quiet: both channels get the same gain
        p.threshold = -30.0;
        Comp m(fs, p);
        double right = 0;
        const double left = sineGain(m, 1000, dbToLin(-10), fs, &right, dbToLin(-40));
        check(left < -8.0 && std::fabs(left - right) < 0.1, "stereo link: quiet channel follows", "left %.2f dB, right %.2f dB", left, right);
    }
    {
        BusCompParams p; // the sidechain filter keeps bass out of the detector, not out of the sound
        p.threshold = -30.0;
        Comp open(fs, p);
        p.scHp = 300.0;
        Comp filtered(fs, p);
        const double bassOpen = sineGain(open, 50, dbToLin(-10), fs), bassFiltered = sineGain(filtered, 50, dbToLin(-10), fs);
        check(bassOpen < -10.0 && bassFiltered > -0.5, "sidechain high-pass 300 Hz, 50 Hz sine", "%.2f dB without, %.2f dB with", bassOpen, bassFiltered);
        Comp filtered2(fs, p);
        Comp open2(fs, BusCompParams{-30.0});
        const double trebleFiltered = sineGain(filtered2, 3000, dbToLin(-10), fs), trebleOpen = sineGain(open2, 3000, dbToLin(-10), fs);
        check(std::fabs(trebleFiltered - trebleOpen) < 0.15, "sidechain high-pass leaves 3 kHz alone", "%.2f dB without, %.2f dB with", trebleOpen, trebleFiltered);
    }
    {
        BusCompParams p; // make-up gain
        p.makeup = 6.0;
        Comp m(fs, p);
        const double g = sineGain(m, 1000, dbToLin(-40), fs);
        check(std::fabs(g - 6.0) < 0.01, "make-up +6 dB, below the threshold", "%.2f dB", g);
    }
    {
        BusCompParams p; // mix
        p.threshold = -30.0;
        p.makeup = 6.0;
        p.mix = 0.0;
        Comp dry(fs, p);
        auto l = sine(1000, dbToLin(-10), fs, 0.5), r = l;
        const auto in = l;
        run(dry, l, r);
        double diff = 0;
        for (size_t i = 0; i < l.size(); ++i) diff = std::max(diff, static_cast<double>(std::fabs(l[i] - in[i])));
        check(diff == 0.0, "mix 0%: untouched", "largest difference %.2e", diff);

        p.mix = 50.0;
        p.makeup = 0.0;
        const auto half = envelope(fs, p, {-10.0}, {1.0});
        const double want = -linToDb(0.5 + 0.5 * dbToLin(-15.0));
        check(std::fabs(half.back() - want) < 0.02, "mix 50%: half dry, half compressed", "%.2f dB down, expected %.2f", half.back(), want);
    }
    {
        BusCompParams p; // no latency: an impulse comes out where it went in
        Comp m(fs, p);
        std::vector<float> l(1000, 0.0f), r(1000, 0.0f);
        l[100] = r[100] = 0.01f;
        run(m, l, r);
        size_t at = 0;
        for (size_t i = 0; i < l.size(); ++i) if (std::fabs(l[i]) > std::fabs(l[at])) at = i;
        check(at == 100, "no latency", "impulse at sample %.0f, sent at 100", static_cast<double>(at));
    }
    {
        BusCompParams p; // turning make-up moves smoothly, without a jump
        Comp m(fs, p);
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
    BusCompParams p;
    p.threshold = -30.0;
    Comp m(fs, p);
    auto l = sine(100, 0.5, fs, 1.0), r = l;
    run(m, l, r);
    std::vector<float> zl(static_cast<size_t>(80 * fs), 0.0f), zr = zl;
    run(m, zl, zr);
    m.meters.reduction.store(0.0f);
    std::vector<float> tl(256, 0.0f), tr(256, 0.0f);
    m.process(tl.data(), tr.data(), 256);
    check(m.meters.reduction.load() == 0.0f, "comes to rest after silence", "reduction %.2e dB", m.meters.reduction.load());

    auto nl = sine(220, 0.5, fs, 20.0), nr = sine(331, 0.4, fs, 20.0);
    const auto start = std::chrono::steady_clock::now();
    run(m, nl, nr);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    check(seconds < 2.0, "speed", "20 s of audio in %.3f s (%.0fx real time)", seconds, 20.0 / seconds);
}

// Made from bits and tested by bits, so that no compiler setting can reason them away.
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

/**
 * Numbers too small to matter must count as zero inside the engine, whatever the host has set:
 * otherwise filters ringing out into silence make the processor many times slower.
 */
static void testDenormals() {
#if defined(__SSE2__) || defined(_M_X64)
    const unsigned before = _mm_getcsr();
    _mm_setcsr(before & ~0x8040u); // as a host that sets nothing
    BusCompressor m;
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
    testStaticCurve();
    for (double fs : {44100.0, 48000.0, 96000.0}) testTiming(fs);
    testAutoRelease();
    testSignal();
    testSilenceAndSpeed();
    testDenormals();
    std::printf("%s\n", failures ? "FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
