// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// HNL Channel Spacer: makes room in one signal for another, per frequency band.
//
// The ducker (the sidechain) is split into bands and its level followed per
// band. Where it is loud, the same band is turned down in the main signal.
//
// The cut is made by subtraction: output = input - sum(amount[k] * bandpass[k](input)).
// The filters never change, only the amounts, so there is nothing to hear
// when amounts move, and with all amounts at zero the output is the input,
// bit for bit. The bandpasses overlap so that their sum is flat: equal
// amounts in every band turn the whole signal down by that amount.
#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>

#include "../../../common/dsp/Filters.hpp"
#include "../Params.hpp"

namespace hnl {

struct SpacerParams {
    double sensitivity = 50.0; // %
    double depth = 9.0;        // dB
    double attack = 10.0;      // ms
    double release = 150.0;    // ms
    double range[4] = {100.0, 100.0, 100.0, 100.0}; // %: low, low mid, high mid, high
    double link = 0.0;         // %: 0 = every band by itself, 100 = all bands follow the whole ducker
    bool self = false;         // the main signal ducks itself
    int listen = kListenOutput;
};

class Spacer {
public:
    struct Meters {
        std::atomic<float> cut[kBands];   // dB, positive
        std::atomic<float> level[kBands]; // dB
        std::atomic<float> outPeak[2];    // linear, since last read
        Meters() {
            for (int k = 0; k < kBands; ++k) { cut[k].store(0.0f); level[k].store(-120.0f); }
            outPeak[0].store(0.0f);
            outPeak[1].store(0.0f);
        }
    } meters;

    void prepare(double sampleRate) {
        fs_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        const double spacing = std::log(kBandHigh / kBandLow) / (kBands - 1);
        const double carveQ = 1.0 / (2.0 * kCarveSpread * spacing);
        for (int k = 0; k < kBands; ++k) {
            const double f = clampFreq(fs_, bandFreq(k));
            const BiquadCoeffs carve = bandpass(f, carveQ), detect = bandpass(f, kDetectQ);
            for (int c = 0; c < 2; ++c) carve_[c][k].set(carve);
            for (int s = 0; s < 2; ++s) detect_[s][k].set(detect);
            // The level follower needs a few cycles to be free of ripple.
            const double ms = std::max(1.0, 3000.0 / (2.0 * kPi * f));
            follow_[k] = 1.0 - onePoleCoef(ms, fs_);
        }
        capCoef_[0] = 1.0 - std::exp(-2.0 * kPi * 0.7 * kBandLow / fs_);
        capCoef_[1] = 1.0 - std::exp(-2.0 * kPi * std::min(1.4 * kBandHigh, 0.47 * fs_) / fs_);
        solveWeights();
        // A sine reads this much too high when the levels of all bands are added.
        totalNorm_ = kPi / (4.0 * kDetectQ * spacing);
        setParams(params_);
        reset();
    }

    void reset() {
        for (int k = 0; k < kBands; ++k) {
            for (int c = 0; c < 2; ++c) { carve_[c][k].reset(); detect_[c][k].reset(); }
            power_[k] = 0.0;
            cut_[k] = amount_[k] = 0.0;
        }
        capState_[0][0] = capState_[0][1] = capState_[1][0] = capState_[1][1] = 0.0;
        phase_ = 0;
    }

    void setParams(const SpacerParams& p) {
        params_ = p;
        threshold_ = thresholdDb(p.sensitivity);
        for (int k = 0; k < kBands; ++k) depth_[k] = p.depth * rangeAmount(bandFreq(k), p.range);
        // Attack and release are the time to cover about two thirds of the way.
        const double step = 1000.0 * kStep / fs_;
        attack_ = 1.0 - std::exp(-step / std::max(0.05, p.attack));
        release_ = 1.0 - std::exp(-step / std::max(1.0, p.release));
    }

    /** In place on l and r. scl and scr are the ducker; they may be null (then it is silent). */
    void process(float* l, float* r, const float* scl, const float* scr, int frames) {
        ScopedNoDenormals guard;
        float peak[2] = {0.0f, 0.0f};
        int done = 0;
        while (done < frames) {
            if (phase_ == 0) control();
            const int n = std::min(frames - done, kStep - phase_);
            chunk(l + done, r + done, scl ? scl + done : nullptr, scr ? scr + done : nullptr, n, peak);
            phase_ = (phase_ + n) % kStep;
            done += n;
        }
        for (int k = 0; k < kBands; ++k) {
            meters.cut[k].store(static_cast<float>(cut_[k]), std::memory_order_relaxed);
            meters.level[k].store(static_cast<float>(levelDb(k)), std::memory_order_relaxed);
        }
        for (int c = 0; c < 2; ++c)
            if (peak[c] > meters.outPeak[c].load(std::memory_order_relaxed)) meters.outPeak[c].store(peak[c], std::memory_order_relaxed);
    }

    // ------------------------------------------------------ for the tests

    /** Gain (dB) the main signal gets at a frequency right now. */
    double gainDbAt(double freq) const {
        std::complex<double> removed = 0.0;
        for (int k = 0; k < kBands; ++k) removed += amount_[k] * weight_[k] * response(carve_[0][k], freq);
        removed += amount_[0] * capWeight_[0] * capResponse(0, freq) + amount_[kBands - 1] * capWeight_[1] * capResponse(1, freq);
        return 20.0 * std::log10(std::max(1e-9, std::abs(1.0 - removed)));
    }

    /** Holds every band at this cut until the next block is processed. */
    void forceCut(double db) {
        for (int k = 0; k < kBands; ++k) {
            cut_[k] = db;
            amount_[k] = 1.0 - dbToLin(-db);
        }
    }

    double cutDb(int band) const { return cut_[band]; }

private:
    static constexpr int kStep = 16;        // samples between updates of the cuts
    static constexpr double kDetectQ = 3.3; // two of these in a row per band

    BiquadCoeffs bandpass(double f, double q) const {
        // The width is corrected for the squeeze that digital filters get towards the top.
        const double w = 2.0 * kPi * f / fs_, sn = std::sin(w);
        const double alpha = sn * std::sinh(std::asinh(0.5 / q) * w / sn), a0 = 1.0 + alpha;
        BiquadCoeffs c;
        c.b0 = alpha / a0;
        c.b1 = 0.0;
        c.b2 = -alpha / a0;
        c.a1 = -2.0 * std::cos(w) / a0;
        c.a2 = (1.0 - alpha) / a0;
        return c;
    }

    std::complex<double> response(const Biquad& b, double freq) const {
        const std::complex<double> z1 = std::polar(1.0, -2.0 * kPi * freq / fs_), z2 = z1 * z1;
        return (b.b0 + b.b1 * z1 + b.b2 * z2) / (1.0 + b.a1 * z1 + b.a2 * z2);
    }

    // Below the lowest and above the highest band there are no neighbours to
    // complete the sum, so a gentle lowpass and highpass stand in for them.
    std::complex<double> capResponse(int cap, double freq) const {
        const std::complex<double> z1 = std::polar(1.0, -2.0 * kPi * freq / fs_);
        const std::complex<double> lp = capCoef_[cap] / (1.0 - (1.0 - capCoef_[cap]) * z1);
        return cap == 0 ? lp : 1.0 - lp;
    }

    /**
     * Gives every carving filter a weight so that together they pass the signal
     * once, as closely as possible over the whole range (least squares, in
     * level and in phase). Inside the range the weights come out alike.
     */
    void solveWeights() {
        const int n = kBands + 2;
        double m[kBands + 2][kBands + 3] = {};
        const double top = std::min(20000.0, 0.45 * fs_);
        for (double f = 20.0; f <= top; f *= 1.02) {
            std::complex<double> h[kBands + 2];
            for (int k = 0; k < kBands; ++k) h[k] = response(carve_[0][k], f);
            h[kBands] = capResponse(0, f);
            h[kBands + 1] = capResponse(1, f);
            for (int a = 0; a < n; ++a) {
                for (int b = 0; b < n; ++b) m[a][b] += h[a].real() * h[b].real() + h[a].imag() * h[b].imag();
                m[a][n] += h[a].real();
            }
        }
        for (int a = 0; a < n; ++a) m[a][a] *= 1.0 + 1e-6; // keeps the solution tame
        for (int i = 0; i < n; ++i) {
            int pivot = i;
            for (int j = i + 1; j < n; ++j)
                if (std::fabs(m[j][i]) > std::fabs(m[pivot][i])) pivot = j;
            for (int k = 0; k <= n; ++k) std::swap(m[i][k], m[pivot][k]);
            for (int j = 0; j < n; ++j) {
                if (j == i) continue;
                const double factor = m[j][i] / m[i][i];
                for (int k = i; k <= n; ++k) m[j][k] -= factor * m[i][k];
            }
        }
        for (int k = 0; k < kBands; ++k) weight_[k] = m[k][n] / m[k][k];
        for (int c = 0; c < 2; ++c) capWeight_[c] = m[kBands + c][n] / m[kBands + c][kBands + c];
    }

    double levelDb(int k) const { return 10.0 * std::log10(power_[k] + 1e-12); }

    /** Works out where every band's cut is heading for the next kStep samples. */
    void control() {
        const double link = 0.01 * params_.link;
        double total = 0.0;
        for (int k = 0; k < kBands; ++k) total += power_[k];
        const double totalDb = 10.0 * std::log10(total / totalNorm_ + 1e-12);
        for (int k = 0; k < kBands; ++k) {
            const double own = levelDb(k), level = own + link * (totalDb - own);
            const double target = cutFor(level - threshold_, depth_[k]);
            cut_[k] += (target - cut_[k]) * (target > cut_[k] ? attack_ : release_);
            if (cut_[k] < 0.01 && target == 0.0) cut_[k] = 0.0;
            const double amount = 1.0 - dbToLin(-cut_[k]);
            step_[k] = (amount - amount_[k]) / kStep;
        }
    }

    void chunk(float* l, float* r, const float* scl, const float* scr, int n, float* peak) {
        double sc[kStep], removed[2][kStep];
        for (int i = 0; i < n; ++i) {
            if (params_.self) sc[i] = 0.5 * (static_cast<double>(l[i]) + r[i]);
            else sc[i] = scl ? 0.5 * (static_cast<double>(scl[i]) + (scr ? scr[i] : scl[i])) : 0.0;
            // The ducker comes straight from the host: what is not a number counts as silence.
            if (!isFinite(sc[i])) sc[i] = 0.0;
        }

        float* const io[2] = {l, r};

        // The two ends follow the lowest and the highest band.
        const double capFrom[2] = {amount_[0], amount_[kBands - 1]}, capInc[2] = {step_[0], step_[kBands - 1]};
        for (int c = 0; c < 2; ++c) {
            double low = capState_[c][0], high = capState_[c][1];
            for (int i = 0; i < n; ++i) {
                const double x = io[c][i];
                low += capCoef_[0] * (x - low);
                high += capCoef_[1] * (x - high);
                removed[c][i] = (capFrom[0] + capInc[0] * (i + 1)) * capWeight_[0] * low
                              + (capFrom[1] + capInc[1] * (i + 1)) * capWeight_[1] * (x - high);
            }
            capState_[c][0] = low + 1e-25;
            capState_[c][1] = high + 1e-25;
        }
        for (int k = 0; k < kBands; ++k) {
            // The ducker's level in this band.
            Biquad &d0 = detect_[0][k], &d1 = detect_[1][k];
            double power = power_[k];
            const double follow = follow_[k];
            for (int i = 0; i < n; ++i) {
                const double y = d1.process(d0.process(sc[i]));
                power += follow * (2.0 * y * y - power);
            }
            power_[k] = power + 1e-25;

            // What to take out of the main signal in this band.
            const double from = amount_[k], inc = step_[k], weight = weight_[k];
            for (int c = 0; c < 2; ++c) {
                Biquad& b = carve_[c][k];
                double amount = from;
                double* const out = removed[c];
                const float* const in = io[c];
                for (int i = 0; i < n; ++i) {
                    amount += inc;
                    out[i] += amount * weight * b.process(in[i]);
                }
            }
            amount_[k] = from + inc * n;
        }

        for (int c = 0; c < 2; ++c) {
            const float* const side = c == 0 ? scl : (scr ? scr : scl);
            for (int i = 0; i < n; ++i) {
                const double cut = removed[c][i];
                double out;
                switch (params_.listen) {
                case kListenSidechain: out = params_.self || !side ? io[c][i] : side[i]; break;
                case kListenRemoved: out = cut; break;
                default: out = io[c][i] - cut; break;
                }
                io[c][i] = static_cast<float>(out);
                peak[c] = std::max(peak[c], std::fabs(io[c][i]));
            }
        }
    }

    SpacerParams params_;
    double fs_ = 48000.0, totalNorm_ = 1.0;
    double threshold_ = -42.0, attack_ = 0.1, release_ = 0.01;
    Biquad carve_[2][kBands];  // left, right
    Biquad detect_[2][kBands]; // first, second stage
    double capCoef_[2] = {}, capWeight_[2] = {}, capState_[2][2] = {};
    double weight_[kBands] = {}, follow_[kBands] = {}, power_[kBands] = {}, depth_[kBands] = {};
    double cut_[kBands] = {}, amount_[kBands] = {}, step_[kBands] = {};
    int phase_ = 0;
};

} // namespace hnl
