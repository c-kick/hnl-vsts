// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// HNL Leveller: the audio engine.
//
// A levelling compressor for single sources such as a vocal or a bass, with
// two characters:
//
//   Opto  slow and smooth. A wide soft knee, an attack of some milliseconds
//         and a release in two stages: about half of the reduction goes
//         quickly, the rest hangs on, and it hangs on more the longer the
//         compression has lasted. A single short peak is let go at once.
//   FET   fast and firm. Attack well under a millisecond, a tighter knee and
//         one plain release time.
//
// Both channels always get the same gain. After the gain element a colour
// stage adds low-order harmonics (mostly second for Opto, mostly third for
// FET) that grow with the level. They are added to the signal rather than
// shaping it, so with Colour at zero the path is exactly clean. No lookahead
// and no latency.
//
// Header-only C++17, no dependencies beyond common/dsp/Filters.hpp.
#pragma once

#include <atomic>
#include <cstdint>

#include "../../../common/dsp/Filters.hpp"

namespace hnl {

enum LevellerType { kOpto = 0, kFet = 1, kLevellerTypeCount };

constexpr int kLevRatioCount = 4;
constexpr double kLevRatios[kLevRatioCount] = {3.0, 4.0, 8.0, 20.0};
/** Knee width in dB per type and ratio. */
constexpr double kLevKneesDb[kLevellerTypeCount][kLevRatioCount] = {{16.0, 14.0, 10.0, 8.0}, {6.0, 5.0, 4.0, 3.0}};

/** Opto: the share of a long reduction that is left to the slow stage, and how fast that stage builds up. */
constexpr double kOptoTailShare = 0.5, kOptoChargeS = 1.5;

/** Harmonics at Colour 100%: second and third order weights per type. */
constexpr double kColour2[kLevellerTypeCount] = {0.040, 0.012}, kColour3[kLevellerTypeCount] = {0.008, 0.030};
/** The colour stage works on the signal scaled so that -12 dBFS is 1, and stops growing at -6 dBFS. */
constexpr double kColourScale = 4.0, kColourLimit = 2.0;

/** The sidechain high-pass is off at (or below) this frequency. */
constexpr double kLevScHpOffHz = 20.0;

/** Time constants in milliseconds; tail is the slow release stage (Opto only, 0 for FET). */
struct LevellerTiming {
    double attackMs, releaseMs, tailMs;
};

/** What the Attack and Release knobs (0..100%) mean for a type. */
inline LevellerTiming levellerTiming(int type, double attackPct, double releasePct) {
    const double a = std::clamp(attackPct, 0.0, 100.0) * 0.01, r = std::clamp(releasePct, 0.0, 100.0) * 0.01;
    if (type == kFet) return {0.02 * std::pow(40.0, a), 50.0 * std::pow(22.0, r), 0.0}; // 0.02..0.8 ms, 50..1100 ms
    const double release = 20.0 * std::pow(9.0, r);                                     // 20..180 ms, 60 in the middle
    return {2.5 * std::pow(16.0, a), release, 25.0 * release};                          // 2.5..40 ms; tail 0.5..4.5 s
}

struct LevellerParams {
    int type = kOpto;
    double threshold = -20.0; // dBFS
    int ratio = 1;            // index into kLevRatios
    double attack = 50.0;     // percent of the type's range
    double release = 50.0;    // percent of the type's range
    double makeup = 0.0;      // dB
    double colour = 20.0;     // percent
    double scHp = kLevScHpOffHz; // Hz
    double mix = 100.0;       // percent compressed
};

class Leveller {
public:
    struct Meters {
        std::atomic<float> reduction{0.0f};              // dB, largest since last read
        std::atomic<float> inPeak[2] = {{0.0f}, {0.0f}}; // linear, largest since last read
        std::atomic<float> outPeak[2] = {{0.0f}, {0.0f}};
    } meters;

    void prepare(double sampleRate) {
        fs_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        smooth_ = 1.0 - onePoleCoef(20.0, fs_);
        dcCoef_ = 1.0 - 2.0 * kPi * 5.0 / fs_;
        update();
        reset();
    }

    void setParams(const LevellerParams& p) {
        p_ = p;
        p_.type = std::clamp(p.type, 0, kLevellerTypeCount - 1);
        p_.ratio = std::clamp(p.ratio, 0, kLevRatioCount - 1);
        update();
    }

    void reset() {
        for (Biquad& b : hp_) b.reset();
        fast_ = slow_ = 0.0;
        makeup_ = makeupTarget_;
        mix_ = mixTarget_;
        colour_ = colourTarget_;
        for (int c = 0; c < 2; ++c) dcIn_[c] = dcOut_[c] = 0.0;
    }

    static constexpr int latency() { return 0; }

    /** Gain reduction in dB (0 or more) for a steady sidechain level. */
    static double staticReduction(double levelDb, double thresholdDb, int type, int ratio) {
        const double slope = 1.0 - 1.0 / kLevRatios[ratio], knee = kLevKneesDb[type][ratio], over = levelDb - thresholdDb;
        if (over <= -0.5 * knee) return 0.0;
        if (over >= 0.5 * knee) return slope * over;
        const double t = over + 0.5 * knee;
        return slope * t * t / (2.0 * knee);
    }

    void process(float* left, float* right, int frames) {
        ScopedNoDenormals guard;
        double peakIn[2] = {0, 0}, peakOut[2] = {0, 0}, deepest = 0.0;
        const bool opto = p_.type == kOpto;
        const double k2 = kColour2[p_.type], k3 = kColour3[p_.type];
        float* const io[2] = {left, right};
        for (int i = 0; i < frames; ++i) {
            const double in[2] = {left[i], right[i]};
            peakIn[0] = std::max(peakIn[0], std::fabs(in[0]));
            peakIn[1] = std::max(peakIn[1], std::fabs(in[1]));

            double sl = in[0], sr = in[1];
            if (hpOn_) {
                sl = hp_[0].process(sl);
                sr = hp_[1].process(sr);
            }
            const double level = std::max(std::fabs(sl), std::fabs(sr));
            const double target = level > kneeStart_ ? staticReduction(linToDb(level), p_.threshold, p_.type, p_.ratio) : 0.0;

            fast_ = target + (target > fast_ ? attack_ : release_) * (fast_ - target);
            double reduction = fast_;
            if (opto) {
                slow_ = fast_ + (fast_ > slow_ ? charge_ : tail_) * (slow_ - fast_);
                reduction = std::max(fast_, kOptoTailShare * slow_);
            }
            deepest = std::max(deepest, reduction);

            makeup_ += smooth_ * (makeupTarget_ - makeup_);
            mix_ += smooth_ * (mixTarget_ - mix_);
            colour_ += smooth_ * (colourTarget_ - colour_);
            const double gain = dbToLin(-reduction);
            for (int c = 0; c < 2; ++c) {
                double wet = in[c] * gain;
                // Harmonics, with the offset of the even ones filtered out again.
                const double w = std::clamp(wet * kColourScale, -kColourLimit, kColourLimit);
                const double harmonics = (k2 * w * w - k3 * w * w * w) / kColourScale;
                dcOut_[c] = harmonics - dcIn_[c] + dcCoef_ * dcOut_[c];
                dcIn_[c] = harmonics;
                wet += colour_ * dcOut_[c];

                const double out = in[c] * (1.0 - mix_) + wet * makeup_ * mix_;
                io[c][i] = static_cast<float>(out);
                peakOut[c] = std::max(peakOut[c], std::fabs(out));
            }
        }
        // Let the states come to rest at exactly zero instead of running into denormals.
        if (fast_ < 1e-9) fast_ = 0.0;
        if (slow_ < 1e-9) slow_ = 0.0;
        for (int c = 0; c < 2; ++c)
            if (std::fabs(dcOut_[c]) < 1e-15) dcOut_[c] = 0.0;

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
        const LevellerTiming t = levellerTiming(p_.type, p_.attack, p_.release);
        attack_ = onePoleCoef(t.attackMs, fs_);
        release_ = onePoleCoef(t.releaseMs, fs_);
        tail_ = onePoleCoef(t.tailMs, fs_);
        charge_ = onePoleCoef(1000.0 * kOptoChargeS, fs_);
        if (p_.type != kOpto) {
            // Leaving Opto: the tail its slow stage still holds is handed to the quick one, so the
            // gain carries on from where it is instead of jumping.
            fast_ = std::max(fast_, kOptoTailShare * slow_);
            slow_ = 0.0;
        }

        kneeStart_ = dbToLin(p_.threshold - 0.5 * kLevKneesDb[p_.type][p_.ratio]);
        makeupTarget_ = dbToLin(p_.makeup);
        mixTarget_ = std::clamp(p_.mix, 0.0, 100.0) * 0.01;
        colourTarget_ = std::clamp(p_.colour, 0.0, 100.0) * 0.01;

        const bool on = p_.scHp > kLevScHpOffHz + 0.5;
        if (on) {
            const BiquadCoeffs c = designHighpass(fs_, p_.scHp, 0.7071067811865476);
            for (Biquad& b : hp_) b.set(c);
        }
        if (on && !hpOn_) for (Biquad& b : hp_) b.reset();
        hpOn_ = on;
    }

    LevellerParams p_;
    double fs_ = 48000.0;
    Biquad hp_[2];
    bool hpOn_ = false;
    double attack_ = 0.0, release_ = 0.0, tail_ = 0.0, charge_ = 0.0, smooth_ = 1.0, dcCoef_ = 0.999;
    double kneeStart_ = 0.0, makeupTarget_ = 1.0, mixTarget_ = 1.0, colourTarget_ = 0.2;
    double makeup_ = 1.0, mix_ = 1.0, colour_ = 0.2;
    double fast_ = 0.0, slow_ = 0.0; // gain reduction in dB
    double dcIn_[2] = {0, 0}, dcOut_[2] = {0, 0};
};

} // namespace hnl
