// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// The measurements of HNL Analyzer. One class, Meter, takes sound in blocks of any length and
// keeps everything it needs in memory of a fixed size; Result is what it has measured so far.
// A capture during playback, the running meter and a file read from disk all go through this
// one class, so the three give the same figures for the same sound.
//
// What is measured, and how:
//   loudness     ITU-R BS.1770-4 / EBU R128: momentary (400 ms), short-term (3 s) and integrated
//                (blocks of 400 ms every 100 ms, gated at -70 LUFS and at 10 LU under the mean)
//   range        EBU Tech 3342: the short-term values every 100 ms, gated at -70 LUFS and at 20 LU
//                under their mean; from the 10th to the 95th percentile
//   true peak    BS.1770 Annex 2, with eight times oversampling (common/dsp/Filters.hpp)
//   RMS          of each channel over the whole sound, and of windows of six lengths that follow
//                each other without overlap: their lowest, highest and mean level, and how often
//                each level occurred (the histogram)
//   the rest     sample peak, lowest and highest sample, DC offset, clipped samples, correlation,
//                mid and side, the mean spectrum in third octaves, the outline of the sound and
//                the loudness over time
//
// Levels in a Result are plain numbers (1.0 is full scale), mean squares or LUFS; the functions
// at the end turn them into the decibels a window shows. Header-only, no dependencies.
#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <vector>

#include "../../../common/dsp/Filters.hpp"
#include "../../../common/dsp/Metering.hpp"

namespace hnl {
namespace analyzer {

constexpr int kRmsWindows = 6;
/** The lengths of the RMS windows, in milliseconds. All are whole numbers of ticks (kTickMs). */
constexpr double kRmsWindowMs[kRmsWindows] = {10, 25, 50, 100, 300, 1000};
constexpr double kTickMs = 5.0;        // the grain of the windows, the outline and the loudness blocks
constexpr int kHistBins = 192;         // half a decibel each
constexpr double kHistFloorDb = -96.0; // the level of the first one: RMS as a plain number, in dB
constexpr int kSpecBands = 30;         // third octaves from 20 Hz to 20 kHz
constexpr int kSeries = 2048;          // points of the outline and of the loudness over time
constexpr double kSilence = -100.0;    // what a loudness reads when there is nothing to measure

/** Lower edge of a band of the spectrum in Hz; specEdge(kSpecBands) is the upper edge of the last. */
inline double specEdge(int band) { return 20.0 * std::pow(1000.0, static_cast<double>(band) / kSpecBands); }

/** One channel over the whole sound. */
struct ChannelStats {
    double samplePeak = 0.0, truePeak = 0.0; // the largest sample, and the largest value between the samples
    double minSample = 0.0, maxSample = 0.0;
    double sum = 0.0, sumSquares = 0.0;      // of all samples
    uint64_t clipped = 0;
};

/** The windows of one length. */
struct RmsWindowStats {
    uint64_t count = 0;                // windows that were full
    double minMs[2] = {0, 0}, maxMs[2] = {0, 0}; // mean square of the quietest and of the loudest window
    double sumRms[2] = {0, 0};         // of the windows' RMS values, for their mean
    uint32_t hist[2][kHistBins] = {};  // how many windows had each level
};

/**
 * What has been measured. Plain data: copied whole from the audio thread to the window, and
 * written to a song or a file by Report.hpp.
 */
struct Result {
    double sampleRate = 0.0;
    int channels = 2;        // 1 for a mono file, whose second channel is not there
    uint64_t frames = 0;

    double integrated = kSilence, momentary = kSilence, shortTerm = kSilence;
    double momentaryMax = kSilence, shortTermMax = kSilence;
    double range = 0.0, rangeLow = kSilence, rangeHigh = kSilence; // LRA, and the two levels it lies between

    ChannelStats ch[2];
    double sumLR = 0.0;      // of left times right, for the correlation and for mid and side
    double correlationNow = 0.0; // over the last 0.3 s

    RmsWindowStats rms[kRmsWindows];

    double spectrum[kSpecBands] = {}; // mean square in each band; 0.5 is a sine at full scale
    uint64_t spectra = 0;             // transforms it is the mean of

    // The outline: the lowest and highest sample of each channel in every stretch of waveStep seconds.
    int waveCount = 0;
    double waveStep = 0.0;
    float waveMin[2][kSeries] = {}, waveMax[2][kSeries] = {};
    // The loudness over time: momentary and short-term, every loudStep seconds.
    int loudCount = 0;
    double loudStep = 0.0;
    float loudM[kSeries] = {}, loudS[kSeries] = {};

    double seconds() const { return sampleRate > 0.0 ? static_cast<double>(frames) / sampleRate : 0.0; }
    bool empty() const { return frames == 0; }
};

// ------------------------------------------------------------ what a result says

/** Decibels of a plain level; kSilence for nothing. */
inline double levelDb(double lin) { return lin > 1e-10 ? 20.0 * std::log10(lin) : kSilence; }
/** Decibels of a mean square. */
inline double powerDb(double ms) { return ms > 1e-20 ? 10.0 * std::log10(ms) : kSilence; }
/** What is added to an RMS level so that a sine at full scale reads 0 dB (as Adobe Audition does unless told otherwise). */
constexpr double kSineRef = 3.0102999566398120;

inline double truePeakDb(const Result& r, int ch) { return levelDb(r.ch[ch].truePeak); }
inline double samplePeakDb(const Result& r, int ch) { return levelDb(r.ch[ch].samplePeak); }
/** The higher of the channels. */
inline double truePeakDb(const Result& r) { return levelDb(std::max(r.ch[0].truePeak, r.channels > 1 ? r.ch[1].truePeak : 0.0)); }
inline double samplePeakDb(const Result& r) { return levelDb(std::max(r.ch[0].samplePeak, r.channels > 1 ? r.ch[1].samplePeak : 0.0)); }
inline bool hasLoudness(const Result& r) { return r.integrated > kSilence + 1.0; }
/** Peak to loudness ratio: how far the highest true peak stands above the integrated loudness. */
inline double plr(const Result& r) { return hasLoudness(r) ? truePeakDb(r) - r.integrated : 0.0; }

/** RMS of a channel over the whole sound, in dB; `sine` adds kSineRef. */
inline double totalRmsDb(const Result& r, int ch, bool sine) {
    if (r.frames == 0) return kSilence;
    const double db = powerDb(r.ch[ch].sumSquares / static_cast<double>(r.frames));
    return db <= kSilence ? kSilence : db + (sine ? kSineRef : 0.0);
}
inline double windowDb(double ms, bool sine) {
    const double db = powerDb(ms);
    return db <= kSilence ? kSilence : db + (sine ? kSineRef : 0.0);
}
inline double minRmsDb(const Result& r, int window, int ch, bool sine) { return r.rms[window].count ? windowDb(r.rms[window].minMs[ch], sine) : kSilence; }
inline double maxRmsDb(const Result& r, int window, int ch, bool sine) { return r.rms[window].count ? windowDb(r.rms[window].maxMs[ch], sine) : kSilence; }
/** The mean of the windows' RMS values, as a level. */
inline double averageRmsDb(const Result& r, int window, int ch, bool sine) {
    const RmsWindowStats& w = r.rms[window];
    if (w.count == 0) return kSilence;
    const double db = levelDb(w.sumRms[ch] / static_cast<double>(w.count));
    return db <= kSilence ? kSilence : db + (sine ? kSineRef : 0.0);
}
/** How far the sample peak of a channel stands above its RMS (a sine: 3 dB). */
inline double crestDb(const Result& r, int ch) {
    const double rms = totalRmsDb(r, ch, false), peak = samplePeakDb(r, ch);
    return rms <= kSilence || peak <= kSilence ? 0.0 : peak - rms;
}
/** The mean of a channel's samples as a part of full scale (-1..1). */
inline double dcOffset(const Result& r, int ch) { return r.frames ? r.ch[ch].sum / static_cast<double>(r.frames) : 0.0; }
/** Left against right over the whole sound, in dB: above zero the left is louder. */
inline double balanceDb(const Result& r) {
    if (r.channels < 2 || r.ch[0].sumSquares <= 0.0 || r.ch[1].sumSquares <= 0.0) return 0.0;
    return 10.0 * std::log10(r.ch[0].sumSquares / r.ch[1].sumSquares);
}
/** How alike left and right are over the whole sound: +1 the same, 0 unrelated, -1 opposite. */
inline double correlation(const Result& r) {
    const double e = r.ch[0].sumSquares * r.ch[1].sumSquares;
    return r.channels > 1 && e > 1e-30 ? std::clamp(r.sumLR / std::sqrt(e), -1.0, 1.0) : (r.channels > 1 ? 0.0 : 1.0);
}
/** RMS of the mid (half the sum) and of the side (half the difference) over the whole sound, in dB. */
inline double midDb(const Result& r, bool sine) {
    if (r.frames == 0) return kSilence;
    return windowDb((r.ch[0].sumSquares + r.ch[1].sumSquares + 2.0 * r.sumLR) / (4.0 * static_cast<double>(r.frames)), sine);
}
inline double sideDb(const Result& r, bool sine) {
    if (r.frames == 0 || r.channels < 2) return kSilence;
    return windowDb(std::max(0.0, r.ch[0].sumSquares + r.ch[1].sumSquares - 2.0 * r.sumLR) / (4.0 * static_cast<double>(r.frames)), sine);
}
/** A band of the mean spectrum in dB; a sine at full scale reads 0. */
inline double spectrumDb(const Result& r, int band) { return r.spectra ? std::max(kSilence, powerDb(2.0 * r.spectrum[band])) : kSilence; }
inline uint64_t clipped(const Result& r) { return r.ch[0].clipped + (r.channels > 1 ? r.ch[1].clipped : 0); }
/** The bin of the histogram a window of this mean square falls in. */
inline int histBin(double ms) {
    const double db = powerDb(ms);
    const int bin = static_cast<int>(std::floor((db - kHistFloorDb) * 2.0));
    return bin < 0 ? 0 : (bin >= kHistBins ? kHistBins - 1 : bin);
}

// ----------------------------------------------------------------- parts

/**
 * A row of values over time in a fixed number of places. Two ways to stay within them: for a
 * capture the row is halved when it is full, each two neighbours becoming one that covers
 * twice the time; for the running meter the oldest value makes way (`rolling`).
 */
template <int kChannels> class Series {
public:
    /** `stride`: how many pushed values make one place to begin with. */
    void reset(bool rolling, int stride = 1) {
        rolling_ = rolling;
        count_ = first_ = fill_ = 0;
        stride_ = stride < 1 ? 1 : stride;
    }
    /** One value for each row; `higher` says, for each row, whether the higher or the lower of two values is kept. */
    inline void push(const float* v, const bool* higher) {
        if (fill_ == 0) for (int c = 0; c < kChannels; ++c) pending_[c] = v[c];
        else for (int c = 0; c < kChannels; ++c) pending_[c] = higher[c] ? std::max(pending_[c], v[c]) : std::min(pending_[c], v[c]);
        if (++fill_ < stride_) return;
        fill_ = 0;
        if (count_ == kSeries) {
            if (rolling_) {
                first_ = (first_ + 1) % kSeries;
                --count_;
            } else {
                for (int c = 0; c < kChannels; ++c)
                    for (int i = 0; i < kSeries / 2; ++i) {
                        const float a = data_[c][2 * i], b = data_[c][2 * i + 1];
                        data_[c][i] = higher[c] ? std::max(a, b) : std::min(a, b);
                    }
                count_ = kSeries / 2;
                stride_ *= 2;
                // The value in hand was a whole place at the old grain: it is the first half of the next one.
                fill_ = stride_ / 2;
                return;
            }
        }
        const int at = (first_ + count_) % kSeries;
        for (int c = 0; c < kChannels; ++c) data_[c][at] = pending_[c];
        ++count_;
    }
    int count() const { return count_; }
    /** Values pushed for each place. */
    int stride() const { return stride_; }
    void copy(int row, float* out) const {
        for (int i = 0; i < count_; ++i) out[i] = data_[row][(first_ + i) % kSeries];
    }

private:
    float data_[kChannels][kSeries] = {};
    float pending_[kChannels] = {};
    int count_ = 0, first_ = 0, fill_ = 0, stride_ = 1;
    bool rolling_ = false;
};

/**
 * The mean spectrum: transforms of about 0.17 s, windowed, every half their length, their power
 * added up per third octave. A bin that lies across the edge of a band is shared between the two.
 */
class MeanSpectrum {
public:
    void prepare(double fs) {
        fs_ = fs;
        n_ = 1024;
        while (n_ < 0.16 * fs && n_ < 65536) n_ *= 2;
        bits_ = 0;
        while ((1 << bits_) < n_) ++bits_;
        in_.assign(n_, 0.0);
        window_.resize(n_);
        twiddle_.resize(n_ / 2);
        work_.resize(n_);
        reverse_.resize(n_);
        double squares = 0.0;
        for (int i = 0; i < n_; ++i) {
            window_[i] = 0.5 - 0.5 * std::cos(2.0 * kPi * i / n_);
            squares += window_[i] * window_[i];
            int j = 0;
            for (int b = 0; b < bits_; ++b) j |= ((i >> b) & 1) << (bits_ - 1 - b);
            reverse_[i] = j;
        }
        for (int i = 0; i < n_ / 2; ++i) twiddle_[i] = std::polar(1.0, -2.0 * kPi * i / n_);
        scale_ = 2.0 / (static_cast<double>(n_) * squares); // the mean square of the sound, from one side of the transform
        // Which bins belong to which band, and for how much.
        const double hz = fs / n_;
        for (int b = 0; b <= kSpecBands; ++b) edge_[b] = specEdge(b) / hz;
        reset();
    }
    void reset() {
        std::fill(in_.begin(), in_.end(), 0.0);
        pos_ = fill_ = 0;
        seen_ = 0;
        std::fill(std::begin(sum), std::end(sum), 0.0);
        count = 0;
    }
    inline void process(double x) {
        in_[pos_] = x;
        if (++pos_ == n_) pos_ = 0;
        ++seen_;
        if (++fill_ < n_ / 2) return;
        fill_ = 0;
        if (seen_ >= static_cast<uint64_t>(n_)) transform(); // not before the first one is full
    }

    double sum[kSpecBands] = {}; // of the mean squares per band
    uint64_t count = 0;

private:
    void transform() {
        for (int i = 0; i < n_; ++i) work_[reverse_[i]] = window_[i] * in_[(pos_ + i) & (n_ - 1)];
        for (int len = 2; len <= n_; len *= 2)
            for (int start = 0; start < n_; start += len)
                for (int k = 0; k < len / 2; ++k) {
                    const std::complex<double> a = work_[start + k], b = work_[start + k + len / 2] * twiddle_[k * (n_ / len)];
                    work_[start + k] = a + b;
                    work_[start + k + len / 2] = a - b;
                }
        int band = 0;
        for (int k = 1; k < n_ / 2; ++k) {
            const double lo = k - 0.5, hi = k + 0.5; // the stretch of the spectrum this bin stands for
            if (hi <= edge_[0]) continue;
            if (lo >= edge_[kSpecBands]) break;
            const double power = std::norm(work_[k]) * scale_;
            while (band < kSpecBands - 1 && edge_[band + 1] <= lo) ++band;
            for (int b = band; b < kSpecBands && edge_[b] < hi; ++b) {
                const double part = std::min(hi, edge_[b + 1]) - std::max(lo, edge_[b]);
                if (part > 0.0) sum[b] += power * part;
            }
        }
        ++count;
    }

    double fs_ = 48000.0, scale_ = 1.0, edge_[kSpecBands + 1] = {};
    int n_ = 8192, bits_ = 13, pos_ = 0, fill_ = 0;
    uint64_t seen_ = 0;
    std::vector<double> in_, window_;
    std::vector<int> reverse_;
    std::vector<std::complex<double>> twiddle_, work_;
};

/**
 * Loudness as BS.1770 and EBU R128 define it. Fed with the K-weighted energy of each tick; a
 * block is 100 ms, so the 400 ms blocks of the integrated loudness overlap by three quarters.
 * The blocks are counted per hundredth of a LU, from which the gated mean and the range follow
 * to within 0.005 LU however long the sound is.
 */
class Loudness {
public:
    static constexpr int kBins = 8000;         // hundredths of a LU from -70 to +10 LUFS
    static constexpr int kTicksPerBlock = 20;  // 100 ms

    Loudness() {
        energy_.resize(kBins);
        for (int b = 0; b < kBins; ++b) energy_[b] = std::pow(10.0, (-70.0 + 0.01 * (b + 0.5) + 0.691) / 10.0);
        gating_.resize(kBins);
        shortTerm_.resize(kBins);
    }
    void reset() {
        std::fill(std::begin(blocks_), std::end(blocks_), 0.0);
        std::fill(gating_.begin(), gating_.end(), 0u);
        std::fill(shortTerm_.begin(), shortTerm_.end(), 0u);
        pos_ = count_ = ticks_ = 0;
        acc_ = 0.0;
        frames_ = 0;
        momentary = shortTerm = momentaryMax = shortTermMax = kSilence;
    }
    /** The K-weighted squares of `frames` frames, all channels added. True when a block of 100 ms ended. */
    inline bool tick(double energy, uint32_t frames) {
        acc_ += energy;
        frames_ += frames;
        if (++ticks_ < kTicksPerBlock) return false;
        blocks_[pos_] = frames_ ? acc_ / frames_ : 0.0;
        pos_ = (pos_ + 1) % kBlocks;
        count_ = std::min(count_ + 1, kBlocks);
        acc_ = 0.0;
        frames_ = 0;
        ticks_ = 0;
        momentary = toLufs(meanOfLast(4));
        shortTerm = toLufs(meanOfLast(kBlocks));
        if (count_ >= 4) {
            momentaryMax = std::max(momentaryMax, momentary);
            count(gating_, momentary);
        }
        if (count_ >= kBlocks) {
            shortTermMax = std::max(shortTermMax, shortTerm);
            count(shortTerm_, shortTerm);
        }
        return true;
    }
    /** The integrated loudness of everything since reset. */
    double integrated() const {
        double sum = 0.0;
        uint64_t n = 0;
        for (int b = 0; b < kBins; ++b)
            if (gating_[b]) { sum += gating_[b] * energy_[b]; n += gating_[b]; }
        if (n == 0) return kSilence;
        const int first = binAbove(toLufs(sum / n) - 10.0);
        sum = 0.0;
        n = 0;
        for (int b = first; b < kBins; ++b)
            if (gating_[b]) { sum += gating_[b] * energy_[b]; n += gating_[b]; }
        return n ? toLufs(sum / n) : kSilence;
    }
    /** The loudness range, and the two levels it lies between. */
    void range(double& lra, double& low, double& high) const {
        lra = 0.0;
        low = high = kSilence;
        double sum = 0.0;
        uint64_t n = 0;
        for (int b = 0; b < kBins; ++b)
            if (shortTerm_[b]) { sum += shortTerm_[b] * energy_[b]; n += shortTerm_[b]; }
        if (n == 0) return;
        const int first = binAbove(toLufs(sum / n) - 20.0);
        n = 0;
        for (int b = first; b < kBins; ++b) n += shortTerm_[b];
        if (n == 0) return;
        // As in EBU Tech 3342: of the values in order, the one a tenth of the way and the one at 95 %.
        const uint64_t lowAt = static_cast<uint64_t>(std::llround((n - 1) * 0.10)), highAt = static_cast<uint64_t>(std::llround((n - 1) * 0.95));
        uint64_t seen = 0;
        bool haveLow = false;
        for (int b = first; b < kBins; ++b) {
            if (!shortTerm_[b]) continue;
            seen += shortTerm_[b];
            if (!haveLow && seen > lowAt) { low = -70.0 + 0.01 * (b + 0.5); haveLow = true; }
            if (seen > highAt) { high = -70.0 + 0.01 * (b + 0.5); break; }
        }
        lra = std::max(0.0, high - low);
    }

    double momentary = kSilence, shortTerm = kSilence, momentaryMax = kSilence, shortTermMax = kSilence;

private:
    static constexpr int kBlocks = 30; // 3 s
    static double toLufs(double ms) { return ms > 1e-12 ? -0.691 + 10.0 * std::log10(ms) : kSilence; }
    /** The first bin whose blocks are louder than `lufs`. */
    static int binAbove(double lufs) { return std::clamp(static_cast<int>(std::ceil((lufs + 70.0) * 100.0 - 0.5)), 0, kBins); }
    static void count(std::vector<uint32_t>& bins, double lufs) {
        const int bin = static_cast<int>(std::floor((lufs + 70.0) * 100.0));
        if (bin >= 0) ++bins[std::min(bin, kBins - 1)]; // below -70 LUFS: the absolute gate
    }
    double meanOfLast(int n) const {
        n = std::min(n, count_);
        if (n == 0) return 0.0;
        double s = 0.0;
        for (int i = 0; i < n; ++i) s += blocks_[(pos_ - 1 - i + 2 * kBlocks) % kBlocks];
        return s / n;
    }

    std::vector<double> energy_;
    std::vector<uint32_t> gating_, shortTerm_;
    double blocks_[kBlocks] = {}, acc_ = 0.0;
    uint32_t frames_ = 0;
    int pos_ = 0, count_ = 0, ticks_ = 0;
};

// ------------------------------------------------------------------ meter

/**
 * Measures a sound. prepare() allocates; after it nothing does, and process() takes the same
 * time for every sample whatever came before, apart from one transform of the spectrum every
 * 0.085 s. Not for two threads at once.
 */
class Meter {
public:
    /** A sample at or above this counts as clipped when its neighbour is there too; above full scale it counts alone. */
    static constexpr double kClipLevel = 0.9999;

    /** `channels` 1 measures the left input alone, as a mono file is measured. */
    void prepare(double fs, int channels = 2) {
        fs_ = fs > 0.0 ? fs : 48000.0;
        channels_ = channels == 1 ? 1 : 2;
        for (auto& k : weight_) k.prepare(fs_);
        spectrum_.prepare(fs_);
        correlation_.prepare(fs_);
        for (int w = 0; w < kRmsWindows; ++w) ticksPerWindow_[w] = static_cast<int>(std::lround(kRmsWindowMs[w] / kTickMs));
        reset(false);
    }
    /** Forgets everything. `rolling`: the outline and the loudness over time keep the newest instead of the whole. */
    void reset(bool rolling) {
        for (auto& k : weight_) k.reset();
        for (auto& t : truePeak_) t.reset();
        spectrum_.reset();
        correlation_.reset();
        loudness_.reset();
        // Rolling, the outline is as coarse as the loudness (100 ms), so that both show the same last minutes.
        wave_.reset(rolling, rolling ? Loudness::kTicksPerBlock : 1);
        loud_.reset(rolling);
        for (auto& c : ch_) c = ChannelStats();
        for (auto& w : rms_) w = RmsWindowStats();
        for (auto& c : clip_) c = Clip();
        std::memset(windowAcc_, 0, sizeof(windowAcc_));
        std::memset(windowTicks_, 0, sizeof(windowTicks_));
        std::memset(windowFrames_, 0, sizeof(windowFrames_));
        frames_ = 0;
        sumLR_ = 0.0;
        tick_ = 0;
        tickFill_ = 0;
        tickEnd_ = boundary(1);
        tickEnergy_ = tickSquares_[0] = tickSquares_[1] = 0.0;
        tickMin_[0] = tickMin_[1] = tickMax_[0] = tickMax_[1] = 0.0f;
        flushed_ = false;
    }

    double sampleRate() const { return fs_; }
    uint64_t frames() const { return frames_; }

    /** `r` may be null for one channel. */
    void process(const float* l, const float* r, uint32_t n) {
        if (channels_ == 1) r = l;
        for (uint32_t i = 0; i < n; ++i) {
            const double x[2] = {l[i], r[i]};
            double energy = 0.0;
            for (int c = 0; c < channels_; ++c) {
                const double v = x[c], a = std::fabs(v);
                ChannelStats& s = ch_[c];
                if (frames_ == 0) s.minSample = s.maxSample = v;
                s.minSample = std::min(s.minSample, v);
                s.maxSample = std::max(s.maxSample, v);
                s.samplePeak = std::max(s.samplePeak, a);
                s.truePeak = std::max(s.truePeak, truePeak_[c].process(v));
                s.sum += v;
                s.sumSquares += v * v;
                tickSquares_[c] += v * v;
                clip_[c].see(v, a, s.clipped);
                const double k = weight_[c].process(v);
                energy += k * k;
                const float f = static_cast<float>(v);
                if (tickFill_ == 0) tickMin_[c] = tickMax_[c] = f;
                else { tickMin_[c] = std::min(tickMin_[c], f); tickMax_[c] = std::max(tickMax_[c], f); }
            }
            if (channels_ == 2) {
                sumLR_ += x[0] * x[1];
                correlation_.process(x[0], x[1]);
                spectrum_.process(0.5 * (x[0] + x[1]));
            } else {
                spectrum_.process(x[0]);
            }
            tickEnergy_ += energy;
            ++tickFill_;
            if (++frames_ == tickEnd_) endTick();
        }
    }

    /**
     * The end of the sound: the true peak looks 16 samples ahead, so the last ones are only
     * judged once silence has followed them. Nothing else changes; process() may go on after it.
     */
    void flush() {
        if (flushed_) return;
        for (int c = 0; c < channels_; ++c)
            for (int i = 0; i < 2 * TruePeak::latency(); ++i) ch_[c].truePeak = std::max(ch_[c].truePeak, truePeak_[c].process(0.0));
        flushed_ = true;
    }

    /** Everything measured so far. */
    void snapshot(Result& r) const {
        r.sampleRate = fs_;
        r.channels = channels_;
        r.frames = frames_;
        r.momentary = loudness_.momentary;
        r.shortTerm = loudness_.shortTerm;
        r.momentaryMax = loudness_.momentaryMax;
        r.shortTermMax = loudness_.shortTermMax;
        r.integrated = loudness_.integrated();
        loudness_.range(r.range, r.rangeLow, r.rangeHigh);
        for (int c = 0; c < 2; ++c) r.ch[c] = c < channels_ ? ch_[c] : ChannelStats();
        r.sumLR = sumLR_;
        r.correlationNow = channels_ == 2 ? correlation_.value : 1.0;
        for (int w = 0; w < kRmsWindows; ++w) r.rms[w] = rms_[w];
        for (int b = 0; b < kSpecBands; ++b) r.spectrum[b] = spectrum_.count ? spectrum_.sum[b] / static_cast<double>(spectrum_.count) : 0.0;
        r.spectra = spectrum_.count;
        r.waveCount = wave_.count();
        r.waveStep = wave_.stride() * kTickMs * 0.001;
        for (int c = 0; c < 2; ++c) {
            wave_.copy(c, r.waveMin[c]);
            wave_.copy(2 + c, r.waveMax[c]);
        }
        r.loudCount = loud_.count();
        r.loudStep = loud_.stride() * 0.1;
        loud_.copy(0, r.loudM);
        loud_.copy(1, r.loudS);
    }

private:
    struct Clip {
        int run = 0;
        bool positive = false, counted = false;
        inline void see(double v, double a, uint64_t& total) {
            if (a < kClipLevel) {
                run = 0;
                counted = false;
                return;
            }
            const bool up = v > 0.0;
            run = run > 0 && up == positive ? run + 1 : 1;
            positive = up;
            if (run >= 2) {
                total += counted ? 1 : 2; // the one before it was not known to be clipped until now
                counted = true;
            } else if (a > 1.0) {
                ++total;
                counted = true;
            } else {
                counted = false;
            }
        }
    };

    /** The frame at which tick number `k` ends: ticks follow the clock, not a rounded length, so no time is lost at 44.1 kHz. */
    uint64_t boundary(uint64_t k) const { return static_cast<uint64_t>(std::llround(static_cast<double>(k) * fs_ * kTickMs * 0.001)); }

    void endTick() {
        const uint32_t n = tickFill_;
        for (int w = 0; w < kRmsWindows; ++w) {
            for (int c = 0; c < channels_; ++c) windowAcc_[w][c] += tickSquares_[c];
            windowFrames_[w] += n;
            if (++windowTicks_[w] < ticksPerWindow_[w]) continue;
            RmsWindowStats& s = rms_[w];
            for (int c = 0; c < channels_; ++c) {
                const double ms = windowAcc_[w][c] / windowFrames_[w];
                if (s.count == 0) s.minMs[c] = s.maxMs[c] = ms;
                s.minMs[c] = std::min(s.minMs[c], ms);
                s.maxMs[c] = std::max(s.maxMs[c], ms);
                s.sumRms[c] += std::sqrt(ms);
                ++s.hist[c][histBin(ms)];
                windowAcc_[w][c] = 0.0;
            }
            ++s.count;
            windowTicks_[w] = 0;
            windowFrames_[w] = 0;
        }
        static const bool kWaveHigher[4] = {false, false, true, true};
        const float wave[4] = {tickMin_[0], tickMin_[1], tickMax_[0], tickMax_[1]};
        wave_.push(wave, kWaveHigher);
        if (loudness_.tick(tickEnergy_, n)) {
            static const bool kLoudHigher[2] = {true, true};
            const float loud[2] = {static_cast<float>(loudness_.momentary), static_cast<float>(loudness_.shortTerm)};
            loud_.push(loud, kLoudHigher);
        }
        tickEnergy_ = tickSquares_[0] = tickSquares_[1] = 0.0;
        tickFill_ = 0;
        tickEnd_ = boundary(++tick_ + 1);
        if (tickEnd_ <= frames_) tickEnd_ = frames_ + 1; // a sample rate under 200 Hz: never two ticks at one frame
    }

    double fs_ = 48000.0;
    int channels_ = 2;
    KWeight weight_[2];
    TruePeak truePeak_[2];
    MeanSpectrum spectrum_;
    Correlation correlation_;
    Loudness loudness_;
    Series<4> wave_;
    Series<2> loud_;
    ChannelStats ch_[2];
    RmsWindowStats rms_[kRmsWindows];
    Clip clip_[2];
    double windowAcc_[kRmsWindows][2] = {};
    uint32_t windowFrames_[kRmsWindows] = {};
    int windowTicks_[kRmsWindows] = {}, ticksPerWindow_[kRmsWindows] = {};
    uint64_t frames_ = 0, tick_ = 0, tickEnd_ = 0;
    uint32_t tickFill_ = 0;
    double sumLR_ = 0.0, tickEnergy_ = 0.0, tickSquares_[2] = {};
    float tickMin_[2] = {}, tickMax_[2] = {};
    bool flushed_ = false;
};

} // namespace analyzer
} // namespace hnl
