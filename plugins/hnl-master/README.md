# HNL Master

A mastering chain in one plugin, for the master bus.

![HNL Master in Reason with the Room Ambience preset](docs/in-use.png)

Signal flow, left to right:

| Stage | Controls |
| --- | --- |
| Input | Gain, level meters |
| Multiband compressor | Three bands with adjustable crossovers; threshold, ratio, attack, release and gain per band |
| Equaliser | Low shelf, peak (with Q) and high shelf |
| Exciter | Tape, Tube or Retro character; frequency, drive, colour (odd to even harmonics) and mix; 4x oversampled |
| Stereo width | 0 to 200%, widening only above a set frequency; the mono sum never changes |
| Reverb | Decay, damping and amount; lows below 200 Hz stay dry |
| Limiter | Gain, ceiling and release; look-ahead with true-peak detection |
| Output | Level meters, momentary / short-term / integrated loudness (LUFS), true-peak maximum |

Twelve presets are in the menu next to the name. A `*` after the preset name means a control was changed since it
was chosen.

## Compact by design

A stage that is switched off collapses to its title bar, and the window shrinks to fit.

Everything on:

![Every stage switched on](docs/everything-on.png)

As a new instance opens:

![Default view](docs/default.png)

Only the limiter:

![Limiter only](docs/limiter-only.png)

## Using it

- Drag a knob up or down; hold Shift for fine steps. The mouse wheel also works.
- Double-click or Ctrl-click a knob to return it to its default.
- Reset meters clears the integrated loudness and the true-peak maximum.

The plugin reports its latency to the host (about 4 ms: 184 samples at 44.1 kHz, 196 at 48 kHz, 340 at 96 kHz).

## Installing

Download the zip from the [releases](https://github.com/c-kick/hnl-vsts/releases) and unzip it into
`C:\Program Files\Common Files\VST3\`. Windows 64-bit, VST3; tested in Reason.
