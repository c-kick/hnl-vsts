// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// The record under HNL Patina: a platter that turns, and what a record that does not run true does
// to the length of a delay. No sound passes through here; the engine reads the delay and uses it.
#pragma once

#include <algorithm>
#include <cmath>

#include "Parts.hpp"

namespace hnl {
namespace patina {

/** Where each scratch lies on the record, in turns, and how hard it clicks. The window draws the same table. */
constexpr int kScratchCount = 6;
constexpr double kScratchAngle[kScratchCount] = {0.57, 0.88, 0.20, 0.41, 0.73, 0.05};
constexpr double kScratchLevel[kScratchCount] = {1.0, 0.7, 0.85, 0.5, 0.6, 0.4};

/** How many scratches a record has at this Amount (0..1) of Vinyl: one to six. */
inline int scratchesAt(double amount) { return std::clamp(1 + static_cast<int>(amount * 5.999), 1, kScratchCount); }

/** Turns a second at 33 1/3, 45 and 78 turns a minute. */
inline double turnsPerSecond(int speed) { return speed == 2 ? 78.0 / 60.0 : speed == 1 ? 0.75 : 100.0 / 180.0; }

/**
 * The platter and the delay it moves.
 *
 * In a moving delay the pitch is off by the rate at which the delay changes. The delay swings
 * around a fixed centre of kCentreMs and stays within kWindowMs of it: an off-centre record at
 * 33 1/3 needs 8.4 ms to each side for 50 cent. Movements with no natural end (a slipping belt, a
 * tired motor, a hit that drags the platter) are steered back to the centre in about two seconds,
 * and near the edge of the window the movement is pressed flat instead of cut off.
 */
class Platter {
public:
    static constexpr double kCentreMs = 12.0, kWindowMs = 11.0, kKneeMs = 9.0;
    static constexpr double kFullDepth = 0.029302; // 50 cent as a fraction of the speed
    static constexpr double kSagDepth = 0.014545;  // 25 cent
    static constexpr double kBeltShare = 0.2;      // a belt slips slowly: a fifth of the depth

    enum Type { kOffCentre, kWarped, kBelt, kMotor, kTypes };

    double angle = 0.0;      // 0..1, one turn
    double cents = 0.0;      // how far off pitch the left channel is right now
    double delay[2] = {0, 0}; // samples, left and right
    bool turned = false;     // the platter passed angle 0 on this sample

    void prepare(double fs) {
        fs_ = fs;
        centre_ = std::round(kCentreMs * 0.001 * fs);
        rateCoef_ = onePoleCoef(300.0, fs);
        mixCoef_ = onePoleCoef(50.0, fs);
        beltCoef_ = onePoleCoef(400.0, fs);
        leak_ = 1.0 / (2.0 * fs);
        reset();
    }
    /** The latency of the delay: its centre, a whole number of samples. */
    int centre() const { return static_cast<int>(centre_); }
    double longest() const { return centre_ + (kWindowMs + 1.0) * 0.001 * fs_; }

    void reset() {
        angle = 0.0; cents = 0.0; turned = false;
        rate_ = -1.0;
        for (int i = 0; i < kTypes; ++i) mix_[i] = -1.0;
        beltTarget_ = belt_ = 0.0; beltPhase_ = 0.0;
        drift_ = 0.0;
        delay[0] = delay[1] = centre_;
        rng_.seed(0x51f15e17u);
    }

    /**
     * One sample on. `turning`: the record turns at all. `type` and `speed` are where it is going;
     * depth 0..1 is Motion's Amount, sag 0..1 how hard a hit drags the platter right now.
     */
    inline void process(bool turning, int type, int speed, double depth, double sag) {
        const double target = turnsPerSecond(speed);
        if (rate_ < 0.0) { // first sample after a reset: no glide
            rate_ = target;
            for (int i = 0; i < kTypes; ++i) mix_[i] = i == type ? 1.0 : 0.0;
        }
        rate_ = target + rateCoef_ * (rate_ - target);
        for (int i = 0; i < kTypes; ++i) {
            const double t = i == type ? 1.0 : 0.0;
            mix_[i] = t + mixCoef_ * (mix_[i] - t);
        }
        turned = false;
        if (turning) {
            const double step = rate_ / fs_;
            angle += step;
            if (angle >= 1.0) { angle -= 1.0; turned = true; }
            beltPhase_ += 2.0 * step; // a new speed for the belt twice a turn
            if (beltPhase_ >= 1.0) { beltPhase_ -= 1.0; beltTarget_ = rng_.bipolar(); }
        }
        belt_ = beltTarget_ + beltCoef_ * (belt_ - beltTarget_);

        const double dev = kFullDepth * depth;                 // the largest speed error, as a fraction
        const double swing = dev / (2.0 * kPi * rate_) * fs_;  // samples, for one swing a turn
        const double a = 2.0 * kPi * angle;
        // An off-centre hole: one swing a turn. A warp: two uneven bumps a turn, and the right channel a little later.
        const double offCentre = swing * std::sin(a);
        const double warpL = swing / 2.3 * (std::sin(2.0 * a) + 0.3 * std::sin(a));
        const double warpR = swing / 2.3 * (std::sin(2.0 * a + 0.35) + 0.3 * std::sin(a + 0.2));
        // A speed that is wrong for a while: the delay grows while the record runs slow.
        const double speedError = dev * (mix_[kBelt] * kBeltShare * belt_ + mix_[kMotor] * motor(angle)) - kSagDepth * sag;
        drift_ += -speedError - drift_ * leak_;

        const double present = smoothstep(depth / 0.05); // at Amount 0 the delay stands still, whatever else is set
        const double common = mix_[kOffCentre] * offCentre + drift_;
        const double before = delay[0];
        delay[0] = centre_ + pressed(present * (common + mix_[kWarped] * warpL));
        delay[1] = centre_ + pressed(present * (common + mix_[kWarped] * warpR));
        cents = -(delay[0] - before) * 1731.234; // 1200 / ln 2
    }

private:
    /** A motor that is tired: a little fast most of the turn, then a sag for a quarter of it. */
    static inline double motor(double turn) {
        return turn < 0.75 ? 0.3333333 * std::sin(kPi * turn / 0.75) : -std::sin(kPi * (turn - 0.75) / 0.25);
    }
    /** Keeps a movement inside the window: untouched up to the knee, pressed flat beyond it. */
    inline double pressed(double samples) const {
        const double knee = kKneeMs * 0.001 * fs_, room = (kWindowMs - kKneeMs) * 0.001 * fs_;
        const double m = std::fabs(samples);
        if (m <= knee) return samples;
        const double out = knee + room * std::tanh((m - knee) / room);
        return samples < 0.0 ? -out : out;
    }

    double fs_ = 48000.0, centre_ = 576.0, rateCoef_ = 0, mixCoef_ = 0, beltCoef_ = 0, leak_ = 0;
    double rate_ = -1.0, mix_[kTypes] = {-1, -1, -1, -1};
    double beltTarget_ = 0, belt_ = 0, beltPhase_ = 0, drift_ = 0;
    Rng rng_;
};

} // namespace patina
} // namespace hnl
