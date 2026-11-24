// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// Reading a recorded sound at any speed.
//
// A sound played faster or slower than it was recorded is read between its
// samples. Each value read is a weighted sum of the samples around that
// place (a windowed sinc). Played faster, the sound would get overtones
// above half the sample rate, which come back as false tones; for higher
// speeds there are therefore wider sets of weights that also filter those
// overtones out. Header-only, no dependencies.
#pragma once

#include <cmath>
#include <vector>

#include "../../../common/dsp/Filters.hpp"

namespace hnl {
namespace sampler {

/** One set of weights: `taps` weights for each of kPhases places between two samples, and their change towards the next place. */
struct Kernel {
    static constexpr int kPhases = 128;
    int taps = 0;
    double maxRatio = 1.0;      // the speed up to which this set is used
    std::vector<float> h, d;    // (kPhases + 1) x taps

    void design(int tapCount, double ratio) {
        taps = tapCount;
        maxRatio = ratio;
        // Above speed 1 the cutoff comes down with the speed; a little under it, so that the slope of the filter has room.
        const double cutoff = ratio <= 1.0 ? 1.0 : 0.92 / ratio, beta = 7.0;
        const int half = taps / 2, length = taps * kPhases + 1;
        h.assign(static_cast<size_t>(kPhases + 1) * taps, 0.0f);
        d.assign(h.size(), 0.0f);
        for (int p = 0; p <= kPhases; ++p) {
            double sum = 0.0;
            std::vector<double> row(taps);
            for (int k = 0; k < taps; ++k) {
                // The distance from the place read to sample k of the window, in samples.
                const double x = (k - half + 1) - static_cast<double>(p) / kPhases;
                const double s = std::fabs(x) < 1e-9 ? cutoff : std::sin(kPi * cutoff * x) / (kPi * x);
                const int n = static_cast<int>(std::lround((x + half) * kPhases));
                row[k] = n < 0 || n >= length ? 0.0 : s * kaiser(n, length, beta);
                sum += row[k];
            }
            for (int k = 0; k < taps; ++k) h[static_cast<size_t>(p) * taps + k] = static_cast<float>(row[k] / sum); // a steady level stays what it is
        }
        for (int p = 0; p < kPhases; ++p)
            for (int k = 0; k < taps; ++k) d[static_cast<size_t>(p) * taps + k] = h[static_cast<size_t>(p + 1) * taps + k] - h[static_cast<size_t>(p) * taps + k];
    }
};

/** The sets of weights, made once for the whole process. */
class Kernels {
public:
    static constexpr int kCount = 6;
    static const Kernels& get() {
        static const Kernels all;
        return all;
    }
    /** The set for a speed (1 = as recorded). */
    const Kernel& forRatio(double ratio) const {
        for (int i = 0; i < kCount - 1; ++i)
            if (ratio <= k_[i].maxRatio) return k_[i];
        return k_[kCount - 1];
    }
    const Kernel& at(int i) const { return k_[i]; }

private:
    Kernels() {
        static const int taps[kCount] = {12, 16, 24, 32, 48, 64};
        static const double ratios[kCount] = {1.02, 1.5, 2.0, 3.0, 4.0, 6.0};
        for (int i = 0; i < kCount; ++i) k_[i].design(taps[i], ratios[i]);
    }
    Kernel k_[kCount];
};

/** Where a sound may be read, and how it goes on at the ends of that stretch. */
struct ReadWindow {
    enum Edge { kSilence, kWrap, kMirror };
    long long lo = 0, hi = 0; // frames lo..hi-1 can be read as they are
    Edge below = kSilence, above = kSilence;
    /** How far back a place above `hi` wraps when that is not the whole window: the length of a loop that starts above `lo`. */
    long long turn = 0;

    /** The frame that stands at place i, or -1 for silence. */
    inline long long map(long long i) const {
        const long long length = hi - lo;
        if (length <= 0) return -1;
        for (int tries = 0; tries < 4 && (i < lo || i >= hi); ++tries) {
            if (i >= hi) {
                if (above == kSilence) return -1;
                i = above == kWrap ? i - (turn > 0 && turn < length ? turn : length) : 2 * hi - 1 - i;
            } else {
                if (below == kSilence) return -1;
                i = below == kWrap ? i + length : 2 * lo - i;
            }
        }
        return i < lo || i >= hi ? -1 : i;
    }
};

/**
 * The value of a sound at place `pos` (in frames, between samples). `x` is one channel; `first` and `step`
 * say where frame 0 of the stretch is and which way it runs (step -1 plays the sound backwards).
 */
inline float readAt(const float* x, long long first, int step, const ReadWindow& w, double pos, const Kernel& k) {
    const double whole = std::floor(pos);
    const long long ipos = static_cast<long long>(whole);
    const double fine = (pos - whole) * Kernel::kPhases;
    const int phase = static_cast<int>(fine);
    const float frac = static_cast<float>(fine - phase);
    const float* h = &k.h[static_cast<size_t>(phase) * k.taps];
    const float* d = &k.d[static_cast<size_t>(phase) * k.taps];
    const long long from = ipos - k.taps / 2 + 1;
    float sum = 0.0f;
    if (from >= w.lo && from + k.taps <= w.hi) { // all of it lies inside: no need to look where each sample is
        const float* p = x + first + step * from;
        if (step > 0)
            for (int i = 0; i < k.taps; ++i) sum += (h[i] + frac * d[i]) * p[i];
        else
            for (int i = 0; i < k.taps; ++i) sum += (h[i] + frac * d[i]) * p[-i];
        return sum;
    }
    for (int i = 0; i < k.taps; ++i) {
        const long long at = w.map(from + i);
        if (at >= 0) sum += (h[i] + frac * d[i]) * x[first + step * at];
    }
    return sum;
}

} // namespace sampler
} // namespace hnl
