// Offline measurements of the channel spacer. Build and run with tests/run.sh.
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

#include "../dsp/Spacer.hpp"

using namespace hnl;

static int failures = 0;

static void check(bool ok, const std::string& name, const char* fmt, double a = 0, double b = 0, double c = 0, double d = 0) {
    std::printf("%s  %-44s ", ok ? "PASS" : "FAIL", name.c_str());
    std::printf(fmt, a, b, c, d);
    std::printf("\n");
    if (!ok) ++failures;
}

typedef std::vector<float> Buf;
const double kFs = 48000.0;

static Buf sine(double f, double amp, double seconds, double fs = kFs) {
    Buf x(static_cast<size_t>(seconds * fs));
    for (size_t i = 0; i < x.size(); ++i) x[i] = static_cast<float>(amp * std::sin(2 * kPi * f * i / fs));
    return x;
}

static Buf mix(const Buf& a, const Buf& b) {
    Buf x(a);
    for (size_t i = 0; i < x.size(); ++i) x[i] += b[i];
    return x;
}

static Buf noise(double amp, double seconds) {
    Buf x(static_cast<size_t>(seconds * kFs));
    uint32_t s = 12345;
    for (float& v : x) {
        s = s * 1664525u + 1013904223u;
        v = static_cast<float>(amp * (static_cast<int32_t>(s) / 2147483648.0));
    }
    return x;
}

/** Runs main (same on both channels) against a mono ducker; returns the left output. */
static Buf run(Spacer& m, const Buf& main, const Buf* ducker, Buf* right = nullptr) {
    Buf l(main), r(main);
    const int block = 250; // not a multiple of the control step
    for (size_t i = 0; i < l.size(); i += block) {
        const int n = static_cast<int>(std::min<size_t>(block, l.size() - i));
        m.process(&l[i], &r[i], ducker ? &(*ducker)[i] : nullptr, ducker ? &(*ducker)[i] : nullptr, n);
    }
    if (right) *right = r;
    return l;
}

/** Level (dB) of one frequency in x between two times. */
static double toneDb(const Buf& x, double f, double from, double to) {
    double re = 0, im = 0;
    const size_t a = static_cast<size_t>(from * kFs), b = std::min(x.size(), static_cast<size_t>(to * kFs));
    for (size_t i = a; i < b; ++i) {
        re += x[i] * std::cos(2 * kPi * f * i / kFs);
        im += x[i] * std::sin(2 * kPi * f * i / kFs);
    }
    return 20 * std::log10(2 * std::hypot(re, im) / (b - a) + 1e-12);
}

static void setup(Spacer& m, const SpacerParams& p, double fs = kFs) {
    m.prepare(fs);
    m.setParams(p);
}

int main() {
    SpacerParams def;

    { // No ducker: the output is the input.
        Spacer m; setup(m, def);
        const Buf in = noise(0.5, 1.0);
        const Buf out = run(m, in, nullptr);
        double worst = 0;
        for (size_t i = 0; i < in.size(); ++i) worst = std::max(worst, std::fabs(static_cast<double>(out[i]) - in[i]));
        check(worst == 0.0, "silent ducker: output equals input", "largest difference %g", worst);
    }

    { // Equal cuts in every band turn the whole signal down evenly.
        Spacer m; setup(m, def);
        for (double cut : {6.0, 12.0, 20.0}) {
            m.forceCut(cut);
            double lo = 0, hi = -200;
            for (double f = 60; f < 12000; f *= 1.03) {
                const double g = m.gainDbAt(f);
                lo = std::min(lo, g);
                hi = std::max(hi, g);
            }
            check(hi < -cut + 1.5 && lo > -cut - 2.5, "even cut of " + std::to_string(static_cast<int>(cut)) + " dB, 60 Hz to 12 kHz", "between %.2f and %.2f dB", lo, hi);
        }
        m.forceCut(12.0);
        check(m.gainDbAt(25) < -6 && m.gainDbAt(18000) < -6, "and still most of it at the ends", "25 Hz %.1f dB, 18 kHz %.1f dB", m.gainDbAt(25), m.gainDbAt(18000));
    }

    const Buf main3 = mix(mix(sine(100, 0.2, 2.0), sine(1000, 0.2, 2.0)), sine(8000, 0.2, 2.0));
    const double ref = -13.98; // 0.2

    { // A ducker at 1 kHz makes room at 1 kHz and leaves the rest.
        Spacer m; setup(m, def);
        const Buf duck = sine(1000, 0.25, 2.0);
        const Buf out = run(m, main3, &duck);
        const double c100 = toneDb(out, 100, 1, 2) - ref, c1k = toneDb(out, 1000, 1, 2) - ref, c8k = toneDb(out, 8000, 1, 2) - ref;
        check(c1k < -5.0 && c1k > -9.5, "ducker at 1 kHz cuts 1 kHz", "%.2f dB", c1k);
        check(c100 > -0.3 && c8k > -0.3, "and leaves 100 Hz and 8 kHz", "%.2f and %.2f dB", c100, c8k);
        check(std::fabs(m.gainDbAt(1000) - c1k) < 0.3, "predicted gain matches the measured one", "%.2f against %.2f dB", m.gainDbAt(1000), c1k);
    }

    { // A broad ducker reaches the full depth.
        SpacerParams p = def;
        p.depth = 12;
        p.sensitivity = 100;
        Spacer m; setup(m, p);
        const Buf duck = noise(0.5, 2.0);
        const Buf out = run(m, main3, &duck);
        const double c100 = toneDb(out, 100, 1, 2) - ref, c1k = toneDb(out, 1000, 1, 2) - ref, c8k = toneDb(out, 8000, 1, 2) - ref;
        check(c1k < -8 && c1k > -13 && c8k < -8 && c8k > -13 && c100 < -4, "noise as ducker cuts everywhere", "%.1f, %.1f, %.1f dB", c100, c1k, c8k);
    }

    { // Below the threshold nothing happens.
        SpacerParams p = def;
        p.sensitivity = 20; // threshold -24 dB
        Spacer m; setup(m, p);
        const Buf duck = sine(1000, 0.03, 2.0); // -30 dB
        const Buf out = run(m, main3, &duck);
        check(std::fabs(toneDb(out, 1000, 1, 2) - ref) < 0.01, "quiet ducker under the threshold: no cut", "%.3f dB", toneDb(out, 1000, 1, 2) - ref);
    }

    { // Link: the whole signal follows the ducker.
        SpacerParams p = def;
        p.link = 100;
        Spacer m; setup(m, p);
        const Buf duck = sine(1000, 0.25, 2.0);
        const Buf out = run(m, main3, &duck);
        const double c100 = toneDb(out, 100, 1, 2) - ref, c1k = toneDb(out, 1000, 1, 2) - ref, c8k = toneDb(out, 8000, 1, 2) - ref;
        check(std::fabs(c100 - c1k) < 1.0 && std::fabs(c8k - c1k) < 1.0 && c1k < -7, "link 100%: every band cut alike", "%.1f, %.1f, %.1f dB", c100, c1k, c8k);
    }

    { // The range controls.
        SpacerParams p = def;
        p.link = 100;
        p.range[0] = 0;
        p.range[3] = 200;
        Spacer m; setup(m, p);
        const Buf duck = sine(1000, 0.25, 2.0);
        const Buf out = run(m, main3, &duck);
        const double c100 = toneDb(out, 100, 1, 2) - ref, c1k = toneDb(out, 1000, 1, 2) - ref, c8k = toneDb(out, 8000, 1, 2) - ref;
        check(c100 > -1.0 && c8k < c1k - 3, "low at 0% and high at 200%", "%.1f, %.1f, %.1f dB", c100, c1k, c8k);
    }

    { // Attack and release.
        SpacerParams p = def;
        p.attack = 10;
        p.release = 200;
        Spacer m; setup(m, p);
        Buf duck = sine(1000, 0.25, 3.0);
        for (size_t i = 0; i < duck.size(); ++i)
            if (i < 0.5 * kFs || i > 1.5 * kFs) duck[i] = 0.0f;
        const Buf main = sine(1000, 0.2, 3.0);
        Buf l(main), r(main);
        double full = 0, tAttack = -1, tRelease = -1;
        std::vector<double> cut;
        for (size_t i = 0; i < l.size(); i += 16) {
            m.process(&l[i], &r[i], &duck[i], &duck[i], 16);
            cut.push_back(m.cutDb(16));
        }
        const int band = 16; // near 1 kHz
        (void)band;
        full = cut[static_cast<size_t>(1.4 * kFs / 16)];
        for (size_t i = 0; i < cut.size(); ++i) {
            const double t = i * 16 / kFs;
            if (tAttack < 0 && t > 0.5 && cut[i] > 0.63 * full) tAttack = (t - 0.5) * 1000;
            if (tRelease < 0 && t > 1.5 && cut[i] < 0.37 * full) tRelease = (t - 1.5) * 1000;
        }
        check(tAttack > 4 && tAttack < 25, "attack 10 ms: two thirds of the cut after", "%.1f ms", tAttack);
        check(tRelease > 150 && tRelease < 300, "release 200 ms: two thirds back after", "%.1f ms", tRelease);
        check(cut.back() == 0.0, "and the cut returns to exactly zero", "%g dB", cut.back());
    }

    { // Self: the signal ducks itself, without a sidechain.
        SpacerParams p = def;
        p.self = true;
        Spacer m; setup(m, p);
        const Buf out = run(m, main3, nullptr);
        check(toneDb(out, 1000, 1, 2) - ref < -3, "self: cuts without a sidechain", "%.2f dB", toneDb(out, 1000, 1, 2) - ref);
    }

    { // Listen: what was removed plus the output is the input.
        SpacerParams p = def;
        Spacer a; setup(a, p);
        p.listen = kListenRemoved;
        Spacer b; setup(b, p);
        p.listen = kListenSidechain;
        Spacer c; setup(c, p);
        const Buf duck = sine(1000, 0.25, 1.0), main = Buf(main3.begin(), main3.begin() + duck.size());
        const Buf out = run(a, main, &duck), removed = run(b, main, &duck), side = run(c, main, &duck);
        double worst = 0, worstSide = 0;
        for (size_t i = 0; i < main.size(); ++i) {
            worst = std::max(worst, std::fabs(static_cast<double>(out[i]) + removed[i] - main[i]));
            worstSide = std::max(worstSide, std::fabs(static_cast<double>(side[i]) - duck[i]));
        }
        check(worst < 1e-6, "listen removed: output + removed = input", "largest difference %g", worst);
        check(worstSide == 0.0, "listen sidechain: plays the ducker", "largest difference %g", worstSide);
    }

    { // Both channels get the same cut; other sample rates behave the same.
        Spacer m; setup(m, def);
        const Buf duck = noise(0.5, 1.0), main = noise(0.3, 1.0);
        Buf right;
        const Buf left = run(m, main, &duck, &right);
        check(left == right, "left and right are treated alike", "%d", 1);
        for (double fs : {44100.0, 96000.0}) {
            Spacer n; setup(n, def, fs);
            n.forceCut(12.0);
            double lo = 0, hi = -200;
            for (double f = 60; f < 12000; f *= 1.05) {
                lo = std::min(lo, n.gainDbAt(f));
                hi = std::max(hi, n.gainDbAt(f));
            }
            check(hi < -10.5 && lo > -14.5, "even 12 dB cut at " + std::to_string(static_cast<int>(fs)) + " Hz", "between %.2f and %.2f dB", lo, hi);
        }
    }

    { // Speed.
        Spacer m; setup(m, def);
        const Buf duck = noise(0.5, 10.0);
        Buf l = noise(0.3, 10.0), r = l;
        const std::clock_t t0 = std::clock();
        for (size_t i = 0; i < l.size(); i += 256) m.process(&l[i], &r[i], &duck[i], &duck[i], static_cast<int>(std::min<size_t>(256, l.size() - i)));
        const double seconds = static_cast<double>(std::clock() - t0) / CLOCKS_PER_SEC;
        bool finite = true;
        for (float v : l) finite = finite && std::isfinite(v);
        check(finite && seconds < 2.0, "10 s of audio", "in %.2f s (%.1f%% of one core)", seconds, seconds * 10);
    }

    std::printf("%s\n", failures ? "FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
