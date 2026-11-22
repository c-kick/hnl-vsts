# HNL Tape Echo

A tape echo with three playback heads and a spring reverb, in the spirit of the tape echo machines of the
1970s: warm repeats that darken and saturate, pitch that bends when you turn the time, and an echo that howls
by itself when you push it.

![HNL Tape Echo](docs/preview.png)

| Default | Help tips | Presets | Saving a preset |
| --- | --- | --- | --- |
| ![Default](docs/default.png) | ![Help tip on Intensity](docs/help.png) | ![Preset menu](docs/menu.png) | ![Name box after Save as...](docs/save.png) |

*Rendered from the plugin's drawing code; not yet screenshots from a host.*

## What it does

- **Three heads on one tape:** head 1 at the echo time, heads 2 and 3 at twice and three times that. Switch
  them in any combination for single echoes or rhythms.
- **A tape that moves:** the time control sets the tape speed. Turning it bends the pitch of the echoes already
  on the tape and the motor takes a moment to follow. Long times are darker than short ones.
- **Saturating repeats:** the echo is recorded again and again through the tape, so every repeat is a little
  more compressed and coloured. Past about 60% intensity it feeds itself and howls, held in check by the tape.
- **Spring reverb:** three springs with the splashy upward sweep of the real thing. The echoes reverberate too.
- **Wow, flutter and hiss:** an unsteady tape speed, and noise that is recorded along with the signal.
- **Tempo sync:** the echo time can follow the song tempo.

## Controls

| Control | What it changes |
| --- | --- |
| Head 1, 2, 3 | Click a head in the tape loop to switch it on or off. Each shows its echo time. |
| Echo time: Free / Sync | Free: the Time knob sets the time. Sync: it follows the tempo of the song. |
| Division | The note value of head 1 with Sync on: 1/32, 1/16 triplet, 1/16, 1/8 triplet or 1/8. With 1/16, head 2 plays an eighth and head 3 a dotted eighth. |
| Dry signal | On for use as an insert. Off for a send: echo and reverb only. |
| Input | Level onto the tape, -12 to +24 dB. Higher drives the tape harder. The level meter reads 0 where the tape starts to saturate clearly (a sine at -12 dBFS with Input at 0). |
| Time | Head 1, from 40 to 400 ms; head 3 reaches 1.2 s. |
| Intensity | How much of the echo is recorded again. One head starts to sustain by itself somewhere past 60%; three heads do so below 50%. |
| Echo | Echo level. At 100% the first repeat is as loud as the input. |
| Reverb | Spring reverb level. The springs are driven by the input and by the echoes (as loud as Echo sets them) and decay in about 2 seconds. |
| Bass, Treble | Tone of the echo, ±10 dB around 200 Hz and from 2.5 kHz. They sit inside the repeat loop, so each repeat gets them again. |
| Wobble | Wow and flutter. 25% is a machine in good order (a few cents of pitch movement); 100% needs service. |
| Noise | Tape hiss: off at 0, about 60 dB under the tape level at 50%, 20 dB more at 100%. With Intensity up it builds and can start the howl by itself. |
| Width | Spreads head 1 to the left, head 3 to the right and the springs apart. At 0% everything is mono. |

Thirteen presets are in the menu at the top right. The **?** button switches help on: rest the mouse on any
control to read what it does.

Drag a knob up or down (Shift for fine steps, mouse wheel also works); double-click or Ctrl-click returns it to
its default. The plugin adds no latency.

A howling echo is loud. The tape holds it back, but Echo, Bass and Treble come after the tape: put a limiter
behind the plugin if you plan to play with Intensity.

## About the model

The tape is modelled as it moves: every recorded sample remembers where on the tape it was written, and each
head plays what is passing under it. Speed changes, wow and flutter therefore behave as they do on a machine.
The echo itself is mono, as on machines of this kind; Width only places the heads on the way out. The springs
are chains of allpass filters in a loop.

It is built from how such machines are known to behave, not measured from one, and it is not affiliated with
any maker of echo machines. Tape splices, motor noise and the wear of a particular unit are not modelled.

## Installing

[Download HNL-Tape-Echo-win64.zip](https://github.com/c-kick/hnl-vsts/releases/download/downloads/HNL-Tape-Echo-win64.zip): the current build, for 64-bit Windows.

The zip holds the plugin as VST3
(copy the `.vst3` folder to `C:\Program Files\Common Files\VST3\`) and as CLAP (copy the `.clap` file to `C:\Program Files\Common Files\CLAP\`). A host needs only one of the
two. Windows 64-bit.

## Changelog

### 0.4.0 (2026-11-22)

- New: three looks. Classic, the look the plugin has always had, stays the default; Hi-Fi Black and Hi-Fi Silver
  dress the same window as a piece of hi-fi equipment. The cog at the top right chooses, for every HNL plugin at
  once. The choice changes nothing about the sound and is not stored in the song.
- To make room for the cog, the preset bar and the help button stand 28 pixels further left.

### 0.3.3 (2026-11-18)

- Fixed: samples and parameter values that are not numbers are caught again in the builds made with clang.

### 0.3.2 (2026-11-17)

- Fixed: a preset's name can be typed in a host that hands over the keys without their text.
- The licence text (GPL-3.0-or-later) comes with the download.

### 0.3.1 (2026-11-17)

- The version is shown behind the title, next to the HNL logo; the preset bar stands at the right.
- The preset menu spreads a long list evenly over its columns.
- A CLAP build next to the VST3.

### 0.3.0 (2026-11-17)

- New: presets of your own, saved, replaced and deleted from the preset menu.

### 0.2.1 (2026-11-17)

- Guards against samples and parameter values that are not numbers, and against whatever else a host may send.

### 0.2.0 (2026-11-16)

- The echoes feed the spring reverb.

### 0.1.0 (2026-11-16)

- First version: a three-head tape echo with spring reverb.
