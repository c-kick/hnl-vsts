// Offline measurements of the thickener. Build and run with tests/run.sh.
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "../dsp/Thickener.hpp"

using namespace hnl;

static int failures = 0;

static void check(bool ok, const std::string& name, const char* fmt, double a = 0, double b = 0, double c = 0) {
    std::printf("%s  %-52s ", ok ? "PASS" : "FAIL", name.c_str());
    std::printf(fmt, a, b, c);
    std::printf("\n");
    if (!ok) ++failures;
}

static void run(Thickener& m, std::vector<float>& l, std::vector<float>& r) {
    const int block = 256;
    for (size_t i = 0; i < l.size(); i += block)
        m.process(&l[i], &r[i], static_cast<int>(std::min<size_t>(block, l.size() - i)));
}

/** A thickener that is set up and at rest. */
struct Unit : Thickener {
    Unit(double fs, const ThickenerParams& p) {
        prepare(fs);
        setParams(p);
        reset();
    }
};

static ThickenerParams noRoom() {
    ThickenerParams p;
    p.room = false;
    return p;
}

/** One full-scale sample on the left, the right or both, then silence. */
static void click(std::vector<float>& l, std::vector<float>& r, size_t n, bool left, bool right) {
    l.assign(n, 0.0f);
    r.assign(n, 0.0f);
    if (left) l[0] = 1.0f;
    if (right) r[0] = 1.0f;
}

static double peak(const std::vector<float>& x, size_t from = 0, size_t to = SIZE_MAX) {
    double m = 0;
    for (size_t i = from; i < std::min(to, x.size()); ++i) m = std::max(m, static_cast<double>(std::fabs(x[i])));
    return m;
}

static double rmsDb(const std::vector<float>& x, size_t from, size_t to) {
    double sum = 0;
    for (size_t i = from; i < to; ++i) sum += static_cast<double>(x[i]) * x[i];
    return linToDb(std::sqrt(sum / static_cast<double>(to - from)));
}

/** Level in dB that a steady tone comes out with, per channel, against going in. */
static void toneGain(double fs, const ThickenerParams& p, double freq, double& left, double& right) {
    Unit m(fs, p);
    const size_t n = static_cast<size_t>(fs);
    std::vector<float> l(n), r(n);
    for (size_t i = 0; i < n; ++i) l[i] = r[i] = static_cast<float>(0.25 * std::sin(2.0 * kPi * freq * static_cast<double>(i) / fs));
    const std::vector<float> in = l;
    run(m, l, r);
    const size_t from = n / 2, to = from + static_cast<size_t>(std::lround(fs / freq * std::floor(0.4 * freq))); // whole waves
    left = rmsDb(l, from, to) - rmsDb(in, from, to);
    right = rmsDb(r, from, to) - rmsDb(in, from, to);
}

int main() {
    const double fs = 48000.0;
    std::vector<float> l, r;

    { // Nothing added: what goes in comes out.
        ThickenerParams p;
        p.amount = 0.0;
        Unit m(fs, p);
        l.resize(4800);
        r.resize(4800);
        uint32_t s = 1;
        std::vector<float> inL(4800), inR(4800);
        for (size_t i = 0; i < 4800; ++i) {
            s = s * 1664525u + 1013904223u;
            inL[i] = l[i] = static_cast<float>(static_cast<int32_t>(s)) / 2147483648.0f;
            s = s * 1664525u + 1013904223u;
            inR[i] = r[i] = static_cast<float>(static_cast<int32_t>(s)) / 2147483648.0f;
        }
        run(m, l, r);
        double worst = 0;
        for (size_t i = 0; i < 4800; ++i) worst = std::max(worst, static_cast<double>(std::max(std::fabs(l[i] - inL[i]), std::fabs(r[i] - inR[i]))));
        check(worst == 0.0, "Amount 0%: output is the input", "largest difference %.2g", worst);
    }

    { // The routing, read off clicks with the room off.
        const ThickenerParams p = noRoom();
        const double a = 0.6, w = 0.62;
        Unit left(fs, p);
        click(l, r, 2000, true, false);
        run(left, l, r);
        check(std::fabs(l[0] - (1.0 + a)) < 1e-6 && peak(l, 1) < 1e-9, "left click: only turned up, nothing late", "%.4f at once, %.2g after", l[0], peak(l, 1));
        check(peak(r) == 0.0, "left click: nothing in the right channel", "%.2g", peak(r));

        Unit right(fs, p);
        click(l, r, 2000, false, true);
        run(right, l, r);
        check(std::fabs(r[0] - (1.0 + a * (1.0 - w))) < 1e-6, "right click: itself plus the part on time", "%.4f, expected %.4f", r[0], 1.0 + a * (1.0 - w));
        check(std::fabs(r[144] - a * w) < 1e-6, "right click: the late part 3 ms (144 samples) on", "%.4f, expected %.4f", r[144], a * w);
        check(peak(r, 1, 144) < 1e-9 && peak(r, 145) < 1e-9 && peak(l) == 0.0, "right click: nothing else, nothing on the left", "%.2g", std::max(peak(r, 1, 144), peak(r, 145)));

        ThickenerParams q = p;
        q.side = 0;
        Unit mirrored(fs, q);
        click(l, r, 2000, true, true);
        run(mirrored, l, r);
        check(std::fabs(l[144] - a * w) < 1e-6 && std::fabs(r[0] - (1.0 + a)) < 1e-6 && peak(r, 1) < 1e-9, "Side Left: the left channel is the late one", "%.4f late on the left", l[144]);
    }

    { // Steady tones follow the curves the UI draws.
        const ThickenerParams p = noRoom();
        double worst = 0, dip = 0, top = 0, gl, gr;
        for (double f : {60.0, 166.667, 250.0, 333.333, 500.0, 1000.0, 2400.0, 5000.0}) {
            toneGain(fs, p, f, gl, gr);
            worst = std::max(worst, std::max(std::fabs(gl - thickenerResponseDb(f, 0, p)), std::fabs(gr - thickenerResponseDb(f, 1, p))));
            if (f == 166.667) dip = gr;
            if (f == 333.333) top = gr;
        }
        check(worst < 0.05, "tones come out as the response curves say", "largest difference %.3f dB", worst);
        check(std::fabs(dip - linToDb(1.0 + 0.6 * (1.0 - 2.0 * 0.62))) < 0.05, "delayed channel at 167 Hz: the first dip", "%.2f dB", dip);
        check(std::fabs(top - linToDb(1.6)) < 0.05, "delayed channel at 333 Hz: as loud as the other", "%.2f dB", top);
    }

    for (double rate : {44100.0, 96000.0}) { // The delay is a time, not a number of samples.
        ThickenerParams p = noRoom();
        p.delayMix = 100.0;
        Unit m(rate, p);
        click(l, r, 4000, false, true);
        run(m, l, r);
        size_t at = 1;
        for (size_t i = 1; i < r.size(); ++i) if (std::fabs(r[i]) > std::fabs(r[at])) at = i;
        const double ms = 1000.0 * static_cast<double>(at) / rate;
        char name[64];
        std::snprintf(name, sizeof(name), "%.0f Hz: the late part arrives after 3 ms", rate);
        check(std::fabs(ms - 3.0) < 0.02, name, "%.3f ms", ms);
    }

    { // The room.
        ThickenerParams p;
        Unit m(fs, p);
        click(l, r, 48000, true, true);
        run(m, l, r);
        const double early = std::max(peak(l, 200, 2400), peak(r, 200, 2400)), late = std::max(peak(l, 24000), peak(r, 24000));
        check(early > 1e-4 && early < 0.2, "Room on: a tail after the click", "peak %.4f between 4 and 50 ms", early);
        check(late < 1e-5, "and it is gone half a second later (decay 0.2 s)", "peak %.2g", late);

        p.decay = 1.5;
        p.roomMix = 100.0;
        Unit ring(fs, p);
        click(l, r, 96000, true, true);
        run(ring, l, r);
        const double fall = rmsDb(l, 4800, 9600) - rmsDb(l, 4800 + 36000, 9600 + 36000); // over 0.75 s
        check(fall > 22.0 && fall < 40.0, "Decay 1.5 s: falls about 30 dB in 0.75 s", "%.1f dB", fall);

        // Left and right enter the room apart: a click on one side reaches both outputs, differently.
        ThickenerParams q;
        q.roomMix = 100.0;
        Unit one(fs, q);
        click(l, r, 9600, true, false);
        run(one, l, r);
        check(peak(r, 1) > 1e-3, "Room: a left click is heard on the right too", "peak %.4f", peak(r, 1));
    }

    { // Switching the room off and on never lets out an old tail.
        ThickenerParams p;
        p.roomMix = 100.0;
        p.decay = 1.5;
        Unit m(fs, p);
        l.assign(24000, 0.0f);
        r.assign(24000, 0.0f);
        for (size_t i = 0; i < l.size(); ++i) l[i] = r[i] = static_cast<float>(0.5 * std::sin(2.0 * kPi * 220.0 * static_cast<double>(i) / fs));
        run(m, l, r);
        p.room = false;
        m.setParams(p);
        l.assign(24000, 0.0f); // 0.5 s: long enough for the switch to fade, far too short for the tail to die
        r.assign(24000, 0.0f);
        run(m, l, r);
        p.room = true;
        m.setParams(p);
        l.assign(24000, 0.0f);
        r.assign(24000, 0.0f);
        run(m, l, r);
        check(peak(l) < 1e-9 && peak(r) < 1e-9, "Room off, then on: starts empty", "peak %.2g", std::max(peak(l), peak(r)));
    }

    { // Everything at once, as far as it goes: bounded, and at rest afterwards.
        ThickenerParams p;
        p.amount = 100.0;
        p.delayMs = 20.0;
        p.delayMix = 100.0;
        p.roomMix = 100.0;
        p.size = 100.0;
        p.decay = 1.5;
        p.damp = 0.0;
        p.output = 12.0;
        Unit m(fs, p);
        l.assign(96000, 0.0f);
        r.assign(96000, 0.0f);
        uint32_t s = 7;
        for (size_t i = 0; i < 48000; ++i) {
            s = s * 1664525u + 1013904223u;
            l[i] = r[i] = static_cast<float>(static_cast<int32_t>(s)) / 2147483648.0f;
        }
        run(m, l, r);
        check(peak(l) < 32.0 && peak(r) < 32.0, "all controls at their highest: bounded", "peak %.2f", std::max(peak(l), peak(r)));
        l.assign(480000, 0.0f);
        r.assign(480000, 0.0f);
        run(m, l, r);
        check(peak(l, 432000) < 1e-9 && peak(r, 432000) < 1e-9, "and silent 9 s after the input stopped", "peak %.2g", std::max(peak(l, 432000), peak(r, 432000)));
    }

    { // Both channels late, by different amounts: each has its dips where the other has none, and they are as loud as each other.
        ThickenerParams p = noRoom();
        p.side = kSideBoth;
        double worst = 0, gl, gr, sumL = 0, sumR = 0;
        for (double f : {60.0, 166.667, 269.7, 333.333, 500.0, 809.0, 1000.0, 2400.0}) {
            toneGain(fs, p, f, gl, gr);
            worst = std::max(worst, std::max(std::fabs(gl - thickenerResponseDb(f, 0, p)), std::fabs(gr - thickenerResponseDb(f, 1, p))));
        }
        check(worst < 0.05, "Side Both: tones come out as the response curves say", "largest difference %.3f dB", worst);
        toneGain(fs, p, 269.7, gl, gr); // the left channel's first dip: 500 / (0.618 * 3 ms)
        check(gl < gr - 4.0, "Side Both: the left dips at 270 Hz, the right does not", "left %.2f dB, right %.2f dB", gl, gr);
        for (double f = 100.0; f < 8000.0; f *= 1.0123) { // the level over all pitches, from the curves
            sumL += std::pow(10.0, 0.1 * thickenerResponseDb(f, 0, p));
            sumR += std::pow(10.0, 0.1 * thickenerResponseDb(f, 1, p));
        }
        check(std::fabs(10.0 * std::log10(sumL / sumR)) < 0.15, "Side Both: left and right are as loud as each other", "%.2f dB apart", 10.0 * std::log10(sumL / sumR));
    }

    { // Low Guard: the lows of the copy are not late.
        ThickenerParams p = noRoom();
        double gl, gr, open, guarded;
        toneGain(fs, p, 60.0, gl, gr);
        open = gl - gr;
        p.guard = 200.0;
        toneGain(fs, p, 60.0, gl, gr);
        guarded = gl - gr;
        check(open > 0.8 && std::fabs(guarded) < 0.25, "Low Guard 200 Hz: at 60 Hz left and right are level", "%.2f dB apart without, %.2f dB with", open, guarded);
        double worst = 0;
        for (double f : {40.0, 100.0, 166.667, 250.0, 500.0, 1000.0, 2400.0}) {
            toneGain(fs, p, f, gl, gr);
            worst = std::max(worst, std::fabs(gr - thickenerResponseDb(f, 1, p)));
        }
        check(worst < 0.1, "Low Guard: tones come out as the response curve says", "largest difference %.3f dB", worst);
        toneGain(fs, p, 2500.0, gl, gr); // a dip of the comb far above the guard
        check(std::fabs(gr - linToDb(1.0 + 0.6 * (1.0 - 2.0 * 0.62))) < 0.15, "Low Guard: the dips above it are as deep as without", "%.2f dB at 2.5 kHz", gr);
    }

    { // Tone: only what lies between Low Cut and High Cut is thickened.
        ThickenerParams p = noRoom();
        p.lowCut = 150.0;
        p.highCut = 3000.0;
        double gl, gr, worst = 0;
        for (double f : {40.0, 100.0, 150.0, 333.333, 1000.0, 3000.0, 6000.0}) {
            toneGain(fs, p, f, gl, gr);
            worst = std::max(worst, std::max(std::fabs(gl - thickenerResponseDb(f, 0, p)), std::fabs(gr - thickenerResponseDb(f, 1, p))));
        }
        check(worst < 0.25, "Low Cut 150 Hz, High Cut 3 kHz: as the response curves say", "largest difference %.3f dB", worst);
        toneGain(fs, p, 30.0, gl, gr);
        const double low = gl;
        toneGain(fs, p, 12000.0, gl, gr);
        check(std::fabs(low) < 0.5 && std::fabs(gl) < 0.5, "outside them the signal is left as it was", "%.2f dB at 30 Hz, %.2f dB at 12 kHz", low, gl);
    }

    { // Clean Hit, heard on the copy alone: a burst after silence starts turned down and comes back.
        ThickenerParams p = noRoom();
        p.listen = true;
        p.amount = 100.0;
        p.delayMix = 0.0;
        p.clean = 100.0;
        Unit m(fs, p);
        const size_t n = 48000, start = 24000;
        l.assign(n, 0.0f);
        r.assign(n, 0.0f);
        for (size_t i = start; i < n; ++i) l[i] = r[i] = static_cast<float>(0.5 * std::sin(2.0 * kPi * 1000.0 * static_cast<double>(i - start) / fs));
        const std::vector<float> in = l;
        run(m, l, r);
        const double hit = rmsDb(l, start + 48, start + 240) - rmsDb(in, start + 48, start + 240); // 1 to 5 ms after the start
        const double body = rmsDb(l, start + 14400, start + 19200) - rmsDb(in, start + 14400, start + 19200); // 300 ms on
        check(hit < -10.0, "Clean Hit 100%: the copy is down in the first 5 ms", "%.1f dB", hit);
        check(body > -0.3, "and back 300 ms later", "%.2f dB", body);
        p.clean = 0.0;
        Unit plain(fs, p);
        l = in;
        r = in;
        run(plain, l, r);
        check(std::fabs(rmsDb(l, start + 48, start + 240) - rmsDb(in, start + 48, start + 240)) < 0.01, "Clean Hit 0%: the copy is the signal", "%.3f dB", rmsDb(l, start + 48, start + 240) - rmsDb(in, start + 48, start + 240));
    }

    { // Crush and Drive, on the copy alone.
        ThickenerParams p = noRoom();
        p.listen = true;
        p.amount = 100.0;
        p.delayMix = 0.0;
        p.crush = 100.0;
        Unit m(fs, p);
        l.assign(48000, 0.0f);
        for (size_t i = 0; i < l.size(); ++i) l[i] = static_cast<float>(0.25 * std::sin(2.0 * kPi * 1000.0 * static_cast<double>(i) / fs));
        r = l;
        const std::vector<float> in = l;
        run(m, l, r);
        // -12 dBFS is 28 dB over the threshold of -40: 6:1 takes up to 23.3 dB off (the detector reads a sine a
        // little under its peak), and part of the 33.3 dB of headroom gained comes back as make-up.
        const double gain = rmsDb(l, 24000, 48000) - rmsDb(in, 24000, 48000), cut = m.meters.crush.load();
        const double makeup = -kCrushThresholdDb * (1.0 - 1.0 / kCrushRatio) * kCrushMakeup;
        check(cut > 22.0 && cut < 23.34, "Crush 100% on a -12 dBFS tone: gain reduction", "%.2f dB", cut);
        check(std::fabs(gain - (makeup - cut)) < 0.5, "and its level: the make-up less that", "%.2f dB, make-up %.2f dB", gain, makeup);

        p.crush = 0.0;
        p.drive = 100.0;
        Unit quiet(fs, p), loud(fs, p);
        std::vector<float> q(48000), big(48000);
        for (size_t i = 0; i < q.size(); ++i) {
            q[i] = static_cast<float>(0.001 * std::sin(2.0 * kPi * 1000.0 * static_cast<double>(i) / fs));
            big[i] = 1000.0f * q[i];
        }
        l = r = q;
        run(quiet, l, r);
        const double small = rmsDb(l, 24000, 48000) - rmsDb(q, 24000, 48000);
        check(std::fabs(small - 0.4 * 18.0) < 0.2, "Drive 100%: a quiet sound comes up 7.2 dB", "%.2f dB", small);
        l = r = big;
        run(loud, l, r);
        check(peak(l, 24000) < dbToLin(-0.6 * 18.0) + 1e-6 && peak(l, 24000) > 0.25, "and a full-scale one is held at -10.8 dBFS", "peak %.3f", peak(l, 24000));
    }

    { // Level Match, Listen and the correlation meter.
        ThickenerParams p;
        p.match = true;
        Unit m(fs, p);
        l.resize(48000 * 3);
        uint32_t s = 11;
        for (float& v : l) {
            s = s * 1664525u + 1013904223u;
            v = 0.25f * static_cast<float>(static_cast<int32_t>(s)) / 2147483648.0f;
        }
        r = l;
        const std::vector<float> in = l;
        run(m, l, r);
        const size_t a = 48000, b = l.size();
        const double both = 10.0 * std::log10((std::pow(10.0, 0.1 * rmsDb(l, a, b)) + std::pow(10.0, 0.1 * rmsDb(r, a, b))) / 2.0) - rmsDb(in, a, b);
        check(std::fabs(both) < 0.2, "Level Match: as loud coming out as going in", "%.2f dB", both);
        check(m.meters.match.load() < -2.5 && m.meters.match.load() > -4.0, "its meter shows the gain it applies", "%.2f dB", m.meters.match.load());
        check(m.meters.correlation.load() > 0.9 && m.meters.correlation.load() < 0.995, "correlation: a little under 1 at the default settings", "%.3f", m.meters.correlation.load());

        ThickenerParams q = noRoom();
        q.listen = true;
        Unit copy(fs, q);
        click(l, r, 2000, true, false);
        run(copy, l, r);
        check(std::fabs(l[0] - 0.6) < 1e-6 && peak(l, 1) < 1e-9, "Listen Copy: only the copy, without the signal", "%.4f", l[0]);

        ThickenerParams off;
        off.amount = 0.0;
        Unit plain(fs, off);
        l = in;
        r = in;
        for (float& v : r) v = -v;
        run(plain, l, r);
        check(plain.meters.correlation.load() < -0.999, "correlation: -1 when right is left upside down", "%.3f", plain.meters.correlation.load());
    }

    std::printf("%s\n", failures ? "FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
