// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// Help texts, shown as tips when help is switched on in the UI.
#pragma once

#include "Params.hpp"

namespace help {

inline const char* forParam(uint32_t id) {
    switch (id) {
    case kAmount: return "How loud the added copy is. The signal itself always passes untouched; the copy comes on top, so more Amount is also louder. At 100% the copy is as loud as the signal.";
    case kDelay: return "How late one channel of the copy is. Too short to hear as an echo: it sets where the dips and rises in that channel lie. 3 ms puts the first dip at 167 Hz; shorter moves them up, longer moves them down and closer together.";
    case kDelayMix: return "How much of the delayed channel is late. The rest stays on time. Around 50% the dips are deepest inside the copy; at 100% the whole channel is late, which sounds widest.";
    case kSide: return "Which channel of the copy is delayed. Left or Right: the other one is only turned up, and the beat leans a little towards it. Both: the left is late by 62% of the delay and the right by all of it, so each has its dips where the other has none and the beat stays in the middle.";
    case kGuard: return "Below this pitch nothing in the copy is late, so the kick and the sub stay in the middle and solid. All the way down is off.";
    case kLowCut: return "Takes the lows out of the copy, so that only what lies above is thickened. All the way down is off.";
    case kHighCut: return "Takes the highs out of the copy, so that hats and cymbals are left alone. All the way up is off.";
    case kClean: return "Turns the copy down for the first moment of each hit, by up to 18 dB. The attack stays as clean as it went in; only the body after it is thickened.";
    case kCrush: return "Compresses the copy hard, as on a parallel drum bus: the quiet parts of it come up, the room with them. It reacts within a fraction of a millisecond. The signal itself is not compressed, so the hits keep their own attack.";
    case kDrive: return "Saturates the copy: grit and density. It is not oversampled, so very bright material can get harsh at high settings; High Cut takes care of that.";
    case kMatch: return "Takes the loudness the copy added back out, so that switching the plugin off and on compares the sound and not the level. Meant for comparing: it follows the level of the last moment.";
    case kListen: return "Copy: hear only what the plugin adds, without the signal itself.";
    case kRoom: return "Switches the small room in the copy on or off. Off leaves the delay alone at work.";
    case kRoomMix: return "How much of the copy is room sound. A little (around 10%) adds a short tail under the hits; at 100% the copy is only the room.";
    case kSize: return "Size of the room: how far apart its reflections are. Small sounds like a box close around the drums, large like a studio booth.";
    case kDecay: return "How long the room rings: the time for its tail to fall by 60 dB.";
    case kDamp: return "How fast the highs of the room die away. High values give a dark, dull room that stays out of the way of hats and cymbals.";
    case kOutput: return "Level of everything coming out. The copy adds level: turn this down if the output meter goes over 0.";
    default: return "";
    }
}

const char* const kPresets = "Starting points. The arrows step through them. A * after the name means a control was changed since the preset was chosen. Save as... at the end of the list keeps the settings of this moment under a name of your own, for use in every song; the x behind such a preset deletes it.";
const char* const kResponse = "What the plugin does to each pitch in the left and the right channel. The room, Clean Hit, Crush and Drive are left out: they depend on the sound. Where the two lines lie apart, a sound in the middle is pulled to one side: that difference is the thickening.";
const char* const kLevels = "Peak levels going in and coming out, in dBFS. Nothing limits the output: it can go over 0. L/R shows how alike the two outputs are: at the right end they are the same (mono), towards the middle they differ more (wide); left of the middle they cancel each other in mono. Under it: the gain of Level Match and how far Crush turns the copy down.";
const char* const kHelp = "Help is on: rest the mouse on a control to read what it does. Click here again to switch it off.";

} // namespace help
