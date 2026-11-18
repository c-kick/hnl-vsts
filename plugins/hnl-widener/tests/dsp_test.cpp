// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// Offline measurements of the widener. Build and run with tests/run.sh.
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

#include "../dsp/Widener.hpp"

using namespace hnl;

static int failures = 0;

static void check(bool ok, const std::string& name, const char* fmt, double a = 0, double b = 0, double c = 0) {
    std::printf("%s  %-56s ", ok ? "PASS" : "FAIL", name.c_str());
    std::printf(fmt, a, b, c);
    std::printf("\n");
    if (!ok) ++failures;
}

static void run(Widener& m, std::vector<float>& l, std::vector<float>& r) {
    const int block = 256;
    for (size_t i = 0; i < l.size(); i += block)
        m.process(&l[i], &r[i], static_cast<int>(std::min<size_t>(block, l.size() - i)));
}

/** A widener that is set up and at rest. */
struct Unit : Widener {
    Unit(double fs, const WidenerParams& p) {
        prepare(fs);
        setParams(p);
        reset();
    }
};

/** Everything that makes side signal off: what goes in comes out. */
static WidenerParams nothing() {
    WidenerParams p;
    p.diffuse = 0.0;
    return p;
}

struct Noise {
    uint32_t s;
    explicit Noise(uint32_t seed) : s(seed) {}
    float next() {
        s = s * 1664525u + 1013904223u;
        return static_cast<float>(static_cast<int32_t>(s)) / 2147483648.0f;
    }
};

static double peak(const std::vector<float>& x, size_t from = 0, size_t to = SIZE_MAX) {
    double m = 0;
    for (size_t i = from; i < std::min(to, x.size()); ++i) m = std::max(m, static_cast<double>(std::fabs(x[i])));
    return m;
}

static double rms(const std::vector<float>& x, size_t from, size_t to) {
    double sum = 0;
    for (size_t i = from; i < to; ++i) sum += static_cast<double>(x[i]) * x[i];
    return std::sqrt(sum / static_cast<double>(to - from));
}
static double rmsDb(const std::vector<float>& x, size_t from, size_t to) { return linToDb(rms(x, from, to)); }

static double correlation(const std::vector<float>& l, const std::vector<float>& r, size_t from, size_t to) {
    double lr = 0, ll = 0, rr = 0;
    for (size_t i = from; i < to; ++i) {
        lr += static_cast<double>(l[i]) * r[i];
        ll += static_cast<double>(l[i]) * l[i];
        rr += static_cast<double>(r[i]) * r[i];
    }
    return lr / std::sqrt(ll * rr + 1e-30);
}

/** A steady tone, the same on both channels (right upside down for `sides`). */
static void tone(std::vector<float>& l, std::vector<float>& r, double fs, double freq, size_t n, double level = 0.25, bool sides = false) {
    l.resize(n);
    r.resize(n);
    for (size_t i = 0; i < n; ++i) {
        l[i] = static_cast<float>(level * std::sin(2.0 * kPi * freq * static_cast<double>(i) / fs));
        r[i] = sides ? -l[i] : l[i];
    }
}

/** How loud a tone in the middle comes out on the left, the right and in mono, against going in (1 = the same). */
static void toneGain(double fs, const WidenerParams& p, double freq, double& left, double& right, double& mono) {
    Unit m(fs, p);
    const size_t n = static_cast<size_t>(fs);
    std::vector<float> l, r;
    tone(l, r, fs, freq, n);
    const std::vector<float> in = l;
    run(m, l, r);
    std::vector<float> sum(n);
    for (size_t i = 0; i < n; ++i) sum[i] = 0.5f * (l[i] + r[i]);
    const size_t from = n / 2, to = from + static_cast<size_t>(std::lround(fs / freq * std::floor(0.4 * freq))); // whole waves
    left = rms(l, from, to) / rms(in, from, to);
    right = rms(r, from, to) / rms(in, from, to);
    mono = rms(sum, from, to) / rms(in, from, to);
}

/** Largest difference between what tones come out with and what the curve says, as a part of the level going in. */
static double againstCurve(double fs, const WidenerParams& p, double* monoWorst = nullptr) {
    const WidenerCurve curve(p, fs);
    double worst = 0, worstMono = 0, gl, gr, gm;
    for (double f : {60.0, 150.0, 333.0, 500.0, 1000.0, 2400.0, 5000.0, 9000.0}) {
        toneGain(fs, p, f, gl, gr, gm);
        const WidenerCurve::Point at = curve.at(f);
        worst = std::max(worst, std::max(std::fabs(gl - dbToLin(at.left)), std::fabs(gr - dbToLin(at.right))));
        worstMono = std::max(worstMono, std::fabs(gm - dbToLin(at.mono)));
    }
    if (monoWorst) *monoWorst = worstMono;
    return worst;
}

/** Positive-going zero crossings per second between two moments. */
static double crossings(const std::vector<double>& x, size_t from, size_t to, double fs) {
    int count = 0;
    for (size_t i = from + 1; i < to; ++i) count += x[i - 1] < 0.0 && x[i] >= 0.0 ? 1 : 0;
    return count * fs / static_cast<double>(to - from);
}

/** The largest step from one sample to the next. */
static double largestStep(const std::vector<float>& x, size_t from, size_t to) {
    double m = 0;
    for (size_t i = from + 1; i < to; ++i) m = std::max(m, static_cast<double>(std::fabs(x[i] - x[i - 1])));
    return m;
}

int main() {
    const double fs = 48000.0;
    std::vector<float> l, r;

    { // Nothing asked for: what goes in comes out.
        Unit m(fs, nothing());
        Noise noise(1);
        std::vector<float> inL(4800), inR(4800);
        for (size_t i = 0; i < 4800; ++i) {
            inL[i] = noise.next();
            inR[i] = noise.next();
        }
        l = inL;
        r = inR;
        run(m, l, r);
        double worst = 0;
        for (size_t i = 0; i < 4800; ++i) worst = std::max(worst, static_cast<double>(std::max(std::fabs(l[i] - inL[i]), std::fabs(r[i] - inR[i]))));
        check(worst == 0.0, "all amounts at 0, width 100%: output is the input", "largest difference %.2g", worst);

        WidenerParams p;
        p.amount = 0.0;
        p.haas = p.diffuse = p.detune = 100.0;
        p.drift = 1.0;
        p.focus = 100.0;
        Unit off(fs, p);
        l = inL;
        r = inR;
        run(off, l, r);
        worst = 0;
        for (size_t i = 0; i < 4800; ++i) worst = std::max(worst, static_cast<double>(std::max(std::fabs(l[i] - inL[i]), std::fabs(r[i] - inR[i]))));
        check(worst < 1e-7, "Amount 0% with everything up: output is the input", "largest difference %.2g", worst);
    }

    { // The promise: left plus right is what went in, whatever is switched on.
        WidenerParams p;
        p.haas = 80.0;
        p.diffuse = 100.0;
        p.detune = 100.0;
        p.cents = 30.0;
        p.width = 170.0;
        p.monoBass = true;
        p.focus = 60.0;
        p.drift = 1.3;
        p.high = 9000.0;
        Unit m(fs, p);
        Noise noise(7);
        const size_t n = 96000;
        std::vector<float> inL(n), inR(n);
        for (size_t i = 0; i < n; ++i) {
            const float burst = i % 12000 < 3000 ? 1.0f : 0.1f;
            inL[i] = 0.4f * burst * noise.next();
            inR[i] = 0.4f * burst * (0.5f * inL[i] + 0.5f * noise.next());
        }
        l = inL;
        r = inR;
        run(m, l, r);
        double worst = 0, moved = 0;
        for (size_t i = 0; i < n; ++i) {
            worst = std::max(worst, std::fabs((static_cast<double>(l[i]) + r[i]) - (static_cast<double>(inL[i]) + inR[i])));
            moved = std::max(moved, static_cast<double>(std::fabs(l[i] - inL[i])));
        }
        check(worst < 1e-6, "everything on, Haas mono safe: left + right is unchanged", "largest difference %.2g", worst);
        check(moved > 0.1, "while each channel on its own is not", "largest difference %.3f", moved);
        check(m.meters.sideOut.load() > m.meters.sideIn.load() + 3.0, "the side meter: more side coming out than going in", "%.1f dB out, %.1f dB in", m.meters.sideOut.load(),
              m.meters.sideIn.load());
    }

    { // Haas, mono safe: the mid comes back late on the sides, added left and taken off right.
        WidenerParams p = nothing();
        p.haas = 100.0;
        p.low = 20.0;
        Unit m(fs, p);
        l.assign(4000, 0.0f);
        r.assign(4000, 0.0f);
        l[100] = r[100] = 1.0f;
        run(m, l, r);
        const double first = designHighpass(fs, 20.0, 0.7071067811865476).b0; // what the range filter lets through at once
        check(l[100] == 1.0f && r[100] == 1.0f && peak(l, 0, 100) == 0.0 && peak(l, 101, 676) == 0.0, "Haas: the click itself is untouched, nothing before 12 ms", "%.4f at once", l[100]);
        check(std::fabs(l[676] - first) < 1e-6 && std::fabs(r[676] + first) < 1e-6, "Haas: 12 ms (576 samples) on, up on the left, down on the right", "%.4f and %.4f", l[676], r[676]);

        for (double rate : {44100.0, 96000.0}) { // The delay is a time, not a number of samples.
            WidenerParams q = p;
            q.haasMs = 10.0;
            Unit other(rate, q);
            l.assign(4000, 0.0f);
            r.assign(4000, 0.0f);
            l[100] = r[100] = 1.0f;
            run(other, l, r);
            const size_t at = 100 + static_cast<size_t>(std::lround(0.01 * rate));
            check(std::fabs(l[at]) > 0.9 && peak(l, 101, at - 2) < 1e-3, "Haas 10 ms at " + std::to_string(static_cast<int>(rate)) + " Hz", "%.4f at sample %.0f", l[at], static_cast<double>(at));
        }
    }

    { // Steady tones follow the curves the UI draws.
        double mono = 0;
        WidenerParams p; // Diffuse at 50%
        double worst = againstCurve(fs, p, &mono);
        check(worst < 0.003, "Default: tones come out as the curves say", "largest difference %.4f", worst);
        check(mono < 1e-6, "Default: the mono curve is flat", "largest difference %.2g", mono);

        p.haas = 70.0;
        p.diffuse = 30.0;
        p.size = 85.0;
        p.amount = 80.0;
        p.high = 6000.0;
        p.haasMs = 10.0; // a whole number of samples at both rates
        worst = againstCurve(fs, p, &mono);
        check(worst < 0.003 && mono < 1e-6, "Haas with Diffuse and Widen Below: as the curves say", "largest difference %.4f, mono %.2g", worst, mono);
        worst = againstCurve(44100.0, p, &mono);
        check(worst < 0.003 && mono < 1e-6, "the same at 44.1 kHz", "largest difference %.4f, mono %.2g", worst, mono);

        p.haasMode = kHaasRight;
        p.haas = 100.0;
        worst = againstCurve(fs, p, &mono);
        check(worst < 0.003 && mono < 0.003, "Haas Right Late: as the curves say, mono included", "largest difference %.4f, mono %.4f", worst, mono);

        // What Right Late does to the right channel alone: late within the range, on time below it.
        WidenerParams q = nothing();
        q.haas = 100.0;
        q.haasMode = kHaasRight;
        q.low = 20.0;
        Unit late(fs, q);
        l.assign(4000, 0.0f);
        r.assign(4000, 0.0f);
        l[100] = r[100] = 1.0f;
        run(late, l, r);
        check(l[100] == 1.0f && peak(l, 101) == 0.0, "Haas Right Late: the left channel is untouched", "%.4f", l[100]);
        check(std::fabs(r[100]) < 0.01 && std::fabs(r[676]) > 0.99, "and the right one comes 12 ms later", "%.4f at once, %.4f late", r[100], r[676]);
        const WidenerCurve curve(q, fs);
        // Left plus right is then the sound plus itself 12 ms late: gone where that is half a wave, whole where it is a whole one.
        check(curve.at(12500.0 / 12.0).mono < -30.0 && std::fabs(curve.at(1000.0).mono) < 0.01, "its mono curve: a hole at 1042 Hz, whole again at 1000 Hz", "%.1f dB and %.3f dB", curve.at(12500.0 / 12.0).mono,
              curve.at(1000.0).mono);
        q.haasMode = kHaasLeft;
        Unit mirrored(fs, q);
        l.assign(4000, 0.0f);
        r.assign(4000, 0.0f);
        l[100] = r[100] = 1.0f;
        run(mirrored, l, r);
        check(r[100] == 1.0f && peak(r, 101) == 0.0 && std::fabs(l[676]) > 0.99, "Haas Left Late: the other way round", "%.4f late on the left", l[676]);
    }

    { // Diffuse: every pitch as loud as it went in, at another moment.
        WidenerParams p;
        p.diffuse = 100.0;
        p.low = 20.0;
        p.listen = kListenSides;
        Unit m(fs, p);
        Noise noise(3);
        const size_t n = 96000;
        l.resize(n);
        for (float& v : l) v = 0.25f * noise.next();
        r = l;
        const std::vector<float> in = l;
        run(m, l, r);
        const double gain = rmsDb(l, n / 2, n) - rmsDb(in, n / 2, n);
        check(std::fabs(gain) < 0.1, "Diffuse 100%, Listen Sides: as loud as the mid", "%.3f dB", gain);
        double opposite = 0;
        for (size_t i = 0; i < n; ++i) opposite = std::max(opposite, static_cast<double>(std::fabs(l[i] + r[i])));
        check(opposite < 1e-7, "Listen Sides: right is left upside down", "largest sum %.2g", opposite);

        p.listen = kListenStereo;
        Unit wide(fs, p);
        l = r = in;
        run(wide, l, r);
        const double full = correlation(l, r, n / 2, n);
        check(std::fabs(full) < 0.1, "Diffuse 100% on mono noise: left and right unrelated", "correlation %.3f", full);
        check(std::fabs(wide.meters.correlation.load() - full) < 0.15, "and the correlation meter agrees", "%.3f", wide.meters.correlation.load());
        check(std::fabs(rmsDb(l, n / 2, n) - rmsDb(r, n / 2, n)) < 0.5, "left and right equally loud", "%.2f dB apart", rmsDb(l, n / 2, n) - rmsDb(r, n / 2, n));
        check(std::fabs(wide.meters.sideOut.load()) < 0.5 && wide.meters.sideIn.load() < -59.0, "side meter: as much side as mid coming out, none going in", "%.2f dB out, %.1f dB in",
              wide.meters.sideOut.load(), wide.meters.sideIn.load());

        p.diffuse = 50.0;
        Unit half(fs, p);
        l = r = in;
        run(half, l, r);
        const double c = correlation(l, r, n / 2, n);
        check(std::fabs(c - 0.6) < 0.08, "Diffuse 50%: correlation near (1 - 0.25) / (1 + 0.25)", "%.3f", c);

        // Size changes how long the mid takes to come through: the time by which half of it has arrived.
        double halfway[2];
        for (int k = 0; k < 2; ++k) {
            p.diffuse = 100.0;
            p.size = k ? 100.0 : 0.0;
            p.listen = kListenSides;
            Unit sized(fs, p);
            l.assign(24000, 0.0f);
            r.assign(24000, 0.0f);
            l[0] = r[0] = 1.0f;
            run(sized, l, r);
            double total = 0, sum = 0;
            for (float v : l) total += static_cast<double>(v) * v;
            size_t at = 0;
            while (at < l.size() && (sum += static_cast<double>(l[at]) * l[at]) < 0.5 * total) ++at;
            halfway[k] = 1000.0 * static_cast<double>(at) / fs;
        }
        check(halfway[0] > 1.0 && halfway[0] < 12.0 && halfway[1] > 5.0 * halfway[0], "Size: half of a click is through in a few ms, or several times that", "%.1f ms at 0%%, %.1f ms at 100%%", halfway[0], halfway[1]);
    }

    { // Detune: one copy higher, one lower, by the cents asked for.
        for (double cents : {9.0, 50.0}) {
            Detuner d;
            d.prepare(fs);
            d.set(cents);
            d.reset();
            const size_t n = 4 * 48000;
            std::vector<double> up(n), down(n);
            for (size_t i = 0; i < n; ++i) d.process(std::sin(2.0 * kPi * 1000.0 * static_cast<double>(i) / fs), up[i], down[i]);
            const double ratio = std::pow(2.0, cents / 1200.0), high = crossings(up, 48000, n, fs), low = crossings(down, 48000, n, fs);
            // Counted over seconds the pitch is off by as much as the two readings take turns: about once a second at 50 cents.
            check(std::fabs(high - 1000.0 * ratio) < 1.5 && std::fabs(low - 1000.0 / ratio) < 1.5, "Detune " + std::to_string(static_cast<int>(cents)) + " cents on 1 kHz: both copies", "%.2f and %.2f Hz, asked %.2f",
                  high, low, 1000.0 * ratio);
            double loudest = 0, quietest = 9;
            for (size_t i = 48000; i + 480 < n; i += 480) { // level per 10 ms
                double sum = 0;
                for (size_t k = i; k < i + 480; ++k) sum += up[k] * up[k];
                loudest = std::max(loudest, std::sqrt(sum / 240.0));
                quietest = std::min(quietest, std::sqrt(sum / 240.0));
            }
            check(loudest < 1.01, "and the higher copy never louder than the tone", "between %.3f and %.3f", quietest, loudest);
        }

        WidenerParams p = nothing();
        p.detune = 100.0;
        Unit m(fs, p);
        Noise noise(5);
        const size_t n = 96000;
        l.resize(n);
        for (float& v : l) v = 0.25f * noise.next();
        r = l;
        run(m, l, r);
        const double c = correlation(l, r, n / 2, n);
        // Not quite none: where one reading hands over to the other, noise loses some of its level.
        check(c > -0.1 && c < 0.4, "Detune 100% on mono noise: left and right have little in common", "correlation %.3f", c);
    }

    { // Drift: the two rows of filters, and what they do to the picture.
        double worst = 0;
        for (double f : {30.0, 100.0, 1000.0, 8000.0, 20000.0}) {
            QuarterWave q;
            double lowest = 9, highest = 0;
            for (int i = 0; i < 48000; ++i) {
                double same, quarter;
                q.process(std::sin(2.0 * kPi * f * i / fs), same, quarter);
                if (i < 24000) continue;
                const double size = std::sqrt(same * same + quarter * quarter); // steady when the two are a quarter of a wave apart
                lowest = std::min(lowest, size);
                highest = std::max(highest, size);
            }
            worst = std::max(worst, highest / lowest - 1.0);
        }
        check(worst < 0.03, "Drift: the two filter rows are a quarter wave apart, 30 Hz to 20 kHz", "worst unevenness %.4f", worst);

        QuarterWave q;
        const size_t n = 4 * 48000;
        std::vector<double> shifted(n);
        double angle = 0;
        for (size_t i = 0; i < n; ++i) {
            double same, quarter;
            q.process(std::sin(2.0 * kPi * 1000.0 * static_cast<double>(i) / fs), same, quarter);
            shifted[i] = same * std::cos(angle) - quarter * std::sin(angle);
            angle += 2.0 * kPi * 4.0 / fs;
        }
        check(std::fabs(crossings(shifted, 48000, n, fs) - 1004.0) < 0.4, "1 kHz moved up by 4 Hz", "%.2f Hz", crossings(shifted, 48000, n, fs));

        // Haas at full with Drift at 1 Hz: a tone in the middle goes from all left to all right and back, once a second.
        WidenerParams p = nothing();
        p.haas = 100.0;
        p.drift = 1.0;
        Unit m(fs, p);
        tone(l, r, fs, 1000.0, 4 * 48000);
        run(m, l, r);
        double lowL = 9, highL = 0, least = 9, most = 0;
        for (size_t i = 48000; i + 480 <= l.size(); i += 480) {
            const double a = rms(l, i, i + 480) / (0.25 * 0.7071), b = rms(r, i, i + 480) / (0.25 * 0.7071);
            lowL = std::min(lowL, a);
            highL = std::max(highL, a);
            least = std::min(least, a * a + b * b);
            most = std::max(most, a * a + b * b);
        }
        check(lowL < 0.1 && highL > 1.9, "Drift on Haas: the left channel swings from nothing to twice the tone", "%.3f to %.3f", lowL, highL);
        check(least > 3.8 && most < 4.2, "while left and right together stay as loud", "%.3f to %.3f", least, most);
    }

    { // Stereo Width and Bass act on the side signal that came in.
        const auto sideGain = [&](const WidenerParams& p, double freq) {
            Unit m(fs, p);
            tone(l, r, fs, freq, 48000, 0.25, true);
            const std::vector<float> in = l;
            run(m, l, r);
            return rmsDb(l, 24000, 48000) - rmsDb(in, 24000, 48000);
        };
        WidenerParams p = nothing();
        p.low = 300.0;
        p.width = 200.0;
        check(std::fabs(sideGain(p, 3000.0) - 6.02) < 0.05, "Stereo Width 200%: a side tone above Widen Above is doubled", "%.2f dB", sideGain(p, 3000.0));
        check(std::fabs(sideGain(p, 30.0)) < 0.15, "and one far below it is left alone", "%.2f dB", sideGain(p, 30.0));
        p.high = 1500.0;
        check(std::fabs(sideGain(p, 12000.0)) < 0.15, "nor is one far above Widen Below", "%.2f dB", sideGain(p, 12000.0));
        p.high = 20000.0;
        p.width = 50.0;
        check(std::fabs(sideGain(p, 3000.0) + 6.02) < 0.01 && std::fabs(sideGain(p, 30.0) + 6.02) < 0.01, "Stereo Width 50%: all of the side signal is halved", "%.2f dB and %.2f dB", sideGain(p, 3000.0),
              sideGain(p, 30.0));
        p.width = 0.0;
        check(sideGain(p, 3000.0) < -200.0, "Stereo Width 0%: no side signal left", "%.0f dB", sideGain(p, 3000.0));
        p.width = 100.0;
        p.monoBass = true;
        check(sideGain(p, 30.0) < -38.0 && std::fabs(sideGain(p, 3000.0)) < 0.1, "Bass Mono: the side signal below Widen Above is gone, the rest stays", "%.1f dB and %.2f dB", sideGain(p, 30.0),
              sideGain(p, 3000.0));

        // A sound in the middle has no side signal of its own: Stereo Width cannot touch it.
        p = nothing();
        p.width = 200.0;
        Unit m(fs, p);
        tone(l, r, fs, 1000.0, 4800);
        const std::vector<float> in = l;
        run(m, l, r);
        double worst = 0;
        for (size_t i = 0; i < l.size(); ++i) worst = std::max(worst, static_cast<double>(std::max(std::fabs(l[i] - in[i]), std::fabs(r[i] - in[i]))));
        check(worst < 1e-7, "Stereo Width 200% on a mono sound: nothing changes", "largest difference %.2g", worst);
    }

    { // Widen Above keeps the lows where they are.
        WidenerParams p;
        p.diffuse = 100.0;
        p.haas = 100.0;
        p.low = 300.0;
        double gl, gr, gm;
        toneGain(fs, p, 40.0, gl, gr, gm);
        check(std::fabs(gl - 1.0) < 0.05 && std::fabs(gr - 1.0) < 0.05, "Widen Above 300 Hz: a 40 Hz tone stays as it is on both sides", "%.3f and %.3f", gl, gr);
    }

    { // Focus: the added side signal steps back on a hit and returns.
        WidenerParams p;
        p.diffuse = 100.0;
        p.focus = 100.0;
        p.listen = kListenSides;
        const size_t n = 48000, start = 14400;
        std::vector<float> in(n, 0.0f);
        for (size_t i = start; i < n; ++i) in[i] = static_cast<float>(0.5 * std::sin(2.0 * kPi * 1000.0 * static_cast<double>(i - start) / fs));
        Unit m(fs, p);
        l = r = in;
        run(m, l, r);
        p.focus = 0.0;
        Unit plain(fs, p);
        std::vector<float> pl = in, pr = in;
        run(plain, pl, pr);
        const double hit = rmsDb(l, start + 48, start + 240) - rmsDb(pl, start + 48, start + 240); // 1 to 5 ms after the start
        const double body = rmsDb(l, start + 14400, start + 19200) - rmsDb(pl, start + 14400, start + 19200); // 300 ms on
        check(hit < -10.0, "Focus 100%: the added side is down in the first 5 ms of a hit", "%.1f dB", hit);
        check(body > -0.3, "and back 300 ms later", "%.2f dB", body);
    }

    { // Switches and stages that come and go do not click: a 100 Hz tone, and the largest step between two samples
      // around the change against the largest one with the settings before or after it left alone.
        const size_t n = 96000, at = 24100; // not at a zero crossing of the tone
        const auto stepAfter = [&](WidenerParams before, WidenerParams after, bool sides = false) {
            before.low = after.low = 20.0;
            double steady = 0;
            for (const WidenerParams& p : {before, after}) {
                Unit alone(fs, p);
                tone(l, r, fs, 100.0, n, 0.25, sides);
                run(alone, l, r);
                steady = std::max(steady, std::max(largestStep(l, n / 2, n), largestStep(r, n / 2, n)));
            }
            Unit m(fs, before);
            tone(l, r, fs, 100.0, n, 0.25, sides);
            m.process(l.data(), r.data(), static_cast<int>(at));
            m.setParams(after);
            m.process(l.data() + at, r.data() + at, static_cast<int>(n - at));
            return std::max(largestStep(l, at - 1, n), largestStep(r, at - 1, n)) / steady;
        };
        WidenerParams a = nothing(), b = nothing();
        a.haas = b.haas = 100.0;
        b.haasMode = kHaasRight;
        check(stepAfter(a, b) < 1.2, "Haas from Mono Safe to Right Late", "%.2f times the steady step", stepAfter(a, b));
        check(stepAfter(b, a) < 1.2, "and back", "%.2f times", stepAfter(b, a));
        b.haasMode = kHaasLeft;
        a.haasMode = kHaasRight;
        check(stepAfter(a, b) < 1.2, "Haas from Right Late to Left Late", "%.2f times", stepAfter(a, b));

        a = nothing();
        for (int which = 0; which < 4; ++which) {
            static const char* const kNames[4] = {"Haas", "Diffuse", "Detune", "Diffuse with Drift"};
            b = nothing();
            (which == 0 ? b.haas : which == 2 ? b.detune : b.diffuse) = 100.0;
            if (which == 3) b.drift = 2.0;
            check(stepAfter(a, b) < 1.2 && stepAfter(b, a) < 1.2, std::string(kNames[which]) + " coming on, and going off", "%.2f and %.2f times", stepAfter(a, b), stepAfter(b, a));
        }
        a = b = WidenerParams();
        a.diffuse = b.diffuse = 100.0;
        b.drift = 2.0;
        check(stepAfter(a, b) < 1.2 && stepAfter(b, a) < 1.2, "Drift coming on under Diffuse, and going off", "%.2f and %.2f times", stepAfter(a, b), stepAfter(b, a));

        a = b = WidenerParams();
        b.listen = kListenMono;
        check(stepAfter(a, b) < 1.2, "Listen from Stereo to Mono", "%.2f times", stepAfter(a, b));
        b.listen = kListenSides;
        check(stepAfter(a, b) < 1.2, "Listen from Stereo to Sides", "%.2f times", stepAfter(a, b));
        a = b = nothing();
        b.monoBass = true;
        b.width = 200.0;
        check(stepAfter(a, b, true) < 1.2, "Bass Mono and Stereo Width 200% coming on, on a side tone", "%.2f times", stepAfter(a, b, true));
        a = b = WidenerParams();
        a.diffuse = b.diffuse = 100.0;
        b.high = 2000.0;
        check(stepAfter(a, b) < 1.2 && stepAfter(b, a) < 1.2, "Widen Below coming on, and going off", "%.2f and %.2f times", stepAfter(a, b), stepAfter(b, a));
    }

    { // A stage whose amount is at zero stops altogether, and starts from nothing.
        WidenerParams p;
        p.haas = p.diffuse = p.detune = 100.0;
        p.haasMs = 40.0;
        p.size = 100.0;
        p.drift = 1.0;
        Unit m(fs, p);
        Noise noise(9);
        std::vector<float> in(48000);
        for (float& v : in) v = 0.5f * noise.next();
        l = r = in;
        run(m, l, r);
        m.setParams(nothing()); // everything off, while the sound goes on
        l = r = in;
        run(m, l, r);
        double worst = 0;
        for (size_t i = 36000; i < in.size(); ++i) worst = std::max(worst, static_cast<double>(std::max(std::fabs(l[i] - in[i]), std::fabs(r[i] - in[i]))));
        check(worst == 0.0, "everything off: 0.75 s later the output is the input again, exactly", "largest difference %.2g", worst);
        m.setParams(p);
        l = r = in;
        run(m, l, r);
        check(std::fabs(l[0] - in[0]) < 0.01 && std::fabs(l[1] - in[1]) < 0.01, "and back on: it starts from nothing", "difference %.4f on the first sample", l[0] - in[0]);
    }

    { // Silence, and the tail after a sound.
        WidenerParams p;
        p.haas = p.diffuse = p.detune = 100.0;
        p.size = 100.0;
        p.drift = 0.5;
        Unit m(fs, p);
        l.assign(48000, 0.0f);
        r.assign(48000, 0.0f);
        run(m, l, r);
        check(peak(l) == 0.0 && peak(r) == 0.0, "silence in, silence out", "peak %.2g", std::max(peak(l), peak(r)));
        check(m.meters.correlation.load() == 1.0f && m.meters.sideOut.load() == -60.0f && m.meters.balance.load() == 0.0f, "and the meters at rest", "correlation %.1f, side %.0f dB, balance %.0f dB",
              m.meters.correlation.load(), m.meters.sideOut.load(), m.meters.balance.load());
        Noise noise(13);
        for (size_t i = 0; i < 24000; ++i) l[i] = r[i] = 0.5f * noise.next();
        run(m, l, r);
        const double soon = peak(l, 24000 + 2400, 24000 + 4800); // 50 to 100 ms after the sound
        l.assign(48000, 0.0f);
        r.assign(48000, 0.0f);
        run(m, l, r);
        check(soon > 1e-4 && peak(l, 24000) < 1e-6, "Diffuse at its largest rings on for a moment; a second later it is silent", "%.2g after 50 ms, %.2g after 1 s", soon, peak(l, 24000));
    }

    { // Output and the meters.
        WidenerParams p = nothing();
        p.output = -6.0;
        Unit m(fs, p);
        tone(l, r, fs, 1000.0, 48000);
        for (float& v : r) v = 0.0f; // left only
        run(m, l, r);
        check(std::fabs(peak(l, 24000) - 0.25 * dbToLin(-6.0)) < 1e-4, "Output -6 dB", "peak %.4f", peak(l, 24000));
        check(m.meters.balance.load() < -50.0, "a sound on the left only: the balance meter is all the way left", "%.1f dB", m.meters.balance.load());
        check(std::fabs(m.meters.sideOut.load()) < 0.1 && std::fabs(m.meters.sideIn.load()) < 0.1, "and it holds as much side as mid", "%.2f dB out, %.2f dB in", m.meters.sideOut.load(), m.meters.sideIn.load());

        p.listen = kListenMono;
        p.output = 0.0;
        Unit mono(fs, p);
        tone(l, r, fs, 1000.0, 48000);
        for (float& v : r) v = 0.0f;
        run(mono, l, r);
        check(std::fabs(peak(l, 24000) - 0.125) < 1e-4 && std::fabs(peak(r, 24000) - 0.125) < 1e-4, "Listen Mono: half of left plus right on both channels", "%.4f and %.4f", peak(l, 24000), peak(r, 24000));
    }

    { // Level Match: as loud coming out as going in, both channels together.
        WidenerParams p;
        p.diffuse = p.detune = 100.0;
        p.low = 20.0;
        p.match = true;
        Unit m(fs, p);
        Noise noise(11);
        const size_t n = 3 * 48000;
        l.resize(n);
        for (float& v : l) v = 0.25f * noise.next();
        r = l;
        const std::vector<float> in = l;
        run(m, l, r);
        const double both = 10.0 * std::log10((std::pow(10.0, 0.1 * rmsDb(l, 48000, n)) + std::pow(10.0, 0.1 * rmsDb(r, 48000, n))) / 2.0) - rmsDb(in, 48000, n);
        check(std::fabs(both) < 0.2, "Level Match: as loud coming out as going in", "%.2f dB", both);
        check(m.meters.match.load() < -2.0 && m.meters.match.load() > -4.5, "its meter shows the gain it applies", "%.2f dB", m.meters.match.load());
        p.listen = kListenSides;
        Unit sides(fs, p);
        l = r = in;
        run(sides, l, r);
        check(std::fabs(sides.meters.match.load() - m.meters.match.load()) < 0.01, "and Listen does not change that gain", "%.2f dB", sides.meters.match.load());
        p.match = false;
        Unit off(fs, p);
        l = r = in;
        run(off, l, r);
        check(off.meters.match.load() == 0.0f, "Level Match off: no gain", "%.2f dB", off.meters.match.load());
    }

    { // Fast enough: a second of audio with everything on, against real time.
        WidenerParams p;
        p.haas = p.diffuse = p.detune = 100.0;
        p.drift = 1.0;
        p.focus = 50.0;
        p.width = 150.0;
        p.high = 8000.0;
        Unit m(fs, p);
        Noise noise(17);
        l.resize(48000 * 10);
        for (float& v : l) v = 0.25f * noise.next();
        r = l;
        timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        run(m, l, r);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        const double seconds = static_cast<double>(t1.tv_sec - t0.tv_sec) + 1e-9 * static_cast<double>(t1.tv_nsec - t0.tv_nsec);
        check(seconds < 1.0, "10 s of audio with everything on", "in %.3f s (%.1f%% of real time)", seconds, 10.0 * seconds);
    }

    std::printf("%s\n", failures ? "FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
