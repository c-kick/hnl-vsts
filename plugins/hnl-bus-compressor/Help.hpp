// Help texts, shown as tips when help is switched on in the UI.
#pragma once

#include "Params.hpp"

namespace help {

inline const char* forParam(uint32_t id) {
    switch (id) {
    case kThreshold: return "The level above which the compressor turns the signal down. Lower it until the needle shows the reduction you want: 2 to 4 dB glues a mix, more is an effect. Presets set it for a mix peaking near 0 dBFS, so adjust it to your level.";
    case kRatio: return "How firmly it holds the level above the threshold. 1.5:1 and 2:1 are gentle and bend in gradually; 4:1 is the usual bus setting; 10:1 is close to limiting.";
    case kAttack: return "How fast it reacts, in milliseconds. Slow (10 or 30) lets the hit of the drums through before it grabs, which keeps the punch. Fast (0.1 to 1) catches the hits and flattens them.";
    case kRelease: return "How fast it lets go, in seconds. Short makes the level breathe with the beat; long is smoother. Auto lets go quickly after a single peak and slowly after a long loud passage.";
    case kMakeup: return "Gain after the compression, to bring back the level it took away. It only affects the compressed signal.";
    case kScHp: return "Keeps the bass out of the level detector, so the kick and bass do not make the whole mix duck. Off at the lowest setting. It does not filter the sound itself.";
    case kMix: return "Blend between the untouched signal (0%) and the compressed one (100%). Lower settings give parallel compression: heavy compression underneath, the original peaks on top.";
    default: return "";
    }
}

const char* const kPresets = "Starting points. The arrows step through them. A * after the name means a control was changed since the preset was chosen. Save as... at the end of the list keeps the settings of this moment under a name of your own, for use in every song; the x behind such a preset deletes it.";
const char* const kGr = "Gain reduction: how many dB the compressor is turning the signal down right now. Both channels always get the same reduction.";
const char* const kLevels = "Peak levels going in and coming out, in dBFS. The compressor does not limit: with make-up gain the output can go over 0.";
const char* const kHelp = "Help is on: rest the mouse on a control to read what it does. Click here again to switch it off.";

} // namespace help
