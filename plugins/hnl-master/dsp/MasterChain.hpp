// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// The complete mastering chain:
//   input gain -> 3-band compressor -> equaliser -> exciter -> widener
//   -> reverb -> true-peak limiter -> meters
#pragma once

#include <atomic>

#include "Effects.hpp"
#include "Processors.hpp"

namespace hnl {

struct BandParams {
    double threshold = -20.0; // dB
    double ratio = 2.0;       // :1
    double attack = 20.0;     // ms
    double release = 150.0;   // ms
    double makeup = 0.0;      // dB
};

struct Params {
    double inputGain = 0.0; // dB

    bool compOn = true;
    double xoverLow = 120.0;   // Hz
    double xoverHigh = 2500.0; // Hz
    BandParams band[3] = {
        {-20.0, 2.0, 30.0, 250.0, 0.0},
        {-20.0, 2.0, 15.0, 150.0, 0.0},
        {-20.0, 2.0, 5.0, 80.0, 0.0},
    };

    bool eqOn = false;
    double eqLowFreq = 80.0, eqLowGain = 0.0;                   // Hz, dB (low shelf)
    double eqMidFreq = 1000.0, eqMidGain = 0.0, eqMidQ = 1.0;   // peak
    double eqHighFreq = 10000.0, eqHighGain = 0.0;              // high shelf

    bool exciterOn = true;
    int exciterMode = Exciter::kTube;
    double exciterFreq = 5000.0; // Hz
    double exciterDrive = 9.0;   // dB
    double exciterColour = 0.3;  // 0 odd .. 1 even
    double exciterMix = 15.0;    // percent

    bool widenerOn = false;
    double width = 130.0;       // percent; 100 = unchanged
    double widenerFreq = 200.0; // Hz; widening applies above this

    bool reverbOn = false;
    double reverbDecay = 1.4;      // seconds
    double reverbDamping = 6000.0; // Hz
    double reverbMix = 8.0;        // percent of reverb added to the dry signal

    bool limiterOn = true;
    double limiterGain = 0.0;     // dB
    double limiterCeiling = -1.0; // dBTP
    double limiterRelease = 150.0; // ms
};

/** Written by the audio thread, read (and peak-reset) by the UI thread. */
struct Meters {
    std::atomic<float> inPeak[2] = {}, outPeak[2] = {}; // linear, held until taken
    std::atomic<float> truePeakMax{0.0f};               // linear, held until reset
    std::atomic<float> compGr[3] = {};                  // dB (<= 0), deepest since taken
    std::atomic<float> limGr{0.0f};                     // dB (<= 0), deepest since taken
    std::atomic<float> lufsMomentary{-100.0f}, lufsShort{-100.0f}, lufsIntegrated{-100.0f};

    static void raise(std::atomic<float>& a, float v) {
        if (v > a.load(std::memory_order_relaxed)) a.store(v, std::memory_order_relaxed);
    }
    static void lower(std::atomic<float>& a, float v) {
        if (v < a.load(std::memory_order_relaxed)) a.store(v, std::memory_order_relaxed);
    }
};

class MasterChain {
public:
    void prepare(double sampleRate) {
        fs_ = sampleRate;
        limiter_.prepare(fs_);
        loud_.prepare(fs_);
        eq_.prepare(fs_);
        reverb_.prepare(fs_);
        smooth_ = onePoleCoef(20.0, fs_);
        applyParams(true);
        reset();
    }

    void reset() {
        for (int c = 0; c < 2; ++c) { xo_[c].reset(); outTp_[c].reset(); }
        for (auto& b : comp_) b.reset();
        exciter_.reset();
        eq_.reset();
        widener_.reset();
        reverb_.reset();
        limiter_.reset();
        loud_.reset();
        inGain_ = inGainT_;
        limGain_ = limGainT_;
        mix_ = mixT_;
        width_ = widthT_;
        revMix_ = revMixT_;
        for (int b = 0; b < 3; ++b) makeup_[b] = makeupT_[b];
    }

    /** Total delay through the chain in samples; constant for a sample rate. */
    int latency() const { return Exciter::latency() + limiter_.latency(); }

    void setParams(const Params& p) { p_ = p; applyParams(false); }
    const Params& params() const { return p_; }

    void resetIntegratedLoudness() { resetLoudness_.store(true, std::memory_order_relaxed); }

    Meters meters;

    void process(float* left, float* right, int frames) {
        ScopedNoDenormals guard;
        if (resetLoudness_.exchange(false, std::memory_order_relaxed)) loud_.resetIntegrated();

        double inPk[2] = {0, 0}, outPk[2] = {0, 0}, tpk = 0.0;
        double grMin[3] = {0, 0, 0}, limMin = 1.0;
        const bool compOn = p_.compOn, excOn = p_.exciterOn, limOn = p_.limiterOn;
        const bool eqOn = p_.eqOn, wideOn = p_.widenerOn, revOn = p_.reverbOn;
        // A stage that was off starts from a clean state: while it is off its filters are not
        // run, and would otherwise let out what they held at the moment it was switched off.
        if (compOn && !compWasOn_) {
            for (auto& x : xo_) x.reset();
            for (auto& b : comp_) b.reset();
        }
        if (excOn && !excWasOn_) exciter_.resetWet();
        if (eqOn && !eqWasOn_) eq_.reset();
        if (wideOn && !wideWasOn_) widener_.reset();
        if (revOn && !revWasOn_) reverb_.reset();
        compWasOn_ = compOn; excWasOn_ = excOn;
        eqWasOn_ = eqOn; wideWasOn_ = wideOn; revWasOn_ = revOn;

        for (int i = 0; i < frames; ++i) {
            inGain_ = inGainT_ + smooth_ * (inGain_ - inGainT_);
            limGain_ = limGainT_ + smooth_ * (limGain_ - limGainT_);
            mix_ = mixT_ + smooth_ * (mix_ - mixT_);
            width_ = widthT_ + smooth_ * (width_ - widthT_);
            revMix_ = revMixT_ + smooth_ * (revMix_ - revMixT_);

            double l = left[i] * inGain_, r = right[i] * inGain_;
            inPk[0] = std::max(inPk[0], std::fabs(l));
            inPk[1] = std::max(inPk[1], std::fabs(r));

            if (compOn) {
                double bl[3], br[3];
                xo_[0].split(l, bl[0], bl[1], bl[2]);
                xo_[1].split(r, br[0], br[1], br[2]);
                l = r = 0.0;
                for (int b = 0; b < 3; ++b) {
                    makeup_[b] = makeupT_[b] + smooth_ * (makeup_[b] - makeupT_[b]);
                    const double gr = comp_[b].process(std::max(std::fabs(bl[b]), std::fabs(br[b])));
                    grMin[b] = std::min(grMin[b], gr);
                    const double g = dbToLin(gr) * makeup_[b];
                    l += bl[b] * g;
                    r += br[b] * g;
                }
            }

            if (eqOn) {
                eq_.tick();
                l = eq_.process(0, l);
                r = eq_.process(1, r);
            }

            l = exciter_.process(0, l, mix_, excOn);
            r = exciter_.process(1, r, mix_, excOn);

            if (wideOn) widener_.process(l, r, width_);

            if (revOn) {
                double wl, wr;
                reverb_.process(l, r, wl, wr);
                l += revMix_ * wl;
                r += revMix_ * wr;
            }

            if (limOn) { l *= limGain_; r *= limGain_; }
            limMin = std::min(limMin, limiter_.process(l, r, limOn));

            outPk[0] = std::max(outPk[0], std::fabs(l));
            outPk[1] = std::max(outPk[1], std::fabs(r));
            tpk = std::max(tpk, std::max(outTp_[0].process(l), outTp_[1].process(r)));
            loud_.process(l, r);

            left[i] = static_cast<float>(l);
            right[i] = static_cast<float>(r);
        }

        for (int c = 0; c < 2; ++c) {
            Meters::raise(meters.inPeak[c], static_cast<float>(inPk[c]));
            Meters::raise(meters.outPeak[c], static_cast<float>(outPk[c]));
        }
        Meters::raise(meters.truePeakMax, static_cast<float>(tpk));
        for (int b = 0; b < 3; ++b) Meters::lower(meters.compGr[b], static_cast<float>(grMin[b]));
        Meters::lower(meters.limGr, static_cast<float>(linToDb(limMin)));
        meters.lufsMomentary.store(static_cast<float>(loud_.momentary), std::memory_order_relaxed);
        meters.lufsShort.store(static_cast<float>(loud_.shortTerm), std::memory_order_relaxed);
        meters.lufsIntegrated.store(static_cast<float>(loud_.integrated), std::memory_order_relaxed);
    }

private:
    void applyParams(bool force) {
        inGainT_ = dbToLin(p_.inputGain);
        limGainT_ = dbToLin(p_.limiterGain);
        mixT_ = 0.01 * p_.exciterMix;
        // Keep the two crossover points at least an octave apart.
        const double f1 = std::clamp(p_.xoverLow, 20.0, 0.2 * fs_);
        const double f2 = std::clamp(p_.xoverHigh, 2.0 * f1, 0.45 * fs_);
        if (force || f1 != f1_ || f2 != f2_) {
            f1_ = f1; f2_ = f2;
            for (int c = 0; c < 2; ++c) xo_[c].setFreqs(fs_, f1, f2);
        }
        for (int b = 0; b < 3; ++b) {
            comp_[b].threshold = p_.band[b].threshold;
            comp_[b].ratio = std::max(1.0, p_.band[b].ratio);
            comp_[b].setTimes(p_.band[b].attack, p_.band[b].release, fs_);
            makeupT_[b] = dbToLin(p_.band[b].makeup);
        }
        if (force || p_.exciterFreq != excFreq_) {
            excFreq_ = p_.exciterFreq;
            exciter_.setFreq(fs_, excFreq_);
        }
        exciter_.setDrive(p_.exciterDrive, std::clamp(p_.exciterColour, 0.0, 1.0),
                          static_cast<Exciter::Mode>(std::clamp(p_.exciterMode, 0, 2)));
        eq_.set(Eq3::kLowShelf, p_.eqLowFreq, p_.eqLowGain);
        eq_.set(Eq3::kPeak, p_.eqMidFreq, p_.eqMidGain, std::max(0.1, p_.eqMidQ));
        eq_.set(Eq3::kHighShelf, p_.eqHighFreq, p_.eqHighGain);
        widthT_ = 0.01 * p_.width;
        if (force || p_.widenerFreq != wideFreq_) {
            wideFreq_ = p_.widenerFreq;
            widener_.setFreq(fs_, wideFreq_);
        }
        revMixT_ = 0.01 * p_.reverbMix;
        reverb_.setDecay(p_.reverbDecay);
        reverb_.setDamping(p_.reverbDamping);
        limiter_.setCeiling(p_.limiterCeiling);
        limiter_.setRelease(p_.limiterRelease);
    }

    Params p_;
    double fs_ = 48000.0, smooth_ = 0.0;
    double f1_ = 0.0, f2_ = 0.0, excFreq_ = 0.0, wideFreq_ = 0.0;
    double width_ = 1.0, widthT_ = 1.0, revMix_ = 0.0, revMixT_ = 0.0;
    bool compWasOn_ = false, excWasOn_ = false, eqWasOn_ = false, wideWasOn_ = false, revWasOn_ = false;
    double inGain_ = 1.0, inGainT_ = 1.0, limGain_ = 1.0, limGainT_ = 1.0, mix_ = 0.0, mixT_ = 0.0;
    double makeup_[3] = {1, 1, 1}, makeupT_[3] = {1, 1, 1};

    Crossover3 xo_[2];
    CompBand comp_[3];
    Exciter exciter_;
    Eq3 eq_;
    Widener widener_;
    Reverb reverb_;
    Limiter limiter_;
    TruePeak outTp_[2];
    LoudnessMeter loud_;
    std::atomic<bool> resetLoudness_{false};
};

} // namespace hnl
