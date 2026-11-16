// What a fitted piano voice is made of: for every measured note, the noise of its attack and its
// partials. Each partial has a frequency early and late in the note (as multiples of the note's own
// frequency), a starting phase, and its level in tenths of a dB at the times of kTimeMs. The data
// files next to this one are written by tools/fit.cpp. No audio is stored in them.
#pragma once

#include <cstdint>

namespace pianodata {

constexpr int kTimes = 29;
constexpr double kSwitchSeconds = 0.35;
static const float kTimeMs[kTimes] = {0, 6, 12, 20, 30, 45, 60, 80, 100, 130, 160, 200, 250, 300, 400, 500, 650, 800, 1000, 1300, 1600, 2000, 2500, 3000, 4000, 5000, 6000, 8000, 10000};

struct Partial {
    float early, late, phase;
    int16_t level[kTimes];
};

constexpr int kKnockBands = 5, kKnockFrames = 4;
static const float kKnockHz[kKnockBands] = {50, 100, 200, 400, 800};
static const float kKnockMs[kKnockFrames + 1] = {0, 30, 60, 120, 240};

struct Note {
    int midi;
    float freq, seconds;
    int count;
    const Partial* partials;
    int16_t knock[kKnockBands][kKnockFrames]; // noise level per band and frame, tenths of a dB
};

} // namespace pianodata
