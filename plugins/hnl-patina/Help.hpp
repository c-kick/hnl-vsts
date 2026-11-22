// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// Help texts, shown as tips when help is switched on in the UI.
#pragma once

#include "Params.hpp"

namespace help {

/** The controls that belong to the whole plugin. */
inline const char* forParam(uint32_t id) {
    switch (id) {
    case kMorph: return "Goes from scene A (0%) to scene B (100%). Every knob moves from its value in A to its value in B; a module that is off in one scene fades in. Where the two scenes use a different type, the module changes over at 50%. Made for automation.";
    case kMix: return "How much of the result is heard next to the untouched signal. With Motion on, anything below 100% puts the sound next to a copy of itself that moves in pitch: a slow chorus.";
    case kOutput: return "Level after everything else. It is not part of a preset.";
    case kMains: return "The mains frequency Hum and Buzz sound at: 50 Hz in Europe and most of the world, 60 Hz in the Americas. It stays with the song and is not part of a preset.";
    case kIdle: return "What the plugin's own sounds (crackle, rumble, the noise bed) do when the music stops. Stop: they go on for 4 seconds, long enough for a gap between phrases, and then fade out. Keep: they go on, for crackle before the first note and after the last.";
    default: return "";
    }
}

/** A control of a scene: the same text for A and for B. */
inline const char* forScene(uint32_t s) {
    switch (s) {
    case sDriveOn: return "Drive: a gain stage that the sound is pushed into. What comes out is as loud as what went in, so Amount changes the sound and not the level.";
    case sDriveType: return "Tube: warm, with even harmonics. Transistor: soft, lopsided. Diode: clean until a sharp knee. Fuzz: a lot of gain into a hard clip, at any Amount.";
    case sDriveAmount: return "How hard the sound is pushed into the stage: up to 36 dB of gain. The level stays the same.";
    case sDriveReact: return "Loud playing is pushed harder: up to 9 dB more drive for a note 6 dB above what came before. Quiet playing stays cleaner.";
    case sDriveTone: return "Tilts the sound before the stage and takes it back after: to the left the lows are driven harder, to the right the highs.";
    case sMotionOn: return "Motion: the record does not run true, so the pitch moves. Off: the pitch stands still; the record still turns for Vinyl.";
    case sMotionType: return "Off-centre: one slow swing a turn. Warped: two uneven bumps a turn, a little different left and right. Slipping Belt: a slow, small, random drift. Tired Motor: a little fast most of the turn, then a sag.";
    case sMotionAmount: return "How far the pitch moves: up to 50 cent either way at 100%. Slipping Belt moves far less, about 7 cent, but slowly and without a pattern.";
    case sMotionReact: return "Hits drag the platter: the pitch sags by up to 25 cent on a hit and recovers in under half a second.";
    case sMotionSpeed: return "The speed of the record: 33 1/3, 45 or 78 turns a minute. It sets how fast the pitch moves and how often a scratch comes round. It works for Vinyl too when Motion is off.";
    case sVinylOn: return "Vinyl: the surface of the record. Crackle, scratches, wear.";
    case sVinylType: return "Dusty: dense fine crackle. Scratched: clicks that come back at the same place every turn, more of them with Amount. Worn Groove: bright, loud passages splash. Shellac: a 78, with no lows, no highs and a heavy surface.";
    case sVinylAmount: return "How much surface noise there is, and how much of the wear is heard in the sound itself.";
    case sVinylReact: return "Crackle hides under loud notes, by up to 18 dB, and returns in the gaps.";
    case sVinylWear: return "How worn the record is: the highs go, and a rumble comes.";
    case sNoiseOn: return "Noise: a bed under the music. No tape hiss; the tape plugins have that.";
    case sNoiseType: return "Hum: mains hum with its harmonics. Buzz: the sharper buzz of a dimmer or a bad earth. Static: bursts of crackling. Room Tone: soft air that breathes.";
    case sNoiseAmount: return "Level of the bed.";
    case sNoiseReact: return "How strongly the bed answers the music, in the way the switch below says.";
    case sNoiseColour: return "Dark to bright: more harmonics in the hum, a narrower buzz, brighter static and air.";
    case sNoiseMode: return "What React does. Follow: the bed is there while the music plays and fades when it stops, so a part that ends does not leave a hum behind. Duck: the bed hides under the music and is heard in the gaps.";
    case sFilterOn: return "Filter: the tone of the playback chain.";
    case sFilterType: return "Tilt: the whole spectrum darker. Band: narrower from both ends, as a small device sounds. Ladder: a resonant low-pass of 24 dB per octave.";
    case sFilterAmount: return "How far the filter closes.";
    case sFilterReact: return "Loud notes open the filter, by up to two octaves for a note 6 dB above what came before; tails get darker.";
    case sFilterRes: return "A peak at the corner of the filter. In Ladder it whistles when turned far up.";
    case sSpaceOn: return "Space: a room around the worn sound. Its lows are kept in the middle.";
    case sSpaceType: return "Room: small and dark. Chamber: larger and smoother. Spring: three springs, with their boing. Early Digital: a reverb at 12 kHz and 12 bits, grainy, with a tail that ends abruptly.";
    case sSpaceAmount: return "Level of the reverb.";
    case sSpaceReact: return "The reverb ducks while you play, by up to 12 dB, and comes up in the gaps.";
    case sSpaceDecay: return "How long the tail rings.";
    default: return "";
    }
}

const char* const kPresets = "Starting points, each with two scenes. The arrows step through them. A * after the name means a control was changed since the preset was chosen. Save as... at the end of the list keeps the settings of this moment under a name of your own, for use in every song; the x behind such a preset deletes it.";
const char* const kRecord = "The record as it turns, drawn off-centre as far as Motion is turned up, with its scratches where Vinyl has them. Next to it the pitch over the last seconds, with a mark where a hit dragged it. An empty platter: Motion and Vinyl are both off.";
const char* const kListener = "What the plugin hears in the music, for every React knob. Level: this moment against the last few seconds, not against full scale, so React works the same on quiet and loud tracks. Hits: the start of a note. Gaps: how far the level has dropped. A sound that stays at one level gives React little to follow.";
const char* const kEdit = "Which scene the knobs below show and change. On every knob the arc is the scene being edited, the tick outside it is the other scene, and the dot is where Morph has it now.";
const char* const kCopy = "A > B copies scene A over scene B, B > A the other way, Swap exchanges them.";
const char* const kOut = "Peak level coming out, left and right, in dBFS.";
const char* const kReactBar = "What React is doing to this module right now.";
const char* const kHelp = "Help is on: rest the mouse on a control to read what it does. Click here again to switch it off.";

} // namespace help
