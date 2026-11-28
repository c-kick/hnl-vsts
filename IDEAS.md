# Ideas for more plugins

A list to pick from, not a plan. The first part is what could still be added to plugins that exist; the rest is
plugins that have not been started.

## Additions to existing plugins

- **HNL Patina:** letting the tail of a reverb ring out when Space changes type (now it is cut in 25 ms), the
  record's speed tied to the song's tempo (one turn a bar), the mains frequency as a setting for all plugins,
  more types per module.

- **HNL Bus Compressor:** an external sidechain input (as in HNL Channel Spacer), and an optional saturating
  gain stage; it is clean as built.
- **HNL Leveller:** oversampling for the colour stage, so that high Colour settings stay clean on bright
  material.
- **HNL Thickener:** oversampling for Drive, more than one room character with a pre-delay (the room is
  our own small model; a click recorded through the rack reverb it replaces would let it be matched), and a
  goniometer next to the L/R bar.
- **HNL Widener:** the response plot moving along with Drift, a full goniometer in place of the fan, and a
  second Diffuse pattern to choose from.
- **HNL Transient Shaper:** an output soft clipper for the boosted peaks, and shaping of only the range the
  detector listens to (multiband).
- **HNL Digital Piano:** more voices, once there are reference recordings to fit them to (single notes across
  the keyboard, dry): an early sampled grand, a General MIDI piano, an electric grand (CP-70 style), the
  workstation organ. The Dance 90s voice would gain most from proper single-note references. The emulator used
  for the Stage 80s voice also holds that machine's electric pianos, harpsichord, clavi and vibraphone.

- **HNL Analyzer:** now that it finds HNL Master in a song, shows its meters of the moment (0.2.0) and sets its
  limiter by the advice (0.3.0): what the limiter would do at another gain shown in HNL Master before it is set,
  so that one click lands without playing the track twice; a key that takes the last click back;
  HNL Master's figures over the time of the track (the gain reduction of each band and of the limiter, the level
  before the chain), before and after compared at equal loudness, and those figures in the reports; the choice
  between several HNL Masters kept with the song; an MP3 reader, should code of others ever be let in;
  more than two channels; a goniometer; a reference's spectrum as a difference curve; reading a marker or a
  loop from the host to capture only that stretch.
- **HNL Sampler:** what its first version leaves out: streaming long sounds from disk, recording and
  resampling, drawing and editing the sound itself, a phrase sequencer with MIDI out, envelopes with more
  stages, envelopes and filters per zone, every preset of a SoundFont, finding moved sounds again, a button
  for the tempo detection that is already in its code, favourite folders and a search in its file page, and
  files dropped on its window (DPF passes none on).

## Dynamics

Built: the bus compressor, the levelling compressor and the transient shaper.

- **De-esser.**
- **Gate/expander.**

## Tone and colour

- **Channel EQ:** console style, with a preamp drive stage.
- **Ladder filter** with an LFO. HNL Patina has a ladder filter that follows the level.
- **Bit and sample-rate reduction**, as early samplers did it: a seventh module for HNL Patina, or a plugin of
  its own.

Built: HNL Patina, which took the place of the saturation box (tube, transistor, diode and fuzz, oversampled)
and of the vinyl half of the lo-fi unit. Cassette wobble is in the tape plugins.

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

- **Stereo tool:** mid/side balance on its own. Mono below a frequency and a correlation meter are in HNL
  Widener.

Built: HNL Analyzer, which took the place of the loudness and spectrum meter.

## Instruments

Built: the digital piano and the sampler.

- **Synth or drum machine.**

## Bigger steps

- **Pitch correction, vocoder, guitar-amp modelling:** possible, but each is a much larger project and hard to
  judge without listening tests in a host.
