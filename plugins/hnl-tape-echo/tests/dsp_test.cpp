// Offline measurements of the tape echo. Build and run with tests/run.sh.
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

#include "../dsp/TapeEcho.hpp"

using namespace hnl;

static int failures = 0;

static void check(bool ok, const std::string& name, const char* fmt, double a = 0, double b = 0, double c = 0, double d = 0) {
    std::printf("%s  %-40s ", ok ? "PASS" : "FAIL", name.c_str());
    std::printf(fmt, a, b, c, d);
    std::printf("\n");
    if (!ok) ++failures;
}

typedef std::vector<float> Buf;

static void run(TapeEcho& m, Buf& l, Buf& r) {
    const int block = 256;
    for (size_t i = 0; i < l.size(); i += block)
        m.process(&l[i], &r[i], static_cast<int>(std::min<size_t>(block, l.size() - i)));
}

/** A 1 kHz burst of 20 ms with soft edges, starting at 0.1 s, in a buffer of `seconds`. */
static Buf burst(double fs, double seconds, double amp, double f = 1000.0) {
    Buf x(static_cast<size_t>(seconds * fs), 0.0f);
    const size_t from = static_cast<size_t>(0.1 * fs), n = static_cast<size_t>(0.02 * fs);
    for (size_t i = 0; i < n; ++i)
        x[from + i] = static_cast<float>(amp * (0.5 - 0.5 * std::cos(2 * kPi * i / n)) * std::sin(2 * kPi * f * i / fs));
    return x;
}

static double rms(const Buf& x, double fs, double from, double to) {
    double sum = 0;
    const size_t a = static_cast<size_t>(from * fs), b = std::min(x.size(), static_cast<size_t>(to * fs));
    for (size_t i = a; i < b; ++i) sum += static_cast<double>(x[i]) * x[i];
    return std::sqrt(sum / std::max<size_t>(1, b - a));
}

static double peak(const Buf& x, double fs, double from, double to, double* at = nullptr) {
    double pk = 0;
    const size_t a = static_cast<size_t>(from * fs), b = std::min(x.size(), static_cast<size_t>(to * fs));
    for (size_t i = a; i < b; ++i)
        if (std::fabs(x[i]) > pk) { pk = std::fabs(x[i]); if (at) *at = i / fs; }
    return pk;
}

/** Amplitude of one frequency component over [from, to) seconds. */
static double tone(const Buf& x, double fs, double from, double to, double f) {
    double re = 0, im = 0;
    const size_t a = static_cast<size_t>(from * fs), b = std::min(x.size(), static_cast<size_t>(to * fs));
    for (size_t i = a; i < b; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2 * kPi * (i - a) / (b - a));
        re += w * x[i] * std::cos(2 * kPi * f * i / fs);
        im += w * x[i] * std::sin(2 * kPi * f * i / fs);
    }
    return 2.0 * std::sqrt(re * re + im * im) / (0.5 * (b - a));
}

/** Echo only, steady transport, one head, no feedback. */
static EchoParams plain(int head = 0) {
    EchoParams p;
    p.wobble = p.noise = p.reverb = p.intensity = 0.0;
    p.echo = 100.0;
    p.width = 0.0;
    p.dry = false;
    for (int k = 0; k < kHeads; ++k) p.head[k] = k == head;
    return p;
}

static void testHeadTimes(double fs) {
    for (int k = 0; k < kHeads; ++k) {
        TapeEcho m;
        m.prepare(fs);
        EchoParams p = plain(k);
        p.time = 0.110;
        m.setParams(p);
        m.reset();
        Buf l = burst(fs, 1.0, 0.05), r = l;
        run(m, l, r);
        double at = 0;
        const double pk = peak(l, fs, 0.0, 1.0, &at);
        const double delay = (at - 0.110) * 1000.0; // the burst peaks at 0.110 s
        char name[64];
        std::snprintf(name, sizeof(name), "head %d time at %.0f kHz", k + 1, fs / 1000);
        check(std::fabs(delay - 110.0 * (k + 1)) < 1.5, name, "%.1f ms, wanted %.0f", delay, 110.0 * (k + 1));
        if (k == 0) check(std::fabs(linToDb(pk / 0.05)) < 1.5, "echo at 100% is as loud as the input", "%+.2f dB", linToDb(pk / 0.05));
    }
}

static void testFeedback() {
    const double fs = 48000;
    TapeEcho m;
    m.prepare(fs);
    EchoParams p = plain(0);
    p.time = 0.2;
    m.setParams(p);
    m.reset();
    Buf l = burst(fs, 1.2, 0.05), r = l;
    run(m, l, r);
    check(peak(l, fs, 0.45, 1.2) < 1e-5, "no feedback: one repeat only", "second repeat %.1f dB", linToDb(peak(l, fs, 0.45, 1.2) / 0.05));

    p.intensity = 30.0;
    m.setParams(p);
    m.reset();
    l = burst(fs, 1.2, 0.05), r = l;
    run(m, l, r);
    const double e1 = peak(l, fs, 0.25, 0.4), e2 = peak(l, fs, 0.45, 0.6), e3 = peak(l, fs, 0.65, 0.8);
    check(std::fabs(e2 / e1 - 0.48) < 0.06 && std::fabs(e3 / e2 - 0.48) < 0.06, "intensity 30%: each repeat about half", "%.2f, %.2f", e2 / e1, e3 / e2);

    // Stable setting: dies away. Past the limit: keeps going by itself, held by the tape.
    for (double intensity : {55.0, 100.0}) {
        p.intensity = intensity;
        m.setParams(p);
        m.reset();
        l = burst(fs, 30.0, 0.05), r = l;
        run(m, l, r);
        const double tail = rms(l, fs, 28.0, 30.0), top = peak(l, fs, 0.0, 30.0);
        if (intensity < 60) check(tail < 1e-4, "intensity 55%: dies away", "%.1f dB after 28 s", linToDb(tail));
        else check(tail > 0.05 && top < 1.5, "intensity 100%: sustains, bounded", "rms %.1f dB, peak %.1f dB", linToDb(tail), linToDb(top));
    }

    // Three heads take off sooner than one.
    for (int heads : {1, 3}) {
        p = plain(0);
        p.time = 0.1;
        p.head[1] = p.head[2] = heads == 3;
        p.intensity = 50.0;
        m.setParams(p);
        m.reset();
        l = burst(fs, 20.0, 0.05), r = l;
        run(m, l, r);
        const double tail = rms(l, fs, 18.0, 20.0);
        if (heads == 1) check(tail < 1e-4, "intensity 50%, one head: dies away", "%.1f dB", linToDb(tail));
        else check(tail > 0.02, "intensity 50%, three heads: sustains", "%.1f dB", linToDb(tail));
    }
}

static void testSaturation() {
    const double fs = 48000;
    double gain[2], thd[2];
    int n = 0;
    for (double level : {-30.0, 0.0}) { // dB on the tape meter
        TapeEcho m;
        m.prepare(fs);
        EchoParams p = plain(0);
        m.setParams(p);
        m.reset();
        const double amp = 0.5 * kTapeLevel * dbToLin(level);
        Buf l(static_cast<size_t>(fs)), r;
        for (size_t i = 0; i < l.size(); ++i) l[i] = static_cast<float>(amp * std::sin(2 * kPi * 400.0 * i / fs));
        r = l;
        run(m, l, r);
        const double fund = tone(l, fs, 0.5, 1.0, 400.0);
        gain[n] = linToDb(fund / amp);
        thd[n] = 100.0 * tone(l, fs, 0.5, 1.0, 1200.0) / fund;
        if (level == 0.0) check(std::fabs(m.meters.tape.load() - 0.0) < 0.3, "tape meter reads 0 for that sine", "%.2f dB", m.meters.tape.load());
        ++n;
    }
    check(thd[0] < 0.1 && std::fabs(gain[0]) < 0.3, "quiet signal: clean, unity", "%.3f%% third, %+.2f dB", thd[0], gain[0]);
    check(thd[1] > 1.0 && thd[1] < 4.0, "0 on the tape meter: a few % third harmonic", "%.2f%%, %+.2f dB", thd[1], gain[1]);

    // Driven hard, the echo cannot pass the tape level by much.
    TapeEcho m;
    m.prepare(fs);
    EchoParams p = plain(0);
    p.input = 24.0;
    m.setParams(p);
    m.reset();
    Buf l(static_cast<size_t>(fs)), r;
    for (size_t i = 0; i < l.size(); ++i) l[i] = static_cast<float>(0.5 * std::sin(2 * kPi * 400.0 * i / fs));
    r = l;
    run(m, l, r);
    check(peak(l, fs, 0.5, 1.0) < 1.3 * kTapeLevel, "+24 dB input: held near the tape level", "peak %.2f dBFS", linToDb(peak(l, fs, 0.5, 1.0)));
}

/** Gain of the echo at a frequency for a head time, in dB. */
static double echoGain(double fs, double time, double f, double bass = 0, double treble = 0) {
    TapeEcho m;
    m.prepare(fs);
    EchoParams p = plain(0);
    p.time = time;
    p.bass = bass;
    p.treble = treble;
    m.setParams(p);
    m.reset();
    Buf l(static_cast<size_t>(1.5 * fs)), r;
    for (size_t i = 0; i < l.size(); ++i) l[i] = static_cast<float>(0.01 * std::sin(2 * kPi * f * i / fs));
    r = l;
    run(m, l, r);
    return linToDb(tone(l, fs, 0.9, 1.5, f) / 0.01);
}

static void testTone() {
    const double fs = 48000;
    const double fast = echoGain(fs, 0.05, 6000), mid = echoGain(fs, 0.125, 6000), slow = echoGain(fs, 0.4, 6000);
    check(fast > mid + 2 && mid > slow + 6, "longer echo is darker (6 kHz)", "%.1f / %.1f / %.1f dB", fast, mid, slow);
    check(echoGain(fs, 0.125, 40) < -8.0 && std::fabs(echoGain(fs, 0.125, 400)) < 0.5, "lows roll off, mids flat", "40 Hz %.1f dB, 400 Hz %+.2f dB",
          echoGain(fs, 0.125, 40), echoGain(fs, 0.125, 400));
    const double b = echoGain(fs, 0.125, 100, 6, 0) - echoGain(fs, 0.125, 100), t = echoGain(fs, 0.125, 4000, 0, -6) - echoGain(fs, 0.125, 4000);
    check(std::fabs(b - 5.0) < 1.5 && std::fabs(t + 5.0) < 1.5, "bass +6 at 100 Hz, treble -6 at 4 kHz", "%+.1f dB, %+.1f dB", b, t);
    const double a44 = echoGain(44100, 0.125, 3000), a96 = echoGain(96000, 0.125, 3000);
    check(std::fabs(a44 - a96) < 0.5, "same tone at 44.1 and 96 kHz (3 kHz)", "%.2f / %.2f dB", a44, a96);
}

static void testReverb(double fs) {
    TapeEcho m;
    m.prepare(fs);
    EchoParams p = plain(0);
    p.echo = 0.0;
    p.reverb = 100.0;
    p.width = 100.0;
    m.setParams(p);
    m.reset();

    // Steady noise: how loud is the reverb against its input?
    Buf l(static_cast<size_t>(8.0 * fs), 0.0f), r;
    uint32_t s = 12345;
    for (size_t i = 0; i < static_cast<size_t>(3.0 * fs); ++i) {
        s = s * 1664525u + 1013904223u;
        l[i] = 0.1f * (static_cast<int32_t>(s) / 2147483648.0f);
    }
    r = l;
    const double inRms = rms(l, fs, 1.0, 3.0);
    run(m, l, r);
    // The springs carry 140 Hz to about 4 kHz: compare with that part of the noise.
    const double level = linToDb(rms(l, fs, 1.5, 3.0) / (inRms * std::sqrt(3900.0 / (0.5 * fs))));
    char name[64];
    std::snprintf(name, sizeof(name), "reverb level at %.0f kHz", fs / 1000);
    check(std::fabs(level) < 3.0, name, "%+.1f dB against the input in its band", level);

    // Decay after the noise stops at 3 s.
    const double start = rms(l, fs, 3.0, 3.1);
    double t60 = 0;
    for (double t = 3.1; t < 7.9; t += 0.05)
        if (rms(l, fs, t, t + 0.1) < start * 0.001) { t60 = t - 3.05; break; }
    std::snprintf(name, sizeof(name), "reverb decay at %.0f kHz", fs / 1000);
    check(t60 > 1.6 && t60 < 3.4, name, "%.2f s to -60 dB", t60);

    double same = 0, both = 0;
    for (size_t i = static_cast<size_t>(fs); i < static_cast<size_t>(3 * fs); ++i) { same += static_cast<double>(l[i]) * r[i]; both += static_cast<double>(l[i]) * l[i]; }
    if (fs == 48000) check(same / both < 0.8, "width 100%: left and right springs differ", "correlation %.2f", same / both);

    // The boing: after a click, the lows arrive before the highs.
    m.reset();
    Buf cl(static_cast<size_t>(0.5 * fs), 0.0f), cr;
    cl[100] = 1.0f;
    cr = cl;
    run(m, cl, cr);
    const auto arrival = [&](double f) {
        double best = 0, at = 0;
        for (double t = 0.0; t < 0.08; t += 0.001) {
            const double v = tone(cl, fs, t, t + 0.006, f);
            if (v > best) { best = v; at = t; }
        }
        return at * 1000.0;
    };
    if (fs == 48000) check(arrival(3200) > arrival(600) + 8.0, "spring sweeps upward", "600 Hz at %.0f ms, 3.2 kHz at %.0f ms", arrival(600), arrival(3200));
}

static void testStereo() {
    const double fs = 48000;
    TapeEcho m;
    m.prepare(fs);
    EchoParams p = plain(0);
    p.width = 100.0;
    p.head[2] = true;
    p.time = 0.1;
    m.setParams(p);
    m.reset();
    Buf l = burst(fs, 0.6, 0.05), r = l;
    run(m, l, r);
    const double l1 = peak(l, fs, 0.15, 0.3), r1 = peak(r, fs, 0.15, 0.3), l3 = peak(l, fs, 0.35, 0.55), r3 = peak(r, fs, 0.35, 0.55);
    check(l1 > 0.03 && r1 < 1e-4 && r3 > 0.02 && l3 < 1e-4, "width 100%: head 1 left, head 3 right", "L %.3f/%.3f  R %.3f/%.3f", l1, l3, r1, r3);

    p.width = 0.0;
    p.reverb = 60.0;
    m.setParams(p);
    m.reset();
    l = burst(fs, 0.6, 0.05), r = l;
    run(m, l, r);
    double diff = 0;
    for (size_t i = 0; i < l.size(); ++i) diff = std::max(diff, static_cast<double>(std::fabs(l[i] - r[i])));
    check(diff < 1e-6, "width 0%: mono", "largest difference %.1e", diff);

    // Dry passes left and right untouched; with dry off and nothing else on, silence.
    p = plain(0);
    p.echo = 0.0;
    p.dry = true;
    m.setParams(p);
    m.reset();
    l = burst(fs, 0.3, 0.3);
    r.assign(l.size(), 0.0f);
    const Buf ref = l;
    run(m, l, r);
    diff = 0;
    for (size_t i = 0; i < l.size(); ++i) diff = std::max(diff, static_cast<double>(std::fabs(l[i] - ref[i]) + std::fabs(r[i])));
    check(diff < 1e-6, "dry: left and right pass unchanged", "largest difference %.1e", diff);
}

/** Largest pitch deviation of the echo of a steady sine, in cents. */
static double pitchWander(double wobble) {
    const double fs = 48000, f = 1200.0;
    TapeEcho m;
    m.prepare(fs);
    EchoParams p = plain(1);
    p.time = 0.15;
    p.wobble = wobble;
    m.setParams(p);
    m.reset();
    Buf l(static_cast<size_t>(8.0 * fs)), r;
    for (size_t i = 0; i < l.size(); ++i) l[i] = static_cast<float>(0.02 * std::sin(2 * kPi * f * i / fs));
    r = l;
    run(m, l, r);
    // Phase against the reference, from which the frequency follows.
    const size_t hop = static_cast<size_t>(0.02 * fs);
    double last = 0, most = 0;
    bool have = false;
    for (size_t a = static_cast<size_t>(fs); a + hop < l.size(); a += hop) {
        double re = 0, im = 0;
        for (size_t i = a; i < a + hop; ++i) {
            re += l[i] * std::cos(2 * kPi * f * static_cast<double>(i) / fs);
            im += l[i] * std::sin(2 * kPi * f * static_cast<double>(i) / fs);
        }
        const double ph = std::atan2(im, re);
        if (have) {
            double d = ph - last;
            while (d > kPi) d -= 2 * kPi;
            while (d < -kPi) d += 2 * kPi;
            const double df = -d / (2 * kPi * 0.02);
            most = std::max(most, std::fabs(1200.0 * std::log2((f + df) / f)));
        }
        last = ph;
        have = true;
    }
    return most;
}

static void testTransport() {
    const double steady = pitchWander(0), some = pitchWander(25), lots = pitchWander(100);
    check(steady < 0.2, "wobble 0%: steady pitch", "%.2f cents", steady);
    check(some > 0.5 && some < 6.0, "wobble 25%: slight wander", "%.1f cents", some);
    check(lots > 15.0 && lots < 60.0, "wobble 100%: seasick", "%.1f cents", lots);

    // Turning the time knob bends what is on the tape and never plays it backwards or clicks.
    const double fs = 48000;
    TapeEcho m;
    m.prepare(fs);
    EchoParams p = plain(2);
    p.time = 0.4;
    m.setParams(p);
    m.reset();
    Buf l(static_cast<size_t>(14.0 * fs)), r;
    for (size_t i = 0; i < l.size(); ++i) l[i] = static_cast<float>(0.02 * std::sin(2 * kPi * 300.0 * i / fs));
    r = l;
    const size_t half = l.size() / 2, third = l.size() / 6;
    m.process(&l[0], &r[0], static_cast<int>(half));
    p.time = 0.04;
    m.setParams(p);
    m.process(&l[half], &r[half], static_cast<int>(third));
    p.time = 0.4;
    m.setParams(p);
    m.process(&l[half + third], &r[half + third], static_cast<int>(l.size() - half - third));
    double jump = 0, top = 0;
    bool finite = true;
    for (size_t i = half; i < l.size(); ++i) {
        finite = finite && std::isfinite(l[i]);
        top = std::max(top, static_cast<double>(std::fabs(l[i])));
        jump = std::max(jump, static_cast<double>(std::fabs(l[i] - l[i - 1])));
    }
    check(finite && top < 0.03 && jump < 0.012, "time 400 -> 40 -> 400 ms: glides, no clicks", "peak %.4f, largest step %.4f", top, jump);
    check(std::fabs(m.meters.headTime.load() - 400.0f) < 40.0f, "head time meter follows the motor", "%.0f ms", m.meters.headTime.load());
    const double settled = tone(l, fs, 13.5, 14.0, 300.0);
    check(std::fabs(linToDb(settled / 0.02)) < 1.0, "pitch back to normal once settled", "300 Hz at %+.2f dB", linToDb(settled / 0.02));
}

static void testNoise() {
    for (double fs : {44100.0, 96000.0}) {
        TapeEcho m;
        m.prepare(fs);
        EchoParams p = plain(0);
        p.noise = 50.0;
        m.setParams(p);
        m.reset();
        Buf l(static_cast<size_t>(2.0 * fs), 0.0f), r = l;
        run(m, l, r);
        const double db = linToDb(rms(l, fs, 1.0, 2.0) / kTapeLevel);
        char name[64];
        std::snprintf(name, sizeof(name), "hiss at 50%%, %.0f kHz", fs / 1000);
        check(db < -57.0 && db > -63.0, name, "%.1f dB under the tape level", db);
    }
    TapeEcho m;
    m.prepare(48000);
    EchoParams p = plain(0);
    p.reverb = 50;
    m.setParams(p);
    m.reset();
    Buf l(48000, 0.0f), r = l;
    run(m, l, r);
    check(peak(l, 48000, 0, 1) == 0.0, "noise off, no input: silence", "peak %.1e", peak(l, 48000, 0, 1));
}

static void testExtremes() {
    bool finite = true;
    double top = 0;
    for (double fs : {44100.0, 192000.0}) {
        TapeEcho m;
        m.prepare(fs);
        EchoParams p;
        p.input = 24; p.intensity = 100; p.echo = 100; p.reverb = 100; p.bass = 10; p.treble = 10;
        p.wobble = 100; p.noise = 100; p.width = 100;
        p.head[0] = p.head[1] = p.head[2] = true;
        for (double time : {0.02, 0.5}) {
            p.time = time;
            m.setParams(p);
            Buf l(static_cast<size_t>(4.0 * fs)), r;
            uint32_t s = 99;
            for (auto& v : l) { s = s * 1664525u + 1013904223u; v = static_cast<int32_t>(s) / 2147483648.0f; }
            r = l;
            run(m, l, r);
            for (float v : l) { finite = finite && std::isfinite(v); top = std::max(top, static_cast<double>(std::fabs(v))); }
        }
    }
    check(finite && top < 6.0, "everything at maximum stays finite", "peak %.1f dBFS", linToDb(top));
}

static void testSpeed() {
    const double fs = 48000;
    TapeEcho m;
    m.prepare(fs);
    EchoParams p;
    p.head[0] = p.head[2] = true;
    p.reverb = 50;
    p.noise = 30;
    m.setParams(p);
    Buf l(static_cast<size_t>(10.0 * fs)), r;
    for (size_t i = 0; i < l.size(); ++i) l[i] = static_cast<float>(0.2 * std::sin(2 * kPi * 220.0 * i / fs));
    r = l;
    const std::clock_t t0 = std::clock();
    run(m, l, r);
    const double seconds = static_cast<double>(std::clock() - t0) / CLOCKS_PER_SEC;
    check(seconds < 1.0, "speed", "%.1fx real time", 10.0 / seconds);
}

int main() {
    for (double fs : {44100.0, 48000.0, 96000.0}) testHeadTimes(fs);
    testFeedback();
    testSaturation();
    testTone();
    for (double fs : {44100.0, 48000.0, 96000.0}) testReverb(fs);
    testStereo();
    testTransport();
    testNoise();
    testExtremes();
    testSpeed();
    std::printf("%s\n", failures ? "FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
