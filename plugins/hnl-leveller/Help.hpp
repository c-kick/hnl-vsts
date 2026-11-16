// Help texts, shown as tips when help is switched on in the UI.
#pragma once

#include "Params.hpp"

namespace help {

inline const char* forParam(uint32_t id) {
    switch (id) {
    case kType: return "Opto is slow and smooth: it rides the level without being heard, and lets go in two stages. FET is fast and firm: it catches every peak and brings a sound forward. Attack, Release and Colour mean different things for each.";
    case kThreshold: return "The level above which the signal is turned down. Lower it until the needle shows the reduction you want: 3 to 6 dB evens out a vocal, 10 or more is a sound of its own. Presets set it for a track peaking around -6 dBFS.";
    case kRatio: return "How firmly the level is held above the threshold. 3:1 and 4:1 for levelling, 8:1 for a tight grip, 20:1 is limiting. Opto bends in gradually; FET has a tighter knee.";
    case kAttack: return "How fast it reacts; the time is shown under the knob. Opto: 2.5 to 40 ms. FET: 0.02 to 0.8 ms, where the slowest setting still lets the click of a note through.";
    case kRelease: return "How fast it lets go; the time is shown under the knob. FET: one time, 50 ms to 1.1 s. Opto: about half goes in the time shown, the rest fades over a few seconds after a long loud passage.";
    case kMakeup: return "Gain after the compression, to bring back the level it took away. It only affects the compressed signal.";
    case kColour: return "Adds harmonics that grow with the level: warm (second harmonic) for Opto, with more edge (third harmonic) for FET. At 0 the compressor is clean.";
    case kScHp: return "Keeps the low end out of the level detector, so plosives or the lowest bass notes do not pull the level down. Off at the lowest setting. It does not filter the sound itself.";
    case kMix: return "Blend between the untouched signal (0%) and the compressed one (100%). Lower settings give parallel compression.";
    default: return "";
    }
}

const char* const kPresets = "Starting points. The arrows step through them. A * after the name means a control was changed since the preset was chosen.";
const char* const kGr = "Gain reduction: how many dB the signal is being turned down right now. Both channels always get the same reduction.";
const char* const kLevels = "Peak levels going in and coming out, in dBFS. The compressor does not limit: with make-up gain the output can go over 0.";
const char* const kHelp = "Help is on: rest the mouse on a control to read what it does. Click here again to switch it off.";

} // namespace help
