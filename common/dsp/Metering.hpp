// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// What the meters of more than one plugin are made of: the loudness filters of ITU-R BS.1770
// and the correlation of left and right. Header-only, no dependencies.
#pragma once

#include <algorithm>
#include <cmath>

#include "Filters.hpp"

namespace hnl {

// ----------------------------------------------------------- K-weighting

/** The two filters of ITU-R BS.1770 loudness for one channel, re-derived for the sample rate (as in libebur128). */
struct KWeight {
    Biquad shelf, hp;

    void prepare(double fs) {
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan(kPi * f0 / fs), Vh = std::pow(10.0, G / 20.0);
            const double Vb = std::pow(Vh, 0.4996667741545416), a0 = 1.0 + K / Q + K * K;
            shelf.set({(Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0,
                       2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0});
        }
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan(kPi * f0 / fs), a0 = 1.0 + K / Q + K * K;
            hp.set({1.0, -2.0, 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0});
        }
        reset();
    }
    void reset() { shelf.reset(); hp.reset(); }
    inline double process(double x) { return hp.process(shelf.process(x)); }
};

// ------------------------------------------------------------ correlation

/** How alike left and right are over the last 0.3 s: +1 the same (mono), 0 unrelated, -1 opposite, which is lost in mono. */
class Correlation {
public:
    void prepare(double fs) { coef_ = onePoleCoef(300.0, fs); reset(); }
    void reset() { lr_ = ll_ = rr_ = 0.0; value = 0.0; }
    inline void process(double l, double r) {
        lr_ = l * r + coef_ * (lr_ - l * r);
        ll_ = l * l + coef_ * (ll_ - l * l);
        rr_ = r * r + coef_ * (rr_ - r * r);
        const double e = ll_ * rr_;
        if (e > 1e-16) value = std::clamp(lr_ / std::sqrt(e), -1.0, 1.0); // in silence it stays where it was
    }

    double value = 0.0;

private:
    double coef_ = 0.0, lr_ = 0.0, ll_ = 0.0, rr_ = 0.0;
};

} // namespace hnl
