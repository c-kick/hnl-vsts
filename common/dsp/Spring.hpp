// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// One spring of a spring reverb, shared by the plugins that carry one (HNL Tape Echo, HNL Patina).
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "Filters.hpp"

namespace hnl {

/**
 * One spring: a long chain of allpass filters that lets the lows through first
 * and the highs later (the "boing"), in a loop with a delay. The allpasses
 * are stretched so the whole sweep lies below the spring's top frequency.
 */
class Spring {
public:
    static constexpr int kSections = 72;

    void prepare(double fs, int stretch, double a, double loopSeconds, double decaySeconds) {
        k_ = stretch;
        a_ = a;
        state_.assign(static_cast<size_t>(kSections) * k_, 0.0);
        at_ = 0;
        line_.setLength(static_cast<size_t>(std::max(1.0, loopSeconds * fs)));
        // The chain adds its own delay: about (1 + a) / (1 - a) samples a section at the top.
        const double chain = kSections * k_ * 0.5 * ((1 + a) / (1 - a) + (1 - a) / (1 + a)) / fs;
        round_ = loopSeconds + chain;
        gain_ = std::pow(10.0, -3.0 * (loopSeconds + chain) / decaySeconds);
        damp_ = std::exp(-2.0 * kPi * 6000.0 / fs);
        reset();
    }
    /** The time the spring takes to fall by 60 dB, in seconds: can be set while it runs. */
    void setDecay(double seconds) { gain_ = std::pow(10.0, -3.0 * round_ / std::max(0.1, seconds)); }
    void reset() {
        std::fill(state_.begin(), state_.end(), 0.0);
        line_.reset();
        lp_ = 0.0;
    }
    inline double process(double x) {
        lp_ = line_.peek() + damp_ * (lp_ - line_.peek());
        double s = x + gain_ * lp_;
        double* w = &state_[static_cast<size_t>(at_) * kSections];
        for (int i = 0; i < kSections; ++i) { // w[n] = x[n] - a w[n-K]; y[n] = a w[n] + w[n-K]
            const double old = w[i], v = s - a_ * old;
            w[i] = v;
            s = a_ * v + old;
        }
        if (++at_ == k_) at_ = 0;
        line_.push(s);
        return s;
    }

private:
    std::vector<double> state_; // k_ rows of kSections: the row in use holds the values from k_ samples ago
    Delay line_;
    int k_ = 1, at_ = 0;
    double a_ = 0.6, gain_ = 0.0, damp_ = 0.0, lp_ = 0.0, round_ = 0.03;
};

} // namespace hnl
