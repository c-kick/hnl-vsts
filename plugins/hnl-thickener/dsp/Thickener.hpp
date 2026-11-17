// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// HNL Thickener: the audio engine.
//
// Adds a second copy of the signal on top of the untouched one. In that copy
// one channel (or both, by different amounts) is a few milliseconds late,
// mixed with a little of itself on time, and the whole copy then passes
// through a small room and, if wanted, a compressor and a saturator:
//
//   in ──┬────────────────────────────────────────────────────────────► + ─► match ─► output
//        │                                                              ▲
//        └─► tone ─► clean hit ─► late channel ─► room ─► crush ─► drive ─► × amount
//
//   tone        low cut and high cut: which part of the sound is thickened
//   clean hit   turns the copy down for the first moment of each hit
//   late        (1 - mix) on time + mix late; below Low Guard next to nothing is late
//   crush       a fast compressor on the copy, as on a parallel drum bus
//   match       takes the loudness the copy added back out, for comparing
//
// The delay is far too short to be heard as an echo. Added to the untouched
// signal it makes shallow dips and rises in that channel, 1 / delay apart
// (333 Hz at 3 ms), which the other channel does not have: a beat that sits
// in the middle gets louder, wider and thicker without being smeared.
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

enum ThickenerSide { kSideLeft = 0, kSideRight, kSideBoth, kSideCount };

constexpr double kThickMaxDelayMs = 20.0;
/** With both channels late, the left one by this much of the delay: its dips never line up with the right one's. */
constexpr double kBothRatio = 0.618;
/** Low Guard, Low Cut and High Cut are off at this end of their range. */
constexpr double kGuardOffHz = 10.0, kLowCutOffHz = 20.0, kHighCutOffHz = 20000.0;
/** Damping 0% and 100% as the frequency above which the room's tail dies away faster. */
constexpr double kRoomDampTopHz = 16000.0, kRoomDampBottomHz = 1000.0;
/** The room's delay lines are this much shorter and longer at Size 0% and 100% than at 50%. */
constexpr double kRoomSizeSpan = 2.0;
/** Clean Hit: the most the copy is turned down on a hit, and the detector's times (as in HNL Transient Shaper). */
constexpr double kCleanRangeDb = 18.0, kCleanKneeDb = 6.0, kCleanSpanDb = 24.0, kCleanFloorDb = -70.0;
constexpr double kCleanHoldMs = 15.0, kCleanPeakReleaseMs = 30.0, kCleanFastAttackMs = 0.2, kCleanFastReleaseMs = 5.0, kCleanSlowMs = 15.0;
/** Crush at 100%: threshold, ratio, and how much of the level lost is given back. */
constexpr double kCrushThresholdDb = -40.0, kCrushRatio = 6.0, kCrushMakeup = 0.85, kCrushAttackMs = 0.3, kCrushReleaseMs = 80.0;
/** Drive at 100%: gain into the saturator, and how much of it is taken back after it. */
constexpr double kDriveDb = 18.0, kDriveTakeBack = 0.6;
/** Level Match: the time over which loudness is compared, and the limits of the gain. */
constexpr double kMatchMs = 300.0, kMatchLowestDb = -24.0, kMatchHighestDb = 12.0;

struct ThickenerParams {
    double amount = 60.0;   // percent: level of the added copy (100 = as loud as the signal itself)
    double delayMs = 3.0;   // how late the delayed channel of the copy is
    double delayMix = 62.0; // percent of that channel that is late; the rest is on time
    int side = kSideRight;  // the delayed channel
    double guard = 0.0;     // Hz: below this nothing is late (off below kGuardOffHz)
    double lowCut = 20.0;   // Hz, on the copy (off at 20)
    double highCut = 20000.0; // Hz, on the copy (off at 20000)
    double clean = 0.0;     // percent: how far the copy is turned down on each hit
    bool room = true;
    double roomMix = 9.0;   // percent of the copy that is room sound
    double size = 50.0;     // percent
    double decay = 0.2;     // seconds for the room's tail to fall by 60 dB
    double damp = 84.0;     // percent
    double crush = 0.0;     // percent: compression of the copy
    double drive = 0.0;     // percent: saturation of the copy
    bool match = false;     // take the added loudness back out
    bool listen = false;    // hear the copy alone
    double output = 0.0;    // dB
};

/** Gain of the added copy for an Amount in percent. */
inline double thickenerGain(double amountPercent) { return std::clamp(amountPercent, 0.0, 100.0) * 0.01; }

/** How late a channel of the copy is in ms, or 0 when it is not delayed. */
inline double thickenerDelayMs(int channel, const ThickenerParams& p) {
    if (p.side == kSideBoth) return channel == 0 ? kBothRatio * p.delayMs : p.delayMs;
    return p.side == channel ? p.delayMs : 0.0;
}

/**
 * What the plugin does to a steady tone of `freq` Hz on channel 0 (left) or
 * 1 (right), in dB. The room's tail, Clean Hit, Crush, Drive and Level Match
 * are left out: they depend on the sound. The UI draws these curves; the
 * tests hold the engine against them.
 */
inline double thickenerResponseDb(double freq, int channel, const ThickenerParams& p) {
    typedef std::complex<double> Cx;
    const Cx j(0.0, 1.0);
    Cx copy(1.0, 0.0);
    if (p.lowCut > kLowCutOffHz) { // second-order Butterworth
        const Cx s = j * (freq / p.lowCut);
        copy *= s * s / (s * s + 1.4142135623730951 * s + 1.0);
    }
    if (p.highCut < kHighCutOffHz) {
        const Cx s = j * (freq / p.highCut);
        copy *= 1.0 / (s * s + 1.4142135623730951 * s + 1.0);
    }
    const double late = thickenerDelayMs(channel, p);
    if (late > 0.0) {
        Cx part(1.0, 0.0); // the part of the channel that may be late
        if (p.guard >= kGuardOffHz) { // a second-order high-pass: it turns the lows down without leaning on them
            const Cx s = j * (freq / p.guard);
            part = s * s / (s * s + 1.4142135623730951 * s + 1.0);
        }
        const double w = std::clamp(p.delayMix, 0.0, 100.0) * 0.01;
        copy *= 1.0 + w * part * (std::polar(1.0, -2.0 * kPi * freq * late * 0.001) - 1.0);
    }
    const double a = thickenerGain(p.amount) * (p.room ? 1.0 - std::clamp(p.roomMix, 0.0, 100.0) * 0.01 : 1.0);
    return linToDb(std::abs((p.listen ? 0.0 : 1.0) + a * copy));
}

/** A delay whose length can change while it runs, by fractions of a sample. */
class MovingDelay {
public:
    void setMax(double samples) {
        buf_.assign(static_cast<size_t>(samples) + 8, 0.0);
        pos_ = 0;
    }
    void reset() { std::fill(buf_.begin(), buf_.end(), 0.0); }
    double longest() const { return static_cast<double>(buf_.size()) - 4.0; }
    /** Stores x and returns the signal `delay` samples ago (1 or more; cubic between samples). */
    inline double process(double x, double delay) {
        const size_t n = buf_.size();
        buf_[pos_] = x;
        const size_t whole = static_cast<size_t>(delay);
        const double f = delay - static_cast<double>(whole);
        const double y0 = at(whole - 1, n), y1 = at(whole, n), y2 = at(whole + 1, n), y3 = at(whole + 2, n);
        if (++pos_ == n) pos_ = 0;
        const double c1 = 0.5 * (y2 - y0), c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3, c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }

private:
    inline double at(size_t ago, size_t n) const { return buf_[pos_ >= ago ? pos_ - ago : pos_ + n - ago]; }
    std::vector<double> buf_ = std::vector<double>(8, 0.0);
    size_t pos_ = 0;
};

/**
 * A small room: two allpass diffusers per channel into eight short delay
 * lines that feed each other (Householder mixing), with high-frequency
 * damping. Left and right go into different lines, so the room hears a
 * difference in time between them. Unlike common/dsp/Reverb.hpp the lines are
 * short, their length can change while running (Size), and the lows are not
 * kept out.
 */
class SmallRoom {
public:
    static constexpr int kLines = 8;

    void prepare(double fs) {
        fs_ = fs;
        static const double apMs[2][2] = {{1.9, 3.1}, {2.3, 3.7}};
        for (int i = 0; i < kLines; ++i) {
            buf_[i].assign(static_cast<size_t>(lineMs(i) * kRoomSizeSpan * 0.001 * fs) + 4, 0.0);
            pos_[i] = 0;
        }
        for (int c = 0; c < 2; ++c)
            for (int a = 0; a < 2; ++a) ap_[c][a].setLength(static_cast<size_t>(apMs[c][a] * 0.001 * fs) | 1u);
        set(size_, decay_, dampFreq_);
        reset();
    }
    /** Size 0..1, the time for the tail to fall by 60 dB in seconds, and the damping frequency. */
    void set(double size, double decaySeconds, double dampFreq) {
        size_ = std::clamp(size, 0.0, 1.0);
        decay_ = std::max(0.05, decaySeconds);
        dampFreq_ = dampFreq;
        const double scale = std::pow(kRoomSizeSpan, 2.0 * size_ - 1.0);
        double mean = 0.0;
        for (int i = 0; i < kLines; ++i) {
            const size_t longest = buf_[i].size() - 1;
            len_[i] = std::clamp<size_t>(static_cast<size_t>(lineMs(i) * scale * 0.001 * fs_) | 1u, 1, longest);
            gain_[i] = std::pow(10.0, -3.0 * static_cast<double>(len_[i]) / (decay_ * fs_));
            mean += gain_[i] * gain_[i] / kLines;
        }
        level_ = 0.45 * std::sqrt(1.0 - mean); // keeps the level roughly independent of the decay time
        damp_ = std::exp(-2.0 * kPi * std::min(dampFreq_, 0.45 * fs_) / fs_);
    }
    void reset() {
        for (auto& b : buf_) std::fill(b.begin(), b.end(), 0.0);
        for (auto& c : ap_) for (auto& a : c) a.reset();
        std::fill(std::begin(lp_), std::end(lp_), 0.0);
    }
    /** Returns the room sound only. */
    inline void process(double l, double r, double& wetL, double& wetR) {
        const double in[2] = {diffuse(0, l), diffuse(1, r)};
        double out[kLines], sum = 0.0;
        for (int i = 0; i < kLines; ++i) {
            const size_t n = buf_[i].size();
            out[i] = buf_[i][pos_[i] >= len_[i] ? pos_[i] - len_[i] : pos_[i] + n - len_[i]];
            sum += out[i];
        }
        wetL = level_ * (out[0] - out[2] + out[4] - out[6]);
        wetR = level_ * (out[1] - out[3] + out[5] - out[7]);
        sum *= 2.0 / kLines;
        for (int i = 0; i < kLines; ++i) {
            const double v = out[i] - sum;
            lp_[i] = v + damp_ * (lp_[i] - v);
            buf_[i][pos_[i]] = gain_[i] * lp_[i] + ((i & 2) ? -in[i & 1] : in[i & 1]);
            if (++pos_[i] == buf_[i].size()) pos_[i] = 0;
        }
    }

private:
    static double lineMs(int i) {
        static const double ms[kLines] = {7.3, 8.9, 10.7, 12.1, 13.9, 15.7, 17.9, 19.3};
        return ms[i];
    }
    inline double diffuse(int ch, double x) {
        for (Delay& d : ap_[ch]) {
            const double delayed = d.peek(), y = delayed - 0.6 * x;
            d.push(x + 0.6 * y);
            x = y;
        }
        return x;
    }

    double fs_ = 48000.0, size_ = 0.5, decay_ = 0.2, dampFreq_ = 1500.0, damp_ = 0.0, level_ = 0.0;
    std::vector<double> buf_[kLines] = {std::vector<double>(4, 0.0), std::vector<double>(4, 0.0), std::vector<double>(4, 0.0), std::vector<double>(4, 0.0),
                                        std::vector<double>(4, 0.0), std::vector<double>(4, 0.0), std::vector<double>(4, 0.0), std::vector<double>(4, 0.0)};
    size_t pos_[kLines] = {}, len_[kLines] = {1, 1, 1, 1, 1, 1, 1, 1};
    double gain_[kLines] = {}, lp_[kLines] = {};
    Delay ap_[2][2];
};

class Thickener {
public:
    struct Meters {
        std::atomic<float> inPeak[2] = {{0.0f}, {0.0f}}; // linear, largest since last read
        std::atomic<float> outPeak[2] = {{0.0f}, {0.0f}};
        std::atomic<float> crush{0.0f};       // dB of gain reduction on the copy, largest since last read
        std::atomic<float> match{0.0f};       // dB that Level Match applies right now
        std::atomic<float> correlation{1.0f}; // of the two outputs over the last moment: 1 = the same, 0 = unrelated
    } meters;

    void prepare(double sampleRate) {
        fs_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        for (MovingDelay& d : delay_) d.setMax(kThickMaxDelayMs * 0.001 * fs_);
        room_.prepare(fs_);
        smooth_ = 1.0 - onePoleCoef(20.0, fs_);
        glide_ = 1.0 - onePoleCoef(60.0, fs_);
        holdSamples_ = static_cast<int>(std::lround(kCleanHoldMs * 0.001 * fs_));
        peakRelease_ = onePoleCoef(kCleanPeakReleaseMs, fs_);
        fastAttack_ = onePoleCoef(kCleanFastAttackMs, fs_);
        fastRelease_ = onePoleCoef(kCleanFastReleaseMs, fs_);
        slowAttack_ = onePoleCoef(kCleanSlowMs, fs_);
        crushAttack_ = onePoleCoef(kCrushAttackMs, fs_);
        crushRelease_ = onePoleCoef(kCrushReleaseMs, fs_);
        window_ = 1.0 - onePoleCoef(kMatchMs, fs_);
        update();
        reset();
    }

    void setParams(const ThickenerParams& p) {
        const ThickenerParams before = p_;
        p_ = p;
        p_.side = std::clamp(p.side, 0, kSideCount - 1);
        update();
        // What a stage still holds from before it was switched off, or from the other channel, is not let out.
        if (p_.side != before.side) resetLate();
        if (guardOn_ && before.guard < kGuardOffHz) for (Biquad& f : guard_) f.reset();
        if (lowCutOn_ && before.lowCut <= kLowCutOffHz) for (Biquad& f : lowCut_) f.reset();
        if (highCutOn_ && before.highCut >= kHighCutOffHz) for (Biquad& f : highCut_) f.reset();
        if (p_.crush > 0.0 && before.crush <= 0.0) crushEnv_ = 0.0;
    }

    void reset() {
        resetLate();
        room_.reset();
        for (Biquad& f : lowCut_) f.reset();
        for (Biquad& f : highCut_) f.reset();
        amount_ = amountTarget_;
        delayMix_ = delayMixTarget_;
        for (int c = 0; c < 2; ++c) delaySamples_[c] = delayTarget_[c];
        roomMix_ = roomTarget_;
        clean_ = cleanTarget_;
        crush_ = crushTarget_;
        drive_ = driveTarget_;
        dry_ = dryTarget_;
        output_ = outputTarget_;
        roomRuns_ = roomTarget_ > 0.0;
        peak_ = 0.0;
        hold_ = 0;
        fast_ = slow_ = kCleanFloorDb;
        crushEnv_ = 0.0;
        loudIn_ = loudOut_ = 0.0;
        matchGain_ = 1.0;
        sumLR_ = sumLL_ = sumRR_ = 0.0;
    }

    void process(float* left, float* right, int frames) {
        ScopedNoDenormals guard;
        double peakIn[2] = {0, 0}, peakOut[2] = {0, 0}, mostCrush = 0.0;
        const double floorLin = dbToLin(kCleanFloorDb);
        for (int i = 0; i < frames; ++i) {
            const double in[2] = {left[i], right[i]};
            peakIn[0] = std::max(peakIn[0], std::fabs(in[0]));
            peakIn[1] = std::max(peakIn[1], std::fabs(in[1]));

            amount_ += smooth_ * (amountTarget_ - amount_);
            delayMix_ += smooth_ * (delayMixTarget_ - delayMix_);
            roomMix_ += smooth_ * (roomTarget_ - roomMix_);
            clean_ += smooth_ * (cleanTarget_ - clean_);
            crush_ += smooth_ * (crushTarget_ - crush_);
            drive_ += smooth_ * (driveTarget_ - drive_);
            dry_ += smooth_ * (dryTarget_ - dry_);
            output_ += smooth_ * (outputTarget_ - output_);

            // The hit detector: a fast and a slow envelope of the input in dB; fast above slow marks a hit.
            const double level = std::max(std::fabs(in[0]), std::fabs(in[1]));
            if (level >= peak_) {
                peak_ = level;
                hold_ = holdSamples_;
            } else if (hold_ > 0) {
                --hold_;
            } else {
                peak_ *= peakRelease_;
            }
            const double env = peak_ > floorLin ? linToDb(peak_) : kCleanFloorDb;
            fast_ = env + (env > fast_ ? fastAttack_ : fastRelease_) * (fast_ - env);
            slow_ = fast_ > slow_ ? std::max(fast_ + slowAttack_ * (slow_ - fast_), fast_ - kCleanSpanDb) : fast_;

            double copy[2] = {in[0], in[1]};

            // Tone: which part of the sound is thickened.
            if (lowCutOn_) for (int c = 0; c < 2; ++c) copy[c] = lowCut_[c].process(copy[c]);
            if (highCutOn_) for (int c = 0; c < 2; ++c) copy[c] = highCut_[c].process(copy[c]);

            // Clean Hit: the copy steps back for the first moment of a hit.
            if (clean_ > 1e-6) {
                const double duck = dbToLin(-clean_ * kCleanRangeDb * (1.0 - std::exp((slow_ - fast_) / kCleanKneeDb)));
                copy[0] *= duck;
                copy[1] *= duck;
            }

            // The late channel: part of it on time, part of it late. Below Low Guard all of it is on time.
            for (int c = 0; c < 2; ++c) {
                if (!late_[c]) continue;
                delaySamples_[c] += glide_ * (delayTarget_[c] - delaySamples_[c]);
                double part = copy[c];
                if (guardOn_) part = guard_[c].process(part);
                copy[c] += delayMix_ * (delay_[c].process(part, delaySamples_[c]) - part);
            }

            // The room. While it is off it does not run, and it starts empty when it comes back.
            if (roomTarget_ > 0.0 && !roomRuns_) {
                room_.reset();
                roomRuns_ = true;
            }
            if (roomRuns_) {
                double wetL, wetR;
                room_.process(copy[0], copy[1], wetL, wetR);
                copy[0] += roomMix_ * (wetL - copy[0]);
                copy[1] += roomMix_ * (wetR - copy[1]);
                if (roomTarget_ <= 0.0 && roomMix_ < 1e-5) {
                    roomMix_ = 0.0;
                    roomRuns_ = false;
                }
            }

            // Crush: a fast compressor on the copy, both channels by the same amount.
            if (crush_ > 1e-6) {
                const double loud = std::max(std::fabs(copy[0]), std::fabs(copy[1]));
                crushEnv_ = loud + (loud > crushEnv_ ? crushAttack_ : crushRelease_) * (crushEnv_ - loud);
                const double threshold = crush_ * kCrushThresholdDb, slope = 1.0 - 1.0 / kCrushRatio;
                const double over = linToDb(crushEnv_) - threshold, cut = over > 0.0 ? over * slope : 0.0;
                const double gain = dbToLin(-threshold * slope * kCrushMakeup - cut);
                copy[0] *= gain;
                copy[1] *= gain;
                mostCrush = std::max(mostCrush, cut);
            }

            // Drive: saturation of the copy.
            if (drive_ > 1e-6) {
                const double g = dbToLin(drive_ * kDriveDb), back = std::pow(g, -kDriveTakeBack);
                copy[0] = std::tanh(g * copy[0]) * back;
                copy[1] = std::tanh(g * copy[1]) * back;
            }

            double out[2] = {dry_ * in[0] + amount_ * copy[0], dry_ * in[1] + amount_ * copy[1]};

            // Level Match: the loudness of the last moment going in against coming out.
            loudIn_ += window_ * (in[0] * in[0] + in[1] * in[1] - loudIn_);
            loudOut_ += window_ * (out[0] * out[0] + out[1] * out[1] - loudOut_);
            double wanted = 1.0;
            if (match_) wanted = loudIn_ > 1e-9 && loudOut_ > 1e-12 ? std::clamp(std::sqrt(loudIn_ / loudOut_), matchLowest_, matchHighest_) : matchGain_;
            matchGain_ += smooth_ * (wanted - matchGain_);

            for (int c = 0; c < 2; ++c) {
                out[c] *= matchGain_ * output_;
                peakOut[c] = std::max(peakOut[c], std::fabs(out[c]));
            }
            left[i] = static_cast<float>(out[0]);
            right[i] = static_cast<float>(out[1]);
            sumLR_ += window_ * (out[0] * out[1] - sumLR_);
            sumLL_ += window_ * (out[0] * out[0] - sumLL_);
            sumRR_ += window_ * (out[1] * out[1] - sumRR_);
        }
        if (peak_ < 1e-12) peak_ = 0.0; // rest at zero instead of running into denormals

        for (int c = 0; c < 2; ++c) {
            raise(meters.inPeak[c], static_cast<float>(peakIn[c]));
            raise(meters.outPeak[c], static_cast<float>(peakOut[c]));
        }
        raise(meters.crush, static_cast<float>(mostCrush));
        meters.match.store(static_cast<float>(linToDb(matchGain_)), std::memory_order_relaxed);
        const double power = std::sqrt(sumLL_ * sumRR_);
        meters.correlation.store(power > 1e-12 ? static_cast<float>(std::clamp(sumLR_ / power, -1.0, 1.0)) : 1.0f, std::memory_order_relaxed);
    }

private:
    static void raise(std::atomic<float>& meter, float value) {
        if (value > meter.load(std::memory_order_relaxed)) meter.store(value, std::memory_order_relaxed);
    }

    void resetLate() {
        for (MovingDelay& d : delay_) d.reset();
        for (Biquad& f : guard_) f.reset();
    }

    void update() {
        amountTarget_ = thickenerGain(p_.amount);
        delayMixTarget_ = std::clamp(p_.delayMix, 0.0, 100.0) * 0.01;
        ThickenerParams inRange = p_;
        inRange.delayMs = std::clamp(p_.delayMs, 0.0, kThickMaxDelayMs);
        for (int c = 0; c < 2; ++c) {
            const double ms = thickenerDelayMs(c, inRange);
            late_[c] = ms > 0.0;
            delayTarget_[c] = std::clamp(ms * 0.001 * fs_, 1.0, delay_[c].longest());
        }
        guardOn_ = p_.guard >= kGuardOffHz;
        lowCutOn_ = p_.lowCut > kLowCutOffHz;
        highCutOn_ = p_.highCut < kHighCutOffHz && p_.highCut < 0.45 * fs_;
        const double q = 0.7071067811865476;
        for (Biquad& f : guard_) f.set(designHighpass(fs_, std::max(p_.guard, kGuardOffHz), q));
        for (Biquad& f : lowCut_) f.set(designHighpass(fs_, p_.lowCut, q));
        for (Biquad& f : highCut_) f.set(designLowpass(fs_, p_.highCut, q));
        cleanTarget_ = std::clamp(p_.clean, 0.0, 100.0) * 0.01;
        roomTarget_ = p_.room ? std::clamp(p_.roomMix, 0.0, 100.0) * 0.01 : 0.0;
        crushTarget_ = std::clamp(p_.crush, 0.0, 100.0) * 0.01;
        driveTarget_ = std::clamp(p_.drive, 0.0, 100.0) * 0.01;
        match_ = p_.match;
        dryTarget_ = p_.listen ? 0.0 : 1.0;
        outputTarget_ = dbToLin(p_.output);
        matchLowest_ = dbToLin(kMatchLowestDb);
        matchHighest_ = dbToLin(kMatchHighestDb);
        const double damp = std::clamp(p_.damp, 0.0, 100.0) * 0.01;
        room_.set(std::clamp(p_.size, 0.0, 100.0) * 0.01, p_.decay, kRoomDampTopHz * std::pow(kRoomDampBottomHz / kRoomDampTopHz, damp));
    }

    ThickenerParams p_;
    double fs_ = 48000.0, smooth_ = 1.0, glide_ = 1.0, window_ = 1.0;
    MovingDelay delay_[2];
    bool late_[2] = {false, true};
    double delayTarget_[2] = {144.0, 144.0}, delaySamples_[2] = {144.0, 144.0};
    bool guardOn_ = false, lowCutOn_ = false, highCutOn_ = false, match_ = false;
    Biquad guard_[2], lowCut_[2], highCut_[2];
    SmallRoom room_;
    bool roomRuns_ = false;
    int holdSamples_ = 720, hold_ = 0;
    double peakRelease_ = 0.0, fastAttack_ = 0.0, fastRelease_ = 0.0, slowAttack_ = 0.0, crushAttack_ = 0.0, crushRelease_ = 0.0;
    double peak_ = 0.0, fast_ = kCleanFloorDb, slow_ = kCleanFloorDb, crushEnv_ = 0.0;
    double loudIn_ = 0.0, loudOut_ = 0.0, matchGain_ = 1.0, matchLowest_ = 0.06, matchHighest_ = 4.0;
    double sumLR_ = 0.0, sumLL_ = 0.0, sumRR_ = 0.0;
    double amountTarget_ = 0.0, delayMixTarget_ = 0.0, roomTarget_ = 0.0, cleanTarget_ = 0.0, crushTarget_ = 0.0, driveTarget_ = 0.0, dryTarget_ = 1.0, outputTarget_ = 1.0;
    double amount_ = 0.0, delayMix_ = 0.0, roomMix_ = 0.0, clean_ = 0.0, crush_ = 0.0, drive_ = 0.0, dry_ = 1.0, output_ = 1.0;
};

} // namespace hnl
