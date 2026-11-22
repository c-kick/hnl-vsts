// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// HNL Patina: makes a clean sound old, and lets the wear follow the music.
//
//   in -+-> drive -> motion -> vinyl -> noise -> filter -> space -> mix -> output
//       |              ^         ^        ^        ^                 ^
//       |              +---------+- platter (angle, speed) -+        |
//       +-> listener -> React of every module                        |
//       +-> (untouched, delayed by latency()) -----------------------+
//
// Two scenes, A and B, hold the same controls; Morph goes from one to the other. Header-only.
#pragma once

#include <atomic>
#include <cstdint>

#include "Listener.hpp"
#include "Modules.hpp"

namespace hnl {

enum PatinaModuleId { kPatinaDrive, kPatinaMotion, kPatinaVinyl, kPatinaNoise, kPatinaFilter, kPatinaSpace, kPatinaModules };

/** One module in one scene. `third` is its own knob: tone, speed, wear, colour, resonance or decay. */
struct PatinaModule {
    bool on = true;
    int type = 0;
    double amount = 0.0; // percent
    double react = 0.0;  // percent
    double third = 0.0;  // Drive: -100..100 %. Motion: 0, 1, 2 (33, 45, 78). Vinyl, Noise, Filter: percent. Space: seconds.
};

struct PatinaScene {
    PatinaModule m[kPatinaModules];
    int noiseMode = 0; // what React does to the bed: 0 = it follows the music, 1 = it ducks under it
};

struct PatinaParams {
    double morph = 30.0;  // percent, from scene A to scene B
    double mix = 100.0;   // percent
    double output = 0.0;  // dB
    int mains = 0;        // 0 = 50 Hz, 1 = 60 Hz
    int idle = 0;         // in silence: 0 = the plugin's own sounds stop, 1 = they keep going
    PatinaScene scene[2];
};

/** What the window shows. Written once a block; hits and the output peaks hold their largest value until read. */
struct PatinaMeters {
    std::atomic<float> level{-12.0f}, hits{0.0f}, gaps{0.0f}, angle{0.0f}, cents{0.0f};
    std::atomic<float> react[kPatinaModules];
    std::atomic<float> outPeak[2];
    PatinaMeters() {
        for (auto& r : react) r.store(0.0f);
        for (auto& p : outPeak) p.store(0.0f);
    }
};

class Patina {
public:
    static constexpr double kOwnHoldSeconds = 4.0, kOwnFadeSeconds = 2.0;

    PatinaMeters meters;

    void prepare(double sampleRate) {
        fs_ = sampleRate;
        listener_.prepare(fs_);
        platter_.prepare(fs_);
        drive_.prepare(fs_);
        vinyl_.prepare(fs_);
        noise_.prepare(fs_);
        filter_.prepare(fs_);
        space_.prepare(fs_);
        for (int c = 0; c < 2; ++c) {
            motion_[c].setMax(platter_.longest());
            dry_[c].setLength(static_cast<size_t>(latency()));
        }
        for (auto& f : fade_) f.prepare(fs_);
        knob_ = onePoleCoef(20.0, fs_);
        rDrive_.set(10.0, 150.0, fs_);
        rMotion_.set(0.1, 400.0, fs_);
        rVinyl_.set(20.0, 400.0, fs_);
        rFollow_.set(50.0, 400.0, fs_); // gone a second and a half after the music
        rDuck_.set(20.0, 400.0, fs_);
        rFilter_.set(15.0, 300.0, fs_);
        rSpace_.set(20.0, 500.0, fs_);
        resolve();
        reset();
    }

    /** Samples by which the output is late: Drive's oversampler and the centre of Motion's delay. Never changes. */
    int latency() const { return patina::Drive::kLatency + platter_.centre(); }

    void setParams(const PatinaParams& p) {
        p_ = p;
        resolve();
    }

    void reset() {
        listener_.reset();
        platter_.reset();
        drive_.reset();
        vinyl_.reset();
        noise_.reset();
        filter_.reset();
        space_.reset();
        for (int c = 0; c < 2; ++c) { motion_[c].reset(); dry_[c].reset(); }
        rDrive_.reset(); rMotion_.reset(); rVinyl_.reset(); rFollow_.reset(); rDuck_.reset(); rFilter_.reset(); rSpace_.reset();
        own_ = 1.0;
        tick_ = 0;
        snap_ = true;
        for (int m = 0; m < kPatinaModules; ++m) {
            running_[m] = false;
            fade_[m].reset(key(m));
            set_[m] = -1e9;
        }
        setType_ = -1;
    }

    /** In-place stereo processing. */
    void process(float* left, float* right, int frames) {
        const ScopedNoDenormals noDenormals;
        float peak[2] = {0.0f, 0.0f}, hit = 0.0f;
        double bar[kPatinaModules] = {};
        for (int i = 0; i < frames; ++i) {
            double l = left[i], r = right[i];
            const double dryL = dry_[0].process(l), dryR = dry_[1].process(r);
            listener_.process(l, r);
            smooth();

            // Where each module stands on this sample: its type, and its Amount as it is heard.
            double amount[kPatinaModules];
            int type[kPatinaModules];
            for (int m = 0; m < kPatinaModules; ++m) {
                const bool run = target_[m].amount > 0.0 || now_[m].amount > 1e-4;
                if (run && !running_[m]) { clear(m); fade_[m].reset(key(m)); set_[m] = -1e9; }
                running_[m] = run;
                if (m != kPatinaMotion && run && fade_[m].process(key(m))) { clear(m); set_[m] = -1e9; }
                type[m] = m == kPatinaMotion ? target_[m].type : fade_[m].current % 8;
                amount[m] = run ? now_[m].amount * (m == kPatinaMotion ? 1.0 : fade_[m].gain) : 0.0;
            }
            const int noiseMode = fade_[kPatinaNoise].current / 8;

            // What the music does to each of them.
            const double push = now_[kPatinaDrive].react * rDrive_.process(listener_.above);
            const double sag = now_[kPatinaMotion].react * rMotion_.process(listener_.hits);
            const double hide = now_[kPatinaVinyl].react * rVinyl_.process(listener_.loud);
            const double follow = rFollow_.process(listener_.present ? 1.0 : 0.0), under = rDuck_.process(listener_.loud);
            const double bedDown = now_[kPatinaNoise].react * (noiseMode == 1 ? under : 1.0 - follow);
            const double open = now_[kPatinaFilter].react * rFilter_.process(std::clamp(listener_.level / 6.0, -0.5, 1.0));
            const double tailDown = now_[kPatinaSpace].react * rSpace_.process(listener_.loud);

            // The plugin's own sounds stop some seconds after the music does, unless they are told to keep going.
            const bool quiet = p_.idle == 0 && listener_.silentFor > kOwnHoldSeconds;
            own_ = quiet ? std::max(0.0, own_ - 1.0 / (kOwnFadeSeconds * fs_)) : std::min(1.0, own_ + 1.0 / (0.005 * fs_));

            const bool turning = running_[kPatinaMotion] || running_[kPatinaVinyl];
            platter_.process(turning, type[kPatinaMotion], speed_, amount[kPatinaMotion], running_[kPatinaMotion] ? sag : 0.0);
            const double sway = platter_.cents * (1.0 / 50.0); // the record's movement, for Noise and Filter

            if ((tick_++ & 15) == 0) control(type, open + 0.05 * sway);

            drive_.process(l, r, running_[kPatinaDrive], type[kPatinaDrive], dbToLin(36.0 * amount[kPatinaDrive] + 9.0 * push),
                           patina::smoothstep(amount[kPatinaDrive] / 0.12));
            l = motion_[0].process(l, platter_.delay[0]);
            r = motion_[1].process(r, platter_.delay[1]);
            if (running_[kPatinaVinyl])
                vinyl_.process(l, r, type[kPatinaVinyl], amount[kPatinaVinyl], now_[kPatinaVinyl].third, own_ * dbToLin(-18.0 * hide), platter_.angle);
            if (running_[kPatinaNoise]) {
                const double a = amount[kPatinaNoise];
                const double down = noiseMode == 1 ? dbToLin(-18.0 * bedDown) : 1.0 - bedDown;
                noise_.process(l, r, type[kPatinaNoise], 0.3 * a * a * own_ * down * (1.0 + 0.03 * sway), p_.mains);
            }
            if (running_[kPatinaFilter]) // a change of type fades the filter out and in as it stands, without sweeping it
                filter_.process(l, r, type[kPatinaFilter], patina::smoothstep(now_[kPatinaFilter].amount / 0.08) * fade_[kPatinaFilter].gain);
            if (running_[kPatinaSpace]) {
                double wl, wr;
                space_.process(type[kPatinaSpace], l, r, wl, wr);
                const double wet = amount[kPatinaSpace] * dbToLin(-12.0 * tailDown);
                l += wet * wl;
                r += wet * wr;
            }

            l = (dryL + mix_ * (l - dryL)) * gain_;
            r = (dryR + mix_ * (r - dryR)) * gain_;
            left[i] = static_cast<float>(l);
            right[i] = static_cast<float>(r);

            peak[0] = std::max(peak[0], std::fabs(left[i]));
            peak[1] = std::max(peak[1], std::fabs(right[i]));
            hit = std::max(hit, static_cast<float>(listener_.hits));
            bar[kPatinaDrive] = push; bar[kPatinaMotion] = sag; bar[kPatinaVinyl] = hide;
            bar[kPatinaNoise] = bedDown; bar[kPatinaFilter] = std::fabs(open); bar[kPatinaSpace] = tailDown;
        }
        if (frames > 0) {
            meters.level.store(static_cast<float>(listener_.level), std::memory_order_relaxed);
            meters.gaps.store(static_cast<float>(listener_.gaps), std::memory_order_relaxed);
            meters.angle.store(static_cast<float>(platter_.angle), std::memory_order_relaxed);
            meters.cents.store(static_cast<float>(platter_.cents), std::memory_order_relaxed);
            for (int m = 0; m < kPatinaModules; ++m)
                meters.react[m].store(running_[m] ? static_cast<float>(std::min(1.0, bar[m])) : 0.0f, std::memory_order_relaxed);
            largest(meters.hits, hit);
            largest(meters.outPeak[0], peak[0]);
            largest(meters.outPeak[1], peak[1]);
        }
    }

    // For the tests: the parts with no sound of their own, and what the engine made of the knobs.
    const patina::Listener& listener() const { return listener_; }
    const patina::Platter& platter() const { return platter_; }
    bool running(int module) const { return running_[module]; }
    double own() const { return own_; }

private:
    /** A module's knobs as they are heard at one moment: 0..1, except Drive's tone (-1..1) and Space's decay (seconds). */
    struct Knobs {
        double amount = 0.0, react = 0.0, third = 0.0;
        int type = 0;
    };

    static void largest(std::atomic<float>& held, float v) {
        if (v > held.load(std::memory_order_relaxed)) held.store(v, std::memory_order_relaxed);
    }

    /** What a module fades for: its type, and for Noise also what React does. */
    int key(int m) const { return target_[m].type + (m == kPatinaNoise ? 8 * noiseMode_ : 0); }

    /** Where Morph puts every knob. Called when a parameter changes. */
    void resolve() {
        const double t = std::clamp(p_.morph, 0.0, 100.0) * 0.01;
        // A type cannot be half of one and half of another: it changes over at 50%, and Morph has to
        // come back a little before it changes again.
        if (t <= 0.48) side_ = 0;
        else if (t >= 0.52) side_ = 1;
        for (int m = 0; m < kPatinaModules; ++m) {
            const PatinaModule& a = p_.scene[0].m[m];
            const PatinaModule& b = p_.scene[1].m[m];
            const double amountA = a.on ? a.amount : 0.0, amountB = b.on ? b.amount : 0.0;
            Knobs& k = target_[m];
            k.amount = std::clamp((amountA + t * (amountB - amountA)) * 0.01, 0.0, 1.0);
            // A module that is off in one scene takes everything but its Amount from the other.
            const int from = a.on && !b.on ? 0 : (b.on && !a.on ? 1 : side_);
            const double mixB = a.on && !b.on ? 0.0 : (b.on && !a.on ? 1.0 : t);
            const double scale = m == kPatinaSpace ? 1.0 : 0.01;
            k.react = std::clamp((a.react + mixB * (b.react - a.react)) * 0.01, 0.0, 1.0);
            k.third = (a.third + mixB * (b.third - a.third)) * scale;
            k.type = p_.scene[from].m[m].type;
            if (m == kPatinaNoise) noiseMode_ = p_.scene[from].noiseMode;
        }
        // The speed is the record's: it is taken from the scene Morph is nearer to, whether Motion is on there or not.
        speed_ = std::clamp(static_cast<int>(std::lround(p_.scene[side_].m[kPatinaMotion].third)), 0, 2);
        mixTarget_ = std::clamp(p_.mix, 0.0, 100.0) * 0.01;
        gainTarget_ = dbToLin(p_.output);
    }

    /** Knobs do not jump: they glide to where they were put, sample by sample. */
    inline void smooth() {
        if (snap_) {
            for (int m = 0; m < kPatinaModules; ++m) now_[m] = target_[m];
            mix_ = mixTarget_;
            gain_ = gainTarget_;
            snap_ = false;
            return;
        }
        for (int m = 0; m < kPatinaModules; ++m) {
            now_[m].amount = target_[m].amount + knob_ * (now_[m].amount - target_[m].amount);
            now_[m].react = target_[m].react + knob_ * (now_[m].react - target_[m].react);
            now_[m].third = target_[m].third + knob_ * (now_[m].third - target_[m].third);
        }
        mix_ = mixTarget_ + knob_ * (mix_ - mixTarget_);
        gain_ = gainTarget_ + knob_ * (gain_ - gainTarget_);
    }

    void clear(int m) {
        switch (m) {
        case kPatinaDrive: drive_.clear(); break;
        case kPatinaVinyl: vinyl_.clear(); break;
        case kPatinaNoise: noise_.clear(); break;
        case kPatinaFilter: filter_.reset(); break;
        case kPatinaSpace: space_.reset(); break;
        default: break;
        }
    }

    /** Sixteen times less often than the sound: the filters that follow a knob are set again where the knob moved. */
    void control(const int* type, double filterOctaves) {
        if (running_[kPatinaDrive] && moved(kPatinaDrive, now_[kPatinaDrive].third)) drive_.setTone(std::clamp(now_[kPatinaDrive].third, -1.0, 1.0));
        if (running_[kPatinaVinyl] && moved(kPatinaVinyl, now_[kPatinaVinyl].third)) vinyl_.setWear(std::clamp(now_[kPatinaVinyl].third, 0.0, 1.0));
        if (running_[kPatinaNoise] && moved(kPatinaNoise, now_[kPatinaNoise].third)) noise_.setColour(std::clamp(now_[kPatinaNoise].third, 0.0, 1.0));
        if (running_[kPatinaFilter]) // moves with the music, so it is set every time
            filter_.set(type[kPatinaFilter], now_[kPatinaFilter].amount, filterOctaves, std::clamp(now_[kPatinaFilter].third, 0.0, 1.0));
        if (running_[kPatinaSpace] && (moved(kPatinaSpace, now_[kPatinaSpace].third) || setType_ != type[kPatinaSpace])) {
            setType_ = type[kPatinaSpace];
            space_.setDecay(setType_, std::clamp(now_[kPatinaSpace].third, 0.2, 4.0));
        }
    }
    bool moved(int m, double v) {
        if (std::fabs(v - set_[m]) < 1e-3) return false;
        set_[m] = v;
        return true;
    }

    double fs_ = 48000.0;
    PatinaParams p_;
    patina::Listener listener_;
    patina::Platter platter_;
    patina::Drive drive_;
    patina::PlatterDelay motion_[2];
    patina::Vinyl vinyl_;
    patina::Noise noise_;
    patina::Filter filter_;
    patina::Space space_;
    Delay dry_[2];
    patina::TypeFade fade_[kPatinaModules];
    patina::Follower rDrive_, rMotion_, rVinyl_, rFollow_, rDuck_, rFilter_, rSpace_;

    Knobs target_[kPatinaModules], now_[kPatinaModules];
    bool running_[kPatinaModules] = {};
    double set_[kPatinaModules] = {-1e9, -1e9, -1e9, -1e9, -1e9, -1e9};
    int side_ = 0, noiseMode_ = 0, speed_ = 0, setType_ = -1;
    double knob_ = 0.0, mix_ = 1.0, mixTarget_ = 1.0, gain_ = 1.0, gainTarget_ = 1.0, own_ = 1.0;
    uint32_t tick_ = 0;
    bool snap_ = true;
};

} // namespace hnl
