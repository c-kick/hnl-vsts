// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// The four processors of the mastering chain: compressor band, exciter,
// true-peak limiter and a BS.1770 loudness meter.
#pragma once

#include "../../../common/dsp/Filters.hpp"
#include "Monitor.hpp"

namespace hnl {

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
 *
 * Three characters:
 *   Tape   smooth, mostly odd harmonics, with the top rolled off
 *   Tube   tanh curve; colour moves it from odd to even harmonics
 *   Retro  adds a rectified part, so a strong second harmonic at any colour
 */
class Exciter {
public:
    enum Mode { kTape, kTube, kRetro };
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
        tapeCoef_ = std::exp(-2.0 * kPi * std::min(9000.0, 0.4 * fs) / fs);
    }
    /** driveDb 0..24, colour 0 (odd harmonics) .. 1 (strong even harmonics). */
    void setDrive(double driveDb, double colour, Mode mode = kTube) {
        mode_ = mode;
        drive_ = dbToLin(driveDb);
        switch (mode) {
        case kTape:
            bias_ = 0.35 * colour;
            biasOut_ = bias_ / std::sqrt(1.0 + bias_ * bias_);
            break;
        case kTube:
            bias_ = 0.6 * colour;
            biasOut_ = std::tanh(bias_);
            break;
        case kRetro:
            bias_ = 0.15 + 0.6 * colour; // amount of the rectified part
            biasOut_ = 0.0;
            break;
        }
    }
    void reset() {
        for (int c = 0; c < 2; ++c) dry_[c].reset();
        resetWet();
    }
    /** Clears everything but the dry signal's delay, which runs whether the exciter is on or not. */
    void resetWet() {
        for (int c = 0; c < 2; ++c) {
            a_[c].reset(); b_[c].reset(); pre_[c].reset(); post_[c].reset();
            tape_[c] = 0.0;
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
        switch (mode_) {
        case kTape:
            for (double& w : v) { const double t = drive_ * w + bias_; w = (t / std::sqrt(1.0 + t * t) - biasOut_) / drive_; }
            break;
        case kTube:
            for (double& w : v) w = (std::tanh(drive_ * w + bias_) - biasOut_) / drive_;
            break;
        case kRetro:
            // sqrt(k^2 + t^2) - k is a full-wave rectifier with a rounded corner.
            for (double& w : v) { const double t = drive_ * w; w = (std::tanh(t) + bias_ * (std::sqrt(kCorner * kCorner + t * t) - kCorner)) / drive_; }
            break;
        }
        const double d0 = b_[ch].down(v[0], v[1]);
        const double d1 = b_[ch].down(v[2], v[3]);
        double h = post_[ch].process(a_[ch].down(d0, d1));
        if (mode_ == kTape) h = tape_[ch] = h + tapeCoef_ * (tape_[ch] - h);
        return dry + mix * h;
    }

private:
    static constexpr double kCorner = 1.0;

    Halfband a_[2], b_[2];
    Delay dry_[2];
    Biquad pre_[2], post_[2];
    Mode mode_ = kTube;
    double drive_ = 1.0, bias_ = 0.0, biasOut_ = 0.0, tapeCoef_ = 0.0, tape_[2] = {0.0, 0.0};
};

// ---------------------------------------------------------------- limiter

/** Minimum over a sliding window (monotonic queue). */
class SlidingMin {
public:
    /** Room for windows up to `w`, so that setWindow() never allocates after it. */
    void reserve(int w) {
        val_.reserve(w + 1);
        idx_.reserve(w + 1);
    }
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
    /** The minimum of what is in the window now; `empty` when nothing was pushed yet. */
    double current(double empty) const { return count_ > 0 ? val_[head_] : empty; }

private:
    int win_ = 1, head_ = 0, count_ = 0;
    long long n_ = 0;
    std::vector<double> val_;
    std::vector<long long> idx_;
};

/** Running mean over a fixed number of samples. */
class BoxAverage {
public:
    /** Room for lengths up to `n`, so that setLength() never allocates after it. */
    void reserve(int n) { buf_.reserve(std::max(n, 1)); }
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
    double lowest() const { return *std::min_element(buf_.begin(), buf_.end()); }

private:
    std::vector<double> buf_;
    size_t pos_ = 0;
    double sum_ = 0.0;
};

/**
 * Limiter with inter-sample (true) peak detection, in one of two modes.
 *
 * Brickwall: look-ahead. The gain is the windowed minimum of the required gain, smoothed by
 * two box filters whose total span is the attack time, so the gain has always fully arrived
 * when the peak leaves the delay line. Nothing passes the ceiling.
 *
 * Soft: no look-ahead. The gain goes down to the required gain at the speed of the attack
 * time and is applied to the sample it was measured on, so what is faster than the attack
 * passes the ceiling, by 12 dB at most (kSoftOver): beyond that it is cut off, so that no
 * setting lets a peak through at any height.
 *
 * The sound is delayed by the longest attack of the brickwall mode (kMaxLookMs) whatever the
 * mode and the attack are: the latency never changes. The gain waits the rest of that time
 * in a delay of its own.
 */
class Limiter {
public:
    static constexpr double kMaxLookMs = 5.0;
    static constexpr double kSoftOver = 3.9810717055349722; // 12 dB

    void prepare(double fs) {
        fs_ = fs;
        lookMax_ = std::max(8, static_cast<int>(std::lround(0.001 * kMaxLookMs * fs)));
        min_.reserve(lookMax_ + 2);
        box1_.reserve(lookMax_);
        box2_.reserve(lookMax_);
        gain_.assign(lookMax_, 1.0);
        for (int c = 0; c < 2; ++c) { delay_[c].setLength(latency()); tp_[c].reset(); }
        glideCoef_ = onePoleCoef(0.5, fs);
        setAttack(attackMs_);
        out_ = 1.0;
        configure(1.0);
        glide_ = 0.0;
    }
    int latency() const { return lookMax_ + TruePeak::latency(); }
    void setCeiling(double db) { ceil_ = dbToLin(db); }
    void setRelease(double ms) { rel_ = onePoleCoef(ms, fs_); }
    /** Brickwall: the time the gain takes to arrive, at most kMaxLookMs. Soft: the attack time. */
    void setAttack(double ms) {
        attackMs_ = ms;
        atk_ = onePoleCoef(ms, fs_);
        lookT_ = std::clamp(static_cast<int>(std::lround(0.001 * std::min(ms, kMaxLookMs) * fs_)), 8, lookMax_);
    }
    void setSoft(bool soft) { softT_ = soft; }
    void reset() { prepare(fs_); }

    /** Processes one stereo sample in place. Returns the applied gain (linear). */
    inline double process(double& l, double& r, bool enabled) {
        if (soft_ != softT_ || (!soft_ && look_ != lookT_)) {
            // Brickwall starts from the lowest gain on its way through, so that no peak still in the
            // delay line is let go; soft from the gain of this moment.
            configure(softT_ ? out_ : pending());
        }
        const double pk = std::max(tp_[0].process(l), tp_[1].process(r));
        const double g = pk > ceil_ ? ceil_ / pk : 1.0;
        // The gain that is needed, let go at the release time: the same in both modes.
        env_ = std::min(g, 1.0 - (1.0 - env_) * rel_);
        double s;
        if (soft_) s = follow_ = env_ < follow_ ? env_ + atk_ * (follow_ - env_) : env_;
        else s = box2_.process(box1_.process(min_.push(env_)));
        if (wait_ > 0) {
            std::swap(s, gain_[gainPos_]);
            if (++gainPos_ == wait_) gainPos_ = 0;
        }
        if (glide_ > 0.0) { // after a change of mode or attack: from the old gain to the new one, not in a step
            s = std::min(1.0, s + glide_);
            glide_ = glide_ > 1e-9 ? glide_ * glideCoef_ : 0.0;
        }
        out_ = s;
        l = delay_[0].process(l);
        r = delay_[1].process(r);
        if (!enabled) return 1.0;
        l *= s;
        r *= s;
        const double top = soft_ ? kSoftOver * ceil_ : ceil_;
        l = std::clamp(l, -top, top);
        r = std::clamp(r, -top, top);
        return s;
    }

private:
    /** Takes the mode and attack that were set, every stage of the gain's way holding `fill`. */
    void configure(double fill) {
        soft_ = softT_;
        look_ = lookT_;
        const int l1 = look_ / 2;
        min_.setWindow(look_ + 2);
        box1_.setLength(l1, fill);
        box2_.setLength(look_ - l1, fill);
        std::fill(gain_.begin(), gain_.end(), fill);
        gainPos_ = 0;
        wait_ = soft_ ? lookMax_ : lookMax_ - look_;
        env_ = follow_ = fill;
        glide_ = std::max(0.0, out_ - fill);
    }
    /** The lowest gain anywhere on its way to the sound. */
    double pending() const {
        double m = std::min(out_, env_);
        for (int i = 0; i < wait_; ++i) m = std::min(m, gain_[i]);
        if (!soft_) m = std::min({m, min_.current(1.0), box1_.lowest(), box2_.lowest()});
        return m;
    }

    double fs_ = 48000.0, ceil_ = 1.0, rel_ = 0.0, atk_ = 0.0, attackMs_ = 3.0, env_ = 1.0, follow_ = 1.0;
    double out_ = 1.0, glide_ = 0.0, glideCoef_ = 0.0;
    bool soft_ = false, softT_ = false;
    int lookMax_ = 240, look_ = 144, lookT_ = 144; // samples: the delay of the sound, and the attack in use and asked for
    int wait_ = 96, gainPos_ = 0;                 // the gain's own delay
    TruePeak tp_[2];
    SlidingMin min_;
    BoxAverage box1_, box2_;
    std::vector<double> gain_;
    Delay delay_[2];
};

// --------------------------------------------------------------- loudness

/** ITU-R BS.1770 loudness: momentary (400 ms), short-term (3 s), integrated. */
class LoudnessMeter {
public:
    void prepare(double fs) {
        for (auto& k : k_) k.prepare(fs);
        blockLen_ = std::max(1, static_cast<int>(std::lround(0.1 * fs)));
        reset();
    }
    void reset() {
        for (auto& k : k_) k.reset();
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
        const double kl = k_[0].process(l), kr = k_[1].process(r);
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

    KWeight k_[2];
    double blocks_[kBlocks] = {};
    unsigned hist_[kBins] = {};
    int blockLen_ = 4800, blockPos_ = 0, blockCount_ = 0, fill_ = 0;
    double acc_ = 0.0;
};

} // namespace hnl
