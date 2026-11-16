// Help texts, shown as tips when help is switched on in the UI.
#pragma once

#include "Params.hpp"

namespace help {

inline const char* forParam(uint32_t id) {
    switch (id) {
    case kVolume: return "Output level. A hard eight-note chord stays just under full scale at 0 dB. Presets leave it alone.";
    case kTone: return "Opens or closes the filter on every note: darker to the left, and to the right it stays open also when played softly. On FM Electric it sets how hard the tone barks: mellow to the left, bright and bell-like to the right.";
    case kVelocity: return "How much playing harder changes the sound: louder and brighter. At 0% every note comes out the same, which suits programmed chord stabs.";
    case kDecay: return "How long a held note rings. 100% is the natural length: long in the bass, short in the treble. Lower fades it sooner, for short, percussive notes.";
    case kRelease: return "How long a note rings on after the key is let go (the time to fall 60 dB). Short is tight and clipped; long lets chords melt into each other. On Grand it follows the dampers: longer in the bass, shorter in the treble, and the top keys ring on.";
    case kDetune: return "Above 0, every note sounds twice, one a little sharp and one a little flat, spread left and right, using two voices per note. On FM Electric it speeds up the shimmer from side to side instead.";
    case kWidth: return "Every voice is stereo. 100% is the full width, with low keys a little to the left and high keys to the right; 0% is mono.";
    case kReverb: return "A touch of room around the piano.";
    case kTune: return "Fine tuning, up to a semitone either way. Presets leave it alone.";
    case kOctave: return "Moves the whole keyboard up or down by octaves.";
    case kVoice: return "The piano that plays. Workstation: the hard sampled piano of late-1980s keyboards, the piano of house records. FM Electric: the glassy, bell-like electric piano of FM synthesizers. Stage 80s: the rounded stage piano of the 1980s. Dance 90s: the thin, cutting piano of 1990s dance records. Grand: a modelled concert grand. The first time a voice is chosen it takes a moment to work out its sound.";
    default: return "";
    }
}

const char* const kPresets = "Starting points. The arrows step through them. A * after the name means a control was changed since the preset was chosen.";
const char* const kKeyboard = "Click a key to hear it. Lower on the key is harder; drag to slide over the keys. Notes from the host do not light up here.";
const char* const kOutput = "Peak level leaving the plugin, in dBFS, and the number of voices sounding. There are 64; when they run out, the quietest note makes way.";
const char* const kHelp = "Help is on: rest the mouse on a control to read what it does. Click here again to switch it off.";

} // namespace help
