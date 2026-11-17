// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// Help texts, shown as tips when help is switched on in the UI.
#pragma once

#include "Params.hpp"

namespace help {

inline const char* forParam(uint32_t id) {
    switch (id) {
    case kRecord: return "How hard the tape is driven. Higher gives more saturation and tape compression. Aim for the VU needles to move around 0; into the red is where the colour gets strong.";
    case kReproduce: return "Playback level, after the tape. It changes the volume, not the sound.";
    case kLink: return "Takes the record level off again at the output, so turning Record changes the colour and not the volume.";
    case kSpeed: return "30 ips is the cleanest and most extended. 15 ips has more character and a head bump lower in the bass. 7.5 ips is the most coloured: the top rolls off early and saturates easily.";
    case kFormula: return "The tape stock. Vintage saturates early and softly. Classic and Modern take more level. High Output stays clean the longest, then clips firmly.";
    case kHead: return "Width of the tape and its heads. Narrow tracks have a bigger bump in the bass and more hiss; wide tracks are flatter and quieter.";
    case kPath: return "Repro: record, then play back from tape, the full sound. Sync: play back through the record head, duller with a bigger bass bump. Input: the electronics only, no tape.";
    case kBias: return "The setting of the record bias. Towards under: brighter, and quiet signals get rougher. Towards over: a duller top and cleaner lows.";
    case kWow: return "Slow pitch drift from the transport. 10% is a well-kept machine; high settings sound like a tired one.";
    case kFlutter: return "Fast pitch shimmer from the transport. It follows the tape speed.";
    case kNoise: return "Tape hiss. Off at 0, the level of real tape at 50%, exaggerated above. It depends on speed, formula and head width.";
    case kCalibration: return "The level in your mix that the machine treats as 0 VU. If the needles sit pinned in the red with Record at 0, raise this; if they barely move, lower it. Presets leave it alone.";
    default: return "";
    }
}

const char* const kPresets = "Starting points. The arrows step through them. A * after the name means a control was changed since the preset was chosen. Save as... at the end of the list keeps the settings of this moment under a name of your own, for use in every song; the x behind such a preset deletes it.";
const char* const kVu = "Level on the tape. 0 VU is the level the machine is lined up for (see Calibration). Around 0 is gentle colour; resting in the red is heavy saturation.";
const char* const kOutput = "Peak level leaving the plugin, in dBFS. The tape rounds peaks off but does not limit: keep an eye on this, or follow with a limiter.";
const char* const kHelp = "Help is on: rest the mouse on a control to read what it does. Click here again to switch it off.";

} // namespace help
