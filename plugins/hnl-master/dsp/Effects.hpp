// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// The optional colour stages of the mastering chain: three-band equaliser,
// stereo widener and reverb. None of them adds latency.
#pragma once

#include "../../../common/dsp/Filters.hpp"
#include "../../../common/dsp/Reverb.hpp"

namespace hnl {

// -------------------------------------------------------------- equaliser

/**
 * Low shelf, peak and high shelf in series. Settings glide to their targets
 * in small steps so turning a knob does not click.
 */
class Eq3 {
public:
    enum { kLowShelf, kPeak, kHighShelf, kBands };

    void prepare(double fs) { fs_ = fs; reset(); }
    void set(int band, double freq, double gainDb, double q = 0.7071067811865476) { target_[band] = {freq, gainDb, q}; }
    /** Jumps to the targets and clears the filters. */
    void reset() {
        for (int b = 0; b < kBands; ++b) {
            cur_[b] = target_[b];
            design(b);
            for (auto& ch : f_) ch[b].reset();
        }
        count_ = 0;
    }
    /** Call once per sample frame, before process(). */
    inline void tick() {
        if (++count_ < kStep) return;
        count_ = 0;
        for (int b = 0; b < kBands; ++b) {
            Band& c = cur_[b];
            const Band& t = target_[b];
            if (c.freq == t.freq && c.gain == t.gain && c.q == t.q) continue;
            c.gain = std::fabs(t.gain - c.gain) < 0.02 ? t.gain : c.gain + 0.2 * (t.gain - c.gain);
            c.freq = std::fabs(std::log(t.freq / c.freq)) < 0.002 ? t.freq : c.freq * std::pow(t.freq / c.freq, 0.2);
            c.q = std::fabs(std::log(t.q / c.q)) < 0.002 ? t.q : c.q * std::pow(t.q / c.q, 0.2);
            design(b);
        }
    }
    inline double process(int ch, double x) {
        return f_[ch][2].process(f_[ch][1].process(f_[ch][0].process(x)));
    }

private:
    struct Band { double freq, gain, q; };
    static constexpr int kStep = 16;

    void design(int b) {
        const Band& c = cur_[b];
        const BiquadCoeffs k = b == kPeak ? designPeak(fs_, c.freq, c.q, c.gain) : designShelf(fs_, c.freq, c.gain, b == kHighShelf);
        f_[0][b].set(k);
        f_[1][b].set(k);
    }

    double fs_ = 48000.0;
    Band target_[kBands] = {{80, 0, 0.7071}, {1000, 0, 1}, {10000, 0, 0.7071}};
    Band cur_[kBands] = {{80, 0, 0.7071}, {1000, 0, 1}, {10000, 0, 0.7071}};
    Biquad f_[2][kBands];
    int count_ = 0;
};

// ---------------------------------------------------------------- widener

/**
 * Mid/side stereo width. Widening only raises the side signal above `freq`,
 * so the bass keeps its original width; narrowing scales the whole side
 * signal. The mid (mono) signal is never touched, so a mono fold-down sounds
 * the same at every setting.
 */
class Widener {
public:
    void setFreq(double fs, double fc) { hp_.set(designHighpass(fs, fc, 0.7071067811865476)); }
    void reset() { hp_.reset(); }
    /** width: 0 = mono, 1 = unchanged, 2 = side signal doubled. */
    inline void process(double& l, double& r, double width) {
        const double mid = 0.5 * (l + r), side = 0.5 * (l - r);
        const double high = hp_.process(side);
        const double s = std::min(width, 1.0) * side + std::max(width - 1.0, 0.0) * high;
        l = mid + s;
        r = mid - s;
    }

private:
    Biquad hp_;
};

} // namespace hnl
