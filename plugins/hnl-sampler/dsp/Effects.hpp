// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// The effects every part of the sampler can send to: a stereo delay and a
// chorus. (The reverb is the shared one in common/dsp/Reverb.hpp.) Each
// returns its own signal only; the engine adds it to the main output.
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "../../../common/dsp/Filters.hpp"

namespace hnl {
namespace sampler {

/** A delay line read between its samples, so that its length can change without clicks. */
struct TapeLine {
    std::vector<float> buf;
    int pos = 0;

    void setLength(int n) { buf.assign(static_cast<size_t>(std::max(4, n)), 0.0f); pos = 0; }
    void reset() { std::fill(buf.begin(), buf.end(), 0.0f); }
    inline void push(float x) {
        buf[static_cast<size_t>(pos)] = x;
        if (++pos == static_cast<int>(buf.size())) pos = 0;
    }
    /** The sample pushed `delay` samples ago (1 = the last one); delay may lie between whole numbers. */
    inline float read(double delay) const {
        const int n = static_cast<int>(buf.size());
        delay = delay < 1.0 ? 1.0 : (delay > n - 2 ? n - 2 : delay);
        const int whole = static_cast<int>(delay);
        const float frac = static_cast<float>(delay - whole);
        int a = pos - whole;
        if (a < 0) a += n;
        const int b = a == 0 ? n - 1 : a - 1;
        return buf[static_cast<size_t>(a)] + frac * (buf[static_cast<size_t>(b)] - buf[static_cast<size_t>(a)]);
    }
};

/**
 * Stereo delay. Width 0 repeats each side on itself; width 100 sends every repeat to the other
 * side (ping-pong). The repeats pass a lowpass, so that each is duller than the one before. A
 * change of time glides, as on a tape echo.
 */
class StereoDelay {
public:
    static constexpr double kMaxSeconds = 4.0;

    void prepare(double fs) {
        fs_ = fs;
        for (TapeLine& l : line_) l.setLength(static_cast<int>(kMaxSeconds * fs) + 8);
        glide_ = onePoleCoef(60.0, fs);
        reset();
    }
    void reset() {
        for (TapeLine& l : line_) l.reset();
        lp_[0] = lp_[1] = 0.0f;
        now_ = target_;
    }
    void set(double seconds, double feedback, double toneHz, double width) {
        target_ = std::clamp(seconds, 0.001, kMaxSeconds - 0.01) * fs_;
        feedback_ = static_cast<float>(std::clamp(feedback, 0.0, 0.95));
        tone_ = static_cast<float>(1.0 - std::exp(-2.0 * kPi * std::min(toneHz, 0.45 * fs_) / fs_));
        cross_ = static_cast<float>(std::clamp(width, 0.0, 1.0));
    }
    inline void process(float l, float r, float& wetL, float& wetR) {
        now_ = target_ + glide_ * (now_ - target_);
        const float dl = line_[0].read(now_), dr = line_[1].read(now_);
        lp_[0] += tone_ * (dl - lp_[0]);
        lp_[1] += tone_ * (dr - lp_[1]);
        // Ping-pong: what comes in goes to the left line only, and the lines feed each other.
        const float backL = (1.0f - cross_) * lp_[0] + cross_ * lp_[1], backR = (1.0f - cross_) * lp_[1] + cross_ * lp_[0];
        line_[0].push(l + cross_ * r + feedback_ * backL);
        line_[1].push((1.0f - cross_) * r + feedback_ * backR);
        wetL = lp_[0];
        wetR = lp_[1];
    }

private:
    double fs_ = 48000.0, target_ = 16800.0, now_ = 16800.0, glide_ = 0.0;
    float feedback_ = 0.35f, tone_ = 0.5f, cross_ = 0.6f, lp_[2] = {0.0f, 0.0f};
    TapeLine line_[2];
};

/** Chorus: on each side two copies of the sound whose delay moves slowly, a quarter turn apart. */
class Chorus {
public:
    void prepare(double fs) {
        fs_ = fs;
        for (TapeLine& l : line_) l.setLength(static_cast<int>(0.05 * fs) + 8);
        reset();
    }
    void reset() {
        for (TapeLine& l : line_) l.reset();
        phase_ = 0.0;
    }
    void set(double rateHz, double depth) {
        inc_ = rateHz / fs_;
        depth_ = std::clamp(depth, 0.0, 1.0);
    }
    inline void process(float l, float r, float& wetL, float& wetR) {
        phase_ += inc_;
        if (phase_ >= 1.0) phase_ -= 1.0;
        line_[0].push(l);
        line_[1].push(r);
        const double base = 0.012 * fs_, swing = 0.006 * fs_ * depth_;
        const double a = std::sin(2.0 * kPi * phase_), b = std::cos(2.0 * kPi * phase_);
        wetL = 0.7f * (line_[0].read(base + swing * a) + line_[0].read(base * 1.4 - swing * b));
        wetR = 0.7f * (line_[1].read(base - swing * a) + line_[1].read(base * 1.4 + swing * b));
    }

private:
    double fs_ = 48000.0, phase_ = 0.0, inc_ = 0.0, depth_ = 0.4;
    TapeLine line_[2];
};

} // namespace sampler
} // namespace hnl
