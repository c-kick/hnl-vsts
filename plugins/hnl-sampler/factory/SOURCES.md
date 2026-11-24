# Where the sounds come from

The sounds in this folder are built into HNL Sampler. They are cut from recordings that their makers gave to the
public domain under [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/): free to use, change and pass on,
without conditions. The sounds here are passed on the same way, under CC0 1.0. The plugin's code is under the GPL
(see `LICENSE` at the top of the repository); these sounds are not bound by it.

CC0 asks for no credit. It is given gladly:

| Instrument | Folder | Cut from | By |
| --- | --- | --- | --- |
| Jazz Kit | `kit/` | [Virtuosity Drums](https://github.com/sfzinstruments/virtuosity_drums), the "mid" microphones | Versilian Studios and Karoryfer Samples; played by Austin McMahon at Virtuosity Musical Instruments, Boston |
| Strings | `strings/` | [VSCO 2 Community Edition](https://github.com/sgossner/VSCO-2-CE): the cello section and the violin section, sustained with vibrato, the loud layer | Versilian Studios |
| Pipe Organ | `organ/` | [Versilian Community Sample Library](https://github.com/sgossner/VCSL), Pipe Organ, the loud stop of the third manual | Sampled by Simon Dalzell of Ivy Audio, winter 2015-16; published by Versilian Studios |

Each of those repositories carries the CC0 text in its `LICENSE` file (checked on 2026-11-23).

## What was done to them

`tools/factory-sounds/make.cpp` makes every file here from the recordings, and `tools/factory-sounds/fetch.sh`
fetches exactly the recordings it uses. Nothing is edited by hand.

- **All:** left and right are mixed to one channel, the level is raised, and the result is stored as FLAC, 16 bit, at
  the rate of the recording (48 kHz for the drums, 44.1 kHz for the rest).
- **Drums:** each hit is cut to a length (0.35 s for a closed hi-hat, 3 s for the crash) and faded out over the
  last 40 % of it. The name says the layer from soft (1) to loud and the take (a, b).
- **Strings and organ:** each note runs from where it starts to the end of a loop in its held part. The loop is
  the one, of those found, over which the level moves least, and it is closed in the sound itself: over its last
  0.4 s (strings) or 0.15 s (organ) what led up to the loop's start fades in. All notes of an instrument are brought
  to one level. The name is the note that sounds; the names in the recordings of the strings lie an octave lower.
