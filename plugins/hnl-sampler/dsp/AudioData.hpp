// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// A sound in memory, with what its file said about itself: where it loops,
// which key it was recorded at, where its markers are. Filled by the readers
// in AudioFile.hpp and Import.hpp; played by the engine. Header-only, no
// dependencies.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace hnl {
namespace sampler {

struct AudioData {
    /** 1 or 2. A file with more channels keeps its first two. */
    int channels = 0;
    double sampleRate = 0.0;
    /** One vector per channel, all of the same length; full scale is -1..1. */
    std::vector<float> samples[2];

    size_t frames() const { return samples[0].size(); }

    // What the file says about itself. A key or velocity of -1, a tempo of 0
    // and an empty list mean that the file does not say.
    int rootKey = -1;       // the MIDI note the sound was recorded at
    double tuneCents = 0.0; // how far above that note it sounds
    int loKey = -1, hiKey = -1, loVel = -1, hiVel = -1;
    double gainDb = 0.0;

    enum LoopType { kForward = 0, kPingPong = 1, kBackward = 2 };
    struct Loop {
        size_t start = 0; // first frame of the loop
        size_t end = 0;   // one past its last frame
        int type = kForward;
    };
    std::vector<Loop> loops;
    std::vector<size_t> cues; // markers or slice points, in frames, ascending
    double tempo = 0.0;       // beats per minute

    // For display.
    int bits = 0;         // per sample, as stored in the file
    bool isFloat = false; // stored as floating point
    std::string format;   // "WAV", "AIFF", "FLAC", "SF2"
};

} // namespace sampler
} // namespace hnl
