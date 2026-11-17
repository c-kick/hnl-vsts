// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// Digital pianos of the 1980s and 1990s, one engine, several voices.
//
// WORKSTATION
//
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
//
// GRAND
//
// A concert grand, fitted in the same way to recordings of single notes played
// soft, medium and loud, with three sets of waves accordingly. Its dampers are
// slow on the heavy bass strings, quick in the treble and absent on the top keys.
//
// FM ELECTRIC
//
// The glassy electric piano of 1980s FM synthesizers, computed as those did
// it: sine waves whose phase is pushed around by other sine waves. Three
// pairs of a carrier and a modulator make a note. One pair has its modulator
// fourteen times higher than the note and makes the bell of the attack; two
// pairs with the modulator at the note's own pitch make the body. The second
// of those is tuned a fraction of a hertz above the first and is added on the
// left and taken off on the right: that gives the slow shimmer from side to
// side, and it vanishes when left and right are summed, so in mono nothing
// cancels. How far each modulator pushes follows velocity and fades during
// the note, so a hard note barks and then mellows.
#pragma once

#include <atomic>
#include <cstdint>

#include "../../../common/dsp/Filters.hpp"
#include "../../../common/dsp/Reverb.hpp"
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>

#include "DanceData.hpp"
#include "GrandData127.hpp"
#include "GrandData40.hpp"
#include "GrandData85.hpp"
#include "StageData127.hpp"
#include "StageData32.hpp"
#include "StageData64.hpp"
#include "StageData96.hpp"
#include "WorkstationData.hpp"

namespace hnl {

constexpr int kMaxVoices = 64;

enum PianoVoice { kWorkstation, kFmElectric, kStage, kDance, kGrand, kVoiceCount };

struct PianoParams {
    int voice = kWorkstation;
    double volume = 0.0;    // dB
    double tone = 0.0;      // -100 dark .. +100 open
    double velocity = 70.0; // percent: how much playing harder adds level and brightness
    double decay = 100.0;   // percent of the natural length of a held note
    double release = 600.0; // ms to fall 60 dB after the key is let go
    double detune = 0.0;    // cents; workstation: above 0 every note sounds twice, one up and one down. FM: a faster shimmer
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
    double rate = 32000.0; // the sample rate it is stored at
    double freq = 261.63;  // its pitch when played at that rate
    double scale = 1.0;    // what one step of the stored numbers is worth
    int length = 0;
    std::vector<int16_t> data[2]; // left and right, 16 bits as in the machines of the time
};

class WaveSet {
public:
    /**
     * One wave for each of the notes from the key lowRoot to topRoot, stored at `rate`. With `right`,
     * the notes were measured in stereo: `notes` is what the left side heard and `right` the other.
     * `wide` gives a mono measurement two sides that differ more, and from lower down.
     */
    WaveSet(const pianodata::Note* notes, const pianodata::Note* right, int count, int lowRoot, int topRoot, double rate, bool wide = false) {
        for (int i = 0; i < count; ++i) {
            if (notes[i].midi < lowRoot || notes[i].midi > topRoot) continue;
            waves_.emplace_back();
            build(waves_.back(), notes[i], right ? &right[i] : nullptr, rate, wide);
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

    static constexpr double kWideFrom = 50.0, kWideSpan = 150.0, kWideFirst = 0.20, kWideRest = 0.36;

    static void build(Wave& w, const pianodata::Note& note, const pianodata::Note* right, double rate, bool wide) {
        using namespace pianodata;
        std::vector<float> sides[2];
        w.root = note.midi;
        w.rate = rate;
        // What the ear takes as the pitch: the fundamental where it settles, when the first partial listed is the fundamental.
        const double first = note.count > 0 ? note.partials[0].late : 1.0;
        w.freq = note.freq * (first > 0.98 && first < 1.02 ? first : 1.0);
        w.length = static_cast<int>(std::min<double>(note.seconds, kTimeMs[kTimes - 1] * 0.001) * rate);
        for (auto& d : sides) d.assign(static_cast<size_t>(w.length) + 4, 0.0f);
        uint32_t rng = 0x9e3779b9u + static_cast<uint32_t>(note.midi) * 2654435761u;
        const int switchAt = std::min(w.length, static_cast<int>(kSwitchSeconds * rate));
        const double silent = -1100; // tenths of a dB

        for (int k = 0; k < note.count; ++k) {
            const Partial& p = note.partials[k];
            const double early = p.early * note.freq, late = p.late * note.freq;
            if (early > 0.47 * rate || late > 0.47 * rate) continue;
            // The two microphones: the same below a few hundred hertz, their own level and phase above.
            // A mono measurement is given two sides here. The fundamental keeps the same level on both,
            // so no note leans by chance. A stereo measurement already has its two sides and is left as it is.
            // Wide: for a source that is mono through and through. The sides part from the bass up and differ
            // about as much as two microphones some way apart would make them.
            const double apart = right ? 0.0 : (wide ? std::clamp((early - kWideFrom) / kWideSpan, 0.0, 1.0) : std::clamp((early - 120.0) / 300.0, 0.0, 1.0));
            // Levels differ only higher up, also when wide: a bass note louder on one side would lean.
            const double tilt = k == 0 ? 0.0 : 3.0 * (right ? 0.0 : std::clamp((early - 120.0) / 300.0, 0.0, 1.0)) * noise(rng);
            const double turnFirst = wide ? kWideFirst : 0.15, turnRest = wide ? kWideRest : 0.25;
            const double offset = kPi * apart * (k == 0 ? (noise(rng) < 0.0 ? -turnFirst : turnFirst) : turnRest * noise(rng));

            // A stereo measurement brings its own differences between the sides. They are kept within
            // bounds: microphones far apart hear single notes far to one side and partly out of phase,
            // which makes an instrument's notes jump about and thin out in mono. The fundamental stays
            // nearly centred, and no partial may differ so much that the sides work against each other.
            double levels[2][kTimes], phases[2] = {p.phase, p.phase};
            for (int j = 0; j < kTimes; ++j) levels[0][j] = levels[1][j] = p.level[j];
            if (right) {
                const Partial& other = right->partials[k];
                const double most = k == 0 ? 10.0 : 30.0; // tenths of a dB either way
                for (int j = 0; j < kTimes; ++j) {
                    const double middle = 0.5 * (p.level[j] + other.level[j]);
                    const double lean = std::clamp(0.5 * (p.level[j] - other.level[j]), -most, most);
                    levels[0][j] = middle + lean;
                    levels[1][j] = middle - lean;
                }
                double turn = std::remainder(static_cast<double>(p.phase) - other.phase, 2.0 * kPi);
                turn = std::clamp(turn, -(k == 0 ? 0.3 : 0.5) * kPi, (k == 0 ? 0.3 : 0.5) * kPi);
                phases[0] = p.phase;
                phases[1] = p.phase - turn;
            }

            for (int ch = 0; ch < 2; ++ch) {
                const double sign = ch == 0 ? 1.0 : -1.0;
                double phase = phases[ch] + sign * offset; // of the sine, at sample `at`
                float* d = sides[ch].data();
                int at = 0;
                for (int j = 0; j + 1 < kTimes && at < w.length; ++j) {
                    const int to = std::min(w.length, static_cast<int>(kTimeMs[j + 1] * 0.001 * rate));
                    const double from = levels[ch][j], until = levels[ch][j + 1];
                    const bool sounding = from > silent || until > silent;
                    // Between two measurements the level moves evenly in dB.
                    double amp = dbToLin(0.1 * from + sign * tilt);
                    const double grow = to > at ? dbToLin(0.1 * (until - from) / (to - at)) : 1.0;
                    while (at < to) {
                        // Early in the note the partial sits where it was measured early, later where it was measured late.
                        const int stop = at < switchAt ? std::min(to, switchAt) : to;
                        const double step = 2.0 * kPi * (at < switchAt ? early : late) / rate;
                        if (sounding) {
                            // Four sines a sample apart, each moved on four samples at a time: they do not wait for each other.
                            double c[4], s[4], a[4];
                            for (int q = 0; q < 4; ++q) {
                                c[q] = std::cos(phase + q * step);
                                s[q] = std::sin(phase + q * step);
                                a[q] = amp * std::pow(grow, q);
                            }
                            const double dc = std::cos(4.0 * step), ds = std::sin(4.0 * step), grow4 = grow * grow * grow * grow;
                            int i = at;
                            for (; i + 4 <= stop; i += 4)
                                for (int q = 0; q < 4; ++q) {
                                    d[i + q] += static_cast<float>(a[q] * s[q]);
                                    const double next = c[q] * dc - s[q] * ds;
                                    s[q] = s[q] * dc + c[q] * ds;
                                    c[q] = next;
                                    a[q] *= grow4;
                                }
                            for (int q = 0; i < stop; ++i, ++q) d[i] += static_cast<float>(a[q] * s[q]);
                        }
                        amp *= std::pow(grow, stop - at);
                        phase = std::fmod(phase + step * (stop - at), 2.0 * kPi);
                        at = stop;
                    }
                }
            }
        }

        // The knock of the attack: noise below the note's own pitch, at the measured level in each octave.
        const int frames = kKnockFrames;
        for (int b = 0; b < kKnockBands; ++b) {
            if (note.knock[b][0] <= silent && note.knock[b][1] <= silent) continue;
            const double wc = 2.0 * kPi * kKnockHz[b] / rate, alpha = std::sin(wc) / (2.0 * 1.414), a0 = 1.0 + alpha;
            const BiquadCoeffs bp{alpha / a0, 0.0, -alpha / a0, -2.0 * std::cos(wc) / a0, (1.0 - alpha) / a0};
            // How loud this filter makes white noise, so the measured level can be set exactly.
            Biquad f[3][2];
            for (auto& pair : f) for (auto& q : pair) q.set(bp);
            double power = 0.0;
            const int warm = static_cast<int>(0.5 * rate);
            for (int i = 0; i < warm; ++i) {
                const double v = f[2][1].process(f[2][0].process(noise(rng)));
                power += v * v;
            }
            const double unit = 1.0 / std::sqrt(power / warm);
            const int last = std::min(w.length, static_cast<int>(0.36 * rate));
            for (int i = 0; i < last; ++i) {
                // The level at the middle of each frame, moving evenly in dB between them and falling away after the last.
                const double ms = i * 1000.0 / rate;
                double db = note.knock[b][0] * 0.1;
                for (int j = 0; j < frames; ++j) {
                    const double mid = 0.5 * (kKnockMs[j] + kKnockMs[j + 1]), next = j + 1 < frames ? 0.5 * (kKnockMs[j + 1] + kKnockMs[j + 2]) : mid + 120.0;
                    const double here = note.knock[b][j] * 0.1, there = j + 1 < frames ? note.knock[b][j + 1] * 0.1 : here - 40.0;
                    if (ms >= mid) db = here + (there - here) * std::min(1.5, (ms - mid) / (next - mid));
                }
                const double amp = dbToLin(db) * unit, shared = noise(rng);
                for (int ch = 0; ch < 2; ++ch)
                    sides[ch][i] += static_cast<float>(amp * f[ch][1].process(f[ch][0].process(0.7071 * shared + 0.7071 * noise(rng))));
            }
        }

        // Level: the same loudness for every wave, measured over the first 200 ms of both sides.
        double sum = 0.0;
        const int window = std::max(1, std::min(w.length, static_cast<int>(0.2 * rate)));
        for (const auto& d : sides)
            for (int i = 0; i < window; ++i) sum += 0.5 * static_cast<double>(d[i]) * d[i];
        const double gain = 0.25 / std::sqrt(sum / window + 1e-20);
        const int fade = std::min(w.length, static_cast<int>(0.03 * rate));
        double peak = 1e-9;
        for (auto& d : sides) {
            for (float& v : d) v = static_cast<float>(v * gain);
            for (int i = 0; i < fade; ++i) d[w.length - 1 - i] *= static_cast<float>(i) / fade; // run out without a step
            for (int i = 0; i < 8 && i < w.length; ++i) d[i] *= static_cast<float>((i + 1) / 9.0);
            for (int i = w.length; i < w.length + 4; ++i) d[i] = 0.0f;
            for (float v : d) peak = std::max(peak, static_cast<double>(std::fabs(v)));
        }
        w.scale = peak / 32000.0;
        for (int ch = 0; ch < 2; ++ch) {
            w.data[ch].resize(sides[ch].size());
            for (size_t i = 0; i < sides[ch].size(); ++i) w.data[ch][i] = static_cast<int16_t>(std::lround(sides[ch][i] / w.scale));
        }
    }

    std::vector<Wave> waves_;
};

/** A fitted voice: one set of waves, or several for different velocities. */
struct Bank {
    std::vector<std::unique_ptr<WaveSet>> layers; // softest first
    std::vector<int> velocity;                    // the velocity each layer was measured at
    std::vector<int> low, top;                    // the lowest and highest key each layer has a wave for
    bool stereo = false;                          // measured in stereo: the keys already lie where they were heard

    /**
     * The layer measured at or just above this velocity, so a note is never brighter than it should be.
     * Where that layer has no wave near the key, the next louder one stands in.
     */
    int layerFor(int vel, int note) const {
        const int last = static_cast<int>(velocity.size()) - 1;
        for (int i = 0; i < last; ++i)
            if (vel <= velocity[i] && note <= top[i] + 3 && note >= low[i] - 3) return i;
        return last;
    }

    /** The voice's waves if they have been computed, else null. Never waits: safe on the audio thread. */
    static const Bank* ready(int voice) { return shared().ready[voice].load(std::memory_order_acquire); }

    /** Asks the worker thread to compute a voice's waves. Never waits: safe on the audio thread. */
    static void request(int voice) { shared().wanted.fetch_or(1u << voice, std::memory_order_release); }

    /**
     * Every piano calls attach() once before it asks for anything and detach() when it goes. The
     * first starts the worker thread and the last stops it, so none is left when the plugin is unloaded.
     */
    static void attach() {
        Shared& s = shared();
        std::lock_guard<std::mutex> guard(s.workerLock);
        if (s.users++ > 0) return;
        s.stop.store(false, std::memory_order_release);
        s.worker = std::thread([] {
            Shared& s = shared();
            while (!s.stop.load(std::memory_order_acquire)) {
                const uint32_t wanted = s.wanted.exchange(0u, std::memory_order_acq_rel);
                for (int v = 0; v < kVoiceCount; ++v)
                    if ((wanted >> v) & 1u) get(v);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
    }
    static void detach() {
        Shared& s = shared();
        std::lock_guard<std::mutex> guard(s.workerLock);
        if (--s.users > 0) return;
        s.stop.store(true, std::memory_order_release);
        if (s.worker.joinable()) s.worker.join();
    }

    /**
     * Computes a voice's waves if that has not been done yet, which takes most of a second, and
     * waits for it. Shared by every instance. Not for the audio thread: there, use ready() and request().
     */
    static const Bank& get(int voice) {
        Shared& s = shared();
        std::lock_guard<std::mutex> guard(s.buildLock);
        std::unique_ptr<Bank>& b = s.banks[voice];
        if (!b) {
            b = std::make_unique<Bank>();
            const auto add = [&](const pianodata::Note* notes, int count, int top, double rate, int vel, int low = 0, const pianodata::Note* right = nullptr, bool wide = false) {
                b->layers.push_back(std::make_unique<WaveSet>(notes, right, count, low, top, rate, wide));
                b->velocity.push_back(vel);
                b->low.push_back(low);
                b->top.push_back(top);
                b->stereo = b->stereo || right != nullptr;
            };
            using namespace pianodata;
            if (voice == kStage) {
                add(stage32::kNotes, stage32::kNoteCount, 127, 20000.0, 32);
                add(stage64::kNotes, stage64::kNoteCount, 127, 20000.0, 64);
                add(stage96::kNotes, stage96::kNoteCount, 127, 20000.0, 96);
                add(stage127::kNotes, stage127::kNoteCount, 127, 20000.0, 127);
            } else if (voice == kGrand) {
                // Soft, medium and loud, measured in stereo. Where a set is not to be trusted (soft notes high
                // up hardly rise above the noise of the recording, and the lowest soft one is lost in rumble),
                // the next louder set stands in.
                add(grand40::kNotes, grand40::kNoteCount, 78, 32000.0, 40, 27, grand40::kNotesRight);
                add(grand85::kNotes, grand85::kNoteCount, 102, 32000.0, 85, 24, grand85::kNotesRight);
                add(grand127::kNotes, grand127::kNoteCount, 108, 32000.0, 127, 24, grand127::kNotesRight);
            } else if (voice == kDance) {
                add(dance::kNotes, dance::kNoteCount, 127, 32000.0, 127);
            } else {
                // Measured from a mono source, so it is given wide sides.
                add(workstation::kNotes, workstation::kNoteCount, 96, 32000.0, 127, 0, nullptr, true);
            }
            s.ready[voice].store(b.get(), std::memory_order_release); // only now may the audio thread see it
        }
        return *b;
    }

private:
    struct Shared {
        std::mutex buildLock, workerLock;
        std::unique_ptr<Bank> banks[kVoiceCount];
        std::atomic<const Bank*> ready[kVoiceCount] = {};
        std::atomic<uint32_t> wanted{0u};
        std::atomic<bool> stop{false};
        std::thread worker;
        int users = 0;
    };
    static Shared& shared() {
        static Shared s;
        return s;
    }
};

// ------------------------------------------------------------------ engine

class Piano {
public:
    Piano() = default;
    ~Piano() { if (attached_) Bank::detach(); }
    Piano(const Piano&) = delete;
    Piano& operator=(const Piano&) = delete;

    void prepare(double sampleRate) {
        fs_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        if (!attached_) {
            Bank::attach();
            attached_ = true;
        }
        if (p_.voice != kFmElectric) Bank::get(p_.voice); // computed here, not at the first note
        voice_ = p_.voice;
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

    /**
     * Takes the settings at once. A voice chosen for the first time has its waves computed here,
     * which takes most of a second: not for the audio thread.
     */
    void setParams(const PianoParams& p) {
        store(p);
        if (p_.voice != kFmElectric) Bank::get(p_.voice);
        voice_ = p_.voice;
        apply();
    }

    /**
     * The same for the audio thread: never waits. A voice whose waves are not there yet is asked
     * for, and the piano keeps the voice it has until they are (see process()).
     */
    void setParamsRealtime(const PianoParams& p) {
        store(p);
        adoptVoice();
        apply();
    }

    /** The voice new notes get now; differs from params().voice while that one is being computed. */
    int soundingVoice() const { return voice_; }
    const PianoParams& params() const { return p_; }

    PianoMeters meters;

    // ---------------------------------------------------------------- MIDI

    void noteOn(int key, int velocity) {
        if (velocity <= 0) { noteOff(key); return; }
        const int note = key + 12 * p_.octave;
        if (note < 0 || note > 127) return;
        for (Voice& v : voices_) // the same key again: the old note makes way
            if (v.active && v.key == key) v.held = v.sustained = false;
        const bool fm = voice_ == kFmElectric;
        const Bank* const bank = fm ? nullptr : Bank::ready(voice_);
        if (!fm && bank == nullptr) return; // not prepared yet
        const bool doubled = !fm && p_.detune > 0.05;
        for (int layer = 0; layer < (doubled ? 2 : 1); ++layer) {
            Voice& v = allocate();
            v.active = true;
            v.kind = voice_;
            v.key = key;
            v.note = note;
            v.layer = doubled ? (layer == 0 ? -1 : 1) : 0;
            v.velocity = velocity / 127.0;
            v.held = true;
            v.sustained = false;
            if (!fm) {
                const int layer = bank->layerFor(velocity, note);
                v.wave = &bank->layers[layer]->forNote(note);
                v.layerVelocity = bank->velocity[layer] / 127.0;
                v.placed = bank->stereo;
            }
            v.pos = 0.0;
            v.env = 1.0;
            v.lp1[0] = v.lp1[1] = v.lp2[0] = v.lp2[1] = 0.0;
            v.age = ++clock_;
            shape(v);
            if (fm) strike(v);
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
        } else if (number == 121) { // reset all controllers
            pedal_ = false;
            for (Voice& v : voices_) v.sustained = false;
            pitchBend(0.0);
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
        if (voice_ != p_.voice) adoptVoice();
        double pk[2] = {0, 0};
        int sounding = 0;

        for (int start = 0; start < frames; start += kChunk) {
            const int n = std::min(kChunk, frames - start);
            std::fill(mixL_, mixL_ + n, 0.0);
            std::fill(mixR_, mixR_ + n, 0.0);
            sounding = 0;
            for (Voice& v : voices_) {
                if (!v.active) continue;
                if (v.kind == kFmElectric) renderFm(v, n);
                else render(v, n);
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
    /** Keeps the settings, within what the engine can work with whoever calls. */
    void store(const PianoParams& p) {
        p_ = p;
        p_.voice = std::clamp(p.voice, 0, kVoiceCount - 1);
        p_.octave = std::clamp(p.octave, -2, 2);
        p_.tone = std::clamp(p.tone, -100.0, 100.0);
        p_.detune = std::clamp(p.detune, 0.0, 100.0);
        p_.tune = std::clamp(p.tune, -1200.0, 1200.0);
        p_.volume = std::clamp(p.volume, -120.0, 24.0);
    }

    /** Moves to the voice asked for once its waves are there. */
    void adoptVoice() {
        if (voice_ == p_.voice) return;
        if (p_.voice == kFmElectric || Bank::ready(p_.voice) != nullptr) voice_ = p_.voice;
        else Bank::request(p_.voice);
    }

    void apply() {
        configure();
        for (Voice& v : voices_) if (v.active) shape(v);
    }

    struct Voice {
        bool active = false, held = false, sustained = false;
        int key = 0, note = 60, layer = 0, kind = kWorkstation;
        bool placed = false; // its wave already has the key where it was heard: no panning by key
        uint64_t age = 0;
        const Wave* wave = nullptr;
        double velocity = 1.0, layerVelocity = 1.0, pos = 0.0, step = 1.0;
        double env = 0.0, decay = 1.0, release = 1.0, level = 1.0;
        double mix[2][2] = {{1, 0}, {0, 1}}; // output side from wave side
        double lpCoef = 1.0, lp1[2] = {0, 0}, lp2[2] = {0, 0};
        // FM: three pairs (bell, body, the body's detuned twin), each a carrier [2p] and its modulator [2p + 1].
        double phase[6] = {}, inc[6] = {};             // in cycles, and cycles per sample
        double push[3] = {}, pushFade[3] = {}, pushRest[3] = {}; // how far the modulator pushes (cycles): the part that fades, and what stays
        double amp[3] = {}, ampFade[3] = {};
    };

    /** One cycle of a sine, for the FM voice. */
    struct SineTable {
        static constexpr int kSize = 4096;
        float v[kSize + 1];
        SineTable() { for (int i = 0; i <= kSize; ++i) v[i] = static_cast<float>(std::sin(2.0 * kPi * i / kSize)); }
    };
    /** cycles must be above -16: the phases here are 0..1 plus a push of a few cycles at most. */
    static inline const SineTable kSine;
    static inline double sine(double cycles) {
        const SineTable& table = kSine;
        const double lifted = cycles + 16.0, x = (lifted - static_cast<int>(lifted)) * SineTable::kSize;
        const int i = static_cast<int>(x);
        return table.v[i] + (x - i) * (table.v[i + 1] - table.v[i]);
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
        if (v.kind == kFmElectric) { shapeFm(v, freq, sens, width); return; }
        v.step = v.wave->rate / fs_ * freq / v.wave->freq;

        v.level = kVoiceLevel * v.wave->scale * std::pow(v.velocity, 2.0 * sens) * (v.layer != 0 ? 0.7071 : 1.0);
        // Wide open at the velocity the wave was measured at; playing softer than that closes the filter, by up to
        // five octaves for the softest touch. Higher keys keep it further open.
        const double cutoff = 16000.0 * std::pow(2.0, p_.tone / 100.0 * 4.0 + std::min(0.0, v.velocity - v.layerVelocity) * 5.0 * sens + (v.note - 60) / 24.0);
        v.lpCoef = 1.0 - std::exp(-2.0 * kPi * std::clamp(cutoff, 150.0, 0.45 * fs_) / fs_);

        // The waves carry the natural decay. Below 100% a held note is faded on top of that:
        // at 50% it is gone in about half the time.
        const double natural = 2.0 * std::pow(2.0, -(v.note - 60) / 24.0), shorter = 100.0 / std::clamp(p_.decay, 5.0, 100.0) - 1.0;
        v.decay = std::exp(-shorter / (natural * fs_));
        v.release = std::exp(-6.908 / (0.001 * std::clamp(p_.release, 5.0, 10000.0) * fs_)); // 60 dB down in that time
        if (v.kind == kGrand) {
            // Dampers: slow to quieten the heavy bass strings, quick in the treble, and none at all on the top keys.
            const double dampers = std::clamp(std::pow(2.0, -(v.note - 60) / 20.0), 0.5, 2.5);
            v.release = v.note >= 90 ? v.decay : std::exp(-6.908 / (0.001 * std::clamp(p_.release, 5.0, 10000.0) * dampers * fs_));
        }

        // Width narrows the recording towards mono; the keys lie low to high from left to right,
        // as the player hears them, and a doubled note has its two halves apart.
        const double pan = std::clamp(width * ((v.placed ? 0.0 : 0.35) * std::clamp((v.note - 64) / 40.0, -1.0, 1.0) + 0.5 * v.layer), -1.0, 1.0);
        const double same = 0.5 * (1.0 + width), cross = 0.5 * (1.0 - width);
        const double gl = std::sqrt(1.0 - pan), gr = std::sqrt(1.0 + pan);
        v.mix[0][0] = gl * same; v.mix[0][1] = gl * cross;
        v.mix[1][0] = gr * cross; v.mix[1][1] = gr * same;
    }

    // ------------------------------------------------------------------ FM

    /** Velocity as the FM voice feels it: with Velocity at 0% every note is a hard one. */
    double touch(const Voice& v) const { return 1.0 - std::clamp(p_.velocity, 0.0, 100.0) / 100.0 * (1.0 - v.velocity); }

    void shapeFm(Voice& v, double freq, double sens, double width) {
        // The twin sits a fraction of a hertz above the body, the same on most keys, so the shimmer
        // is as slow in the treble as in the bass (and never more than a few cents). Detune widens it.
        const double f = freq / fs_, apart = std::min((0.6 + 0.25 * p_.detune) / fs_, f * (0.0023 + 0.0006 * p_.detune));
        v.inc[0] = f;          v.inc[1] = 14.0 * f;
        v.inc[2] = f;          v.inc[3] = f;
        v.inc[4] = f + apart;  v.inc[5] = f + apart;

        v.level = kFmLevel * std::pow(v.velocity, 2.0 * sens);
        v.decay = 1.0; // the pairs fade by themselves
        v.release = std::exp(-6.908 / (0.001 * std::clamp(p_.release, 5.0, 10000.0) * fs_));

        const double length = std::clamp(p_.decay, 5.0, 100.0) / 100.0, high = std::pow(2.0, -(v.note - 60) / 24.0);
        const double seconds[3] = {1.2 * high * length, 4.0 * high * length, 4.0 * high * length};
        const double pushSeconds[3] = {0.5 * high, 1.0 * std::sqrt(high), 1.0 * std::sqrt(high)};
        for (int p = 0; p < 3; ++p) {
            v.ampFade[p] = std::exp(-1.0 / (seconds[p] * fs_));
            v.pushFade[p] = std::exp(-1.0 / (pushSeconds[p] * fs_));
        }

        // The keys lie low to high from left to right.
        const double pan = std::clamp(width * 0.35 * std::clamp((v.note - 64) / 40.0, -1.0, 1.0), -1.0, 1.0);
        const double same = 0.5 * (1.0 + width), cross = 0.5 * (1.0 - width);
        const double gl = std::sqrt(1.0 - pan), gr = std::sqrt(1.0 + pan);
        v.mix[0][0] = gl * same; v.mix[0][1] = gl * cross;
        v.mix[1][0] = gr * cross; v.mix[1][1] = gr * same;
    }

    /** The start of an FM note: how hard each modulator pushes, and how loud each pair is. */
    void strike(Voice& v) {
        const double hard = touch(v), bright = std::pow(2.0, p_.tone / 100.0 * 1.5);
        const double freq = v.inc[0] * fs_;
        const double keys = std::clamp(std::pow(2.0, -(v.note - 60) / 30.0), 0.4, 1.15); // less push higher up, as on the originals
        for (double& ph : v.phase) ph = 0.0;

        // Bell: its overtones lie around fourteen times the note. Held back where they would pass the top of what
        // the sample rate can carry, and left out above that.
        const double room = (0.45 * fs_ - freq) / (14.0 * freq) - 1.0;
        const double bell = std::min((1.1 + 1.4 * hard) * bright, std::max(room, 0.0));
        v.push[0] = bell / (2.0 * kPi);
        v.pushRest[0] = 0.0;
        v.amp[0] = room <= 0.0 ? 0.0 : 0.4 * std::pow(hard, 1.5) * std::clamp(1.0 - (v.note - 66) / 40.0, 0.2, 1.0) * std::min(1.0, room);

        // Body: a soft note is nearly a sine, a hard one barks, and both mellow as the note rings.
        // The twin is softer and rounder than the body itself.
        static const double kHalfPush[3] = {0.0, 1.0, 0.7}, kHalfLevel[3] = {0.0, 0.7, 0.3};
        for (int p = 1; p < 3; ++p) {
            const double start = (0.3 + 1.5 * hard * hard) * bright * keys * kHalfPush[p], rest = 0.3 * start + 0.1;
            v.push[p] = (start - rest) / (2.0 * kPi);
            v.pushRest[p] = rest / (2.0 * kPi);
            v.amp[p] = kHalfLevel[p];
        }
    }

    void renderFm(Voice& v, int n) {
        for (int i = 0; i < n; ++i) {
            double out[3];
            for (int p = 0; p < 3; ++p) {
                double& car = v.phase[2 * p];
                double& mod = v.phase[2 * p + 1];
                mod += v.inc[2 * p + 1];
                if (mod >= 1.0) mod -= 1.0;
                car += v.inc[2 * p];
                if (car >= 1.0) car -= 1.0;
                out[p] = sine(car + (v.push[p] + v.pushRest[p]) * sine(mod)) * v.amp[p];
                v.push[p] *= v.pushFade[p];
                v.amp[p] *= v.ampFade[p];
            }
            // The twin is added on the left and taken off on the right.
            const double gain = v.env * v.level, centre = 0.7071 * out[0] + out[1];
            const double left = (centre + out[2]) * gain, right = (centre - out[2]) * gain;
            mixL_[i] += left * v.mix[0][0] + right * v.mix[0][1];
            mixR_[i] += left * v.mix[1][0] + right * v.mix[1][1];
            if (!(v.held || v.sustained)) v.env *= v.release;
        }
        if (v.env * (v.amp[0] + v.amp[1] + v.amp[2]) < 1e-5) v.active = false;
    }

    // --------------------------------------------------------- workstation

    void render(Voice& v, int n) {
        const Wave& w = *v.wave;
        const int16_t* d[2] = {w.data[0].data(), w.data[1].data()};
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
    static constexpr double kFmLevel = 0.10;    // as loud as the workstation voice

    PianoParams p_;
    double fs_ = 48000.0, gainCoef_ = 0.0;
    double volume_ = 1.0, volumeT_ = 1.0, rev_ = 0.0, revT_ = 0.0, bend_ = 1.0;
    bool pedal_ = false, attached_ = false;
    int voice_ = kWorkstation; // the voice new notes get: p_.voice once its waves are there
    long tail_ = 0;
    uint64_t clock_ = 0;
    Voice voices_[kMaxVoices];
    double mixL_[kChunk] = {}, mixR_[kChunk] = {};
    Reverb reverb_;
};

} // namespace hnl
