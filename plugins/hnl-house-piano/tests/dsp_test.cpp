// Offline measurements of the piano engine. Build and run with tests/run.sh.
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

#include "../dsp/Piano.hpp"

using namespace hnl;

static int failures = 0;

static void check(bool ok, const std::string& name, const char* fmt, double a = 0, double b = 0, double c = 0, double d = 0) {
    std::printf("%s  %-42s ", ok ? "PASS" : "FAIL", name.c_str());
    std::printf(fmt, a, b, c, d);
    std::printf("\n");
    if (!ok) ++failures;
}

typedef std::vector<float> Buf;

struct Take {
    Buf l, r;
    double fs;
};

/** Dry, centred settings for measuring. */
static PianoParams plain() {
    PianoParams p;
    p.reverb = 0.0;
    return p;
}

static void run(Piano& m, Take& t, double seconds) {
    const size_t from = t.l.size(), n = static_cast<size_t>(seconds * t.fs);
    t.l.resize(from + n);
    t.r.resize(from + n);
    for (size_t i = 0; i < n; i += 256) m.process(&t.l[from + i], &t.r[from + i], static_cast<int>(std::min<size_t>(256, n - i)));
}

/** One note, held for `hold` seconds and followed for `after` more. */
static Take play(double fs, const PianoParams& p, int note, int velocity, double hold, double after = 0.0) {
    Piano m;
    m.prepare(fs);
    m.setParams(p);
    m.reset();
    Take t;
    t.fs = fs;
    m.noteOn(note, velocity);
    run(m, t, hold);
    m.noteOff(note);
    if (after > 0) run(m, t, after);
    return t;
}

static double rms(const Buf& x, double fs, double from, double to) {
    double sum = 0;
    const size_t a = static_cast<size_t>(from * fs), b = std::min(x.size(), static_cast<size_t>(to * fs));
    for (size_t i = a; i < b; ++i) sum += static_cast<double>(x[i]) * x[i];
    return std::sqrt(sum / std::max<size_t>(1, b - a));
}

static void bin(const Buf& x, double fs, size_t a, size_t b, double f, double& re, double& im) {
    re = im = 0;
    for (size_t i = a; i < b; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2 * kPi * (i - a) / (b - a));
        re += w * x[i] * std::cos(2 * kPi * f * i / fs);
        im += w * x[i] * std::sin(2 * kPi * f * i / fs);
    }
}

static double tone(const Buf& x, double fs, double from, double to, double f) {
    double re, im;
    const size_t a = static_cast<size_t>(from * fs), b = std::min(x.size(), static_cast<size_t>(to * fs));
    bin(x, fs, a, b, f, re, im);
    return 2.0 * std::sqrt(re * re + im * im) / (0.5 * (b - a));
}

/** The pitch of the component near f, in cents off, from how its phase moves between two windows. */
static double centsOff(const Buf& x, double fs, double from, double f) {
    const size_t a = static_cast<size_t>(from * fs), n = static_cast<size_t>(0.2 * fs), hop = static_cast<size_t>(0.02 * fs);
    double re0, im0, re1, im1;
    bin(x, fs, a, a + n, f, re0, im0);
    bin(x, fs, a + hop, a + hop + n, f, re1, im1);
    double d = std::atan2(im1, re1) - std::atan2(im0, re0);
    while (d > kPi) d -= 2 * kPi;
    while (d < -kPi) d += 2 * kPi;
    const double df = -d / (2 * kPi * (hop / fs));
    return 1200.0 * std::log2((f + df) / f);
}

static double noteFreq(int note) { return 440.0 * std::pow(2.0, (note - 69) / 12.0); }

static void testPitch() {
    double worst = 0;
    int worstNote = 0;
    for (double fs : {44100.0, 48000.0, 96000.0})
        for (int note = 24; note <= 104; note += (fs == 48000.0 ? 1 : 7)) {
            const Take t = play(fs, plain(), note, 100, 1.6);
            const double off = centsOff(t.l, fs, 1.3, noteFreq(note)); // in the loop, where the tone stands still
            if (std::fabs(off) > std::fabs(worst)) { worst = off; worstNote = note; }
        }
    check(std::fabs(worst) < 1.5, "in tune on every key, three sample rates", "worst %+.2f cents (note %.0f)", worst, worstNote);

    PianoParams p = plain();
    p.tune = 50;
    p.octave = 1;
    Take t = play(48000, p, 57, 100, 1.6);
    check(std::fabs(centsOff(t.l, 48000, 1.3, 440.0 * std::pow(2.0, 50.0 / 1200.0))) < 1.5, "octave +1 and tune +50 cents", "%+.2f cents off",
          centsOff(t.l, 48000, 1.3, 440.0 * std::pow(2.0, 50.0 / 1200.0)));

    Piano m;
    m.prepare(48000);
    m.setParams(plain());
    t = Take{{}, {}, 48000};
    m.noteOn(69, 100);
    m.pitchBend(1.0);
    run(m, t, 1.6);
    check(std::fabs(centsOff(t.l, 48000, 1.3, noteFreq(71))) < 1.5, "pitch bend up: two semitones", "%+.2f cents off", centsOff(t.l, 48000, 1.3, noteFreq(71)));
}

static void testLevels() {
    // Neighbouring keys sound alike, also where one wave hands over to the next.
    double worstStep = 0;
    double last = 0;
    for (int note = 30; note <= 96; ++note) {
        const Take t = play(48000, plain(), note, 100, 0.4);
        const double db = linToDb(rms(t.l, 48000, 0.0, 0.3));
        if (note > 30) worstStep = std::max(worstStep, std::fabs(db - last));
        last = db;
    }
    check(worstStep < 2.5, "even level from key to key", "largest step %.2f dB", worstStep);

    const Take loud = play(48000, plain(), 60, 127, 0.5), soft = play(48000, plain(), 60, 40, 0.5);
    const double level = linToDb(rms(loud.l, 48000, 0, 0.3) / rms(soft.l, 48000, 0, 0.3));
    const auto bright = [](const Take& t) { // upper partials against the fundamental
        double hi = 0;
        for (int k = 6; k <= 14; ++k) hi += tone(t.l, 48000, 0.02, 0.3, k * noteFreq(60) * std::sqrt(1 + 3.5e-4 * k * k));
        return linToDb(hi / tone(t.l, 48000, 0.02, 0.3, noteFreq(60)));
    };
    check(level > 8 && level < 30, "velocity 127 against 40: louder", "%.1f dB", level);
    check(bright(loud) > bright(soft) + 6, "and brighter", "%.1f dB against %.1f dB", bright(loud), bright(soft));

    PianoParams p = plain();
    p.velocity = 0;
    const Take a = play(48000, p, 60, 127, 0.3), b = play(48000, p, 60, 20, 0.3);
    check(std::fabs(linToDb(rms(a.l, 48000, 0, 0.3) / rms(b.l, 48000, 0, 0.3))) < 0.1, "velocity 0%: every key press the same", "%.2f dB apart",
          linToDb(rms(a.l, 48000, 0, 0.3) / rms(b.l, 48000, 0, 0.3)));

    p = plain();
    p.tone = -100;
    const Take dark = play(48000, p, 60, 127, 0.5);
    check(bright(dark) < bright(loud) - 15, "tone -100: dark", "%.1f dB against %.1f dB", bright(dark), bright(loud));

    // A full chord, as hard as it goes, stays under full scale at the default volume.
    Piano m;
    m.prepare(48000);
    m.setParams(PianoParams());
    Take t{{}, {}, 48000};
    for (int note : {36, 48, 55, 60, 64, 67, 72, 76}) m.noteOn(note, 127);
    run(m, t, 1.0);
    double peak = 0;
    for (size_t i = 0; i < t.l.size(); ++i) peak = std::max(peak, static_cast<double>(std::max(std::fabs(t.l[i]), std::fabs(t.r[i]))));
    check(peak > 0.25 && peak < 1.0, "eight-note chord at velocity 127", "peak %.1f dBFS", linToDb(peak));
}

static void testEnvelope() {
    const double fs = 48000;
    // Held: the level falls by itself, slower in the bass.
    const auto fall = [&](int note, double decay) { // dB per second between 1 and 2 seconds
        PianoParams p = plain();
        p.decay = decay;
        const Take t = play(fs, p, note, 100, 2.2);
        return linToDb(rms(t.l, fs, 1.0, 1.2) / rms(t.l, fs, 2.0, 2.2));
    };
    const double low = fall(36, 100), mid = fall(60, 100), high = fall(84, 100);
    check(low < mid && mid < high && std::fabs(mid - 8.69 / 2.2) < 0.5, "held note fades: bass slowest", "%.1f / %.1f / %.1f dB per second", low, mid, high);
    check(std::fabs(fall(60, 50) - 2 * mid) < 0.5, "decay 50%: twice as fast", "%.1f dB per second", fall(60, 50));

    // Let go: gone after the release time.
    PianoParams p = plain();
    Take t = play(fs, p, 60, 100, 0.5, 0.5);
    const double before = rms(t.l, fs, 0.45, 0.5), at = rms(t.l, fs, 0.5 + 0.15, 0.5 + 0.16), later = rms(t.l, fs, 0.9, 1.0);
    check(std::fabs(linToDb(at / before) + 60) < 8 && later == 0.0, "release 150 ms: 60 dB down, then silent", "%.1f dB, later %.1e", linToDb(at / before), later);

    // The pedal holds the note; lifting it lets go.
    Piano m;
    m.prepare(fs);
    m.setParams(plain());
    t = Take{{}, {}, fs};
    m.controller(64, 127);
    m.noteOn(60, 100);
    run(m, t, 0.3);
    m.noteOff(60);
    run(m, t, 0.7);
    const double held = rms(t.l, fs, 0.8, 1.0);
    m.controller(64, 0);
    run(m, t, 1.0);
    check(held > 0.01 && rms(t.l, fs, 1.8, 2.0) == 0.0 && m.activeVoices() == 0, "sustain pedal holds, then lets go", "held %.1f dB", linToDb(held));

    // The loop: a note held for a long time stays smooth, with no click where it repeats.
    double worst = 0;
    for (int note : {24, 41, 60, 77, 96, 108}) {
        p = plain();
        p.decay = 1000;
        t = play(fs, p, note, 127, 3.0);
        double step = 0, top = 0;
        for (size_t i = static_cast<size_t>(1.5 * fs); i < t.l.size(); ++i) {
            step = std::max(step, static_cast<double>(std::fabs(t.l[i] - t.l[i - 1])));
            top = std::max(top, static_cast<double>(std::fabs(t.l[i])));
        }
        // For a smooth wave the largest step is bounded by its highest frequency, here under 14 kHz.
        worst = std::max(worst, step / top);
    }
    check(worst < 1.2, "loop repeats without a click", "largest step %.2f of the peak", worst);
}

static void testStereo() {
    const double fs = 48000;
    const auto correlation = [&](const Take& t, double from, double to) {
        double lr = 0, ll = 0, rr = 0;
        for (size_t i = static_cast<size_t>(from * fs); i < static_cast<size_t>(to * fs); ++i) {
            lr += static_cast<double>(t.l[i]) * t.r[i];
            ll += static_cast<double>(t.l[i]) * t.l[i];
            rr += static_cast<double>(t.r[i]) * t.r[i];
        }
        return lr / std::sqrt(ll * rr + 1e-30);
    };
    double least = 1, most = 0, worstBalance = 0;
    for (int note : {28, 40, 52, 60, 64, 72, 84, 96}) {
        PianoParams p = plain();
        const Take t = play(fs, p, note, 100, 1.5);
        const double c = correlation(t, 0.0, 1.5);
        least = std::min(least, c);
        most = std::max(most, c);
        worstBalance = std::max(worstBalance, std::fabs(linToDb(rms(t.l, fs, 0, 1.5) / rms(t.r, fs, 0, 1.5))));
    }
    check(least > 0.3 && most < 0.95, "every note is stereo: left and right differ", "correlation %.2f to %.2f", least, most);
    check(worstBalance < 4.0, "and balanced, low keys a little left, high a little right", "at most %.1f dB to one side", worstBalance);

    const Take bass = play(fs, plain(), 28, 100, 1.0);
    const double f = noteFreq(28) * 2; // the second partial, about 82 Hz
    check(std::fabs(linToDb(tone(bass.l, fs, 0.5, 1.0, f) / tone(bass.r, fs, 0.5, 1.0, f)) + 0.0) < 3.0, "the bass sits in the middle", "%.1f dB",
          linToDb(tone(bass.l, fs, 0.5, 1.0, f) / tone(bass.r, fs, 0.5, 1.0, f)));

    PianoParams p = plain();
    p.width = 0;
    const Take mono = play(fs, p, 60, 100, 0.5);
    double diff = 0;
    for (size_t i = 0; i < mono.l.size(); ++i) diff = std::max(diff, static_cast<double>(std::fabs(mono.l[i] - mono.r[i])));
    check(diff < 1e-6 && rms(mono.l, fs, 0, 0.5) > 0.01, "width 0%: mono", "largest difference %.1e", diff);

    // Summed to mono, the stereo sound keeps its level.
    const Take wide = play(fs, plain(), 60, 100, 0.5);
    Buf sum(wide.l.size());
    for (size_t i = 0; i < sum.size(); ++i) sum[i] = 0.5f * (wide.l[i] + wide.r[i]);
    check(linToDb(rms(sum, fs, 0, 0.5) / rms(wide.l, fs, 0, 0.5)) > -3.0, "mono sum loses little", "%.1f dB", linToDb(rms(sum, fs, 0, 0.5) / rms(wide.l, fs, 0, 0.5)));

    // Doubling: two halves, one up and one down, so the level wavers.
    p = plain();
    p.detune = 10;
    p.decay = 1000;
    const Take two = play(fs, p, 69, 100, 2.5);
    double lo = 1e9, hi = 0;
    Buf m2(two.l.size());
    for (size_t i = 0; i < m2.size(); ++i) m2[i] = 0.5f * (two.l[i] + two.r[i]);
    for (double t0 = 1.0; t0 < 2.4; t0 += 0.02) {
        const double v = tone(m2, fs, t0, t0 + 0.04, 440.0);
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    check(hi > 3 * lo, "detune 10 cents: the two halves beat", "level moves %.1f dB", linToDb(hi / lo));
}

static void testVoices() {
    Piano m;
    m.prepare(48000);
    PianoParams p;
    p.detune = 5;
    p.reverb = 100;
    m.setParams(p);
    Take t{{}, {}, 48000};
    m.controller(64, 127);
    for (int round = 0; round < 3; ++round)
        for (int note = 21; note <= 108; ++note) {
            m.noteOn(note, 1 + (note * 7) % 127);
            run(m, t, 0.005);
            m.noteOff(note);
        }
    bool finite = true;
    double peak = 0;
    for (float v : t.l) { finite = finite && std::isfinite(v); peak = std::max(peak, static_cast<double>(std::fabs(v))); }
    check(finite && m.activeVoices() <= kMaxVoices && m.activeVoices() > 32, "264 notes with the pedal down", "%.0f voices, peak %.1f dBFS", m.activeVoices(), linToDb(peak));
    m.controller(123, 0);
    m.controller(64, 0);
    run(m, t, 8.0);
    check(m.activeVoices() == 0 && rms(t.l, 48000, t.l.size() / 48000.0 - 0.5, t.l.size() / 48000.0) == 0.0, "all notes off: silent once the reverb is gone", "%.0f voices",
          m.activeVoices());

    // Out-of-range keys and odd input do no harm.
    m.noteOn(127, 127); m.noteOn(0, 127); m.noteOff(55); m.pitchBend(5.0); m.controller(1, 64);
    p.octave = 2;
    m.setParams(p);
    m.noteOn(127, 100);
    run(m, t, 0.2);
    finite = true;
    for (float v : t.l) finite = finite && std::isfinite(v);
    check(finite, "keys at the edges stay finite", "");
}

static void testSpeed() {
    std::clock_t t0 = std::clock();
    Piano m;
    m.prepare(48000);
    const double build = static_cast<double>(std::clock() - t0) / CLOCKS_PER_SEC;
    check(build < 1.5, "computing the waves", "%.0f ms", build * 1000);
    PianoParams p;
    p.detune = 5;
    m.setParams(p);
    m.controller(64, 127);
    for (int note = 36; note < 68; ++note) m.noteOn(note, 100); // 64 voices
    Take t{{}, {}, 48000};
    t0 = std::clock();
    run(m, t, 10.0);
    const double seconds = static_cast<double>(std::clock() - t0) / CLOCKS_PER_SEC;
    check(seconds < 2.5, "speed with all 64 voices", "%.1fx real time", 10.0 / seconds);
}

int main() {
    testSpeed();
    testPitch();
    testLevels();
    testEnvelope();
    testStereo();
    testVoices();
    std::printf("%s\n", failures ? "FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
