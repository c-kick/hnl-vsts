// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// Help texts, shown as tips when help is switched on in the UI.
#pragma once

#include "Params.hpp"

namespace help {

inline const char* forParam(uint32_t id) {
    switch (id) {
    case kInputGain: return "Level going into the chain. Use it to bring a quiet mix up to where the compressor thresholds sit, or a hot one down.";

    case kCompOn: return "Switches the three-band compressor on or off. It evens out the lows, mids and highs separately, so a loud kick does not pull the vocals down.";
    case kXoverLow: return "Where the low band ends and the mid band begins.";
    case kXoverHigh: return "Where the mid band ends and the high band begins. It stays at least an octave above the low crossover.";
    case kLowThreshold: case kMidThreshold: case kHighThreshold:
        return "Level above which this band is turned down. Lower means more of the signal is compressed. Watch the GR meter: 1 to 3 dB is typical for mastering.";
    case kLowRatio: case kMidRatio: case kHighRatio:
        return "How firmly the band is held once it passes the threshold. 2:1 means 2 dB over comes out as 1 dB over. 1.5 to 3 is gentle glue; above 4 is obvious control.";
    case kLowAttack: case kMidAttack: case kHighAttack:
        return "How fast the compressor reacts. Slower lets the start of each hit through, which keeps punch; faster catches peaks but can dull them.";
    case kLowRelease: case kMidRelease: case kHighRelease:
        return "How fast the band comes back up after a loud moment. Too fast pumps and distorts the lows; too slow keeps quiet passages held down.";
    case kLowMakeup: case kMidMakeup: case kHighMakeup:
        return "Level of this band after compression. Makes up for the gain reduction, or rebalances lows, mids and highs.";

    case kEqOn: return "Switches the equaliser on or off. It sits after the compressor.";
    case kEqLowFreq: return "Everything below this frequency is raised or lowered by Low Gain.";
    case kEqLowGain: return "Boost or cut of the lows. A little goes far on a master: half a dB to 2 dB.";
    case kEqMidFreq: return "Centre of the peak band.";
    case kEqMidGain: return "Boost or cut around the peak frequency.";
    case kEqMidQ: return "Width of the peak band. Low values are broad and gentle; high values are narrow, for fixing one spot.";
    case kEqHighFreq: return "Everything above this frequency is raised or lowered by High Gain.";
    case kEqHighGain: return "Boost or cut of the highs. Above 10 kHz adds air; lower adds presence.";

    case kExcOn: return "Switches the exciter on or off. It adds new harmonics to the top of the mix, which brightens in a way an equaliser cannot.";
    case kExcMode: return "Character of the added harmonics. Tape: smooth and rounded. Tube: classic, steered by Colour. Retro: bright and edgy, with a strong second harmonic.";
    case kExcFreq: return "Only the part of the mix above this frequency is excited. Higher is airier and more subtle.";
    case kExcDrive: return "How hard that part is pushed into saturation. More drive gives denser harmonics.";
    case kExcColour: return "Balance of the harmonics. Low: odd harmonics, crisp. High: even harmonics, warm.";
    case kExcMix: return "How much of the excited signal is added. Start low; it is easy to overdo, and tiring to listen to when you do.";

    case kWideOn: return "Switches the stereo widener on or off.";
    case kWideWidth: return "100% leaves the stereo image alone. Above widens, below narrows, 0% is mono. A mono fold-down of the result sounds the same at any setting.";
    case kWideFreq: return "Widening only applies above this frequency, so the bass stays centred and solid.";

    case kRevOn: return "Switches the reverb on or off. A touch can glue a dry mix together.";
    case kRevDecay: return "How long the reverb takes to die away.";
    case kRevDamping: return "Above this frequency the reverb fades faster. Lower sounds darker and further away.";
    case kRevMix: return "How much reverb is added to the mix. For mastering, 5 to 10% is already clearly there.";

    case kLimOn: return "Switches the limiter on or off. It is the last stage and stops the output going over the ceiling.";
    case kLimGain: return "Pushes the mix into the limiter: this is the loudness control. Watch the GR meter; beyond 3 to 4 dB the mix starts to lose punch.";
    case kLimCeiling: return "The highest level the output may reach, measured between the samples as well (true peak). -1 dB is a safe choice for streaming.";
    case kLimRelease: return "How fast the limiter lets go. Faster is louder but can distort; slower is cleaner but can pump.";
    default: return "";
    }
}

const char* const kPresets = "Starting points. The arrows step through them. A * after the name means a control was changed since the preset was chosen. Save as... at the end of the list keeps the settings of this moment under a name of your own, for use in every song; the x behind such a preset deletes it.";
const char* const kInputMeter = "Peak level going into the chain, after Input Gain.";
const char* const kOutputMeter = "Peak level leaving the plugin.";
const char* const kGainReduction = "Gain reduction: how much this stage is turning the signal down right now.";
const char* const kLoudness = "Loudness of the output in LUFS. Momentary is the last 0.4 s, short-term the last 3 s, integrated the whole stretch since the last reset. Streaming services aim around -14 LUFS integrated.";
const char* const kTruePeak = "The highest true peak since the last reset. It turns red when it passes the limiter's ceiling.";
const char* const kReset = "Starts the integrated loudness and the true-peak maximum again. Do this before playing the song through for a reading.";
const char* const kHelp = "Help is on: rest the mouse on a control to read what it does. Click here again to switch it off.";

} // namespace help
