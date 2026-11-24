// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// The complete mastering chain:
//   input gain -> 3-band compressor -> equaliser -> exciter -> widener
//   -> reverb -> true-peak limiter (brickwall or soft) -> loudness match -> bypass -> dither -> meters
// The equaliser can stand before the compressor instead (Params::eqFirst).
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
    bool eqFirst = false; // the equaliser before the compressor instead of after it
    double eqLowFreq = 80.0, eqLowGain = 0.0;                   // Hz, dB (low shelf)
    double eqMidFreq = 1000.0, eqMidGain = 0.0, eqMidQ = 1.0;   // peak
    double eqHighFreq = 10000.0, eqHighGain = 0.0;              // high shelf
    double eqLowCut = 0.0;  // Hz, 12 dB per octave; 0 = off
    int eqMode = 0;         // what it works on: 0 left and right, 1 the mid (mono) part only, 2 the side part only

    // For listening, not for the result: only the soloed bands of the compressor, or all but the muted ones.
    bool bandSolo[3] = {false, false, false}, bandMute[3] = {false, false, false};

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
    bool limiterSoft = false;      // soft instead of brickwall: peaks faster than the attack pass the ceiling
    double limiterAttack = 3.0;    // ms; brickwall uses at most Limiter::kMaxLookMs of it

    int dither = 0;      // 0 off, or the word length the result is rounded to with dither: 16 or 24 bits
    bool bypass = false; // the unprocessed sound, as late as the processed one
    bool match = false;  // the processed sound brought to the loudness of the unprocessed one, to compare the two fairly
};

/** Written by the audio thread, read (and peak-reset) by the UI thread. */
struct Meters {
    std::atomic<float> inPeak[2] = {}, outPeak[2] = {}; // linear, held until taken
    std::atomic<float> truePeakMax{0.0f};               // linear, held until reset
    std::atomic<float> compGr[3] = {};                  // dB (<= 0), deepest since taken
    std::atomic<float> limGr{0.0f};                     // dB (<= 0), deepest since taken
    std::atomic<float> lufsMomentary{-100.0f}, lufsShort{-100.0f}, lufsIntegrated{-100.0f};
    std::atomic<float> matchDb{0.0f};                   // dB the processed sound is turned by, 0 when not matched
    std::atomic<float> correlation{0.0f};               // of left and right at the output, -1..1
    std::atomic<float> spectrum[Spectrum::kBands];      // dB, of what goes into the compressor; highest since taken

    Meters() { for (auto& s : spectrum) s.store(-100.0f); }

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
        spec_.prepare(fs_);
        match_.prepare(fs_);
        corr_.prepare(fs_);
        for (auto& d : dry_) d.setLength(latency());
        bypassStep_ = 1.0 / (0.01 * fs_);
        smooth_ = onePoleCoef(20.0, fs_);
        orderStep_ = 1.0 / (0.005 * fs_);
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
        for (auto& f : lowCut_) f.reset();
        for (auto& d : dry_) d.reset();
        spec_.reset();
        match_.reset();
        corr_.reset();
        dither_[0].reset(0x9e3779b9u);
        dither_[1].reset(0x7f4a7c15u);
        matchGain_ = 1.0;
        bypass_ = p_.bypass ? 1.0 : 0.0;
        eqMode_ = p_.eqMode;
        eqFirst_ = p_.eqFirst;
        orderGain_ = 1.0;
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
        const bool lowCutOn = eqOn && p_.eqLowCut > 0.0;
        if (eqOn && !eqWasOn_) eq_.reset();
        if (lowCutOn && !lowCutWasOn_) for (auto& f : lowCut_) f.reset();
        lowCutWasOn_ = lowCutOn;
        const int ditherBits = p_.dither;
        double corrSum = 0.0;
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

            const double dl = dry_[0].process(left[i]), dr = dry_[1].process(right[i]);
            double l = left[i] * inGain_, r = right[i] * inGain_;
            inPk[0] = std::max(inPk[0], std::fabs(l));
            inPk[1] = std::max(inPk[1], std::fabs(r));

            // The equaliser changes place in a 5 ms dip: down, the two stages around and their filters
            // cleared, up again. Changing place at full level would let out what the filters held from
            // the other order, as a click. With one of the two off there is nothing to hear, and no dip.
            // What the equaliser works on (left and right, mid or side) changes in the same dip.
            const bool newPlace = eqFirst_ != p_.eqFirst, newMode = eqMode_ != p_.eqMode;
            if (newPlace || newMode) {
                const bool placeHeard = newPlace && compOn && eqOn, heard = placeHeard || (newMode && eqOn);
                if (heard && orderGain_ > 0.0) {
                    orderGain_ = std::max(0.0, orderGain_ - orderStep_);
                } else {
                    eqFirst_ = p_.eqFirst;
                    eqMode_ = p_.eqMode;
                    if (placeHeard) for (auto& x : xo_) x.reset();
                    if (heard) eq_.reset();
                }
            } else if (orderGain_ < 1.0) {
                orderGain_ = std::min(1.0, orderGain_ + orderStep_);
            }

            if (eqOn && eqFirst_) equalise(l, r, lowCutOn);

            if (spec_.process(0.5 * (l + r)))
                for (int b = 0; b < Spectrum::kBands; ++b) Meters::raise(meters.spectrum[b], static_cast<float>(spec_.db[b]));

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

            if (eqOn && !eqFirst_) equalise(l, r, lowCutOn);
            if (orderGain_ < 1.0) {
                const double g = 0.5 - 0.5 * std::cos(3.14159265358979 * orderGain_);
                l *= g;
                r *= g;
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

            // The loudness of both is followed whether it is used or not, so that the match is there at once.
            const double matched = match_.process(dl, dr, l, r), matchT = p_.match ? matched : 1.0;
            matchGain_ = matchT + smooth_ * (matchGain_ - matchT);
            l *= matchGain_;
            r *= matchGain_;
            if (bypass_ != (p_.bypass ? 1.0 : 0.0)) bypass_ = p_.bypass ? std::min(1.0, bypass_ + bypassStep_) : std::max(0.0, bypass_ - bypassStep_);
            if (bypass_ > 0.0) { // over in 10 ms, without a click
                const double w = 0.5 - 0.5 * std::cos(3.14159265358979 * bypass_);
                l += w * (dl - l);
                r += w * (dr - r);
            }
            if (ditherBits != 0 && bypass_ < 1.0) { // the unprocessed sound is left as it came
                l = dither_[0].process(l, ditherBits);
                r = dither_[1].process(r, ditherBits);
            }
            corr_.process(l, r);
            corrSum += corr_.value;

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
        meters.matchDb.store(p_.match ? static_cast<float>(linToDb(matchGain_)) : 0.0f, std::memory_order_relaxed);
        if (frames > 0) meters.correlation.store(static_cast<float>(corrSum / frames), std::memory_order_relaxed);
        meters.lufsMomentary.store(static_cast<float>(loud_.momentary), std::memory_order_relaxed);
        meters.lufsShort.store(static_cast<float>(loud_.shortTerm), std::memory_order_relaxed);
        meters.lufsIntegrated.store(static_cast<float>(loud_.integrated), std::memory_order_relaxed);
    }

private:
    /** The equaliser, with its low cut before it, on left and right or on the mid or side part alone. */
    inline void equalise(double& l, double& r, bool lowCutOn) {
        if (lowCutOn) {
            l = lowCut_[0].process(l);
            r = lowCut_[1].process(r);
        }
        eq_.tick();
        if (eqMode_ == 0) {
            l = eq_.process(0, l);
            r = eq_.process(1, r);
            return;
        }
        double mid = 0.5 * (l + r), side = 0.5 * (l - r);
        if (eqMode_ == 1) mid = eq_.process(0, mid);
        else side = eq_.process(0, side);
        l = mid + side;
        r = mid - side;
    }

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
        // Solo and mute go with the gain of a band, and so fade as it does. A muted band is silent, soloed or not.
        const bool anySolo = p_.bandSolo[0] || p_.bandSolo[1] || p_.bandSolo[2];
        for (int b = 0; b < 3; ++b)
            if (p_.bandMute[b] || (anySolo && !p_.bandSolo[b])) makeupT_[b] = 0.0;
        if (force || p_.eqLowCut != lowCutFreq_) {
            lowCutFreq_ = p_.eqLowCut;
            const auto h = designHighpass(fs_, std::max(lowCutFreq_, 10.0), 0.7071067811865476);
            for (auto& f : lowCut_) f.set(h);
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
        limiter_.setAttack(p_.limiterAttack);
        limiter_.setSoft(p_.limiterSoft);
    }

    Params p_;
    double fs_ = 48000.0, smooth_ = 0.0;
    double f1_ = 0.0, f2_ = 0.0, excFreq_ = 0.0, wideFreq_ = 0.0;
    double width_ = 1.0, widthT_ = 1.0, revMix_ = 0.0, revMixT_ = 0.0;
    bool eqFirst_ = false;                     // the order in use; follows p_.eqFirst through a dip
    int eqMode_ = 0;                           // what the equaliser works on, in use; follows p_.eqMode through the same dip
    double lowCutFreq_ = -1.0, matchGain_ = 1.0, bypass_ = 0.0, bypassStep_ = 0.0; // bypass_: 0 processed .. 1 unprocessed
    bool lowCutWasOn_ = false;
    double orderGain_ = 1.0, orderStep_ = 0.0; // that dip, 1 = none
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
    Biquad lowCut_[2];
    Delay dry_[2]; // the unprocessed sound, as late as the processed one
    Spectrum spec_;
    LoudnessMatch match_;
    Correlation corr_;
    Dither dither_[2];
    std::atomic<bool> resetLoudness_{false};
};

} // namespace hnl
