// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// Fits a piano voice when the only reference is a few recorded chords.
//
// Each chord is taken apart into the partials of its notes. A partial that two
// notes share is divided between them. All partials of all notes are then pooled
// into one picture of the instrument: how loud a partial at a given frequency is,
// and how that changes over time. From that picture a note is made up for every
// few keys, written in the same form as tools/fit.cpp writes.
//
// This is much weaker than fitting single notes: the picture is only truly known
// in the register of the chords, the attack is blurred (telling the notes of a
// chord apart needs long measuring windows), and the rest of the keyboard is
// extrapolated. No audio from the recordings ends up in the plugin.
//
//   g++ -O2 -o fit_chords tools/fit_chords.cpp
//   ./fit_chords <name> <held seconds> <wav>:<midi>,<midi>,... [...] > dsp/<Name>Data.hpp
//
// <held seconds>: how long the chords are held before they are let go; only that
// part is measured, and the decay after it is continued at the rate it had.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static const double PI = 3.14159265358979323846;
static const double kTimesMs[] = {0, 6, 12, 20, 30, 45, 60, 80, 100, 130, 160, 200, 250, 300, 400, 500, 650, 800, 1000,
                                  1300, 1600, 2000, 2500, 3000, 4000, 5000, 6000, 8000, 10000};
static const int kTimes = sizeof(kTimesMs) / sizeof(kTimesMs[0]);
static const int kTargets[] = {24, 30, 36, 42, 48, 54, 57, 60, 63, 66, 69, 72, 78, 84, 90, 96};

static bool load(const std::string& path, std::vector<double>& x, double& fs) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::vector<uint8_t> b;
    uint8_t buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) b.insert(b.end(), buf, buf + n);
    std::fclose(f);
    int channels = 1;
    for (size_t pos = 12; pos + 8 <= b.size();) {
        uint32_t size;
        std::memcpy(&size, &b[pos + 4], 4);
        if (!std::memcmp(&b[pos], "fmt ", 4)) {
            uint16_t c; uint32_t rate;
            std::memcpy(&c, &b[pos + 10], 2);
            std::memcpy(&rate, &b[pos + 12], 4);
            channels = c;
            fs = rate;
        } else if (!std::memcmp(&b[pos], "data", 4)) {
            for (size_t i = 0; i + 2 * channels <= size && pos + 8 + i + 2 <= b.size(); i += 2 * channels) {
                int16_t s;
                std::memcpy(&s, &b[pos + 8 + i], 2);
                x.push_back(s / 32768.0);
            }
        }
        pos += 8 + size + (size & 1);
    }
    return !x.empty();
}

static double component(const std::vector<double>& x, double fs, long a, long n, double f) {
    double re = 0, im = 0, sum = 0;
    for (long i = 0; i < n; ++i) {
        const long at = a + i;
        if (at < 0 || at >= static_cast<long>(x.size())) continue;
        const double w = 0.5 - 0.5 * std::cos(2 * PI * (i + 0.5) / n), t = i / fs;
        re += w * x[at] * std::cos(2 * PI * f * t);
        im += w * x[at] * std::sin(2 * PI * f * t);
        sum += w;
    }
    return 2 * std::sqrt(re * re + im * im) / sum;
}

static double peakFreq(const std::vector<double>& x, double fs, long a, long n, double centre, double span, double* level) {
    double best = -1, at = centre;
    for (int pass = 0; pass < 3; ++pass) {
        const double c = at;
        for (int i = -12; i <= 12; ++i) {
            const double f = c + span * i / 12.0, v = component(x, fs, a, n, f);
            if (v > best) { best = v; at = f; }
        }
        span /= 8;
    }
    *level = best;
    return at;
}

struct Point {
    double freq;         // Hz
    double stretch;      // how far above the exact harmonic, as a ratio
    double db[kTimes];   // level over time
};

int main(int argc, char** argv) {
    if (argc < 4) { std::fprintf(stderr, "usage: fit_chords <name> <held seconds> <wav>:<midi>,<midi>,... [...]\n"); return 2; }
    const double held = std::atof(argv[2]);
    std::vector<Point> pool;

    for (int a = 3; a < argc; ++a) {
        const std::string arg = argv[a], path = arg.substr(0, arg.rfind(':'));
        std::vector<int> notes;
        for (const char* s = arg.c_str() + arg.rfind(':') + 1; *s;) { notes.push_back(std::atoi(s)); while (*s && *s != ',') ++s; if (*s) ++s; }
        std::vector<double> x;
        double fs = 44100;
        if (!load(path, x, fs)) { std::fprintf(stderr, "cannot read %s\n", path.c_str()); return 2; }
        double peak = 0;
        for (double v : x) peak = std::max(peak, std::fabs(v));
        long on = 0;
        while (on < static_cast<long>(x.size()) && std::fabs(x[on]) < 0.01 * peak) ++on;
        const long win = static_cast<long>(0.1 * fs); // long enough to tell the notes of a chord apart

        // Where every partial of every note lies.
        struct Found { int note; double freq, stretch; };
        std::vector<Found> found;
        for (int midi : notes) {
            const double nominal = 440.0 * std::pow(2.0, (midi - 69) / 12.0);
            double level;
            const double f1 = peakFreq(x, fs, on + static_cast<long>(0.1 * fs), static_cast<long>(0.4 * fs), nominal, 0.02 * nominal, &level);
            double ratio = 1.0, slope = 0.0;
            for (int k = 1; k * f1 * ratio < 14000.0 && k <= 60; ++k) {
                double fk = peakFreq(x, fs, on + static_cast<long>(0.01 * fs), static_cast<long>(0.25 * fs), k * f1 * (ratio + slope), 0.012 * k * f1 + 3.0, &level);
                if (level > peak * 2e-4) {
                    const double r = fk / (k * f1);
                    slope = std::clamp(0.5 * slope + 0.5 * (r - ratio), 0.0, 0.004);
                    ratio = r;
                } else {
                    ratio += slope;
                    fk = k * f1 * ratio;
                }
                found.push_back({midi, fk, ratio});
            }
            std::fprintf(stderr, "%s note %d: %.2f Hz\n", path.c_str(), midi, f1);
        }

        for (const Found& p : found) {
            int sharers = 0; // notes with a partial too close to tell apart, this one included
            for (const Found& q : found)
                if (std::fabs(q.freq - p.freq) < 14.0 && (q.note != p.note || &q == &p)) ++sharers;
            Point point;
            point.freq = p.freq;
            point.stretch = p.stretch;
            double top = -200;
            int lastHeld = 0;
            for (int j = 0; j < kTimes; ++j) {
                const double t = kTimesMs[j] / 1000.0;
                if (t > held) { point.db[j] = 0; continue; }
                lastHeld = j;
                long at = on + static_cast<long>(t * fs) - win / 2;
                if (at < on) at = on;
                const double amp = component(x, fs, at, win, p.freq) / std::sqrt(static_cast<double>(sharers));
                point.db[j] = std::max(-120.0, 20 * std::log10(amp / peak + 1e-9));
                top = std::max(top, point.db[j]);
            }
            if (top < -75) continue;
            // After the chord is let go: carry on at the rate of the last second that was held.
            int from = lastHeld;
            while (from > 0 && kTimesMs[lastHeld] - kTimesMs[from] < 1000.0) --from;
            const double rate = std::min(-1.5, (point.db[lastHeld] - point.db[from]) / ((kTimesMs[lastHeld] - kTimesMs[from]) / 1000.0)); // dB per second
            for (int j = lastHeld + 1; j < kTimes; ++j) point.db[j] = std::max(-120.0, point.db[lastHeld] + rate * (kTimesMs[j] - kTimesMs[lastHeld]) / 1000.0);
            pool.push_back(point);
        }
    }
    std::fprintf(stderr, "%zu partials pooled\n", pool.size());

    // How the stretch of the partials grows with frequency, from the pooled notes: ratio = 1 + c f^2.
    double num = 0, den = 0;
    for (const Point& p : pool) { num += (p.stretch - 1.0) * p.freq * p.freq; den += p.freq * p.freq * p.freq * p.freq; }
    const double stretchPerHz2 = std::max(0.0, num / std::max(den, 1e-9));

    std::printf("// A piano made up from measurements of a few recorded chords, written by tools/fit_chords.cpp:\n// do not edit by hand.\n#pragma once\n\n#include \"PianoFit.hpp\"\n\n");
    std::printf("namespace pianodata {\nnamespace %s {\n\n", argv[1]);
    std::string notes;
    int count = 0;
    uint32_t rng = 0x1234567u;
    for (int midi : kTargets) {
        const double f0 = 440.0 * std::pow(2.0, (midi - 69) / 12.0);
        // Lower strings are less stiff for their pitch: keep the stretch of a partial by its number, eased towards the bass.
        const double stiff = stretchPerHz2 * 330.0 * 330.0 * std::pow(2.0, (midi - 64) / 9.0);
        std::string rows;
        int kept = 0;
        double longest = 0.2;
        for (int k = 1; k <= 220; ++k) {
            const double ratio = std::sqrt((1.0 + 2.0 * stiff * k * k) / (1.0 + 2.0 * stiff)), f = k * f0 * ratio;
            if (f > 14000.0) break;
            // The pooled picture at this frequency: partials nearby count most. Below the lowest measured one, the lowest few stand in.
            double weight = 0, db[kTimes] = {};
            std::vector<std::pair<double, const Point*>> near;
            for (const Point& p : pool) near.push_back({std::fabs(std::log2(p.freq / f)), &p});
            std::sort(near.begin(), near.end(), [](const auto& l, const auto& r) { return l.first < r.first; });
            const double reach = std::max(0.12, near[std::min<size_t>(3, near.size() - 1)].first);
            for (const auto& n : near) {
                const double w = std::exp(-0.5 * (n.first / reach) * (n.first / reach));
                if (w < 1e-3) break;
                for (int j = 0; j < kTimes; ++j) db[j] += w * std::pow(10.0, 0.1 * n.second->db[j]); // by power, so the strong ones count
                weight += w;
            }
            double top = -200;
            for (int j = 0; j < kTimes; ++j) { db[j] = 10.0 * std::log10(db[j] / weight + 1e-13); top = std::max(top, db[j]); }
            // Low notes ring longer: stretch time for keys below the chords, shorten it above.
            const double pace = std::pow(2.0, (midi - 64) / 30.0);
            double paced[kTimes];
            for (int j = 0; j < kTimes; ++j) {
                const double t = kTimesMs[j] * pace;
                int i = 0;
                while (i + 2 < kTimes && kTimesMs[i + 1] < t) ++i;
                const double u = std::clamp((t - kTimesMs[i]) / (kTimesMs[i + 1] - kTimesMs[i]), 0.0, 3.0);
                paced[j] = std::max(-120.0, db[i] + u * (db[i + 1] - db[i]));
                if (paced[j] > -95) longest = std::max(longest, kTimesMs[j] / 1000.0);
            }
            if (top < -72) continue;
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            const double phase = (static_cast<int32_t>(rng) / 2147483648.0) * PI;
            char line[1024];
            int at = std::snprintf(line, sizeof line, "    {%.5ff, %.5ff, %.3ff, {", k * ratio, k * ratio, phase);
            for (int j = 0; j < kTimes; ++j) at += std::snprintf(line + at, sizeof line - at, "%s%ld", j ? "," : "", std::lround(paced[j] * 10));
            std::snprintf(line + at, sizeof line - at, "}},\n");
            rows += line;
            ++kept;
        }
        std::printf("static const Partial kNote%d[] = {\n%s};\n\n", midi, rows.c_str());
        char line[512];
        std::snprintf(line, sizeof line, "    {%d, %.4ff, %.3ff, %d, kNote%d, {{-1200,-1200,-1200,-1200}, {-1200,-1200,-1200,-1200}, {-1200,-1200,-1200,-1200}, {-1200,-1200,-1200,-1200}, {-1200,-1200,-1200,-1200}}},\n",
                      midi, f0, std::min(10.0, longest + 0.3), kept, midi);
        notes += line;
        ++count;
        std::fprintf(stderr, "made note %3d: %3d partials, %.1f s\n", midi, kept, longest);
    }
    std::printf("static const Note kNotes[] = {\n%s};\nconstexpr int kNoteCount = %d;\n\n} // namespace %s\n} // namespace pianodata\n", notes.c_str(), count, argv[1]);
    return 0;
}
