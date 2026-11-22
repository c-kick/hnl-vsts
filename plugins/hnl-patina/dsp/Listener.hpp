// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// The listener: what HNL Patina hears in the music, for the React knob of every module.
// It has no sound of its own, so a test can feed it a signal and read what it makes of it.
#pragma once

#include <algorithm>
#include <cmath>

#include "../../../common/dsp/Filters.hpp"

namespace hnl {
namespace patina {

/**
 * One detector for the whole plugin. It listens to the louder of the two channels.
 *
 *   level     how loud this moment is against the last few seconds, in dB, held within -12..+12
 *   loud      the same as a number: 0 at 12 dB below the recent average, 1 at the average or above
 *   above     0 at the average, 1 at 6 dB above it
 *   hits      0..1: the rise of a fast envelope above a slower one (the method of HNL Transient Shaper)
 *   gaps      0..1: how far the level has dropped below the recent average
 *   present   there is music: the input is above the floor, or was a moment ago
 *   silentFor seconds since the input was last above the floor
 */
class Listener {
public:
    static constexpr double kFloorDb = -70.0;   // below this there is no music
    static constexpr double kAverageFloorDb = -50.0; // the recent average never counts as lower than this
    static constexpr double kSpanDb = 12.0;

    double level = -kSpanDb, loud = 0.0, above = 0.0, hits = 0.0, gaps = 1.0, silentFor = 1e9;
    bool present = false;

    void prepare(double fs) {
        fs_ = fs;
        fastUp_ = onePoleCoef(1.0, fs);
        fastDown_ = onePoleCoef(60.0, fs);
        slow_ = onePoleCoef(3000.0, fs);
        hitUpA_ = onePoleCoef(0.5, fs);
        hitUpB_ = onePoleCoef(25.0, fs);
        hitDown_ = onePoleCoef(50.0, fs);
        floor_ = dbToLin(kFloorDb);
        averageFloor_ = dbToLin(kAverageFloorDb);
        hitFloor_ = dbToLin(-60.0);
        reset();
    }
    void reset() {
        fast_ = average_ = a_ = b_ = 0.0;
        silent_ = 1e12;
        level = -kSpanDb; loud = 0.0; above = 0.0; hits = 0.0; gaps = 1.0; silentFor = 1e9;
        present = false;
    }
    inline void process(double l, double r) {
        const double x = std::max(std::fabs(l), std::fabs(r));
        fast_ = x + (x > fast_ ? fastUp_ : fastDown_) * (fast_ - x);
        average_ = fast_ + slow_ * (average_ - fast_);
        level = std::clamp(linToDb(fast_ / std::max(average_, averageFloor_)), -kSpanDb, kSpanDb);
        loud = std::clamp(1.0 + level / kSpanDb, 0.0, 1.0);
        above = std::clamp(level / 6.0, 0.0, 1.0);
        gaps = std::clamp(-level / kSpanDb, 0.0, 1.0);

        a_ = x + (x > a_ ? hitUpA_ : hitDown_) * (a_ - x);
        b_ = x + (x > b_ ? hitUpB_ : hitDown_) * (b_ - x);
        hits = std::clamp(linToDb((a_ + hitFloor_) / (b_ + hitFloor_)) / kSpanDb, 0.0, 1.0);

        if (fast_ > floor_) silent_ = 0.0;
        else silent_ += 1.0;
        silentFor = silent_ / fs_;
        present = silentFor < 0.05;
    }

private:
    double fs_ = 48000.0, fastUp_ = 0, fastDown_ = 0, slow_ = 0, hitUpA_ = 0, hitUpB_ = 0, hitDown_ = 0;
    double floor_ = 0, averageFloor_ = 0, hitFloor_ = 0;
    double fast_ = 0, average_ = 0, a_ = 0, b_ = 0, silent_ = 1e12;
};

/** A value that follows another, with one time to rise and another to fall. */
struct Follower {
    double up = 0, down = 0, y = 0;
    void set(double upMs, double downMs, double fs) { up = onePoleCoef(upMs, fs); down = onePoleCoef(downMs, fs); }
    void reset(double v = 0.0) { y = v; }
    inline double process(double x) { return y = x + (x > y ? up : down) * (y - x); }
};

} // namespace patina
} // namespace hnl
