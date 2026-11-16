// The optional colour stages of the mastering chain: three-band equaliser,
// stereo widener and reverb. None of them adds latency.
#pragma once

#include "Filters.hpp"

namespace hm {

// -------------------------------------------------------------- equaliser

/** Peaking filter (Audio EQ Cookbook). */
inline BiquadCoeffs designPeak(double fs, double fc, double q, double gainDb) {
    const double A = std::pow(10.0, gainDb / 40.0), w = 2.0 * kPi * clampFreq(fs, fc) / fs;
    const double c = std::cos(w), alpha = std::sin(w) / (2.0 * q), a0 = 1.0 + alpha / A;
    return {(1 + alpha * A) / a0, -2 * c / a0, (1 - alpha * A) / a0, -2 * c / a0, (1 - alpha / A) / a0};
}

/** Low (high = false) or high shelf with a Butterworth-like slope. */
inline BiquadCoeffs designShelf(double fs, double fc, double gainDb, bool high) {
    const double A = std::pow(10.0, gainDb / 40.0), w = 2.0 * kPi * clampFreq(fs, fc) / fs;
    const double c = (high ? -1.0 : 1.0) * std::cos(w);
    const double sq = 2.0 * std::sqrt(A) * std::sin(w) / (2.0 * 0.7071067811865476);
    const double a0 = (A + 1) + (A - 1) * c + sq, sign = high ? -1.0 : 1.0;
    return {A * ((A + 1) - (A - 1) * c + sq) / a0, sign * 2 * A * ((A - 1) - (A + 1) * c) / a0,
            A * ((A + 1) - (A - 1) * c - sq) / a0, sign * -2 * ((A - 1) + (A + 1) * c) / a0,
            ((A + 1) + (A - 1) * c - sq) / a0};
}

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

// ----------------------------------------------------------------- reverb

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

} // namespace hm
