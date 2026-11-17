# HNL Digital Piano

An instrument with five pianos, each as its own voice, all in stereo: the digital pianos of the 1980s and
1990s, and a concert grand.

![HNL Digital Piano](docs/preview.png)

| Grand | Help tips | Presets | Saving a preset |
| --- | --- | --- | --- |
| ![Grand voice](docs/grand.png) | ![Help tip on Width](docs/help.png) | ![Preset menu](docs/menu.png) | ![Name box after Save as...](docs/save.png) |

*Rendered from the plugin's drawing code; not yet screenshots from a host.*

## Voices

| Voice | What it is | Where its sound comes from |
| --- | --- | --- |
| Workstation | The sampled piano of late-1980s workstation keyboards: the piano of countless house records. Hard attack, strong fundamental, a tone that mellows within a second. | Fitted to recordings of single notes, a note every three keys from C1 to C7, held for six seconds; the fade after that carries on at the rate it had. Within about 1.5 dB of them. Its source answers a softer touch only by playing quieter; here soft notes are also a little darker (Velocity). The source is mono; the two sides are made here, further apart than for the other voices. |
| FM Electric | The glassy electric piano of 1980s FM synthesizers: a bell on the attack, a body that barks when played hard, a slow shimmer from side to side. | Computed with FM synthesis. An FM patch of its own in that style; not compared with a reference. |
| Stage 80s | The rounded, slightly artificial stage piano of the mid 1980s. Its tone changes a lot with touch. | Fitted to single notes at four velocities, a note every six keys, rendered with an emulator of the original sound engine. Within about 1.5 dB of them. |
| Dance 90s | The thin, cutting piano of 1990s dance records. | Worked out from three recorded chords around middle C, the only reference there was. Chords it plays are within about 2.5 dB of those; the rest of the keyboard is extrapolated and the attack is blurred. The weakest of the fitted voices. |
| Grand | A concert grand, from soft to loud. | Fitted in stereo to single notes of a real grand played soft, medium and loud, every third key, from the University of Iowa Musical Instrument Samples. |

Switching voice affects new notes; notes already sounding keep theirs. The first time a voice is chosen, the
plugin works out its waves, which takes around a second. It does so beside the audio, which carries on: until
the waves are there, new notes still get the voice that was playing. A song that opens with a voice already
chosen has it from the first note.

**No audio of any instrument is stored in the plugin or in this repository.** A fitted voice holds
measurements: for each reference note, every partial (its frequency, its starting phase, and its level at 29
moments from the first millisecond to ten seconds) and the dull knock of the attack. `tools/fit.cpp` and
`tools/fit_chords.cpp` make those from recordings; the data is in `dsp/*Data.hpp`. The plugin adds up sine
waves from them and so computes one wave per measured note and velocity; as on the original machines, each wave
serves the keys around it, played a little faster or slower. The "within about N dB" figures compare the
plugin's output with the reference in third-octave bands over the first seconds of a note.

The measured pianos are mono, except the grand. Their waves are made in stereo here, as if recorded with two microphones: left and
right carry the same partials at somewhat different levels and phases. The fundamental keeps the same level on
both sides, so no note leans by chance, and the bass stays in the middle. With Width at 0% the result is mono,
and closest to the measured sound. All voices place low keys a little to the left and high keys to the right.

### FM Electric

Sine waves whose phase is pushed around by other sine waves, three pairs per note. One pair has its modulator
fourteen times above the note and makes the bell of the attack; two pairs make the body. The second body pair
is tuned a fraction of a hertz above the first, added on the left and taken off on the right: that is the slow
shimmer from side to side, and it disappears completely in mono, so nothing cancels there.

### Grand

Fitted to the piano recordings of the [University of Iowa Musical Instrument Samples](https://theremin.music.uiowa.edu/MIS.html),
which are free to use: single notes at three dynamics, of which every third key was measured. The three
dynamics became three sets of waves; in between, a filter does the rest.

- Those are recordings in a room, with its noise. The fitting follows each partial only until it sinks into
  that noise and lets it fade on from there, so the noise itself is not part of the sound.
- **Stereo:** the recordings are stereo and both sides were measured, with the note's start and the place of
  its partials found once for both. The two microphones stood far apart: in the recordings some notes are
  7 dB louder on one side, and for several the sides partly cancel when summed. An instrument should not do
  that, so the measured differences are kept within bounds: the fundamental stays nearly centred, a partial
  leans at most 3 dB to a side, and no partial is more than a quarter turn out of step between the sides.
  The result is the real piano's own width, with every key near the middle and at most about 3 dB lost in mono.
  The tone lies between the two microphones. Unlike the other voices, keys are not placed left to right on
  top of that.
- The soft notes high up hardly rise above the noise; from about two octaves above middle C, soft playing uses
  the medium notes with a darker filter. The top keys use the loud notes for every touch. The lowest three
  keys borrow from the lowest C, played a little slower.
- A real note fades fast at first (about 20 dB in the first second at middle C) and then lingers quietly for a
  long time. That comes straight from the measurements.
- Added on top: dampers that are slow on the bass strings, quick in the treble and absent on the top keys,
  which ring on.

Not there: strings ringing in sympathy with the pedal down, the noise of pedal and keys, and the change of tone
when a note is struck again while it still sounds. A first version of this voice was a model written from the
physics alone; it did not sound convincing and was replaced.

## Controls

| Control | What it changes |
| --- | --- |
| Volume | Output level. A hard eight-note chord stays just under full scale at 0 dB. |
| Tone | Closes or opens the filter on every note; to the right it also stays open for soft notes. On FM Electric: how hard the tone barks, from mellow to bright and bell-like. |
| Velocity | How much playing harder adds level and brightness. At 0% every note is the same, for programmed stabs. |
| Decay | How long a held note rings: 100% is the natural length (long in the bass, short in the treble); lower fades it sooner. |
| Release | How long a note rings on after the key is let go: the time to fall 60 dB, 50 ms to 5 s (default 600 ms). On Grand it follows the dampers: longer in the bass, shorter in the treble. |
| Detune | Above 0, every note sounds twice, one sharp and one flat, spread left and right (two voices per note). On FM Electric: a faster shimmer instead. |
| Width | 100% is the full stereo image; 0% is mono. |
| Reverb | A touch of room. |
| Tune | Fine tuning, a semitone either way. |
| Octave | Shifts the keyboard by up to two octaves. |
| Voice | Which piano plays. |

Twenty-seven presets are in the menu next to the name; they leave Volume and Tune alone. The **?** button switches
help on: rest the mouse on any control to read what it does.

The keyboard on screen plays notes for trying a sound: click a key (lower on the key is harder), drag to slide.
Drag a knob up or down (Shift for fine steps, mouse wheel also works); double-click or Ctrl-click returns it to
its default.

## MIDI

Notes with velocity, sustain pedal (CC 64), pitch bend (two semitones), all notes off (CC 123), all sound
off (CC 120) and reset all controllers (CC 121: pitch bend to the middle, pedal up). 64 voices; when they run out, the quietest note makes way. No latency.

The plugin was called HNL House Piano up to 0.3.0. It kept its identity, so songs that used it should open
with the renamed plugin.

## Installing

Download the zip from the [releases](https://github.com/c-kick/hnl-vsts/releases) and unzip it into
`C:\Program Files\Common Files\VST3\`. Windows 64-bit, VST3. It is an instrument: in Reason, add it from the
instruments, not the effects.
