// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// A small stereo room, shared by the plugins that carry one (HNL Thickener, HNL Patina).
#pragma once

#include <algorithm>
#include <cmath>
#include <iterator>
#include <vector>

#include "Filters.hpp"

namespace hnl {

/** The room's delay lines are this much shorter and longer at Size 0% and 100% than at 50%. */
constexpr double kRoomSizeSpan = 2.0;

/**
 * A small room: two allpass diffusers per channel into eight short delay
 * lines that feed each other (Householder mixing), with high-frequency
 * damping. Left and right go into different lines, so the room hears a
 * difference in time between them. Unlike common/dsp/Reverb.hpp the lines are
 * short, their length can change while running (Size), and the lows are not
 * kept out.
 */
class SmallRoom {
public:
    static constexpr int kLines = 8;

    void prepare(double fs) {
        fs_ = fs;
        static const double apMs[2][2] = {{1.9, 3.1}, {2.3, 3.7}};
        for (int i = 0; i < kLines; ++i) {
            buf_[i].assign(static_cast<size_t>(lineMs(i) * kRoomSizeSpan * 0.001 * fs) + 4, 0.0);
            pos_[i] = 0;
        }
        for (int c = 0; c < 2; ++c)
            for (int a = 0; a < 2; ++a) ap_[c][a].setLength(static_cast<size_t>(apMs[c][a] * 0.001 * fs) | 1u);
        set(size_, decay_, dampFreq_);
        reset();
    }
    /** Size 0..1, the time for the tail to fall by 60 dB in seconds, and the damping frequency. */
    void set(double size, double decaySeconds, double dampFreq) {
        size_ = std::clamp(size, 0.0, 1.0);
        decay_ = std::max(0.05, decaySeconds);
        dampFreq_ = dampFreq;
        const double scale = std::pow(kRoomSizeSpan, 2.0 * size_ - 1.0);
        double mean = 0.0;
        for (int i = 0; i < kLines; ++i) {
            const size_t longest = buf_[i].size() - 1;
            len_[i] = std::clamp<size_t>(static_cast<size_t>(lineMs(i) * scale * 0.001 * fs_) | 1u, 1, longest);
            gain_[i] = std::pow(10.0, -3.0 * static_cast<double>(len_[i]) / (decay_ * fs_));
            mean += gain_[i] * gain_[i] / kLines;
        }
        level_ = 0.45 * std::sqrt(1.0 - mean); // keeps the level roughly independent of the decay time
        damp_ = std::exp(-2.0 * kPi * std::min(dampFreq_, 0.45 * fs_) / fs_);
    }
    void reset() {
        for (auto& b : buf_) std::fill(b.begin(), b.end(), 0.0);
        for (auto& c : ap_) for (auto& a : c) a.reset();
        std::fill(std::begin(lp_), std::end(lp_), 0.0);
    }
    /** Returns the room sound only. */
    inline void process(double l, double r, double& wetL, double& wetR) {
        const double in[2] = {diffuse(0, l), diffuse(1, r)};
        double out[kLines], sum = 0.0;
        for (int i = 0; i < kLines; ++i) {
            const size_t n = buf_[i].size();
            out[i] = buf_[i][pos_[i] >= len_[i] ? pos_[i] - len_[i] : pos_[i] + n - len_[i]];
            sum += out[i];
        }
        wetL = level_ * (out[0] - out[2] + out[4] - out[6]);
        wetR = level_ * (out[1] - out[3] + out[5] - out[7]);
        sum *= 2.0 / kLines;
        for (int i = 0; i < kLines; ++i) {
            const double v = out[i] - sum;
            lp_[i] = v + damp_ * (lp_[i] - v);
            buf_[i][pos_[i]] = gain_[i] * lp_[i] + ((i & 2) ? -in[i & 1] : in[i & 1]);
            if (++pos_[i] == buf_[i].size()) pos_[i] = 0;
        }
    }

private:
    static double lineMs(int i) {
        static const double ms[kLines] = {7.3, 8.9, 10.7, 12.1, 13.9, 15.7, 17.9, 19.3};
        return ms[i];
    }
    inline double diffuse(int ch, double x) {
        for (Delay& d : ap_[ch]) {
            const double delayed = d.peek(), y = delayed - 0.6 * x;
            d.push(x + 0.6 * y);
            x = y;
        }
        return x;
    }

    double fs_ = 48000.0, size_ = 0.5, decay_ = 0.2, dampFreq_ = 1500.0, damp_ = 0.0, level_ = 0.0;
    std::vector<double> buf_[kLines] = {std::vector<double>(4, 0.0), std::vector<double>(4, 0.0), std::vector<double>(4, 0.0), std::vector<double>(4, 0.0),
                                        std::vector<double>(4, 0.0), std::vector<double>(4, 0.0), std::vector<double>(4, 0.0), std::vector<double>(4, 0.0)};
    size_t pos_[kLines] = {}, len_[kLines] = {1, 1, 1, 1, 1, 1, 1, 1};
    double gain_[kLines] = {}, lp_[kLines] = {};
    Delay ap_[2][2];
};

} // namespace hnl
