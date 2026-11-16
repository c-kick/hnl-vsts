// A grand piano worked out from how a piano makes its sound, not from recordings.
//
// For a key and a velocity, this writes down every partial of the note in the same
// form the fitted voices use (see PianoFit.hpp), and the engine computes its waves
// from that. What goes into it:
//
// - Stiff strings. The partials lie above the exact harmonics, a little in the
//   middle of the keyboard, more in the bass (thick wound strings) and much more
//   at the top (short strings).
// - The hammer. It strikes about an eighth of the way along the string, which
//   weakens every eighth partial or so. Its felt is soft when it moves slowly and
//   hard when it moves fast: a harder blow puts far more energy into the upper
//   partials. That is most of the difference between soft and loud on a piano.
// - Two or three strings per note. They are never tuned exactly alike. Together
//   they first give their energy to the soundboard quickly (the loud, fast-fading
//   start of the note) and then settle into moving against each other, which the
//   soundboard hardly hears: a quiet, long-lasting, slowly beating aftersound.
//   Each partial is therefore written twice: once loud and fading fast, and once
//   softer, a fraction of a hertz off, fading slowly.
// - Higher partials die sooner than low ones, and high notes sooner than low notes.
// - The soundboard. It does not carry the lowest bass fundamentals well, has a
//   broad presence in the low mids and around 2 to 3 kHz, and a fine pattern of
//   stronger and weaker spots that gives every key its own colour.
// - The knock of the hammer and the key, heard as a short thump under the note.
#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

#include "PianoFit.hpp"

namespace hnl {

struct GrandTables {
    std::vector<std::vector<pianodata::Partial>> partials; // one list per note
    std::vector<pianodata::Note> notes;
};

namespace grand {

constexpr double kPiG = 3.14159265358979323846;

inline double noise(uint32_t& state) {
    state ^= state << 13; state ^= state >> 17; state ^= state << 5;
    return static_cast<double>(static_cast<int32_t>(state)) * (1.0 / 2147483648.0);
}

/** Stiffness of the strings of a key: least around the top of the bass strings. */
inline double stiffness(int note) {
    const double least = 6.0e-5;
    if (note >= 42) return least * std::pow(1.2e-2 / least, (note - 42) / 66.0);
    return least * std::pow(4.0e-4 / least, (42 - note) / 21.0);
}

/** How well the soundboard carries a frequency, in dB. */
inline double soundboard(double f) {
    const double low = (f / 85.0) * (f / 85.0);
    double db = 10.0 * std::log10(low / (1.0 + low) + 1e-9); // the lowest fundamentals are barely heard
    const double x = std::log2(f / 100.0);
    db += 2.5 * std::exp(-(x - 1.6) * (x - 1.6) / 1.2);  // warmth around 300 Hz
    db += 3.0 * std::exp(-(x - 4.7) * (x - 4.7) / 0.9);  // presence around 2.6 kHz
    db -= 2.0 * std::exp(-(x - 3.2) * (x - 3.2) / 0.5);  // a slight hollow near 900 Hz
    // The fine pattern of its resonances: the same for every key, so it colours each differently.
    db += 1.6 * std::sin(7.3 * x + 0.9) + 1.1 * std::sin(17.1 * x + 2.2) + 0.8 * std::sin(31.7 * x + 4.0);
    return db;
}

/** Level in dB over time for one component: starts at `start`, fades at `rate` dB per second. */
inline void envelope(pianodata::Partial& p, double start, double rate, double rise, double strike) {
    for (int j = 0; j < pianodata::kTimes; ++j) {
        const double t = pianodata::kTimeMs[j] * 0.001;
        double db = start - rate * t;
        db += strike * std::exp(-t / 0.025);               // the first moments are brighter
        if (t < rise) db -= 14.0 * (1.0 - t / rise);       // the hammer is still on the string
        if (t > 7.0) db -= 10.0 * (t - 7.0);               // nobody holds a note much longer: let it go
        p.level[j] = static_cast<int16_t>(std::lround(std::fmax(-120.0, db) * 10.0));
    }
}

} // namespace grand

/** velocity: 0..1. One note every four keys, from the lowest A to the top C. */
inline GrandTables makeGrand(double velocity) {
    using namespace grand;
    using pianodata::Partial;
    GrandTables out;
    out.partials.reserve(32);

    for (int note = 21; note <= 109; note += 4) {
        const double f0 = 440.0 * std::pow(2.0, (note - 69) / 12.0), stiff = stiffness(note);
        uint32_t rng = 0x51ed270bu + static_cast<uint32_t>(note) * 2246822519u;
        std::vector<Partial> list;

        // Hammer: where it strikes, and how hard its felt is at this speed.
        const double strikePoint = 0.122 - 0.03 * std::fmin(1.0, std::fmax(0.0, (note - 60) / 48.0));
        const double hardness = (1700.0 + 13.0 * f0) * (0.08 + 0.92 * velocity * velocity);
        // Strings: one in the low bass, two above it, three from the tenor up.
        const int strings = note < 32 ? 1 : (note < 42 ? 2 : 3);
        const double fade = 6.5 * std::pow(2.0, (note - 60) / 16.0); // dB per second, the fundamental's first fade
        const double rise = note < 45 ? 0.004 : 0.002;

        for (int k = 1; k <= 96; ++k) {
            const double ratio = k * std::sqrt((1.0 + stiff * k * k) / (1.0 + stiff)), f = ratio * f0;
            if (f > 13500.0) break;
            const double comb = std::fmax(0.08, std::fabs(std::sin(k * kPiG * strikePoint)));
            double db = 20.0 * std::log10(comb / k) - 24.0 * std::log10(1.0 + (f / hardness) * (f / hardness)) + soundboard(f);
            db += 1.5 * noise(rng); // no two strings are quite alike
            if (db < -78.0 && k > 4) continue;

            const double rate = fade * (1.0 + 0.25 * std::pow(k - 1.0, 0.9));
            const double strike = k > 2 ? 3.5 : 0.0;
            Partial loud;
            loud.early = loud.late = static_cast<float>(ratio);
            loud.phase = static_cast<float>(0.3 * noise(rng));
            envelope(loud, db - 8.0, rate, rise, strike);
            list.push_back(loud);

            // The aftersound: the strings moving against each other. Softer, slightly off pitch, long-lived.
            if (strings > 1 && k <= 14) {
                const double off = (0.12 + 0.3 * std::fabs(noise(rng))) * (1.0 + 0.12 * k) * (noise(rng) < 0.0 ? -1.0 : 1.0); // Hz
                Partial after;
                after.early = after.late = static_cast<float>(ratio + off / f0);
                after.phase = static_cast<float>(kPiG * noise(rng));
                envelope(after, db - 8.0 - (strings == 3 ? 13.0 : 18.0), rate * 0.24, rise, 0.0);
                list.push_back(after);
            }
        }

        // How long it sounds: until the last component is inaudible.
        double seconds = 0.5;
        for (const Partial& p : list)
            for (int j = 0; j < pianodata::kTimes; ++j)
                if (p.level[j] > -900) seconds = std::fmax(seconds, pianodata::kTimeMs[j] * 0.001);

        pianodata::Note n;
        n.midi = note;
        n.freq = static_cast<float>(f0);
        n.seconds = static_cast<float>(std::fmin(10.0, seconds + 0.2));
        // The knock: louder against the note in the treble, where the note itself is thin; harder blows knock more.
        const double knock = -40.0 + 9.0 * std::fmin(1.0, std::fmax(0.0, (note - 48) / 48.0)) + 6.0 * (velocity - 0.5);
        for (int b = 0; b < pianodata::kKnockBands; ++b) {
            const bool under = pianodata::kKnockHz[b] <= 0.5 * f0; // only below the note itself
            const double shape = -1.5 * std::fabs(b - 2.0);       // most of it between 100 and 400 Hz
            static const double kFall[pianodata::kKnockFrames] = {0.0, -6.0, -15.0, -26.0};
            for (int j = 0; j < pianodata::kKnockFrames; ++j)
                n.knock[b][j] = static_cast<int16_t>(under ? std::lround((knock + shape + kFall[j]) * 10.0) : -1200);
        }
        out.partials.push_back(std::move(list));
        n.count = static_cast<int>(out.partials.back().size());
        n.partials = out.partials.back().data();
        out.notes.push_back(n);
    }
    return out;
}

} // namespace hnl
