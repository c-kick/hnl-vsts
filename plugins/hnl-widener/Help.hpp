// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// Help texts, shown as tips when help is switched on in the UI.
#pragma once

#include "Params.hpp"

namespace help {

inline const char* forParam(uint32_t id) {
    switch (id) {
    case kAmount: return "How much of Haas, Diffuse and Detune together is added. One knob for all three: turn it down to compare, or automate it. At 0% only Stereo Width and Bass are left at work.";
    case kHaas: return "Adds the sound to the sides a few milliseconds late. That is too short to hear as an echo, and the ear takes it for width. In each channel it makes rises and dips that the other channel has the other way round.";
    case kHaasTime: return "How late. Short times (under 5 ms) give few, broad rises and dips and can lean the low notes to one side: raise Widen Above. Long times (over 25 ms) begin to sound like a slap.";
    case kHaasMode: return "Mono Safe: the late sound goes to the sides only, added to the left and taken off the right, so that a mono speaker plays exactly what went in. Left Late and Right Late are the Haas effect as it is usually made: that whole channel is late. It is wider still and leans to the other side, but in mono it leaves holes: the green line in the plot shows them.";
    case kDiffuse: return "Sends every narrow band of pitch to a place of its own between left and right, with no pattern to it. This turns a mono sound into a wide one without an echo or a wobble. At 100% left and right have as little in common as two different recordings.";
    case kSize: return "How far the bands of Diffuse are smeared in time. Small is tight and a little hollow, large is smooth and begins to sound like a small room: on drums it rings on after each hit.";
    case kDetune: return "Adds a copy tuned a little higher, less one tuned a little lower, to the sides: the trick of doubling a voice or a guitar. It moves slowly, like a soft chorus that only exists at the sides.";
    case kCents: return "How far the two copies of Detune are tuned up and down, in hundredths of a semitone. Under 10 is thick without sounding out of tune; from 20 up it wobbles.";
    case kWidth: return "The width the sound already had: 100% leaves it, 0% makes it mono, 200% doubles what differs between left and right. Widening only works between Widen Above and Widen Below. It does nothing to a mono sound: that takes Haas, Diffuse or Detune.";
    case kLow: return "Nothing is widened below this pitch, so the bass and the kick stay in the middle, where they are strongest.";
    case kHigh: return "Nothing is widened above this pitch. All the way up is off.";
    case kBass: return "Mono: takes out what differs between left and right below Widen Above, so that the lows are the same on both speakers. As Is leaves the lows as they came in.";
    case kFocus: return "Holds back what the plugin adds for the first moment of each hit. The attack of a drum or a pluck then stays in the middle, sharp, and the sound opens up behind it.";
    case kDrift: return "Shifts everything the plugin adds upward by a fraction of a hertz. Each pitch then keeps wandering between left and right, and the picture never stands still. Slow (under 0.3 Hz) is alive without drawing attention; fast is a swirl. In mono none of it can be heard. All the way down is off.";
    case kListen: return "Mono: hear what a mono speaker would play. Sides: hear only what differs between left and right, which is everything this plugin adds.";
    case kMatch: return "Takes the loudness the width added back out, so that switching the plugin off and on compares the sound and not the level. Meant for comparing: it follows the level of the last moment, and it turns the mono sum down by the same amount.";
    case kOutput: return "Level of everything coming out. Width adds level on each side: turn this down if the output meters go into the red.";
    default: return "";
    }
}

const char* const kPresets = "Starting points. The arrows step through them. A * after the name means a control was changed since the preset was chosen. Save as... at the end of the list keeps the settings of this moment under a name of your own, for use in every song; the x behind such a preset deletes it.";
const char* const kResponse = "What the plugin does to a sound in the middle, at each pitch: how loud it comes out on the left, on the right, and in the two added up (mono). Where left and right lie apart, that pitch is pulled to one side: that is the width. The green line stays flat as long as a mono speaker plays exactly what went in. Detune and Drift are left out: they move.";
const char* const kImage = "The fan shows how wide the sound is going in (outline) and coming out (filled): a line straight up is mono, the lines marked L and R are a sound on one speaker only. Past those, in the red, more differs between left and right than they share. It leans to the louder side. The bars are the peak levels in dBFS; nothing limits the output. L/R shows how alike the two outputs are: the same at the right end (mono), different towards the middle (wide), cancelling each other in mono left of it. MATCH is the gain Level Match applies.";
const char* const kHelp = "Help is on: rest the mouse on a control to read what it does. Click here again to switch it off.";

} // namespace help
