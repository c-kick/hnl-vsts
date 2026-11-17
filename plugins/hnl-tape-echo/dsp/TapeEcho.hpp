// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// A tape echo with three playback heads and a spring reverb.
//
//   input -> preamp -+-> record (+ feedback, + hiss) -> tape loop -> heads 1..3
//                    |        ^                                        |
//                    |        +---- intensity <---- bass / treble <----+--> echo
//                    |                                                 | echo level
//                    +-> spring reverb <-------------------------------+
//                              +-----------------------------------------> reverb
//
// One loop of tape runs past a record head and three playback heads at one,
// two and three times the same distance. The tape is modelled as it moves:
// every recorded sample remembers where on the tape it was written, and a
// head plays what is passing under it now. Changing the speed therefore
// bends the pitch of what is already on the tape, and wow and flutter are
// simply an unsteady speed.
//
// The echo is mono, as on the machines of this kind; the heads can be spread
// over the stereo image on the way out. Built from how such machines are
// known to behave, not measured from one.
#pragma once

#include <atomic>
#include <cstdint>

#include "../../../common/dsp/Filters.hpp"

namespace hnl {

constexpr int kHeads = 3;
constexpr double kTapeLevel = 0.5;      // where the tape runs out of room, linear full scale
constexpr double kMinHeadTime = 0.020;  // seconds between the record head and head 1
constexpr double kMaxHeadTime = 0.500;

struct EchoParams {
    double input = 0.0;      // dB into the preamp
    double time = 0.125;     // seconds from the record head to head 1; heads 2 and 3 are at 2x and 3x
    bool head[kHeads] = {false, true, false};
    double intensity = 35.0; // percent of feedback; howls by itself somewhere past 60
    double echo = 60.0;      // percent
    double reverb = 0.0;     // percent; the springs hear the input and the echo
    double bass = 0.0;       // dB, on the echo
    double treble = 0.0;     // dB, on the echo
    double wobble = 25.0;    // percent of wow and flutter
    double noise = 0.0;      // percent; 0 off, 50 a machine in good order
    double width = 50.0;     // percent: how far heads 1 and 3 and the springs are spread left and right
    bool dry = true;         // pass the input through
};

/** Written by the audio thread, read by the UI thread. */
struct EchoMeters {
    std::atomic<float> tape{-60.0f};     // record level, dB; 0 is where the tape starts to saturate clearly
    std::atomic<float> outPeak[2] = {};  // linear, held until taken
    std::atomic<float> headTime{125.0f}; // milliseconds to head 1 at the speed the tape runs now
};

/** The loop of tape: written at the record head, read wherever a head sits. */
class TapeLoop {
public:
    void prepare(double fs, double maxSeconds) {
        size_t n = 64;
        while (n < static_cast<size_t>(maxSeconds * fs) + 16) n <<= 1;
        sig_.assign(n, 0.0);
        pos_.assign(n, 0.0);
        mask_ = static_cast<int64_t>(n) - 1;
    }

    /** Blank tape that has been running at `speed` (head distances per sample). */
    void reset(double speed) {
        const int64_t n = mask_ + 1;
        w_ = n;
        now_ = 0.0;
        for (int64_t i = 0; i <= w_; ++i) {
            sig_[i & mask_] = 0.0;
            pos_[i & mask_] = (i - w_) * speed;
        }
        for (int k = 0; k < kHeads; ++k) idx_[k] = w_ - 2;
    }

    /** Moves the tape on by `advance` head distances and records x. */
    inline void write(double x, double advance) {
        now_ += advance;
        ++w_;
        sig_[w_ & mask_] = x;
        pos_[w_ & mask_] = now_;
    }

    /** What is under a head `distance` head distances past the record head. */
    inline double read(int k, double distance) {
        const double target = now_ - distance;
        int64_t i = idx_[k];
        const int64_t newest = w_ - 2, oldest = w_ - mask_ + 4;
        if (i < oldest) i = oldest;
        while (i < newest && pos_[(i + 1) & mask_] <= target) ++i;
        idx_[k] = i;
        const double p0 = pos_[i & mask_], p1 = pos_[(i + 1) & mask_];
        double f = p1 > p0 ? (target - p0) / (p1 - p0) : 0.0;
        f = f < 0.0 ? 0.0 : (f > 1.0 ? 1.0 : f);
        const double ym = sig_[(i - 1) & mask_], y0 = sig_[i & mask_], y1 = sig_[(i + 1) & mask_], y2 = sig_[(i + 2) & mask_];
        const double c1 = 0.5 * (y1 - ym), c2 = ym - 2.5 * y0 + 2.0 * y1 - 0.5 * y2, c3 = 0.5 * (y2 - ym) + 1.5 * (y0 - y1);
        return ((c3 * f + c2) * f + c1) * f + y0;
    }

private:
    std::vector<double> sig_, pos_;
    int64_t mask_ = 0, w_ = 0, idx_[kHeads] = {};
    double now_ = 0.0;
};

/**
 * One spring: a long chain of allpass filters that lets the lows through first
 * and the highs later (the "boing"), in a loop with a delay. The allpasses
 * are stretched so the whole sweep lies below the spring's top frequency.
 */
class Spring {
public:
    static constexpr int kSections = 72;

    void prepare(double fs, int stretch, double a, double loopSeconds, double decaySeconds) {
        k_ = stretch;
        a_ = a;
        state_.assign(static_cast<size_t>(kSections) * k_, 0.0);
        at_ = 0;
        line_.setLength(static_cast<size_t>(std::max(1.0, loopSeconds * fs)));
        // The chain adds its own delay: about (1 + a) / (1 - a) samples a section at the top.
        const double chain = kSections * k_ * 0.5 * ((1 + a) / (1 - a) + (1 - a) / (1 + a)) / fs;
        gain_ = std::pow(10.0, -3.0 * (loopSeconds + chain) / decaySeconds);
        damp_ = std::exp(-2.0 * kPi * 6000.0 / fs);
        reset();
    }
    void reset() {
        std::fill(state_.begin(), state_.end(), 0.0);
        line_.reset();
        lp_ = 0.0;
    }
    inline double process(double x) {
        lp_ = line_.peek() + damp_ * (lp_ - line_.peek());
        double s = x + gain_ * lp_;
        double* w = &state_[static_cast<size_t>(at_) * kSections];
        for (int i = 0; i < kSections; ++i) { // w[n] = x[n] - a w[n-K]; y[n] = a w[n] + w[n-K]
            const double old = w[i], v = s - a_ * old;
            w[i] = v;
            s = a_ * v + old;
        }
        if (++at_ == k_) at_ = 0;
        line_.push(s);
        return s;
    }

private:
    std::vector<double> state_; // k_ rows of kSections: the row in use holds the values from k_ samples ago
    Delay line_;
    int k_ = 1, at_ = 0;
    double a_ = 0.6, gain_ = 0.0, damp_ = 0.0, lp_ = 0.0;
};

class TapeEcho {
public:
    void prepare(double sampleRate) {
        fs_ = sampleRate;
        loop_.prepare(fs_, kHeads * kMaxHeadTime * 1.15 + 0.05);
        gainCoef_ = onePoleCoef(20.0, fs_);
        headCoef_ = onePoleCoef(8.0, fs_);
        motorCoef_ = onePoleCoef(280.0, fs_);
        vuCoef_ = onePoleCoef(65.0, fs_);
        toneCoef_ = onePoleCoef(30.0, fs_ / kBlock);
        driftCoef_ = std::exp(-2.0 * kPi * 12.0 / fs_);
        driftGain_ = 12.0 * std::sqrt(fs_ / 48000.0); // the same wander at every sample rate

        pre_.setHighShelf(fs_, 2500.0, 6.0);
        de_.setInverseOf(pre_);
        hissHp_.set(designHighpass(fs_, 400.0, 0.7071067811865476));
        const BiquadCoeffs hp = designHighpass(fs_, 85.0, 0.7071067811865476);
        for (auto& h : headHp_) h.set(hp);

        // Springs: the sweep ends at fs / (2 stretch), a little over 4 kHz.
        const int stretch = std::max(1, static_cast<int>(std::lround(fs_ / 8600.0)));
        const double top = 0.5 * fs_ / stretch;
        static const double kCoef[kSprings] = {0.62, 0.58, 0.65}, kLoop[kSprings] = {0.0293, 0.0371, 0.0437};
        for (int s = 0; s < kSprings; ++s) spring_[s].prepare(fs_, stretch, kCoef[s], kLoop[s], 3.6);
        springHp_.set(designHighpass(fs_, 140.0, 0.7071067811865476));
        for (int c = 0; c < 2; ++c) {
            springLp_[c][0].set(designLowpass(fs_, 0.82 * top, 0.54));
            springLp_[c][1].set(designLowpass(fs_, 0.82 * top, 1.31));
        }
        configure();
        reset();
    }

    void reset() {
        speed_ = speedT_;
        loop_.reset(speed_ / fs_);
        pre_.reset(); de_.reset(); hissHp_.reset(); springHp_.reset();
        for (int k = 0; k < kHeads; ++k) {
            headHp_[k].reset(); headLp_[k].reset(); bass_[k].reset(); treble_[k].reset();
            headGain_[k] = headT_[k];
        }
        for (auto& s : spring_) s.reset();
        for (auto& c : springLp_) for (auto& f : c) f.reset();
        in_ = inT_; fbGain_ = fbT_; echo_ = echoT_; rev_ = revT_; dry_ = dryT_; width_ = widthT_;
        bassNow_ = p_.bass; trebleNow_ = p_.treble;
        feedback_ = 0.0; vu_ = 0.0; drift_ = 0.0; blockLeft_ = 0;
        tonesFor_[0] = -1.0;
    }

    void setParams(const EchoParams& p) { p_ = p; configure(); }
    const EchoParams& params() const { return p_; }

    EchoMeters meters;

    void process(float* left, float* right, int frames) {
        ScopedNoDenormals guard;
        double outPk[2] = {0, 0};

        for (int i = 0; i < frames; ++i) {
            if (blockLeft_ == 0) {
                updateTones();
                blockLeft_ = kBlock;
            }
            --blockLeft_;

            in_ = inT_ + gainCoef_ * (in_ - inT_);
            fbGain_ = fbT_ + gainCoef_ * (fbGain_ - fbT_);
            echo_ = echoT_ + gainCoef_ * (echo_ - echoT_);
            rev_ = revT_ + gainCoef_ * (rev_ - revT_);
            dry_ = dryT_ + gainCoef_ * (dry_ - dryT_);
            width_ = widthT_ + gainCoef_ * (width_ - widthT_);
            speed_ = speedT_ + motorCoef_ * (speed_ - speedT_); // the motor takes a moment

            // Preamp: clips softly 12 dB above the tape's own limit.
            const double x = 0.5 * (static_cast<double>(left[i]) + right[i]) * in_;
            const double amp = x / std::sqrt(1.0 + x * x * kPreampInv2);

            // Record: the highs are lifted going on, so they saturate first, and cut again coming off.
            double rec = amp + feedback_;
            if (noiseGain_ > 0.0) rec += noiseGain_ * hissHp_.process(white());
            vu_ = std::fabs(rec) + vuCoef_ * (vu_ - std::fabs(rec));
            const double onTape = de_.process(kTapeLevel * std::tanh(pre_.process(rec) * (1.0 / kTapeLevel)));
            loop_.write(onTape, speed_ * (1.0 + speedDeviation()) / fs_);

            // Playback heads.
            double sum = 0.0, heads = 0.0, h[kHeads];
            for (int k = 0; k < kHeads; ++k) {
                headGain_[k] = headT_[k] + headCoef_ * (headGain_[k] - headT_[k]);
                double y = loop_.read(k, k + 1.0);
                y = treble_[k].process(bass_[k].process(headLp_[k].process(headHp_[k].process(y))));
                h[k] = y * headGain_[k];
                sum += h[k];
                heads += headGain_[k];
            }
            // More heads feed back more, but not in proportion.
            feedback_ = fbGain_ * sum / std::sqrt(std::max(1.0, heads));

            // Springs, driven by the preamp and by the echo as loud as it is set.
            const double drive = springHp_.process(amp + echo_ * sum);
            const double s0 = spring_[0].process(drive), s1 = spring_[1].process(drive), s2 = spring_[2].process(drive);
            const double rl = s0 + 0.5 * s2, rr = s1 + 0.5 * s2, rm = 0.5 * (rl + rr);
            const double revL = springLp_[0][1].process(springLp_[0][0].process(rm + width_ * (rl - rm)));
            const double revR = springLp_[1][1].process(springLp_[1][0].process(rm + width_ * (rr - rm)));

            // Head 1 leans left and head 3 right.
            const double yl = dry_ * left[i] + echo_ * (sum - width_ * h[2]) + rev_ * revL;
            const double yr = dry_ * right[i] + echo_ * (sum - width_ * h[0]) + rev_ * revR;
            outPk[0] = std::max(outPk[0], std::fabs(yl));
            outPk[1] = std::max(outPk[1], std::fabs(yr));
            left[i] = static_cast<float>(yl);
            right[i] = static_cast<float>(yr);
        }

        // A sine's rectified mean is 2/pi of its peak; 0 on the meter is a sine peaking at half the tape level.
        const double vu = linToDb(vu_ * (kPi / 2.0) / (0.5 * kTapeLevel));
        meters.tape.store(static_cast<float>(std::max(vu, -60.0)), std::memory_order_relaxed);
        meters.headTime.store(static_cast<float>(1000.0 / speed_), std::memory_order_relaxed);
        for (int c = 0; c < 2; ++c)
            if (static_cast<float>(outPk[c]) > meters.outPeak[c].load(std::memory_order_relaxed))
                meters.outPeak[c].store(static_cast<float>(outPk[c]), std::memory_order_relaxed);
    }

private:
    /** How far the tape speed is off, as a fraction: slow wow, faster flutter and some randomness. */
    inline double speedDeviation() {
        if (wobble_ <= 0.0) return 0.0;
        for (int k = 0; k < 4; ++k) {
            phase_[k] += phaseInc_[k];
            if (phase_[k] > 2.0 * kPi) phase_[k] -= 2.0 * kPi;
        }
        drift_ = driftCoef_ * drift_ + (1.0 - driftCoef_) * white() * driftGain_;
        return wobble_ * (0.020 * std::sin(phase_[0]) + 0.010 * std::sin(phase_[1])
                        + 0.0035 * std::sin(phase_[2]) + 0.0015 * std::sin(phase_[3]) + 0.0015 * drift_);
    }

    inline double white() { // xorshift, -1..1
        rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
        return static_cast<double>(static_cast<int32_t>(rng_)) * (1.0 / 2147483648.0);
    }

    /** The filters that follow the tape speed and the tone controls; called every kBlock samples. */
    void updateTones() {
        bassNow_ = p_.bass + toneCoef_ * (bassNow_ - p_.bass);
        trebleNow_ = p_.treble + toneCoef_ * (trebleNow_ - p_.treble);
        if (std::fabs(speed_ - tonesFor_[0]) < 1e-3 && std::fabs(bassNow_ - tonesFor_[1]) < 1e-3 && std::fabs(trebleNow_ - tonesFor_[2]) < 1e-3) return;
        tonesFor_[0] = speed_; tonesFor_[1] = bassNow_; tonesFor_[2] = trebleNow_;
        // Slow tape cannot hold short wavelengths: long echoes are darker.
        const double top = std::clamp(5500.0 * std::pow(speed_ / 8.0, 0.6), 2000.0, 11000.0);
        const BiquadCoeffs lp = designLowpass(fs_, top, 0.62);
        const BiquadCoeffs lo = designShelf(fs_, 200.0, bassNow_, false), hi = designShelf(fs_, 2500.0, trebleNow_, true);
        for (int k = 0; k < kHeads; ++k) {
            headLp_[k].set(lp);
            bass_[k].set(lo);
            treble_[k].set(hi);
        }
    }

    void configure() {
        const auto pct = [](double v) { return std::clamp(v, 0.0, 100.0) / 100.0; };
        inT_ = dbToLin(p_.input);
        speedT_ = 1.0 / std::clamp(p_.time, kMinHeadTime, kMaxHeadTime);
        for (int k = 0; k < kHeads; ++k) headT_[k] = p_.head[k] ? 1.0 : 0.0;
        fbT_ = 1.6 * pct(p_.intensity);
        echoT_ = pct(p_.echo) * pct(p_.echo);
        revT_ = kSpringLevel * pct(p_.reverb) * pct(p_.reverb);
        dryT_ = p_.dry ? 1.0 : 0.0;
        widthT_ = pct(p_.width);
        wobble_ = pct(p_.wobble) * pct(p_.wobble); // fine control at the low end
        static const double kFreqs[4] = {0.43, 1.13, 5.9, 11.7};
        for (int k = 0; k < 4; ++k) phaseInc_[k] = 2.0 * kPi * kFreqs[k] / fs_;
        // At 50% the hiss in the echo is about 60 dB under the tape level, at 100% twenty dB
        // more. The same noise per hertz at every sample rate.
        noiseGain_ = p_.noise < 0.5 ? 0.0 : kTapeLevel * dbToLin(-53.0 + 0.4 * (std::min(p_.noise, 100.0) - 50.0)) * std::sqrt(3.0 * fs_ / 48000.0);
    }

    static constexpr int kSprings = 3, kBlock = 32;
    static constexpr double kPreampInv2 = 1.0 / (4.0 * kTapeLevel * 4.0 * kTapeLevel);
    static constexpr double kSpringLevel = 0.5; // reverb at 100% is about as loud as the input

    EchoParams p_;
    double fs_ = 48000.0;
    double gainCoef_ = 0, headCoef_ = 0, motorCoef_ = 0, vuCoef_ = 0, toneCoef_ = 0, driftCoef_ = 0, driftGain_ = 12.0;
    double in_ = 1, inT_ = 1, fbGain_ = 0, fbT_ = 0, echo_ = 0, echoT_ = 0, rev_ = 0, revT_ = 0, dry_ = 1, dryT_ = 1, width_ = 0, widthT_ = 0;
    double speed_ = 8.0, speedT_ = 8.0; // head distances per second
    double headGain_[kHeads] = {}, headT_[kHeads] = {};
    double bassNow_ = 0, trebleNow_ = 0, tonesFor_[3] = {-1.0, 0.0, 0.0};
    double feedback_ = 0, vu_ = 0, wobble_ = 0, drift_ = 0, noiseGain_ = 0;
    double phase_[4] = {0.0, 1.9, 0.7, 4.1}, phaseInc_[4] = {};
    int blockLeft_ = 0;
    uint32_t rng_ = 0x6d2b79f5u;

    TapeLoop loop_;
    Shelf1 pre_, de_;
    Biquad hissHp_, springHp_, headHp_[kHeads], headLp_[kHeads], bass_[kHeads], treble_[kHeads], springLp_[2][2];
    Spring spring_[kSprings];
};

} // namespace hnl
