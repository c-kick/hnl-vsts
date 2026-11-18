// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// HNL Widener: the audio engine.
//
// A stereo signal is a mid signal (left plus right: what a mono speaker
// plays) and a side signal (left minus right: everything that makes it
// wide). This plugin leaves the mid alone and makes side signal, out of the
// mid itself, in three ways that can be mixed:
//
//   in ─► mid ──┬──────────────────────────────────────────────────► + ─► late ─► listen ─► output
//               │                                                    ▲
//               └─► range ─┬─► haas ────┐                            │ (left: plus, right: minus)
//                          ├─► diffuse ─┼─► × amount ─► drift ─► focus
//                          └─► detune ──┘                            │
//         side ─► stereo width, mono bass ───────────────────────────┘
//
//   range     between Widen Above and Widen Below: which part of the sound is widened
//   haas      the mid, a few milliseconds late
//   diffuse   the mid through four allpass filters: every narrow band of pitch lands somewhere else
//   detune    the mid a few cents higher, less the mid a few cents lower
//   drift     shifts all of that up by a fraction of a hertz, so that the picture never stands still
//   focus     turns it down for the first moment of each hit, which then stays in the middle
//   late      Haas the classic way instead: one whole channel late (the only thing here that changes the mono sum)
//   match     if wanted, turns everything down by as much as the width made it louder, for comparing
//
// What is added to the left is taken from the right, so left plus right is
// what went in: on a mono speaker the plugin cannot be heard, whatever the
// settings (Haas in Left Late or Right Late excepted).
//
// Nothing looks ahead and nothing is oversampled: there is no latency.
//
// Header-only C++17, no dependencies beyond common/dsp/Filters.hpp.
#pragma once

#include <atomic>
#include <complex>
#include <cstdint>
#include <iterator>

#include "../../../common/dsp/Filters.hpp"

namespace hnl {

enum WidenerHaasMode { kHaasSafe = 0, kHaasLeft, kHaasRight, kHaasModeCount };
enum WidenerListen { kListenStereo = 0, kListenMono, kListenSides, kListenCount };

constexpr double kHaasMaxMs = 40.0;
/** Widen Below is off at this end of its range. */
constexpr double kWideHighOffHz = 20000.0;
/** Diffuse: the four allpass delays before Size scales them, their feedback, and the scale at Size 0% and 100%. */
constexpr int kDiffuseStages = 4;
constexpr double kDiffuseMs[kDiffuseStages] = {2.1, 3.7, 5.9, 8.3};
constexpr double kDiffuseGain = 0.55, kDiffuseSmallest = 0.25, kDiffuseLargest = 1.5;
/**
 * The part of the diffuser's input that comes out at once, and the gain that makes up for taking it off.
 * Left in, it would be the mid itself on the sides: louder on the left, quieter on the right.
 */
constexpr double kDiffuseDirect = kDiffuseGain * kDiffuseGain * kDiffuseGain * kDiffuseGain;
constexpr double kDiffuseMakeup = 1.0042134; // 1 / sqrt(1 - kDiffuseDirect^2)
/** How long a stage keeps running after its amount reached zero, in ms: until its delay lines are empty. */
constexpr double kHaasTailMs = 45.0, kDiffuseTailMs = 400.0, kDetuneTailMs = 45.0, kDriftTailMs = 60.0;
/** Detune: how late the higher and the lower copy are at least, and the stretch of delay each one keeps running through. */
constexpr double kDetuneBaseMs[2] = {7.0, 11.0}, kDetuneWindowMs = 25.0;
constexpr double kDriftMaxHz = 4.0;
/** Focus: the most the added side signal is turned down on a hit, and the detector's times (as in HNL Thickener's Clean Hit). */
constexpr double kFocusRangeDb = 18.0, kFocusKneeDb = 6.0, kFocusSpanDb = 24.0, kFocusFloorDb = -70.0;
constexpr double kFocusHoldMs = 15.0, kFocusPeakReleaseMs = 30.0, kFocusFastAttackMs = 0.2, kFocusFastReleaseMs = 5.0, kFocusSlowMs = 15.0;
/** The meters compare over this time, and show a side signal against the mid within these limits. */
constexpr double kWideMeterMs = 300.0, kWideMeterLowestDb = -60.0, kWideMeterHighestDb = 24.0;
/** Level Match: the time over which loudness is compared (kWideMeterMs), and the limits of the gain. */
constexpr double kWideMatchLowestDb = -24.0, kWideMatchHighestDb = 12.0;

struct WidenerParams {
    double amount = 100.0;  // percent: Haas, Diffuse and Detune together
    double haas = 0.0;      // percent
    double haasMs = 12.0;   // how late
    int haasMode = kHaasSafe;
    double diffuse = 50.0;  // percent
    double size = 50.0;     // percent: how far the diffuser's reflections lie apart
    double detune = 0.0;    // percent
    double cents = 9.0;     // how far up and down
    double width = 100.0;   // percent: the side signal that came in (100 = unchanged)
    double low = 120.0;     // Hz: nothing is widened below this
    double high = 20000.0;  // Hz: nothing is widened above this (off at 20000)
    bool monoBass = false;  // take the incoming side signal below `low` out
    double focus = 0.0;     // percent: how far the added side signal steps back on a hit
    double drift = 0.0;     // Hz (0 = off)
    int listen = kListenStereo;
    double output = 0.0;    // dB
    bool match = false;     // take the loudness the width added back out
};

inline bool widenHighOn(double high, double fs) { return high < kWideHighOffHz && high < 0.45 * fs; }

/** Length in samples of one of the diffuser's allpass delays. */
inline size_t diffuseLength(int stage, double sizePercent, double fs) {
    const double scale = kDiffuseSmallest * std::pow(kDiffuseLargest / kDiffuseSmallest, std::clamp(sizePercent, 0.0, 100.0) * 0.01);
    return std::max<size_t>(1, static_cast<size_t>(std::lround(kDiffuseMs[stage] * scale * 0.001 * fs)));
}

/**
 * What the plugin does to a steady tone of some pitch that sits in the
 * middle (the same on both channels): its level in the left and the right
 * output and in the two summed to mono, in dB. Detune, Drift and Focus are
 * left out: they move. The UI draws these curves; the tests hold the engine
 * against them.
 */
class WidenerCurve {
public:
    struct Point { double left, right, mono; };

    WidenerCurve(const WidenerParams& p, double sampleRate) : fs_(sampleRate > 0.0 ? sampleRate : 48000.0) {
        const double amount = std::clamp(p.amount, 0.0, 100.0) * 0.01, q = 0.7071067811865476;
        haas_ = amount * std::clamp(p.haas, 0.0, 100.0) * 0.01;
        diffuse_ = amount * std::clamp(p.diffuse, 0.0, 100.0) * 0.01;
        mode_ = std::clamp(p.haasMode, 0, kHaasModeCount - 1);
        listen_ = std::clamp(p.listen, 0, kListenCount - 1);
        delay_ = std::clamp(p.haasMs, 0.0, kHaasMaxMs) * 0.001 * fs_;
        hp_ = designHighpass(fs_, p.low, q);
        lp_ = designLowpass(fs_, p.high, q);
        highOn_ = widenHighOn(p.high, fs_);
        for (int i = 0; i < kDiffuseStages; ++i) len_[i] = static_cast<double>(diffuseLength(i, p.size, fs_));
    }

    Point at(double freq) const {
        typedef std::complex<double> Cx;
        const double w = 2.0 * kPi * std::min(freq, 0.499 * fs_) / fs_;
        const Cx z = std::polar(1.0, -w);
        Cx band = at(hp_, z);
        if (highOn_) band *= at(lp_, z);
        const Cx late = std::polar(1.0, -w * delay_);
        Cx side(0.0, 0.0);
        if (mode_ == kHaasSafe) side += haas_ * late;
        if (diffuse_ > 0.0) {
            Cx all(1.0, 0.0);
            for (double n : len_) {
                const Cx d = std::polar(1.0, -w * n);
                all *= (d - kDiffuseGain) / (1.0 - kDiffuseGain * d);
            }
            side += diffuse_ * kDiffuseMakeup * (all - kDiffuseDirect);
        }
        side *= band;
        Cx l = 1.0 + side, r = 1.0 - side;
        if (mode_ != kHaasSafe) (mode_ == kHaasLeft ? l : r) *= 1.0 + haas_ * band * (late - 1.0);
        const Cx mono = 0.5 * (l + r), sides = 0.5 * (l - r);
        if (listen_ == kListenMono) l = r = mono;
        if (listen_ == kListenSides) return {linToDb(std::abs(sides)), linToDb(std::abs(sides)), linToDb(0.0)};
        return {linToDb(std::abs(l)), linToDb(std::abs(r)), linToDb(std::abs(mono))};
    }

private:
    static std::complex<double> at(const BiquadCoeffs& c, const std::complex<double>& z) {
        return (c.b0 + c.b1 * z + c.b2 * z * z) / (1.0 + c.a1 * z + c.a2 * z * z);
    }

    double fs_, haas_ = 0.0, diffuse_ = 0.0, delay_ = 0.0, len_[kDiffuseStages] = {};
    int mode_ = kHaasSafe, listen_ = kListenStereo;
    BiquadCoeffs hp_, lp_;
    bool highOn_ = false;
};

/** A delay line that is written once and can be read at several places, by fractions of a sample. */
class TapLine {
public:
    void setMax(double samples) {
        buf_.assign(static_cast<size_t>(samples) + 8, 0.0);
        pos_ = 0;
    }
    void reset() { std::fill(buf_.begin(), buf_.end(), 0.0); }
    double longest() const { return static_cast<double>(buf_.size()) - 4.0; }
    inline void push(double x) {
        if (++pos_ == buf_.size()) pos_ = 0;
        buf_[pos_] = x;
    }
    /** The signal `delay` samples before the last one pushed (1 or more; cubic between samples). */
    inline double tap(double delay) const {
        const size_t n = buf_.size(), whole = static_cast<size_t>(delay);
        const double f = delay - static_cast<double>(whole);
        const double y0 = at(whole - 1, n), y1 = at(whole, n), y2 = at(whole + 1, n), y3 = at(whole + 2, n);
        const double c1 = 0.5 * (y2 - y0), c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3, c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }

private:
    inline double at(size_t ago, size_t n) const { return buf_[pos_ >= ago ? pos_ - ago : pos_ + n - ago]; }
    std::vector<double> buf_ = std::vector<double>(8, 0.0);
    size_t pos_ = 0;
};

/**
 * Four allpass filters in a row. Every pitch comes out as loud as it went
 * in, but at a different moment: as a side signal that puts each narrow band
 * of pitch at a place of its own between left and right. The little that
 * comes out at once is taken off again (kDiffuseDirect).
 */
class Diffuser {
public:
    void prepare(double fs) {
        fs_ = fs;
        for (int i = 0; i < kDiffuseStages; ++i) {
            buf_[i].assign(diffuseLength(i, 100.0, fs) + 2, 0.0);
            pos_[i] = 0;
        }
        set(size_);
    }
    void set(double sizePercent) {
        size_ = sizePercent;
        for (int i = 0; i < kDiffuseStages; ++i) len_[i] = std::min(diffuseLength(i, sizePercent, fs_), buf_[i].size() - 1);
    }
    void reset() {
        for (auto& b : buf_) std::fill(b.begin(), b.end(), 0.0);
    }
    inline double process(double x) {
        const double in = x;
        for (int i = 0; i < kDiffuseStages; ++i) {
            std::vector<double>& b = buf_[i];
            const size_t n = b.size(), from = pos_[i] >= len_[i] ? pos_[i] - len_[i] : pos_[i] + n - len_[i];
            const double y = b[from] - kDiffuseGain * x;
            b[pos_[i]] = x + kDiffuseGain * y;
            if (++pos_[i] == n) pos_[i] = 0;
            x = y;
        }
        return kDiffuseMakeup * (x - kDiffuseDirect * in);
    }

private:
    double fs_ = 48000.0, size_ = 50.0;
    std::vector<double> buf_[kDiffuseStages] = {std::vector<double>(2, 0.0), std::vector<double>(2, 0.0), std::vector<double>(2, 0.0), std::vector<double>(2, 0.0)};
    size_t pos_[kDiffuseStages] = {}, len_[kDiffuseStages] = {1, 1, 1, 1};
};

/**
 * Two copies of the signal, one a few cents higher and one as much lower.
 * Each is read from a delay that keeps getting shorter (higher) or longer
 * (lower); before it runs out, a second reading half the stretch away takes
 * over, fading in as the first fades out.
 */
class Detuner {
public:
    void prepare(double fs) {
        fs_ = fs;
        window_ = kDetuneWindowMs * 0.001 * fs;
        line_.setMax((kDetuneBaseMs[1] + kDetuneWindowMs) * 0.001 * fs + 2.0);
        set(cents_);
        reset();
    }
    void set(double cents) {
        cents_ = cents;
        const double ratio = std::pow(2.0, cents / 1200.0);
        upRate_ = (ratio - 1.0) / window_;
        downRate_ = (1.0 - 1.0 / ratio) / window_;
    }
    void reset() {
        line_.reset();
        upPhase_ = 0.0;
        downPhase_ = 0.25;
    }
    inline void process(double x, double& up, double& down) {
        line_.push(x);
        upPhase_ -= upRate_;
        if (upPhase_ < 0.0) upPhase_ += 1.0;
        downPhase_ += downRate_;
        if (downPhase_ >= 1.0) downPhase_ -= 1.0;
        up = read(kDetuneBaseMs[0] * 0.001 * fs_, upPhase_);
        down = read(kDetuneBaseMs[1] * 0.001 * fs_, downPhase_);
    }

private:
    inline double read(double base, double phase) const {
        const double other = phase < 0.5 ? phase + 0.5 : phase - 0.5, s = std::sin(kPi * phase), g = s * s;
        return g * line_.tap(base + window_ * phase) + (1.0 - g) * line_.tap(base + window_ * other);
    }

    double fs_ = 48000.0, window_ = 1200.0, cents_ = 9.0, upRate_ = 0.0, downRate_ = 0.0, upPhase_ = 0.0, downPhase_ = 0.25;
    TapLine line_;
};

/**
 * Two rows of allpass filters whose outputs are a quarter of a wave apart at
 * every pitch from 0.0005 to 0.4995 of the sample rate (the design is Olli
 * Niemitalo's). With those two a signal can be moved up by a fixed number of
 * hertz: turn(x, c, s) with c and s the cosine and sine of an angle that
 * keeps growing.
 */
class QuarterWave {
public:
    void reset() {
        for (Section& s : a_) s.clear();
        for (Section& s : b_) s.clear();
        held_ = 0.0;
    }
    /** `same` is the signal as it is heard when nothing turns; `quarter` lags it by a quarter of a wave. */
    inline void process(double x, double& same, double& quarter) {
        double a = x, b = x;
        for (Section& s : a_) a = s.process(a);
        for (Section& s : b_) b = s.process(b);
        quarter = held_; // this row is one sample late
        held_ = a;
        same = b;
    }

private:
    struct Section {
        double k, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        explicit Section(double root) : k(root * root) {}
        void clear() { x1 = x2 = y1 = y2 = 0; }
        inline double process(double x) {
            const double y = k * (x + y2) - x2;
            x2 = x1;
            x1 = x;
            y2 = y1;
            y1 = y;
            return y;
        }
    };
    Section a_[4] = {Section(0.6923878), Section(0.9360654322959), Section(0.9882295226860), Section(0.9987488452737)};
    Section b_[4] = {Section(0.4021921162426), Section(0.8561710882420), Section(0.9722909545651), Section(0.9952884791278)};
    double held_ = 0.0;
};

class Widener {
public:
    struct Meters {
        std::atomic<float> inPeak[2] = {{0.0f}, {0.0f}}; // linear, largest since last read
        std::atomic<float> outPeak[2] = {{0.0f}, {0.0f}};
        std::atomic<float> correlation{1.0f}; // of the two outputs over the last moment: 1 = the same, 0 = unrelated
        std::atomic<float> sideIn{-60.0f};    // dB: the side signal against the mid, going in
        std::atomic<float> sideOut{-60.0f};   // and coming out
        std::atomic<float> balance{0.0f};     // dB: the right output against the left
        std::atomic<float> match{0.0f};       // dB that Level Match applies right now
    } meters;

    void prepare(double sampleRate) {
        fs_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        haasLine_.setMax(kHaasMaxMs * 0.001 * fs_);
        diffuser_.prepare(fs_);
        detuner_.prepare(fs_);
        smooth_ = 1.0 - onePoleCoef(20.0, fs_);
        glide_ = 1.0 - onePoleCoef(60.0, fs_);
        holdSamples_ = static_cast<int>(std::lround(kFocusHoldMs * 0.001 * fs_));
        peakRelease_ = onePoleCoef(kFocusPeakReleaseMs, fs_);
        fastAttack_ = onePoleCoef(kFocusFastAttackMs, fs_);
        fastRelease_ = onePoleCoef(kFocusFastReleaseMs, fs_);
        slowAttack_ = onePoleCoef(kFocusSlowMs, fs_);
        window_ = 1.0 - onePoleCoef(kWideMeterMs, fs_);
        const double tails[4] = {kHaasTailMs, kDiffuseTailMs, kDetuneTailMs, kDriftTailMs};
        for (int i = 0; i < 4; ++i) tail_[i] = static_cast<int>(std::lround(tails[i] * 0.001 * fs_));
        update();
        reset();
    }

    void setParams(const WidenerParams& p) {
        p_ = p;
        p_.haasMode = std::clamp(p.haasMode, 0, kHaasModeCount - 1);
        p_.listen = std::clamp(p.listen, 0, kListenCount - 1);
        update();
    }

    void reset() {
        resetHaas();
        diffuser_.reset();
        detuner_.reset();
        quarter_.reset();
        for (Biquad* f : {&hpMid_, &lpMid_, &hpSide_, &lpSide_}) f->reset();
        high_ = highTarget_;
        mode_ = p_.haasMode;
        haas_.target = haasTarget_;
        for (Stage* s : {&haas_, &diffuse_, &detune_, &drift_}) s->jump();
        delay_ = delayTarget_;
        amount_ = amountTarget_;
        width_ = widthTarget_;
        bass_ = bassTarget_;
        focus_ = focusTarget_;
        output_ = outputTarget_;
        mid_ = midTarget_;
        side_ = sideTarget_;
        sideRuns_ = width_ > 1.0 || bass_ > 0.0;
        angle_ = 0.0;
        peak_ = 0.0;
        hold_ = 0;
        fast_ = slow_ = kFocusFloorDb;
        inMM_ = inSS_ = outMM_ = outSS_ = sumLR_ = sumLL_ = sumRR_ = 0.0;
        loudIn_ = loudOut_ = 0.0;
        matchGain_ = 1.0;
    }

    void process(float* left, float* right, int frames) {
        ScopedNoDenormals guard;
        double peakIn[2] = {0, 0}, peakOut[2] = {0, 0};
        const double floorLin = dbToLin(kFocusFloorDb);
        for (int i = 0; i < frames; ++i) {
            const double in[2] = {left[i], right[i]};
            peakIn[0] = std::max(peakIn[0], std::fabs(in[0]));
            peakIn[1] = std::max(peakIn[1], std::fabs(in[1]));

            amount_ += smooth_ * (amountTarget_ - amount_);
            width_ += smooth_ * (widthTarget_ - width_);
            bass_ += smooth_ * (bassTarget_ - bass_);
            focus_ += smooth_ * (focusTarget_ - focus_);
            output_ += smooth_ * (outputTarget_ - output_);
            mid_ += smooth_ * (midTarget_ - mid_);
            side_ += smooth_ * (sideTarget_ - side_);
            high_ += smooth_ * (highTarget_ - high_);

            const double m = 0.5 * (in[0] + in[1]), s = 0.5 * (in[0] - in[1]);

            // The hit detector: a fast and a slow envelope of the input in dB; fast above slow marks a hit.
            const double loud = std::max(std::fabs(in[0]), std::fabs(in[1]));
            if (loud >= peak_) {
                peak_ = loud;
                hold_ = holdSamples_;
            } else if (hold_ > 0) {
                --hold_;
            } else {
                peak_ *= peakRelease_;
            }
            const double env = peak_ > floorLin ? linToDb(peak_) : kFocusFloorDb;
            fast_ = env + (env > fast_ ? fastAttack_ : fastRelease_) * (fast_ - env);
            slow_ = fast_ > slow_ ? std::max(fast_ + slowAttack_ * (slow_ - fast_), fast_ - kFocusSpanDb) : fast_;

            // The side signal that came in: narrower or wider, and without its lows if wanted. Widening
            // only raises what lies between Widen Above and Widen Below. While neither is asked for the
            // filters do not run, and they start empty when they come back.
            const bool sideWanted = widthTarget_ > 1.0 || width_ > 1.0 + 1e-6 || bassTarget_ > 0.0 || bass_ > 1e-6;
            if (sideWanted && !sideRuns_) {
                hpSide_.reset();
                lpSide_.reset();
            }
            sideRuns_ = sideWanted;
            double own = width_ * s;
            if (sideRuns_) {
                const double high = hpSide_.process(s), band = high + high_ * (lpSide_.process(high) - high);
                const double base = s + bass_ * (high - s);
                own = std::min(width_, 1.0) * base + std::max(width_ - 1.0, 0.0) * band;
            }

            // What the three makers of side signal get to work on: the mid, within the range to widen.
            // The filter for Widen Below always runs and is faded in and out, so that it never starts cold.
            double source = hpMid_.process(m);
            source += high_ * (lpMid_.process(source) - source);
            double made = 0.0;

            // Each maker's amount is applied to what goes into its delay lines, not to what comes out:
            // the lines then fill and empty smoothly, and a maker that comes on does not click when the
            // start of the sound reaches the end of a line. It runs on until its lines are empty.

            // Haas. A change of mode waits until the old one has faded out.
            bool started = false;
            haas_.target = mode_ == p_.haasMode ? haasTarget_ : 0.0;
            if (!haas_.step(smooth_, tail_[0], started) && mode_ != p_.haasMode) mode_ = p_.haasMode;
            if (started) {
                resetHaas();
                delay_ = delayTarget_;
            }
            if (haas_.runs) {
                delay_ += glide_ * (delayTarget_ - delay_);
                if (mode_ == kHaasSafe) {
                    haasLine_.push(haas_.value * source);
                    made += haasLine_.tap(delay_);
                }
            }

            // Diffuse.
            started = false;
            if (diffuse_.step(smooth_, tail_[1], started)) {
                if (started) diffuser_.reset();
                made += diffuser_.process(diffuse_.value * source);
            }

            // Detune: the higher copy less the lower one.
            started = false;
            if (detune_.step(smooth_, tail_[2], started)) {
                if (started) detuner_.reset();
                double up, down;
                detuner_.process(detune_.value * source, up, down);
                made += 0.7071067811865476 * (up - down);
            }

            made *= amount_;

            // Drift: all of it a fraction of a hertz higher. Against the mid, which stays where it is,
            // every pitch then keeps wandering between left and right.
            started = false;
            if (drift_.step(smooth_, tail_[3], started)) {
                if (started) quarter_.reset();
                double same, quarter;
                quarter_.process(drift_.value * made, same, quarter);
                made = (1.0 - drift_.value) * made + same * std::cos(angle_) - quarter * std::sin(angle_);
                angle_ += driftStep_;
                if (angle_ > kPi) angle_ -= 2.0 * kPi;
            }

            // Focus: it steps back for the first moment of a hit.
            if (focus_ > 1e-6) made *= dbToLin(-focus_ * kFocusRangeDb * (1.0 - std::exp((slow_ - fast_) / kFocusKneeDb)));

            double out[2] = {m + own + made, m - own - made};

            // Haas the classic way: one channel late, within the range to widen.
            if (haas_.runs && mode_ != kHaasSafe) {
                double& late = out[mode_ == kHaasLeft ? 0 : 1];
                double part = hpLate_.process(late);
                part += high_ * (lpLate_.process(part) - part);
                part *= haas_.value;
                haasLine_.push(part);
                late += amount_ * (haasLine_.tap(delay_) - part);
            }

            // Level Match: the loudness of the last moment going in against coming out, both channels
            // together and before Listen, so that listening to a part does not change the gain.
            loudIn_ += window_ * (in[0] * in[0] + in[1] * in[1] - loudIn_);
            loudOut_ += window_ * (out[0] * out[0] + out[1] * out[1] - loudOut_);
            double wanted = 1.0;
            if (match_) wanted = loudIn_ > 1e-9 && loudOut_ > 1e-12 ? std::clamp(std::sqrt(loudIn_ / loudOut_), matchLowest_, matchHighest_) : matchGain_;
            matchGain_ += smooth_ * (wanted - matchGain_);

            // Listen: all of it, only what a mono speaker plays, or only what makes it wide.
            const double om = 0.5 * (out[0] + out[1]), os = 0.5 * (out[0] - out[1]), level = matchGain_ * output_;
            out[0] = (mid_ * om + side_ * os) * level;
            out[1] = (mid_ * om - side_ * os) * level;

            for (int c = 0; c < 2; ++c) peakOut[c] = std::max(peakOut[c], std::fabs(out[c]));
            left[i] = static_cast<float>(out[0]);
            right[i] = static_cast<float>(out[1]);

            const double outM = 0.5 * (out[0] + out[1]), outS = 0.5 * (out[0] - out[1]);
            inMM_ += window_ * (m * m - inMM_);
            inSS_ += window_ * (s * s - inSS_);
            outMM_ += window_ * (outM * outM - outMM_);
            outSS_ += window_ * (outS * outS - outSS_);
            sumLR_ += window_ * (out[0] * out[1] - sumLR_);
            sumLL_ += window_ * (out[0] * out[0] - sumLL_);
            sumRR_ += window_ * (out[1] * out[1] - sumRR_);
        }
        if (peak_ < 1e-12) peak_ = 0.0; // rest at zero instead of running into denormals

        for (int c = 0; c < 2; ++c) {
            raise(meters.inPeak[c], static_cast<float>(peakIn[c]));
            raise(meters.outPeak[c], static_cast<float>(peakOut[c]));
        }
        const double power = std::sqrt(sumLL_ * sumRR_);
        meters.correlation.store(power > 1e-12 ? static_cast<float>(std::clamp(sumLR_ / power, -1.0, 1.0)) : 1.0f, std::memory_order_relaxed);
        meters.sideIn.store(against(inSS_, inMM_, kWideMeterLowestDb), std::memory_order_relaxed);
        meters.sideOut.store(against(outSS_, outMM_, kWideMeterLowestDb), std::memory_order_relaxed);
        meters.balance.store(against(sumRR_, sumLL_, 0.0), std::memory_order_relaxed);
        meters.match.store(static_cast<float>(linToDb(matchGain_)), std::memory_order_relaxed);
    }

private:
    /** An amount that glides to its target, and whether the stage it belongs to has to run. */
    struct Stage {
        double target = 0.0, value = 0.0;
        bool runs = false;
        int rest = 0;
        /**
         * One sample on. `started` is set when the stage comes back from being off; once its amount
         * has reached zero it runs for `tail` samples more.
         */
        inline bool step(double k, int tail, bool& started) {
            value += k * (target - value);
            if (target > 0.0) {
                if (!runs) started = true;
                runs = true;
                rest = tail;
            } else if (runs && value < 1e-4) {
                value = 0.0;
                if (--rest <= 0) runs = false;
            }
            return runs;
        }
        void jump() {
            value = target;
            runs = target > 0.0;
            rest = 0;
        }
    };

    static void raise(std::atomic<float>& meter, float value) {
        if (value > meter.load(std::memory_order_relaxed)) meter.store(value, std::memory_order_relaxed);
    }

    /** One power against another in dB, within the limits of the meters; `silent` when there is nothing to compare. */
    static float against(double a, double b, double silent) {
        if (a + b < 1e-10) return static_cast<float>(silent);
        return static_cast<float>(std::clamp(0.5 * (linToDb(a) - linToDb(b)), kWideMeterLowestDb, kWideMeterHighestDb));
    }

    void resetHaas() {
        haasLine_.reset();
        hpLate_.reset();
        lpLate_.reset();
    }

    void update() {
        const auto part = [](double percent) { return std::clamp(percent, 0.0, 100.0) * 0.01; };
        amountTarget_ = part(p_.amount);
        haasTarget_ = part(p_.haas);
        diffuse_.target = part(p_.diffuse);
        detune_.target = part(p_.detune);
        focusTarget_ = part(p_.focus);
        widthTarget_ = std::clamp(p_.width, 0.0, 200.0) * 0.01;
        bassTarget_ = p_.monoBass ? 1.0 : 0.0;
        outputTarget_ = dbToLin(p_.output);
        match_ = p_.match;
        midTarget_ = p_.listen == kListenSides ? 0.0 : 1.0;
        sideTarget_ = p_.listen == kListenMono ? 0.0 : 1.0;
        delayTarget_ = std::clamp(p_.haasMs * 0.001 * fs_, 1.0, haasLine_.longest());
        highTarget_ = widenHighOn(p_.high, fs_) ? 1.0 : 0.0;
        const double q = 0.7071067811865476;
        for (Biquad* f : {&hpMid_, &hpSide_, &hpLate_}) f->set(designHighpass(fs_, p_.low, q));
        // Switched off, the filters for Widen Below keep the setting they had while they fade out.
        if (highTarget_ > 0.0)
            for (Biquad* f : {&lpMid_, &lpSide_, &lpLate_}) f->set(designLowpass(fs_, p_.high, q));
        diffuser_.set(p_.size);
        detuner_.set(std::clamp(p_.cents, 0.0, 100.0));
        const double drift = std::clamp(p_.drift, 0.0, kDriftMaxHz);
        drift_.target = drift > 0.0 ? 1.0 : 0.0;
        if (drift > 0.0) driftStep_ = 2.0 * kPi * drift / fs_; // kept while it fades out
    }

    WidenerParams p_;
    double fs_ = 48000.0, smooth_ = 1.0, glide_ = 1.0, window_ = 1.0;
    TapLine haasLine_;
    Diffuser diffuser_;
    Detuner detuner_;
    QuarterWave quarter_;
    Biquad hpMid_, lpMid_, hpSide_, lpSide_, hpLate_, lpLate_;
    bool sideRuns_ = false;
    int mode_ = kHaasSafe, tail_[4] = {2160, 19200, 2160, 2880}; // Haas, Diffuse, Detune, Drift
    double highTarget_ = 0.0, high_ = 0.0;
    Stage haas_, diffuse_, detune_, drift_;
    double haasTarget_ = 0.0, delayTarget_ = 576.0, delay_ = 576.0, driftStep_ = 0.0, angle_ = 0.0;
    int holdSamples_ = 720, hold_ = 0;
    double peakRelease_ = 0.0, fastAttack_ = 0.0, fastRelease_ = 0.0, slowAttack_ = 0.0;
    double peak_ = 0.0, fast_ = kFocusFloorDb, slow_ = kFocusFloorDb;
    bool match_ = false;
    double loudIn_ = 0.0, loudOut_ = 0.0, matchGain_ = 1.0, matchLowest_ = dbToLin(kWideMatchLowestDb), matchHighest_ = dbToLin(kWideMatchHighestDb);
    double inMM_ = 0.0, inSS_ = 0.0, outMM_ = 0.0, outSS_ = 0.0, sumLR_ = 0.0, sumLL_ = 0.0, sumRR_ = 0.0;
    double amountTarget_ = 1.0, widthTarget_ = 1.0, bassTarget_ = 0.0, focusTarget_ = 0.0, outputTarget_ = 1.0, midTarget_ = 1.0, sideTarget_ = 1.0;
    double amount_ = 1.0, width_ = 1.0, bass_ = 0.0, focus_ = 0.0, output_ = 1.0, mid_ = 1.0, side_ = 1.0;
};

} // namespace hnl
