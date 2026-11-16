// HNL Transient Shaper: the audio engine.
//
// Turns the hit of a sound up or down separately from what follows it, and
// does so by the shape of the sound rather than its level: there is no
// threshold, a quiet hit is treated like a loud one.
//
// How it works. The detector's level (the louder channel, optionally only
// its lows, mids or highs) is held over a few milliseconds so that the
// individual waves of a low note are not mistaken for hits, and followed in
// dB by three envelopes:
//
//   fast     follows the level almost at once
//   slow     rises slowly (the Length control), falls with the fast one
//   hanging  rises with the fast one, falls slowly
//
// "fast above slow" marks an attack, "hanging above fast" marks a decay.
// Each drives its own gain change, up to +/-12 dB for the attack and
// +/-18 dB for the sustain. The audio is delayed by one millisecond so the
// gain is in place when the hit arrives; that delay is the latency.
//
// Header-only C++17, no dependencies beyond common/dsp/Filters.hpp.
#pragma once

#include <atomic>
#include <cstdint>

#include "../../../common/dsp/Filters.hpp"

namespace hnl {

enum ShaperFocus { kFocusFull = 0, kFocusLow, kFocusMid, kFocusHigh, kFocusCount };

/** Largest gain change in dB at 100%, and the difference between envelopes (dB) at which 63% of it is reached. */
constexpr double kAttackRangeDb = 12.0, kAttackKneeDb = 6.0, kSustainRangeDb = 18.0, kSustainKneeDb = 6.0;
/** Envelopes never drift further apart than this, so one very large jump does not last longer than a normal hit. */
constexpr double kEnvelopeSpanDb = 24.0;
/** Levels below this are treated as silence. */
constexpr double kShaperFloorDb = -70.0;
constexpr double kLookaheadMs = 1.0, kHoldMs = 15.0, kPeakReleaseMs = 30.0, kFastAttackMs = 0.35, kFastReleaseMs = 5.0, kHangMs = 80.0;
/** Edges of the detector's Low, Mid and High ranges. */
constexpr double kFocusLowHz = 150.0, kFocusHighHz = 2500.0;

struct ShaperParams {
    double attack = 0.0;   // -100..100 percent
    double sustain = 0.0;  // -100..100 percent
    double length = 20.0;  // ms: how long a hit counts as attack
    int focus = kFocusFull;
    double output = 0.0;   // dB
    double mix = 100.0;    // percent shaped
};

class TransientShaper {
public:
    struct Meters {
        std::atomic<float> boost{0.0f}, cut{0.0f};       // dB (0 or more), largest since last read
        std::atomic<float> inPeak[2] = {{0.0f}, {0.0f}}; // linear, largest since last read
        std::atomic<float> outPeak[2] = {{0.0f}, {0.0f}};
    } meters;

    void prepare(double sampleRate) {
        fs_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        latency_ = std::max(1, static_cast<int>(std::lround(kLookaheadMs * 0.001 * fs_)));
        for (Delay& d : delay_) d.setLength(static_cast<size_t>(latency_));
        holdSamples_ = static_cast<int>(std::lround(kHoldMs * 0.001 * fs_));
        peakRelease_ = onePoleCoef(kPeakReleaseMs, fs_);
        fastAttack_ = onePoleCoef(kFastAttackMs, fs_);
        fastRelease_ = onePoleCoef(kFastReleaseMs, fs_);
        hang_ = onePoleCoef(kHangMs, fs_);
        smooth_ = 1.0 - onePoleCoef(20.0, fs_);
        update();
        reset();
    }

    void setParams(const ShaperParams& p) {
        const int before = p_.focus;
        p_ = p;
        p_.focus = std::clamp(p.focus, 0, kFocusCount - 1);
        update();
        if (p_.focus != before) resetFilters();
    }

    void reset() {
        for (Delay& d : delay_) d.reset();
        resetFilters();
        peak_ = 0.0;
        hold_ = 0;
        fast_ = slow_ = hanging_ = kShaperFloorDb;
        attackAmt_ = attackTarget_;
        sustainAmt_ = sustainTarget_;
        output_ = outputTarget_;
        mix_ = mixTarget_;
    }

    int latency() const { return latency_; }

    void process(float* left, float* right, int frames) {
        ScopedNoDenormals guard;
        double peakIn[2] = {0, 0}, peakOut[2] = {0, 0}, most = 0.0, least = 0.0;
        const double floorLin = dbToLin(kShaperFloorDb);
        for (int i = 0; i < frames; ++i) {
            const double l = left[i], r = right[i];
            peakIn[0] = std::max(peakIn[0], std::fabs(l));
            peakIn[1] = std::max(peakIn[1], std::fabs(r));

            // Detector: the chosen range of the louder channel, held over the gaps between waves.
            double dl = l, dr = r;
            if (hpOn_) {
                dl = hp_[0].process(dl);
                dr = hp_[1].process(dr);
            }
            if (lpOn_) {
                dl = lp_[0].process(dl);
                dr = lp_[1].process(dr);
            }
            const double level = std::max(std::fabs(dl), std::fabs(dr));
            if (level >= peak_) {
                peak_ = level;
                hold_ = holdSamples_;
            } else if (hold_ > 0) {
                --hold_;
            } else {
                peak_ *= peakRelease_;
            }
            const double env = peak_ > floorLin ? linToDb(peak_) : kShaperFloorDb;

            fast_ = env + (env > fast_ ? fastAttack_ : fastRelease_) * (fast_ - env);
            slow_ = fast_ > slow_ ? std::max(fast_ + slowAttack_ * (slow_ - fast_), fast_ - kEnvelopeSpanDb) : fast_;
            hanging_ = fast_ > hanging_ ? fast_ : std::min(fast_ + hang_ * (hanging_ - fast_), fast_ + kEnvelopeSpanDb);

            attackAmt_ += smooth_ * (attackTarget_ - attackAmt_);
            sustainAmt_ += smooth_ * (sustainTarget_ - sustainAmt_);
            output_ += smooth_ * (outputTarget_ - output_);
            mix_ += smooth_ * (mixTarget_ - mix_);

            double gainDb = 0.0;
            if (attackAmt_ != 0.0) gainDb += attackAmt_ * kAttackRangeDb * (1.0 - std::exp((slow_ - fast_) / kAttackKneeDb));
            if (sustainAmt_ != 0.0) gainDb += sustainAmt_ * kSustainRangeDb * (1.0 - std::exp((fast_ - hanging_) / kSustainKneeDb));
            most = std::max(most, gainDb);
            least = std::min(least, gainDb);

            const double gain = (1.0 - mix_) + mix_ * output_ * dbToLin(gainDb);
            const double outL = delay_[0].process(l) * gain, outR = delay_[1].process(r) * gain;
            left[i] = static_cast<float>(outL);
            right[i] = static_cast<float>(outR);
            peakOut[0] = std::max(peakOut[0], std::fabs(outL));
            peakOut[1] = std::max(peakOut[1], std::fabs(outR));
        }
        if (peak_ < 1e-12) peak_ = 0.0; // rest at zero instead of running into denormals

        raise(meters.boost, static_cast<float>(most));
        raise(meters.cut, static_cast<float>(-least));
        for (int c = 0; c < 2; ++c) {
            raise(meters.inPeak[c], static_cast<float>(peakIn[c]));
            raise(meters.outPeak[c], static_cast<float>(peakOut[c]));
        }
    }

private:
    static void raise(std::atomic<float>& meter, float value) {
        if (value > meter.load(std::memory_order_relaxed)) meter.store(value, std::memory_order_relaxed);
    }

    void resetFilters() {
        for (Biquad& b : hp_) b.reset();
        for (Biquad& b : lp_) b.reset();
    }

    void update() {
        // For a strong hit the boost has fallen to 63% of its peak after `length` milliseconds.
        const double knee = std::log(kEnvelopeSpanDb / kAttackKneeDb);
        slowAttack_ = onePoleCoef(std::clamp(p_.length, 1.0, 500.0) / knee, fs_);
        attackTarget_ = std::clamp(p_.attack, -100.0, 100.0) * 0.01;
        sustainTarget_ = std::clamp(p_.sustain, -100.0, 100.0) * 0.01;
        outputTarget_ = dbToLin(p_.output);
        mixTarget_ = std::clamp(p_.mix, 0.0, 100.0) * 0.01;

        hpOn_ = p_.focus == kFocusMid || p_.focus == kFocusHigh;
        lpOn_ = p_.focus == kFocusLow || p_.focus == kFocusMid;
        const double q = 0.7071067811865476;
        const BiquadCoeffs hp = designHighpass(fs_, p_.focus == kFocusMid ? kFocusLowHz : kFocusHighHz, q);
        const BiquadCoeffs lp = designLowpass(fs_, p_.focus == kFocusMid ? kFocusHighHz : kFocusLowHz, q);
        for (Biquad& b : hp_) b.set(hp);
        for (Biquad& b : lp_) b.set(lp);
    }

    ShaperParams p_;
    double fs_ = 48000.0;
    int latency_ = 48, holdSamples_ = 720, hold_ = 0;
    Delay delay_[2];
    Biquad hp_[2], lp_[2];
    bool hpOn_ = false, lpOn_ = false;
    double peakRelease_ = 0.0, fastAttack_ = 0.0, fastRelease_ = 0.0, slowAttack_ = 0.0, hang_ = 0.0, smooth_ = 1.0;
    double peak_ = 0.0, fast_ = kShaperFloorDb, slow_ = kShaperFloorDb, hanging_ = kShaperFloorDb;
    double attackTarget_ = 0.0, sustainTarget_ = 0.0, outputTarget_ = 1.0, mixTarget_ = 1.0;
    double attackAmt_ = 0.0, sustainAmt_ = 0.0, output_ = 1.0, mix_ = 1.0;
};

} // namespace hnl
