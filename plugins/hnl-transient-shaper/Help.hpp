// Help texts, shown as tips when help is switched on in the UI.
#pragma once

#include "Params.hpp"

namespace help {

inline const char* forParam(uint32_t id) {
    switch (id) {
    case kAttack: return "Turns the hit of each sound up (to the right) or down (to the left), by up to 12 dB. Up gives punch and snap; down softens a sound and pushes it back in the mix. It works the same on quiet and loud hits.";
    case kSustain: return "Turns up or down what follows the hit: the ring of a drum, the room, the tail. By up to 18 dB. Down makes drums tight and dry; up makes them longer and fatter.";
    case kLength: return "How long the start of a sound counts as the hit. Short (5 to 10 ms) shapes only the click; long (30 ms and more) includes the body of the drum.";
    case kFocus: return "What the detector listens to. Full: every hit. Low: only kick and bass, below 150 Hz. Mid: snare and toms. High: hats and clicks, above 2.5 kHz. The whole signal is shaped either way.";
    case kOutput: return "Level of the shaped signal. More attack means higher peaks: turn this down if the output meter goes over 0.";
    case kMix: return "Blend between the untouched signal (0%) and the shaped one (100%).";
    default: return "";
    }
}

const char* const kPresets = "Starting points. The arrows step through them. A * after the name means a control was changed since the preset was chosen.";
const char* const kGain = "What the shaper is doing right now: the needle swings right when it turns the signal up and left when it turns it down. With Attack up it kicks right on every hit.";
const char* const kLevels = "Peak levels going in and coming out, in dBFS, and the largest boost and cut of the last moment. Nothing limits the output: it can go over 0.";
const char* const kHelp = "Help is on: rest the mouse on a control to read what it does. Click here again to switch it off.";

} // namespace help
