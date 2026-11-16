// A digital piano in the manner of the sample-playback workstations of the
// late 1980s: bright, a little thin, with a sustain that stands still.
//
// Those instruments played short recordings of a piano. The start of each
// note was the real attack; after a fraction of a second the recording ran
// into a short loop, so the tone froze while an envelope faded it out. One
// recording served several keys, played faster or slower. A lowpass filter
// that opened with velocity did the rest.
//
// This engine works the same way, but makes its own recordings: when it
// starts, it computes one wave per three keys from a model of a struck
// string (stiff-string partials, hammer position, a bright body), each with
// an attack and a loop. No samples of any instrument are used.
//
// Every wave is stereo, as if the piano were recorded with two microphones:
// left and right carry the same partials at somewhat different levels and
// phases, more so higher up, so the bass stays in the middle and the upper
// part of the tone spreads out.
#pragma once

#include <atomic>
#include <cstdint>

#include "../../../common/dsp/Filters.hpp"
#include "../../../common/dsp/Reverb.hpp"

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
    double decay = 100.0;   // percent of the natural decay time
    double release = 150.0; // ms to fall 60 dB after the key is let go
    double detune = 0.0;    // cents; above 0 every note sounds twice, one up and one down
    double width = 100.0;   // percent: 0 mono, 100 the full stereo recording; also spreads the keys and doubled notes
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

/** One computed recording: an attack that runs into a loop. */
struct Wave {
    int root = 60;          // the key it was made for
    double freq = 261.63;   // its pitch when played at kWaveRate
    int loopStart = 0, loopLength = 1;
    std::vector<float> data[2]; // left and right, loopStart + loopLength samples each
};

class WaveSet {
public:
    static constexpr int kFirstRoot = 22, kStep = 3, kCount = 29; // roots 22, 25 .. 106

    WaveSet() {
        for (int i = 0; i < kCount; ++i) build(waves_[i], kFirstRoot + i * kStep);
    }

    const Wave& forNote(int note) const {
        const int i = (note - kFirstRoot + kStep / 2) / kStep;
        return waves_[note < kFirstRoot ? 0 : (i >= kCount ? kCount - 1 : i)];
    }

private:
    static double noise(uint32_t& state) {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        return static_cast<double>(static_cast<int32_t>(state)) * (1.0 / 2147483648.0);
    }

    /** The colour of the instrument's body: thin lows, a hollow low-mid, a forward presence. */
    static double body(double f) {
        const double low = (f / 70.0) * (f / 70.0);
        const double dip = std::log2(f / 420.0), lift = std::log2(f / 2800.0);
        return low / (1.0 + low) * dbToLin(-4.0 * std::exp(-dip * dip / 0.6) + 5.0 * std::exp(-lift * lift / 1.2));
    }

    static void build(Wave& w, int root) {
        const double nominal = 440.0 * std::pow(2.0, (root - 69) / 12.0);
        // The loop holds a whole number of periods in a whole number of samples.
        const int periods = std::max(4, static_cast<int>(std::lround(0.2 * nominal)));
        w.root = root;
        w.loopLength = static_cast<int>(std::lround(periods * kWaveRate / nominal));
        w.freq = periods * kWaveRate / w.loopLength;
        w.loopStart = static_cast<int>(std::lround(kWaveRate * 0.9 * std::pow(2.0, -(root - 21) / 40.0)));
        for (auto& d : w.data) d.assign(static_cast<size_t>(w.loopStart + w.loopLength), 0.0f);

        // A stiff string: the partials lie a little above the harmonics, more so higher up.
        const double stiff = std::max(1.0e-4, 3.5e-4 * std::pow(2.0, (root - 60) / 9.0));
        const double hammer = std::clamp(3000.0 * std::pow(2.0, (root - 60) / 24.0), 1900.0, 9000.0);
        const double sustain = 2.5 * std::pow(2.0, -(root - 60) / 24.0); // seconds, for the fundamental
        const double first = sustain / (1.0 + std::pow(w.freq / 1800.0, 1.3));
        const double strikePoint = 0.12, strikeFade = std::exp(-1.0 / (0.04 * kWaveRate));
        uint32_t rng = 0x9e3779b9u + static_cast<uint32_t>(root) * 2654435761u;

        for (int k = 1; k <= 160; ++k) {
            const double f = k * w.freq * std::sqrt((1.0 + stiff * k * k) / (1.0 + stiff));
            if (f > 14000.0) break;
            const double level = std::pow(k, -0.75) * (0.3 + 0.7 * std::fabs(std::sin(kPi * k * strikePoint))) * std::exp(-f / hammer) * body(f);
            const double life = sustain / (1.0 + std::pow(f / 1800.0, 1.3));
            const double fade = std::exp(-(1.0 / life - 1.0 / first) / kWaveRate); // per sample, against the fundamental
            const double loopFreq = std::round(f / w.freq * periods) * w.freq / periods; // the nearest that fits the loop
            // The two microphones: the same below a few hundred hertz, their own level and phase above.
            const double apart = std::min(1.0, f / 700.0);
            const double tilt = 3.0 * apart * noise(rng), phase = 0.5 * noise(rng), offset = 0.28 * kPi * apart * noise(rng);

            for (int ch = 0; ch < 2; ++ch) {
                const double sign = ch == 0 ? 1.0 : -1.0;
                double amp = level * dbToLin(sign * tilt), strike = std::min(2.0, f / 2500.0);
                double c = std::cos(phase + sign * offset), s = std::sin(phase + sign * offset);
                float* d = w.data[ch].data();
                // Attack: the partial at its own frequency, fading against the fundamental.
                double step = 2.0 * kPi * f / kWaveRate, dc = std::cos(step), ds = std::sin(step);
                for (int i = 0; i < w.loopStart; ++i) {
                    d[i] += static_cast<float>(amp * (1.0 + strike) * s);
                    const double next = c * dc - s * ds;
                    s = s * dc + c * ds;
                    c = next;
                    amp *= fade;
                    strike *= strikeFade;
                }
                // Loop: the tone as it is at that moment, frozen, the partial moved to where it repeats exactly.
                amp *= 1.0 + strike;
                step = 2.0 * kPi * loopFreq / kWaveRate; dc = std::cos(step); ds = std::sin(step);
                for (int i = 0; i < w.loopLength; ++i) {
                    d[w.loopStart + i] += static_cast<float>(amp * s);
                    const double next = c * dc - s * ds;
                    s = s * dc + c * ds;
                    c = next;
                }
            }
        }

        // Level: the same loudness for every wave, measured over the first 150 ms of both sides.
        double sum = 0.0;
        const int window = std::min(w.loopStart, static_cast<int>(0.15 * kWaveRate));
        for (const auto& d : w.data)
            for (int i = 0; i < window; ++i) sum += 0.5 * static_cast<double>(d[i]) * d[i];
        const double gain = 0.25 / std::sqrt(sum / window + 1e-20);

        // The knock of the hammer: a short burst of filtered noise, partly shared by the two sides.
        const double lpCoef = std::exp(-2.0 * kPi * std::min(6000.0, 1.5 * hammer) / kWaveRate), hpCoef = std::exp(-2.0 * kPi * 250.0 / kWaveRate);
        const double burstFade = std::exp(-1.0 / (0.010 * kWaveRate));
        double lp[2] = {0, 0}, hp[2] = {0, 0}, level = 0.12;
        const int burst = std::min(w.loopStart, static_cast<int>(0.08 * kWaveRate));
        for (auto& d : w.data)
            for (float& v : d) v = static_cast<float>(v * gain);
        for (int i = 0; i < burst; ++i) {
            const double shared = noise(rng);
            for (int ch = 0; ch < 2; ++ch) {
                const double n = 0.7 * shared + 0.7 * noise(rng);
                lp[ch] = n + lpCoef * (lp[ch] - n);
                hp[ch] = lp[ch] + hpCoef * (hp[ch] - lp[ch]);
                w.data[ch][i] += static_cast<float>(level * (lp[ch] - hp[ch]));
            }
            level *= burstFade;
        }
        for (auto& d : w.data)
            for (int i = 0; i < 16; ++i) d[i] *= static_cast<float>(i / 16.0); // no step at the very start
    }

    Wave waves_[kCount];
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
        // Playing softer closes the filter, by up to seven octaves for the softest touch; higher keys keep it further open.
        const double cutoff = 12000.0 * std::pow(2.0, p_.tone / 100.0 * 4.0 + (v.velocity - 1.0) * 7.0 * sens + (v.note - 60) / 24.0);
        v.lpCoef = 1.0 - std::exp(-2.0 * kPi * std::clamp(cutoff, 150.0, 0.45 * fs_) / fs_);

        // The natural decay: long in the bass, short in the treble.
        const double seconds = 2.2 * std::pow(2.0, -(v.note - 60) / 18.0) * std::clamp(p_.decay, 10.0, 1000.0) / 100.0;
        v.decay = std::exp(-1.0 / (seconds * fs_));
        v.release = std::exp(-6.908 / (0.001 * std::clamp(p_.release, 5.0, 5000.0) * fs_)); // 60 dB down in that time

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
        const double loopEnd = w.loopStart + w.loopLength;
        const int last = w.loopStart + w.loopLength - 1;
        for (int i = 0; i < n; ++i) {
            const int at = static_cast<int>(v.pos);
            const double f = v.pos - at;
            // Neighbours for the interpolation, wrapping inside the loop.
            const bool looping = v.pos >= w.loopStart + 1;
            const int im = at > 0 ? (looping && at == w.loopStart ? last : at - 1) : 0;
            const int i1 = at + 1 > last ? at + 1 - w.loopLength : at + 1;
            const int i2 = at + 2 > last ? at + 2 - w.loopLength : at + 2;
            double s[2];
            for (int ch = 0; ch < 2; ++ch) {
                const double ym = d[ch][im], y0 = d[ch][at], y1 = d[ch][i1], y2 = d[ch][i2];
                const double c1 = 0.5 * (y1 - ym), c2 = ym - 2.5 * y0 + 2.0 * y1 - 0.5 * y2, c3 = 0.5 * (y2 - ym) + 1.5 * (y0 - y1);
                v.lp1[ch] += v.lpCoef * (((c3 * f + c2) * f + c1) * f + y0 - v.lp1[ch]);
                v.lp2[ch] += v.lpCoef * (v.lp1[ch] - v.lp2[ch]);
                s[ch] = v.lp2[ch] * v.env * v.level;
            }
            mixL_[i] += s[0] * v.mix[0][0] + s[1] * v.mix[0][1];
            mixR_[i] += s[0] * v.mix[1][0] + s[1] * v.mix[1][1];

            v.env *= (v.held || v.sustained) ? v.decay : v.release;
            v.pos += v.step;
            if (v.pos >= loopEnd) v.pos -= w.loopLength;
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
