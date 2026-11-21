// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// What the sampler's editor can say about a recorded sound: which note it is,
// where its hits start, how fast it goes, where a held note can loop, how loud
// its peak is, and a min/max overview for drawing it. Pure functions that run
// on a worker or UI thread, never on the audio thread. Samples that are not
// numbers count as 0. A position these functions return always lies inside
// the sound. Header-only, no dependencies beyond common/dsp.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "../../../common/dsp/Filters.hpp"

namespace hnl {
namespace sampler {

namespace analysis_detail {

/** Sample rates the analysis accepts; outside these the functions find nothing. */
inline bool validRate(double fs) { return isFinite(fs) && fs >= 1000.0 && fs <= 1.0e6; }

/** x[i], or 0 when it is not a number. `x` must have passed through hidden(). */
inline float sampleAt(const float* x, size_t i) {
    uint32_t bits;
    std::memcpy(&bits, x + i, sizeof(bits));
    if ((bits & 0x7f800000u) == 0x7f800000u) return 0.0f;
    float v;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}

/** Frames [from, from + n) of x with everything that is not a number replaced by 0. */
inline std::vector<float> cleanCopy(const float* x, size_t from, size_t n) {
    std::vector<float> out(n, 0.0f);
    if (!x) return out;
    x = hidden(x);
    for (size_t i = 0; i < n; ++i) out[i] = sampleAt(x, from + i);
    return out;
}

/** The mean of left and right (or left alone) over [from, from + n), cleaned. */
inline std::vector<float> monoCopy(const float* left, const float* right, size_t from, size_t n) {
    std::vector<float> out = cleanCopy(left, from, n);
    if (right) {
        right = hidden(right);
        for (size_t i = 0; i < n; ++i) out[i] = 0.5f * (out[i] + sampleAt(right, from + i));
    }
    return out;
}

inline size_t nextPow2(size_t n) {
    size_t p = 1;
    while (p < n) p <<= 1;
    return p;
}

/** In-place complex FFT of a power-of-two size, on separate real and imaginary arrays. The inverse is not scaled. */
class Fft {
public:
    explicit Fft(size_t size) : n_(std::max<size_t>(2, nextPow2(size))), cos_(n_ / 2), sin_(n_ / 2), rev_(n_) {
        for (size_t k = 0; k < n_ / 2; ++k) {
            const double a = -2.0 * kPi * static_cast<double>(k) / static_cast<double>(n_);
            cos_[k] = std::cos(a);
            sin_[k] = std::sin(a);
        }
        size_t bits = 0;
        while ((size_t(1) << bits) < n_) ++bits;
        for (size_t i = 0; i < n_; ++i) {
            size_t r = 0;
            for (size_t b = 0; b < bits; ++b) r |= ((i >> b) & 1u) << (bits - 1 - b);
            rev_[i] = r;
        }
    }
    size_t size() const { return n_; }
    void forward(double* re, double* im) const { run(re, im, 1.0); }
    void inverse(double* re, double* im) const { run(re, im, -1.0); }

private:
    void run(double* re, double* im, double sign) const {
        for (size_t i = 0; i < n_; ++i) {
            const size_t j = rev_[i];
            if (i < j) {
                std::swap(re[i], re[j]);
                std::swap(im[i], im[j]);
            }
        }
        for (size_t len = 2; len <= n_; len <<= 1) {
            const size_t half = len / 2, step = n_ / len;
            for (size_t i = 0; i < n_; i += len)
                for (size_t k = 0; k < half; ++k) {
                    const double wr = cos_[k * step], wi = sign * sin_[k * step];
                    const size_t a = i + k, b = a + half;
                    const double tr = re[b] * wr - im[b] * wi, ti = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - tr;
                    im[b] = im[a] - ti;
                    re[a] += tr;
                    im[a] += ti;
                }
        }
    }
    size_t n_;
    std::vector<double> cos_, sin_;
    std::vector<size_t> rev_;
};

inline bool signChange(float a, float b) { return (a <= 0.0f && b > 0.0f) || (a >= 0.0f && b < 0.0f); }

/** The zero crossing at or before `at` (at most `reach` frames back): the sample of the pair nearer to 0; `at` when there is none. */
inline size_t crossingBefore(const std::vector<float>& x, size_t at, size_t reach) {
    if (x.empty()) return 0;
    at = std::min(at, x.size() - 1);
    const size_t stop = at > reach ? at - reach : 0;
    for (size_t i = at; i > stop && i >= 1; --i)
        if (signChange(x[i - 1], x[i])) return std::fabs(x[i - 1]) < std::fabs(x[i]) ? i - 1 : i;
    return at;
}

// ------------------------------------------------------------ onset curve

/** How much new sound starts in each hop of `size / 4` frames: the positive change of the log spectrum. */
struct OnsetCurve {
    std::vector<double> value; // over all bands
    std::vector<double> body;  // over the bands below 500 Hz: kick and snare, hardly hats
    size_t size = 0, hop = 1;
};

/**
 * Spectral flux: Hann-windowed spectra of about 20 ms every 5 ms, gathered into bands a third of an octave
 * wide (at least one bin), each band's level compressed as log(1 + 1000 * rms), and the sum of the bands'
 * rises since the previous spectrum. Every band counts the same, so a kick in three low bins weighs as much
 * as a hat spread over hundreds. Value m belongs to the spectrum centred on frame m * hop; a spectrum that
 * runs past the end of the sound counts 0, as the end is not a hit. Two real spectra are taken with one
 * complex FFT. Cost: about 2.5 N log2 N per hop, roughly 0.1 s for a minute of sound at 48 kHz.
 */
inline OnsetCurve onsetCurve(const std::vector<float>& x, double fs) {
    OnsetCurve c;
    size_t N = 256;
    while (static_cast<double>(N) < fs * 0.02 && N < 32768) N <<= 1;
    c.size = N;
    c.hop = N / 4;
    const size_t n = x.size(), M = n / c.hop + 1, bins = N / 2;
    c.value.assign(M, 0.0);
    c.body.assign(M, 0.0);
    std::vector<double> w(N);
    double wsum = 0.0;
    for (size_t j = 0; j < N; ++j) {
        w[j] = 0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(j) / static_cast<double>(N));
        wsum += w[j];
    }
    const double gain = 1000.0 * 2.0 / wsum, binHz = fs / static_cast<double>(N);
    std::vector<size_t> edges{1}; // band i holds bins [edges[i], edges[i + 1])
    while (edges.back() <= bins) {
        const size_t k = edges.back();
        edges.push_back(std::min(bins + 1, std::max(k + 1, static_cast<size_t>(std::ceil(static_cast<double>(k) * 1.26)))));
    }
    const size_t bands = edges.size() - 1;
    size_t bodyBands = 0;
    while (bodyBands < bands && static_cast<double>(edges[bodyBands + 1]) * binHz <= 500.0) ++bodyBands;
    Fft fft(N);
    std::vector<double> re(N), im(N), prev(bands, 0.0), a(bands), b(bands);
    auto fill = [&](size_t m, std::vector<double>& out) {
        const long long first = static_cast<long long>(m * c.hop) - static_cast<long long>(N / 2);
        for (size_t j = 0; j < N; ++j) {
            const long long idx = first + static_cast<long long>(j);
            out[j] = (m < M && idx >= 0 && idx < static_cast<long long>(n)) ? w[j] * x[static_cast<size_t>(idx)] : 0.0;
        }
    };
    auto flux = [&](size_t m, const std::vector<double>& now) {
        double all = 0.0, low = 0.0;
        for (size_t i = 0; i < bands; ++i) {
            const double d = now[i] - prev[i];
            if (d > 0.0) {
                all += d;
                if (i < bodyBands) low += d;
            }
        }
        prev = now;
        if (m * c.hop + N / 2 > n) return; // runs past the end
        c.value[m] = all;
        c.body[m] = low;
    };
    std::vector<double> ea(bands), eb(bands);
    for (size_t m = 0; m < M; m += 2) {
        fill(m, re);
        fill(m + 1, im);
        fft.forward(re.data(), im.data());
        std::fill(ea.begin(), ea.end(), 0.0);
        std::fill(eb.begin(), eb.end(), 0.0);
        for (size_t i = 0; i < bands; ++i)
            for (size_t k = edges[i]; k < edges[i + 1]; ++k) {
                const size_t q = (N - k) % N;
                const double zr = re[k], zi = im[k], cr = re[q], ci = -im[q];
                const double ar = 0.5 * (zr + cr), ai = 0.5 * (zi + ci), br = 0.5 * (zi - ci), bi = -0.5 * (zr - cr);
                ea[i] += ar * ar + ai * ai;
                eb[i] += br * br + bi * bi;
            }
        for (size_t i = 0; i < bands; ++i) {
            const double width = static_cast<double>(edges[i + 1] - edges[i]);
            a[i] = std::log(1.0 + gain * std::sqrt(ea[i] / width));
            b[i] = std::log(1.0 + gain * std::sqrt(eb[i] / width));
        }
        flux(m, a);
        if (m + 1 < M) flux(m + 1, b);
    }
    return c;
}

/**
 * Where the rise that a hit makes starts, looking for its peak in [lo, b] and for the quiet before it from a.
 * The energy of the last millisecond is followed back from its peak to where it first rose above a tenth of
 * the way from the quiet to the peak. Done on the sound and on its first difference (which makes hats stand
 * out over a kick's tail); the one where the peak stands out more from the quiet wins.
 */
inline size_t riseStart(const std::vector<float>& x, size_t a, size_t lo, size_t b, double fs) {
    const size_t B = std::max<size_t>(2, static_cast<size_t>(fs * 0.001 + 0.5));
    const size_t base = a > B + 1 ? a - B - 1 : 0, len = b - base;
    std::vector<double> pre(len + 1);
    double bestRatio = -1.0;
    size_t best = lo;
    for (int variant = 0; variant < 2; ++variant) {
        pre[0] = 0.0;
        for (size_t i = 0; i < len; ++i) {
            const size_t g = base + i;
            const double y = variant == 0 ? x[g] : (g > 0 ? static_cast<double>(x[g]) - x[g - 1] : x[g]);
            pre[i + 1] = pre[i] + y * y;
        }
        auto e = [&](size_t i) {
            size_t s = i >= B ? i - B : 0;
            if (s < base) s = base;
            return pre[i - base] - pre[s - base];
        };
        size_t p = lo;
        for (size_t i = lo; i <= b; ++i)
            if (e(i) > e(p)) p = i;
        double quiet = e(p);
        for (size_t i = a; i <= p; ++i) quiet = std::min(quiet, e(i));
        const double top = e(p), ratio = (top + 1e-30) / (quiet + 1e-30 + top * 1e-9);
        const double thr = quiet + 0.1 * (top - quiet);
        size_t i0 = p;
        while (i0 > a && e(i0 - 1) > thr) --i0;
        const size_t start = i0 > 0 ? i0 - 1 : 0;
        if (ratio > bestRatio) {
            bestRatio = ratio;
            best = start;
        }
    }
    return best;
}

// ------------------------------------------------------------------ pitch

struct YinWindow {
    double period = 0;       // in frames, 0 when the window held no pitch
    double aperiodicity = 1; // the normalised difference at that period: 0 = exactly periodic
    bool silent = true;
};

/**
 * The normalised difference function of YIN over windows of W frames, for periods tauMin..tauMax. The
 * difference d(tau) is computed from the energies (running sums) and the correlation (one FFT pair).
 */
class Yin {
public:
    Yin(double fs, size_t available) {
        tauMin_ = std::max<size_t>(3, static_cast<size_t>(fs / 4500.0));
        tauMax_ = static_cast<size_t>(std::ceil(fs / 25.0));
        W_ = tauMax_;
        if (W_ + tauMax_ + 2 > available) {
            tauMax_ = available / 2 > 1 ? available / 2 - 1 : 0;
            W_ = tauMax_;
        }
        ok_ = tauMax_ >= tauMin_ + 4;
        if (!ok_) return;
        fft_.reset(new Fft(W_ + tauMax_ + 1));
        const size_t N = fft_->size();
        ar_.assign(N, 0.0);
        ai_.assign(N, 0.0);
        br_.assign(N, 0.0);
        bi_.assign(N, 0.0);
        seg_.assign(W_ + tauMax_ + 1, 0.0);
        d_.assign(tauMax_ + 1, 0.0);
        dn_.assign(tauMax_ + 1, 1.0);
        cum_.assign(tauMax_ + 1, 0.0);
    }
    bool ok() const { return ok_; }
    size_t length() const { return W_ + tauMax_ + 1; }

    /** Analyse the window starting at frame `start`; x must hold at least start + length() frames. */
    YinWindow analyse(const std::vector<float>& x, size_t start) {
        YinWindow r;
        if (!ok_ || start + length() > x.size()) return r;
        const size_t L = length(), N = fft_->size();
        double mean = 0.0;
        for (size_t j = 0; j < L; ++j) mean += x[start + j];
        mean /= static_cast<double>(L);
        double ea = 0.0, peak = 0.0;
        for (size_t j = 0; j < L; ++j) {
            seg_[j] = x[start + j] - mean;
            peak = std::max(peak, std::fabs(seg_[j]));
        }
        for (size_t j = 0; j < W_; ++j) ea += seg_[j] * seg_[j];
        if (ea < 1e-12 * static_cast<double>(W_) || peak < 1e-6) return r;
        r.silent = false;
        std::fill(ar_.begin(), ar_.end(), 0.0);
        std::fill(ai_.begin(), ai_.end(), 0.0);
        std::fill(br_.begin(), br_.end(), 0.0);
        std::fill(bi_.begin(), bi_.end(), 0.0);
        for (size_t j = 0; j < W_; ++j) ar_[j] = seg_[j];
        for (size_t j = 0; j < L; ++j) br_[j] = seg_[j];
        fft_->forward(ar_.data(), ai_.data());
        fft_->forward(br_.data(), bi_.data());
        for (size_t k = 0; k < N; ++k) { // conj(A) * B
            const double pr = ar_[k] * br_[k] + ai_[k] * bi_[k], pi = ar_[k] * bi_[k] - ai_[k] * br_[k];
            br_[k] = pr;
            bi_[k] = pi;
        }
        fft_->inverse(br_.data(), bi_.data());
        const double scale = 1.0 / static_cast<double>(N);
        double eb = ea; // energy of seg[tau, tau + W)
        d_[0] = 0.0;
        for (size_t tau = 1; tau <= tauMax_; ++tau) {
            eb += seg_[tau + W_ - 1] * seg_[tau + W_ - 1] - seg_[tau - 1] * seg_[tau - 1];
            d_[tau] = std::max(0.0, ea + std::max(0.0, eb) - 2.0 * br_[tau] * scale);
        }
        double sum = 0.0;
        dn_[0] = 1.0;
        cum_[0] = 0.0;
        for (size_t tau = 1; tau <= tauMax_; ++tau) {
            sum += d_[tau];
            cum_[tau] = sum > 1e-12 * ea ? sum : 0.0;
            dn_[tau] = sum > 1e-12 * ea ? d_[tau] * static_cast<double>(tau) / sum : 1.0;
        }
        // The first dip below the threshold, followed down to its bottom; else the deepest dip.
        const double threshold = 0.15;
        size_t tau = 0;
        for (size_t t = tauMin_; t < tauMax_; ++t)
            if (dn_[t] < threshold) {
                while (t + 1 < tauMax_ && dn_[t + 1] < dn_[t]) ++t;
                tau = t;
                break;
            }
        if (tau == 0) {
            tau = tauMin_;
            for (size_t t = tauMin_; t < tauMax_; ++t)
                if (dn_[t] < dn_[tau]) tau = t;
        }
        // The bottom of the dip between whole lags, measured on the window itself.
        double period = 0.0, depth = 0.0;
        bottom(tau, period, depth);
        // Octave guard: a strong upper partial can dip below the threshold at a fraction of the real period;
        // when a whole multiple of the period found is clearly more periodic, that multiple is the period.
        for (size_t k = 2; k <= 4; ++k) {
            const size_t c = static_cast<size_t>(static_cast<double>(k) * period + 0.5);
            if (c + k + 2 >= tauMax_) break;
            size_t m = c - k;
            for (size_t t = c - k; t <= c + k; ++t)
                if (dn_[t] < dn_[m]) m = t;
            double p2, d2;
            bottom(m, p2, d2);
            if (d2 < 0.5 * depth && depth - d2 > 0.02) {
                period = p2;
                depth = d2;
                break;
            }
        }
        r.period = period;
        r.aperiodicity = std::clamp(depth, 0.0, 1.0);
        return r;
    }

private:
    /** The normalised difference at a lag between whole frames, with 4-point interpolation, scaled as at lag `ref`. */
    double dnAt(double tau, size_t ref) const {
        const size_t i = static_cast<size_t>(tau);
        const double f = tau - static_cast<double>(i);
        const double w0 = -f * (f - 1.0) * (f - 2.0) / 6.0, w1 = (f + 1.0) * (f - 1.0) * (f - 2.0) / 2.0;
        const double w2 = -(f + 1.0) * f * (f - 2.0) / 2.0, w3 = (f + 1.0) * f * (f - 1.0) / 6.0;
        double d = 0.0;
        for (size_t j = 0; j < W_; ++j) {
            const double* s = &seg_[j + i - 1];
            const double e = seg_[j] - (w0 * s[0] + w1 * s[1] + w2 * s[2] + w3 * s[3]);
            d += e * e;
        }
        return cum_[ref] > 0.0 ? d * tau / cum_[ref] : 1.0;
    }

    /** The lowest point of the dip around whole lag t, by golden-section search over t - 1..t + 1. */
    void bottom(size_t t, double& period, double& depth) const {
        double a = std::max(2.0, static_cast<double>(t) - 1.0), b = std::min(static_cast<double>(tauMax_) - 2.0, static_cast<double>(t) + 1.0);
        if (!(b > a)) {
            period = static_cast<double>(t);
            depth = dn_[t];
            return;
        }
        const double g = 0.6180339887498949;
        double c = b - g * (b - a), d = a + g * (b - a), fc = dnAt(c, t), fd = dnAt(d, t);
        for (int it = 0; it < 24; ++it) {
            if (fc < fd) {
                b = d;
                d = c;
                fd = fc;
                c = b - g * (b - a);
                fc = dnAt(c, t);
            } else {
                a = c;
                c = d;
                fc = fd;
                d = a + g * (b - a);
                fd = dnAt(d, t);
            }
        }
        period = 0.5 * (a + b);
        depth = std::min(fc, fd);
        if (dn_[t] < depth) { // the whole lag itself was lower: keep it
            period = static_cast<double>(t);
            depth = dn_[t];
        }
    }

    size_t tauMin_ = 0, tauMax_ = 0, W_ = 0;
    bool ok_ = false;
    std::unique_ptr<Fft> fft_;
    std::vector<double> ar_, ai_, br_, bi_, seg_, d_, dn_, cum_;
};

struct PitchFound {
    double hz = 0, confidence = 0;
};

/** A Blackman-Harris windowed DFT of x[s, s + len) at frequency f, as real and imaginary part. */
inline void dftAt(const std::vector<float>& x, size_t s, size_t len, double f, double fs, double& re, double& im) {
    const double w = 2.0 * kPi * f / fs, cw = std::cos(w), sw = std::sin(w), step = 2.0 * kPi / static_cast<double>(len);
    double c = 1.0, sn = 0.0;
    re = im = 0.0;
    for (size_t j = 0; j < len; ++j) {
        const double t = step * static_cast<double>(j);
        const double win = 0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2.0 * t) - 0.01168 * std::cos(3.0 * t);
        re += win * x[s + j] * c;
        im -= win * x[s + j] * sn;
        const double nc = c * cw - sn * sw;
        sn = sn * cw + c * sw;
        c = nc;
    }
}

/**
 * The frequency of the first partial near f, from how far its phase moves between two windows of eight
 * periods that follow each other, the first starting at s (moved back when the sound is too short). That
 * is the pitch a tuner reads even when the upper partials of a string are stretched. 0 when the first partial
 * is more than 30 dB below the strongest of the first four, or the sound is too short.
 */
inline double firstPartial(const std::vector<float>& x, size_t s, double f, double fs) {
    const size_t len = static_cast<size_t>(8.0 * fs / f + 0.5);
    if (len < 16 || 2 * len > x.size()) return 0.0;
    s = std::min(s, x.size() - 2 * len);
    double re, im, top = 0.0, first = 0.0;
    for (int k = 1; k <= 4; ++k) {
        if (k * f >= 0.45 * fs) break;
        dftAt(x, s, len, k * f, fs, re, im);
        const double mag = std::sqrt(re * re + im * im);
        if (k == 1) first = mag;
        top = std::max(top, mag);
    }
    if (!(first > 0.03 * top) || !(first > 1e-9)) return 0.0;
    double re2, im2;
    dftAt(x, s, len, f, fs, re, im);
    dftAt(x, s + len, len, f, fs, re2, im2);
    double turn = std::atan2(im2, re2) - std::atan2(im, re) - 2.0 * kPi * f * static_cast<double>(len) / fs;
    turn -= 2.0 * kPi * std::floor(turn / (2.0 * kPi) + 0.5);
    const double hz = f + turn * fs / (2.0 * kPi * static_cast<double>(len));
    return std::fabs(hz / f - 1.0) < 0.03 ? hz : 0.0;
}

/**
 * YIN on `count` windows spread evenly over [from, to) of x, each refined to its first partial where that is
 * strong enough, and what they agree on: the windows within 60 cents of the median of the periodic ones,
 * averaged in cents. Confidence is the share of sounding windows that agree, times how periodic they are.
 */
inline PitchFound consensusPitch(const std::vector<float>& x, size_t from, size_t to, double fs, int count) {
    PitchFound out;
    to = std::min(to, x.size());
    from = std::min(from, to);
    if (x.empty()) return out;
    Yin yin(fs, x.size());
    if (!yin.ok()) return out;
    const size_t L = yin.length();
    if (to - from < L) { // widen the stretch to one window, inside the sound
        from = to >= L ? to - L : 0;
        if (from + L > x.size()) return out;
        to = from + L;
    }
    const size_t span = to - from - L;
    const int n = span == 0 ? 1 : std::max(1, count);
    std::vector<double> logs, aper;
    int sounding = 0;
    for (int i = 0; i < n; ++i) {
        const size_t s = from + (n == 1 ? 0 : static_cast<size_t>(static_cast<double>(span) * i / (n - 1)));
        const YinWindow w = yin.analyse(x, s);
        if (w.silent) continue;
        ++sounding;
        if (w.period > 0 && w.aperiodicity < 0.35) {
            const double refined = firstPartial(x, s, fs / w.period, fs);
            logs.push_back(std::log2(refined > 0.0 ? refined : fs / w.period));
            aper.push_back(w.aperiodicity);
        }
    }
    if (logs.empty() || sounding == 0) return out;
    std::vector<double> sorted = logs;
    std::sort(sorted.begin(), sorted.end());
    const double median = sorted[sorted.size() / 2];
    double sum = 0.0, apSum = 0.0;
    int agree = 0;
    for (size_t i = 0; i < logs.size(); ++i)
        if (std::fabs(logs[i] - median) < 60.0 / 1200.0) {
            sum += logs[i];
            apSum += aper[i];
            ++agree;
        }
    out.hz = std::pow(2.0, sum / agree);
    out.confidence = std::clamp(static_cast<double>(agree) / sounding * (1.0 - apSum / agree), 0.0, 1.0);
    return out;
}

/** The best-phase mean of the onset curve on a grid of period T (in hops), less the curve's mean. */
struct GridScore {
    double score = -1e300, phase = 0, mid = 0, grid = 0;
    bool valid = false;
};

inline double curveAt(const std::vector<double>& o, double pos) {
    const size_t i = static_cast<size_t>(pos);
    if (i + 1 >= o.size()) return o.empty() ? 0.0 : o.back();
    const double f = pos - static_cast<double>(i);
    return o[i] + f * (o[i + 1] - o[i]);
}

inline GridScore gridScore(const std::vector<double>& o, double mean, double T, double phaseStep) {
    GridScore g;
    const double last = static_cast<double>(o.size() - 1);
    if (!(T > 0.0) || last < T) return g;
    for (double ph = 0.0; ph < T; ph += phaseStep) {
        double sum = 0.0;
        int k = 0;
        for (double p = ph; p <= last; p += T, ++k) sum += curveAt(o, p);
        if (k < 3) continue; // a tempo needs three beats in the sound to be told at all
        const double v = sum / k - mean;
        if (v > g.score) {
            g.score = v;
            g.phase = ph;
            g.grid = sum / k;
            g.valid = true;
        }
    }
    if (!g.valid) return g;
    double sum = 0.0;
    int k = 0;
    for (double p = g.phase + 0.5 * T; p <= last; p += T, ++k) sum += curveAt(o, p);
    g.mid = k > 0 ? sum / k - mean : 0.0;
    return g;
}

} // namespace analysis_detail

// ------------------------------------------------------------------- pitch

struct PitchResult {
    double hz = 0;          // 0 when no pitch was found
    double confidence = 0;  // 0..1
    int key = -1;           // nearest MIDI note (A4 = 69 = 440 Hz)
    double cents = 0;       // how far the sound lies above that note, -50..50
};

/**
 * The pitch of a single note. Looks at the steady part of the sound (after the attack), over several windows, and takes what they agree on. Range about 25 Hz to 4.5 kHz.
 *
 * The attack ends 30 ms after the sound first comes within 3 dB of its loudest 10 ms; from there up to 3 s,
 * until the sound has fallen 26 dB below that, nine windows of 40 ms are each measured with YIN (normalised
 * difference, first dip below 0.15, the dip's bottom found between whole lags, octave guard), then refined to
 * the first partial's own frequency from its phase (see firstPartial), and the windows that agree are
 * averaged. Cost: one pass over at most the first 30 s, nine FFT pairs of 4k to 8k points and a few
 * windowed DFTs; under 20 ms.
 */
inline PitchResult detectPitch(const float* mono, size_t frames, double sampleRate) {
    using namespace analysis_detail;
    PitchResult out;
    if (!mono || frames == 0 || !validRate(sampleRate)) return out;
    const double fs = sampleRate;
    const size_t n = std::min(frames, static_cast<size_t>(30.0 * fs));
    const std::vector<float> x = cleanCopy(mono, 0, n);

    const size_t block = std::max<size_t>(1, static_cast<size_t>(0.01 * fs));
    const size_t blocks = (n + block - 1) / block;
    std::vector<double> env(blocks, 0.0);
    for (size_t b = 0; b < blocks; ++b) {
        double s = 0.0;
        const size_t end = std::min(n, (b + 1) * block);
        for (size_t i = b * block; i < end; ++i) s += static_cast<double>(x[i]) * x[i];
        env[b] = s / static_cast<double>(end - b * block);
    }
    size_t loud = 0;
    for (size_t b = 1; b < blocks; ++b)
        if (env[b] > env[loud]) loud = b;
    if (!(env[loud] > 1e-14)) return out;
    size_t risen = 0; // the first 10 ms within 3 dB of the loudest
    while (risen < loud && env[risen] < 0.5 * env[loud]) ++risen;
    size_t sustainEnd = loud;
    for (size_t b = loud; b < blocks; ++b)
        if (env[b] >= env[loud] * 0.0025) sustainEnd = b; // -26 dB
    const size_t from = std::min(n, (risen + 1) * block + static_cast<size_t>(0.03 * fs));
    const size_t to = std::min(n, std::min((sustainEnd + 1) * block, from + static_cast<size_t>(3.0 * fs)));
    const PitchFound p = consensusPitch(x, std::min(from, to), to, fs, 9);
    if (!(p.hz > 0.0) || !isFinite(p.hz)) return out;
    out.hz = p.hz;
    out.confidence = p.confidence;
    const double semis = 12.0 * std::log2(p.hz / 440.0);
    const double key = std::floor(semis + 0.5);
    out.key = static_cast<int>(std::clamp(69.0 + key, 0.0, 127.0));
    out.cents = std::clamp(100.0 * (semis - key), -50.0, 50.0);
    return out;
}

// --------------------------------------------------------------- transients

/**
 * Where the hits of a drum loop or phrase start, ascending, in frames. sensitivity 0..1: higher finds more.
 * The first frame of the sound that is not silence counts as a hit. Each position is moved back to the start of its rise
 * (and to a zero crossing close before it, at most a few milliseconds), so that a slice does not start with a click or without its attack.
 * minGapSeconds: hits closer together than this count as one.
 *
 * The spectral flux (see onsetCurve) is scaled to its largest value; a hit is a local peak that rises above
 * a threshold of a constant plus a multiple of the curve's median over the surrounding 0.2 s, both lower at a
 * higher sensitivity. Each peak is then placed at the start of its rise in the 1 ms energy, and moved back
 * to a zero crossing at most 1 ms before it. Of hits closer than the minimum gap, the earliest stays.
 * Cost: the onset curve plus a few thousand operations per hit; about 0.1 s per minute of sound at 48 kHz.
 */
inline std::vector<size_t> detectTransients(const float* mono, size_t frames, double sampleRate, double sensitivity,
                                            double minGapSeconds = 0.03) {
    using namespace analysis_detail;
    std::vector<size_t> hits;
    if (!mono || frames == 0 || !validRate(sampleRate)) return hits;
    const double fs = sampleRate;
    const double sens = isFinite(sensitivity) ? std::clamp(sensitivity, 0.0, 1.0) : 0.5;
    const double gapSeconds = isFinite(minGapSeconds) ? std::clamp(minGapSeconds, 0.0, 1.0e6) : 0.03;
    const std::vector<float> x = cleanCopy(mono, 0, frames);

    float peak = 0.0f;
    for (float v : x) peak = std::max(peak, std::fabs(v));
    if (!(peak > 1e-7f)) return hits;
    const float silence = std::max(peak * 0.001f, 1e-7f); // -60 dB below the peak counts as silence
    size_t first = 0;
    while (first < frames && std::fabs(x[first]) <= silence) ++first;
    if (first >= frames) return hits;

    std::vector<size_t> found{first};
    const OnsetCurve c = onsetCurve(x, fs);
    const std::vector<double>& o = c.value;
    const size_t M = o.size();
    double top = 0.0;
    for (double v : o) top = std::max(top, v);
    if (top > 1e-12) {
        const double delta = 0.005 + 0.1 * (1.0 - sens) * (1.0 - sens), lambda = 2.0 + 6.0 * (1.0 - sens);
        const size_t K = std::max<size_t>(1, static_cast<size_t>(0.1 * fs / static_cast<double>(c.hop)));
        std::vector<double> near;
        const size_t N = c.size;
        for (size_t m = 0; m < M; ++m) {
            const double v = o[m] / top;
            const bool peakHere = (m < 1 || o[m] > o[m - 1]) && (m < 2 || o[m] > o[m - 2]) && (m + 1 >= M || o[m] >= o[m + 1]) &&
                                  (m + 2 >= M || o[m] >= o[m + 2]);
            if (!peakHere) continue;
            const size_t a = m > K ? m - K : 0, b = std::min(M, m + K + 1);
            near.assign(o.begin() + static_cast<std::ptrdiff_t>(a), o.begin() + static_cast<std::ptrdiff_t>(b));
            std::nth_element(near.begin(), near.begin() + static_cast<std::ptrdiff_t>(near.size() / 2), near.end());
            const double local = near[near.size() / 2] / top;
            if (v <= delta + lambda * local) continue;
            const size_t centre = m * c.hop;
            const size_t lo = centre > N / 2 ? centre - N / 2 : 0, from = centre > 3 * N / 4 ? centre - 3 * N / 4 : 0;
            const size_t end = std::min(frames - 1, centre + N / 2);
            if (lo > end) continue;
            size_t s = riseStart(x, from, lo, end, fs);
            s = crossingBefore(x, s, std::max<size_t>(1, static_cast<size_t>(0.001 * fs)));
            found.push_back(std::clamp(s, first, frames - 1));
        }
    }
    std::sort(found.begin(), found.end());
    found.erase(std::unique(found.begin(), found.end()), found.end());
    const double gap = gapSeconds * fs;
    for (size_t s : found)
        if (hits.empty() || static_cast<double>(s - hits.back()) >= gap) hits.push_back(s);
    return hits;
}

// -------------------------------------------------------------------- tempo

struct TempoResult {
    double bpm = 0;         // 0 when none was found
    double confidence = 0;  // 0..1
    double beats = 0;       // how many beats the whole sound is long at that tempo
};

/**
 * The tempo of a loop. When the sound is cut as a loop (its length a whole number of beats, preferably of bars of four), that wins over a neighbouring tempo. Range lowBpm..highBpm.
 *
 * Every tempo in the range (in steps of 0.25 bpm, then 0.02 around the best) lays a grid of beats over the
 * smoothed onset curve at its best phase; the tempo whose beats carry the most onset strength on average
 * wins. When the points halfway between its beats carry at least 35 % as much low-frequency onset (kicks and
 * snares, not hats: a backbeat), the double tempo is taken if it is in range. A tempo at which the whole
 * sound is a whole number of beats, within 4 % of that, replaces it when its grid fits nearly as well. A tempo
 * needs three beats in the sound to count. Confidence is the share of beats that carry a clear hit times how
 * far the beats stand out from the rest. Cost: the onset curve of at most the first 120 s plus about 500 grid
 * fits; about 0.1 s per minute of sound at 48 kHz.
 */
inline TempoResult detectTempo(const float* mono, size_t frames, double sampleRate, double lowBpm = 70.0, double highBpm = 180.0) {
    using namespace analysis_detail;
    TempoResult out;
    if (!mono || frames == 0 || !validRate(sampleRate)) return out;
    const double fs = sampleRate;
    double low = isFinite(lowBpm) ? std::clamp(lowBpm, 20.0, 400.0) : 70.0;
    double high = isFinite(highBpm) ? std::clamp(highBpm, 20.0, 400.0) : 180.0;
    if (high < low) std::swap(low, high);

    const size_t n = std::min(frames, static_cast<size_t>(120.0 * fs));
    const std::vector<float> x = cleanCopy(mono, 0, n);
    const OnsetCurve c = onsetCurve(x, fs);
    const size_t M = c.value.size();
    if (M < 8) return out;
    std::vector<double> o(M);
    for (size_t m = 0; m < M; ++m)
        o[m] = 0.25 * (m > 0 ? c.value[m - 1] : 0.0) + 0.5 * c.value[m] + 0.25 * (m + 1 < M ? c.value[m + 1] : 0.0);
    double mean = 0.0, top = 0.0;
    for (double v : o) {
        mean += v;
        top = std::max(top, v);
    }
    mean /= static_cast<double>(M);
    if (!(top > 1e-9)) return out;
    const double hopsPerMinute = 60.0 * fs / static_cast<double>(c.hop);
    auto score = [&](double bpm, double phaseStep) { return gridScore(o, mean, hopsPerMinute / bpm, phaseStep); };

    double best = 0.0;
    GridScore bestScore;
    const double step = std::max(0.25, (high - low) / 2000.0);
    for (double bpm = low; bpm <= high + 1e-9; bpm += step) {
        const GridScore g = score(bpm, 0.5);
        if (g.valid && g.score > bestScore.score) {
            bestScore = g;
            best = bpm;
        }
    }
    if (!bestScore.valid) return out;
    auto refine = [&](double around, GridScore& g) {
        double r = around;
        g = score(around, 0.25);
        for (double bpm = std::max(low, around - 0.3); bpm <= std::min(high, around + 0.3) + 1e-9; bpm += 0.02) {
            const GridScore h = score(bpm, 0.25);
            if (h.valid && h.score > g.score) {
                g = h;
                r = bpm;
            }
        }
        return r;
    };
    best = refine(best, bestScore);
    // A backbeat: kicks and snares (the body curve) halfway between the beats about as strong as on them.
    std::vector<double> body(M);
    double bodyMean = 0.0;
    for (size_t m = 0; m < M; ++m) {
        body[m] = 0.25 * (m > 0 ? c.body[m - 1] : 0.0) + 0.5 * c.body[m] + 0.25 * (m + 1 < M ? c.body[m + 1] : 0.0);
        bodyMean += body[m] / static_cast<double>(M);
    }
    auto backbeat = [&](double bpm, const GridScore& g) {
        const double T = hopsPerMinute / bpm;
        double on = 0.0, mid = 0.0;
        int kOn = 0, kMid = 0;
        for (double p = g.phase; p <= static_cast<double>(M - 1); p += T, ++kOn) on += curveAt(body, p);
        for (double p = g.phase + 0.5 * T; p <= static_cast<double>(M - 1); p += T, ++kMid) mid += curveAt(body, p);
        on = kOn > 0 ? on / kOn - bodyMean : 0.0;
        mid = kMid > 0 ? mid / kMid - bodyMean : 0.0;
        return on > 1e-12 && mid >= 0.35 * on;
    };
    while (best * 2.0 <= high + 1e-9 && backbeat(best, bestScore)) {
        GridScore g;
        const double twice = refine(best * 2.0, g);
        if (!g.valid) break;
        best = twice;
        bestScore = g;
    }
    // The loop-length prior: whole numbers of beats near the tempo found.
    const double minutes = static_cast<double>(frames) / (60.0 * fs);
    const double nLo = std::ceil(minutes * best * 0.96), nHi = std::floor(minutes * best * 1.04);
    double loopBpm = 0.0, loopValue = -1e300;
    GridScore loopScore;
    for (double beats = std::max(1.0, nLo); beats <= nHi && beats <= 100000.0; beats += 1.0) {
        const double bpm = beats / minutes;
        if (bpm < low - 1e-9 || bpm > high + 1e-9) continue;
        const GridScore g = score(bpm, 0.25);
        if (!g.valid) continue;
        const double whole = std::fmod(beats, 4.0) == 0.0 ? 1.0 : (std::fmod(beats, 2.0) == 0.0 ? 0.97 : 0.94);
        if (g.score * whole > loopValue) {
            loopValue = g.score * whole;
            loopBpm = bpm;
            loopScore = g;
        }
    }
    if (loopBpm > 0.0 && loopScore.score >= 0.8 * bestScore.score) {
        best = loopBpm;
        bestScore = loopScore;
    }
    // How sure: the share of beats with a clear hit, times the contrast of the beats with the whole curve.
    const double T = hopsPerMinute / best, level = mean + 0.2 * (top - mean);
    int count = 0, strong = 0;
    for (double p = bestScore.phase; p <= static_cast<double>(M - 1); p += T, ++count)
        if (curveAt(o, p) > level) ++strong;
    const double contrast = bestScore.grid > 1e-12 ? std::clamp(bestScore.score / bestScore.grid, 0.0, 1.0) : 0.0;
    out.confidence = count > 0 ? std::clamp(contrast * strong / count, 0.0, 1.0) : 0.0;
    if (out.confidence < 0.1) return out;
    out.bpm = best;
    out.beats = minutes * best;
    return out;
}

/**
 * Slice points every `division` of a beat (1 = a beat, 0.5 = an eighth, 0.25 = a sixteenth) for a sound of `beats` beats; frame 0 included, the end not.
 * Each point is rounded to the nearest frame. Without a usable number of beats or division the only point is frame 0.
 */
inline std::vector<size_t> beatSlices(size_t frames, double beats, double division) {
    std::vector<size_t> out;
    if (frames == 0) return out;
    out.push_back(0);
    if (!isFinite(beats) || !isFinite(division) || !(beats > 0.0) || !(division > 0.0)) return out;
    const double perSlice = static_cast<double>(frames) * division / beats; // frames per slice
    if (!(perSlice > 0.0) || !isFinite(perSlice)) return out;
    if (perSlice <= 1.0) { // finer than a frame: every frame once
        for (size_t i = 1; i < frames; ++i) out.push_back(i);
        return out;
    }
    const double count = std::min(std::ceil(static_cast<double>(frames) / perSlice), static_cast<double>(frames));
    for (double k = 1.0; k < count; k += 1.0) {
        const double pos = std::floor(k * perSlice + 0.5);
        if (pos >= static_cast<double>(frames)) break;
        const size_t p = static_cast<size_t>(pos);
        if (p > out.back()) out.push_back(p);
    }
    return out;
}

// --------------------------------------------------------------------- loop

struct LoopResult {
    size_t start = 0, end = 0; // end is one past the last frame; both 0 when none was found
    double quality = 0;        // 0..1: how alike the sound is on both sides of the joint
};

/**
 * A place where a held note can loop without a bump: within [from, to) of the sound, a start and an end a whole number of periods apart
 * (as far apart as minSeconds at least), where the waveform matches in value and slope, in both channels when right is not null.
 *
 * The period comes from YIN over the stretch. Loop lengths of the 32 smallest whole numbers of periods that
 * are long enough (each rounded both ways to whole frames) are each slid over the first 10 s of the stretch,
 * comparing two periods on either side of the joint with what the end continues into. The best start is then
 * moved within a period to where value and slope match best across the joint. Quality is 1 minus the root
 * of the normalised difference. Without a pitch, 64 lengths from minSeconds up are tried instead. Cost: about
 * 64 passes over at most 10 s of the stretch; roughly 0.1 s at 48 kHz, stereo.
 */
inline LoopResult findLoop(const float* left, const float* right, size_t frames, double sampleRate, size_t from, size_t to,
                           double minSeconds = 0.25) {
    using namespace analysis_detail;
    LoopResult out;
    if (!left || frames == 0 || !validRate(sampleRate)) return out;
    const double fs = sampleRate;
    to = std::min(to, frames);
    if (from >= to) return out;
    const double minSec = isFinite(minSeconds) ? std::clamp(minSeconds, 0.0, 1.0e6) : 0.25;
    const double minLenD = std::ceil(minSec * fs);
    if (minLenD >= static_cast<double>(to - from)) return out;
    const size_t minLen = std::max<size_t>(16, static_cast<size_t>(minLenD));
    if (minLen + 2 > to - from) return out;

    // The pitch over at most 20 s of the stretch.
    const size_t region = to - from;
    const std::vector<float> m = monoCopy(left, right, from, std::min(region, static_cast<size_t>(20.0 * fs)));
    const PitchFound pitch = consensusPitch(m, 0, m.size(), fs, 9);
    const double P = pitch.hz > 0.0 && pitch.confidence > 0.3 ? fs / pitch.hz : 0.0;
    const size_t w = P > 0.0 ? std::clamp(static_cast<size_t>(2.0 * P + 0.5), size_t(32), static_cast<size_t>(0.05 * fs))
                             : std::max<size_t>(16, static_cast<size_t>(0.005 * fs));

    std::vector<size_t> lengths;
    if (P > 0.0) {
        const double kMin = std::ceil(static_cast<double>(minLen) / P);
        for (int i = 0; i < 32; ++i) {
            const double len = (kMin + i) * P;
            lengths.push_back(static_cast<size_t>(std::floor(len)));
            lengths.push_back(static_cast<size_t>(std::ceil(len)));
        }
    } else {
        const size_t step = std::max<size_t>(1, static_cast<size_t>(0.0005 * fs));
        for (size_t i = 0; i < 64; ++i) lengths.push_back(minLen + i * step);
    }
    std::sort(lengths.begin(), lengths.end());
    lengths.erase(std::unique(lengths.begin(), lengths.end()), lengths.end());

    // The frames the comparisons can touch, cleaned: [lo, hi) of the sound.
    const size_t searchSpan = static_cast<size_t>(10.0 * fs);
    const size_t lo = from > w ? from - w : 0;
    const size_t hi = std::min(frames, to + w);
    const size_t span = hi - lo;
    const std::vector<float> L = cleanCopy(left, lo, span);
    const std::vector<float> R = right ? cleanCopy(right, lo, span) : std::vector<float>();
    std::vector<double> energy(span + 1, 0.0);
    for (size_t i = 0; i < span; ++i)
        energy[i + 1] = energy[i] + static_cast<double>(L[i]) * L[i] + (right ? static_cast<double>(R[i]) * R[i] : 0.0);

    // err(s) over joint window [s - w, s + w): sum of (x[i] - x[i + D])^2, all positions relative to lo.
    auto windowError = [&](size_t s, size_t D, double& diff, double& en) {
        diff = 0.0;
        for (size_t i = s - w; i < s + w; ++i) {
            const double dl = static_cast<double>(L[i]) - L[i + D];
            diff += dl * dl;
            if (right) {
                const double dr = static_cast<double>(R[i]) - R[i + D];
                diff += dr * dr;
            }
        }
        en = energy[s + w] - energy[s - w] + energy[s + D + w] - energy[s + D - w];
    };
    double bestNorm = 1e300;
    size_t bestS = 0, bestD = 0;
    std::vector<double> d2;
    for (size_t D : lengths) {
        // s - w >= 0, s + D + w <= span, s >= from, s + D <= to
        const size_t sLo = std::max(from - lo, w);
        if (D + w > span || sLo + D + w > span || from + D > to) continue;
        const size_t sHi = std::min({span - D - w, to - lo - D, sLo + searchSpan}); // inclusive
        if (sHi < sLo) continue;
        const size_t first = sLo - w, last = sHi + w; // d2 over [first, last)
        d2.assign(last - first + 1, 0.0);
        for (size_t i = first; i < last; ++i) {
            const double dl = static_cast<double>(L[i]) - L[i + D];
            double v = dl * dl;
            if (right) {
                const double dr = static_cast<double>(R[i]) - R[i + D];
                v += dr * dr;
            }
            d2[i - first + 1] = d2[i - first] + v;
        }
        for (size_t s = sLo; s <= sHi; ++s) {
            const double diff = d2[s + w - first] - d2[s - w - first];
            const double en = energy[s + w] - energy[s - w] + energy[s + D + w] - energy[s + D - w];
            const double norm = en > 1e-20 ? diff / en : 2.0;
            if (norm < bestNorm) {
                bestNorm = norm;
                bestS = s;
                bestD = D;
            }
        }
    }
    if (bestD == 0 || bestNorm >= 1e300) return out;
    // Within a period of the best start, the joint whose value and slope match best, as long as the shapes
    // around it still match nearly as well.
    const size_t sLo = std::max(from - lo, w), sHi = std::min(span - bestD - w, to - lo - bestD);
    const size_t reach = P > 0.0 ? static_cast<size_t>(P + 1.0) : w;
    const size_t a = std::max(sLo, bestS > reach ? bestS - reach : 0), b = std::min(sHi, bestS + reach);
    double bestJoint = 1e300;
    size_t joint = bestS;
    for (size_t s = a; s <= b; ++s) {
        double diff, en;
        windowError(s, bestD, diff, en);
        const double norm = en > 1e-20 ? diff / en : 2.0;
        if (norm > 1.25 * bestNorm + 1e-9) continue;
        auto jump = [&](const std::vector<float>& x) {
            const double v = static_cast<double>(x[s]) - x[s + bestD];
            const double sl = (static_cast<double>(x[s]) - x[s - 1]) - (static_cast<double>(x[s + bestD]) - x[s + bestD - 1]);
            return v * v + sl * sl;
        };
        const double j = jump(L) + (right ? jump(R) : 0.0);
        if (j < bestJoint) {
            bestJoint = j;
            joint = s;
        }
    }
    double diff, en;
    windowError(joint, bestD, diff, en);
    const double norm = en > 1e-20 ? diff / en : 2.0;
    out.start = lo + joint;
    out.end = lo + joint + bestD;
    out.quality = std::clamp(1.0 - std::sqrt(std::max(0.0, norm)), 0.0, 1.0);
    return out;
}

// ------------------------------------------------------------ small helpers

/** The largest absolute sample value, and the gain in dB that brings it to `targetDb` (0 = full scale). 0 dB gain for silence. */
inline double peakOf(const float* x, size_t frames) {
    if (!x || frames == 0) return 0.0;
    x = hidden(x);
    float peak = 0.0f;
    for (size_t i = 0; i < frames; ++i) peak = std::max(peak, std::fabs(analysis_detail::sampleAt(x, i)));
    return peak;
}

inline double normaliseGainDb(const float* left, const float* right, size_t frames, double targetDb = 0.0) {
    const double peak = std::max(peakOf(left, frames), peakOf(right, frames));
    if (!(peak > 1e-9)) return 0.0;
    const double target = isFinite(targetDb) ? std::clamp(targetDb, -200.0, 200.0) : 0.0;
    return target - 20.0 * std::log10(peak);
}

/**
 * The nearest zero crossing (sign change) to `at`, looking at most `reach` frames either way; `at` itself when there is none.
 * A crossing lies between two samples; of the two, the one nearer to 0 is returned. `at` is first pulled into the sound.
 */
inline size_t nearestZeroCrossing(const float* mono, size_t frames, size_t at, size_t reach) {
    if (!mono || frames == 0) return 0;
    at = std::min(at, frames - 1);
    mono = hidden(mono);
    reach = std::min(reach, frames);
    auto crossing = [&](size_t i, size_t& where) { // between i - 1 and i
        const float a = analysis_detail::sampleAt(mono, i - 1), b = analysis_detail::sampleAt(mono, i);
        if (!analysis_detail::signChange(a, b)) return false;
        where = std::fabs(a) < std::fabs(b) ? i - 1 : i;
        return true;
    };
    size_t best = at, bestDistance = SIZE_MAX, where = 0;
    for (size_t d = 0; d <= reach; ++d) {
        if (bestDistance != SIZE_MAX && d > bestDistance + 1) break;
        const size_t up = at + d;
        if (up >= 1 && up < frames && crossing(up, where)) {
            const size_t dist = where > at ? where - at : at - where;
            if (dist < bestDistance || (dist == bestDistance && where < best)) {
                bestDistance = dist;
                best = where;
            }
        }
        if (d <= at && at - d >= 1 && crossing(at - d, where)) {
            const size_t dist = where > at ? where - at : at - where;
            if (dist < bestDistance || (dist == bestDistance && where < best)) {
                bestDistance = dist;
                best = where;
            }
        }
    }
    if (bestDistance > reach) return at;
    return best;
}

/**
 * min and max of each of `columns` equal stretches of [from, to), for drawing a waveform: out holds 2*columns floats (min, max, min, max...).
 * A column narrower than a frame shows the frame it falls on. Outside the sound, or for an empty stretch, the
 * columns are 0. Cost: one pass over [from, to).
 */
inline void waveformOverview(const float* x, size_t frames, size_t from, size_t to, int columns, float* out) {
    if (!out || columns <= 0) return;
    const size_t cols = static_cast<size_t>(columns);
    to = std::min(to, frames);
    if (!x || frames == 0 || from >= to) {
        for (size_t c = 0; c < 2 * cols; ++c) out[c] = 0.0f;
        return;
    }
    x = hidden(x);
    const double width = static_cast<double>(to - from) / static_cast<double>(cols);
    for (size_t c = 0; c < cols; ++c) {
        size_t a = from + static_cast<size_t>(width * static_cast<double>(c));
        size_t b = from + static_cast<size_t>(width * static_cast<double>(c + 1));
        a = std::min(a, to - 1);
        b = std::clamp(b, a + 1, to);
        float lo = analysis_detail::sampleAt(x, a), hi = lo;
        for (size_t i = a + 1; i < b; ++i) {
            const float v = analysis_detail::sampleAt(x, i);
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
        out[2 * c] = lo;
        out[2 * c + 1] = hi;
    }
}

} // namespace sampler
} // namespace hnl
