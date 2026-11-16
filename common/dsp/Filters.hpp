// Building blocks shared by all plugins: biquads and their designs, a
// first-order shelf with an exact inverse,
// Linkwitz-Riley crossover, delay, halfband oversampling and a true-peak
// estimator. Header-only, no dependencies.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "Safety.hpp"

namespace hnl {

constexpr double kPi = 3.14159265358979323846;

inline double dbToLin(double db) { return std::exp(db * 0.11512925464970229); }
inline double linToDb(double lin) { return 8.685889638065035 * std::log(std::max(lin, 1e-12)); }

/** One-pole smoothing coefficient for a time constant in milliseconds. */
inline double onePoleCoef(double ms, double fs) {
    return ms <= 0.0 ? 0.0 : std::exp(-1.0 / (0.001 * ms * fs));
}

// ---------------------------------------------------------------- biquad

struct BiquadCoeffs {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
};

inline double clampFreq(double fs, double fc) { return std::clamp(fc, 10.0, 0.45 * fs); }

inline BiquadCoeffs designLowpass(double fs, double fc, double q) {
    const double w = 2.0 * kPi * clampFreq(fs, fc) / fs;
    const double c = std::cos(w), alpha = std::sin(w) / (2.0 * q), a0 = 1.0 + alpha;
    return {(1 - c) * 0.5 / a0, (1 - c) / a0, (1 - c) * 0.5 / a0, -2 * c / a0, (1 - alpha) / a0};
}

inline BiquadCoeffs designHighpass(double fs, double fc, double q) {
    const double w = 2.0 * kPi * clampFreq(fs, fc) / fs;
    const double c = std::cos(w), alpha = std::sin(w) / (2.0 * q), a0 = 1.0 + alpha;
    return {(1 + c) * 0.5 / a0, -(1 + c) / a0, (1 + c) * 0.5 / a0, -2 * c / a0, (1 - alpha) / a0};
}

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

/** Transposed direct form II biquad. */
struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    void set(const BiquadCoeffs& c) { b0 = c.b0; b1 = c.b1; b2 = c.b2; a1 = c.a1; a2 = c.a2; }
    void reset() { z1 = z2 = 0; }
    inline double process(double x) {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

/** First-order filter; used as a high shelf and as its exact inverse. */
struct Shelf1 {
    double b0 = 1, b1 = 0, a1 = 0, z = 0;

    /** Unity at DC, gainDb at the top, half the gain (in dB) at fc. */
    void setHighShelf(double fs, double fc, double gainDb) {
        const double g = std::sqrt(dbToLin(gainDb));
        const double kz = std::tan(kPi * std::min(fc / g, 0.45 * fs) / fs), kp = std::tan(kPi * std::min(fc * g, 0.45 * fs) / fs);
        b0 = kp * (kz + 1) / (kz * (kp + 1));
        b1 = kp * (kz - 1) / (kz * (kp + 1));
        a1 = (kp - 1) / (kp + 1);
    }
    void setInverseOf(const Shelf1& s) { b0 = 1.0 / s.b0; b1 = s.a1 / s.b0; a1 = s.b1 / s.b0; }
    void reset() { z = 0; }
    inline double process(double x) {
        const double y = b0 * x + z;
        z = b1 * x - a1 * y;
        return y;
    }
};

// ------------------------------------------------------------- crossover

/** 4th-order Linkwitz-Riley split (two cascaded Butterworth sections per side). */
struct LR4 {
    Biquad lp[2], hp[2];

    void setFreq(double fs, double fc) {
        const auto l = designLowpass(fs, fc, 0.7071067811865476);
        const auto h = designHighpass(fs, fc, 0.7071067811865476);
        for (int i = 0; i < 2; ++i) { lp[i].set(l); hp[i].set(h); }
    }
    void reset() { for (int i = 0; i < 2; ++i) { lp[i].reset(); hp[i].reset(); } }
    inline void split(double x, double& lo, double& hi) {
        lo = lp[1].process(lp[0].process(x));
        hi = hp[1].process(hp[0].process(x));
    }
};

/**
 * Three-band crossover whose bands sum to an allpass (flat magnitude).
 * The low band is passed through the second crossover's allpass response so
 * it stays phase-aligned with mid + high.
 */
struct Crossover3 {
    LR4 x1, x2, comp;

    void setFreqs(double fs, double f1, double f2) {
        x1.setFreq(fs, f1);
        x2.setFreq(fs, f2);
        comp.setFreq(fs, f2);
    }
    void reset() { x1.reset(); x2.reset(); comp.reset(); }
    inline void split(double x, double& low, double& mid, double& high) {
        double lo, rest, cl, ch;
        x1.split(x, lo, rest);
        x2.split(rest, mid, high);
        comp.split(lo, cl, ch);
        low = cl + ch;
    }
};

// ----------------------------------------------------------------- delay

struct Delay {
    std::vector<double> buf;
    size_t pos = 0;

    void setLength(size_t n) { buf.assign(n, 0.0); pos = 0; }
    void reset() { std::fill(buf.begin(), buf.end(), 0.0); pos = 0; }
    inline double process(double x) {
        if (buf.empty()) return x;
        const double y = buf[pos];
        buf[pos] = x;
        if (++pos == buf.size()) pos = 0;
        return y;
    }
    /** peek() then push() is process() split in two, for feedback loops. */
    inline double peek() const { return buf.empty() ? 0.0 : buf[pos]; }
    inline void push(double x) {
        if (buf.empty()) return;
        buf[pos] = x;
        if (++pos == buf.size()) pos = 0;
    }
};

// --------------------------------------------------------------- windows

inline double besselI0(double x) {
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 64; ++k) {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        if (term < 1e-14 * sum) break;
    }
    return sum;
}

inline double kaiser(int n, int length, double beta) {
    const double r = 2.0 * n / (length - 1) - 1.0;
    return besselI0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / besselI0(beta);
}

// ------------------------------------------------------------- halfband

/** Linear-phase halfband FIR used for one 2x up/down sampling stage. */
class Halfband {
public:
    /** taps must be of the form 4k+3. */
    void design(int taps, double beta) {
        n_ = taps;
        const int m = (n_ - 1) / 2;
        h_.resize(n_);
        double sum = 0.0;
        for (int i = 0; i < n_; ++i) {
            const int k = i - m;
            const double s = k == 0 ? 0.5 : (k % 2 == 0 ? 0.0 : std::sin(kPi * k / 2.0) / (kPi * k));
            h_[i] = s * kaiser(i, n_, beta);
            sum += h_[i];
        }
        for (auto& v : h_) v /= sum;
        up_.assign((n_ + 1) / 2, 0.0);
        dn_.assign(n_, 0.0);
        upPos_ = dnPos_ = 0;
    }
    void reset() {
        std::fill(up_.begin(), up_.end(), 0.0);
        std::fill(dn_.begin(), dn_.end(), 0.0);
    }
    /** One input sample in, two output samples (at twice the rate) out. */
    inline void up(double x, double& y0, double& y1) {
        const int k = static_cast<int>(up_.size());
        if (++upPos_ == k) upPos_ = 0;
        up_[upPos_] = x;
        double a = 0.0, b = 0.0;
        int idx = upPos_;
        for (int j = 0; j < k; ++j) {
            a += h_[2 * j] * up_[idx];
            if (2 * j + 1 < n_) b += h_[2 * j + 1] * up_[idx];
            idx = idx == 0 ? k - 1 : idx - 1;
        }
        y0 = 2.0 * a;
        y1 = 2.0 * b;
    }
    /** Two input samples (at twice the rate) in, one output sample out. */
    inline double down(double v0, double v1) {
        if (++dnPos_ == n_) dnPos_ = 0;
        dn_[dnPos_] = v0;
        if (++dnPos_ == n_) dnPos_ = 0;
        dn_[dnPos_] = v1;
        double y = 0.0;
        int idx = dnPos_;
        for (int j = 0; j < n_; ++j) {
            y += h_[j] * dn_[idx];
            idx = idx == 0 ? n_ - 1 : idx - 1;
        }
        return y;
    }

private:
    int n_ = 0, upPos_ = 0, dnPos_ = 0;
    std::vector<double> h_, up_, dn_;
};

// ------------------------------------------------------------ true peak

/**
 * Inter-sample peak estimator: 8x polyphase interpolation. process() returns
 * the largest absolute value around the sample pushed latency() samples ago.
 */
class TruePeak {
public:
    static constexpr int kPhases = 8, kTapsPerPhase = 32;

    TruePeak() {
        const int n = kPhases * kTapsPerPhase + 1, m = (n - 1) / 2;
        h_.resize(n);
        for (int i = 0; i < n; ++i) {
            const double t = static_cast<double>(i - m) / kPhases;
            const double s = i == m ? 1.0 : std::sin(kPi * t) / (kPi * t);
            h_[i] = s * kaiser(i, n, 5.0);
        }
        for (int p = 1; p < kPhases; ++p) { // unity gain at DC for every phase
            double sum = 0.0;
            for (int j = 0; j < kTapsPerPhase; ++j) sum += h_[kPhases * j + p];
            for (int j = 0; j < kTapsPerPhase; ++j) h_[kPhases * j + p] /= sum;
        }
        hist_.assign(kTapsPerPhase + 1, 0.0);
    }
    static constexpr int latency() { return kTapsPerPhase / 2; }
    void reset() { std::fill(hist_.begin(), hist_.end(), 0.0); }
    inline double process(double x) {
        const int k = static_cast<int>(hist_.size());
        if (++pos_ == k) pos_ = 0;
        hist_[pos_] = x;
        // Unroll the ring so the inner loops run over contiguous memory.
        double lin[kTapsPerPhase + 1];
        int idx = pos_;
        for (int j = 0; j <= kTapsPerPhase; ++j) {
            lin[j] = hist_[idx];
            idx = idx == 0 ? k - 1 : idx - 1;
        }
        double peak = std::fabs(lin[latency()]);
        for (int p = 1; p < kPhases; ++p) {
            double s = 0.0;
            for (int j = 0; j < kTapsPerPhase; ++j) s += h_[kPhases * j + p] * lin[j];
            peak = std::max(peak, std::fabs(s));
        }
        return peak;
    }

private:
    int pos_ = 0;
    std::vector<double> h_, hist_;
};

} // namespace hnl
