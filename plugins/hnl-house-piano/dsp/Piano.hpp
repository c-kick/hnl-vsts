// A digital piano in the manner of the sample-playback workstations of the
// late 1980s: a hard, bright attack over a strong fundamental, a tone that
// loses its overtones within a second, and a tail that is almost a pure tone.
//
// The engine computes its own waves when it starts, one for every measured
// note (a note every six semitones), by adding up sine waves: each partial
// has a frequency, a starting phase and a level that changes over time, as
// measured from reference recordings of such a piano (see tools/fit.cpp and
// PianoData.hpp). No audio from any instrument is stored or played. As on
// the original machines, one wave serves several keys, played faster or
// slower, and a lowpass filter that opens with velocity does the rest.
//
// The measured piano is mono. Here every wave is made in stereo, as if the
// piano were recorded with two microphones: left and right carry the same
// partials at somewhat different levels and phases, more so higher up, so
// the bass stays in the middle and the upper part of the tone spreads out.
#pragma once

#include <atomic>
#include <cstdint>

#include "../../../common/dsp/Filters.hpp"
#include "../../../common/dsp/Reverb.hpp"
#include "PianoData.hpp"

#if defined(__SSE2__) || defined(_M_X64)
#include <immintrin.h>
#endif

namespace hnl {

constexpr double kWaveRate = 32000.0; // the rate the waves are stored at, as in the machines of the time
constexpr int kMaxVoices = 64;

struct PianoParams {
    double volume = 0.0;    // dB
    double tone = 0.0;      // -100 dark .. +100 open
    double velocity = 70.0; // percent: how much playing harder adds level and brightness
    double decay = 100.0;   // percent of the natural length of a held note
    double release = 600.0; // ms to fall 60 dB after the key is let go
    double detune = 0.0;    // cents; above 0 every note sounds twice, one up and one down
    double width = 100.0;   // percent: 0 mono, 100 the full stereo image; also spreads the keys and doubled notes
    double reverb = 15.0;   // percent
    double tune = 0.0;      // cents
    int octave = 0;         // -2 .. +2
};

/** Written by the audio thread, read by the UI thread. */
struct PianoMeters {
    std::atomic<float> outPeak[2] = {}; // linear, held until taken
    std::atomic<float> voices{0.0f};
};

// ------------------------------------------------------------------ waves

/** One computed note, from its first moment until it has died away. */
struct Wave {
    int root = 60;         // the key it was made for
    double freq = 261.63;  // its pitch when played at kWaveRate
    int length = 0;
    std::vector<float> data[2]; // left and right
};

class WaveSet {
public:
    static constexpr int kTopRoot = 96; // above this the measurement is too short to be of use

    WaveSet() {
        for (int i = 0; i < pianodata::kNoteCount && pianodata::kNotes[i].midi <= kTopRoot; ++i) {
            waves_.emplace_back();
            build(waves_.back(), pianodata::kNotes[i]);
        }
    }

    /** The wave made for the nearest measured key. */
    const Wave& forNote(int note) const {
        const Wave* best = &waves_[0];
        for (const Wave& w : waves_)
            if (std::abs(w.root - note) < std::abs(best->root - note)) best = &w;
        return *best;
    }

private:
    static double noise(uint32_t& state) {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        return static_cast<double>(static_cast<int32_t>(state)) * (1.0 / 2147483648.0);
    }

    static void build(Wave& w, const pianodata::Note& note) {
        using namespace pianodata;
        w.root = note.midi;
        w.freq = note.freq * (note.count > 0 ? note.partials[0].late : 1.0f); // what the ear takes as the pitch
        w.length = static_cast<int>(note.seconds * kWaveRate);
        for (auto& d : w.data) d.assign(static_cast<size_t>(w.length) + 4, 0.0f);
        uint32_t rng = 0x9e3779b9u + static_cast<uint32_t>(note.midi) * 2654435761u;
        const int switchAt = std::min(w.length, static_cast<int>(kSwitchSeconds * kWaveRate));
        const double silent = -1100; // tenths of a dB

        for (int k = 0; k < note.count; ++k) {
            const Partial& p = note.partials[k];
            const double early = p.early * note.freq, late = p.late * note.freq;
            if (early > 0.47 * kWaveRate || late > 0.47 * kWaveRate) continue;
            // The two microphones: the same below a few hundred hertz, their own level and phase above.
            // The fundamental keeps the same level on both sides, so no note leans by chance.
            const double apart = std::clamp((early - 120.0) / 300.0, 0.0, 1.0);
            const double tilt = k == 0 ? 0.0 : 3.0 * apart * noise(rng);
            const double offset = kPi * apart * (k == 0 ? (noise(rng) < 0.0 ? -0.15 : 0.15) : 0.25 * noise(rng));

            for (int ch = 0; ch < 2; ++ch) {
                const double sign = ch == 0 ? 1.0 : -1.0;
                double c = std::cos(p.phase + sign * offset), s = std::sin(p.phase + sign * offset);
                double step = 2.0 * kPi * early / kWaveRate, dc = std::cos(step), ds = std::sin(step);
                float* d = w.data[ch].data();
                int at = 0;
                for (int j = 0; j + 1 < kTimes && at < w.length; ++j) {
                    const int to = std::min(w.length, static_cast<int>(kTimeMs[j + 1] * 0.001 * kWaveRate));
                    const double from = p.level[j], until = p.level[j + 1];
                    const bool sounding = from > silent || until > silent;
                    // Between two measurements the level moves evenly in dB.
                    double amp = dbToLin(0.1 * from + sign * tilt);
                    const double grow = to > at ? dbToLin(0.1 * (until - from) / (to - at)) : 1.0;
                    if (!sounding && (switchAt < at || switchAt >= to)) { // nothing to add: only keep the phase
                        const double turn = step * (to - at), tc = std::cos(turn), ts = std::sin(turn), next = c * tc - s * ts;
                        s = s * tc + c * ts;
                        c = next;
                        at = to;
                        continue;
                    }
                    for (; at < to; ++at) {
                        if (at == switchAt) { // from here on the partial sits where it was measured late in the note
                            step = 2.0 * kPi * late / kWaveRate; dc = std::cos(step); ds = std::sin(step);
                        }
                        if (sounding) d[at] += static_cast<float>(amp * s);
                        const double next = c * dc - s * ds;
                        s = s * dc + c * ds;
                        c = next;
                        amp *= grow;
                    }
                }
            }
        }

        // The knock of the attack: noise below the note's own pitch, at the measured level in each octave.
        const int frames = kKnockFrames;
        for (int b = 0; b < kKnockBands; ++b) {
            if (note.knock[b][0] <= silent && note.knock[b][1] <= silent) continue;
            const double wc = 2.0 * kPi * kKnockHz[b] / kWaveRate, alpha = std::sin(wc) / (2.0 * 1.414), a0 = 1.0 + alpha;
            const BiquadCoeffs bp{alpha / a0, 0.0, -alpha / a0, -2.0 * std::cos(wc) / a0, (1.0 - alpha) / a0};
            // How loud this filter makes white noise, so the measured level can be set exactly.
            Biquad f[3][2];
            for (auto& pair : f) for (auto& q : pair) q.set(bp);
            double power = 0.0;
            const int warm = static_cast<int>(0.5 * kWaveRate);
            for (int i = 0; i < warm; ++i) {
                const double v = f[2][1].process(f[2][0].process(noise(rng)));
                power += v * v;
            }
            const double unit = 1.0 / std::sqrt(power / warm);
            const int last = std::min(w.length, static_cast<int>(0.36 * kWaveRate));
            for (int i = 0; i < last; ++i) {
                // The level at the middle of each frame, moving evenly in dB between them and falling away after the last.
                const double ms = i * 1000.0 / kWaveRate;
                double db = note.knock[b][0] * 0.1;
                for (int j = 0; j < frames; ++j) {
                    const double mid = 0.5 * (kKnockMs[j] + kKnockMs[j + 1]), next = j + 1 < frames ? 0.5 * (kKnockMs[j + 1] + kKnockMs[j + 2]) : mid + 120.0;
                    const double here = note.knock[b][j] * 0.1, there = j + 1 < frames ? note.knock[b][j + 1] * 0.1 : here - 40.0;
                    if (ms >= mid) db = here + (there - here) * std::min(1.5, (ms - mid) / (next - mid));
                }
                const double amp = dbToLin(db) * unit, shared = noise(rng);
                for (int ch = 0; ch < 2; ++ch)
                    w.data[ch][i] += static_cast<float>(amp * f[ch][1].process(f[ch][0].process(0.7071 * shared + 0.7071 * noise(rng))));
            }
        }

        // Level: the same loudness for every wave, measured over the first 200 ms of both sides.
        double sum = 0.0;
        const int window = std::max(1, std::min(w.length, static_cast<int>(0.2 * kWaveRate)));
        for (const auto& d : w.data)
            for (int i = 0; i < window; ++i) sum += 0.5 * static_cast<double>(d[i]) * d[i];
        const double gain = 0.25 / std::sqrt(sum / window + 1e-20);
        const int fade = std::min(w.length, static_cast<int>(0.03 * kWaveRate));
        for (auto& d : w.data) {
            for (float& v : d) v = static_cast<float>(v * gain);
            for (int i = 0; i < fade; ++i) d[w.length - 1 - i] *= static_cast<float>(i) / fade; // run out without a step
            for (int i = 0; i < 8 && i < w.length; ++i) d[i] *= static_cast<float>((i + 1) / 9.0);
            for (int i = w.length; i < w.length + 4; ++i) d[i] = 0.0f;
        }
    }

    std::vector<Wave> waves_;
};

// ------------------------------------------------------------------ engine

class Piano {
public:
    void prepare(double sampleRate) {
        fs_ = sampleRate;
        waves(); // computed here, not at the first note
        reverb_.prepare(fs_);
        reverb_.setDecay(1.9);
        reverb_.setDamping(5500.0);
        gainCoef_ = onePoleCoef(20.0, fs_);
        configure();
        reset();
    }

    void reset() {
        for (Voice& v : voices_) v.active = false;
        reverb_.reset();
        volume_ = volumeT_; rev_ = revT_;
        pedal_ = false;
        bend_ = 1.0;
        tail_ = 0;
    }

    void setParams(const PianoParams& p) {
        p_ = p;
        configure();
        for (Voice& v : voices_) if (v.active) shape(v);
    }
    const PianoParams& params() const { return p_; }

    PianoMeters meters;

    // ---------------------------------------------------------------- MIDI

    void noteOn(int key, int velocity) {
        if (velocity <= 0) { noteOff(key); return; }
        const int note = key + 12 * p_.octave;
        if (note < 0 || note > 127) return;
        for (Voice& v : voices_) // the same key again: the old note makes way
            if (v.active && v.key == key) v.held = v.sustained = false;
        const bool doubled = p_.detune > 0.05;
        for (int layer = 0; layer < (doubled ? 2 : 1); ++layer) {
            Voice& v = allocate();
            v.active = true;
            v.key = key;
            v.note = note;
            v.layer = doubled ? (layer == 0 ? -1 : 1) : 0;
            v.velocity = velocity / 127.0;
            v.held = true;
            v.sustained = false;
            v.wave = &waves().forNote(note);
            v.pos = 0.0;
            v.env = 1.0;
            v.lp1[0] = v.lp1[1] = v.lp2[0] = v.lp2[1] = 0.0;
            v.age = ++clock_;
            shape(v);
        }
    }

    void noteOff(int key) {
        for (Voice& v : voices_)
            if (v.active && v.key == key && v.held) {
                v.held = false;
                v.sustained = pedal_;
            }
    }

    void controller(int number, int value) {
        if (number == 64) {
            pedal_ = value >= 64;
            if (!pedal_) for (Voice& v : voices_) v.sustained = false;
        } else if (number == 120) {
            for (Voice& v : voices_) v.active = false;
        } else if (number == 123) {
            for (Voice& v : voices_) v.held = v.sustained = false;
        }
    }

    /** value: -1 .. +1, two semitones each way. */
    void pitchBend(double value) {
        bend_ = std::pow(2.0, 2.0 * std::clamp(value, -1.0, 1.0) / 12.0);
        for (Voice& v : voices_) if (v.active) shape(v);
    }

    int activeVoices() const {
        int n = 0;
        for (const Voice& v : voices_) n += v.active ? 1 : 0;
        return n;
    }

    // --------------------------------------------------------------- audio

    /** Writes `frames` samples of output. */
    void process(float* left, float* right, int frames) {
        ScopedNoDenormals guard;
        double pk[2] = {0, 0};
        int sounding = 0;

        for (int start = 0; start < frames; start += kChunk) {
            const int n = std::min(kChunk, frames - start);
            std::fill(mixL_, mixL_ + n, 0.0);
            std::fill(mixR_, mixR_ + n, 0.0);
            sounding = 0;
            for (Voice& v : voices_) {
                if (!v.active) continue;
                render(v, n);
                ++sounding;
            }
            if (sounding > 0) tail_ = static_cast<long>(4.0 * fs_); // let the reverb ring out, then rest
            const bool idle = sounding == 0 && tail_ <= 0;
            for (int i = 0; i < n; ++i) {
                volume_ = volumeT_ + gainCoef_ * (volume_ - volumeT_);
                rev_ = revT_ + gainCoef_ * (rev_ - revT_);
                double l = mixL_[i] * volume_, r = mixR_[i] * volume_;
                if (!idle) {
                    double wl, wr;
                    reverb_.process(l, r, wl, wr);
                    l += rev_ * wl;
                    r += rev_ * wr;
                }
                pk[0] = std::max(pk[0], std::fabs(l));
                pk[1] = std::max(pk[1], std::fabs(r));
                left[start + i] = static_cast<float>(l);
                right[start + i] = static_cast<float>(r);
            }
            if (tail_ > 0) {
                tail_ -= n;
                if (tail_ <= 0) reverb_.reset();
            }
        }
        meters.voices.store(static_cast<float>(sounding), std::memory_order_relaxed);
        for (int c = 0; c < 2; ++c)
            if (static_cast<float>(pk[c]) > meters.outPeak[c].load(std::memory_order_relaxed))
                meters.outPeak[c].store(static_cast<float>(pk[c]), std::memory_order_relaxed);
    }

private:
    struct ScopedNoDenormals {
#if defined(__SSE2__) || defined(_M_X64)
        unsigned saved = _mm_getcsr();
        ScopedNoDenormals() { _mm_setcsr(saved | 0x8040); } // FTZ + DAZ
        ~ScopedNoDenormals() { _mm_setcsr(saved); }
#endif
    };

    struct Voice {
        bool active = false, held = false, sustained = false;
        int key = 0, note = 60, layer = 0;
        uint64_t age = 0;
        const Wave* wave = nullptr;
        double velocity = 1.0, pos = 0.0, step = 1.0;
        double env = 0.0, decay = 1.0, release = 1.0, level = 1.0;
        double mix[2][2] = {{1, 0}, {0, 1}}; // output side from wave side
        double lpCoef = 1.0, lp1[2] = {0, 0}, lp2[2] = {0, 0};
    };

    static const WaveSet& waves() {
        static const WaveSet set; // computed once, shared by every instance
        return set;
    }

    /** A free voice, or else the quietest one. */
    Voice& allocate() {
        Voice* quietest = &voices_[0];
        for (Voice& v : voices_) {
            if (!v.active) return v;
            const double a = v.env * v.level * (v.held ? 4.0 : 1.0), b = quietest->env * quietest->level * (quietest->held ? 4.0 : 1.0);
            if (a < b) quietest = &v;
        }
        return *quietest;
    }

    /** Everything about a voice that follows from the controls. */
    void shape(Voice& v) {
        const double sens = std::clamp(p_.velocity, 0.0, 100.0) / 100.0, width = std::clamp(p_.width, 0.0, 100.0) / 100.0;
        const double cents = p_.tune + v.layer * p_.detune;
        const double freq = 440.0 * std::pow(2.0, (v.note - 69) / 12.0 + cents / 1200.0) * bend_;
        v.step = kWaveRate / fs_ * freq / v.wave->freq;

        v.level = kVoiceLevel * std::pow(v.velocity, 2.0 * sens) * (v.layer != 0 ? 0.7071 : 1.0);
        // Wide open at full velocity; playing softer closes the filter, by up to five octaves for the softest touch.
        // Higher keys keep it further open.
        const double cutoff = 16000.0 * std::pow(2.0, p_.tone / 100.0 * 4.0 + (v.velocity - 1.0) * 5.0 * sens + (v.note - 60) / 24.0);
        v.lpCoef = 1.0 - std::exp(-2.0 * kPi * std::clamp(cutoff, 150.0, 0.45 * fs_) / fs_);

        // The waves carry the natural decay. Below 100% a held note is faded on top of that:
        // at 50% it is gone in about half the time.
        const double natural = 2.0 * std::pow(2.0, -(v.note - 60) / 24.0), shorter = 100.0 / std::clamp(p_.decay, 5.0, 100.0) - 1.0;
        v.decay = std::exp(-shorter / (natural * fs_));
        v.release = std::exp(-6.908 / (0.001 * std::clamp(p_.release, 5.0, 10000.0) * fs_)); // 60 dB down in that time

        // Width narrows the recording towards mono; the keys lie low to high from left to right,
        // as the player hears them, and a doubled note has its two halves apart.
        const double pan = std::clamp(width * (0.35 * std::clamp((v.note - 64) / 40.0, -1.0, 1.0) + 0.5 * v.layer), -1.0, 1.0);
        const double same = 0.5 * (1.0 + width), cross = 0.5 * (1.0 - width);
        const double gl = std::sqrt(1.0 - pan), gr = std::sqrt(1.0 + pan);
        v.mix[0][0] = gl * same; v.mix[0][1] = gl * cross;
        v.mix[1][0] = gr * cross; v.mix[1][1] = gr * same;
    }

    void render(Voice& v, int n) {
        const Wave& w = *v.wave;
        const float* d[2] = {w.data[0].data(), w.data[1].data()};
        const double end = w.length - 1;
        for (int i = 0; i < n; ++i) {
            if (v.pos >= end) { v.active = false; return; } // the note has run its course
            const int at = static_cast<int>(v.pos);
            const double f = v.pos - at;
            const int im = at > 0 ? at - 1 : 0;
            double s[2];
            for (int ch = 0; ch < 2; ++ch) {
                const double ym = d[ch][im], y0 = d[ch][at], y1 = d[ch][at + 1], y2 = d[ch][at + 2];
                const double c1 = 0.5 * (y1 - ym), c2 = ym - 2.5 * y0 + 2.0 * y1 - 0.5 * y2, c3 = 0.5 * (y2 - ym) + 1.5 * (y0 - y1);
                v.lp1[ch] += v.lpCoef * (((c3 * f + c2) * f + c1) * f + y0 - v.lp1[ch]);
                v.lp2[ch] += v.lpCoef * (v.lp1[ch] - v.lp2[ch]);
                s[ch] = v.lp2[ch] * v.env * v.level;
            }
            mixL_[i] += s[0] * v.mix[0][0] + s[1] * v.mix[0][1];
            mixR_[i] += s[0] * v.mix[1][0] + s[1] * v.mix[1][1];

            v.env *= (v.held || v.sustained) ? v.decay : v.release;
            v.pos += v.step;
        }
        if (v.env < 1e-5) v.active = false;
    }

    void configure() {
        volumeT_ = dbToLin(p_.volume);
        const double r = std::clamp(p_.reverb, 0.0, 100.0) / 100.0;
        revT_ = 1.5 * r * r;
    }

    static constexpr int kChunk = 64;
    static constexpr double kVoiceLevel = 0.16; // a hard eight-note chord stays just under full scale

    PianoParams p_;
    double fs_ = 48000.0, gainCoef_ = 0.0;
    double volume_ = 1.0, volumeT_ = 1.0, rev_ = 0.0, revT_ = 0.0, bend_ = 1.0;
    bool pedal_ = false;
    long tail_ = 0;
    uint64_t clock_ = 0;
    Voice voices_[kMaxVoices];
    double mixL_[kChunk] = {}, mixR_[kChunk] = {};
    Reverb reverb_;
};

} // namespace hnl
