// Offline measurements of the piano engine. Build and run with tests/run.sh.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
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
            const Take t = play(fs, plain(), note, 100, 0.8);
            // Once the attack has settled. The lowest keys have a weak fundamental next to a much stronger
            // second partial, so there the pitch is read from that one.
            const double off = centsOff(t.l, fs, 0.45, (note < 36 ? 2.0 : 1.0) * noteFreq(note));
            if (std::fabs(off) > std::fabs(worst)) { worst = off; worstNote = note; }
        }
    check(std::fabs(worst) < 4.0, "in tune on every key, three sample rates", "worst %+.2f cents (note %.0f)", worst, worstNote);

    PianoParams p = plain();
    p.tune = 50;
    p.octave = 1;
    Take t = play(48000, p, 57, 100, 0.8);
    check(std::fabs(centsOff(t.l, 48000, 0.45, 440.0 * std::pow(2.0, 50.0 / 1200.0))) < 1.5, "octave +1 and tune +50 cents", "%+.2f cents off",
          centsOff(t.l, 48000, 0.45, 440.0 * std::pow(2.0, 50.0 / 1200.0)));

    Piano m;
    m.prepare(48000);
    m.setParams(plain());
    t = Take{{}, {}, 48000};
    m.noteOn(69, 100);
    m.pitchBend(1.0);
    run(m, t, 0.8);
    check(std::fabs(centsOff(t.l, 48000, 0.45, noteFreq(71))) < 1.5, "pitch bend up: two semitones", "%+.2f cents off", centsOff(t.l, 48000, 0.45, noteFreq(71)));
}

static void testLevels() {
    // Neighbouring keys sound alike, also where one wave hands over to the next.
    double worstStep = 0;
    double last = 0;
    for (int note = 30; note <= 96; ++note) {
        const Take t = play(48000, plain(), note, 100, 0.4);
        const double db = linToDb(std::hypot(rms(t.l, 48000, 0.0, 0.3), rms(t.r, 48000, 0.0, 0.3)));
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
    check(bright(loud) > bright(soft) + 1.5, "and brighter", "%.1f dB against %.1f dB", bright(loud), bright(soft));

    PianoParams p = plain();
    p.velocity = 0;
    const Take a = play(48000, p, 60, 127, 0.3), b = play(48000, p, 60, 20, 0.3);
    check(std::fabs(linToDb(rms(a.l, 48000, 0, 0.3) / rms(b.l, 48000, 0, 0.3))) < 0.1, "velocity 0%: every key press the same", "%.2f dB apart",
          linToDb(rms(a.l, 48000, 0, 0.3) / rms(b.l, 48000, 0, 0.3)));

    p = plain();
    p.tone = -100;
    const Take dark = play(48000, p, 60, 127, 0.5);
    check(bright(dark) < bright(loud) - 8, "tone -100: dark", "%.1f dB against %.1f dB", bright(dark), bright(loud));

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
    const auto lasts = [&](int note, double decay) { // seconds until the note is 40 dB under its start
        PianoParams p = plain();
        p.decay = decay;
        const Take t = play(fs, p, note, 100, 10.0);
        const double start = rms(t.l, fs, 0.0, 0.1);
        for (double at = 0.1; at < 9.9; at += 0.05)
            if (rms(t.l, fs, at, at + 0.1) < 0.01 * start) return at;
        return 10.0;
    };
    const double low = lasts(36, 100), mid = lasts(60, 100), high = lasts(84, 100);
    check(low > mid - 0.5 && mid > high && mid > 2.5 && high > 0.5, "held note fades: treble goes first", "%.1f / %.1f / %.1f s to -40 dB", low, mid, high);
    check(lasts(60, 50) < 0.75 * mid && lasts(60, 50) > 0.3 * mid, "decay 50%: gone sooner", "%.1f s", lasts(60, 50));

    // Let go: gone after the release time.
    PianoParams p = plain();
    p.release = 150;
    Take t = play(fs, p, 60, 100, 0.5, 0.5);
    p.release = 600;
    const Take slow = play(fs, p, 60, 100, 0.5, 0.5);
    check(std::fabs(linToDb(rms(slow.l, fs, 0.79, 0.81) / rms(slow.l, fs, 0.49, 0.5)) + 30) < 5, "default release: 30 dB down after 300 ms", "%.1f dB",
          linToDb(rms(slow.l, fs, 0.79, 0.81) / rms(slow.l, fs, 0.49, 0.5)));
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
    run(m, t, 2.0);
    check(held > 0.003 && rms(t.l, fs, 2.8, 3.0) == 0.0 && m.activeVoices() == 0, "sustain pedal holds, then lets go", "held %.1f dB", linToDb(held));

    // A note held until it has run its course ends without a click, and frees its voice.
    double worst = 0;
    bool freed = true;
    for (int note : {24, 41, 60, 77, 96, 108}) {
        Piano held;
        held.prepare(fs);
        held.setParams(plain());
        held.reset();
        t = Take{{}, {}, fs};
        held.noteOn(note, 127);
        run(held, t, 12.0);
        freed = freed && held.activeVoices() == 0;
        double top = 0, step = 0;
        for (size_t i = 1; i < t.l.size(); ++i) top = std::max(top, static_cast<double>(std::fabs(t.l[i])));
        for (size_t i = static_cast<size_t>(0.3 * fs); i < t.l.size(); ++i) step = std::max(step, static_cast<double>(std::fabs(t.l[i] - t.l[i - 1])));
        worst = std::max(worst, step / top);
    }
    check(freed && worst < 0.5, "a held note dies away by itself, no click", "largest late step %.3f of the peak", worst);
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
    check(least > 0.25 && most < 0.95, "every note is stereo: left and right differ", "correlation %.2f to %.2f", least, most);
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

static void testFm() {
    const double fs = 48000;
    PianoParams p = plain();
    p.voice = kFmElectric;

    double worst = 0;
    for (double rate : {44100.0, 96000.0})
        for (int note = 28; note <= 100; note += 6) {
            const Take t = play(rate, p, note, 100, 0.8);
            Buf mid(t.l.size());
            for (size_t i = 0; i < mid.size(); ++i) mid[i] = 0.5f * (t.l[i] + t.r[i]);
            const double off = centsOff(mid, rate, 0.4, noteFreq(note));
            if (std::fabs(off) > std::fabs(worst)) worst = off;
        }
    check(std::fabs(worst) < 2.0, "FM: in tune", "worst %+.2f cents", worst);

    // As loud as the workstation voice, give or take.
    double apart = 0;
    for (int note : {40, 52, 64, 76, 88}) {
        const Take a = play(fs, plain(), note, 100, 0.4), b = play(fs, p, note, 100, 0.4);
        apart = std::max(apart, std::fabs(linToDb(std::hypot(rms(b.l, fs, 0, 0.3), rms(b.r, fs, 0, 0.3)) / std::hypot(rms(a.l, fs, 0, 0.3), rms(a.r, fs, 0, 0.3)))));
    }
    check(apart < 4.0, "FM: as loud as the workstation voice", "at most %.1f dB apart", apart);

    // The bell: overtones around fourteen times the note, strong at the start and gone later.
    const Take hard = play(fs, p, 60, 120, 3.0), soft = play(fs, p, 60, 45, 3.0);
    const double f = noteFreq(60);
    const auto bell = [&](const Take& t, double at) { return linToDb((tone(t.l, fs, at, at + 0.1, 13 * f) + tone(t.l, fs, at, at + 0.1, 15 * f)) / tone(t.l, fs, at, at + 0.1, f)); };
    check(bell(hard, 0.02) > -20 && bell(hard, 2.5) < bell(hard, 0.02) - 15, "FM: the bell rings at the start, then fades", "%.1f dB, later %.1f dB", bell(hard, 0.02), bell(hard, 2.5));
    const auto bark = [&](const Take& t, double at) { return linToDb(tone(t.l, fs, at, at + 0.1, 3 * f) / tone(t.l, fs, at, at + 0.1, f)); };
    check(bark(hard, 0.02) > bark(soft, 0.02) + 10 && bark(hard, 2.5) < bark(hard, 0.02) - 4, "FM: hard notes bark, and mellow as they ring", "hard %.1f dB, soft %.1f dB, hard later %.1f dB",
          bark(hard, 0.02), bark(soft, 0.02), bark(hard, 2.5));

    // Stereo, and safe in mono: the two halves never cancel.
    double lr = 0, ll = 0, rr = 0, lo = 1e9, hi = 0;
    const Take t = play(fs, p, 72, 100, 4.0);
    for (size_t i = 0; i < t.l.size(); ++i) { lr += static_cast<double>(t.l[i]) * t.r[i]; ll += static_cast<double>(t.l[i]) * t.l[i]; rr += static_cast<double>(t.r[i]) * t.r[i]; }
    Buf mid(t.l.size());
    for (size_t i = 0; i < mid.size(); ++i) mid[i] = 0.5f * (t.l[i] + t.r[i]);
    for (double at = 0.2; at < 3.8; at += 0.1) {
        const double v = rms(mid, fs, at, at + 0.1) / std::hypot(rms(t.l, fs, at, at + 0.1), rms(t.r, fs, at, at + 0.1));
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    check(lr / std::sqrt(ll * rr) < 0.9 && lr / std::sqrt(ll * rr) > 0.3, "FM: stereo", "correlation %.2f", lr / std::sqrt(ll * rr));
    check(linToDb(hi / lo) < 2.0, "FM: summed to mono nothing cancels", "moves %.1f dB", linToDb(hi / lo));
    p.width = 0;
    const Take mono = play(fs, p, 60, 100, 0.5);
    double diff = 0;
    for (size_t i = 0; i < mono.l.size(); ++i) diff = std::max(diff, static_cast<double>(std::fabs(mono.l[i] - mono.r[i])));
    check(diff < 1e-6, "FM: width 0% is mono", "largest difference %.1e", diff);
    p.width = 100;

    // Top of the keyboard: the bell is left out before it could fold back as false tones.
    const Take top = play(44100, p, 100, 127, 0.5);
    const double ft = noteFreq(100), fold = 44100 - 13 * ft; // where the 13th would land if it were made
    check(tone(top.l, 44100, 0.02, 0.3, fold) < 0.003 * tone(top.l, 44100, 0.02, 0.3, ft), "FM: no folded-back bell on high keys", "%.1f dB under the note",
          -linToDb(tone(top.l, 44100, 0.02, 0.3, fold) / tone(top.l, 44100, 0.02, 0.3, ft)));

    // Release, pedal and running out all work as for the other voice.
    Piano m;
    m.prepare(fs);
    m.setParams(p);
    m.reset();
    Take e{{}, {}, fs};
    m.noteOn(60, 100);
    run(m, e, 0.5);
    m.noteOff(60);
    run(m, e, 1.5);
    const bool released = m.activeVoices() == 0;
    m.noteOn(96, 100);
    run(m, e, 20.0);
    check(released && m.activeVoices() == 0, "FM: notes end after release, and by themselves", "%.0f voices left", m.activeVoices());

    m.controller(64, 127);
    for (int note = 36; note < 100; ++note) m.noteOn(note, 100);
    Take s{{}, {}, fs};
    const std::clock_t t0 = std::clock();
    run(m, s, 5.0);
    const double seconds = static_cast<double>(std::clock() - t0) / CLOCKS_PER_SEC;
    bool finite = true;
    for (float v : s.l) finite = finite && std::isfinite(v);
    check(finite && seconds < 1.6, "FM: speed with all 64 voices", "%.1fx real time", 5.0 / seconds);
}

/** What every voice made of computed waves has to do. */
static void testWaves(int voice, const char* name) {
    const double fs = 48000;
    const std::string tag = std::string(name) + ": ";
    const std::clock_t t0 = std::clock();
    { Piano warm; PianoParams q; q.voice = voice; warm.prepare(fs); warm.setParams(q); }
    const double build = static_cast<double>(std::clock() - t0) / CLOCKS_PER_SEC;
    check(build < 3.0, tag + "working out its waves", "%.0f ms", build * 1000);

    PianoParams p = plain();
    p.voice = voice;
    double worst = 0, lastDb = 0, step = 0, least = 1, most = 0;
    for (int note = 28; note <= 100; note += 3) {
        const Take t = play(fs, p, note, 100, 0.8);
        Buf mid(t.l.size());
        for (size_t i = 0; i < mid.size(); ++i) mid[i] = 0.5f * (t.l[i] + t.r[i]);
        const double off = centsOff(mid, fs, 0.45, noteFreq(note));
        if (std::fabs(off) > std::fabs(worst)) worst = off;
        const double db = linToDb(std::hypot(rms(t.l, fs, 0.0, 0.3), rms(t.r, fs, 0.0, 0.3)));
        if (note > 28) step = std::max(step, std::fabs(db - lastDb));
        lastDb = db;
        double lr = 0, ll = 0, rr = 0;
        for (size_t i = 0; i < t.l.size(); ++i) { lr += static_cast<double>(t.l[i]) * t.r[i]; ll += static_cast<double>(t.l[i]) * t.l[i]; rr += static_cast<double>(t.r[i]) * t.r[i]; }
        least = std::min(least, lr / std::sqrt(ll * rr));
        most = std::max(most, lr / std::sqrt(ll * rr));
    }
    check(std::fabs(worst) < 4.0, tag + "in tune", "worst %+.2f cents", worst);
    check(step < 3.5, tag + "even level from key to key", "largest step %.2f dB", step);
    check(least > 0.0 && most < 0.97, tag + "stereo on every key, the sides never against each other", "correlation %.2f to %.2f", least, most);

    const Take a = play(fs, plain(), 60, 100, 0.4), b = play(fs, p, 60, 100, 0.4);
    const double apart = linToDb(std::hypot(rms(b.l, fs, 0, 0.3), rms(b.r, fs, 0, 0.3)) / std::hypot(rms(a.l, fs, 0, 0.3), rms(a.r, fs, 0, 0.3)));
    check(std::fabs(apart) < 4.0, tag + "as loud as the workstation voice", "%+.1f dB", apart);

    const Take loud = play(fs, p, 60, 127, 0.5), soft = play(fs, p, 60, 40, 0.5);
    const auto bright = [&](const Take& t) {
        double hi = 0;
        for (int k = 5; k <= 12; ++k) hi += tone(t.l, fs, 0.02, 0.3, k * noteFreq(60) * (1 + 2.0e-4 * k * k));
        return linToDb(hi / tone(t.l, fs, 0.02, 0.3, noteFreq(60)));
    };
    check(rms(loud.l, fs, 0, 0.3) > 3 * rms(soft.l, fs, 0, 0.3) && bright(loud) > bright(soft) + 1, tag + "harder is louder and brighter", "%.1f dB louder, %.1f dB brighter",
          linToDb(rms(loud.l, fs, 0, 0.3) / rms(soft.l, fs, 0, 0.3)), bright(loud) - bright(soft));

    p.width = 0;
    const Take mono = play(fs, p, 60, 100, 0.3);
    double diff = 0;
    for (size_t i = 0; i < mono.l.size(); ++i) diff = std::max(diff, static_cast<double>(std::fabs(mono.l[i] - mono.r[i])));
    check(diff < 1e-6, tag + "width 0% is mono", "largest difference %.1e", diff);
    p.width = 100;

    // Held until it has run its course: no click, and the voice is freed.
    double click = 0;
    bool freed = true;
    for (int note : {30, 60, 90}) {
        Piano m;
        m.prepare(fs);
        m.setParams(p);
        m.reset();
        Take t{{}, {}, fs};
        m.noteOn(note, 120);
        run(m, t, 12.0);
        freed = freed && m.activeVoices() == 0;
        double top = 0, jump = 0;
        for (size_t i = 1; i < t.l.size(); ++i) top = std::max(top, static_cast<double>(std::fabs(t.l[i])));
        for (size_t i = static_cast<size_t>(0.3 * fs); i < t.l.size(); ++i) jump = std::max(jump, static_cast<double>(std::fabs(t.l[i] - t.l[i - 1])));
        click = std::max(click, jump / top);
    }
    check(freed && click < 0.5, tag + "a held note dies away by itself, no click", "largest late step %.3f of the peak", click);
}

static void testGrand() {
    const double fs = 48000;
    PianoParams p = plain();
    p.voice = kGrand;
    p.velocity = 100;

    // Two stages: a fast fade at first, a slow aftersound later.
    const Take t = play(fs, p, 60, 110, 7.0);
    const double early = linToDb(rms(t.l, fs, 0.1, 0.3) / rms(t.l, fs, 1.1, 1.3)), late = linToDb(rms(t.l, fs, 3.5, 3.7) / rms(t.l, fs, 5.5, 5.7)) / 2.0;
    check(early > 1.6 * late && late > 0.3, "Grand: fades fast at first, then slowly", "%.1f dB/s, later %.1f dB/s", early, late);

    // The hammer: a soft note is nearly all fundamental, a hard one is rich.
    const auto upper = [&](int vel) {
        const Take n = play(fs, p, 48, vel, 0.5);
        double hi = 0;
        for (int k = 8; k <= 20; ++k) hi += tone(n.l, fs, 0.02, 0.3, k * noteFreq(48) * std::sqrt(1 + 9.0e-5 * k * k));
        return linToDb(hi / tone(n.l, fs, 0.02, 0.3, noteFreq(48)));
    };
    check(upper(127) > upper(30) + 10, "Grand: a hard note is much richer than a soft one", "upper partials %.1f dB hard, %.1f dB soft", upper(127), upper(30));

    // Bass rings long, treble short.
    const auto lasts = [&](int note) {
        const Take n = play(fs, p, note, 100, 10.0);
        const double start = rms(n.l, fs, 0.0, 0.1);
        for (double at = 0.1; at < 9.8; at += 0.05)
            if (rms(n.l, fs, at, at + 0.1) < 0.03 * start) return at;
        return 10.0;
    };
    const double low = lasts(33), mid = lasts(60), high = lasts(93);
    check(low >= mid && mid > 2 * high && high > 0.15, "Grand: bass rings long, treble short", "%.1f / %.1f / %.1f s to -30 dB", low, mid, high);

    // Measured in stereo, but kept in hand: no key leans far to one side or thins out in mono.
    double lean = 0, loss = 0;
    for (int note = 21; note <= 108; note += 3)
        for (int vel : {40, 85, 127}) {
            const Take n = play(fs, p, note, vel, 1.0);
            Buf mid(n.l.size());
            for (size_t i = 0; i < mid.size(); ++i) mid[i] = 0.5f * (n.l[i] + n.r[i]);
            lean = std::max(lean, std::fabs(linToDb(rms(n.l, fs, 0, 1) / rms(n.r, fs, 0, 1))));
            loss = std::min(loss, linToDb(rms(mid, fs, 0, 1) / std::hypot(rms(n.l, fs, 0, 1), rms(n.r, fs, 0, 1)) * std::sqrt(2.0)));
        }
    check(lean < 4.5 && loss > -3.5, "Grand: every key near the middle, and whole in mono", "at most %.1f dB to a side, %.1f dB lost in mono", lean, loss);

    // Dampers: a middle key stops when let go, a top key has none and rings on.
    const Take stopped = play(fs, p, 60, 100, 0.2, 0.6), ringing = play(fs, p, 96, 100, 0.2, 0.6);
    const double a = linToDb(rms(stopped.l, fs, 0.6, 0.7) / rms(stopped.l, fs, 0.1, 0.2)), b = linToDb(rms(ringing.l, fs, 0.6, 0.7) / rms(ringing.l, fs, 0.1, 0.2));
    check(a < -40 && b > -25, "Grand: dampers stop a middle key, not a top key", "%.1f dB, top key %.1f dB", a, b);
}

/**
 * On the audio thread a voice's waves are never computed: the piano asks for them and keeps the
 * voice it has until they are there. Must run before anything else has asked for the Dance voice.
 */
static void testRealtimeVoiceChange() {
    using Clock = std::chrono::steady_clock;
    Piano m;
    m.prepare(48000.0);
    PianoParams p = plain();
    p.voice = kDance;
    const Clock::time_point t0 = Clock::now();
    m.setParamsRealtime(p);
    const double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    check(ms < 50.0 && Bank::ready(kDance) == nullptr, "realtime voice change returns at once", "%.2f ms, waves there: %.0f", ms, static_cast<double>(Bank::ready(kDance) != nullptr));
    check(m.soundingVoice() == kWorkstation && m.params().voice == kDance, "the old voice stays until then", "sounding %.0f, asked for %.0f", static_cast<double>(m.soundingVoice()), static_cast<double>(m.params().voice));
    Take t{{}, {}, 48000.0};
    m.noteOn(60, 100);
    run(m, t, 0.05);
    double peak = 0;
    for (float v : t.l) peak = std::max(peak, static_cast<double>(std::fabs(v)));
    check(peak > 0.01 && m.activeVoices() == 1, "and plays", "peak %.3f", peak);
    double waited = 0;
    for (; waited < 20000.0 && m.soundingVoice() != kDance; waited += 5.0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        run(m, t, 0.005);
    }
    check(m.soundingVoice() == kDance && Bank::ready(kDance) != nullptr, "the new voice takes over when its waves are there", "after %.0f ms", waited);
    check(m.activeVoices() == 1, "the note that was sounding rings on", "%.0f voices", static_cast<double>(m.activeVoices()));
}

/** Settings far outside their range must not take the engine outside its tables, nor silence it. */
static void testWildSettings() {
    bool numbers = true;
    double top = 0, quietest = 1e9;
    for (int voice : {static_cast<int>(kFmElectric), static_cast<int>(kWorkstation), 99, -3}) {
        for (int field = 0; field < 4; ++field) {
            for (double wild : {1e6, -1e6}) {
                Piano m;
                m.prepare(48000.0);
                PianoParams p = plain();
                p.voice = voice;
                if (field == 0) p.tone = wild;
                else if (field == 1) p.detune = wild;
                else if (field == 2) p.volume = wild > 0 ? wild : 0.0;
                else p.octave = static_cast<int>(wild);
                m.setParams(p);
                Take t{{}, {}, 48000.0};
                m.noteOn(60, 127);
                m.noteOn(64, 1);
                run(m, t, 0.2);
                double peak = 0;
                for (size_t i = 0; i < t.l.size(); ++i) {
                    uint32_t a, b;
                    std::memcpy(&a, &t.l[i], 4);
                    std::memcpy(&b, &t.r[i], 4);
                    numbers = numbers && (a & 0x7f800000u) != 0x7f800000u && (b & 0x7f800000u) != 0x7f800000u;
                    peak = std::max(peak, static_cast<double>(std::max(std::fabs(t.l[i]), std::fabs(t.r[i]))));
                }
                top = std::max(top, peak);
                quietest = std::min(quietest, peak);
            }
        }
    }
    check(numbers && top < 8.0 && quietest > 1e-4, "settings far out of range: sound, bounded, no crash", "peaks between %.4f and %.3f", quietest, top);
}

/** "Reset all controllers" puts the pitch bend and the pedal back. */
static void testResetControllers() {
    Piano a, b;
    a.prepare(48000.0);
    b.prepare(48000.0);
    a.setParams(plain());
    b.setParams(plain());
    b.pitchBend(1.0);
    b.controller(64, 127);
    b.controller(121, 0);
    Take ta{{}, {}, 48000.0}, tb{{}, {}, 48000.0};
    a.noteOn(60, 100);
    b.noteOn(60, 100);
    run(a, ta, 0.3);
    run(b, tb, 0.3);
    size_t different = 0;
    for (size_t i = 0; i < ta.l.size(); ++i) different += ta.l[i] != tb.l[i] ? 1 : 0;
    check(different == 0, "controller 121 undoes the pitch bend", "%.0f samples differ from a note played straight", static_cast<double>(different));
    b.noteOff(60);
    run(b, tb, 3.0);
    check(b.activeVoices() == 0, "and lifts the pedal", "%.0f voices still sounding", static_cast<double>(b.activeVoices()));
}

int main() {
    testRealtimeVoiceChange();
    testSpeed();
    testFm();
    testWaves(kStage, "Stage");
    testWaves(kDance, "Dance");
    testWaves(kGrand, "Grand");
    testGrand();
    testPitch();
    testLevels();
    testEnvelope();
    testStereo();
    testVoices();
    testWildSettings();
    testResetControllers();
    std::printf("%s\n", failures ? "FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
