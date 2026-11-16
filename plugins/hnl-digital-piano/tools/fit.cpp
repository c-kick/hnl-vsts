// Fits a piano voice to reference recordings: measures, for every recorded note,
// the noise of its attack and its partials (frequency, starting phase, and level
// over time) and writes them as a data file for dsp/. The recordings themselves are
// not part of this repository and no audio from them ends up in the plugin: only
// these measurements, from which the engine computes its own waves.
//
//   g++ -O2 -o fit tools/fit.cpp
//   ./fit <folder> <file pattern> <first> <last> <step> <name> > dsp/<Name>Data.hpp
//
// One 16-bit WAV per note, mono or stereo, named by the pattern with the MIDI number of the
// pitch that sounds, for example "piano_%d.wav" with 24 96 6 for a note every six
// semitones from MIDI 24 to 96. <name> becomes the namespace of the data.
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
static const double kSwitch = 0.35; // seconds: before this the partials are measured early, after it late

// The knock of the attack: noise below the note's own pitch, in octave bands, over the first moments.
static const double kKnockHz[] = {50, 100, 200, 400, 800};
static const double kKnockMs[] = {0, 30, 60, 120, 240}; // frame edges
static const int kKnockBands = 5, kKnockFrames = 4;

/** Level of x[a, a+n) in the octave around fc: two bandpass sections, as the engine uses to make the noise. */
static double bandLevel(const std::vector<double>& x, double fs, long a, long n, double fc) {
    const double w = 2 * PI * fc / fs, alpha = std::sin(w) / (2 * 1.414), a0 = 1 + alpha;
    const double b0 = alpha / a0, a1 = -2 * std::cos(w) / a0, a2 = (1 - alpha) / a0;
    double z[2][2] = {{0, 0}, {0, 0}}, sum = 0;
    const long pre = std::min(a, static_cast<long>(0.05 * fs));
    for (long i = a - pre; i < a + n && i < static_cast<long>(x.size()); ++i) {
        double v = x[i];
        for (auto& s : z) {
            const double y = b0 * v + s[0];
            s[0] = -a1 * y + s[1];
            s[1] = -b0 * v - a2 * y;
            v = y;
        }
        if (i >= a) sum += v * v;
    }
    return std::sqrt(sum / n);
}

/** x: the recording (the middle of left and right when it is stereo); sides: left and right apart, then. */
static bool load(const std::string& path, std::vector<double>& x, std::vector<double> sides[2], double& fs) {
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
            for (size_t i = 0; i + 2 * channels <= size && pos + 8 + i + 2 * channels <= b.size(); i += 2 * channels) {
                int16_t s[2] = {0, 0};
                std::memcpy(s, &b[pos + 8 + i], channels >= 2 ? 4 : 2);
                if (channels >= 2) {
                    sides[0].push_back(s[0] / 32768.0);
                    sides[1].push_back(s[1] / 32768.0);
                    x.push_back((s[0] + s[1]) / 65536.0);
                } else {
                    x.push_back(s[0] / 32768.0);
                }
            }
        }
        pos += 8 + size + (size & 1);
    }
    return !x.empty();
}

/** Hann-windowed component at f over x[a, a+n): amplitude, and phase as in A sin(2 pi f t + phase) with t from `origin`. */
static double component(const std::vector<double>& x, double fs, long a, long n, double f, long origin, double* phase = nullptr) {
    double re = 0, im = 0, sum = 0;
    for (long i = 0; i < n; ++i) {
        const long at = a + i;
        if (at < 0 || at >= static_cast<long>(x.size())) continue;
        const double w = 0.5 - 0.5 * std::cos(2 * PI * (i + 0.5) / n), t = (at - origin) / fs;
        re += w * x[at] * std::cos(2 * PI * f * t);
        im += w * x[at] * std::sin(2 * PI * f * t);
        sum += w;
    }
    if (phase) *phase = std::atan2(re, im); // x = A sin(wt + p): re = A/2 sin p, im = A/2 cos p
    return 2 * std::sqrt(re * re + im * im) / sum;
}

static double peakFreq(const std::vector<double>& x, double fs, long a, long n, double centre, double span, double* level) {
    double best = -1, at = centre;
    for (int pass = 0; pass < 3; ++pass) {
        const double c = at;
        for (int i = -12; i <= 12; ++i) {
            const double f = c + span * i / 12.0, v = component(x, fs, a, n, f, a);
            if (v > best) { best = v; at = f; }
        }
        span /= 8;
    }
    *level = best;
    return at;
}

int main(int argc, char** argv) {
    if (argc < 7) { std::fprintf(stderr, "usage: fit <folder> <file pattern> <first> <last> <step> <name>\n"); return 2; }
    const int first = std::atoi(argv[3]), lastNote = std::atoi(argv[4]), stride = std::atoi(argv[5]);
    std::printf("// Measurements of a piano, written by tools/fit.cpp: do not edit by hand.\n#pragma once\n\n#include \"PianoFit.hpp\"\n\n");
    std::printf("namespace pianodata {\nnamespace %s {\n\n", argv[6]);

    std::string notes, notesRight;
    int count = 0;
    bool stereo = false;
    for (int midi = first; midi <= lastNote; midi += stride) {
        char file[512];
        std::snprintf(file, sizeof file, argv[2], midi);
        const std::string path = std::string(argv[1]) + "/" + file;
        std::vector<double> x, sides[2];
        double fs = 44100;
        if (!load(path, x, sides, fs)) { std::fprintf(stderr, "skipping %s\n", path.c_str()); continue; }
        // A stereo recording is measured as one note: where it starts and where its partials lie are
        // found once, and their levels and phases then taken from the left and the right apart.
        const int outs = sides[0].empty() ? 1 : 2;
        const std::vector<double>* in[2] = {outs == 2 ? &sides[0] : &x, &sides[1]};
        stereo = stereo || outs == 2;
        const long N = static_cast<long>(x.size());
        const double nominal = 440.0 * std::pow(2.0, (midi - 69) / 12.0);
        double peak = 0;
        for (double v : x) peak = std::max(peak, std::fabs(v));
        // The start of the note: where it first reaches a tenth of the peak, less a few milliseconds
        // for the rise. A lower mark would be set off by the noise of a quiet recording.
        long on = 0;
        while (on < N && std::fabs(x[on]) < 0.1 * peak) ++on;
        on = std::max(0L, on - static_cast<long>(0.004 * fs));
        while (on < N && std::fabs(x[on]) < 0.01 * peak) ++on;
        {
            // In a quiet recording a stray click can be the loudest thing there is. So also listen for the
            // note's own pitch: where its first three partials come up is where the note is.
            const long hop = static_cast<long>(0.01 * fs), span = std::max(static_cast<long>(0.04 * fs), static_cast<long>(4 * fs / nominal));
            std::vector<double> heard;
            double most = 0;
            for (long a = 0; a + span < N; a += hop) {
                double sum = 0;
                for (int k = 1; k <= 3; ++k) sum += component(x, fs, a, span, k * nominal, a);
                heard.push_back(sum);
                most = std::max(most, sum);
            }
            size_t first = 0;
            while (first < heard.size() && heard[first] < 0.35 * most) ++first;
            const long byPitch = static_cast<long>(first) * hop + span / 2 - static_cast<long>(0.01 * fs);
            if (std::labs(byPitch - on) > static_cast<long>(0.08 * fs) + span / 2) {
                on = std::max(0L, byPitch);
                std::fprintf(stderr, "note %3d: start taken from its pitch\n", midi);
            }
            // The peak that levels are given against: that of the note itself.
            peak = 1e-9;
            for (int c = 0; c < outs; ++c)
                for (long i = on; i < std::min(N, on + static_cast<long>(0.5 * fs)); ++i) peak = std::max(peak, std::fabs((*in[c])[i]));
        }
        long end = N; // where the note has died away
        while (end > on + 1 && std::fabs(x[end - 1]) < 0.0005 * peak) --end;
        // With some silence before the note, the noise of the recording can be measured there, and
        // kept out of the result.
        const long quiet = on - static_cast<long>(0.02 * fs);
        double seconds = 0.2;
        bool carriedOn = false; // a partial outlasts the recording

        // The note's own frequency, from its fundamental once things have settled.
        double level;
        const long settle = on + std::min(static_cast<long>(0.3 * fs), (end - on) / 3);
        const long longWin = std::min(std::max(static_cast<long>(0.3 * fs), static_cast<long>(16 * fs / nominal)), end - settle);
        double f1 = peakFreq(x, fs, settle, longWin, nominal, 0.03 * nominal, &level);
        if (nominal < 200.0) {
            // A low note's fundamental is weak, and rumble can outweigh it. Its partials together say
            // where it is: take the pitch, and the stiffness of the string, whose partials carry the most.
            double best = -1;
            const int last = nominal < 70.0 ? 9 : 6;
            for (double stiff : {0.0, 1.0e-4, 2.0e-4, 3.5e-4, 5.5e-4}) {
                if (stiff > 0.0 && nominal >= 70.0) break;
                for (int i = -40; i <= 40; ++i) {
                    const double f = nominal * (1.0 + 0.001 * i);
                    double sum = 0;
                    for (int k = nominal < 70.0 ? 2 : 1; k <= last; ++k) sum += component(x, fs, settle, longWin, k * f * std::sqrt(1.0 + stiff * k * k), settle);
                    if (sum > best) { best = sum; f1 = f; }
                }
            }
        }
        const double period = fs / f1;

        const long earlyWin = std::min(std::max(static_cast<long>(0.12 * fs), static_cast<long>(10 * period)), end - on);
        const long lateAt = on + static_cast<long>(0.5 * fs), lateWin = std::max(static_cast<long>(0.3 * fs), static_cast<long>(16 * period));
        const long win = static_cast<long>(std::clamp(4.0 * period, 0.012 * fs, 0.2 * fs));

        std::string rows[2];
        int kept = 0;
        double lastRatio = 1.0, slope = 0.0;
        const double top = std::min(14500.0, 0.46 * fs);
        for (int k = 1; k * f1 * lastRatio < top && k <= 900; ++k) {
            // Follow the stretch of the partials upward from the previous one.
            const double guess = k * f1 * (lastRatio + slope);
            double le, ll = 0;
            double fe = peakFreq(x, fs, on + static_cast<long>(0.004 * fs), earlyWin, guess, std::min(0.3, 0.05 + 0.02 * k) * f1, &le);
            const long win0 = static_cast<long>(std::clamp(4.0 * period, 0.012 * fs, 0.2 * fs));
            const auto noiseAt = [&](const std::vector<double>& s) {
                return quiet > 2 * win0 ? std::max(component(s, fs, quiet - win0, win0, fe, on), component(s, fs, quiet - 2 * win0, win0, fe, on)) : 0.0;
            };
            const double floorAmp = noiseAt(x);
            const bool heard = le > peak * 1e-4 && le > 4.0 * floorAmp;
            if (!heard) fe = guess;
            double fl = fe;
            if (lateAt + lateWin < end) {
                const double cand = peakFreq(x, fs, lateAt, lateWin, fe, std::min(0.2, 0.04 + 0.015 * k) * f1, &ll);
                if (ll > peak * 2e-4) fl = cand;
            }
            // The fundamental of a very low note is weak and sits next to a much stronger second partial: it
            // cannot be placed to the cent by itself. The pitch found above from the partials together can.
            if (k == 1 && nominal < 70.0) fe = fl = f1;
            if (heard) {
                const double ratio = fe / (k * f1);
                slope = std::clamp(0.5 * slope + 0.5 * (ratio - lastRatio), 0.0, 0.004);
                lastRatio = ratio;
            } else {
                lastRatio += slope;
            }

            double level[2][kTimes], phase[2] = {0, 0}, top = -200, clear = -200;
            for (int c = 0; c < outs; ++c) {
                const std::vector<double>& s = *in[c];
                const double sideFloor = noiseAt(s), floorDb = sideFloor > 0.0 ? 20 * std::log10(sideFloor / peak + 1e-9) : -200.0;
                double sideTop = -200;
                int measured = -1; // the last time at which the recording still had the partial
                for (int j = 0; j < kTimes; ++j) {
                    const double t = kTimesMs[j] / 1000.0;
                    long a = on + static_cast<long>(t * fs) - win / 2;
                    if (a < on) a = on;
                    double v = -120;
                    if (a + win / 2 < end) {
                        double p;
                        const double amp = component(s, fs, a, win, t < kSwitch ? fe : fl, on, &p);
                        if (j == 0) phase[c] = p;
                        v = std::max(-120.0, 20 * std::log10(amp / peak + 1e-9));
                        measured = j;
                    }
                    level[c][j] = v;
                    sideTop = std::max(sideTop, v);
                }
                top = std::max(top, sideTop);
                clear = std::max(clear, sideTop - floorDb);
                // Once a partial has sunk into the noise it cannot be followed any further: let it carry on
                // fading at the rate it had, instead of taking the noise for the partial.
                int sunk = -1;
                for (int j = 2; j < kTimes && sunk < 0; ++j)
                    if (kTimesMs[j] >= 100 && level[c][j] < floorDb + 10 && level[c][j - 1] < floorDb + 13) sunk = j - 1;
                if (sunk >= 0) {
                    int from = sunk;
                    while (from > 0 && (kTimesMs[sunk] - kTimesMs[from] < 300 || from > sunk - 2)) --from;
                    const double span = (kTimesMs[sunk] - kTimesMs[from]) / 1000.0;
                    const double rate = std::min(-6.0, span > 0 ? (level[c][sunk] - level[c][from]) / span : -6.0); // dB per second
                    for (int j = sunk + 1; j < kTimes; ++j) level[c][j] = std::max(-120.0, level[c][sunk] + rate * (kTimesMs[j] - kTimesMs[sunk]) / 1000.0);
                }
                // The recording may stop while the partial still sounds (a key that was let go after a few
                // seconds): then it carries on fading at the rate of its last second and a half.
                if (sunk < 0 && measured >= 0 && measured < kTimes - 1 && kTimesMs[measured] >= 2000 && level[c][measured] > -95) {
                    int from = measured;
                    while (from > 0 && kTimesMs[measured] - kTimesMs[from] < 1500) --from;
                    const double rate = std::min(-3.0, (level[c][measured] - level[c][from]) * 1000.0 / (kTimesMs[measured] - kTimesMs[from]));
                    for (int j = measured + 1; j < kTimes; ++j) level[c][j] = std::max(-120.0, level[c][measured] + rate * (kTimesMs[j] - kTimesMs[measured]) / 1000.0);
                    carriedOn = true;
                }
            }
            if (top < -72 || clear < 12) continue; // never audible, or never clear of the noise
            for (int c = 0; c < outs; ++c) {
                char line[1024];
                int at = std::snprintf(line, sizeof line, "    {%.5ff, %.5ff, %.3ff, {", fe / f1, fl / f1, phase[c]);
                for (int j = 0; j < kTimes; ++j) {
                    at += std::snprintf(line + at, sizeof line - at, "%s%ld", j ? "," : "", std::lround(level[c][j] * 10));
                    if (level[c][j] > -95) seconds = std::max(seconds, kTimesMs[j] / 1000.0);
                }
                std::snprintf(line + at, sizeof line - at, "}},\n");
                rows[c] += line;
            }
            ++kept;
        }
        std::printf("static const Partial kNote%d[] = {\n%s};\n\n", midi, rows[0].c_str());
        if (outs == 2) std::printf("static const Partial kNote%dRight[] = {\n%s};\n\n", midi, rows[1].c_str());
        // The knock: only in bands well below the fundamental, where no partial can be mistaken for it.
        for (int c = 0; c < outs; ++c) {
            const std::vector<double>& s = *in[c];
            std::string knock;
            for (int b = 0; b < kKnockBands; ++b) {
                knock += b ? ", {" : "{";
                for (int j = 0; j < kKnockFrames; ++j) {
                    const long a = on + static_cast<long>(kKnockMs[j] * 0.001 * fs), n = static_cast<long>((kKnockMs[j + 1] - kKnockMs[j]) * 0.001 * fs);
                    double v = -120;
                    if (kKnockHz[b] <= 0.4 * f1 && a + n < end) v = std::max(-120.0, 20 * std::log10(bandLevel(s, fs, a, n, kKnockHz[b]) / peak + 1e-9));
                    // A knock is only a knock when it stands clear of the noise before the note.
                    if (quiet > n && v > -120 && v < 20 * std::log10(bandLevel(s, fs, quiet - n, n, kKnockHz[b]) / peak + 1e-9) + 8) v = -120;
                    knock += (j ? "," : "") + std::to_string(std::lround(v * 10));
                }
                knock += "}";
            }
            char line[512];
            std::snprintf(line, sizeof line, "    {%d, %.4ff, %.3ff, %d, kNote%d%s, {%s}},\n", midi, f1, (carriedOn ? seconds + 0.3 : std::min((end - on) / fs, seconds + 0.3)), kept, midi, c ? "Right" : "", knock.c_str());
            (c ? notesRight : notes) += line;
        }
        ++count;
        std::fprintf(stderr, "note %3d  starts %.3f s  %8.2f Hz (%+5.1f cents)  %5.2f s  %3d partials\n", midi, on / fs, f1, 1200 * std::log2(f1 / nominal), (carriedOn ? seconds + 0.3 : std::min((end - on) / fs, seconds + 0.3)), kept);
    }
    std::printf("static const Note kNotes[] = {\n%s};\n", notes.c_str());
    if (stereo) std::printf("// The same notes as heard on the right; kNotes is the left.\nstatic const Note kNotesRight[] = {\n%s};\n", notesRight.c_str());
    std::printf("constexpr int kNoteCount = %d;\n\n} // namespace %s\n} // namespace pianodata\n", count, argv[6]);
    return 0;
}
