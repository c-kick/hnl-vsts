// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// Offline measurements of HNL Patina. Build and run with tests/run.sh.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../dsp/Patina.hpp"

using namespace hnl;

static int failures = 0;

static void check(bool ok, const std::string& name, const char* fmt, double a = 0, double b = 0, double c = 0) {
    std::printf("%s  %-58s ", ok ? "PASS" : "FAIL", name.c_str());
    std::printf(fmt, a, b, c);
    std::printf("\n");
    if (!ok) ++failures;
}

static const char* const kModule[kPatinaModules] = {"Drive", "Motion", "Vinyl", "Noise", "Filter", "Space"};
static const int kTypes[kPatinaModules] = {4, 4, 4, 4, 3, 4};
static const char* const kTypeName[kPatinaModules][4] = {
    {"Tube", "Transistor", "Diode", "Fuzz"}, {"Off-centre", "Warped", "Slipping Belt", "Tired Motor"},
    {"Dusty", "Scratched", "Worn Groove", "Shellac"}, {"Hum", "Buzz", "Static", "Room Tone"},
    {"Tilt", "Band", "Ladder", ""}, {"Room", "Chamber", "Spring", "Early Digital"}};

struct Stereo {
    std::vector<float> l, r;
    size_t size() const { return l.size(); }
};

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
static double rms(const std::vector<float>& x, size_t from, size_t to) {
    double s = 0;
    for (size_t i = from; i < to; ++i) s += static_cast<double>(x[i]) * x[i];
    return std::sqrt(s / std::max<size_t>(1, to - from));
}
static double peak(const std::vector<float>& x, size_t from, size_t to) {
    double p = 0;
    for (size_t i = from; i < to; ++i) p = std::max(p, static_cast<double>(std::fabs(x[i])));
    return p;
}
static double db(double lin) { return linToDb(lin); }

static Stereo sine(double f, double amp, double fs, double seconds) {
    Stereo s;
    s.l.resize(static_cast<size_t>(seconds * fs));
    for (size_t i = 0; i < s.l.size(); ++i) s.l[i] = static_cast<float>(amp * std::sin(2 * kPi * f * i / fs));
    s.r = s.l;
    return s;
}
static Stereo silence(double fs, double seconds) {
    Stereo s;
    s.l.assign(static_cast<size_t>(seconds * fs), 0.0f);
    s.r = s.l;
    return s;
}
/** Noise that is the same on every run, different left and right. */
static Stereo noise(double amp, double fs, double seconds) {
    Stereo s;
    patina::Rng rng;
    rng.seed(12345u);
    s.l.resize(static_cast<size_t>(seconds * fs));
    s.r.resize(s.l.size());
    for (size_t i = 0; i < s.l.size(); ++i) { s.l[i] = static_cast<float>(amp * rng.bipolar()); s.r[i] = static_cast<float>(amp * rng.bipolar()); }
    return s;
}
/** A tone with a short loud hit `perSecond` times a second. */
static Stereo hits(double fs, double seconds, double perSecond, double quiet = 0.05, double loud = 0.7) {
    Stereo s = sine(180.0, 1.0, fs, seconds);
    for (size_t i = 0; i < s.size(); ++i) {
        const double t = std::fmod(i / fs, 1.0 / perSecond);
        const float g = static_cast<float>(t < 0.03 ? loud : quiet);
        s.l[i] *= g;
        s.r[i] *= g;
    }
    return s;
}
static void append(Stereo& a, const Stereo& b) {
    a.l.insert(a.l.end(), b.l.begin(), b.l.end());
    a.r.insert(a.r.end(), b.r.begin(), b.r.end());
}

/** Everything off, Mix 100%: nothing but the delay. */
static PatinaParams allOff() {
    PatinaParams p;
    p.morph = 0.0;
    for (auto& s : p.scene)
        for (auto& m : s.m) { m.on = false; m.amount = 0.0; m.react = 0.0; m.third = 0.0; }
    for (auto& s : p.scene) s.m[kPatinaSpace].third = 0.8;
    return p;
}
/** One module on in both scenes, the rest off. */
static PatinaParams only(int module, int type, double amount, double react = 0.0, double third = -1e9) {
    PatinaParams p = allOff();
    for (auto& s : p.scene) {
        PatinaModule& m = s.m[module];
        m.on = true; m.type = type; m.amount = amount; m.react = react;
        if (third > -1e8) m.third = third;
    }
    return p;
}

struct Engine : Patina {
    Engine(double fs, const PatinaParams& p) {
        setParams(p);
        prepare(fs);
        setParams(p);
        reset();
    }
};

static void run(Patina& e, Stereo& s, int block = 256) {
    for (size_t i = 0; i < s.size(); i += block)
        e.process(&s.l[i], &s.r[i], static_cast<int>(std::min<size_t>(block, s.size() - i)));
}
static Stereo through(double fs, const PatinaParams& p, Stereo s) {
    Engine e(fs, p);
    run(e, s);
    return s;
}
/** Largest difference between out and in delayed by `late` samples. */
static double differsFromDelayed(const Stereo& out, const Stereo& in, int late) {
    double worst = 0;
    for (size_t i = late; i < out.size(); ++i)
        worst = std::max(worst, static_cast<double>(std::max(std::fabs(out.l[i] - in.l[i - late]), std::fabs(out.r[i] - in.r[i - late]))));
    return worst;
}
static double largestStep(const std::vector<float>& x, size_t from, size_t to) {
    double worst = 0;
    for (size_t i = from + 1; i < to; ++i) worst = std::max(worst, static_cast<double>(std::fabs(x[i] - x[i - 1])));
    return worst;
}

// ---------------------------------------------------------------- latency

static void latency() {
    for (double fs : {44100.0, 48000.0, 96000.0}) {
        Engine e(fs, allOff());
        const int late = e.latency();
        check(late == 36 + static_cast<int>(std::lround(0.012 * fs)), "latency at " + std::to_string(static_cast<int>(fs)) + " Hz: 36 + 12 ms (samples)", "%.0f", late);
        const Stereo in = noise(0.5, fs, 0.5);
        Stereo out = in;
        run(e, out);
        check(differsFromDelayed(out, in, late) == 0.0, "  all modules off: the input, that much later, exactly", "%.3g", differsFromDelayed(out, in, late));
    }
    const double fs = 48000.0;
    { // Everything on and turned up, Mix 0: still the input, as late as reported.
        PatinaParams p;
        for (auto& s : p.scene) {
            for (auto& m : s.m) { m.on = true; m.amount = 80.0; m.react = 50.0; }
            s.m[kPatinaSpace].third = 1.0;
        }
        p.mix = 0.0;
        Engine e(fs, p);
        const Stereo in = noise(0.5, fs, 0.5);
        Stereo out = in;
        run(e, out);
        check(differsFromDelayed(out, in, e.latency()) < 1e-6, "everything on, Mix 0: the input, as late as reported", "%.3g", differsFromDelayed(out, in, e.latency()));
    }
    // At Amount 0 a module passes its input unchanged, whatever its type and its own knob say.
    for (int m = 0; m < kPatinaModules; ++m) {
        double worst = 0;
        for (int type = 0; type < kTypes[m]; ++type)
            for (double third : {0.0, 100.0}) {
                PatinaParams p = only(m, type, 0.0, 100.0, m == kPatinaSpace ? (third > 0 ? 4.0 : 0.2) : (m == kPatinaMotion ? third / 50.0 : third));
                p.idle = 1;
                Engine e(fs, p);
                const Stereo in = noise(0.5, fs, 0.3);
                Stereo out = in;
                run(e, out);
                worst = std::max(worst, differsFromDelayed(out, in, e.latency()));
            }
        check(worst == 0.0, std::string(kModule[m]) + " at Amount 0: the input unchanged, every type", "%.3g", worst);
    }
}

// ----------------------------------------------------------------- motion

static void motion() {
    const double fs = 48000.0;
    static const char* const kSpeed[3] = {"33", "45", "78"};
    // The speed error, read from the delay itself, at Amount 100%.
    for (int type = 0; type < 4; ++type)
        for (int speed = 0; speed < 3; ++speed) {
            Engine e(fs, only(kPatinaMotion, type, 100.0, 0.0, speed));
            Stereo s = sine(1000.0, 0.25, fs, 8.0);
            double most = 0, away = 0;
            for (size_t i = 0; i < s.size(); ++i) {
                e.process(&s.l[i], &s.r[i], 1);
                if (i > fs) most = std::max(most, std::fabs(e.platter().cents));
                away = std::max(away, std::fabs(e.platter().delay[0] - e.platter().centre()) * 1000.0 / fs);
            }
            const double want = type == 2 ? 10.0 : 50.0; // a belt slips slowly: a fifth of the depth
            const bool ok = type == 0 ? std::fabs(most - 50.0) < 2.0 : (most > 0.3 * want && most < want * 1.1);
            check(ok && away <= patina::Platter::kWindowMs, std::string(kTypeName[1][type]) + " at " + kSpeed[speed] + ": largest pitch error (cent), swing (ms)",
                  "%.1f  %.2f", most, away);
        }
    { // The same in the sound: a 1 kHz tone through an off-centre record at 33, read from its zero crossings.
        const Stereo out = through(fs, only(kPatinaMotion, 0, 100.0, 0.0, 0), sine(1000.0, 0.5, fs, 6.0));
        double lo = 1e9, hi = 0, last = -1;
        int count = 0;
        double first = 0;
        for (size_t i = static_cast<size_t>(fs); i + 1 < out.size(); ++i)
            if (out.l[i] < 0 && out.l[i + 1] >= 0) {
                const double at = i + out.l[i] / (out.l[i] - out.l[i + 1]);
                if (count % 20 == 0) { // over 20 cycles
                    if (last >= 0) { const double f = 20.0 * fs / (at - first); lo = std::min(lo, f); hi = std::max(hi, f); }
                    first = at;
                    last = at;
                }
                ++count;
            }
        const double up = 1200.0 * std::log2(hi / 1000.0), down = 1200.0 * std::log2(lo / 1000.0);
        check(std::fabs(up - 50.0) < 4.0 && std::fabs(down + 50.0) < 4.0, "a 1 kHz tone on an off-centre record: highest and lowest pitch (cent)", "%+.1f  %+.1f", up, down);
    }
    { // Hits drag the platter. React 100%: about 25 cent down on a hit, with no other movement.
        PatinaParams p = only(kPatinaMotion, 0, 6.0, 100.0, 0);
        Engine e(fs, p);
        Stereo s = hits(fs, 6.0, 1.0);
        double lowest = 0, bar = 0;
        for (size_t i = 0; i < s.size(); ++i) {
            e.process(&s.l[i], &s.r[i], 1);
            if (i > 2 * fs) lowest = std::min(lowest, e.platter().cents);
            bar = std::max<double>(bar, e.meters.react[kPatinaMotion].load());
        }
        check(lowest < -15.0 && lowest > -32.0, "a hit drags the platter, React 100% (cent)", "%.1f", lowest);
        check(bar > 0.5, "  and Motion's React bar shows it", "%.2f", bar);
        Engine still(fs, only(kPatinaMotion, 0, 6.0, 0.0, 0));
        Stereo t = hits(fs, 6.0, 1.0);
        double low0 = 0;
        for (size_t i = 0; i < t.size(); ++i) { still.process(&t.l[i], &t.r[i], 1); low0 = std::min(low0, still.platter().cents); }
        check(low0 > -4.0, "  and does not at React 0 (cent)", "%.1f", low0);
    }
    { // Hits in a row at full Amount and React, every type and speed: the delay stays inside its window.
        double worst = 0, worstStep = 0;
        for (int type = 0; type < 4; ++type)
            for (int speed = 0; speed < 3; ++speed) {
                Engine e(fs, only(kPatinaMotion, type, 100.0, 100.0, speed));
                Stereo s = hits(fs, 12.0, 8.0);
                double before = e.platter().centre();
                for (size_t i = 0; i < s.size(); ++i) {
                    e.process(&s.l[i], &s.r[i], 1);
                    for (int c = 0; c < 2; ++c) worst = std::max(worst, std::fabs(e.platter().delay[c] - e.platter().centre()) * 1000.0 / fs);
                    if (i > 0) worstStep = std::max(worstStep, std::fabs(e.platter().delay[0] - before));
                    before = e.platter().delay[0];
                }
            }
        check(worst <= patina::Platter::kWindowMs && worstStep < 0.1, "8 hits a second for 12 s: furthest from the centre (ms), largest step (samples)", "%.2f  %.4f", worst, worstStep);
    }
}

// ------------------------------------------------------------------ vinyl

/** The same signal with everything below `fc` taken out. */
static std::vector<float> highs(const std::vector<float>& x, double fs, double fc) {
    Biquad a, b;
    a.set(designHighpass(fs, fc, 0.7071067811865476));
    b.set(designHighpass(fs, fc, 0.7071067811865476));
    std::vector<float> y(x.size());
    for (size_t i = 0; i < x.size(); ++i) y[i] = static_cast<float>(b.process(a.process(x[i])));
    return y;
}

static void vinyl() {
    const double fs = 48000.0;
    static const char* const kSpeed[3] = {"33", "45", "78"};
    // One scratch (Amount under a sixth) clicks once a turn.
    for (int speed = 0; speed < 3; ++speed) {
        PatinaParams p = only(kPatinaVinyl, patina::Vinyl::kScratched, 12.0);
        for (auto& s : p.scene) s.m[kPatinaMotion].third = speed;
        p.idle = 1;
        const Stereo out = through(fs, p, silence(fs, 10.0));
        const double top = peak(out.l, 0, out.size());
        std::vector<double> clicks;
        for (size_t i = 0; i < out.size(); ++i)
            if (std::fabs(out.l[i]) > 0.62 * top && (clicks.empty() || i - clicks.back() > 0.1 * fs)) clicks.push_back(static_cast<double>(i));
        const double turn = 1.0 / patina::turnsPerSecond(speed);
        double off = 0;
        for (size_t k = 1; k < clicks.size(); ++k) off = std::max(off, std::fabs((clicks[k] - clicks[k - 1]) / fs - turn));
        check(clicks.size() >= 4 && off < 0.002, std::string("a scratch at ") + kSpeed[speed] + ": clicks, furthest from one turn apart (s), a turn (s)", "%.0f  %.4f  %.3f",
              static_cast<double>(clicks.size()), off, turn);
    }
    { // Crackle hides under a loud tone.
        double level[2];
        for (int k = 0; k < 2; ++k) {
            PatinaParams p = only(kPatinaVinyl, patina::Vinyl::kDusty, 60.0, k ? 100.0 : 0.0, 0.0);
            const Stereo out = through(fs, p, sine(200.0, 0.5, fs, 6.0));
            const std::vector<float> h = highs(out.l, fs, 2500.0);
            level[k] = rms(h, static_cast<size_t>(2 * fs), h.size());
        }
        const double down = db(level[0] / level[1]);
        check(down > 14.0 && down < 19.0, "crackle under a loud tone, React 100% against 0 (dB down)", "%.1f", down);
    }
    { // In Silence. Stop: there in a gap of two seconds, gone six seconds after the music. Keep: goes on.
        PatinaParams p = only(kPatinaVinyl, patina::Vinyl::kDusty, 60.0, 100.0, 20.0);
        Stereo in = sine(200.0, 0.5, fs, 2.0);
        append(in, silence(fs, 10.0));
        const Stereo stop = through(fs, p, in);
        p.idle = 1;
        const Stereo keep = through(fs, p, in);
        const size_t s = static_cast<size_t>(fs);
        const double gap = rms(stop.l, 3 * s, 4 * s), gone = peak(stop.l, static_cast<size_t>(8.6 * fs), 12 * s), kept = rms(keep.l, 10 * s, 12 * s);
        check(gap > 1e-3 && gone < 1e-5, "In Silence, Stop: crackle in a gap of 2 s (rms), and 6 s after the music (peak)", "%.4f  %.2g", gap, gone);
        check(kept > 1e-3, "In Silence, Keep: crackle 8 s after the music (rms)", "%.4f", kept);
    }
    { // Wear takes the highs away.
        double at[2];
        for (int k = 0; k < 2; ++k) {
            PatinaParams p = only(kPatinaVinyl, patina::Vinyl::kDusty, 100.0, 100.0, k ? 100.0 : 0.0);
            const Stereo out = through(fs, p, sine(8000.0, 0.25, fs, 1.0));
            at[k] = db(tone(out.l, static_cast<size_t>(0.5 * fs), out.size(), 8000.0, fs) / 0.25);
        }
        check(at[0] > -6.0 && at[1] < -15.0, "8 kHz at Amount 100%: Wear 0 and Wear 100% (dB)", "%.1f  %.1f", at[0], at[1]);
    }
}

// ------------------------------------------------------------------ noise

static void noiseBed() {
    const double fs = 48000.0;
    for (int mains = 0; mains < 2; ++mains) { // the hum is at the mains frequency, with its harmonics
        PatinaParams p = only(kPatinaNoise, patina::Noise::kHum, 100.0, 0.0, 40.0);
        p.mains = mains;
        p.idle = 1;
        const Stereo out = through(fs, p, silence(fs, 3.0));
        const double f0 = mains ? 60.0 : 50.0, wrong = mains ? 50.0 : 60.0;
        const size_t from = static_cast<size_t>(fs);
        const double a1 = tone(out.l, from, out.size(), f0, fs), a2 = tone(out.l, from, out.size(), 2 * f0, fs), other = tone(out.l, from, out.size(), wrong, fs);
        check(a1 > 0.02 && a2 > 0.3 * a1 && other < 0.01 * a1, std::string("hum at ") + (mains ? "60" : "50") + " Hz: fundamental and octave (dBFS), the other mains (dB below)", "%.1f  %.1f  %.0f",
              db(a1), db(a2), db(a1 / std::max(other, 1e-12)));
    }
    for (int type = 0; type < 4; ++type) { // every bed is about as loud
        PatinaParams p = only(kPatinaNoise, type, 100.0, 0.0, 50.0);
        p.idle = 1;
        const Stereo out = through(fs, p, silence(fs, 4.0));
        const double level = db(rms(out.l, static_cast<size_t>(fs), out.size())), top = db(peak(out.l, 0, out.size()));
        check(level > -40.0 && level < -12.0 && top < -3.0, std::string(kTypeName[3][type]) + " at Amount 100%: level and peak (dBFS)", "%.1f  %.1f", level, top);
    }
    { // Follow: the bed fades when the music stops. Duck: it is under the music and comes up in the gaps.
        Stereo in = sine(1000.0, 0.5, fs, 3.0);
        append(in, silence(fs, 5.0));
        PatinaParams p = only(kPatinaNoise, patina::Noise::kHum, 100.0, 100.0, 40.0);
        p.idle = 1;
        const Stereo follow = through(fs, p, in);
        for (auto& s : p.scene) s.noiseMode = 1;
        const Stereo duck = through(fs, p, in);
        const size_t s = static_cast<size_t>(fs);
        const double fDuring = tone(follow.l, 1 * s, 3 * s, 100.0, fs), fAfter = tone(follow.l, 6 * s, 8 * s, 100.0, fs);
        const double dDuring = tone(duck.l, 1 * s, 3 * s, 100.0, fs), dAfter = tone(duck.l, 6 * s, 8 * s, 100.0, fs);
        check(fDuring > 0.01 && fAfter < 0.01 * fDuring, "Follow: hum with the music (dBFS), and 3 s after it stopped (dB down)", "%.1f  %.0f", db(fDuring), db(fDuring / std::max(fAfter, 1e-12)));
        check(db(dAfter / dDuring) > 14.0, "Duck: hum in the gap against under the music (dB up)", "%.1f", db(dAfter / dDuring));
    }
}

// ------------------------------------------------------------------ drive

static void drive() {
    const double fs = 48000.0;
    for (int type = 0; type < 4; ++type) {
        double worst = 0, third[2] = {};
        int k = 0;
        for (double amount : {25.0, 100.0}) {
            const Stereo out = through(fs, only(kPatinaDrive, type, amount), sine(220.0, 0.25, fs, 2.0));
            const size_t from = static_cast<size_t>(fs);
            worst = std::max(worst, std::fabs(db(rms(out.l, from, out.size()) / (0.25 / std::sqrt(2.0)))));
            third[k++] = db((tone(out.l, from, out.size(), 660.0, fs) + tone(out.l, from, out.size(), 440.0, fs)) / 0.25);
        }
        check(worst < 0.5 && (type == patina::Drive::kFuzz ? third[0] > -14.0 : third[1] > third[0] + 6.0) && third[1] > -30.0, std::string(kTypeName[0][type]) + ": level against the input (dB off), harmonics at 25% and 100% (dB)",
              "%.2f  %.1f  %.1f", worst, third[0], third[1]);
    }
    { // Loud playing is pushed harder: after a jump in level the harmonics are stronger with React.
        double harm[2];
        for (int k = 0; k < 2; ++k) {
            Stereo in = sine(220.0, 0.05, fs, 3.0);
            const Stereo loud = sine(220.0, 0.2, fs, 0.2);
            Stereo all = in;
            append(all, loud);
            Engine e(fs, only(kPatinaDrive, 0, 30.0, k ? 100.0 : 0.0));
            run(e, all);
            const size_t from = in.size() + static_cast<size_t>(0.05 * fs);
            harm[k] = db(tone(all.l, from, all.size(), 660.0, fs) / tone(all.l, from, all.size(), 220.0, fs));
        }
        check(harm[1] > harm[0] + 3.0, "a jump of 12 dB at Amount 30%: third harmonic at React 0 and 100% (dB)", "%.1f  %.1f", harm[0], harm[1]);
    }
}

// ----------------------------------------------------------------- filter

static void filter() {
    const double fs = 48000.0;
    for (int type = 0; type < 3; ++type) {
        double g[3];
        int k = 0;
        for (double f : {60.0, 1000.0, 8000.0}) {
            const Stereo out = through(fs, only(kPatinaFilter, type, 60.0), sine(f, 0.25, fs, 1.0));
            g[k++] = db(tone(out.l, static_cast<size_t>(0.5 * fs), out.size(), f, fs) / 0.25);
        }
        const bool ok = type == patina::Filter::kTilt ? (g[0] > -1.0 && g[2] < -8.0 && g[2] > -13.0)
                      : type == patina::Filter::kBand ? (g[0] < -6.0 && g[1] > -2.0 && g[2] < -6.0)
                      : (g[0] > -1.0 && g[2] < -24.0);
        check(ok, std::string(kTypeName[4][type]) + " at Amount 60%: 60 Hz, 1 kHz, 8 kHz (dB)", "%.1f  %.1f  %.1f", g[0], g[1], g[2]);
    }
    { // Resonance lifts the corner of the ladder.
        double g[2];
        for (int k = 0; k < 2; ++k) {
            const double fc = 20000.0 * std::pow(0.01, 0.5);
            const Stereo out = through(fs, only(kPatinaFilter, patina::Filter::kLadder, 50.0, 0.0, k ? 80.0 : 0.0), sine(fc, 0.1, fs, 1.0));
            g[k] = db(tone(out.l, static_cast<size_t>(0.5 * fs), out.size(), fc, fs) / 0.1);
        }
        check(g[1] > g[0] + 8.0 && g[1] < 24.0, "Ladder at its corner: Resonance 0 and 80% (dB)", "%.1f  %.1f", g[0], g[1]);
    }
    { // Loud notes open it: a tone 6 dB above what came before is brighter with React.
        double g[2];
        for (int k = 0; k < 2; ++k) {
            Stereo in = sine(4000.0, 0.05, fs, 3.0);
            const size_t quiet = in.size();
            append(in, sine(4000.0, 0.2, fs, 0.15));
            Engine e(fs, only(kPatinaFilter, patina::Filter::kLadder, 50.0, k ? 100.0 : 0.0));
            run(e, in);
            g[k] = db(tone(in.l, quiet + static_cast<size_t>(0.05 * fs), in.size(), 4000.0, fs) / 0.2);
        }
        check(g[1] > g[0] + 6.0, "Ladder, a loud note at 4 kHz: React 0 and 100% (dB)", "%.1f  %.1f", g[0], g[1]);
    }
}

// ------------------------------------------------------------------ space

static void space() {
    const double fs = 48000.0;
    for (int type = 0; type < 4; ++type) {
        // A burst, then silence: how loud the tail is, and that a longer Decay rings longer.
        double tail[2], during = 0;
        for (int k = 0; k < 2; ++k) {
            Stereo in = noise(0.25, fs, 0.5);
            append(in, silence(fs, 3.0));
            const Stereo out = through(fs, only(kPatinaSpace, type, 100.0, 0.0, k ? 3.0 : 0.4), in);
            const size_t s = static_cast<size_t>(fs);
            tail[k] = db(rms(out.l, static_cast<size_t>(1.2 * fs), static_cast<size_t>(1.5 * fs)) + 1e-9);
            if (k == 0) during = db(rms(out.l, s / 4, s / 2) / rms(in.l, s / 4, s / 2));
        }
        check(tail[1] > tail[0] + 10.0 && during < 9.0, std::string(kTypeName[5][type]) + ": tail 0.7 s after a burst at Decay 0.4 and 3 s (dBFS), level with it (dB)",
              "%.1f  %.1f  %.1f", tail[0], tail[1], during);
    }
    for (int type = 0; type < 4; ++type) { // the lows of the reverb are in the middle: a bass note on the left only
        Stereo in = sine(70.0, 0.25, fs, 2.0);
        std::fill(in.r.begin(), in.r.end(), 0.0f);
        Engine e(fs, only(kPatinaSpace, type, 100.0, 0.0, 1.0));
        const int late = e.latency();
        Stereo out = in;
        run(e, out);
        std::vector<float> wetL(out.size(), 0.0f), side(out.size(), 0.0f);
        for (size_t i = late; i < out.size(); ++i) { wetL[i] = out.l[i] - in.l[i - late]; side[i] = 0.5f * (wetL[i] - out.r[i]); }
        const size_t from = static_cast<size_t>(fs);
        const double wet = tone(out.r, from, out.size(), 70.0, fs), apart = tone(side, from, side.size(), 70.0, fs);
        check(wet < 1e-5 || apart < 0.1 * wet, std::string(kTypeName[5][type]) + ", 70 Hz on the left only: reverb (dBFS), the part that differs (dB below)", "%.1f  %.1f",
              db(wet + 1e-12), db(wet / std::max(apart, 1e-12)));
    }
    { // The tail ducks while you play.
        double wet[2];
        for (int k = 0; k < 2; ++k) {
            Stereo in = sine(500.0, 0.25, fs, 3.0);
            std::fill(in.r.begin(), in.r.end(), 0.0f);
            const Stereo out = through(fs, only(kPatinaSpace, 1, 100.0, k ? 100.0 : 0.0, 1.0), in);
            wet[k] = rms(out.r, static_cast<size_t>(2 * fs), out.size());
        }
        check(std::fabs(db(wet[0] / wet[1]) - 12.0) < 1.5, "the reverb under a steady tone, React 100% against 0 (dB down)", "%.1f", db(wet[0] / wet[1]));
    }
}

// --------------------------------------------------------------- listener

static void listener() {
    const double fs = 48000.0;
    patina::Listener l;
    l.prepare(fs);
    const auto feed = [&](double amp, double seconds, double* hit = nullptr) {
        const size_t n = static_cast<size_t>(seconds * fs);
        for (size_t i = 0; i < n; ++i) {
            const double x = amp * std::sin(2 * kPi * 300.0 * i / fs);
            l.process(x, 0.5 * x);
            if (hit) *hit = std::max(*hit, l.hits);
        }
    };
    check(!l.present && l.gaps == 1.0, "listener, nothing yet: no music, all gap", "%.0f  %.2f", l.present, l.gaps);
    feed(0.1, 8.0);
    check(std::fabs(l.level) < 1.5 && l.present && l.gaps < 0.15, "a steady tone: level against the average (dB), gaps", "%.2f  %.2f", l.level, l.gaps);
    double hit = 0;
    feed(0.2, 0.05, &hit);
    check(std::fabs(l.level - 6.0) < 1.0 && l.above > 0.8 && hit > 0.4, "6 dB louder: level (dB), above, hit", "%.2f  %.2f  %.2f", l.level, l.above, hit);
    feed(0.0, 0.5);
    check(l.gaps > 0.9 && !l.present && l.loud < 0.1, "half a second of silence: gaps, music, loud", "%.2f  %.0f  %.2f", l.gaps, l.present, l.loud);
    feed(0.0, 20.0);
    hit = 0;
    feed(0.001, 0.1, &hit); // -60 dBFS after a long silence: measured against the floor of the average, not against nothing
    check(l.level < 0.0 && l.present, "a very quiet note after silence: level (dB), music", "%.1f  %.0f", l.level, l.present);
    feed(0.0, 3.0);
    check(std::fabs(l.silentFor - 3.0) < 0.2, "seconds since the music (s)", "%.2f", l.silentFor);
    // The level is the same for quiet and loud music: it is measured against the music itself.
    double level[2];
    int k = 0;
    for (double amp : {0.01, 0.5}) {
        patina::Listener q;
        q.prepare(fs);
        for (size_t i = 0; i < static_cast<size_t>(6 * fs); ++i) q.process(amp * std::sin(2 * kPi * 300.0 * i / fs), 0.0);
        for (size_t i = 0; i < static_cast<size_t>(0.05 * fs); ++i) q.process(2 * amp * std::sin(2 * kPi * 300.0 * i / fs), 0.0);
        level[k++] = q.level;
    }
    check(std::fabs(level[0] - level[1]) < 0.3, "a jump of 6 dB at -40 and at -6 dBFS: level (dB)", "%.2f  %.2f", level[0], level[1]);
}

// ------------------------------------------------------- scenes and morph

/** Two scenes that differ in everything. */
static PatinaParams twoScenes() {
    PatinaParams p;
    p.idle = 1;
    static const int typeA[kPatinaModules] = {0, 0, 0, 0, 0, 0}, typeB[kPatinaModules] = {3, 1, 1, 2, 2, 1};
    for (int m = 0; m < kPatinaModules; ++m) {
        p.scene[0].m[m] = {true, typeA[m], 25.0, 30.0, m == kPatinaSpace ? 0.6 : (m == kPatinaMotion ? 0.0 : 20.0)};
        p.scene[1].m[m] = {true, typeB[m], 70.0, 80.0, m == kPatinaSpace ? 2.0 : (m == kPatinaMotion ? 2.0 : 70.0)};
    }
    p.scene[1].noiseMode = 1;
    return p;
}
static double differ(const Stereo& a, const Stereo& b) {
    double worst = 0;
    for (size_t i = 0; i < a.size(); ++i) worst = std::max(worst, static_cast<double>(std::max(std::fabs(a.l[i] - b.l[i]), std::fabs(a.r[i] - b.r[i]))));
    return worst;
}

static void scenes() {
    const double fs = 48000.0;
    const Stereo in = hits(fs, 3.0, 2.0, 0.1, 0.5);
    for (int end = 0; end < 2; ++end) { // Morph at an end is that scene, whatever the other scene holds
        PatinaParams p = twoScenes();
        p.morph = end ? 100.0 : 0.0;
        PatinaParams same = p;
        same.scene[1 - end] = same.scene[end];
        const double d = differ(through(fs, p, in), through(fs, same, in));
        check(d == 0.0, std::string("Morph at ") + (end ? "100%: scene B" : "0%: scene A") + ", exactly (largest difference)", "%.3g", d);
    }
    { // A module that is off in one scene fades in over the whole morph, with the other scene's type.
        PatinaParams p = allOff();
        p.scene[1].m[kPatinaFilter] = {true, patina::Filter::kLadder, 80.0, 0.0, 0.0};
        p.scene[0].m[kPatinaFilter].type = patina::Filter::kTilt;
        double g[3];
        int k = 0;
        for (double morph : {0.0, 25.0, 100.0}) {
            p.morph = morph;
            const Stereo out = through(fs, p, sine(6000.0, 0.25, fs, 1.0));
            g[k++] = db(tone(out.l, static_cast<size_t>(0.5 * fs), out.size(), 6000.0, fs) / 0.25);
        }
        check(std::fabs(g[0]) < 0.01 && g[1] < -3.0 && g[2] < g[1] - 12.0, "Filter off in A, Ladder 80% in B: 6 kHz at Morph 0, 25, 100% (dB)", "%.2f  %.1f  %.1f", g[0], g[1], g[2]);
    }
    { // The same from the same start: twice from a reset.
        Engine e(fs, twoScenes());
        Stereo a = in, b = in;
        run(e, a);
        e.reset();
        run(e, b);
        check(differ(a, b) == 0.0, "the same output after a reset (largest difference)", "%.3g", differ(a, b));
        Stereo c = in;
        Engine f(fs, twoScenes());
        run(f, c, 61);
        check(differ(a, c) < 1e-6, "and in blocks of 61 instead of 256 (largest difference)", "%.3g", differ(a, c));
    }
}

// ---------------------------------------------------------------- switches

/** The largest step from one sample to the next around changes made every 0.4 s, against the step of the tone itself. */
static void noJump(const std::string& name, PatinaParams p, const std::vector<PatinaParams>& changes, double allowed) {
    const double fs = 48000.0;
    Engine e(fs, p);
    Stereo s = sine(110.0, 0.25, fs, 0.4 * (changes.size() + 2));
    const size_t part = static_cast<size_t>(0.4 * fs);
    for (size_t k = 0; k < changes.size() + 2; ++k) {
        if (k >= 1 && k <= changes.size()) e.setParams(changes[k - 1]);
        for (size_t i = k * part; i < (k + 1) * part; i += 64) e.process(&s.l[i], &s.r[i], 64);
    }
    const double step = std::max(largestStep(s.l, part / 2, s.size()), largestStep(s.r, part / 2, s.size()));
    check(step < allowed, name + ": largest step between samples", "%.4f", step);
}

static void switches() {
    // A 110 Hz tone at 0.25 steps by 0.0036 a sample at most. Modules that add no clicks of their own
    // must stay close to what they step by themselves through every switch.
    static const int quiet[4] = {kPatinaDrive, kPatinaMotion, kPatinaFilter, kPatinaSpace};
    for (int m : quiet) {
        PatinaParams on = only(m, 0, 70.0, 50.0, m == kPatinaSpace ? 1.0 : (m == kPatinaMotion ? 0.0 : 50.0));
        std::vector<PatinaParams> list;
        PatinaParams off = on;
        for (auto& s : off.scene) s.m[m].on = false;
        list.push_back(off);
        list.push_back(on);
        for (int type = 1; type < kTypes[m]; ++type) {
            PatinaParams t = on;
            for (auto& s : t.scene) s.m[m].type = type;
            list.push_back(t);
        }
        list.push_back(on);
        // What the module's own sound steps by when nothing is switched: a driven tone has steep edges of its own.
        double own = 0;
        for (int type = 0; type < kTypes[m]; ++type) {
            PatinaParams t = on;
            for (auto& s : t.scene) s.m[m].type = type;
            const Stereo steady = through(48000.0, t, sine(110.0, 0.25, 48000.0, 1.0));
            own = std::max(own, largestStep(steady.l, steady.size() / 2, steady.size()));
        }
        noJump(std::string(kModule[m]) + " off, on, every type (allowed: " + std::to_string(1.5 * own + 0.01).substr(0, 5) + ")", on, list, 1.5 * own + 0.01);
    }
    { // The change-over at Morph 50%, there and back, and a morph held close to it.
        PatinaParams p = twoScenes();
        for (auto& s : p.scene) { s.m[kPatinaVinyl].on = false; s.m[kPatinaNoise].on = false; }
        p.scene[1].m[kPatinaDrive].type = 1; // not into the fuzz: its own harmonics are steps too
        std::vector<PatinaParams> list;
        for (double morph : {45.0, 55.0, 51.0, 49.0, 47.0, 100.0, 0.0}) { PatinaParams q = p; q.morph = morph; list.push_back(q); }
        p.morph = 0.0;
        noJump("Morph across 50% and back, two scenes of different types", p, list, 0.05);
    }
    { // Noise and its React switch: the bed has no clicks in it either.
        PatinaParams on = only(kPatinaNoise, patina::Noise::kHum, 60.0, 50.0, 30.0);
        on.idle = 1;
        std::vector<PatinaParams> list;
        PatinaParams duck = on;
        for (auto& s : duck.scene) s.noiseMode = 1;
        PatinaParams room = on;
        for (auto& s : room.scene) s.m[kPatinaNoise].type = patina::Noise::kRoomTone;
        PatinaParams off = on;
        for (auto& s : off.scene) s.m[kPatinaNoise].on = false;
        list = {duck, on, room, off, on};
        noJump("Noise: React switch, type, off and on", on, list, 0.03);
    }
}

// ----------------------------------------------------------- sample rates

static void rates() {
    for (double fs : {8000.0, 44100.0, 192000.0, 384000.0}) {
        PatinaParams p = twoScenes();
        p.morph = 60.0;
        Stereo s = hits(fs, 1.0, 4.0, 0.1, 0.5);
        Engine e(fs, p);
        run(e, s);
        bool numbers = true;
        for (float v : s.l) numbers = numbers && hnl::isFinite(v);
        check(numbers && peak(s.l, 0, s.size()) < 8.0 && rms(s.l, s.size() / 2, s.size()) > 1e-3, "two heavy scenes at " + std::to_string(static_cast<int>(fs)) + " Hz: peak, level", "%.2f  %.3f",
              peak(s.l, 0, s.size()), rms(s.l, s.size() / 2, s.size()));
    }
    { // What it costs: everything on, at 48 kHz.
        const double fs = 48000.0;
        PatinaParams p = twoScenes();
        p.morph = 60.0;
        Stereo s = hits(fs, 10.0, 4.0, 0.1, 0.5);
        Engine e(fs, p);
        const auto t0 = std::chrono::steady_clock::now();
        run(e, s);
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        check(seconds < 5.0, "10 s of sound with every module on: time taken (s), share of one core (%)", "%.2f  %.1f", seconds, 10.0 * seconds);
    }
}

int main() {
    latency();
    listener();
    motion();
    vinyl();
    noiseBed();
    drive();
    filter();
    space();
    scenes();
    switches();
    rates();
    std::printf("%s\n", failures ? "FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
