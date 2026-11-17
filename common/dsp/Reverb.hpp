// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// A small stereo reverb, shared by the plugins that carry one.
#pragma once

#include <iterator>

#include "Filters.hpp"

namespace hnl {

/**
 * Small stereo reverb: two allpass diffusers per channel into an eight-line
 * feedback delay network (Householder mixing) with high-frequency damping.
 * Lows below 200 Hz are kept out of the reverb so the bass stays dry.
 */
class Reverb {
public:
    static constexpr int kLines = 8;

    void prepare(double fs) {
        fs_ = fs;
        static const double lineMs[kLines] = {31.7, 37.9, 43.3, 47.5, 53.9, 61.3, 67.7, 73.9};
        static const double apMs[2][2] = {{4.7, 7.9}, {5.3, 8.9}};
        for (int i = 0; i < kLines; ++i) {
            len_[i] = static_cast<int>(lineMs[i] * 0.001 * fs) | 1;
            line_[i].setLength(len_[i]);
        }
        for (int c = 0; c < 2; ++c) {
            for (int a = 0; a < 2; ++a) ap_[c][a].setLength(static_cast<int>(apMs[c][a] * 0.001 * fs) | 1);
            lowCut_[c].set(designHighpass(fs, 200.0, 0.7071067811865476));
        }
        setDecay(decay_);
        setDamping(dampFreq_);
        reset();
    }
    /** Time for the tail to fall by 60 dB, in seconds. */
    void setDecay(double seconds) {
        decay_ = std::max(0.05, seconds);
        double mean = 0.0;
        for (int i = 0; i < kLines; ++i) {
            gain_[i] = std::pow(10.0, -3.0 * len_[i] / (decay_ * fs_));
            mean += gain_[i] * gain_[i] / kLines;
        }
        // Keeps the reverb level roughly independent of the decay time.
        level_ = 0.45 * std::sqrt(1.0 - mean);
    }
    /** Frequency above which the tail dies away faster. */
    void setDamping(double freq) {
        dampFreq_ = freq;
        damp_ = std::exp(-2.0 * kPi * std::min(freq, 0.45 * fs_) / fs_);
    }
    void reset() {
        for (auto& l : line_) l.reset();
        for (auto& c : ap_) for (auto& a : c) a.reset();
        for (auto& f : lowCut_) f.reset();
        std::fill(std::begin(lp_), std::end(lp_), 0.0);
    }
    /** Returns the reverb signal only; the caller mixes it with the dry signal. */
    inline void process(double l, double r, double& wetL, double& wetR) {
        const double in[2] = {diffuse(0, lowCut_[0].process(l)), diffuse(1, lowCut_[1].process(r))};
        double out[kLines], sum = 0.0;
        for (int i = 0; i < kLines; ++i) { out[i] = line_[i].peek(); sum += out[i]; }
        wetL = level_ * (out[0] - out[2] + out[4] - out[6]);
        wetR = level_ * (out[1] - out[3] + out[5] - out[7]);
        sum *= 2.0 / kLines;
        for (int i = 0; i < kLines; ++i) {
            const double v = out[i] - sum;
            lp_[i] = v + damp_ * (lp_[i] - v);
            line_[i].push(gain_[i] * lp_[i] + ((i & 2) ? -in[i & 1] : in[i & 1]));
        }
    }

private:
    inline double diffuse(int ch, double x) {
        for (Delay& d : ap_[ch]) {
            const double delayed = d.peek(), y = delayed - 0.6 * x;
            d.push(x + 0.6 * y);
            x = y;
        }
        return x;
    }

    double fs_ = 48000.0, decay_ = 1.4, dampFreq_ = 6000.0, damp_ = 0.0, level_ = 0.0;
    int len_[kLines] = {};
    double gain_[kLines] = {}, lp_[kLines] = {};
    Delay line_[kLines], ap_[2][2];
    Biquad lowCut_[2];
};

} // namespace hnl
