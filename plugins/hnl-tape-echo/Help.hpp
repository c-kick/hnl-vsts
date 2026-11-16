// Help texts, shown as tips when help is switched on in the UI.
#pragma once

#include "Params.hpp"

namespace help {

inline const char* forParam(uint32_t id) {
    switch (id) {
    case kInput: return "Level into the preamp and onto the tape. Higher drives the tape harder: the echoes get thicker and more compressed. Watch the tape meter; around 0 is a healthy level.";
    case kTime: return "Time from the record head to head 1; heads 2 and 3 play at twice and three times that. It sets the tape speed, so turning it bends the pitch of the echoes already on the tape, and long times sound darker.";
    case kSync: return "Free: the Time knob sets the echo time. Sync: the time follows the song tempo, in the note value chosen under Division.";
    case kDivision: return "The note value of head 1 when Sync is on. Heads 2 and 3 are twice and three times as long: with 1/16, head 2 plays an eighth and head 3 a dotted eighth.";
    case kHead1: return "Playback head 1, the shortest echo. Click to switch it on or off. With Width up it leans to the left.";
    case kHead2: return "Playback head 2, at twice the time of head 1. It stays in the middle.";
    case kHead3: return "Playback head 3, at three times the time of head 1. With Width up it leans to the right.";
    case kIntensity: return "How much of the echo is recorded again: the number of repeats. Somewhere past 60% the echo feeds itself and howls, held in check only by the tape. More heads get there sooner.";
    case kEcho: return "Level of the echo. At 100% the first repeat is as loud as the input.";
    case kReverb: return "Level of the spring reverb. The springs hear the input and the echoes, as loud as Echo sets them, and have the splashy sweep of real springs.";
    case kBass: return "Lows of the echo, around 200 Hz and below. It sits inside the repeat loop, so every repeat gets it again: cutting makes the repeats thin out, boosting makes them boom.";
    case kTreble: return "Highs of the echo, from about 2.5 kHz. Also inside the loop: cutting makes each repeat darker than the last.";
    case kWobble: return "Unsteadiness of the tape speed: slow wow and faster flutter. A little brings the echoes alive; a lot sounds like a machine that needs service.";
    case kNoise: return "Tape hiss. Off at 0, a machine in good order at 50%. It is recorded with the signal, so with Intensity up it builds, and it can start the howl by itself.";
    case kWidth: return "Spreads the sound out: head 1 to the left, head 3 to the right, and the springs apart. At 0% everything is mono, as on a machine of this kind.";
    case kDry: return "On: the input passes through next to the echo, for use as an insert. Off: echo and reverb only, for use on a send.";
    default: return "";
    }
}

const char* const kPresets = "Starting points. The arrows step through them. A * after the name means a control was changed since the preset was chosen.";
const char* const kTape = "Level going onto the tape, feedback included. Around 0 the tape starts to saturate clearly; far into the red the echoes are squashed and gritty.";
const char* const kOutput = "Peak level leaving the plugin, in dBFS. A howling echo is loud: keep an eye on this.";
const char* const kHelp = "Help is on: rest the mouse on a control to read what it does. Click here again to switch it off.";

} // namespace help
