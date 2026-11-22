// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// Five of HNL Patina's six modules: Drive, Vinyl, Noise, Filter and Space. (The sixth, Motion, is
// the platter in Platter.hpp and a delay.) Each one is given its type and how far it is turned up
// on every sample; what React and Morph make of the knobs is worked out by the engine in Patina.hpp.
#pragma once

#include <algorithm>
#include <cmath>
#include <iterator>

#include "../../../common/dsp/Reverb.hpp"
#include "Platter.hpp"

namespace hnl {
namespace patina {

/**
 * Changing a module's type: the module is turned down to nothing in 25 ms, changes, and comes back
 * in 25 ms. At nothing every module passes its input unchanged, so no old sound gets out and the
 * level does not jump. A change that arrives during the fade is simply where the fade ends up.
 */
struct TypeFade {
    int current = 0;
    double gain = 1.0, step = 0.0;

    void prepare(double fs) { step = 1.0 / (0.025 * fs); }
    void reset(int type) { current = type; gain = 1.0; }
    /** One sample on. True on the sample the type changes: the module clears itself then. */
    inline bool process(int target) {
        if (target != current) {
            gain -= step;
            if (gain <= 0.0) { gain = 0.0; current = target; return true; }
        } else if (gain < 1.0) {
            gain = std::min(1.0, gain + step);
        }
        return false;
    }
};

// ------------------------------------------------------------------ drive

/**
 * Four gain stages, four times oversampled. What comes out is as loud as what went in (measured
 * over 50 ms), so Amount changes the sound and not the level. The oversampler is 36 samples late;
 * when Drive is off the sound passes a plain delay of that length.
 */
class Drive {
public:
    enum Type { kTube, kTransistor, kDiode, kFuzz };
    static constexpr int kLatency = 36;

    Drive() {
        for (int c = 0; c < 2; ++c) {
            a_[c].design(63, 8.96);
            b_[c].design(23, 8.96);
            dry_[c].setLength(kLatency);
        }
    }
    void prepare(double fs) {
        fs_ = fs;
        rms_ = onePoleCoef(50.0, fs);
        setTone(0.0);
        reset();
    }
    /** Tone -1..1: tilts the sound before the stage (dark .. bright) and takes it back after. */
    void setTone(double tone) {
        for (int c = 0; c < 2; ++c) {
            pre_[c].setHighShelf(fs_, 1000.0, 6.0 * tone);
            post_[c].setInverseOf(pre_[c]);
        }
    }
    void reset() {
        for (auto& d : dry_) d.reset();
        clear();
    }
    /** Clears everything but the plain delay, which runs whether Drive is on or not. */
    void clear() {
        for (int c = 0; c < 2; ++c) { a_[c].reset(); b_[c].reset(); pre_[c].reset(); post_[c].reset(); }
        in_ = out_ = 0.0;
    }
    /** gain: linear drive into the stage. blend 0..1: how much of the stage is heard. */
    inline void process(double& l, double& r, bool running, int type, double gain, double blend) {
        const double d0 = dry_[0].process(l), d1 = dry_[1].process(r);
        if (!running) { l = d0; r = d1; return; }
        const double w0 = post_[0].process(stage(0, pre_[0].process(l), type, gain));
        const double w1 = post_[1].process(stage(1, pre_[1].process(r), type, gain));
        const double pi = d0 * d0 + d1 * d1, po = w0 * w0 + w1 * w1;
        in_ = pi + rms_ * (in_ - pi);
        out_ = po + rms_ * (out_ - po);
        const double match = std::clamp(std::sqrt((in_ + 1e-9) / (out_ + 1e-9)), 0.05, 4.0);
        l = d0 + blend * (w0 * match - d0);
        r = d1 + blend * (w1 * match - d1);
    }

    /** The curve of a stage, with a slope of one for small signals. */
    static inline double curve(int type, double t) {
        switch (type) {
        case kTransistor: { // soft, and softer on one side than on the other
            if (t >= 0.0) return t / std::sqrt(1.0 + t * t);
            const double u = t / 1.3;
            return 1.3 * u / std::sqrt(1.0 + u * u);
        }
        case kDiode: // straight for a long while, then a sharp knee
            return t / std::pow(1.0 + std::pow(std::fabs(t), 2.5), 0.4);
        case kFuzz: { // a lot of gain into a hard, lopsided clip
            const double u = std::clamp(1.25 * (std::tanh(4.0 * t + 0.4) - 0.379949), -0.8, 0.9);
            return u / 4.27815;
        }
        default: // tube: tanh around a small bias, so even harmonics too (the curve of HNL Master's exciter)
            return (std::tanh(t + 0.25) - 0.244919) / 0.940015;
        }
    }

private:
    inline double stage(int ch, double x, int type, double gain) {
        double u0, u1, v[4];
        a_[ch].up(x, u0, u1);
        b_[ch].up(u0, v[0], v[1]);
        b_[ch].up(u1, v[2], v[3]);
        for (double& w : v) w = curve(type, gain * w);
        const double e0 = b_[ch].down(v[0], v[1]);
        const double e1 = b_[ch].down(v[2], v[3]);
        return a_[ch].down(e0, e1);
    }

    double fs_ = 48000.0, rms_ = 0.0, in_ = 0.0, out_ = 0.0;
    Halfband a_[2], b_[2];
    Delay dry_[2];
    Shelf1 pre_[2], post_[2];
};

// ------------------------------------------------------------------ vinyl

/**
 * The surface of a record: crackle, scratches that come back once a turn, a worn groove, a 78.
 * `own` is the level of what the record adds by itself (React and In Silence are in it); the wear
 * of the sound itself follows Amount alone.
 */
class Vinyl {
public:
    enum Type { kDusty, kScratched, kWorn, kShellac };

    void prepare(double fs) {
        fs_ = fs;
        rate_ = fs / 48000.0;
        for (int c = 0; c < 2; ++c) {
            tick_[c].set(designBandpass(fs, 3500.0, 1.2));
            click_[c].set(designBandpass(fs, 1600.0, 0.7));
            surface_[c].set(designBandpass(fs, 2500.0, 0.5));
            wornHp_[c].set(designHighpass(fs, 3000.0, 0.7071067811865476));
            shellacHp_[c].set(designHighpass(fs, 180.0, 0.7071067811865476));
            shellacLp_[c].set(designLowpass(fs, 4500.0, 0.7071067811865476));
        }
        rumble_.set(designBandpass(fs, 28.0, 2.0));
        setWear(0.2);
        reset();
    }
    /** Wear 0..1: where the highs are lost. */
    void setWear(double wear) {
        const BiquadCoeffs lp = designLowpass(fs_, 9000.0 * std::pow(2.0, -2.0 * wear), 0.7071067811865476);
        for (auto& f : loss_) f.set(lp);
    }
    void reset() {
        clear();
        rng_.seed(0x7a3c91e5u);
        before_ = 0.0;
    }
    void clear() {
        for (int c = 0; c < 2; ++c) {
            tick_[c].reset(); click_[c].reset(); surface_[c].reset(); wornHp_[c].reset();
            shellacHp_[c].reset(); shellacLp_[c].reset(); loss_[c].reset();
        }
        rumble_.reset();
    }
    inline void process(double& l, double& r, int type, double amount, double wear, double own, double angle) {
        // What the record does to the sound itself.
        const double lost = amount * (0.3 + 0.7 * wear);
        l += lost * (loss_[0].process(l) - l);
        r += lost * (loss_[1].process(r) - r);
        if (type == kWorn) { // a played-out groove splashes on bright, loud passages
            const double hl = wornHp_[0].process(l), hr = wornHp_[1].process(r);
            l += amount * 0.5 * std::tanh(3.0 * hl * std::fabs(hl));
            r += amount * 0.5 * std::tanh(3.0 * hr * std::fabs(hr));
        } else if (type == kShellac) { // a 78 has no lows and no highs
            const double band = std::min(1.0, 2.0 * amount);
            l += band * (shellacLp_[0].process(shellacHp_[0].process(l)) - l);
            r += band * (shellacLp_[1].process(shellacHp_[1].process(r)) - r);
        }

        // What the record adds: crackle, more of it on one side of the turn than on the other.
        static const double kDensity[4][2] = {{30.0, 400.0}, {15.0, 150.0}, {20.0, 200.0}, {100.0, 900.0}};
        static const double kSurface[4] = {0.006, 0.004, 0.004, 0.05};
        const double perSecond = (kDensity[type][0] + kDensity[type][1] * amount) * (1.0 + 0.5 * std::sin(2.0 * kPi * angle));
        double el = 0.0, er = 0.0;
        if (rng_.unit() * fs_ < perSecond) {
            const double u = rng_.unit(), amp = (0.15 + 0.85 * u * u * u) * (rng_.unit() < 0.5 ? -4.0 : 4.0) * rate_, pan = rng_.unit();
            el = amp * pan;
            er = amp * (1.0 - pan);
        }
        double cl = 0.0, cr = 0.0;
        if (type == kScratched) { // each scratch clicks as it passes the needle, once a turn
            const int count = scratchesAt(amount);
            for (int i = 0; i < count; ++i) {
                const double at = kScratchAngle[i];
                const bool passed = angle >= before_ ? (at > before_ && at <= angle) : (at > before_ || at <= angle);
                if (passed) { cl += 6.0 * kScratchLevel[i] * rate_; cr += 4.8 * kScratchLevel[i] * rate_; }
            }
        }
        before_ = angle;
        const double hiss = kSurface[type] * std::sqrt(rate_);
        const double sl = tick_[0].process(el + 0.5 * cl) + click_[0].process(cl) + surface_[0].process(hiss * rng_.bipolar() * 8.0);
        const double sr = tick_[1].process(er + 0.5 * cr) + click_[1].process(cr) + surface_[1].process(hiss * rng_.bipolar() * 8.0);
        const double low = rumble_.process(rng_.bipolar()) * 0.6 * std::sqrt(rate_) * wear;
        const double level = own * amount * std::sqrt(amount) * 0.5;
        l += level * (sl + low);
        r += level * (sr + low);
    }

private:
    double fs_ = 48000.0, rate_ = 1.0, before_ = 0.0;
    Biquad tick_[2], click_[2], surface_[2], wornHp_[2], shellacHp_[2], shellacLp_[2], loss_[2], rumble_;
    Rng rng_;
};

// ------------------------------------------------------------------ noise

/** The bed under the music: mains hum, a buzz, static, the air of a room. No tape hiss. */
class Noise {
public:
    enum Type { kHum, kBuzz, kStatic, kRoomTone };

    void prepare(double fs) {
        fs_ = fs;
        burstDecay_ = onePoleCoef(10.0, fs);
        dc_ = onePoleCoef(8.0, fs);    // about 20 Hz
        airCoef_ = onePoleCoef(1000.0, fs);
        setColour(0.4);
        reset();
    }
    /** Colour 0..1, dark to bright. */
    void setColour(double colour) {
        colour_ = colour;
        staticHp_ = std::exp(-2.0 * kPi * std::min(1500.0 + 3000.0 * colour, 0.45 * fs_) / fs_);
        const double fc = std::min(150.0 * std::pow(2.0, 5.0 * colour), 0.45 * fs_);
        roomLp_ = std::exp(-2.0 * kPi * fc / fs_);
        roomGain_ = 0.3 * std::sqrt(fs_ / (kPi * fc));
    }
    void reset() {
        clear();
        rng_.seed(0x1d872b41u);
        phase_ = 0.0;
        airTarget_ = air_ = airClock_ = 0.0;
    }
    void clear() {
        low_ = burst_ = 0.0;
        for (int c = 0; c < 2; ++c) hp_[c] = lp1_[c] = lp2_[c] = 0.0;
    }
    /** Adds the bed at `level` (linear). mains: 0 = 50 Hz, 1 = 60 Hz. */
    inline void process(double& l, double& r, int type, double level, int mains) {
        const double f0 = mains == 1 ? 60.0 : 50.0, top = 0.45 * fs_;
        phase_ += f0 / fs_;
        if (phase_ >= 1.0) phase_ -= 1.0;
        const double a = 2.0 * kPi * phase_;
        double sl = 0.0, sr = 0.0;
        switch (type) {
        case kHum: { // the fundamental, the octave a rectifier adds, and more harmonics as Colour goes up
            double s = std::sin(a);
            if (2 * f0 < top) s += (0.6 + 0.8 * colour_) * std::sin(2.0 * a + 0.7);
            if (3 * f0 < top) s += (0.25 + 0.9 * colour_) * std::sin(3.0 * a + 1.9);
            for (int k = 4; k <= 8 && k * f0 < top; ++k) s += colour_ * 1.2 / (k - 2) * std::sin(k * a + 0.6 * k);
            sl = sr = s * 0.33;
            break;
        }
        case kBuzz: { // a narrow pulse twice a cycle: narrower, so brighter, as Colour goes up
            const double f1 = 2.0 * f0, limit = (top / f1) * (top / f1) / 13.8;
            const double k = std::min(5.0 + 150.0 * colour_ * colour_, limit);
            const double pulse = std::exp(k * (std::cos(2.0 * a) - 1.0));
            low_ = pulse + dc_ * (low_ - pulse);
            sl = sr = 0.8 * (pulse - low_) + 0.25 * std::sin(a);
            break;
        }
        case kStatic: { // short bursts of crackling
            if (rng_.unit() * fs_ < 12.0 * (1.0 + 3.0 * colour_)) burst_ = std::max(burst_, 0.2 + 0.8 * rng_.unit());
            burst_ *= burstDecay_;
            for (int c = 0; c < 2; ++c) {
                const double x = rng_.bipolar() * burst_;
                hp_[c] = x + staticHp_ * (hp_[c] - x);
                (c == 0 ? sl : sr) = 1.2 * (x - hp_[c]);
            }
            break;
        }
        default: { // room tone: soft noise that breathes
            airClock_ += 1.0 / (0.7 * fs_);
            if (airClock_ >= 1.0) { airClock_ -= 1.0; airTarget_ = rng_.bipolar(); }
            air_ = airTarget_ + airCoef_ * (air_ - airTarget_);
            for (int c = 0; c < 2; ++c) {
                const double x = rng_.bipolar();
                lp1_[c] = x + roomLp_ * (lp1_[c] - x);
                lp2_[c] = lp1_[c] + roomLp_ * (lp2_[c] - lp1_[c]);
                (c == 0 ? sl : sr) = roomGain_ * lp2_[c] * (1.0 + 0.4 * air_);
            }
            break;
        }
        }
        l += level * sl;
        r += level * sr;
    }

private:
    double fs_ = 48000.0, colour_ = 0.4, phase_ = 0.0;
    double burstDecay_ = 0, dc_ = 0, staticHp_ = 0, roomLp_ = 0, roomGain_ = 1, airCoef_ = 0;
    double low_ = 0, burst_ = 0, hp_[2] = {}, lp1_[2] = {}, lp2_[2] = {}, airTarget_ = 0, air_ = 0, airClock_ = 0;
    Rng rng_;
};

// ----------------------------------------------------------------- filter

/** Tilt (the whole spectrum darker), Band (narrower from both ends) or Ladder (a resonant low-pass). */
class Filter {
public:
    enum Type { kTilt, kBand, kLadder };

    void prepare(double fs) {
        fs_ = fs;
        set(kTilt, 0.0, 0.0, 0.0);
        reset();
    }
    void reset() {
        for (int c = 0; c < 2; ++c) {
            shelf_[c].reset(); peak_[c].reset(); hp_[c].reset(); lp_[c].reset();
            for (double& s : stage_[c]) s = 0.0;
        }
    }
    /** closed 0..1: how far it is closed. octaves: how far the music and the record move it open (+) or shut. res 0..1. */
    void set(int type, double closed, double octaves, double res) {
        const double move = std::pow(2.0, octaves);
        switch (type) {
        case kTilt: {
            const double fc = 1000.0 * move;
            const BiquadCoeffs peak = designPeak(fs_, 1.5 * fc, 2.0, 9.0 * res * closed);
            for (int c = 0; c < 2; ++c) {
                shelf_[c].setHighShelf(fs_, clampFreq(fs_, fc), -18.0 * closed);
                peak_[c].set(peak);
            }
            break;
        }
        case kBand: {
            const double q = 0.7071067811865476 + 3.3 * res;
            const BiquadCoeffs hp = designHighpass(fs_, 20.0 * std::pow(25.0, closed) / std::sqrt(move), q);
            const BiquadCoeffs lp = designLowpass(fs_, 20000.0 * std::pow(0.125, closed) * move, q);
            for (int c = 0; c < 2; ++c) { hp_[c].set(hp); lp_[c].set(lp); }
            break;
        }
        default: {
            const double fc = std::clamp(20000.0 * std::pow(0.01, closed) * move, 40.0, 0.45 * fs_);
            const double g = std::tan(kPi * fc / fs_);
            g_ = g / (1.0 + g);
            k_ = 3.8 * res;
            break;
        }
        }
    }
    inline void process(double& l, double& r, int type, double blend) {
        double x[2] = {l, r};
        for (int c = 0; c < 2; ++c) {
            double y;
            switch (type) {
            case kTilt: y = peak_[c].process(shelf_[c].process(x[c])); break;
            case kBand: y = lp_[c].process(hp_[c].process(x[c])); break;
            default: { // four one-pole stages in a loop, solved without a delay in the loop
                double* s = stage_[c];
                const double G = g_, G2 = G * G;
                const double sum = (1.0 - G) * (G2 * G * s[0] + G2 * s[1] + G * s[2] + s[3]);
                double u = (x[c] * (1.0 + 0.6 * k_) - k_ * sum) / (1.0 + k_ * G2 * G2);
                for (int i = 0; i < 4; ++i) {
                    const double v = G * u + (1.0 - G) * s[i];
                    s[i] = 2.0 * v - s[i];
                    u = v;
                }
                y = u;
                break;
            }
            }
            x[c] += blend * (y - x[c]);
        }
        l = x[0];
        r = x[1];
    }

private:
    double fs_ = 48000.0, g_ = 0.5, k_ = 0.0;
    Shelf1 shelf_[2];
    Biquad peak_[2], hp_[2], lp_[2];
    double stage_[2][4] = {};
};

// ------------------------------------------------------------------ space

/**
 * Four rooms: a small room, a chamber, three springs, and an early digital reverb that runs at
 * 12 kHz with 12 bits. Returns the reverb only; below 200 Hz it is mono, so the lows stay in the middle.
 */
class Space {
public:
    enum Type { kRoom, kChamber, kSpring, kDigital };

    void prepare(double fs) {
        fs_ = fs;
        room_.prepare(fs);
        chamber_.prepare(fs);
        const int stretch = std::max(1, static_cast<int>(std::lround(fs / 8600.0)));
        const double top = 0.5 * fs / stretch;
        static const double kCoef[3] = {0.62, 0.58, 0.65}, kLoop[3] = {0.0293, 0.0371, 0.0437};
        for (int s = 0; s < 3; ++s) spring_[s].prepare(fs, stretch, kCoef[s], kLoop[s], 2.0);
        springHp_.set(designHighpass(fs, 140.0, 0.7071067811865476));
        for (int c = 0; c < 2; ++c) {
            springLp_[c][0].set(designLowpass(fs, 0.82 * top, 0.54));
            springLp_[c][1].set(designLowpass(fs, 0.82 * top, 1.31));
        }
        every_ = std::max(1, static_cast<int>(std::lround(fs / 12000.0)));
        digital_.prepare(fs / every_);
        digital_.setDamping(3500.0);
        for (auto& f : digitalLp_) f.set(designLowpass(fs, 0.4 * fs / every_, 0.7071067811865476));
        for (auto& f : sideHp_) f.set(designHighpass(fs, 200.0, 0.7071067811865476));
        for (int t = 0; t < 4; ++t) setDecay(t, 0.8);
        reset();
    }
    /** The time the tail takes to fall by 60 dB, in seconds, for one type. */
    void setDecay(int type, double seconds) {
        switch (type) {
        case kRoom: room_.set(0.75, seconds, 4000.0); break;
        case kChamber: chamber_.setDecay(1.5 * seconds); chamber_.setDamping(5000.0); break;
        case kSpring: for (auto& s : spring_) s.setDecay(1.2 * seconds); break;
        default: digital_.setDecay(seconds); break;
        }
    }
    void reset() {
        room_.reset();
        chamber_.reset();
        for (auto& s : spring_) s.reset();
        springHp_.reset();
        for (auto& c : springLp_) for (auto& f : c) f.reset();
        digital_.reset();
        for (auto& f : digitalLp_) f.reset();
        for (auto& f : sideHp_) f.reset();
        count_ = 0;
        held_[0] = held_[1] = 0.0;
    }
    inline void process(int type, double l, double r, double& wetL, double& wetR) {
        switch (type) {
        case kRoom: room_.process(l, r, wetL, wetR); break;
        case kChamber: chamber_.process(l, r, wetL, wetR); break;
        case kSpring: {
            const double in = springHp_.process(0.5 * (l + r));
            const double s0 = spring_[0].process(in), s1 = spring_[1].process(in), s2 = spring_[2].process(in);
            wetL = 0.5 * springLp_[0][1].process(springLp_[0][0].process(s0 + 0.5 * s2));
            wetR = 0.5 * springLp_[1][1].process(springLp_[1][0].process(s1 + 0.5 * s2));
            break;
        }
        default: { // one sample in every few goes through the reverb; the rest hear it held
            const double fl = digitalLp_[0].process(l), fr = digitalLp_[1].process(r);
            if (++count_ >= every_) {
                count_ = 0;
                double a, b;
                digital_.process(fl, fr, a, b);
                held_[0] = std::round(a * 2048.0) * (1.0 / 2048.0);
                held_[1] = std::round(b * 2048.0) * (1.0 / 2048.0);
            }
            wetL = held_[0];
            wetR = held_[1];
            break;
        }
        }
        const double mid = 0.5 * (wetL + wetR), side = sideHp_[1].process(sideHp_[0].process(0.5 * (wetL - wetR)));
        wetL = mid + side;
        wetR = mid - side;
    }

private:
    double fs_ = 48000.0;
    SmallRoom room_;
    Reverb chamber_, digital_;
    Spring spring_[3];
    Biquad springHp_, springLp_[2][2], digitalLp_[2], sideHp_[2];
    int every_ = 4, count_ = 0;
    double held_[2] = {};
};

} // namespace patina
} // namespace hnl
