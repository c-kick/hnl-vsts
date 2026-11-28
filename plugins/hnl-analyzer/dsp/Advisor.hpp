// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// The mastering advisor: what has to change to bring a measured sound to a loudness target
// under a true-peak ceiling. Plain arithmetic on three measured figures and three targets, so
// the same figures always give the same advice. It tells apart what a change of gain alone can
// do from what needs the peaks brought down (a limiter or a compressor), and changes nothing
// itself. Header-only, no dependencies.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace hnl {
namespace analyzer {

struct Targets {
    double loudness = -14.0; // integrated, LUFS
    double truePeak = -1.0;  // the ceiling, dBTP
    double plr = 13.0;       // the peak to loudness ratio one likes for this kind of music, dB
};

/** Within this of a target counts as on it: what a meter shows as the same figure, and far less than can be heard. */
constexpr double kOnTarget = 0.1;
/** How far the PLR may lie from the preferred one before the advisor remarks on it. */
constexpr double kPlrSlack = 1.0;

struct Advice {
    enum Kind {
        kNoMeasurement, // nothing measured, or only silence
        kAchieved,      // loudness on target, peaks under the ceiling
        kGain,          // a change of gain alone does it
        kDynamics,      // the gain that reaches the loudness would push the peaks over the ceiling
        kPeaks,         // loudness on target, but the peaks are over the ceiling
    };
    Kind kind = kNoMeasurement;

    double loudness = 0, truePeak = 0, plr = 0; // as measured
    double gain = 0;            // to reach the loudness target
    double maxGain = 0;         // the most gain the ceiling allows; below zero when the peaks are over it already
    double peakReduction = 0;   // the least the peaks must come down so that both targets are met
    double targetPlr = 0;       // the largest PLR that fits both targets: ceiling minus loudness target
    double loudnessAtMaxGain = 0; // where the loudness ends up with maxGain and nothing else
    double plrFromPreferred = 0;  // measured PLR minus the preferred one
};

/** `loudness` in LUFS and `truePeak` in dBTP as measured; `valid` false when there is nothing to judge. */
inline Advice advise(bool valid, double loudness, double truePeak, const Targets& t) {
    Advice a;
    if (!valid) return a;
    a.loudness = loudness;
    a.truePeak = truePeak;
    a.plr = truePeak - loudness;
    a.gain = t.loudness - loudness;
    a.maxGain = t.truePeak - truePeak;
    a.peakReduction = std::max(0.0, a.gain - a.maxGain);
    a.targetPlr = t.truePeak - t.loudness;
    a.loudnessAtMaxGain = loudness + a.maxGain;
    a.plrFromPreferred = a.plr - t.plr;
    const bool loud = std::fabs(a.gain) <= kOnTarget, under = a.maxGain >= -kOnTarget;
    if (loud && under) a.kind = Advice::kAchieved;
    else if (loud) a.kind = Advice::kPeaks;
    else if (a.peakReduction <= kOnTarget) a.kind = Advice::kGain;
    else a.kind = Advice::kDynamics;
    return a;
}

inline const char* adviceTitle(const Advice& a) {
    switch (a.kind) {
    case Advice::kAchieved: return "TARGET ACHIEVED";
    case Advice::kGain: return "GAIN ADJUSTMENT";
    case Advice::kDynamics: return "DYNAMIC PROCESSING";
    case Advice::kPeaks: return "PEAKS OVER THE CEILING";
    default: return "NO MEASUREMENT";
    }
}

/** The advice in words: what to do, then what the PLR says. Figures with two decimals, as the window shows them. */
inline std::string adviceText(const Advice& a, const Targets& t) {
    char s[400];
    std::string out;
    switch (a.kind) {
    case Advice::kNoMeasurement:
        return "Nothing measured yet: capture a track, or open a file.";
    case Advice::kAchieved:
        std::snprintf(s, sizeof(s), "Loudness is on target and the true peak is %.2f dB under the ceiling. Nothing to change.", std::max(0.0, a.maxGain));
        out = s;
        break;
    case Advice::kGain:
        std::snprintf(s, sizeof(s), "%s the level by %.2f dB. Gain alone does it: the true peak ends at %.2f dBTP, under the ceiling of %.2f.",
                      a.gain > 0 ? "Raise" : "Lower", std::fabs(a.gain), a.truePeak + a.gain, t.truePeak);
        out = s;
        break;
    case Advice::kDynamics:
        if (a.maxGain > kOnTarget)
            std::snprintf(s, sizeof(s), "Gain alone reaches %.2f LUFS (%+.2f dB) before the peaks touch the ceiling. For the target: raise the level by %.2f dB and bring the peaks down by at least %.2f dB with a limiter or compressor.",
                          a.loudnessAtMaxGain, a.maxGain, a.gain, a.peakReduction);
        else
            std::snprintf(s, sizeof(s), "The peaks leave no room for gain. For the target: raise the level by %.2f dB and bring the peaks down by at least %.2f dB with a limiter or compressor.",
                          a.gain, a.peakReduction);
        out = s;
        break;
    case Advice::kPeaks:
        std::snprintf(s, sizeof(s), "Loudness is on target, but the true peak is %.2f dB over the ceiling. Bring the peaks down by that much with a limiter; lowering the level instead would leave the loudness at %.2f LUFS.",
                      -a.maxGain, a.loudnessAtMaxGain);
        out = s;
        break;
    }
    if (a.plrFromPreferred > kPlrSlack)
        std::snprintf(s, sizeof(s), " PLR %.1f dB above the preferred %.1f: more dynamic than usual for this kind of music.", a.plrFromPreferred, t.plr);
    else if (a.plrFromPreferred < -kPlrSlack)
        std::snprintf(s, sizeof(s), " PLR %.1f dB under the preferred %.1f: already more limited than usual; more limiting costs punch.", -a.plrFromPreferred, t.plr);
    else
        std::snprintf(s, sizeof(s), " PLR within %.0f dB of the preferred %.1f.", kPlrSlack, t.plr);
    return out + s;
}

/** Where the limiter of HNL Master stands. */
struct Limiter {
    double gain = 0.0;     // dB
    double ceiling = -1.0; // dBTP
    bool on = true;
};
/** How far its two knobs go. */
constexpr double kLimiterGainMin = 0.0, kLimiterGainMax = 24.0, kLimiterCeilingMin = -6.0, kLimiterCeilingMax = 0.0;

/** What to set the limiter of HNL Master to, for a measurement that was made behind it. */
struct LimiterChange {
    bool any = false;     // there is something to set
    bool reaches = true;  // the knobs go as far as the targets ask
    double gain = 0.0, ceiling = -1.0;
};

/**
 * The ceiling goes to the true-peak target and the gain changes by what the loudness is from its
 * target, both in tenths of a decibel as the knobs show them. The limiter then holds the peaks
 * that the gain pushes up, which costs a little of the loudness: so the track is measured again
 * afterwards, and a second time brings it the rest of the way.
 */
inline LimiterChange changeFor(const Advice& a, const Targets& t, const Limiter& now) {
    LimiterChange c;
    c.gain = now.gain;
    c.ceiling = now.ceiling;
    if (a.kind == Advice::kNoMeasurement || a.kind == Advice::kAchieved) return c;
    const auto tenths = [](double v) { return std::round(v * 10.0) / 10.0; };
    const double gain = (now.on ? now.gain : 0.0) + a.gain; // a limiter that is off added nothing to what was measured
    c.gain = tenths(std::clamp(gain, kLimiterGainMin, kLimiterGainMax));
    c.ceiling = tenths(std::clamp(t.truePeak, kLimiterCeilingMin, kLimiterCeilingMax));
    c.reaches = std::fabs(c.gain - gain) < 0.06 && std::fabs(c.ceiling - t.truePeak) < 0.06;
    c.any = !now.on || std::fabs(c.gain - now.gain) > 0.04 || std::fabs(c.ceiling - now.ceiling) > 0.04;
    return c;
}

} // namespace analyzer
} // namespace hnl
