// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// Small parts of HNL Patina: a fixed random sequence, a band-pass design and the moving delay
// under the record. The small room and the spring come from common/dsp/.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <vector>

#include "../../../common/dsp/Filters.hpp"
#include "../../../common/dsp/Room.hpp"
#include "../../../common/dsp/Spring.hpp"

namespace hnl {
namespace patina {

/** A random sequence that is the same on every run: the crackle of a song renders the same each time. */
struct Rng {
    uint32_t s = 0x2545f491u;
    void seed(uint32_t v) { s = v ? v : 0x2545f491u; }
    inline uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    inline double unit() { return static_cast<double>(next() >> 8) * (1.0 / 16777216.0); } // 0..1
    inline double bipolar() { return 2.0 * unit() - 1.0; }
};

/** Band-pass with a gain of one at its centre. */
inline BiquadCoeffs designBandpass(double fs, double fc, double q) {
    const double w = 2.0 * kPi * clampFreq(fs, fc) / fs;
    const double c = std::cos(w), alpha = std::sin(w) / (2.0 * q), a0 = 1.0 + alpha;
    return {alpha / a0, 0.0, -alpha / a0, -2 * c / a0, (1 - alpha) / a0};
}

inline double smoothstep(double x) {
    x = x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x);
    return x * x * (3.0 - 2.0 * x);
}

/**
 * The delay the record's speed moves: its length changes while it runs, by fractions of a sample.
 * At a whole number of samples it returns the samples exactly as they went in.
 */
class PlatterDelay {
public:
    void setMax(double samples) {
        buf_.assign(static_cast<size_t>(samples) + 8, 0.0);
        pos_ = 0;
    }
    void reset() { std::fill(buf_.begin(), buf_.end(), 0.0); }
    /** Stores x and returns the signal `delay` samples ago (1 or more; cubic between samples). */
    inline double process(double x, double delay) {
        const size_t n = buf_.size();
        buf_[pos_] = x;
        const size_t whole = static_cast<size_t>(delay);
        const double f = delay - static_cast<double>(whole);
        const double y0 = at(whole - 1, n), y1 = at(whole, n), y2 = at(whole + 1, n), y3 = at(whole + 2, n);
        if (++pos_ == n) pos_ = 0;
        const double c1 = 0.5 * (y2 - y0), c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3, c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }

private:
    inline double at(size_t ago, size_t n) const { return buf_[pos_ >= ago ? pos_ - ago : pos_ + n - ago]; }
    std::vector<double> buf_ = std::vector<double>(8, 0.0);
    size_t pos_ = 0;
};

} // namespace patina
} // namespace hnl
