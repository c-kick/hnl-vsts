// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// Offline measurements of the sampler: the document as text, the engine's sound, and the core
// that reads files and hands documents to the audio thread. Build and run with tests/run.sh.
#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h>
#endif

#include "../dsp/Sampler.hpp"

using namespace hnl;
using namespace hnl::sampler;

static int failures = 0;

static void check(bool ok, const std::string& name, const char* fmt = "", double a = 0, double b = 0, double c = 0, double d = 0) {
    std::printf("%s  %-58s ", ok ? "PASS" : "FAIL", name.c_str());
    std::printf(fmt, a, b, c, d);
    std::printf("\n");
    if (!ok) ++failures;
}

typedef std::vector<float> Buf;
const double kTwoPi = 6.283185307179586;

/** An engine with a document of its own and room to record every output. */
struct Rig {
    std::unique_ptr<Engine> engine{new Engine()};
    std::unique_ptr<Instrument> doc{new Instrument()};
    double fs;
    Buf out[2 * kOutputs];

    explicit Rig(double rate = 48000.0) : fs(rate) {
        engine->prepare(fs);
        engine->setDocument(doc.get());
    }
    /** Adds a sound to the document and a zone for it in a part; returns the zone. */
    Zone& zone(int part, const std::shared_ptr<AudioData>& data, int lo = 0, int hi = 127, int root = 60) {
        Sample s;
        s.id = doc->nextId++;
        s.path = "mem" + std::to_string(s.id);
        s.data = data;
        doc->samples.push_back(s);
        Zone z;
        z.id = doc->nextId++;
        z.sample = s.id;
        z.v[kZLoKey] = lo; z.v[kZHiKey] = hi; z.v[kZRoot] = root;
        doc->parts[part].zones.push_back(z);
        return doc->parts[part].zones.back();
    }
    void run(double seconds) {
        const size_t n = static_cast<size_t>(seconds * fs), from = out[0].size();
        for (Buf& b : out) b.resize(from + n);
        for (size_t done = 0; done < n; done += 256) {
            float* o[2 * kOutputs];
            for (int c = 0; c < 2 * kOutputs; ++c) o[c] = out[c].data() + from + done;
            engine->process(o, static_cast<int>(std::min<size_t>(256, n - done)));
        }
    }
    void clear() { for (Buf& b : out) b.clear(); }
};

static std::shared_ptr<AudioData> sine(double hz, double seconds, double amp = 0.5, double rate = 48000.0, int channels = 1) {
    auto d = std::make_shared<AudioData>();
    d->channels = channels;
    d->sampleRate = rate;
    const size_t n = static_cast<size_t>(seconds * rate);
    for (int c = 0; c < channels; ++c) {
        d->samples[c].resize(n);
        for (size_t i = 0; i < n; ++i) d->samples[c][i] = static_cast<float>(amp * std::sin(kTwoPi * hz * i / rate + c * 1.0));
    }
    return d;
}

static double peak(const Buf& x, size_t a = 0, size_t b = ~size_t(0)) {
    double p = 0;
    for (size_t i = a; i < std::min(b, x.size()); ++i) p = std::max(p, static_cast<double>(std::fabs(x[i])));
    return p;
}
static double rms(const Buf& x, size_t a, size_t b) {
    double s = 0;
    b = std::min(b, x.size());
    for (size_t i = a; i < b; ++i) s += static_cast<double>(x[i]) * x[i];
    return std::sqrt(s / std::max<size_t>(1, b - a));
}
/** The amplitude of one frequency in a stretch of sound. */
static double level(const Buf& x, double fs, double hz, size_t a, size_t b) {
    double re = 0, im = 0;
    b = std::min(b, x.size());
    for (size_t i = a; i < b; ++i) {
        const double w = 0.5 - 0.5 * std::cos(kTwoPi * (i - a) / (b - a));
        re += w * x[i] * std::cos(kTwoPi * hz * i / fs);
        im += w * x[i] * std::sin(kTwoPi * hz * i / fs);
    }
    return 4.0 * std::sqrt(re * re + im * im) / (b - a);
}
/** The frequency of a steady tone, from where it crosses zero going up. */
static double pitchOf(const Buf& x, double fs, size_t a, size_t b) {
    double first = -1, last = -1;
    int count = 0;
    b = std::min(b, x.size());
    for (size_t i = a + 1; i < b; ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f) {
            const double at = (i - 1) + (-x[i - 1]) / (x[i] - x[i - 1]);
            if (first < 0) first = at;
            last = at;
            ++count;
        }
    return count < 2 ? 0.0 : (count - 1) * fs / (last - first);
}
static double cents(double hz, double want) { return 1200.0 * std::log2(hz / want); }
/** The largest step between two samples in a row. */
static double largestStep(const Buf& x, size_t a, size_t b) {
    double s = 0;
    for (size_t i = a + 1; i < std::min(b, x.size()); ++i) s = std::max(s, static_cast<double>(std::fabs(x[i] - x[i - 1])));
    return s;
}

// ------------------------------------------------------------------- tests

static void documentAsText() {
    Instrument doc;
    doc.name = "Kit \xC3\xA9";
    doc.macroNames[2] = "Brightness";
    doc.g[kGDelayFeedback] = 55;
    doc.tuning[61] = -13.5;
    doc.tuningName = "Test";
    const int id = doc.useSample("C:\\Sounds\\kick one.wav");
    doc.parts[3].name = "Drums";
    doc.parts[3].v[kPCutoff] = 1234.5;
    doc.parts[3].v[slotParam(2, kSlotSource)] = kSrcLfo2;
    Zone z;
    z.sample = id;
    z.v[kZLoKey] = 36; z.v[kZHiKey] = 40; z.v[kZLoopEnd] = 123456789.0;
    doc.parts[3].zones.push_back(z);
    const std::string text = toText(doc);
    Instrument back;
    const bool ok = fromText(text, back);
    check(ok && toText(back) == text, "document: text and back gives the same text", "%.0f bytes", static_cast<double>(text.size()));
    check(back.parts[3].zones.size() == 1 && back.parts[3].zones[0].sample == id && back.parts[3].zones[0].v[kZLoopEnd] == 123456789.0 &&
          back.samples.size() == 1 && back.samples[0].path == "C:\\Sounds\\kick one.wav" && back.tuning[61] == -13.5 && back.macroNames[2] == "Brightness",
          "document: zone, sample path, tuning and names survive");
    Instrument odd;
    check(!fromText("something else\nname = x\n", odd) && !fromText("", odd), "document: other text is refused");
    const bool read = fromText("HNL Sampler instrument\npart 2\np.cutoff = 1e9\np.level = abc\np.nonsense = 3\np.mode = 1,4\nzone\nz.lokey = 90\nz.hikey = 20\nz.sample = 77\n"
                               "part 99\np.level = -6\ng.delay_feedback = -5\n", odd);
    check(read && odd.parts[1].v[kPCutoff] == 20000 && odd.parts[1].v[kPLevel] == 0 && odd.parts[1].v[kPMode] == 1 && odd.parts[1].zones[0].v[kZLoKey] == 20 &&
          odd.parts[1].zones[0].v[kZHiKey] == 90 && odd.parts[1].zones[0].sample == 0 && odd.g[kGDelayFeedback] == 0,
          "document: values clamped, unknown lines skipped, zone put in order");
    bool tables = partInfo().size() == static_cast<size_t>(kPartParamCount) && zoneInfo().size() == static_cast<size_t>(kZoneParamCount) &&
                  globalInfo().size() == static_cast<size_t>(kGlobalParamCount);
    for (const std::vector<Info>* t : {&partInfo(), &zoneInfo(), &globalInfo()})
        for (size_t i = 0; i < t->size(); ++i) {
            const Info& p = (*t)[i];
            tables = tables && p.min < p.max && p.def >= p.min && p.def <= p.max && (p.kind != kLog || p.min > 0) && (p.kind != kChoice || p.choices != nullptr);
            tables = tables && std::fabs(fromNorm(p, toNorm(p, p.def)) - p.def) <= 1e-6 * (p.max - p.min);
            for (size_t j = 0; j < i; ++j) tables = tables && (*t)[j].symbol != p.symbol;
        }
    check(tables, "settings: counts, ranges, defaults, knob scale, unique symbols", "%.0f per part", kPartParamCount);
}

/** Zones that cross over into each other: a fade over keys or velocities at an end of a zone's range. */
static void fades() {
    // Two zones that share four keys, one fading out over them and the other in: together the same power on every key.
    Zone low, high;
    low.v[kZLoKey] = 48; low.v[kZHiKey] = 60; low.v[kZFadeHiKey] = 4;
    high.v[kZLoKey] = 57; high.v[kZHiKey] = 72; high.v[kZFadeLoKey] = 4;
    double worst = 0;
    for (int key = 48; key <= 72; ++key) {
        const double a = key <= 60 ? zoneFade(low.v, key, 100) : 0.0, b = key >= 57 ? zoneFade(high.v, key, 100) : 0.0;
        worst = std::max(worst, std::fabs(a * a + b * b - 1.0));
    }
    check(worst < 1e-6 && zoneFade(low.v, 56, 100) == 1.0f && zoneFade(low.v, 60, 100) < zoneFade(low.v, 59, 100), "fades: a zone fading out and its neighbour fading in keep the power (worst)", "%.2g", worst);
    Zone plain;
    check(zoneFade(plain.v, 0, 1) == 1.0f && zoneFade(plain.v, 127, 127) == 1.0f, "fades: a zone without fades is at full level everywhere", "%.3f", zoneFade(plain.v, 0, 1));

    // In the engine: a key inside the fade is softer by what the fade says, a key outside it is not touched.
    Rig r;
    Zone& z = r.zone(0, sine(261.6256, 3.0), 48, 60, 60);
    z.v[kZFadeHiKey] = 4;
    z.v[kZKeyTrack] = 0; // the same pitch on every key, so that only the level differs
    double got[2];
    const int keys[2] = {50, 59};
    for (int i = 0; i < 2; ++i) {
        r.clear();
        r.engine->controller(0, 120, 0);
        r.engine->noteOn(0, keys[i], 127);
        r.run(0.5);
        got[i] = rms(r.out[0], 4800, 22000);
    }
    const double want = std::sin(1.5707963267948966 * 2.0 / 5.0);
    check(std::fabs(20 * std::log10(got[1] / got[0] / want)) < 0.1, "fades: a key one from the end of a fade of four plays that much softer", "%.2f dB", 20 * std::log10(got[1] / got[0]));
    z.v[kZFadeHiKey] = 0;
    z.v[kZFadeLoVel] = 20;
    r.doc->parts[0].v[kPVelocity] = 0; // touch itself changes nothing here
    const int vels[2] = {127, 10};
    for (int i = 0; i < 2; ++i) {
        r.clear();
        r.engine->controller(0, 120, 0);
        r.engine->noteOn(0, 50, vels[i]);
        r.run(0.5);
        got[i] = rms(r.out[0], 4800, 22000);
    }
    const double soft = std::sin(1.5707963267948966 * 10.0 / 21.0);
    check(std::fabs(20 * std::log10(got[1] / got[0] / soft)) < 0.1, "fades: and so does a soft note inside a fade over velocities", "%.2f dB", 20 * std::log10(got[1] / got[0]));
}

static void pitchAndLevel() {
    for (double fs : {44100.0, 48000.0, 96000.0}) {
        Rig r(fs);
        r.zone(0, sine(261.6256, 3.0));
        r.engine->noteOn(0, 60, 127);
        r.run(1.0);
        const size_t a = static_cast<size_t>(0.2 * fs), b = static_cast<size_t>(0.9 * fs);
        const double hz = pitchOf(r.out[0], fs, a, b), amp = level(r.out[0], fs, 261.6256, a, b);
        char name[80];
        std::snprintf(name, sizeof(name), "root key at %.0f Hz: pitch and level as recorded", fs);
        check(std::fabs(cents(hz, 261.6256)) < 0.5 && std::fabs(20 * std::log10(amp / 0.5)) < 0.1, name, "%.2f cents, %.3f dB", cents(hz, 261.6256), 20 * std::log10(amp / 0.5));
    }
    Rig r;
    r.zone(0, sine(261.6256, 3.0));
    double worst = 0;
    for (int key : {36, 48, 67, 72, 84}) {
        r.clear();
        r.engine->controller(0, 120, 0);
        r.engine->noteOn(0, key, 127);
        r.run(0.5);
        const double want = 261.6256 * std::exp2((key - 60) / 12.0);
        worst = std::max(worst, std::fabs(cents(pitchOf(r.out[0], 48000, 4800, 22000), want)));
    }
    check(worst < 1.0, "other keys: an octave up is twice the frequency (worst)", "%.2f cents", worst);

    r.doc->parts[0].zones[0].v[kZTune] = 50;
    r.doc->parts[0].v[kPFine] = -20;
    r.doc->tuning[60] = 7;
    r.clear();
    r.engine->controller(0, 120, 0);
    r.engine->noteOn(0, 60, 127);
    r.run(0.5);
    const double off = cents(pitchOf(r.out[0], 48000, 4800, 22000), 261.6256);
    check(std::fabs(off - 37.0) < 0.5, "zone tune, part fine and the tuning table add up", "%.2f cents (37)", off);
}

static void interpolation() {
    // A pure tone played a fifth up: what is in the output besides that tone.
    Rig r;
    r.zone(0, sine(1000.0, 3.0));
    r.engine->noteOn(0, 67, 127);
    r.run(1.0);
    // The tone itself is fitted (a sine and a cosine at its frequency) and taken out; what stays is counted.
    const double hz = 1000.0 * std::exp2(7.0 / 12.0);
    double cc = 0, ss = 0, cs = 0, xc = 0, xs = 0;
    for (size_t i = 9600; i < 43200; ++i) {
        const double c = std::cos(kTwoPi * hz * i / 48000.0), sn = std::sin(kTwoPi * hz * i / 48000.0);
        cc += c * c; ss += sn * sn; cs += c * sn; xc += r.out[0][i] * c; xs += r.out[0][i] * sn;
    }
    const double det = cc * ss - cs * cs, fa = (xc * ss - xs * cs) / det, fb = (xs * cc - xc * cs) / det;
    double rest = 0;
    for (size_t i = 9600; i < 43200; ++i) {
        const double e = r.out[0][i] - fa * std::cos(kTwoPi * hz * i / 48000.0) - fb * std::sin(kTwoPi * hz * i / 48000.0);
        rest += e * e;
    }
    const double residue = 20 * std::log10(std::sqrt(rest / 33600.0) / std::sqrt(0.5 * (fa * fa + fb * fb)));
    check(residue < -70.0, "a tone played a fifth up stays a tone (everything else)", "%.1f dB", residue);

    // A high tone played two octaves up would land above half the sample rate: it must be gone, not folded back.
    Rig h;
    h.zone(0, sine(9000.0, 3.0));
    h.engine->noteOn(0, 84, 127);
    h.run(0.5);
    const double folded = 20 * std::log10(rms(h.out[0], 4800, 20000) / 0.3536);
    check(folded < -50.0, "9 kHz played two octaves up does not fold back", "%.1f dB", folded);
    Rig l;
    l.zone(0, sine(2000.0, 3.0));
    l.engine->noteOn(0, 84, 127);
    l.run(0.5);
    const double kept = 20 * std::log10(level(l.out[0], 48000, 8000.0, 4800, 20000) / 0.5);
    check(std::fabs(kept) < 1.0, "while 2 kHz played two octaves up keeps its level", "%.2f dB", kept);

    Rig s;
    s.zone(0, sine(500.0, 2.0, 0.5, 48000.0, 2));
    s.engine->noteOn(0, 60, 127);
    s.run(0.3);
    double diff = 0;
    for (size_t i = 2000; i < 12000; ++i) diff = std::max(diff, static_cast<double>(std::fabs(s.out[0][i] - s.out[1][i])));
    check(diff > 0.2, "a stereo sound keeps its two sides apart", "%.3f", diff);
}

static void loops() {
    const double hz = 480.0; // 100 frames a period at 48 kHz
    for (int type = 1; type <= 3; ++type) {
        Rig r;
        Zone& z = r.zone(0, sine(hz, 1.0));
        z.v[kZLoop] = type; z.v[kZLoopStart] = 10000; z.v[kZLoopEnd] = 20000;
        r.engine->noteOn(0, 60, 127);
        r.run(3.0);
        const double sounding = rms(r.out[0], 120000, 144000), step = largestStep(r.out[0], 9000, 144000), own = 0.5 * kTwoPi * hz / 48000.0;
        char name[80];
        std::snprintf(name, sizeof(name), "loop %s: sounds on after the sound's end, no jump", kLoopNames[type]);
        // Forward joins whole periods; the turning points of the other two bend the wave but do not jump.
        check(sounding > 0.3 && step < 1.3 * own, name, "rms %.3f, step %.4f (own %.4f)", sounding, step, own);
    }
    {   // The first time round, what follows the loop's end is the loop's start, not the start of the sound.
        // A sound that begins with silence, played a little off its own pitch so that it is read between samples;
        // at several pitches, as how near the last sample read lies to the loop's end decides how much a wrong one weighs.
        double worst = 0.0;
        for (int tune = 5; tune <= 95; tune += 10) {
            Rig r;
            std::shared_ptr<AudioData> late = sine(hz, 1.0);
            for (size_t i = 0; i < 5000; ++i) late->samples[0][i] = 0.0f;
            Zone& z = r.zone(0, late);
            z.v[kZLoop] = 1; z.v[kZLoopStart] = 10000; z.v[kZLoopEnd] = 20000; z.v[kZTune] = tune;
            r.engine->noteOn(0, 60, 127);
            r.run(0.6);
            worst = std::max(worst, largestStep(r.out[0], 9000, 28000) / (0.5 * kTwoPi * hz * std::exp2(tune / 1200.0) / 48000.0));
        }
        check(worst < 1.05, "loop Forward: no jump the first time round when the sound begins otherwise", "largest step %.3f times the wave's own", worst);
    }
    {
        Rig r;
        r.zone(0, sine(hz, 1.0));
        r.engine->noteOn(0, 60, 127);
        r.run(1.5);
        check(rms(r.out[0], 50000, 70000) < 1e-6 && r.engine->activeVoices() == 0, "no loop: silence and no voice left after the sound's end", "%.0f voices", r.engine->activeVoices());
    }
    {   // A loop that does not fit: 97.75 periods. Without crossfade it jumps, with it it does not.
        double steps[2];
        for (int pass = 0; pass < 2; ++pass) {
            Rig r;
            Zone& z = r.zone(0, sine(hz, 1.0));
            z.v[kZLoop] = 1; z.v[kZLoopStart] = 10000; z.v[kZLoopEnd] = 19775; z.v[kZCrossfade] = pass == 0 ? 0 : 2000;
            r.engine->noteOn(0, 60, 127);
            r.run(2.0);
            steps[pass] = largestStep(r.out[0], 9000, 96000);
        }
        check(steps[0] > 0.3 && steps[1] < 0.05, "crossfade: a loop that does not fit jumps without it, not with it", "%.3f -> %.3f", steps[0], steps[1]);
    }
    {   // Loop only while the key is down.
        Rig r;
        Zone& z = r.zone(0, sine(hz, 1.0));
        z.v[kZLoop] = 1; z.v[kZLoopStart] = 10000; z.v[kZLoopEnd] = 20000; z.v[kZLoopRelease] = 0;
        r.doc->parts[0].v[envParam(0, kEnvRelease)] = 20000;
        r.engine->noteOn(0, 60, 127);
        r.run(1.0);
        r.engine->noteOff(0, 60, 0);
        r.run(1.5);
        check(r.engine->activeVoices() == 0, "loop not in release: the sound plays on to its end after the key", "%.0f voices", r.engine->activeVoices());
    }
    {   // Loop start and length moved while playing: a shorter loop is a higher buzz.
        Rig r;
        auto ramp = std::make_shared<AudioData>();
        ramp->channels = 1; ramp->sampleRate = 48000; ramp->samples[0].resize(48000);
        for (int i = 0; i < 48000; ++i) ramp->samples[0][i] = static_cast<float>((i % 4800) / 480.0 - 0.5);
        Zone& z = r.zone(0, ramp);
        z.v[kZLoop] = 1; z.v[kZLoopStart] = 0; z.v[kZLoopEnd] = 4800;
        r.doc->parts[0].v[kPLoopLength] = 10;
        r.engine->noteOn(0, 60, 127);
        r.run(1.0);
        const double hzShort = pitchOf(r.out[0], 48000, 24000, 48000);
        check(std::fabs(hzShort - 100.0) < 1.0, "loop length 10%: the loop turns ten times as fast", "%.1f Hz (100)", hzShort);
    }
}

static void reverseAndStart() {
    auto ramp = std::make_shared<AudioData>();
    ramp->channels = 1; ramp->sampleRate = 48000; ramp->samples[0].resize(24000);
    for (int i = 0; i < 24000; ++i) ramp->samples[0][i] = static_cast<float>(i / 24000.0);
    Rig r;
    r.doc->parts[0].v[kPVelocity] = 0;
    Zone& z = r.zone(0, ramp);
    r.engine->noteOn(0, 60, 100);
    r.run(0.25);
    const double up = r.out[0][9600] - r.out[0][2400];
    z.v[kZReverse] = 1;
    r.clear();
    r.engine->controller(0, 120, 0);
    r.engine->noteOn(0, 60, 100);
    r.run(0.25);
    const double down = r.out[0][9600] - r.out[0][2400], startsAt = r.out[0][480];
    check(up > 0.25 && down < -0.25 && startsAt > 0.9, "reverse: the sound runs backwards from its end", "%.3f, %.3f, starts at %.3f", up, down, startsAt);
    z.v[kZReverse] = 0;
    z.v[kZStart] = 12000; z.v[kZEnd] = 18000;
    r.clear();
    r.engine->controller(0, 120, 0);
    r.engine->noteOn(0, 60, 100);
    r.run(0.25);
    check(std::fabs(r.out[0][480] - 0.52) < 0.01 && peak(r.out[0], 6200, 12000) < 1e-6, "start and end: only that stretch plays", "%.3f at 10 ms", r.out[0][480]);
    z.v[kZStart] = 0; z.v[kZEnd] = 0;
    r.doc->parts[0].v[kPStart] = 50;
    r.clear();
    r.engine->controller(0, 120, 0);
    r.engine->noteOn(0, 60, 100);
    r.run(0.1);
    check(std::fabs(r.out[0][480] - 0.52) < 0.01, "part start 50%: the note begins halfway into the sound", "%.3f", r.out[0][480]);
}

static void zones() {
    Rig r;
    Zone& low = r.zone(0, sine(300.0, 2.0), 0, 59, 48);
    Zone& high = r.zone(0, sine(700.0, 2.0), 60, 127, 72);
    (void)low;
    r.engine->noteOn(0, 48, 100);
    r.run(0.3);
    const double a = pitchOf(r.out[0], 48000, 2400, 14000);
    r.clear();
    r.engine->controller(0, 120, 0);
    r.engine->noteOn(0, 72, 100);
    r.run(0.3);
    const double b = pitchOf(r.out[0], 48000, 2400, 14000);
    check(std::fabs(a - 300) < 1 && std::fabs(b - 700) < 1, "key ranges choose the zone", "%.1f and %.1f Hz", a, b);

    high.v[kZLoVel] = 100;
    r.clear();
    r.engine->controller(0, 120, 0);
    r.engine->noteOn(0, 72, 50);
    r.run(0.1);
    const double soft = peak(r.out[0]);
    r.engine->noteOn(0, 72, 110);
    r.run(0.1);
    check(soft == 0.0 && peak(r.out[0]) > 0.1, "velocity ranges choose the zone", "%.3f, %.3f", soft, peak(r.out[0]));

    {   // Two zones that take turns.
        Rig t;
        Zone& one = t.zone(0, sine(300.0, 2.0));
        one.v[kZSeqLength] = 2; one.v[kZSeqPosition] = 1;
        Zone& two = t.zone(0, sine(700.0, 2.0));
        two.v[kZSeqLength] = 2; two.v[kZSeqPosition] = 2;
        double hz[4];
        for (int i = 0; i < 4; ++i) {
            t.clear();
            t.engine->controller(0, 120, 0);
            t.engine->noteOn(0, 60, 100);
            t.run(0.2);
            hz[i] = pitchOf(t.out[0], 48000, 2400, 9000);
        }
        check(std::fabs(hz[0] - 300) < 2 && std::fabs(hz[1] - 700) < 2 && std::fabs(hz[2] - 300) < 2 && std::fabs(hz[3] - 700) < 2, "round robin: the zones take turns", "%.0f %.0f %.0f", hz[0], hz[1], hz[2]);
    }
    {   // An open hi-hat cut off by the closed one.
        Rig t;
        Zone& open = t.zone(0, sine(300.0, 2.0), 46, 46, 46);
        open.v[kZOffBy] = 1; open.v[kZOneShot] = 1;
        Zone& closed = t.zone(0, sine(700.0, 0.05), 42, 42, 42);
        closed.v[kZGroup] = 1; closed.v[kZOneShot] = 1;
        t.engine->noteOn(0, 46, 100);
        t.engine->noteOff(0, 46, 0);
        t.run(0.5);
        const double rings = rms(t.out[0], 20000, 24000);
        t.engine->noteOn(0, 42, 100);
        t.run(0.3);
        check(rings > 0.2 && rms(t.out[0], 34000, 38000) < 1e-4, "one shot rings on after the key; its group cuts it off", "%.3f -> %.5f", rings, rms(t.out[0], 34000, 38000));
    }
    {   // A sound for letting go of the key.
        Rig t;
        Zone& z = t.zone(0, sine(700.0, 0.2));
        z.v[kZTrigger] = 1;
        t.engine->noteOn(0, 60, 100);
        t.run(0.2);
        const double before = peak(t.out[0]);
        t.engine->noteOff(0, 60, 0);
        t.run(0.1);
        check(before == 0.0 && rms(t.out[0], 10000, 14000) > 0.2, "release trigger: sounds when the key is let go", "%.3f", rms(t.out[0], 10000, 14000));
    }
}

static void envelopeAndPedal() {
    Rig r;
    r.zone(0, sine(1000.0, 10.0));
    double* P = r.doc->parts[0].v;
    P[kPVelocity] = 0;
    P[envParam(0, kEnvAttack)] = 100; P[envParam(0, kEnvDecay)] = 200; P[envParam(0, kEnvSustain)] = 25; P[envParam(0, kEnvRelease)] = 300;
    r.engine->noteOn(0, 60, 100);
    r.run(1.0);
    const double early = rms(r.out[0], 2160, 2640) / 0.3536, top = rms(r.out[0], 4700, 5100) / 0.3536, held = rms(r.out[0], 40000, 44000) / 0.3536;
    r.engine->noteOff(0, 60, 0);
    r.run(1.0);
    const double gone = rms(r.out[0], 48000 + 16000, 48000 + 18000) / 0.3536;
    check(early > 0.3 && early < 0.75 && top > 0.9 && std::fabs(held - 0.25) < 0.01 && gone < 0.002 && r.engine->activeVoices() == 0,
          "envelope: attack 100 ms, sustain 25%, gone 300 ms after the key", "%.2f at 50 ms, top %.2f, held %.3f", early, top, held);

    r.clear();
    r.engine->controller(0, 64, 127);
    r.engine->noteOn(0, 60, 100);
    r.run(0.5);
    r.engine->noteOff(0, 60, 0);
    r.run(0.5);
    const double pedal = rms(r.out[0], 40000, 44000);
    r.engine->controller(0, 64, 0);
    r.run(1.0);
    check(pedal > 0.05 && r.engine->activeVoices() == 0, "sustain pedal holds the note until it comes up", "%.3f", pedal);

    P[kPVelocity] = 100;
    r.clear();
    r.engine->noteOn(0, 60, 127);
    r.run(0.5);
    const double loud = rms(r.out[0], 20000, 23000);
    r.engine->controller(0, 120, 0);
    r.engine->noteOn(0, 60, 64);
    r.run(0.5);
    const double softer = rms(r.out[0], 44000, 47000);
    check(std::fabs(20 * std::log10(softer / loud) + 11.9) < 0.5, "velocity 64 at full sensitivity is 12 dB down", "%.1f dB", 20 * std::log10(softer / loud));
}

static void oscillatorAndFilter() {
    Rig r;
    double* P = r.doc->parts[0].v;
    P[kPOscLevel] = 100; P[kPVelocity] = 0;
    r.engine->noteOn(0, 69, 100);
    r.run(0.5);
    const double f1 = level(r.out[0], 48000, 440, 4800, 24000), f2 = level(r.out[0], 48000, 880, 4800, 24000);
    check(std::fabs(cents(pitchOf(r.out[0], 48000, 4800, 24000), 440)) < 1 && std::fabs(f2 / f1 - 0.5) < 0.02, "oscillator: a sawtooth at A 440 without samples", "2nd/1st %.3f", f2 / f1);

    // A sawtooth near the top: what is not one of its overtones is what folded back.
    r.clear();
    r.engine->controller(0, 120, 0);
    r.engine->noteOn(0, 108, 100); // 4186 Hz
    r.run(0.5);
    const double base = 440.0 * std::exp2(39.0 / 12.0);
    double folded = 0;
    for (double hz : {1000.0, 3000.0, 5500.0, 7000.0}) folded = std::max(folded, level(r.out[0], 48000, hz, 4800, 24000));
    check(20 * std::log10(folded / level(r.out[0], 48000, base, 4800, 24000)) < -40, "sawtooth at 4 kHz: little folds back between its overtones", "%.1f dB",
          20 * std::log10(folded / level(r.out[0], 48000, base, 4800, 24000)));

    // Noise through each filter: what is left an octave and more beyond the cutoff.
    P[kPOscLevel] = 0; P[kPNoiseLevel] = 100; P[kPCutoff] = 1000; P[kPResonance] = 0;
    double low[9], high[9];
    for (int type = 0; type <= 8; ++type) {
        P[kPFilterType] = type;
        r.clear();
        r.engine->reset(); // the same noise every time
        r.engine->noteOn(0, 60, 100);
        r.run(2.0);
        low[type] = high[type] = 0;
        for (double hz = 100; hz <= 250; hz += 10) low[type] += level(r.out[0], 48000, hz, 9600, 96000);
        for (double hz = 4000; hz <= 10000; hz += 400) high[type] += level(r.out[0], 48000, hz, 9600, 96000);
    }
    const auto db = [](double a, double b) { return 20 * std::log10(a / b); };
    check(db(high[1], high[0]) < -18 && db(high[2], high[1]) < -15 && std::fabs(db(low[1], low[0])) < 1.5, "lowpass 12 and 24: highs down, lows kept", "%.1f, %.1f dB",
          db(high[1], high[0]), db(high[2], high[0]));
    check(db(low[4], low[0]) < -18 && db(low[5], low[4]) < -15 && std::fabs(db(high[4], high[0])) < 1.5, "highpass 12 and 24: lows down, highs kept", "%.1f, %.1f dB",
          db(low[4], low[0]), db(low[5], low[0]));
    check(db(low[3], low[0]) < -8 && db(high[3], high[0]) < -8, "bandpass: both sides down", "%.1f, %.1f dB", db(low[3], low[0]), db(high[3], high[0]));
    P[kPFilterType] = 8;
    double morph[3];
    for (int m = 0; m < 3; ++m) {
        P[kPMorph] = m * 33.3334;
        r.clear();
        r.engine->reset(); // the same noise every time
        r.engine->noteOn(0, 60, 100);
        r.run(1.0);
        double lo = 0, hi = 0;
        for (double hz = 100; hz <= 250; hz += 10) lo += level(r.out[0], 48000, hz, 9600, 48000);
        for (double hz = 4000; hz <= 10000; hz += 400) hi += level(r.out[0], 48000, hz, 9600, 48000);
        morph[m] = db(hi / 16, lo / 16);
    }
    check(morph[0] < -15 && std::fabs(morph[1]) < 6 && morph[2] > 15, "morph: from lowpass by bandpass to highpass", "%.0f, %.0f, %.0f dB highs over lows", morph[0], morph[1], morph[2]);

    // Resonance: a peak at the cutoff.
    P[kPFilterType] = 1; P[kPResonance] = 90;
    r.clear();
    r.engine->reset(); // the same noise every time
    r.engine->noteOn(0, 60, 100);
    r.run(2.0);
    double at = 0, under = 0;
    for (double hz = 950; hz <= 1050; hz += 10) at += level(r.out[0], 48000, hz, 9600, 96000);
    for (double hz = 200; hz <= 300; hz += 10) under += level(r.out[0], 48000, hz, 9600, 96000);
    check(db(at, under) > 10 && peak(r.out[0]) < 4.0, "resonance 90%: a peak at the cutoff, bounded", "%.1f dB, peak %.2f", db(at, under), peak(r.out[0]));

    // Saturation.
    P[kPNoiseLevel] = 0; P[kPFilterType] = 0;
    r.zone(0, sine(1000.0, 5.0, 0.9));
    bool bounded = true;
    double third[5];
    for (int type = 0; type <= 4; ++type) {
        P[kPSatType] = type; P[kPDrive] = 24;
        r.clear();
        r.engine->controller(0, 120, 0);
        r.engine->noteOn(0, 60, 100);
        r.run(0.5);
        third[type] = level(r.out[0], 48000, type == 4 ? 2000 : 3000, 4800, 24000) / level(r.out[0], 48000, 1000, 4800, 24000);
        bounded = bounded && peak(r.out[0]) < 1.5;
    }
    check(bounded && third[0] < 1e-3 && third[1] > 0.1 && third[2] > 0.2 && third[3] > 0.05 && third[4] > 0.02, "saturation: every shape adds overtones and stays bounded",
          "%.3f %.3f %.3f %.3f", third[1], third[2], third[3], third[4]);
}

static void modulation() {
    Rig r;
    r.zone(0, sine(1000.0, 10.0));
    double* P = r.doc->parts[0].v;
    P[kPVelocity] = 0;
    // LFO 1 to pitch, 100 cents either way.
    P[lfoParam(0, kLfoRate)] = 5;
    P[slotParam(0, kSlotSource)] = kSrcLfo1; P[slotParam(0, kSlotDest)] = kDstPitch; P[slotParam(0, kSlotAmount)] = 100.0 / 24.0;
    r.engine->noteOn(0, 60, 100);
    r.run(1.0);
    double lo = 1e9, hi = 0;
    for (size_t a = 9600; a + 960 <= 48000; a += 240) {
        const double hz = pitchOf(r.out[0], 48000, a, a + 960);
        lo = std::min(lo, hz);
        hi = std::max(hi, hz);
    }
    check(cents(hi, 1000) > 80 && cents(hi, 1000) < 105 && cents(lo, 1000) < -80 && cents(lo, 1000) > -105, "matrix: LFO to pitch gives vibrato of the set depth", "%.0f..%.0f cents",
          cents(lo, 1000), cents(hi, 1000));

    // The mod wheel scales that row (via), and can move the row's amount itself.
    P[slotParam(0, kSlotVia)] = kSrcWheel;
    r.clear();
    r.engine->controller(0, 120, 0);
    r.engine->noteOn(0, 60, 100);
    r.run(0.5);
    const double still = std::fabs(cents(pitchOf(r.out[0], 48000, 4800, 24000), 1000));
    P[slotParam(0, kSlotVia)] = kSrcNone; P[slotParam(0, kSlotAmount)] = 0;
    P[slotParam(5, kSlotSource)] = kSrcWheel; P[slotParam(5, kSlotDest)] = kDstSlot1; P[slotParam(5, kSlotAmount)] = 100.0 / 24.0;
    r.engine->controller(0, 1, 127);
    r.clear();
    r.engine->controller(0, 120, 0);
    r.engine->noteOn(0, 60, 100);
    r.run(1.0);
    hi = 0;
    for (size_t a = 9600; a + 960 <= 48000; a += 240) hi = std::max(hi, pitchOf(r.out[0], 48000, a, a + 960));
    check(still < 1.0 && cents(hi, 1000) > 80, "matrix: wheel as via holds a row back; wheel to a row's amount opens it", "%.2f, %.0f cents", still, cents(hi, 1000));
    for (int s = 0; s < kSlots; ++s) P[slotParam(s, kSlotSource)] = 0;
    r.engine->controller(0, 1, 0);

    // A macro to level; velocity to cutoff; pitch bend; steps.
    P[slotParam(0, kSlotSource)] = kSrcMacro1 + 2; P[slotParam(0, kSlotDest)] = kDstLevel; P[slotParam(0, kSlotAmount)] = -100;
    r.engine->setMacro(2, 0.5f);
    r.clear();
    r.engine->controller(0, 120, 0);
    r.engine->noteOn(0, 60, 100);
    r.run(0.5);
    check(std::fabs(level(r.out[0], 48000, 1000, 4800, 24000) - 0.25) < 0.005, "matrix: macro 3 at half, to level at -100%, halves the level", "%.3f",
          level(r.out[0], 48000, 1000, 4800, 24000));
    P[slotParam(0, kSlotSource)] = 0;
    r.engine->pitchBend(0, 1.0);
    r.clear();
    r.engine->controller(0, 120, 0);
    r.engine->noteOn(0, 60, 100);
    r.run(0.5);
    check(std::fabs(cents(pitchOf(r.out[0], 48000, 4800, 24000), 1000) - 200) < 1, "pitch bend: full up is the bend range", "%.1f cents",
          cents(pitchOf(r.out[0], 48000, 4800, 24000), 1000));
    r.engine->pitchBend(0, 0.0);

    P[slotParam(0, kSlotSource)] = kSrcSteps; P[slotParam(0, kSlotDest)] = kDstPitch; P[slotParam(0, kSlotAmount)] = 50;
    P[kPSeqSteps] = 2; P[kPSeqSync] = 5; // quarter notes: half a second at 120
    P[kPSeqValue] = 0; P[kPSeqValue + 1] = 100;
    r.clear();
    r.engine->controller(0, 120, 0);
    r.engine->noteOn(0, 60, 100);
    r.run(1.0);
    const double s1 = cents(pitchOf(r.out[0], 48000, 4800, 20000), 1000), s2 = cents(pitchOf(r.out[0], 48000, 28000, 44000), 1000);
    check(std::fabs(s1) < 1 && std::fabs(s2 - 1200) < 2, "steps: each lasts its note length and moves the pitch", "%.0f, %.0f cents", s1, s2);
    r.engine->setTempo(240);
    r.clear();
    r.engine->controller(0, 120, 0);
    r.engine->noteOn(0, 60, 100);
    r.run(0.5);
    check(std::fabs(cents(pitchOf(r.out[0], 48000, 14000, 22000), 1000) - 1200) < 2, "steps follow the tempo", "%.0f cents", cents(pitchOf(r.out[0], 48000, 14000, 22000), 1000));
}

static void unisonChordMono() {
    Rig r;
    r.zone(0, sine(500.0, 10.0));
    double* P = r.doc->parts[0].v;
    P[kPVelocity] = 0;
    r.engine->noteOn(0, 60, 100);
    r.run(1.0);
    const double one = rms(r.out[0], 9600, 48000);
    P[kPUnison] = 7; P[kPDetune] = 20;
    r.clear();
    r.engine->controller(0, 120, 0);
    r.engine->noteOn(0, 60, 100);
    r.run(4.0);
    const double seven = rms(r.out[0], 9600, 192000);
    double apart = 0;
    for (size_t i = 9600; i < 192000; ++i) apart += std::fabs(r.out[0][i] - r.out[1][i]);
    check(std::fabs(20 * std::log10(seven / one)) < 3 && apart / 182400 > 0.05, "unison 7: about as loud as one, and wide", "%.1f dB, L-R %.3f", 20 * std::log10(seven / one), apart / 182400);

    P[kPUnison] = 1; P[kPChord1] = 7; P[kPChord2] = 12;
    r.clear();
    r.engine->controller(0, 120, 0);
    r.engine->noteOn(0, 60, 100);
    r.run(1.0);
    const double c0 = level(r.out[0], 48000, 500, 9600, 48000), c1 = level(r.out[0], 48000, 500 * std::exp2(7 / 12.0), 9600, 48000), c2 = level(r.out[0], 48000, 1000, 9600, 48000);
    check(c0 > 0.4 && c1 > 0.4 && c2 > 0.4 && r.engine->activeVoices() == 1, "chord: a fifth and an octave on top, in one voice", "%.2f %.2f %.2f", c0, c1, c2);
    P[kPChord1] = 0; P[kPChord2] = 0;

    P[kPMode] = 1; P[kPGlide] = 200;
    r.clear();
    r.engine->controller(0, 120, 0);
    r.engine->noteOn(0, 60, 100);
    r.run(0.3);
    r.engine->noteOn(0, 72, 100);
    r.run(1.0);
    const double mid = pitchOf(r.out[0], 48000, 14400 + 1500, 14400 + 3000), end = pitchOf(r.out[0], 48000, 50000, 60000);
    const int voices = r.engine->activeVoices();
    r.engine->noteOff(0, 72, 0);
    r.run(0.2);
    const double back = pitchOf(r.out[0], 48000, 70000, 72000);
    check(voices == 1 && mid > 520 && mid < 980 && std::fabs(end - 1000) < 2 && std::fabs(back - 500) < 40, "mono with glide: one voice slides up, and back to the key still held",
          "%.0f Hz on the way, %.0f, back at %.0f", mid, end, back);

    // MPE: each note bends with its own channel.
    Rig m;
    m.zone(0, sine(500.0, 10.0));
    m.doc->parts[0].v[kPMpe] = 1;
    m.doc->parts[0].v[kPChannel] = 5; // ignored with MPE on
    m.engine->noteOn(1, 60, 100);
    m.engine->noteOn(2, 72, 100);
    m.engine->pitchBend(2, 0.25); // 12 of 48 semitones
    m.run(0.5);
    const double n1 = level(m.out[0], 48000, 500, 4800, 24000), n2 = level(m.out[0], 48000, 2000, 4800, 24000), n3 = level(m.out[0], 48000, 1000, 4800, 24000);
    check(n1 > 0.3 && n2 > 0.3 && n3 < 0.02, "MPE: a bend on one channel moves only that note", "%.2f %.2f %.3f", n1, n2, n3);
}

static void routingAndEffects() {
    Rig r;
    r.zone(0, sine(500.0, 10.0));
    r.zone(1, sine(800.0, 10.0));
    r.doc->parts[1].v[kPOutput] = 2;
    r.doc->parts[1].v[kPChannel] = 2;
    r.doc->parts[0].v[kPChannel] = 1;
    r.engine->noteOn(0, 60, 100);
    r.engine->noteOn(1, 60, 100);
    r.run(0.5);
    bool othersSilent = true;
    for (int c = 2; c < 2 * kOutputs; ++c)
        if (c != 4 && c != 5) othersSilent = othersSilent && peak(r.out[c]) == 0.0;
    check(level(r.out[0], 48000, 500, 4800, 24000) > 0.2 && level(r.out[0], 48000, 800, 4800, 24000) < 1e-3 && level(r.out[4], 48000, 800, 4800, 24000) > 0.2 && othersSilent,
          "parts: each on its own channel and output pair", "%.3f on 1-2, %.3f on 5-6", level(r.out[0], 48000, 500, 4800, 24000), level(r.out[4], 48000, 800, 4800, 24000));

    r.engine->controller(0, 120, 0);
    r.run(0.1);
    r.clear();
    r.doc->parts[1].v[kPDelaySend] = 100;
    r.doc->g[kGDelayTime] = 250; r.doc->g[kGDelayWidth] = 0;
    r.engine->noteOn(1, 60, 100);
    r.run(0.05);
    r.engine->noteOff(1, 60, 0);
    r.run(1.0);
    const double echo = rms(r.out[0], 13000, 14000), dry = rms(r.out[4], 1000, 2000), tail = rms(r.out[0], 9000, 11000);
    check(echo > 0.1 && tail < 0.01 && dry > 0.1, "delay send: the echo comes back on the main pair a quarter second later", "%.3f (between: %.4f)", echo, tail);

    r.doc->parts[1].v[kPDelaySend] = 0; r.doc->parts[1].v[kPReverbSend] = 100;
    r.clear();
    r.engine->controller(0, 120, 0);
    r.run(0.5);
    r.clear();
    r.engine->noteOn(1, 60, 100);
    r.run(0.05);
    r.engine->noteOff(1, 60, 0);
    r.run(6.0);
    check(rms(r.out[0], 14400, 24000) > 0.003 && rms(r.out[0], 280000, 288000) < 1e-4, "reverb send: a tail that dies away", "%.4f -> %.6f", rms(r.out[0], 14400, 24000),
          rms(r.out[0], 280000, 288000));

    r.engine->setMaster(-20);
    r.doc->parts[1].v[kPReverbSend] = 0;
    r.clear();
    r.engine->noteOn(1, 60, 100);
    r.run(0.5);
    check(std::fabs(20 * std::log10(level(r.out[4], 48000, 800, 9600, 24000) / 0.5) + 20 + 2.69) < 0.3, "master volume applies to every output", "%.2f dB",
          20 * std::log10(level(r.out[4], 48000, 800, 9600, 24000) / 0.5));
}

static void voicesAndDocuments() {
    Rig r;
    Zone& all = r.zone(0, sine(500.0, 1.0));
    all.v[kZLoop] = 1;
    for (int i = 0; i < 100; ++i) r.engine->noteOn(0, 20 + i, 100);
    r.run(0.2);
    check(r.engine->activeVoices() == 32 && peak(r.out[0]) < 40, "a hundred keys: the part keeps to its 32 voices", "%.0f voices, peak %.1f", r.engine->activeVoices(), peak(r.out[0]));
    r.doc->parts[0].v[kPVoices] = 64;
    for (int i = 0; i < 100; ++i) r.engine->noteOn(0, 20 + i, 100);
    r.run(0.2);
    check(r.engine->activeVoices() == Engine::kVoices, "and never more than there are", "%.0f", r.engine->activeVoices());
    r.engine->controller(0, 123, 0);
    r.run(1.0);
    check(r.engine->activeVoices() == 0, "all notes off lets every voice end", "%.0f", r.engine->activeVoices());

    // A new document while a note sounds: the note plays on from the old one, which comes back when the note is over.
    Rig d;
    d.zone(0, sine(500.0, 1.0));
    d.engine->noteOn(0, 60, 100);
    d.run(0.2);
    std::unique_ptr<Instrument> empty(new Instrument());
    d.engine->setDocument(empty.get());
    d.run(0.2);
    const double plays = rms(d.out[0], 12000, 18000);
    const Instrument* early = d.engine->takeReleased();
    d.run(1.0);
    const Instrument* late = d.engine->takeReleased();
    check(plays > 0.2 && early == nullptr && late == d.doc.get() && d.engine->takeReleased() == nullptr, "a replaced document is held while a note plays from it, then handed back", "%.3f", plays);
    d.engine->noteOn(0, 60, 100);
    d.run(0.1);
    check(d.engine->activeVoices() == 0, "and new notes follow the new document", "%d", d.engine->activeVoices());

    // Listening to a file.
    Rig p;
    auto sound = sine(600.0, 0.2, 0.5, 44100.0);
    p.engine->setPreview(sound.get());
    p.run(0.1);
    const double hz = pitchOf(p.out[0], 48000, 480, 4000);
    p.run(0.3);
    check(std::fabs(hz - 600) < 1 && p.engine->takeReleasedPreview() == sound.get() && rms(p.out[0], 12000, 19000) == 0.0, "preview: plays the sound at its own pitch, then lets go of it",
          "%.1f Hz", hz);

    // The same notes in blocks of another length: the same sound.
    Buf first;
    for (int block : {256, 61}) {
        Engine* e = new Engine();
        Instrument* doc = new Instrument();
        doc->parts[0].v[kPOscLevel] = 80; doc->parts[0].v[kPNoiseLevel] = 20; doc->parts[0].v[kPFilterType] = 2; doc->parts[0].v[kPCutoff] = 900;
        doc->parts[0].v[kPFilterEnv] = 60; doc->parts[0].v[kPUnison] = 3; doc->parts[0].v[kPReverbSend] = 30;
        doc->parts[0].v[slotParam(0, kSlotSource)] = kSrcLfo2; doc->parts[0].v[slotParam(0, kSlotDest)] = kDstPan; doc->parts[0].v[slotParam(0, kSlotAmount)] = 80;
        doc->parts[0].v[lfoParam(1, kLfoRetrig)] = 0;
        e->prepare(48000);
        e->setDocument(doc);
        Buf l(24000), rr(24000), spare(24000);
        const int notesAt[3] = {0, 4000, 9000};
        for (int done = 0; done < 24000;) {
            int n = std::min(block, 24000 - done);
            for (int k = 0; k < 3; ++k) {
                if (notesAt[k] == done) e->noteOn(0, 50 + 7 * k, 100);
                else if (notesAt[k] > done && notesAt[k] < done + n) n = notesAt[k] - done;
            }
            float* o[2 * kOutputs];
            o[0] = l.data() + done; o[1] = rr.data() + done;
            for (int c = 2; c < 2 * kOutputs; ++c) o[c] = spare.data();
            e->process(o, n);
            done += n;
        }
        if (first.empty()) first = l;
        else {
            double worst = 0;
            for (int i = 0; i < 24000; ++i) worst = std::max(worst, static_cast<double>(std::fabs(l[i] - first[i])));
            check(worst < 1e-5 && peak(l) > 0.05, "the same notes in blocks of 61 or 256: the same sound", "%.2g apart", worst);
        }
        delete e;
        delete doc;
    }
}

/** Instruments with every setting at a random place in its range, played with random notes and controllers. */
static void randomInstruments() {
    Rng rng;
    rng.s = 0x1234abcdu;
    const auto unit = [&rng] { return 0.5 + 0.5 * static_cast<double>(rng.bipolar()); };
    std::shared_ptr<AudioData> sounds[3] = {sine(220.0, 0.4), sine(1000.0, 0.05, 0.9, 44100.0, 2), sine(60.0, 1.0, 1.0, 96000.0)};
    sounds[2]->samples[0].resize(3); // a sound of three samples
    bool numbers = true;
    double loudest = 0;
    int leftOver = 0;
    for (int round = 0; round < 300; ++round) {
        Rig r(round % 5 == 0 ? 22050.0 : (round % 7 == 0 ? 192000.0 : 48000.0));
        for (int p = 0; p < 2; ++p) {
            Part& part = r.doc->parts[p];
            for (int id = 0; id < kPartParamCount; ++id) {
                const Info& info = partInfo()[static_cast<size_t>(id)];
                const int kind = static_cast<int>(rng.next() % 5u);
                part.v[id] = cleanValue(info, kind == 0 ? info.min : (kind == 1 ? info.max : (kind == 2 ? info.def : fromNorm(info, unit()))));
            }
            part.v[kPMute] = 0;
            part.v[kPChannel] = 0;
            for (int z = 0; z < 3; ++z) {
                Zone& zone = r.zone(p, sounds[rng.next() % 3u]);
                for (int id = 0; id < kZoneParamCount; ++id) {
                    const Info& info = zoneInfo()[static_cast<size_t>(id)];
                    double value = fromNorm(info, unit());
                    if (info.max > 1e6) value = std::floor(unit() * unit() * 30000.0); // places in a sound: inside it and far beyond its end
                    zone.v[id] = cleanValue(info, value);
                }
                zone.v[kZLoKey] = 0; zone.v[kZHiKey] = 127; zone.v[kZLoVel] = 1; zone.v[kZHiVel] = 127;
                zone.v[kZSeqLength] = 1; zone.v[kZTrigger] = z == 2 ? 1 : 0;
            }
        }
        for (int id = 0; id < kGlobalParamCount; ++id) r.doc->g[id] = fromNorm(globalInfo()[static_cast<size_t>(id)], unit());
        for (double& t : r.doc->tuning) t = 2400.0 * rng.bipolar();
        r.engine->setTempo(round % 3 == 0 ? 20.0 : 300.0);
        for (int m = 0; m < kMacros; ++m) r.engine->setMacro(m, static_cast<float>(unit()));
        for (int step = 0; step < 12; ++step) {
            const int what = static_cast<int>(rng.next() % 8u), key = static_cast<int>(rng.next() % 128u), channel = static_cast<int>(rng.next() % 16u);
            if (what < 4) r.engine->noteOn(channel, key, 1 + static_cast<int>(rng.next() % 127u));
            else if (what == 4) r.engine->noteOff(channel, key, 0);
            else if (what == 5) r.engine->pitchBend(channel, rng.bipolar());
            else if (what == 6) r.engine->controller(channel, step % 2 ? 1 : 64, static_cast<int>(rng.next() % 128u));
            else r.engine->channelPressure(channel, static_cast<int>(rng.next() % 128u));
            r.run(0.02);
        }
        for (int c = 0; c < 2 * kOutputs; ++c)
            for (float v : r.out[c]) {
                numbers = numbers && isFinite(v);
                loudest = std::max(loudest, static_cast<double>(std::fabs(v)));
            }
        // Afterwards every note ends: all notes off, the pedal up, and the longest release there is.
        for (int ch = 0; ch < 16; ++ch) { r.engine->controller(ch, 64, 0); r.engine->controller(ch, 123, 0); }
        r.clear();
        r.run(25.0);
        leftOver += r.engine->activeVoices();
    }
    check(numbers && loudest < 2000.0, "300 random instruments: numbers, and bounded (peak)", "%.1f", loudest);
    check(leftOver == 0, "and every note ends after all notes off", "%.0f voices left", leftOver);
}

// ----------------------------------------------------------- the whole core

/** Plays the part of the audio thread until `done` says so, or for at most `seconds`. */
template <class Fn> static bool pump(Sampler& s, double seconds, Fn done) {
    Buf buf(256);
    float* o[2 * kOutputs];
    for (float*& p : o) p = buf.data();
    for (int i = 0; i < static_cast<int>(seconds * 200); ++i) {
        s.process(o, 256);
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

static void core() {
    const fs::path dir = fs::temp_directory_path() / "hnl-sampler-test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const fs::path wav = dir / fs::u8path("t\xC3\xB6ne.wav");
    AudioData tone = *sine(440.0, 1.0);
    tone.rootKey = 69;
    AudioData::Loop loop;
    loop.start = 4800; loop.end = 9600;
    tone.loops.push_back(loop);
    std::string error;
    check(writeWav(wav, tone, 24, error), "core: a test file written");

    std::unique_ptr<Sampler> s(new Sampler());
    s->prepare(48000);
    check(s->document()->name == "Init" && s->document()->parts[0].v[kPOscLevel] == 0 && s->document()->parts[0].zones.empty(), "core: starts empty and silent");
    const uint64_t before = s->revision();
    s->edit([](Instrument& doc) { doc.parts[0].v[kPOscLevel] = 0; });
    s->addZone(0, wav.u8string(), 0, 127, 60);
    const bool loaded = pump(*s, 5.0, [&] { return s->loading() == 0 && s->document()->samples.size() == 1 && s->document()->samples[0].data != nullptr; });
    std::shared_ptr<const Instrument> doc = s->document();
    check(loaded && s->revision() > before, "core: the file is read beside the audio thread");
    check(loaded && doc->parts[0].zones[0].v[kZRoot] == 69 && doc->parts[0].zones[0].v[kZLoop] == 1 && doc->parts[0].zones[0].v[kZLoopStart] == 4800 &&
          doc->parts[0].zones[0].v[kZLoopEnd] == 9600, "core: the zone took root key and loop from the file");

    Buf l(48000), spare(48000);
    float* o[2 * kOutputs];
    o[0] = l.data();
    for (int c = 1; c < 2 * kOutputs; ++c) o[c] = spare.data();
    s->begin();
    s->engine.noteOn(0, 69, 127);
    s->process(o, 48000);
    check(std::fabs(pitchOf(l, 48000, 24000, 46000) - 440) < 0.5 && rms(l, 40000, 46000) > 0.3, "core: and plays, looping past its end", "%.1f Hz", pitchOf(l, 48000, 24000, 46000));

    // The same file in a second sampler is the same memory.
    std::unique_ptr<Sampler> other(new Sampler());
    other->addZone(2, wav.u8string(), 0, 127, 60);
    check(other->document()->samples[0].data == doc->samples[0].data, "core: two samplers share one copy of a sound");

    // Saved as text and read again.
    const std::string text = s->text();
    std::unique_ptr<Sampler> third(new Sampler());
    check(third->setText(text) && third->text() == text && third->document()->samples[0].data != nullptr && !third->setText("nonsense") && third->text() == text,
          "core: text out and in again; text that is no instrument changes nothing");

    // A file that is not there.
    s->addZone(1, (dir / "missing.wav").u8string(), 0, 127, 60);
    const bool flagged = pump(*s, 5.0, [&] { return s->loading() == 0 && s->document()->samples.size() == 2 && s->document()->samples[1].missing; });
    check(flagged, "core: a file that is not there is marked missing");

    // SFZ.
    const fs::path sfz = dir / "set.sfz";
    {
        const std::string body = "<group> ampeg_release=0.5 loop_mode=loop_continuous\n<region> sample=t\xC3\xB6ne.wav lokey=40 hikey=50 pitch_keycenter=45 volume=-6\n"
                                 "<region> sample=t\xC3\xB6ne.wav lokey=51 hikey=60 tune=10\n";
        std::ofstream(sfz, std::ios::binary) << body;
    }
    const bool imported = s->import(3, sfz.u8string(), 0, error);
    pump(*s, 5.0, [&] { return s->loading() == 0; });
    std::shared_ptr<const Instrument> withSfz = s->document();
    check(imported && withSfz->parts[3].zones.size() == 2 && withSfz->parts[3].zones[0].v[kZRoot] == 45 && withSfz->parts[3].zones[0].v[kZGain] == -6 &&
          withSfz->parts[3].zones[1].v[kZRoot] == 69 && withSfz->parts[3].zones[1].v[kZTune] == 10 && withSfz->parts[3].zones[0].v[kZLoop] == 1 &&
          withSfz->parts[3].v[envParam(0, kEnvRelease)] == 500 && withSfz->parts[3].zones[0].v[kZLoopStart] == 4800,
          "core: an SFZ file becomes a part (ranges, root, loop from the file, release)");
    check(!s->import(3, (dir / "none.sfz").u8string(), 0, error) && !error.empty() && s->document()->parts[3].zones.size() == 2, "core: an SFZ that cannot be read leaves the part alone");

    // Edits and loads from other threads while the audio runs: nothing breaks, nothing piles up.
    std::atomic<bool> stop{false};
    std::thread audio([&] {
        Buf buf(64);
        float* out[2 * kOutputs];
        for (float*& p : out) p = buf.data();
        int n = 0;
        while (!stop.load()) {
            s->begin();
            if (n % 7 == 0) s->engine.noteOn(0, 40 + n % 40, 100);
            if (n % 7 == 3) s->engine.noteOff(0, 40 + (n - 3) % 40, 0);
            s->process(out, 64);
            ++n;
        }
    });
    for (int i = 0; i < 400; ++i) {
        s->edit([i](Instrument& d) {
            d.parts[0].v[kPCutoff] = 200.0 + i;
            if (i % 50 == 49 && !d.parts[0].zones.empty()) d.parts[0].zones.pop_back();
        });
        if (i % 50 == 0) s->addZone(0, wav.u8string(), 0, 127, 60);
        if (i % 97 == 0) s->preview(wav.u8string());
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    stop.store(true);
    audio.join();
    s->idle();
    check(s->document()->parts[0].v[kPCutoff] == 599.0, "core: 400 edits beside a running audio thread", "%d zones", static_cast<double>(s->document()->parts[0].zones.size()));

    doc.reset();
    withSfz.reset();
    s.reset();
    other.reset();
    third.reset();
    check(SampleCache::get().find(wav.u8string()) == nullptr, "core: a sound nobody uses any more leaves memory");
    fs::remove_all(dir, ec);
}

// ------------------------------------------------- what an abuse test found

/** A FLAC file of one block of 65535 frames of silence that says it has `total` frames: about 60 bytes. */
static std::vector<uint8_t> flacClaiming(uint64_t total) {
    const auto crc = [](const std::vector<uint8_t>& b, size_t from, unsigned poly, unsigned top) {
        unsigned c = 0;
        for (size_t i = from; i < b.size(); ++i) {
            c ^= static_cast<unsigned>(b[i]) << (top - 7);
            for (int k = 0; k < 8; ++k) c = (c & (1u << top)) ? ((c << 1) ^ poly) : (c << 1);
        }
        return c & ((2u << top) - 1);
    };
    std::vector<uint8_t> b = {'f', 'L', 'a', 'C', 0x80, 0, 0, 34};
    const uint8_t info[34] = {0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0, 0, 0, 0x0B, 0xB8, 0x00, static_cast<uint8_t>(0xF0 | ((total >> 32) & 15)),
                              static_cast<uint8_t>(total >> 24), static_cast<uint8_t>(total >> 16), static_cast<uint8_t>(total >> 8), static_cast<uint8_t>(total)};
    b.insert(b.end(), info, info + 34);
    const size_t at = b.size();
    b.insert(b.end(), {0xFF, 0xF8, 0x7A, 0x08, 0x00, 0xFF, 0xFE});
    b.push_back(static_cast<uint8_t>(crc(b, at, 0x07, 7)));
    b.insert(b.end(), {0x00, 0x20, 0x00}); // a constant subframe
    const unsigned c16 = crc(b, at, 0x8005, 15);
    b.push_back(static_cast<uint8_t>(c16 >> 8));
    b.push_back(static_cast<uint8_t>(c16 & 255));
    return b;
}

static void hardened() {
    const fs::path dir = fs::temp_directory_path() / "hnl-sampler-test-hard";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    std::string error;
    const std::shared_ptr<AudioData> tone = sine(440.0, 1.0);
    const fs::path a = dir / "a.wav", b = dir / "b.wav";
    writeWav(a, *tone, 16, error);
    writeWav(b, *tone, 16, error);
    Buf buf(256);
    float* o[2 * kOutputs];
    for (float*& p : o) p = buf.data();

    // A file that is in no zone, listened to twice while it still plays: the second time must not free it.
    {
        Sampler s;
        s.prepare(48000);
        bool held = true;
        for (int round = 0; round < 3; ++round) {
            s.preview(a.u8string());
            for (int i = 0; i < 40; ++i) { // the reader hands it over, the audio thread takes it, the UI collects
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                s.process(o, 256);
                s.idle();
            }
            held = held && SampleCache::get().find(a.u8string()) != nullptr;
        }
        check(held, "preview: a file listened to again while it plays stays in memory");
        s.preview("");
        const bool gone = pump(s, 3.0, [&] { s.idle(); return SampleCache::get().find(a.u8string()) == nullptr; });
        check(gone, "preview: and leaves memory once it has been stopped");
        // A sound shorter than a block, right after another: both come back, none is forgotten.
        AudioData blip;
        blip.channels = 1;
        blip.sampleRate = 48000;
        blip.samples[0].assign(40, 0.5f);
        writeWav(dir / "blip.wav", blip, 16, error);
        s.preview(a.u8string());
        pump(s, 3.0, [&] { return SampleCache::get().find(a.u8string()) != nullptr; });
        s.process(o, 256);
        s.preview((dir / "blip.wav").u8string());
        const bool both = pump(s, 3.0, [&] { s.idle(); return SampleCache::get().find(a.u8string()) == nullptr && SampleCache::get().find((dir / "blip.wav").u8string()) == nullptr; });
        check(both, "preview: a sound of less than a block after another one: both leave memory");
    }

#ifndef _WIN32
    // A path that names a pipe: opening it would wait for a writer for good.
    {
        const fs::path pipe = dir / "pipe.wav";
        check(::mkfifo(pipe.c_str(), 0600) == 0, "a pipe made for the test");
        AudioData d;
        AudioInfo info;
        const auto t0 = std::chrono::steady_clock::now();
        const bool read = readAudioFile(pipe, d, error), probed = probeAudioFile(pipe, info, nullptr, error);
        std::string text;
        const bool asText = importdetail::readFile(pipe, 1000, text, error);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        check(!read && !probed && !asText && ms < 500, "a pipe is not opened as a sound or as an instrument file", "%.1f ms", ms);
        check(!readAudioFile(dir, d, error) && !readAudioFile("/dev/zero", d, error), "nor a folder or a device");
        Sampler s;
        s.prepare(48000);
        s.addZone(0, pipe.u8string(), 0, 127, 60);
        s.addZone(0, a.u8string(), 0, 127, 60);
        const bool went = pump(s, 5.0, [&] { return s.loading() == 0; });
        std::shared_ptr<const Instrument> doc = s.document();
        check(went && doc->samples.size() == 2 && doc->samples[0].missing && doc->samples[1].data != nullptr, "an instrument that names a pipe: marked missing, and the next sound is read");
    } // and the sampler closes: its reader is not stuck
#endif

    // Memory: one sound, and all of them together.
    {
        using audiofile::fitsInMemory;
        check(fitsInMemory(audiofile::kMaxSoundValues, 1) && !fitsInMemory(audiofile::kMaxSoundValues + 1, 1) && fitsInMemory(audiofile::kMaxSoundValues / 2, 2) &&
              !fitsInMemory(audiofile::kMaxSoundValues / 2 + 1, 2) && !fitsInMemory(audiofile::kMaxSoundValues / 2 + 1, 6), "memory: one sound may take 512 MiB, counted over the channels kept");
        AudioData d;
        const std::vector<uint8_t> claims = flacClaiming(audiofile::kMaxSoundValues + 1), fine = flacClaiming(65535);
        const bool small = decodeAudio(fine.data(), fine.size(), d, error) && d.frames() == 65535;
        error.clear();
        const bool refused = !decodeAudio(claims.data(), claims.size(), d, error) && error == audiofile::kTooLarge && d.frames() == 0;
        check(small && refused, "memory: a FLAC file of 60 bytes that says it is larger than that is refused before anything is set aside", "%.0f bytes", static_cast<double>(claims.size()));

        SampleCache& cache = SampleCache::get();
        const uint64_t one = tone->frames() * sizeof(float);
        cache.setLimit(cache.bytes() + one + one / 2); // room for one of the two
        Sampler s;
        s.prepare(48000);
        s.addZone(0, a.u8string(), 0, 127, 60);
        pump(s, 5.0, [&] { return s.loading() == 0; });
        s.addZone(0, b.u8string(), 0, 127, 60);
        pump(s, 5.0, [&] { return s.loading() == 0; });
        std::shared_ptr<const Instrument> doc = s.document();
        check(doc->samples.size() == 2 && doc->samples[0].data != nullptr && doc->samples[1].missing && doc->samples[1].error == SampleCache::kFull && cache.bytes() >= one && cache.bytes() < 2 * one,
              "memory: a sound beyond what all sounds together may take is not kept, and says why");
        cache.setLimit(SampleCache::kMaxBytes);
    }

    // A path that would not come back from a saved instrument.
    {
        Sampler s;
        const bool newline = s.addZone(0, (dir / "new\nline.wav").u8string(), 0, 127, 60), tab = s.addZone(0, (dir / "tab\there.wav").u8string(), 0, 127, 60);
        const bool longOne = s.addZone(0, "/" + std::string(text::kMaxPath, 'x') + ".wav", 0, 127, 60), plain = s.addZone(0, a.u8string(), 0, 127, 60);
        Sampler again;
        check(!newline && !tab && !longOne && plain && s.document()->samples.size() == 1 && again.setText(s.text()) && again.document()->samples[0].path == a.u8string(),
              "a path with a line end or a tab in it, or too long to save, is not taken; another is, and comes back as it was");
    }

    // A value too small to be a normal number is zero, and so the same after every save.
    {
        double tiny = 1.0, small = 0.0, minus = 1.0;
        const bool parsed = text::parse("9.999888672e-321", tiny) && text::parse("2.5e-300", small) && text::parse("-1e-320", minus);
        check(parsed && tiny == 0.0 && std::fabs(small / 2.5e-300 - 1.0) < 1e-9 && minus == 0.0 && !std::signbit(minus), "text: a number too small to be normal reads as zero");
        Instrument doc, back;
        const std::string in = "HNL Sampler instrument\npart 1\np.mod7_amount = 9.999888672e-321\n";
        const bool ok = fromText(in, doc) && fromText(toText(doc), back);
        check(ok && toText(back) == toText(doc), "text: and an instrument with such a value is the same text after every save");
    }

    // A note on the largest instrument there can be: every part full of zones on every key.
    {
        Sampler s;
        s.prepare(48000);
        s.edit([&](Instrument& doc) {
            const int sample = doc.useSample(a.u8string());
            for (Part& p : doc.parts)
                for (int z = 0; z < kMaxPartZones; ++z) {
                    Zone zone;
                    zone.id = doc.nextId++;
                    zone.sample = sample;
                    p.zones.push_back(zone);
                }
        });
        pump(s, 5.0, [&] { return s.loading() == 0; });
        s.process(o, 256);
        double best = 1e9;
        for (int k = 0; k < 8; ++k) {
            const auto t0 = std::chrono::steady_clock::now();
            s.engine.noteOn(0, 40 + k, 100);
            best = std::min(best, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
        }
        check(s.engine.activeVoices() == 64 && best < 2.0, "a note on 8 parts of 2048 zones each starts no more voices than there are", "%.2f ms, %.0f voices", best, s.engine.activeVoices());
        s.process(o, 256);
    }
    fs::remove_all(dir, ec);
}

int main() {
    documentAsText();
    pitchAndLevel();
    fades();
    interpolation();
    loops();
    reverseAndStart();
    zones();
    envelopeAndPedal();
    oscillatorAndFilter();
    modulation();
    unisonChordMono();
    routingAndEffects();
    voicesAndDocuments();
    randomInstruments();
    core();
    hardened();
    std::printf("%s\n", failures ? "FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
