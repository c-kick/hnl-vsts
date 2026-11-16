# HNL House Piano

A digital piano in the manner of the sample-playback workstations of the late 1980s: the piano of countless
house and dance records. Hard attack, strong fundamental, a tone that mellows within a second, and in stereo.

![HNL House Piano](docs/preview.png)

| Help tips | Presets |
| --- | --- |
| ![Help tip on Width](docs/help.png) | ![Preset menu](docs/menu.png) |

*Rendered from the plugin's drawing code; not yet screenshots from a host.*

## How it makes its sound

Those workstations played short recordings of a piano through a filter and an envelope. What comes out has a
character of its own: a hard, bright attack over a strong fundamental, overtones that fall away within a second,
and a tail that is almost a pure tone.

This plugin plays no recordings. It was fitted to reference recordings of such a piano, one note every six
semitones: for each of those notes, `tools/fit.cpp` measured every partial (its frequency, its starting phase,
and its level at 29 moments from the first millisecond to ten seconds) and the dull knock of the attack below the
note's pitch. Those measurements are in `dsp/PianoData.hpp`. When the plugin loads, it adds up sine waves from
them and so computes one wave per measured note; as on the original machines, each wave serves the keys around
it, played a little faster or slower. **No audio of any instrument is stored in the plugin or in this
repository.**

Measured against the reference notes, in third-octave bands from the first moment to four seconds in, the
plugin's output differs by about 2 dB on average. Keys between the measured notes cannot be checked that way.

## Stereo

The measured piano is mono. Here every wave is made in stereo, as if the piano were recorded with two
microphones: left and right carry the same partials at somewhat different levels and phases. The fundamental
keeps the same level on both sides, so no note leans to one side by chance; the bass stays in the middle, and
low keys sit a little to the left and high keys to the right. With Width at 0% the result is mono, and closest
to the measured sound.

## Controls

| Control | What it changes |
| --- | --- |
| Volume | Output level. A hard eight-note chord stays just under full scale at 0 dB. |
| Tone | Closes or opens the filter on every note: darker and rounder to the left. At full velocity it is already wide open; to the right it also stays open for soft notes. |
| Velocity | How much playing harder adds level and brightness. At 0% every note is the same, for programmed stabs. |
| Decay | How long a held note rings: 100% is the natural length as measured (long in the bass, short in the treble); lower fades it sooner. |
| Release | How long a note rings on after the key is let go: the time to fall 60 dB, 50 ms to 5 s (default 600 ms). |
| Detune | Above 0, every note sounds twice, one sharp and one flat, spread left and right. Uses two voices per note. |
| Width | 100% is the full stereo image; 0% is mono. |
| Reverb | A touch of room. |
| Tune | Fine tuning, a semitone either way. |
| Octave | Shifts the keyboard by up to two octaves. |

Eleven presets are in the menu next to the name; they leave Volume and Tune alone. The **?** button switches
help on: rest the mouse on any control to read what it does.

The keyboard on screen plays notes for trying a sound: click a key (lower on the key is harder), drag to slide.
Drag a knob up or down (Shift for fine steps, mouse wheel also works); double-click or Ctrl-click returns it to
its default.

## MIDI

Notes with velocity, sustain pedal (CC 64), pitch bend (two semitones), all notes off (CC 123) and all sound
off (CC 120). 64 voices; when they run out, the quietest note makes way. No latency.

## Installing

Download the zip from the [releases](https://github.com/c-kick/hnl-vsts/releases) and unzip it into
`C:\Program Files\Common Files\VST3\`. Windows 64-bit, VST3. It is an instrument: in Reason, add it from the
instruments, not the effects.
