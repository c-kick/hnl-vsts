// Help texts, shown as tips when help is switched on in the UI.
#pragma once

#include "Params.hpp"

namespace help {

inline const char* forParam(uint32_t id) {
    switch (id) {
    case kSensitivity: return "How readily the ducker makes room. It moves the dashed line in the display: bands where the ducker rises above the line are turned down. Higher reacts to quieter sounds.";
    case kDepth: return "The deepest cut a band can get. A band reaches it when the ducker is far above the line; just above the line the cut is small. A broad ducker such as a voice comes close to this figure; a single note stays above it.";
    case kAttack: return "How fast a band is turned down when the ducker comes in. Short for drums, a little longer for voices so their first consonants do not punch holes.";
    case kRelease: return "How fast a band comes back when the ducker leaves. Short follows every syllable; long keeps the room open between words.";
    case kLow: return "Amount of ducking for the lows, around 80 Hz and below, as a share of Depth. At 0% the lows are left alone; 200% doubles the depth there.";
    case kLowMid: return "Amount of ducking around 400 Hz, where the body of most voices and instruments sits. Between the four ranges the amount glides from one to the next.";
    case kHighMid: return "Amount of ducking around 2 kHz, the range that decides how clearly a voice is heard.";
    case kHigh: return "Amount of ducking for the highs, around 10 kHz and above.";
    case kLink: return "At 0% every band listens to its own part of the ducker: room is made only where it is needed. At 100% all bands follow the ducker as a whole and the entire signal dips, as with a classic sidechain compressor.";
    case kSource: return "Sidechain: the signal at the sidechain input makes the room. Self: the signal ducks itself, which smooths its own loudest ranges and needs no sidechain.";
    case kListen: return "Output: the result. Sidechain: what arrives at the sidechain input, to check the cabling. Removed: only what is being taken out.";
    default: return "";
    }
}

const char* const kPresets = "Starting points. The arrows step through them. A * after the name means a control was changed since the preset was chosen. Save as... at the end of the list keeps the settings of this moment under a name of your own, for use in every song; the x behind such a preset deletes it.";
const char* const kDisplay = "Bars from below: the level of the ducker per band. Dashed line: where ducking starts (Sensitivity). Curve from above: what is being cut from this channel right now. Thin line: the deepest each range may go (Depth and the four range controls).";
const char* const kOutput = "Peak level leaving the plugin, in dBFS, and the deepest cut at this moment.";
const char* const kHelp = "Help is on: rest the mouse on a control to read what it does. Click here again to switch it off.";

} // namespace help
