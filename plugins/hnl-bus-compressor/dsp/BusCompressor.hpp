// HNL Bus Compressor: the audio engine.
//
// A stereo-linked VCA-style compressor for a mix or drum bus. The sidechain
// takes the louder of the two channels (optionally high-passed, so the bass
// does not drive the compression), a soft-knee gain computer turns its level
// into a gain reduction in dB, and that reduction is smoothed with the attack
// and release times. Both channels always get the same gain, so the stereo
// image does not move. No lookahead and no latency.
//
// Auto release follows two time constants: a short peak lets go quickly,
// compression that has lasted a while lets go slowly.
//
// Header-only C++17, no dependencies beyond common/dsp/Filters.hpp.
#pragma once

#include <atomic>
#include <cstdint>

#include "../../../common/dsp/Filters.hpp"

namespace hnl {

constexpr int kRatioCount = 4, kAttackCount = 6, kReleaseCount = 5;
constexpr double kRatios[kRatioCount] = {1.5, 2.0, 4.0, 10.0};
/** Knee width per ratio: gentle ratios bend in gradually, 10:1 is close to a hard knee. */
constexpr double kKneesDb[kRatioCount] = {12.0, 10.0, 6.0, 3.0};
constexpr double kAttacksMs[kAttackCount] = {0.1, 0.3, 1.0, 3.0, 10.0, 30.0};
constexpr double kReleasesS[kReleaseCount - 1] = {0.1, 0.3, 0.6, 1.2};
constexpr int kAutoRelease = kReleaseCount - 1;

/** Auto release: time constants of the quick and the slow part, and how fast the slow part builds up. */
constexpr double kAutoFastS = 0.15, kAutoSlowS = 2.5, kAutoChargeS = 1.5;

/** The sidechain high-pass is off at (or below) this frequency. */
constexpr double kScHpOffHz = 20.0;

struct BusCompParams {
    double threshold = -16.0; // dBFS
    int ratio = 2;            // index into kRatios
    int attack = 4;           // index into kAttacksMs
    int release = kAutoRelease; // index into kReleasesS, or kAutoRelease
    double makeup = 0.0;      // dB
    double scHp = kScHpOffHz; // Hz
    double mix = 100.0;       // percent compressed
};

class BusCompressor {
public:
    struct Meters {
        std::atomic<float> reduction{0.0f};                  // dB, largest since last read
        std::atomic<float> inPeak[2] = {{0.0f}, {0.0f}};     // linear, largest since last read
        std::atomic<float> outPeak[2] = {{0.0f}, {0.0f}};
    } meters;

    void prepare(double sampleRate) {
        fs_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        smooth_ = 1.0 - onePoleCoef(20.0, fs_);
        update();
        reset();
    }

    void setParams(const BusCompParams& p) {
        p_ = p;
        p_.ratio = std::clamp(p.ratio, 0, kRatioCount - 1);
        p_.attack = std::clamp(p.attack, 0, kAttackCount - 1);
        p_.release = std::clamp(p.release, 0, kReleaseCount - 1);
        update();
    }

    void reset() {
        for (Biquad& b : hp_) b.reset();
        fast_ = slow_ = 0.0;
        makeup_ = makeupTarget_;
        mix_ = mixTarget_;
    }

    static constexpr int latency() { return 0; }

    /** Gain reduction in dB (0 or more) for a steady sidechain level. */
    static double staticReduction(double levelDb, double thresholdDb, int ratio) {
        const double slope = 1.0 - 1.0 / kRatios[ratio], knee = kKneesDb[ratio], over = levelDb - thresholdDb;
        if (over <= -0.5 * knee) return 0.0;
        if (over >= 0.5 * knee) return slope * over;
        const double t = over + 0.5 * knee;
        return slope * t * t / (2.0 * knee);
    }

    void process(float* left, float* right, int frames) {
        ScopedNoDenormals guard;
        double peakIn[2] = {0, 0}, peakOut[2] = {0, 0}, deepest = 0.0;
        const bool automatic = p_.release == kAutoRelease;
        for (int i = 0; i < frames; ++i) {
            const double l = left[i], r = right[i];
            peakIn[0] = std::max(peakIn[0], std::fabs(l));
            peakIn[1] = std::max(peakIn[1], std::fabs(r));

            double sl = l, sr = r;
            if (hpOn_) {
                sl = hp_[0].process(l);
                sr = hp_[1].process(r);
            }
            const double level = std::max(std::fabs(sl), std::fabs(sr));
            const double target = level > kneeStart_ ? staticReduction(linToDb(level), p_.threshold, p_.ratio) : 0.0;

            fast_ = target + (target > fast_ ? attack_ : release_) * (fast_ - target);
            double reduction = fast_;
            if (automatic) {
                slow_ = fast_ + (fast_ > slow_ ? autoCharge_ : autoSlow_) * (slow_ - fast_);
                reduction = std::max(fast_, slow_);
            }
            deepest = std::max(deepest, reduction);

            makeup_ += smooth_ * (makeupTarget_ - makeup_);
            mix_ += smooth_ * (mixTarget_ - mix_);
            const double gain = (1.0 - mix_) + mix_ * makeup_ * dbToLin(-reduction);
            const double outL = l * gain, outR = r * gain;
            left[i] = static_cast<float>(outL);
            right[i] = static_cast<float>(outR);
            peakOut[0] = std::max(peakOut[0], std::fabs(outL));
            peakOut[1] = std::max(peakOut[1], std::fabs(outR));
        }
        // Let the envelopes come to rest at exactly zero instead of running into denormals.
        if (fast_ < 1e-9) fast_ = 0.0;
        if (slow_ < 1e-9) slow_ = 0.0;

        raise(meters.reduction, static_cast<float>(deepest));
        for (int c = 0; c < 2; ++c) {
            raise(meters.inPeak[c], static_cast<float>(peakIn[c]));
            raise(meters.outPeak[c], static_cast<float>(peakOut[c]));
        }
    }

private:
    static void raise(std::atomic<float>& meter, float value) {
        if (value > meter.load(std::memory_order_relaxed)) meter.store(value, std::memory_order_relaxed);
    }

    void update() {
        const bool automatic = p_.release == kAutoRelease;
        attack_ = onePoleCoef(kAttacksMs[p_.attack], fs_);
        release_ = onePoleCoef(1000.0 * (automatic ? kAutoFastS : kReleasesS[p_.release]), fs_);
        autoCharge_ = onePoleCoef(1000.0 * kAutoChargeS, fs_);
        autoSlow_ = onePoleCoef(1000.0 * kAutoSlowS, fs_);
        if (!automatic) slow_ = 0.0;

        kneeStart_ = dbToLin(p_.threshold - 0.5 * kKneesDb[p_.ratio]);
        makeupTarget_ = dbToLin(p_.makeup);
        mixTarget_ = std::clamp(p_.mix, 0.0, 100.0) * 0.01;

        const bool on = p_.scHp > kScHpOffHz + 0.5;
        if (on) {
            const BiquadCoeffs c = designHighpass(fs_, p_.scHp, 0.7071067811865476);
            for (Biquad& b : hp_) b.set(c);
        }
        if (on && !hpOn_) for (Biquad& b : hp_) b.reset();
        hpOn_ = on;
    }

    BusCompParams p_;
    double fs_ = 48000.0;
    Biquad hp_[2];
    bool hpOn_ = false;
    double attack_ = 0.0, release_ = 0.0, autoCharge_ = 0.0, autoSlow_ = 0.0, smooth_ = 1.0;
    double kneeStart_ = 0.0, makeupTarget_ = 1.0, mixTarget_ = 1.0, makeup_ = 1.0, mix_ = 1.0;
    double fast_ = 0.0, slow_ = 0.0; // gain reduction in dB
};

} // namespace hnl
