// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// What the chain shows and compares, apart from its sound: the spectrum of what goes into the
// compressor, the loudness match for the bypass, the correlation of left and right, and dither.
#pragma once

#include <complex>
#include <cstdint>

#include "../../../common/dsp/Filters.hpp"

namespace hnl {

// --------------------------------------------------------------- spectrum

/**
 * Spectrum in kBands bands spaced evenly over the octaves from 20 Hz to 20 kHz. A transform of
 * about 85 ms, windowed, every half its length. A sine at full scale reads 0 dB in its band.
 */
class Spectrum {
public:
    static constexpr int kBands = 32;

    /** Lower edge of a band in Hz; edge(kBands) is the upper edge of the last. */
    static double edge(int band) { return 20.0 * std::pow(1000.0, static_cast<double>(band) / kBands); }

    void prepare(double fs) {
        fs_ = fs;
        n_ = 4096;
        while (n_ < 0.08 * fs && n_ < 65536) n_ *= 2;
        bits_ = 0;
        while ((1 << bits_) < n_) ++bits_;
        in_.assign(n_, 0.0);
        window_.resize(n_);
        twiddle_.resize(n_ / 2);
        work_.resize(n_);
        for (int i = 0; i < n_; ++i) window_[i] = 0.5 - 0.5 * std::cos(2.0 * kPi * i / n_);
        for (int i = 0; i < n_ / 2; ++i) twiddle_[i] = std::polar(1.0, -2.0 * kPi * i / n_);
        reset();
    }
    void reset() {
        std::fill(in_.begin(), in_.end(), 0.0);
        pos_ = 0;
        fill_ = 0;
        for (double& b : db) b = -100.0;
        fresh = false;
    }
    /** One sample in. Returns true when `db` holds a new spectrum. */
    inline bool process(double x) {
        in_[pos_] = x;
        if (++pos_ == n_) pos_ = 0;
        if (++fill_ < n_ / 2) return false;
        fill_ = 0;
        transform();
        return true;
    }

    double db[kBands] = {}; // dBFS of each band, of the last transform
    bool fresh = false;

private:
    void transform() {
        for (int i = 0; i < n_; ++i) {
            int j = 0; // the bits of i in reverse
            for (int b = 0; b < bits_; ++b) j |= ((i >> b) & 1) << (bits_ - 1 - b);
            work_[j] = window_[i] * in_[(pos_ + i) % n_];
        }
        for (int len = 2; len <= n_; len *= 2)
            for (int start = 0; start < n_; start += len)
                for (int k = 0; k < len / 2; ++k) {
                    const std::complex<double> a = work_[start + k], b = work_[start + k + len / 2] * twiddle_[k * (n_ / len)];
                    work_[start + k] = a + b;
                    work_[start + k + len / 2] = a - b;
                }
        // The window spreads a sine over three bins holding 1.5 times the power of its highest one.
        const double perBin = 16.0 / (static_cast<double>(n_) * n_), summed = perBin / 1.5, hz = fs_ / n_;
        for (int b = 0; b < kBands; ++b) {
            const double lo = edge(b) / hz, hi = edge(b + 1) / hz;
            const int from = static_cast<int>(std::ceil(lo)), to = std::min(static_cast<int>(std::ceil(hi)), n_ / 2);
            double power = 0.0;
            if (to - from >= 3) {
                for (int k = from; k < to; ++k) power += std::norm(work_[k]);
                power *= summed;
            } else { // a band of a bin or two: the strongest bin at it
                const int centre = static_cast<int>(std::lround(std::sqrt(lo * hi)));
                for (int k = std::max(1, std::min(from, centre) - 1); k <= std::min(n_ / 2 - 1, std::max(to, centre)); ++k) power = std::max(power, std::norm(work_[k]));
                power *= perBin;
            }
            db[b] = power > 1e-10 ? 10.0 * std::log10(power) : -100.0;
        }
        fresh = true;
    }

    double fs_ = 48000.0;
    int n_ = 4096, bits_ = 12, pos_ = 0, fill_ = 0;
    std::vector<double> in_, window_;
    std::vector<std::complex<double>> twiddle_, work_;
};

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

// ---------------------------------------------------------- loudness match

/**
 * The gain that brings the processed sound to the loudness of the unprocessed one: both are
 * measured as loudness is (K-weighted) over the last seconds, and the gain is the difference.
 * It stands still in silence and never goes beyond 24 dB down or 12 dB up.
 */
class LoudnessMatch {
public:
    void prepare(double fs) {
        for (auto& k : dry_) k.prepare(fs);
        for (auto& k : wet_) k.prepare(fs);
        coef_ = onePoleCoef(3000.0, fs);
        reset();
    }
    void reset() {
        for (auto& k : dry_) k.reset();
        for (auto& k : wet_) k.reset();
        msDry_ = msWet_ = 0.0;
        gain_ = 1.0;
        count_ = 0;
    }
    /** One frame of the unprocessed and of the processed sound, lined up in time. Returns the gain. */
    inline double process(double dl, double dr, double wl, double wr) {
        const double a = dry_[0].process(dl), b = dry_[1].process(dr), c = wet_[0].process(wl), d = wet_[1].process(wr);
        const double dry = a * a + b * b, wet = c * c + d * d;
        // Only what is heard counts: in silence both hold what they had, so the gain holds too.
        if (dry > kFloor || wet > kFloor) {
            msDry_ = dry + coef_ * (msDry_ - dry);
            msWet_ = wet + coef_ * (msWet_ - wet);
        }
        if (++count_ >= 32) {
            count_ = 0;
            if (msDry_ > kFloor && msWet_ > kFloor) gain_ = std::clamp(std::sqrt(msDry_ / msWet_), 0.0630957344480193, 3.9810717055349722);
        }
        return gain_;
    }

private:
    static constexpr double kFloor = 1e-7; // -70 dB

    KWeight dry_[2], wet_[2];
    double coef_ = 0.0, msDry_ = 0.0, msWet_ = 0.0, gain_ = 1.0;
    int count_ = 0;
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

// ----------------------------------------------------------------- dither

/**
 * Rounds to 16 or 24 bits with dither: noise of two steps from peak to peak, triangular, added
 * before the rounding, so that what the rounding takes away becomes a steady hiss instead of
 * distortion that moves with the sound. The host's own rounding to that word length then changes nothing.
 */
class Dither {
public:
    /** Left and right each get a seed of their own, so that their noise is not the same. */
    void reset(uint32_t seed) { state_ = seed; }
    inline double process(double x, int bits) {
        const double steps = bits == 16 ? 32768.0 : 8388608.0;
        return std::floor(x * steps + next() + next() + 0.5) / steps; // two even ones make a triangular one
    }

private:
    /** Evenly spread over -0.5..0.5. */
    inline double next() {
        state_ = state_ * 1664525u + 1013904223u;
        return static_cast<double>(state_ >> 8) * (1.0 / 16777216.0) - 0.5;
    }
    uint32_t state_ = 0x9e3779b9u;
};

} // namespace hnl
