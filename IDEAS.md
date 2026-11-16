# Ideas for more plugins

A list to pick from, not a plan. The first part is what could still be added to plugins that exist; the rest is
plugins that have not been started.

## Additions to existing plugins

- **HNL Bus Compressor:** an external sidechain input (as in HNL Channel Spacer), and an optional saturating
  gain stage; it is clean as built.
- **HNL Leveller:** oversampling for the colour stage, so that high Colour settings stay clean on bright
  material.
- **HNL Transient Shaper:** an output soft clipper for the boosted peaks, and shaping of only the range the
  detector listens to (multiband).
- **HNL Digital Piano:** more voices, once there are reference recordings to fit them to (single notes across
  the keyboard, dry): an early sampled grand, a General MIDI piano, an electric grand (CP-70 style), the
  workstation organ. The Dance 90s voice would gain most from proper single-note references. The emulator used
  for the Stage 80s voice also holds that machine's electric pianos, harpsichord, clavi and vibraphone.

## Dynamics

Built: the bus compressor, the levelling compressor and the transient shaper.

- **De-esser.**
- **Gate/expander.**

## Tone and colour

- **Channel EQ:** console style, with a preamp drive stage.
- **Saturation box:** tube, transformer, diode and fuzz in one, oversampled.
- **Lo-fi unit:** bit and sample-rate reduction, vinyl crackle and warp, cassette wobble.
- **Ladder filter** with envelope follower and LFO.

## Modulation and space

- **Plate/hall reverb:** a full standalone reverb with pre-delay, size, decay, damping and modulation. The
  reverbs inside HNL Master, HNL Tape Echo and HNL Digital Piano are deliberately simple.
- **Chorus/ensemble:** the bucket-brigade chorus of 80s string machines and polysynths, plus a wide
  "dimension" mode. Can reuse the moving delay of the tape plugins.
- **Phaser** and **tape-style flanger.**
- **Rotary speaker, tremolo and vibrato.**
- **Shimmer or granular delay.**
- **Convolution reverb** that loads your own impulse responses.

## Utility

- **Stereo tool:** mid/side balance, mono below a frequency, correlation meter.
- **Loudness and spectrum meter** as a plugin of its own.

## Instruments

Built: the digital piano.

- **Synth or drum machine.**

## Bigger steps

- **Pitch correction, vocoder, guitar-amp modelling:** possible, but each is a much larger project and hard to
  judge without listening tests in a host.
