// The four processors of the mastering chain: compressor band, exciter,
// true-peak limiter and a BS.1770 loudness meter.
#pragma once

#include "Filters.hpp"

namespace hm {

// ------------------------------------------------------- compressor band

/** Feed-forward compressor gain computer with soft knee, smoothed in dB. */
struct CompBand {
    double threshold = -20.0, ratio = 2.0, knee = 6.0;
    double attackCoef = 0.0, releaseCoef = 0.0;
    double env = 0.0; // current gain reduction in dB (<= 0)

    void setTimes(double attackMs, double releaseMs, double fs) {
        attackCoef = onePoleCoef(attackMs, fs);
        releaseCoef = onePoleCoef(releaseMs, fs);
    }
    void reset() { env = 0.0; }
    /** peak: linked absolute level of the band. Returns gain reduction in dB. */
    inline double process(double peak) {
        const double over = linToDb(peak) - threshold;
        const double slope = 1.0 / ratio - 1.0;
        double gr;
        if (2.0 * over < -knee) gr = 0.0;
        else if (2.0 * over > knee) gr = slope * over;
        else { const double t = over + 0.5 * knee; gr = slope * t * t / (2.0 * knee); }
        const double coef = gr < env ? attackCoef : releaseCoef;
        env = gr + coef * (env - gr);
        return env;
    }
};

// ---------------------------------------------------------------- exciter

/**
 * Harmonic exciter. The band above `freq` is saturated at 4x oversampling and
 * mixed back in. The dry signal is delayed by latency() so both stay aligned.
 */
class Exciter {
public:
    static constexpr int kLatency = 36;

    Exciter() {
        for (int c = 0; c < 2; ++c) {
            a_[c].design(63, 8.96);
            b_[c].design(23, 8.96);
            dry_[c].setLength(kLatency);
        }
    }
    static constexpr int latency() { return kLatency; }
    void setFreq(double fs, double fc) {
        const auto h = designHighpass(fs, fc, 0.7071067811865476);
        for (int c = 0; c < 2; ++c) { pre_[c].set(h); post_[c].set(h); }
    }
    /** driveDb 0..24, colour 0 (odd harmonics) .. 1 (strong even harmonics). */
    void setDrive(double driveDb, double colour) {
        drive_ = dbToLin(driveDb);
        bias_ = 0.6 * colour;
        biasOut_ = std::tanh(bias_);
    }
    void reset() {
        for (int c = 0; c < 2; ++c) {
            a_[c].reset(); b_[c].reset(); dry_[c].reset(); pre_[c].reset(); post_[c].reset();
        }
    }
    /** mix: linear amount of generated signal added (0 = dry only). */
    inline double process(int ch, double x, double mix, bool enabled) {
        const double dry = dry_[ch].process(x);
        if (!enabled) return dry;
        const double s = pre_[ch].process(x);
        double u0, u1, v[4];
        a_[ch].up(s, u0, u1);
        b_[ch].up(u0, v[0], v[1]);
        b_[ch].up(u1, v[2], v[3]);
        for (double& w : v) w = (std::tanh(drive_ * w + bias_) - biasOut_) / drive_;
        const double d0 = b_[ch].down(v[0], v[1]);
        const double d1 = b_[ch].down(v[2], v[3]);
        const double h = post_[ch].process(a_[ch].down(d0, d1));
        return dry + mix * h;
    }

private:
    Halfband a_[2], b_[2];
    Delay dry_[2];
    Biquad pre_[2], post_[2];
    double drive_ = 1.0, bias_ = 0.0, biasOut_ = 0.0;
};

// ---------------------------------------------------------------- limiter

/** Minimum over a sliding window (monotonic queue). */
class SlidingMin {
public:
    void setWindow(int w) {
        win_ = w;
        val_.assign(w + 1, 0.0);
        idx_.assign(w + 1, 0);
        head_ = count_ = 0;
        n_ = 0;
    }
    inline double push(double x) {
        const int cap = static_cast<int>(val_.size());
        while (count_ > 0 && val_[(head_ + count_ - 1) % cap] >= x) --count_;
        const int tail = (head_ + count_) % cap;
        val_[tail] = x;
        idx_[tail] = n_;
        ++count_;
        if (idx_[head_] <= n_ - win_) { head_ = (head_ + 1) % cap; --count_; }
        ++n_;
        return val_[head_];
    }

private:
    int win_ = 1, head_ = 0, count_ = 0;
    long long n_ = 0;
    std::vector<double> val_;
    std::vector<long long> idx_;
};

/** Running mean over a fixed number of samples. */
class BoxAverage {
public:
    void setLength(int n, double fill) {
        buf_.assign(std::max(n, 1), fill);
        pos_ = 0;
        sum_ = fill * buf_.size();
    }
    inline double process(double x) {
        sum_ += x - buf_[pos_];
        buf_[pos_] = x;
        if (++pos_ == buf_.size()) {
            pos_ = 0;
            sum_ = 0.0; // re-sum once per cycle so rounding never accumulates
            for (double v : buf_) sum_ += v;
        }
        return sum_ / buf_.size();
    }

private:
    std::vector<double> buf_;
    size_t pos_ = 0;
    double sum_ = 0.0;
};

/**
 * Look-ahead brickwall limiter with inter-sample (true) peak detection.
 * The gain is the windowed minimum of the required gain, smoothed by two box
 * filters whose total span stays inside the look-ahead, so the gain has always
 * fully arrived when the peak leaves the delay line.
 */
class Limiter {
public:
    void prepare(double fs) {
        fs_ = fs;
        look_ = std::max(8, static_cast<int>(std::lround(0.003 * fs)));
        const int l1 = look_ / 2;
        min_.setWindow(look_ + 2);
        box1_.setLength(l1, 1.0);
        box2_.setLength(look_ - l1, 1.0);
        for (int c = 0; c < 2; ++c) { delay_[c].setLength(latency()); tp_[c].reset(); }
        env_ = 1.0;
    }
    int latency() const { return look_ + TruePeak::latency(); }
    void setCeiling(double db) { ceil_ = dbToLin(db); }
    void setRelease(double ms) { rel_ = onePoleCoef(ms, fs_); }
    void reset() { prepare(fs_); }

    /** Processes one stereo sample in place. Returns the applied gain (linear). */
    inline double process(double& l, double& r, bool enabled) {
        const double pk = std::max(tp_[0].process(l), tp_[1].process(r));
        const double g = pk > ceil_ ? ceil_ / pk : 1.0;
        env_ = std::min(g, 1.0 - (1.0 - env_) * rel_);
        const double s = box2_.process(box1_.process(min_.push(env_)));
        l = delay_[0].process(l);
        r = delay_[1].process(r);
        if (!enabled) return 1.0;
        l = std::clamp(l * s, -ceil_, ceil_);
        r = std::clamp(r * s, -ceil_, ceil_);
        return s;
    }

private:
    double fs_ = 48000.0, ceil_ = 1.0, rel_ = 0.0, env_ = 1.0;
    int look_ = 144;
    TruePeak tp_[2];
    SlidingMin min_;
    BoxAverage box1_, box2_;
    Delay delay_[2];
};

// --------------------------------------------------------------- loudness

/** ITU-R BS.1770 loudness: momentary (400 ms), short-term (3 s), integrated. */
class LoudnessMeter {
public:
    void prepare(double fs) {
        // K-weighting, re-derived for the sample rate (as in libebur128).
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan(kPi * f0 / fs), Vh = std::pow(10.0, G / 20.0);
            const double Vb = std::pow(Vh, 0.4996667741545416), a0 = 1.0 + K / Q + K * K;
            const BiquadCoeffs c{(Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0,
                                 (Vh - Vb * K / Q + K * K) / a0, 2.0 * (K * K - 1.0) / a0,
                                 (1.0 - K / Q + K * K) / a0};
            shelf_[0].set(c); shelf_[1].set(c);
        }
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan(kPi * f0 / fs), a0 = 1.0 + K / Q + K * K;
            const BiquadCoeffs c{1.0, -2.0, 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0};
            hp_[0].set(c); hp_[1].set(c);
        }
        blockLen_ = std::max(1, static_cast<int>(std::lround(0.1 * fs)));
        reset();
    }
    void reset() {
        for (int c = 0; c < 2; ++c) { shelf_[c].reset(); hp_[c].reset(); }
        std::fill(std::begin(blocks_), std::end(blocks_), 0.0);
        blockPos_ = blockCount_ = fill_ = 0;
        acc_ = 0.0;
        momentary = shortTerm = integrated = kSilence;
        resetIntegrated();
    }
    void resetIntegrated() {
        std::fill(std::begin(hist_), std::end(hist_), 0u);
        integrated = kSilence;
    }
    inline void process(double l, double r) {
        const double kl = hp_[0].process(shelf_[0].process(l));
        const double kr = hp_[1].process(shelf_[1].process(r));
        acc_ += kl * kl + kr * kr;
        if (++fill_ == blockLen_) endBlock();
    }

    static constexpr double kSilence = -100.0;
    double momentary = kSilence, shortTerm = kSilence, integrated = kSilence;

private:
    static constexpr int kBlocks = 30, kBins = 750; // 0.1 LU bins from -70 LUFS
    static double toLufs(double ms) { return ms > 1e-12 ? -0.691 + 10.0 * std::log10(ms) : kSilence; }
    static double binEnergy(int b) { return std::pow(10.0, (-70.0 + 0.1 * (b + 0.5) + 0.691) / 10.0); }

    double meanOfLast(int n) const {
        n = std::min(n, blockCount_);
        if (n == 0) return 0.0;
        double s = 0.0;
        for (int i = 0; i < n; ++i) s += blocks_[(blockPos_ - 1 - i + 2 * kBlocks) % kBlocks];
        return s / n;
    }
    void endBlock() {
        blocks_[blockPos_] = acc_ / blockLen_;
        blockPos_ = (blockPos_ + 1) % kBlocks;
        blockCount_ = std::min(blockCount_ + 1, kBlocks);
        acc_ = 0.0;
        fill_ = 0;
        momentary = toLufs(meanOfLast(4));
        shortTerm = toLufs(meanOfLast(kBlocks));
        if (blockCount_ < 4) return;
        const int bin = static_cast<int>(std::floor((momentary + 70.0) * 10.0));
        if (bin >= 0) ++hist_[std::min(bin, kBins - 1)];
        // Two-stage gating: absolute (-70 LUFS, implied by the histogram range),
        // then relative (-10 LU below the absolutely-gated mean).
        double sum = 0.0, cnt = 0.0;
        for (int b = 0; b < kBins; ++b) { sum += hist_[b] * binEnergy(b); cnt += hist_[b]; }
        if (cnt == 0.0) { integrated = kSilence; return; }
        const double gate = toLufs(sum / cnt) - 10.0;
        const int first = std::max(0, static_cast<int>(std::ceil((gate + 70.0) * 10.0 - 0.5)));
        sum = cnt = 0.0;
        for (int b = first; b < kBins; ++b) { sum += hist_[b] * binEnergy(b); cnt += hist_[b]; }
        integrated = cnt > 0.0 ? toLufs(sum / cnt) : kSilence;
    }

    Biquad shelf_[2], hp_[2];
    double blocks_[kBlocks] = {};
    unsigned hist_[kBins] = {};
    int blockLen_ = 4800, blockPos_ = 0, blockCount_ = 0, fill_ = 0;
    double acc_ = 0.0;
};

} // namespace hm
