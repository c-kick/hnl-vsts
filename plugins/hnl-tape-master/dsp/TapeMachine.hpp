// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// A two-track mastering tape machine.
//
// Signal path (the stages between the two resamplers run at 4x the rate):
//
//   record level -> [4x up] -> record amplifier -> pre-emphasis -> tape
//   saturation -> de-emphasis -> transport (wow and flutter) -> [4x down]
//   -> playback head (head bump, low and high losses) -> hiss
//   -> reproduce level
//
// The model is built from the documented behaviour of professional
// half-track machines and tape stock (level for 3% third harmonic,
// frequency response against speed, noise against track width). It is not
// measured from a particular machine, and the saturation is a memoryless
// curve between pre- and de-emphasis, not a hysteresis simulation.
//
// Levels: a sine at the calibration level (kReferenceDb dBFS unless changed)
// reads 0 VU, and everything the tape does is relative to that.
#pragma once

#include <atomic>
#include <cstdint>

#include "../../../common/dsp/Filters.hpp"

namespace hnl {

constexpr double kReferenceDb = -12.0;

enum TapeSpeed { kSpeed7, kSpeed15, kSpeed30, kSpeedCount };
enum TapeFormula { kVintage, kClassic, kModern, kHighOutput, kFormulaCount };
enum TapeHead { kQuarterInch, kHalfInch, kOneInch, kHeadCount };
enum TapePath { kRepro, kSync, kInput, kPathCount };

struct TapeParams {
    double record = 0.0;    // dB: how hard the tape is driven
    double reproduce = 0.0; // dB: playback level
    bool link = true;       // take the record level off again at the output
    int speed = kSpeed30;
    int formula = kClassic;
    int head = kHalfInch;
    int path = kRepro;
    double bias = 0.0;    // -100 (under) .. +100 (over) percent
    double wow = 10.0;    // percent; 10 is a well-kept machine
    double flutter = 10.0; // percent
    double noise = 0.0;   // percent; 0 off, 50 the level of real tape, 100 twenty dB more
    double calibration = kReferenceDb; // dBFS of a sine that reads 0 VU
};

/** Written by the audio thread, read by the UI thread. */
struct TapeMeters {
    std::atomic<float> vu[2] = {{-60.0f}, {-60.0f}}; // level on tape, dB relative to 0 VU
    std::atomic<float> outPeak[2] = {};              // linear, held until taken
};

/** What a tape formula changes. */
struct FormulaInfo {
    double mol;       // level for 3% third harmonic, dB above 0 VU
    double softness;  // 0 = firm knee .. 1 = soft, early knee
    double highs;     // scales the frequency where the top starts to fall
    double noise;     // hiss, dB below 0 VU, at 15 ips on half-inch
};

inline const FormulaInfo& formulaInfo(int formula) {
    static const FormulaInfo table[kFormulaCount] = {
        {6.0, 1.0, 0.85, -58.0},  // vintage: little headroom, soft, noisy
        {9.0, 0.4, 1.0, -62.0},   // classic
        {12.0, 0.15, 1.1, -65.0}, // modern
        {15.0, 0.0, 1.15, -67.0}, // high output: clean until late, then firm
    };
    return table[std::clamp(formula, 0, kFormulaCount - 1)];
}

inline double speedIps(int speed) { return speed == kSpeed7 ? 7.5 : speed == kSpeed15 ? 15.0 : 30.0; }

/** Delay line read at a fractional, moving position (Catmull-Rom). */
class MovingDelay {
public:
    void setMaxDelay(int samples) {
        size_t n = 16;
        while (n < static_cast<size_t>(samples) + 8) n <<= 1;
        buf_.assign(n, 0.0);
        mask_ = n - 1;
        w_ = 0;
    }
    void reset() { std::fill(buf_.begin(), buf_.end(), 0.0); }
    /** Writes x and returns the signal `delay` samples ago (delay >= 2). */
    inline double process(double x, double delay) {
        w_ = (w_ + 1) & mask_;
        buf_[w_] = x;
        const double whole = std::floor(delay), f = whole + 1.0 - delay; // f: 0..1 towards the newer sample
        const size_t i = w_ - static_cast<size_t>(whole) - 1;            // the older neighbour
        const double ym = buf_[(i - 1) & mask_], y0 = buf_[i & mask_], y1 = buf_[(i + 1) & mask_], y2 = buf_[(i + 2) & mask_];
        const double c1 = 0.5 * (y1 - ym), c2 = ym - 2.5 * y0 + 2.0 * y1 - 0.5 * y2, c3 = 0.5 * (y2 - ym) + 1.5 * (y0 - y1);
        return ((c3 * f + c2) * f + c1) * f + y0;
    }

private:
    std::vector<double> buf_;
    size_t mask_ = 0, w_ = 0;
};

class TapeMachine {
public:
    static constexpr int kOversample = 4;
    static constexpr int kResamplerLatency = 36; // 63- and 23-tap halfbands, up and down

    void prepare(double sampleRate) {
        fs_ = sampleRate;
        transport_ = static_cast<int>(std::lround(0.001 * fs_)); // room for wow: 1 ms
        for (int c = 0; c < 2; ++c) {
            a_[c].design(63, 8.96);
            b_[c].design(23, 8.96);
            delay_[c].setMaxDelay(2 * kOversample * transport_);
        }
        smooth_ = onePoleCoef(20.0, fs_);
        vuCoef_ = onePoleCoef(65.0, fs_);
        driftCoef_ = std::exp(-2.0 * kPi * 30.0 / fs_);
        calibrateNoise();
        configure();
        reset();
    }

    void reset() {
        for (int c = 0; c < 2; ++c) {
            a_[c].reset(); b_[c].reset(); delay_[c].reset();
            vu_[c] = 0.0;
        }
        resetTape();
        rec_ = recT_;
        out_ = outT_;
        lastDelay_ = kOversample * transport_;
        drift_ = 0.0;
    }

    /** Delay through the machine in samples; the same on every path. */
    int latency() const { return kResamplerLatency + transport_; }

    void setParams(const TapeParams& p) { p_ = p; configure(); }
    const TapeParams& params() const { return p_; }

    TapeMeters meters;

    void process(float* left, float* right, int frames) {
        ScopedNoDenormals guard;
        float* io[2] = {left, right};
        const bool tape = p_.path != kInput;
        // On the Input path the tape's filters are not run. Back on the tape they start clean,
        // instead of letting out what they held when the path was switched away.
        if (tape && !tapeWasOn_) resetTape();
        tapeWasOn_ = tape;
        const double base = kOversample * transport_;
        double outPk[2] = {0, 0};

        for (int i = 0; i < frames; ++i) {
            rec_ = recT_ + smooth_ * (rec_ - recT_);
            out_ = outT_ + smooth_ * (out_ - outT_);

            // One transport for both channels: they move together.
            const double delay = tape ? base + transportDeviation() * kOversample * fs_ : base;
            const double step = (delay - lastDelay_) / kOversample;

            for (int c = 0; c < 2; ++c) {
                const double x = io[c][i] * rec_;
                vu_[c] = std::fabs(x) + vuCoef_ * (vu_[c] - std::fabs(x));

                double u0, u1, v[kOversample];
                a_[c].up(x, u0, u1);
                b_[c].up(u0, v[0], v[1]);
                b_[c].up(u1, v[2], v[3]);
                for (int k = 0; k < kOversample; ++k) {
                    double s = v[k] / std::sqrt(1.0 + v[k] * v[k] * ampInv2_); // record amplifier
                    if (tape) s = de_[c].process(magnetise(pre_[c].process(s)));
                    v[k] = delay_[c].process(s, lastDelay_ + step * (k + 1));
                }
                const double d0 = b_[c].down(v[0], v[1]); // in this order: the filters keep state
                const double d1 = b_[c].down(v[2], v[3]);
                double y = a_[c].down(d0, d1);

                if (tape) {
                    for (auto& f : head_[c]) y = f.process(y);
                    if (noiseGain_ > 0.0) y += noiseGain_ * hiss_[c][1].process(hiss_[c][0].process(white(c)));
                }
                y *= out_;
                outPk[c] = std::max(outPk[c], std::fabs(y));
                io[c][i] = static_cast<float>(y);
            }
            lastDelay_ = delay;
        }

        for (int c = 0; c < 2; ++c) {
            // A sine's rectified mean is 2/pi of its peak.
            const double vu = linToDb(vu_[c] * (kPi / 2.0)) - p_.calibration;
            meters.vu[c].store(static_cast<float>(std::max(vu, -60.0)), std::memory_order_relaxed);
            if (static_cast<float>(outPk[c]) > meters.outPeak[c].load(std::memory_order_relaxed))
                meters.outPeak[c].store(static_cast<float>(outPk[c]), std::memory_order_relaxed);
        }
    }

private:
    /** Clears the filters that only run on the tape paths. */
    void resetTape() {
        for (int c = 0; c < 2; ++c) {
            pre_[c].reset(); de_[c].reset();
            for (auto& f : head_[c]) f.reset();
            for (auto& f : hiss_[c]) f.reset();
        }
    }

    /** The tape's transfer curve. x and the result are in linear amplitude. */
    inline double magnetise(double x) const {
        const double u = x * satInv_;
        double y = (1.0 - soft_) * std::tanh(u) + soft_ * u / std::sqrt(1.0 + u * u);
        // Too little bias leaves a kink around zero: quiet signals lose level and gain grit.
        if (under_ > 0.0) y -= under_ * u * std::exp(-44.0 * u * u);
        return y * sat_;
    }

    /** Deviation of the tape's position from steady running, in seconds. */
    inline double transportDeviation() {
        for (int k = 0; k < 4; ++k) {
            phase_[k] += phaseInc_[k];
            if (phase_[k] > 2.0 * kPi) phase_[k] -= 2.0 * kPi;
        }
        drift_ = driftCoef_ * drift_ + (1.0 - driftCoef_) * white(0) * 8.0;
        return wow_ * (0.45e-3 * std::sin(phase_[0]) + 0.15e-3 * std::sin(phase_[1]))
             + flutter_ * (10e-6 * std::sin(phase_[2]) + 3e-6 * std::sin(phase_[3]) + 2e-6 * drift_);
    }

    inline double white(int c) { // xorshift, -1..1
        uint32_t& s = rng_[c];
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return static_cast<double>(static_cast<int32_t>(s)) * (1.0 / 2147483648.0);
    }

    /** Measures the hiss filters so the noise level is the same at every sample rate. */
    void calibrateNoise() {
        for (int c = 0; c < 2; ++c) {
            hiss_[c][0].set(designShelf(fs_, 5000.0, 8.0, true));
            hiss_[c][1].set(designHighpass(fs_, 40.0, 0.7071067811865476));
            hiss_[c][0].reset(); hiss_[c][1].reset();
        }
        double sum = 0.0;
        const int n = 1 << 15;
        for (int i = 0; i < n; ++i) {
            const double v = hiss_[0][1].process(hiss_[0][0].process(white(0)));
            sum += v * v;
        }
        hissRms_ = std::sqrt(sum / n);
    }

    void configure() {
        const FormulaInfo& tape = formulaInfo(p_.formula);
        const double ips = speedIps(p_.speed), bias = std::clamp(p_.bias, -100.0, 100.0) / 100.0;
        const bool sync = p_.path == kSync;
        const double osFs = kOversample * fs_, reference = dbToLin(p_.calibration);

        recT_ = dbToLin(p_.record);
        outT_ = dbToLin(p_.reproduce - (p_.link ? p_.record : 0.0));

        // The record amplifier clips softly 24 dB above 0 VU.
        const double amp = reference * dbToLin(24.0);
        ampInv2_ = 1.0 / (amp * amp);

        // Scale the curve so the third harmonic reaches 3% at the formula's level.
        // For y = x - a x^3 the third harmonic is a x^2 / 4 of the fundamental, so
        // 3% needs a = 0.12 / level^2 (0.136 once the higher terms are counted). The amplifier before the tape already has
        // a = 0.5 / amp^2; the tape supplies the rest.
        soft_ = tape.softness;
        const double cubic = (1.0 - soft_) / 3.0 + soft_ / 2.0;
        const double mol = reference * dbToLin(tape.mol + 1.5 * bias); // more bias, cleaner lows
        satInv_ = std::sqrt((0.136 / (mol * mol) - 0.5 * ampInv2_) / cubic);
        sat_ = 1.0 / satInv_;
        under_ = bias < 0.0 ? -0.2 * bias : 0.0;

        // Slow tape has less room for short wavelengths: the top saturates first.
        const double emphasis = ips <= 7.5 ? 12.0 : ips <= 15.0 ? 7.0 : 3.5;
        const double corner = ips <= 7.5 ? 3200.0 : ips <= 15.0 ? 4500.0 : 6000.0;
        for (int c = 0; c < 2; ++c) {
            pre_[c].setHighShelf(osFs, corner, emphasis);
            de_[c].setInverseOf(pre_[c]);
        }

        // Playback head. The bump and the low roll-off follow the wavelength, so they
        // move up with the speed; narrow tracks and the sync head make them stronger.
        const double width = p_.head == kQuarterInch ? 1.3 : p_.head == kHalfInch ? 1.0 : 0.75;
        const double bumpFreq = 3.6 * ips * (sync ? 0.8 : 1.0);
        const double bumpDb = (2.2 - 0.35 * std::log2(ips / 7.5)) * width * (sync ? 1.3 : 1.0);
        const double topDb = (ips <= 7.5 ? -6.0 : ips <= 15.0 ? -2.0 : -0.5) * (sync ? 2.0 : 1.0);
        const double topFreq = (ips <= 7.5 ? 9000.0 : ips <= 15.0 ? 16000.0 : 18000.0) * tape.highs * (sync ? 0.7 : 1.0);
        const double biasDb = -2.0 * bias * std::sqrt(15.0 / ips); // more bias, duller top
        const BiquadCoeffs filters[kHeadFilters] = {
            designHighpass(fs_, 0.3 * bumpFreq, 0.7071067811865476),
            designPeak(fs_, bumpFreq, 1.2, bumpDb),
            designPeak(fs_, 2.3 * bumpFreq, 1.0, -0.35 * bumpDb),
            designShelf(fs_, topFreq, topDb, true),
            designShelf(fs_, 6000.0, biasDb, true),
        };
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < kHeadFilters; ++k) head_[c][k].set(filters[k]);

        // Transport. The faster the tape, the faster the flutter.
        wow_ = std::clamp(p_.wow, 0.0, 100.0) / 100.0;
        flutter_ = std::clamp(p_.flutter, 0.0, 100.0) / 100.0;
        const double rate = std::sqrt(ips / 15.0);
        const double freqs[4] = {0.55, 1.37, 8.3 * rate, 23.0 * rate};
        for (int k = 0; k < 4; ++k) phaseInc_[k] = 2.0 * kPi * freqs[k] / fs_;

        // Hiss: twice the track width or twice the speed is about 3 dB less.
        if (p_.noise < 0.5) {
            noiseGain_ = 0.0;
        } else {
            const double db = tape.noise + (p_.head == kQuarterInch ? 3.0 : p_.head == kHalfInch ? 0.0 : -3.0)
                            + (ips <= 7.5 ? 3.0 : ips <= 15.0 ? 0.0 : -2.0) + 0.4 * (std::min(p_.noise, 100.0) - 50.0);
            noiseGain_ = reference * 0.7071067811865476 * dbToLin(db) / hissRms_;
        }
    }

    static constexpr int kHeadFilters = 5;

    TapeParams p_;
    double fs_ = 48000.0, smooth_ = 0.0, vuCoef_ = 0.0, driftCoef_ = 0.0;
    int transport_ = 48;
    bool tapeWasOn_ = true;
    double rec_ = 1.0, recT_ = 1.0, out_ = 1.0, outT_ = 1.0;
    double ampInv2_ = 0.0, sat_ = 1.0, satInv_ = 1.0, soft_ = 0.0, under_ = 0.0;
    double wow_ = 0.0, flutter_ = 0.0, phase_[4] = {0.0, 1.0, 2.0, 3.0}, phaseInc_[4] = {}, drift_ = 0.0, lastDelay_ = 0.0;
    double noiseGain_ = 0.0, hissRms_ = 1.0, vu_[2] = {0.0, 0.0};
    uint32_t rng_[2] = {0x2545f491u, 0x9e3779b9u};

    Halfband a_[2], b_[2];
    MovingDelay delay_[2];
    Shelf1 pre_[2], de_[2];
    Biquad head_[2][kHeadFilters], hiss_[2][2];
};

} // namespace hnl
