// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// The part of the sampler that makes sound: it plays the document it is
// given (Model.hpp) in answer to notes.
//
// A note starts one voice for every zone that covers its key and velocity.
// A voice reads its zone's sound (Interp.hpp), up to sixteen times at once
// for unison and chords, adds an oscillator and noise, and sends the sum
// through a saturator and a filter. Three envelopes, three LFOs, a row of
// steps and the modulation matrix move its settings: they are worked out
// every `ctl_` samples (about 1500 times a second), counted from the start
// of the voice, so that the result does not depend on how the host cuts
// time into blocks.
//
// Everything here runs on the audio thread and allocates nothing after
// prepare(). The engine does not own documents: it is handed a pointer
// (setDocument) and says when it no longer uses one (takeReleased), which
// is when no sounding voice plays a sound from it any more.
#pragma once

#include <atomic>
#include <cstring>

#include "../../../common/dsp/Filters.hpp"
#include "../../../common/dsp/Reverb.hpp"
#include "Effects.hpp"
#include "Interp.hpp"
#include "Model.hpp"

namespace hnl {
namespace sampler {

inline uint32_t hash32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x;
}
/** -1..1 from two whole numbers; the same numbers always give the same value. */
inline float hashBipolar(uint32_t a, uint32_t b) {
    return static_cast<float>(hash32(a * 0x9e3779b9u + hash32(b)) >> 8) * (2.0f / 16777216.0f) - 1.0f;
}

struct Rng {
    uint32_t s = 0x2545f491u;
    inline uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    inline float bipolar() { return static_cast<float>(next() >> 8) * (2.0f / 16777216.0f) - 1.0f; }
};

/** Delay, attack, hold, decay, sustain, release. tick() moves it on by a stretch of time and gives its level, 0..1. */
struct Envelope {
    enum Stage { kIdle, kDelay, kAttack, kHold, kDecay, kSustain, kRelease };
    int stage = kIdle;
    double value = 0.0, time = 0.0;

    void start() { stage = kDelay; time = 0.0; }
    void release() { if (stage != kIdle) stage = kRelease; }
    bool idle() const { return stage == kIdle; }

    /** `p` holds the seven envelope fields of a part (EnvField), times in ms. */
    double tick(const double* p, double ms) {
        // The curve setting bends decay and release from a straight line (0) to the steep start and long tail of a discharging capacitor (100).
        const double ratio = std::pow(10.0, 2.0 - 0.043 * p[kEnvCurve]), attackRatio = std::max(ratio, 0.3);
        if (stage == kDelay) {
            time += ms;
            if (time >= p[kEnvDelay]) { stage = kAttack; time = 0.0; }
            else return value;
        }
        if (stage == kAttack) {
            const double coef = std::exp(-std::log((1.0 + attackRatio) / attackRatio) * ms / std::max(p[kEnvAttack], 0.01));
            value = (1.0 + attackRatio) * (1.0 - coef) + value * coef;
            if (value >= 1.0) { value = 1.0; stage = kHold; time = 0.0; }
            return value;
        }
        if (stage == kHold) {
            time += ms;
            if (time >= p[kEnvHold]) stage = kDecay;
            return value;
        }
        const double coefFor = std::log((1.0 + ratio) / ratio) * ms;
        if (stage == kDecay) {
            const double sustain = 0.01 * p[kEnvSustain], coef = std::exp(-coefFor / std::max(p[kEnvDecay], 0.01));
            value = (sustain - ratio) * (1.0 - coef) + value * coef;
            if (value <= sustain) { value = sustain; stage = kSustain; }
        } else if (stage == kSustain) {
            value = 0.01 * p[kEnvSustain];
        } else if (stage == kRelease) {
            const double coef = std::exp(-coefFor / std::max(p[kEnvRelease], 0.01));
            value = -ratio * (1.0 - coef) + value * coef;
            if (value <= 1e-5) { value = 0.0; stage = kIdle; }
        }
        return value;
    }
};

/** One turn of an LFO: `phase` 0..1, `cycle` counts the turns, for the shapes that take a new chance value each turn. */
inline float lfoShape(int wave, double phase, uint32_t cycle, uint32_t seed) {
    const float p = static_cast<float>(phase);
    switch (wave) {
    case 0: return static_cast<float>(std::sin(2.0 * kPi * phase));
    case 1: return p < 0.25f ? 4.0f * p : (p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f);
    case 2: return 1.0f - 2.0f * p;
    case 3: return 2.0f * p - 1.0f;
    case 4: return p < 0.5f ? 1.0f : -1.0f;
    case 5: return hashBipolar(cycle, seed);
    default: {
        const float a = hashBipolar(cycle, seed), b = hashBipolar(cycle + 1u, seed), t = p * p * (3.0f - 2.0f * p);
        return a + (b - a) * t;
    }
    }
}

/** A filter with a lowpass, bandpass and highpass output at once, that stays stable while its settings move. */
struct Svf {
    float ic1 = 0.0f, ic2 = 0.0f;
    void reset() { ic1 = ic2 = 0.0f; }
    inline void process(float x, float a1, float a2, float a3, float k, float& lp, float& bp, float& hp) {
        const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        lp = v2;
        bp = v1;
        hp = x - k * v1 - v2;
    }
};

inline float shape(int type, float x) {
    switch (type) {
    case 1: { // soft: close to tanh
        const float c = x < -3.0f ? -3.0f : (x > 3.0f ? 3.0f : x);
        return c * (27.0f + c * c) / (27.0f + 9.0f * c * c);
    }
    case 2: return x < -1.0f ? -1.0f : (x > 1.0f ? 1.0f : x);
    case 3: return static_cast<float>(std::sin(0.5 * kPi * x));
    case 4: { // tube: the two halves of the wave are not treated alike
        const float y = x + 0.25f * x * x, c = y < -3.0f ? -3.0f : (y > 3.0f ? 3.0f : y);
        return c * (27.0f + c * c) / (27.0f + 9.0f * c * c);
    }
    default: return x;
    }
}

inline float polyBlep(double t, double dt) {
    if (t < dt) { const double x = t / dt; return static_cast<float>(x + x - x * x - 1.0); }
    if (t > 1.0 - dt) { const double x = (t - 1.0) / dt; return static_cast<float>(x * x + x + x + 1.0); }
    return 0.0f;
}

class Engine {
public:
    static constexpr int kVoices = 64, kPlayers = 16, kMaxCtl = 256, kChunk = 128, kHeld = 8;

    struct Meters {
        std::atomic<float> outPeak[2];
        std::atomic<float> partPeak[kParts];
        std::atomic<int> voices{0};
        std::atomic<uint32_t> keys[4];   // which keys sound, a bit each
        std::atomic<int> zone{0};        // the zone of the newest voice, and how far along its sound it is (0..1)
        std::atomic<float> position{0.0f};
        Meters() {
            for (auto& p : outPeak) p.store(0.0f);
            for (auto& p : partPeak) p.store(0.0f);
            for (auto& k : keys) k.store(0u);
        }
    } meters;

    void prepare(double fs) {
        fs_ = fs > 0.0 ? fs : 48000.0;
        ctl_ = std::clamp(static_cast<int>(std::lround(fs_ / 1500.0)), 8, kMaxCtl);
        Kernels::get();
        delay_.prepare(fs_);
        chorus_.prepare(fs_);
        reverb_.prepare(fs_);
        smooth_ = static_cast<float>(1.0 - onePoleCoef(8.0, fs_));
        reset();
    }

    /** Silence, and everything as at the start. The document stays. */
    void reset() {
        for (Voice& v : voices_) v.active = false;
        if (preview_.data != nullptr) previewReleased_ = preview_.data;
        preview_.data = nullptr;
        fxIdle_ = 1LL << 60;
        for (Chan& c : chan_) c = Chan();
        for (PartState& p : part_) p = PartState();
        delay_.reset();
        chorus_.reset();
        reverb_.reset();
        rng_ = Rng();
        now_ = 0;
        order_ = 0;
        master_ = masterTarget_;
        fxKey_ = -1;
        releaseUnused();
    }

    // ------------------------------------------------------------ documents

    /** The document to play from now on. It must stay where it is until takeReleased() hands it back. */
    void setDocument(const Instrument* doc) {
        if (doc == doc_) return;
        doc_ = doc;
        fxKey_ = -1;
        if (doc == nullptr) {
            for (Voice& v : voices_) v.active = false;
            return;
        }
        for (const Instrument*& h : held_)
            if (h == doc) return;
        for (const Instrument*& h : held_)
            if (h == nullptr) { h = doc; return; }
        // No room to remember another one: the oldest goes, with the notes that still play from it.
        for (Voice& v : voices_)
            if (v.active && v.doc == held_[0]) v.active = false;
        pushReleased(held_[0]);
        for (int i = 0; i + 1 < kHeld; ++i) held_[i] = held_[i + 1];
        held_[kHeld - 1] = doc;
    }
    /** A document the engine no longer uses, or null. Call until it gives null. */
    const Instrument* takeReleased() {
        if (releasedCount_ == 0) return nullptr;
        return released_[--releasedCount_];
    }

    /** Plays a sound as it is, apart from the document, for listening to a file before using it. Null stops it. */
    void setPreview(const AudioData* data) {
        if (preview_.data != nullptr) previewReleased_ = preview_.data;
        preview_.data = data;
        preview_.pos = 0.0;
        preview_.gain = 1.0f;
        preview_.stopping = false;
    }
    void stopPreview() { preview_.stopping = true; }
    /** A previewed sound the engine has finished with, or null. */
    const AudioData* takeReleasedPreview() {
        const AudioData* p = previewReleased_;
        previewReleased_ = nullptr;
        return p;
    }

    // --------------------------------------------------------------- inputs

    void setMaster(double db) { masterTarget_ = static_cast<float>(dbToLin(db)); }
    void setMacro(int index, float value) { if (index >= 0 && index < kMacros) macro_[index] = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value); }
    void setTempo(double bpm) { bpm_ = isFinite(bpm) && bpm >= 20.0 && bpm <= 999.0 ? bpm : 120.0; }

    void noteOn(int channel, int key, int velocity) {
        if (velocity <= 0) { noteOff(channel, key, 64); return; }
        if (doc_ == nullptr) return;
        for (int p = 0; p < kParts; ++p)
            if (listens(p, channel)) partNoteOn(p, channel, key, velocity);
    }

    void noteOff(int channel, int key, int velocity) {
        if (doc_ == nullptr) return;
        for (int p = 0; p < kParts; ++p)
            if (listens(p, channel)) partNoteOff(p, channel, key, velocity);
    }

    void controller(int channel, int cc, int value) {
        Chan& c = chan_[channel & 15];
        const float v = static_cast<float>(value & 127) / 127.0f;
        switch (cc) {
        case 1: c.wheel = v; break;
        case 7: c.volume = v; break;
        case 11: c.expression = v; break;
        case 74: c.timbre = v; break;
        case 64:
            c.sustain = value >= 64;
            if (!c.sustain)
                for (Voice& x : voices_)
                    if (x.active && x.channel == (channel & 15) && x.pedalled) { x.pedalled = false; releaseVoice(x); }
            break;
        case 120:
            for (Voice& x : voices_) x.active = false;
            preview_.stopping = true;
            for (PartState& p : part_) p.stackCount = 0;
            break;
        case 121: {
            const bool sustain = c.sustain;
            c = Chan();
            c.sustain = sustain;
            break;
        }
        case 123:
            for (Voice& x : voices_)
                if (x.active) { x.pedalled = false; releaseVoice(x, true); }
            for (PartState& p : part_) p.stackCount = 0;
            break;
        default: break;
        }
    }
    void pitchBend(int channel, double value) { chan_[channel & 15].bend = static_cast<float>(value < -1.0 ? -1.0 : (value > 1.0 ? 1.0 : value)); }
    void channelPressure(int channel, int value) { chan_[channel & 15].pressure = static_cast<float>(value & 127) / 127.0f; }
    void polyPressure(int channel, int key, int value) {
        for (Voice& v : voices_)
            if (v.active && v.channel == (channel & 15) && v.key == key) v.polyPressure = static_cast<float>(value & 127) / 127.0f;
    }

    // ---------------------------------------------------------------- sound

    /** Writes `frames` samples to each of the 2 x kOutputs channels in `outs`. */
    void process(float* const* outs, int frames) {
        ScopedNoDenormals noDenormals;
        for (int done = 0; done < frames;) {
            const int n = std::min(kChunk, frames - done);
            float* o[2 * kOutputs];
            for (int c = 0; c < 2 * kOutputs; ++c) {
                o[c] = outs[c] + done;
                std::memset(o[c], 0, sizeof(float) * static_cast<size_t>(n));
            }
            chunk(o, n);
            done += n;
            now_ += n;
        }
        updateMeters();
        releaseUnused();
    }

    int activeVoices() const {
        int n = 0;
        for (const Voice& v : voices_) n += v.active ? 1 : 0;
        return n;
    }

private:
    struct Chan {
        float bend = 0.0f, pressure = 0.0f, wheel = 0.0f, timbre = 0.5f, volume = 1.0f, expression = 1.0f;
        bool sustain = false;
    };

    struct Player {
        double pos = 0.0, inc = 0.0, oscPhase = 0.0, oscInc = 0.0;
        float panL = 1.0f, panR = 1.0f, semis = 0.0f;
        int dir = 1;
        bool inLoop = false, done = false;
    };

    struct Voice {
        bool active = false, released = false, pedalled = false, withOsc = false, killed = false;
        int part = 0, channel = 0, key = 0, pitchKey = 0, velocity = 0, zoneIndex = -1, zoneId = 0, countdown = 0;
        uint32_t order = 0, seed = 0;
        const Instrument* doc = nullptr;
        const AudioData* data = nullptr;
        double z[kZoneParamCount];
        // What the sound is read from: frame `first` of the file is place 0, `step` says which way it runs, `length` how far.
        long long first = 0, length = 0;
        int step = 1;
        double loopLo = 0.0, loopHi = 0.0, crossfade = 0.0, rootPeriod = 0.0;
        bool looping = false;
        int loopType = 0;
        const Kernel* kernel = nullptr;
        Player players[kPlayers];
        int playerCount = 1, unison = 1;
        Rng noise;
        Envelope env[kEnvs];
        double lfoPhase[kLfos], age = 0.0; // age in ms
        uint32_t lfoCycle[kLfos];
        float lfoValue[kLfos], stepValue = 0.0f, random = 0.0f, polyPressure = 0.0f, glide = 0.0f;
        double modPhase = 0.0, modInc = 0.0;
        Svf filter[2][2];
        float dcIn[2], dcOut[2], pink[3];
        // Settings of the moment, and how much each changes per sample until the next time they are worked out.
        float gain = 0.0f, gainStep = 0.0f, panL = 1.0f, panR = 1.0f, panLStep = 0.0f, panRStep = 0.0f;
        float sampleLevel = 0.0f, sampleStep = 0.0f, oscLevel = 0.0f, oscStep = 0.0f, noiseLevel = 0.0f, noiseStep = 0.0f;
        float modAmount = 0.0f, drive = 1.0f, driveOut = 1.0f, width = 0.5f;
        float fa1 = 0.0f, fa2 = 0.0f, fa3 = 0.0f, fk = 2.0f, mixLp = 1.0f, mixBp = 0.0f, mixHp = 0.0f;
        float fb1 = 0.0f, fb2 = 0.0f, fb3 = 0.0f, fbk = 1.4142135f; // the second stage of the 24 dB filters: no resonance of its own
        int filterType = 0, satType = 0, modMode = 0, oscWave = 0, noiseColour = 0;
        bool first_ = true;
    };

    struct PartState {
        int stack[16];
        unsigned char down[128] = {}; // the velocity each key went down with, 0 when it is up
        int stackCount = 0, lastKey = -1, robin = 0;
        float gainL = 0.0f, gainR = 0.0f; // smoothed
        float sendDelay = 0.0f, sendReverb = 0.0f, sendChorus = 0.0f;
        double lfoPhase[kLfos] = {0.0, 0.0, 0.0}, lfoRate[kLfos] = {0.0, 0.0, 0.0};
        long long lfoTime = 0;
        float peak = 0.0f;
        bool fresh = true;
    };

    struct Preview {
        const AudioData* data = nullptr;
        double pos = 0.0;
        float gain = 1.0f;
        bool stopping = false;
    };

    static inline void panGains(float pan, float& l, float& r) { // -1..1; the middle is 1 on both sides
        const double a = (static_cast<double>(pan < -1.0f ? -1.0f : (pan > 1.0f ? 1.0f : pan)) + 1.0) * 0.25 * kPi;
        l = static_cast<float>(1.4142135623730951 * std::cos(a));
        r = static_cast<float>(1.4142135623730951 * std::sin(a));
    }

    bool listens(int p, int channel) const {
        const double* v = doc_->parts[p].v;
        if (v[kPMute] >= 0.5) return false;
        const int want = static_cast<int>(v[kPChannel]);
        return v[kPMpe] >= 0.5 || want == 0 || want == (channel & 15) + 1;
    }

    // ---------------------------------------------------------------- notes

    void partNoteOn(int p, int channel, int key, int velocity) {
        const Part& part = doc_->parts[p];
        PartState& ps = part_[p];
        ps.down[key & 127] = static_cast<unsigned char>(velocity);
        const int mode = static_cast<int>(part.v[kPMode]);
        if (mode != 0) {
            for (int i = 0; i < ps.stackCount; ++i)
                if (ps.stack[i] == key) { std::memmove(&ps.stack[i], &ps.stack[i + 1], sizeof(int) * static_cast<size_t>(ps.stackCount - i - 1)); --ps.stackCount; break; }
            if (ps.stackCount == 16) { std::memmove(&ps.stack[0], &ps.stack[1], sizeof(int) * 15); --ps.stackCount; }
            ps.stack[ps.stackCount++] = key;
            bool sounding = false;
            for (Voice& v : voices_)
                if (v.active && v.part == p && !v.released) sounding = true;
            if (mode == 2 && sounding) { // legato: the note that sounds moves to the new key
                moveVoices(p, key);
                ps.lastKey = key;
                return;
            }
            for (Voice& v : voices_)
                if (v.active && v.part == p) v.killed = true;
        }
        const int pitchKey = key + static_cast<int>(part.v[kPTranspose]);
        const float glideFrom = ps.lastKey >= 0 && part.v[kPGlide] > 1.5 ? static_cast<float>(ps.lastKey - key) : 0.0f;
        ps.lastKey = key;
        const int turn = ps.robin++;
        bool any = false;
        for (size_t i = 0; i < part.zones.size(); ++i) {
            const Zone& z = part.zones[i];
            if (z.v[kZTrigger] >= 0.5 || !covers(z, pitchKey, velocity, turn)) continue;
            startVoice(p, channel, key, pitchKey, velocity, static_cast<int>(i), !any, glideFrom);
            any = true;
        }
        if (!any && (part.v[kPOscLevel] > 0.0 || part.v[kPNoiseLevel] > 0.0)) startVoice(p, channel, key, pitchKey, velocity, -1, true, glideFrom);
    }

    static bool covers(const Zone& z, int key, int velocity, int turn) {
        if (key < z.v[kZLoKey] || key > z.v[kZHiKey] || velocity < z.v[kZLoVel] || velocity > z.v[kZHiVel]) return false;
        const int length = static_cast<int>(z.v[kZSeqLength]);
        return length <= 1 || turn % length == static_cast<int>(z.v[kZSeqPosition]) - 1;
    }

    void partNoteOff(int p, int channel, int key, int velocity) {
        const Part& part = doc_->parts[p];
        PartState& ps = part_[p];
        (void)velocity;
        if (static_cast<int>(part.v[kPMode]) != 0) {
            bool wasTop = ps.stackCount > 0 && ps.stack[ps.stackCount - 1] == key;
            for (int i = 0; i < ps.stackCount; ++i)
                if (ps.stack[i] == key) { std::memmove(&ps.stack[i], &ps.stack[i + 1], sizeof(int) * static_cast<size_t>(ps.stackCount - i - 1)); --ps.stackCount; break; }
            if (ps.stackCount > 0) { // another key is still down: the note goes back to it
                if (wasTop) { moveVoices(p, ps.stack[ps.stackCount - 1]); ps.lastKey = ps.stack[ps.stackCount - 1]; }
                return;
            }
        }
        const int heldVelocity = ps.down[key & 127];
        ps.down[key & 127] = 0;
        for (Voice& v : voices_) {
            if (!v.active || v.part != p || v.released || v.pedalled) continue;
            if (static_cast<int>(part.v[kPMode]) == 0 && (v.key != key || v.channel != (channel & 15))) continue;
            if (chan_[channel & 15].sustain) v.pedalled = true;
            else releaseVoice(v);
        }
        if (heldVelocity == 0 || chan_[channel & 15].sustain) return;
        // Zones that sound when a key is let go.
        const int pitchKey = key + static_cast<int>(part.v[kPTranspose]);
        for (size_t i = 0; i < part.zones.size(); ++i) {
            const Zone& z = part.zones[i];
            if (z.v[kZTrigger] < 0.5 || !covers(z, pitchKey, heldVelocity, 0)) continue;
            Voice* v = startVoice(p, channel, key, pitchKey, heldVelocity, static_cast<int>(i), false, 0.0f);
            if (v != nullptr) v->released = true; // no key holds it: it plays out by itself
        }
    }

    /** A one-shot zone plays to its end whatever the key does; `force` ends it all the same. */
    void releaseVoice(Voice& v, bool force = false) {
        if (!force && v.z[kZOneShot] >= 0.5 && v.data != nullptr) return;
        v.released = true;
        for (Envelope& e : v.env) e.release();
    }

    void moveVoices(int p, int key) {
        const Part& part = doc_->parts[p];
        for (Voice& v : voices_) {
            if (!v.active || v.part != p || v.released) continue;
            const int pitchKey = key + static_cast<int>(part.v[kPTranspose]);
            v.glide += static_cast<float>(v.pitchKey - pitchKey);
            v.key = key;
            v.pitchKey = pitchKey;
        }
    }

    Voice& freeVoice(int p, int cap) {
        int inPart = 0;
        Voice* empty = nullptr;
        Voice* oldestInPart = nullptr;
        Voice* best = &voices_[0];
        for (Voice& v : voices_) {
            if (!v.active) { if (empty == nullptr) empty = &v; continue; }
            if (v.part == p) {
                ++inPart;
                if (oldestInPart == nullptr || score(v) < score(*oldestInPart)) oldestInPart = &v;
            }
            if (score(v) < score(*best) || !best->active) best = &v;
        }
        if (inPart >= cap && oldestInPart != nullptr) return *oldestInPart;
        return empty != nullptr ? *empty : *best;
    }
    /** The voice with the lowest score makes way: released ones first, then the quietest, then the oldest. */
    static double score(const Voice& v) { return (v.released ? 0.0 : 4.0) + v.gain + 1e-9 * v.order; }

    Voice* startVoice(int p, int channel, int key, int pitchKey, int velocity, int zoneIndex, bool withOsc, float glideFrom) {
        const Part& part = doc_->parts[p];
        const Zone* zone = zoneIndex >= 0 ? &part.zones[static_cast<size_t>(zoneIndex)] : nullptr;
        const AudioData* data = nullptr;
        if (zone != nullptr) {
            const Sample* s = doc_->findSample(zone->sample);
            data = s != nullptr ? s->data.get() : nullptr;
            if (data != nullptr && (data->frames() < 2 || data->sampleRate <= 0.0)) data = nullptr;
            if (data == nullptr && !withOsc) return nullptr; // its sound is not there (yet)
            if (zone->v[kZGroup] > 0.0) // this zone cuts off the ones that listen to its group
                for (Voice& v : voices_)
                    if (v.active && v.part == p && v.z[kZOffBy] == zone->v[kZGroup] && v.zoneId != zone->id) v.killed = true;
        }
        Voice& v = freeVoice(p, static_cast<int>(part.v[kPVoices]));
        v = Voice();
        v.active = true;
        v.part = p;
        v.channel = channel & 15;
        v.key = key;
        v.pitchKey = pitchKey;
        v.velocity = velocity;
        v.zoneIndex = zoneIndex;
        v.zoneId = zone != nullptr ? zone->id : 0;
        v.withOsc = withOsc;
        v.order = order_++;
        v.seed = rng_.next();
        v.noise.s = v.seed | 1u;
        v.random = hashBipolar(v.seed, 17u);
        v.doc = doc_;
        v.data = data;
        v.glide = glideFrom;
        const Zone blank;
        std::memcpy(v.z, zone != nullptr ? zone->v : blank.v, sizeof(v.z));
        for (int i = 0; i < kLfos; ++i) { v.lfoPhase[i] = part.v[lfoParam(i, kLfoPhase)] / 360.0; v.lfoCycle[i] = 0; v.lfoValue[i] = 0.0f; }
        for (Envelope& e : v.env) e.start();
        v.dcIn[0] = v.dcIn[1] = v.dcOut[0] = v.dcOut[1] = 0.0f;
        v.pink[0] = v.pink[1] = v.pink[2] = 0.0f;

        // Unison and chord: every copy is a player with its own pitch and place between left and right.
        int notes = 1;
        float chord[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        for (int c = 0; c < 3; ++c)
            if (part.v[kPChord1 + c] != 0.0) chord[notes++] = static_cast<float>(part.v[kPChord1 + c]);
        int unison = static_cast<int>(part.v[kPUnison]);
        while (unison * notes > kPlayers) --unison;
        v.playerCount = unison * notes;
        v.unison = unison;
        for (int n = 0; n < notes; ++n)
            for (int u = 0; u < unison; ++u) {
                Player& pl = v.players[n * unison + u];
                pl = Player();
                pl.semis = chord[n];
                pl.oscPhase = unison > 1 || n > 0 ? 0.5 + 0.5 * hashBipolar(v.seed, static_cast<uint32_t>(40 + n * unison + u)) : 0.0;
            }
        if (data != nullptr) {
            const long long frames = static_cast<long long>(data->frames());
            long long s = static_cast<long long>(v.z[kZStart]), e = static_cast<long long>(v.z[kZEnd]);
            if (e <= 0 || e > frames) e = frames;
            if (s > e - 2) s = std::max(0LL, e - 2);
            v.step = v.z[kZReverse] >= 0.5 ? -1 : 1;
            v.first = v.step > 0 ? s : e - 1;
            v.length = e - s;
            v.rootPeriod = data->sampleRate / (440.0 * std::exp2((v.z[kZRoot] - 69.0) / 12.0));
        }
        v.countdown = 0;
        return &v;
    }

    // -------------------------------------------------------------- control

    /** Works out a voice's settings for the next ctl_ samples. `at` is the moment, in samples since the engine started. */
    void control(Voice& v, long long at) {
        const Part& part = doc_->parts[v.part];
        const double* P = part.v;
        const double ms = 1000.0 * ctl_ / fs_;
        if (v.zoneIndex >= 0 && static_cast<size_t>(v.zoneIndex) < part.zones.size() && part.zones[static_cast<size_t>(v.zoneIndex)].id == v.zoneId && v.doc != nullptr)
            std::memcpy(v.z, part.zones[static_cast<size_t>(v.zoneIndex)].v, sizeof(v.z)); // follows the zone while it is being edited
        const Chan& ch = chan_[v.channel];
        const bool mpe = P[kPMpe] >= 0.5;
        const Chan& common = mpe ? chan_[0] : ch;

        if (v.killed) { // makes way within one stretch, without a click
            if (v.gain <= 1e-6f || v.first_) { v.active = false; return; } // also one that was cut off before it made a sound
            v.gainStep = -v.gain / ctl_;
            v.panLStep = v.panRStep = v.sampleStep = v.oscStep = v.noiseStep = 0.0f;
            return;
        }

        float src[kSourceCount];
        src[kSrcNone] = 0.0f;
        src[kSrcVelocity] = v.velocity / 127.0f;
        src[kSrcKey] = (v.pitchKey - 60) / 60.0f;
        src[kSrcRandom] = v.random;
        for (int e = 0; e < kEnvs; ++e) src[kSrcEnv1 + e] = static_cast<float>(v.env[e].tick(&P[envParam(e, 0)], ms));
        if (v.env[0].idle()) { v.active = false; return; }
        v.age += ms;

        // First what moves the matrix's own rows and the LFO rates, from the values of last time.
        float mod[kDestCount];
        for (float& m : mod) m = 0.0f;
        src[kSrcLfo1] = v.lfoValue[0]; src[kSrcLfo2] = v.lfoValue[1]; src[kSrcLfo3] = v.lfoValue[2];
        src[kSrcSteps] = v.stepValue;
        src[kSrcWheel] = common.wheel;
        src[kSrcPressure] = std::max(ch.pressure, v.polyPressure);
        src[kSrcBend] = ch.bend;
        src[kSrcTimbre] = ch.timbre;
        for (int m = 0; m < kMacros; ++m) src[kSrcMacro1 + m] = macro_[m];
        for (int pass = 0; pass < 2; ++pass) {
            if (pass == 1) {
                for (int l = 0; l < kLfos; ++l) {
                    const double* L = &P[lfoParam(l, 0)];
                    const int sync = static_cast<int>(L[kLfoSync]);
                    double rate = sync > 0 ? bpm_ / (60.0 * syncBeats(sync)) : L[kLfoRate];
                    double phase;
                    uint32_t cycle;
                    if (L[kLfoRetrig] < 0.5) { // shared by all notes of the part
                        const PartState& ps = part_[v.part];
                        const double turns = ps.lfoPhase[l] + ps.lfoRate[l] * static_cast<double>(at - ps.lfoTime) / fs_ + L[kLfoPhase] / 360.0;
                        const double whole = std::floor(turns);
                        phase = turns - whole;
                        cycle = static_cast<uint32_t>(static_cast<long long>(whole));
                    } else {
                        rate *= std::exp2(4.0 * mod[kDstLfo1Rate + l]);
                        v.lfoPhase[l] += rate * ctl_ / fs_;
                        while (v.lfoPhase[l] >= 1.0) { v.lfoPhase[l] -= 1.0; ++v.lfoCycle[l]; }
                        phase = v.lfoPhase[l];
                        cycle = v.lfoCycle[l];
                    }
                    const double fade = L[kLfoFade] > 0.0 ? std::min(1.0, v.age / L[kLfoFade]) : 1.0;
                    v.lfoValue[l] = static_cast<float>(fade) * lfoShape(static_cast<int>(L[kLfoWave]), phase, cycle, L[kLfoRetrig] < 0.5 ? static_cast<uint32_t>(101 + l) : v.seed + static_cast<uint32_t>(l));
                    src[kSrcLfo1 + l] = v.lfoValue[l];
                }
                // The row of steps: one after the other, as long as the note lasts.
                const double stepMs = 60000.0 * syncBeats(static_cast<int>(P[kPSeqSync])) / bpm_;
                const int steps = static_cast<int>(P[kPSeqSteps]);
                const double place = v.age / std::max(stepMs, 1.0);
                const int index = static_cast<int>(std::fmod(place, static_cast<double>(steps)));
                const float want = static_cast<float>(0.01 * P[kPSeqValue + std::clamp(index, 0, kSteps - 1)]);
                const float lag = static_cast<float>(std::exp(-ms / (1.0 + 0.01 * P[kPSeqSmooth] * stepMs * 0.5)));
                v.stepValue = P[kPSeqSmooth] > 0.0 && !v.first_ ? want + lag * (v.stepValue - want) : want;
                src[kSrcSteps] = v.stepValue;
                const float keep[5] = {mod[kDstSlot1], mod[kDstSlot2], mod[kDstSlot3], mod[kDstSlot4], 0.0f};
                for (float& m : mod) m = 0.0f;
                for (int i = 0; i < 4; ++i) mod[kDstSlot1 + i] = keep[i];
            }
            for (int s = 0; s < kSlots; ++s) {
                const double* S = &P[slotParam(s, 0)];
                const int source = static_cast<int>(S[kSlotSource]), dest = static_cast<int>(S[kSlotDest]), via = static_cast<int>(S[kSlotVia]);
                if (source == kSrcNone || dest == kDstNone) continue;
                const bool early = dest >= kDstLfo1Rate; // rows and LFO rates
                if (pass == 0 && !early) continue;
                if (pass == 1 && dest >= kDstSlot1) continue;
                float amount = static_cast<float>(0.01 * S[kSlotAmount]);
                if (pass == 1 && s < 4) amount = std::clamp(amount + mod[kDstSlot1 + s], -1.0f, 1.0f);
                mod[dest] += amount * src[source] * (via == kSrcNone ? 1.0f : src[via]);
            }
        }

        // Pitch.
        v.glide *= static_cast<float>(std::exp(-ms / std::max(0.2 * P[kPGlide], 0.05)));
        if (std::fabs(v.glide) < 0.001f) v.glide = 0.0f;
        const double bend = P[kPBend] * common.bend + (mpe && v.channel != 0 ? 48.0 * ch.bend : 0.0);
        const int tuneKey = std::clamp(v.pitchKey, 0, 127);
        const double shared = 0.01 * P[kPFine] + 0.01 * doc_->tuning[tuneKey] + bend + v.glide + 24.0 * mod[kDstPitch];
        const double detune = std::max(0.0, P[kPDetune] + 100.0 * mod[kDstDetune]), spread = 0.01 * P[kPSpread];
        const double samplePitch = (v.pitchKey - v.z[kZRoot]) * 0.01 * v.z[kZKeyTrack] + 0.01 * v.z[kZTune];
        const double sampleRatio = v.data != nullptr ? v.data->sampleRate / fs_ : 1.0;
        const double oscHz = 440.0 * std::exp2((v.pitchKey - 69.0 + P[kPOscCoarse] + 0.01 * P[kPOscFine] + shared) / 12.0);
        const int unison = v.unison;
        double fastest = 0.0;
        for (int i = 0; i < v.playerCount; ++i) {
            Player& pl = v.players[i];
            const int u = i % std::max(1, unison);
            const double place = unison > 1 ? 2.0 * u / (unison - 1) - 1.0 : 0.0; // -1..1 across the copies
            const double semis = shared + pl.semis + 0.01 * detune * place;
            pl.inc = sampleRatio * std::exp2((samplePitch + semis) / 12.0);
            pl.oscInc = std::min(0.45, oscHz * std::exp2((pl.semis + 0.01 * detune * place) / 12.0) / fs_);
            panGains(static_cast<float>(spread * place), pl.panL, pl.panR);
            fastest = std::max(fastest, pl.inc);
        }
        v.kernel = &Kernels::get().forRatio(fastest);

        // Where the sound is read and where it loops.
        if (v.data != nullptr) {
            const long long frames = static_cast<long long>(v.data->frames());
            long long s = static_cast<long long>(v.z[kZStart]), e = static_cast<long long>(v.z[kZEnd]);
            if (e <= 0 || e > frames) e = frames;
            if (s > e - 2) s = std::max(0LL, e - 2);
            long long ls = static_cast<long long>(v.z[kZLoopStart]), le = static_cast<long long>(v.z[kZLoopEnd]);
            if (le <= 0 || le > e) le = e;
            ls = std::clamp(ls, s, std::max(s, le - 2));
            double lo = v.step > 0 ? static_cast<double>(ls - s) : static_cast<double>(e - le);
            double hi = v.step > 0 ? static_cast<double>(le - s) : static_cast<double>(e - ls);
            const double whole = hi - lo;
            // The part's Loop start and Loop length, and what moves them, shift and shorten the loop while it plays.
            const double length = std::clamp(whole * 0.01 * P[kPLoopLength] * (1.0 + mod[kDstLoopLength]), std::min(whole, 8.0), whole);
            lo = std::clamp(lo + whole * (0.01 * P[kPLoopStart] + mod[kDstLoopStart]), 0.0, static_cast<double>(v.length) - length);
            v.loopLo = std::floor(lo);
            v.loopHi = v.loopLo + std::floor(length);
            v.loopType = static_cast<int>(v.z[kZLoop]);
            v.looping = v.loopType != 0 && v.loopHi - v.loopLo >= 2.0 && (!v.released || v.z[kZLoopRelease] >= 0.5);
            v.crossfade = v.loopType == 1 ? std::min({v.z[kZCrossfade], v.loopLo, v.loopHi - v.loopLo}) : 0.0;
            if (v.first_) {
                const double offset = std::clamp(0.01 * P[kPStart] + mod[kDstStart], 0.0, 0.999) * static_cast<double>(v.length - 2);
                for (int i = 0; i < v.playerCount; ++i) v.players[i].pos = offset;
            }
        }

        // The modulation oscillator.
        v.modMode = static_cast<int>(P[kPModMode]);
        v.modAmount = std::clamp(static_cast<float>(0.01 * P[kPModAmount]) + mod[kDstModAmount], 0.0f, 1.0f);
        v.modInc = std::min(0.45, oscHz * P[kPModRatio] * std::exp2(2.0 * mod[kDstModRatio] + P[kPModFine] / 1200.0) / fs_);

        // Levels.
        const float sens = static_cast<float>(0.01 * P[kPVelocity]), vel = v.velocity / 127.0f;
        const float touch = 1.0f - sens + sens * vel * vel;
        const float chanGain = ch.volume * ch.volume * ch.expression * ch.expression;
        const float level = std::clamp(1.0f + mod[kDstLevel], 0.0f, 2.0f);
        const float gain = src[kSrcEnv1] * touch * chanGain * level * static_cast<float>(dbToLin(v.z[kZGain]));
        float pl, pr;
        panGains(static_cast<float>(0.01 * v.z[kZPan]) + mod[kDstPan], pl, pr);
        const float size = 1.0f / std::sqrt(static_cast<float>(std::max(1, unison)));
        const float sampleLevel = v.data != nullptr ? size * std::clamp(static_cast<float>(0.01 * P[kPSampleLevel]) + mod[kDstSampleLevel], 0.0f, 1.0f) : 0.0f;
        const float oscLevel = v.withOsc ? 0.4f * size * std::clamp(static_cast<float>(0.01 * P[kPOscLevel]) + mod[kDstOscLevel], 0.0f, 1.0f) : 0.0f;
        const float noiseLevel = v.withOsc ? 0.4f * std::clamp(static_cast<float>(0.01 * P[kPNoiseLevel]) + mod[kDstNoiseLevel], 0.0f, 1.0f) : 0.0f;
        if (v.first_) {
            v.gain = 0.0f; // every note starts from silence, within one stretch
            v.panL = pl; v.panR = pr;
            v.sampleLevel = sampleLevel; v.oscLevel = oscLevel; v.noiseLevel = noiseLevel;
        }
        const float per = 1.0f / ctl_;
        v.gainStep = (gain - v.gain) * per;
        v.panLStep = (pl - v.panL) * per;
        v.panRStep = (pr - v.panR) * per;
        v.sampleStep = (sampleLevel - v.sampleLevel) * per;
        v.oscStep = (oscLevel - v.oscLevel) * per;
        v.noiseStep = (noiseLevel - v.noiseLevel) * per;
        v.oscWave = static_cast<int>(P[kPOscWave]);
        v.width = std::clamp(static_cast<float>(0.01 * P[kPOscWidth]) + 0.45f * mod[kDstWidth], 0.05f, 0.95f);
        v.noiseColour = static_cast<int>(P[kPNoiseColour]);

        // Saturation.
        v.satType = static_cast<int>(P[kPSatType]);
        const double driveDb = std::clamp(P[kPDrive] + 24.0 * mod[kDstDrive], 0.0, 48.0);
        v.drive = static_cast<float>(dbToLin(driveDb));
        v.driveOut = static_cast<float>(dbToLin(-0.5 * driveDb));

        // Filter.
        v.filterType = static_cast<int>(P[kPFilterType]);
        if (v.filterType != 0) {
            const double octaves = 0.01 * P[kPKeyTrack] * (v.pitchKey - 60) / 12.0 + 0.07 * P[kPFilterEnv] * src[kSrcEnv2] +
                                   0.04 * P[kPFilterVel] * (vel - 0.5) + 6.0 * mod[kDstCutoff];
            const double hz = std::clamp(P[kPCutoff] * std::exp2(octaves), 16.0, std::min(20000.0, 0.45 * fs_));
            const double res = std::clamp(0.01 * P[kPResonance] + mod[kDstResonance], 0.0, 1.0);
            const double g = std::tan(kPi * hz / fs_), k = 2.0 - 1.96 * res;
            v.fa1 = static_cast<float>(1.0 / (1.0 + g * (g + k)));
            v.fa2 = static_cast<float>(g) * v.fa1;
            v.fa3 = static_cast<float>(g) * v.fa2;
            v.fk = static_cast<float>(k);
            // Only the first of the two stages of a 24 dB filter resonates: two peaks on top of each other would be 50 dB and more.
            v.fbk = 1.4142135f;
            v.fb1 = static_cast<float>(1.0 / (1.0 + g * (g + 1.4142135623730951)));
            v.fb2 = static_cast<float>(g) * v.fb1;
            v.fb3 = static_cast<float>(g) * v.fb2;
            float l = 0.0f, b = 0.0f, h = 0.0f;
            switch (v.filterType) {
            case 1: case 2: l = 1.0f; break;
            case 3: b = v.fk; break;
            case 4: case 5: h = 1.0f; break;
            case 6: l = 1.0f; h = 1.0f; break;
            case 7: l = 1.0f; h = -1.0f; break;
            default: { // morph: low, band, high, notch
                const float m = 3.0f * std::clamp(static_cast<float>(0.01 * P[kPMorph]) + mod[kDstMorph], 0.0f, 1.0f);
                if (m < 1.0f) { l = 1.0f - m; b = m * v.fk; }
                else if (m < 2.0f) { b = (2.0f - m) * v.fk; h = m - 1.0f; }
                else { h = 1.0f; l = m - 2.0f; }
            }
            }
            v.mixLp = l; v.mixBp = b; v.mixHp = h;
        }
        v.first_ = false;
    }

    // --------------------------------------------------------------- render

    void render(Voice& v, float* outL, float* outR, int n) {
        float mo[kMaxCtl], xl[kMaxCtl], xr[kMaxCtl];
        std::memset(xl, 0, sizeof(float) * static_cast<size_t>(n));
        std::memset(xr, 0, sizeof(float) * static_cast<size_t>(n));
        const bool fm = v.modMode == 1 && v.modAmount > 0.0f;
        if (v.modMode != 0) {
            for (int i = 0; i < n; ++i) {
                mo[i] = static_cast<float>(std::sin(2.0 * kPi * v.modPhase));
                v.modPhase += v.modInc;
                if (v.modPhase >= 1.0) v.modPhase -= 1.0;
            }
        }
        const float index = v.modAmount * v.modAmount * 4.0f / static_cast<float>(2.0 * kPi); // in turns of the wave

        bool sampleLeft = false;
        for (int p = 0; p < v.playerCount; ++p) {
            Player& pl = v.players[p];
            if (v.data != nullptr && !pl.done) {
                sampleLeft = true;
                const bool stereo = v.data->channels > 1 && v.data->samples[1].size() == v.data->samples[0].size();
                const float* dl = v.data->samples[0].data();
                const float* dr = stereo ? v.data->samples[1].data() : dl;
                const Kernel& k = *v.kernel;
                const double reach = fm ? index * v.rootPeriod : 0.0;
                float level = v.sampleLevel;
                for (int i = 0; i < n; ++i, level += v.sampleStep) {
                    ReadWindow w;
                    w.lo = 0;
                    w.hi = v.length;
                    if (v.looping) {
                        w.hi = static_cast<long long>(v.loopHi);
                        // Ping-pong turns round at the end; backward does so once and from then on jumps from start to end.
                        w.above = v.loopType == 1 || (v.loopType == 3 && pl.inLoop) ? ReadWindow::kWrap : ReadWindow::kMirror;
                        // With a crossfade the sound just before the loop is what was last heard, so that is what lies below its start.
                        if (pl.inLoop && pl.pos >= v.loopLo && v.crossfade < 1.0) {
                            w.lo = static_cast<long long>(v.loopLo);
                            w.below = v.loopType == 2 ? ReadWindow::kMirror : ReadWindow::kWrap;
                        }
                    }
                    const double at = fm ? pl.pos + reach * mo[i] : pl.pos;
                    float sl = readAt(dl, v.first, v.step, w, at, k), sr = stereo ? readAt(dr, v.first, v.step, w, at, k) : sl;
                    if (v.looping && v.crossfade >= 1.0 && pl.pos > v.loopHi - v.crossfade) {
                        // Towards the end of the loop the sound from before its start fades in, so that the jump back is not heard.
                        ReadWindow plain;
                        plain.hi = v.length;
                        const float a = static_cast<float>((v.loopHi - pl.pos) / v.crossfade);
                        const double before = at - (v.loopHi - v.loopLo);
                        const float bl = readAt(dl, v.first, v.step, plain, before, k);
                        sl = a * sl + (1.0f - a) * bl;
                        sr = stereo ? a * sr + (1.0f - a) * readAt(dr, v.first, v.step, plain, before, k) : sl;
                    }
                    xl[i] += level * pl.panL * sl;
                    xr[i] += level * pl.panR * sr;
                    pl.pos += pl.dir * pl.inc;
                    if (v.looping) {
                        const double lo = v.loopLo, hi = v.loopHi, span = hi - lo;
                        if (pl.dir > 0 && pl.pos >= hi) {
                            pl.inLoop = true;
                            if (v.loopType == 1) { pl.pos -= span; if (pl.pos >= hi) pl.pos = lo; }
                            else { pl.pos = 2.0 * hi - pl.pos - 1e-9; pl.dir = -1; if (pl.pos < lo) pl.pos = lo; }
                        } else if (pl.dir < 0 && pl.pos < lo) {
                            if (v.loopType == 2) { pl.pos = 2.0 * lo - pl.pos; pl.dir = 1; if (pl.pos >= hi) pl.pos = lo; }
                            else { pl.pos += span; if (pl.pos < lo) pl.pos = hi - 1.0; }
                        }
                    } else {
                        if (pl.dir < 0) pl.dir = 1; // the loop was switched off on the way back
                        if (pl.pos >= static_cast<double>(v.length)) { pl.done = true; break; }
                    }
                }
            }
            if (v.oscLevel > 0.0f || v.oscStep > 0.0f) {
                float level = v.oscLevel;
                const double dt = pl.oscInc;
                for (int i = 0; i < n; ++i, level += v.oscStep) {
                    double t = pl.oscPhase + (fm ? index * mo[i] : 0.0f);
                    t -= std::floor(t);
                    float s;
                    switch (v.oscWave) {
                    case 0: s = static_cast<float>(std::sin(2.0 * kPi * t)); break;
                    case 1: s = static_cast<float>(4.0 * std::fabs(t - 0.5) - 1.0); break;
                    case 2: s = static_cast<float>(2.0 * t - 1.0) - polyBlep(t, dt); break;
                    default: {
                        const double w = v.oscWave == 3 ? 0.5 : v.width;
                        double t2 = t - w;
                        if (t2 < 0.0) t2 += 1.0;
                        s = (t < w ? 1.0f : -1.0f) + polyBlep(t, dt) - polyBlep(t2, dt);
                    }
                    }
                    xl[i] += level * pl.panL * s;
                    xr[i] += level * pl.panR * s;
                    pl.oscPhase += dt;
                    if (pl.oscPhase >= 1.0) pl.oscPhase -= 1.0;
                }
            }
        }
        v.sampleLevel += v.sampleStep * n;
        v.oscLevel += v.oscStep * n;
        // A zone whose sound has run out, with no oscillator going, is finished.
        if (v.data != nullptr && !sampleLeft && !v.withOsc) v.env[0].stage = Envelope::kIdle;
        if (v.data != nullptr && !sampleLeft && v.withOsc && v.oscLevel <= 0.0f && v.noiseLevel <= 0.0f && v.oscStep <= 0.0f && v.noiseStep <= 0.0f)
            v.env[0].stage = Envelope::kIdle;

        if (v.noiseLevel > 0.0f || v.noiseStep > 0.0f) {
            float level = v.noiseLevel;
            for (int i = 0; i < n; ++i, level += v.noiseStep) {
                float s = v.noise.bipolar();
                if (v.noiseColour == 1) { // pink: three lowpasses of white noise added up
                    v.pink[0] = 0.99765f * v.pink[0] + s * 0.0990460f;
                    v.pink[1] = 0.96300f * v.pink[1] + s * 0.2965164f;
                    v.pink[2] = 0.57000f * v.pink[2] + s * 1.0526913f;
                    s = 0.25f * (v.pink[0] + v.pink[1] + v.pink[2] + s * 0.1848f);
                }
                xl[i] += level * s;
                xr[i] += level * s;
            }
        }
        v.noiseLevel += v.noiseStep * n;

        if (v.modMode >= 2 && v.modAmount > 0.0f) { // AM keeps the sound's own level, ring replaces it by the product
            const float a = v.modAmount;
            for (int i = 0; i < n; ++i) {
                const float m = v.modMode == 2 ? 1.0f - a + a * (0.5f + 0.5f * mo[i]) : 1.0f - a + a * mo[i];
                xl[i] *= m;
                xr[i] *= m;
            }
        }
        if (v.satType != 0) {
            for (int i = 0; i < n; ++i) {
                xl[i] = v.driveOut * shape(v.satType, v.drive * xl[i]);
                xr[i] = v.driveOut * shape(v.satType, v.drive * xr[i]);
            }
            if (v.satType == 4) { // the tube shape leaves a steady offset: taken out again
                const float r = static_cast<float>(1.0 - 2.0 * kPi * 12.0 / fs_);
                for (int i = 0; i < n; ++i) {
                    const float yl = xl[i] - v.dcIn[0] + r * v.dcOut[0], yr = xr[i] - v.dcIn[1] + r * v.dcOut[1];
                    v.dcIn[0] = xl[i]; v.dcIn[1] = xr[i];
                    xl[i] = v.dcOut[0] = yl;
                    xr[i] = v.dcOut[1] = yr;
                }
            }
        }
        if (v.filterType != 0) {
            const bool steep = v.filterType == 2 || v.filterType == 5;
            for (int c = 0; c < 2; ++c) {
                float* x = c == 0 ? xl : xr;
                for (int i = 0; i < n; ++i) {
                    float lp, bp, hp;
                    v.filter[c][0].process(x[i], v.fa1, v.fa2, v.fa3, v.fk, lp, bp, hp);
                    float y = v.mixLp * lp + v.mixBp * bp + v.mixHp * hp;
                    if (steep) {
                        v.filter[c][1].process(y, v.fb1, v.fb2, v.fb3, v.fbk, lp, bp, hp);
                        y = v.mixLp * lp + v.mixHp * hp;
                    }
                    x[i] = y;
                }
            }
        }
        float g = v.gain, pl = v.panL, pr = v.panR;
        for (int i = 0; i < n; ++i) {
            outL[i] += g * pl * xl[i];
            outR[i] += g * pr * xr[i];
            g += v.gainStep;
            pl += v.panLStep;
            pr += v.panRStep;
        }
        v.gain = g < 0.0f ? 0.0f : g;
        v.panL = pl;
        v.panR = pr;
    }

    void chunk(float* const* o, int n) {
        float bus[kParts][2][kChunk];
        bool used[kParts] = {};
        if (doc_ != nullptr) {
            for (int p = 0; p < kParts; ++p) { // the LFOs the notes of a part share run on, whether or not a note sounds
                PartState& ps = part_[p];
                for (int l = 0; l < kLfos; ++l) {
                    const double* L = &doc_->parts[p].v[lfoParam(l, 0)];
                    const int sync = static_cast<int>(L[kLfoSync]);
                    ps.lfoPhase[l] += ps.lfoRate[l] * static_cast<double>(now_ - ps.lfoTime) / fs_;
                    ps.lfoRate[l] = sync > 0 ? bpm_ / (60.0 * syncBeats(sync)) : L[kLfoRate];
                }
                ps.lfoTime = now_;
            }
            for (Voice& v : voices_) {
                if (!v.active) continue;
                if (!used[v.part]) {
                    std::memset(bus[v.part], 0, sizeof(bus[v.part]));
                    used[v.part] = true;
                }
                for (int done = 0; done < n && v.active;) {
                    if (v.countdown == 0) {
                        control(v, now_ + done);
                        v.countdown = ctl_;
                        if (!v.active) break;
                    }
                    const int m = std::min(n - done, v.countdown);
                    render(v, bus[v.part][0] + done, bus[v.part][1] + done, m);
                    v.countdown -= m;
                    done += m;
                }
            }
        }

        float fx[3][2][kChunk]; // what goes to the delay, the reverb and the chorus
        std::memset(fx, 0, sizeof(fx));
        bool anyFx = false;
        for (int p = 0; p < kParts; ++p) {
            PartState& ps = part_[p];
            if (doc_ == nullptr) continue;
            const double* P = doc_->parts[p].v;
            float tl, tr;
            panGains(static_cast<float>(0.01 * P[kPPan]), tl, tr);
            const float level = P[kPMute] >= 0.5 ? 0.0f : static_cast<float>(dbToLin(P[kPLevel]));
            tl *= level;
            tr *= level;
            const float sd = static_cast<float>(0.01 * P[kPDelaySend]), sr = static_cast<float>(0.01 * P[kPReverbSend]), sc = static_cast<float>(0.01 * P[kPChorusSend]);
            if (!used[p] || ps.fresh) { // nothing sounded: the smoothed settings jump to where they should be
                ps.gainL = tl; ps.gainR = tr; ps.sendDelay = sd; ps.sendReverb = sr; ps.sendChorus = sc;
                ps.fresh = false;
                if (!used[p]) { ps.peak = 0.0f; continue; }
            }
            const int out = std::clamp(static_cast<int>(P[kPOutput]), 0, kOutputs - 1);
            float* ol = o[2 * out];
            float* orr = o[2 * out + 1];
            float peak = 0.0f;
            for (int i = 0; i < n; ++i) {
                ps.gainL += smooth_ * (tl - ps.gainL);
                ps.gainR += smooth_ * (tr - ps.gainR);
                ps.sendDelay += smooth_ * (sd - ps.sendDelay);
                ps.sendReverb += smooth_ * (sr - ps.sendReverb);
                ps.sendChorus += smooth_ * (sc - ps.sendChorus);
                const float l = ps.gainL * bus[p][0][i], r = ps.gainR * bus[p][1][i];
                ol[i] += l;
                orr[i] += r;
                fx[0][0][i] += ps.sendDelay * l; fx[0][1][i] += ps.sendDelay * r;
                fx[1][0][i] += ps.sendReverb * l; fx[1][1][i] += ps.sendReverb * r;
                fx[2][0][i] += ps.sendChorus * l; fx[2][1][i] += ps.sendChorus * r;
                peak = std::max(peak, std::max(std::fabs(l), std::fabs(r)));
            }
            ps.peak = std::max(ps.peak, peak);
            anyFx = anyFx || ps.sendDelay + ps.sendReverb + ps.sendChorus > 0.0f;
        }

        if (preview_.data != nullptr) renderPreview(o[0], o[1], n);

        // The effects run on while they still ring, and rest when nothing has gone in for a while.
        if (anyFx) fxIdle_ = 0;
        if (doc_ != nullptr && fxIdle_ < fxTail()) {
            setFx();
            const double* G = doc_->g;
            const float dl = static_cast<float>(dbToLin(G[kGDelayLevel])), rl = static_cast<float>(dbToLin(G[kGReverbLevel])), cl = static_cast<float>(dbToLin(G[kGChorusLevel]));
            for (int i = 0; i < n; ++i) {
                float wl, wr;
                delay_.process(fx[0][0][i], fx[0][1][i], wl, wr);
                o[0][i] += dl * wl;
                o[1][i] += dl * wr;
                double rwl, rwr;
                reverb_.process(fx[1][0][i], fx[1][1][i], rwl, rwr);
                o[0][i] += rl * static_cast<float>(rwl);
                o[1][i] += rl * static_cast<float>(rwr);
                chorus_.process(fx[2][0][i], fx[2][1][i], wl, wr);
                o[0][i] += cl * wl;
                o[1][i] += cl * wr;
            }
            fxIdle_ += n;
            if (fxIdle_ >= fxTail()) { delay_.reset(); reverb_.reset(); chorus_.reset(); }
        }

        for (int i = 0; i < n; ++i) {
            master_ += smooth_ * (masterTarget_ - master_);
            for (int c = 0; c < 2 * kOutputs; ++c) o[c][i] *= master_;
        }
        for (int c = 0; c < 2; ++c) {
            float peak = 0.0f;
            for (int i = 0; i < n; ++i) peak = std::max(peak, std::fabs(o[c][i]));
            if (peak > meters.outPeak[c].load(std::memory_order_relaxed)) meters.outPeak[c].store(peak, std::memory_order_relaxed);
        }
    }

    long long fxTail() const { return static_cast<long long>(30.0 * fs_); }

    void setFx() {
        const double* G = doc_->g;
        const int key = static_cast<int>(bpm_ * 100.0);
        if (fxKey_ == key && fxDoc_ == doc_) return;
        fxKey_ = key;
        fxDoc_ = doc_;
        const int sync = static_cast<int>(G[kGDelaySync]);
        const double seconds = sync > 0 ? 60.0 * syncBeats(sync) / bpm_ : 0.001 * G[kGDelayTime];
        delay_.set(seconds, 0.01 * G[kGDelayFeedback], G[kGDelayTone], 0.01 * G[kGDelayWidth]);
        reverb_.setDecay(G[kGReverbDecay]);
        reverb_.setDamping(G[kGReverbDamp]);
        chorus_.set(G[kGChorusRate], 0.01 * G[kGChorusDepth]);
    }

    void renderPreview(float* outL, float* outR, int n) {
        const AudioData& d = *preview_.data;
        const long long frames = static_cast<long long>(d.frames());
        const bool stereo = d.channels > 1 && d.samples[1].size() == d.samples[0].size();
        const double inc = d.sampleRate > 0.0 ? d.sampleRate / fs_ : 1.0;
        const Kernel& k = Kernels::get().forRatio(inc);
        ReadWindow w;
        w.hi = frames;
        const float fade = static_cast<float>(1.0 / (0.005 * fs_));
        for (int i = 0; i < n; ++i) {
            if (preview_.pos >= static_cast<double>(frames) || preview_.gain <= 0.0f) {
                previewReleased_ = preview_.data;
                preview_.data = nullptr;
                return;
            }
            const float l = readAt(d.samples[0].data(), 0, 1, w, preview_.pos, k);
            const float r = stereo ? readAt(d.samples[1].data(), 0, 1, w, preview_.pos, k) : l;
            outL[i] += 0.7f * preview_.gain * l;
            outR[i] += 0.7f * preview_.gain * r;
            preview_.pos += inc;
            if (preview_.stopping) preview_.gain -= fade;
        }
    }

    void updateMeters() {
        uint32_t keys[4] = {0u, 0u, 0u, 0u};
        int count = 0;
        const Voice* newest = nullptr;
        for (const Voice& v : voices_) {
            if (!v.active) continue;
            ++count;
            if (!v.released) keys[(v.key >> 5) & 3] |= 1u << (v.key & 31);
            if (v.data != nullptr && (newest == nullptr || v.order > newest->order)) newest = &v;
        }
        meters.voices.store(count, std::memory_order_relaxed);
        for (int i = 0; i < 4; ++i) meters.keys[i].store(keys[i], std::memory_order_relaxed);
        meters.zone.store(newest != nullptr ? newest->zoneId : 0, std::memory_order_relaxed);
        if (newest != nullptr && newest->length > 0) {
            const double pos = newest->players[0].pos / static_cast<double>(newest->length);
            meters.position.store(static_cast<float>(newest->step > 0 ? pos : 1.0 - pos), std::memory_order_relaxed);
        }
        for (int p = 0; p < kParts; ++p) {
            if (part_[p].peak > meters.partPeak[p].load(std::memory_order_relaxed)) meters.partPeak[p].store(part_[p].peak, std::memory_order_relaxed);
            part_[p].peak = 0.0f;
        }
    }

    /** Hands back the documents that are neither the one in use nor needed by a sounding voice. */
    void releaseUnused() {
        for (const Instrument*& h : held_) {
            if (h == nullptr || h == doc_) continue;
            bool needed = false;
            for (const Voice& v : voices_) needed = needed || (v.active && v.doc == h);
            if (!needed) { pushReleased(h); h = nullptr; }
        }
    }
    void pushReleased(const Instrument* doc) {
        if (releasedCount_ < kHeld * 2) released_[releasedCount_++] = doc;
    }

    double fs_ = 48000.0, bpm_ = 120.0;
    int ctl_ = 32;
    long long now_ = 0, fxIdle_ = 1LL << 60;
    uint32_t order_ = 0;
    const Instrument* doc_ = nullptr;
    const Instrument* held_[kHeld] = {};
    const Instrument* released_[kHeld * 2] = {};
    int releasedCount_ = 0;
    const Instrument* fxDoc_ = nullptr;
    int fxKey_ = -1;
    Voice voices_[kVoices];
    Chan chan_[16];
    PartState part_[kParts];
    Preview preview_;
    const AudioData* previewReleased_ = nullptr;
    float macro_[kMacros] = {};
    float master_ = 1.0f, masterTarget_ = 1.0f, smooth_ = 0.01f;
    Rng rng_;
    StereoDelay delay_;
    Chorus chorus_;
    Reverb reverb_;
};

} // namespace sampler
} // namespace hnl
