#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink
# Builds and runs this plugin's offline tests with the host compiler:
#   audiofile_test  reading WAV, AIFF and FLAC, also damaged ones
#   import_test     reading SFZ, SoundFont and Scala files
#   analysis_test   pitch, hits, tempo and loop points found in a sound
#   dsp_test        measurements of the engine, and the core that reads files beside the audio
#   plugin_test     the plugin driven through DPF's host-side wrapper
#   abuse_test      the same, by a host that does everything a host may do (common/tests/HostAbuse.hpp)
#   ui_test         the UI class itself, clicked and dragged, on the drawing stand-in of tools/ui-preview
set -e
cd "$(dirname "$0")/.."
ROOT=../..
OUT=$ROOT/build/hnl-sampler-tests
mkdir -p "$OUT"
# Each engine test plainly, and once more with the flags the plugin itself is built with (DPF: -O3 -ffast-math).
for t in audiofile import analysis dsp; do
    g++ -std=c++17 -O2 -Wall -Wextra -o "$OUT/${t}_test" tests/${t}_test.cpp -lpthread
    "$OUT/${t}_test" > "$OUT/${t}_test.log" || { grep -v '^PASS' "$OUT/${t}_test.log"; exit 1; }
    g++ -std=c++17 -O3 -ffast-math -Wall -Wextra -o "$OUT/${t}_test_release" tests/${t}_test.cpp -lpthread
    "$OUT/${t}_test_release" > "$OUT/${t}_test.log" || { grep -v '^PASS' "$OUT/${t}_test.log"; exit 1; }
    echo "$t: $(grep -c '^PASS' "$OUT/${t}_test.log") checks passed, in both builds"
done
g++ -std=gnu++17 -O2 -w -I$ROOT/dpf/distrho -I. -DDISTRHO_PLUGIN_TARGET_STATIC=1 -o "$OUT/plugin_test" \
    tests/plugin_test.cpp SamplerPlugin.cpp $ROOT/dpf/distrho/src/DistrhoPlugin.cpp -lpthread
"$OUT/plugin_test"
g++ -std=gnu++17 -O2 -w -I$ROOT/dpf/distrho -I. -DDISTRHO_PLUGIN_TARGET_STATIC=1 -o "$OUT/abuse_test" \
    tests/abuse_test.cpp SamplerPlugin.cpp $ROOT/dpf/distrho/src/DistrhoPlugin.cpp -lpthread
"$OUT/abuse_test"
# The UI works in a folder of its own: its sounds, and the instruments it saves.
rm -rf "$OUT/ui"
mkdir -p "$OUT/ui/presets"
g++ -std=gnu++17 -O1 -Wall -Wextra -I$ROOT/tools/ui-preview -I. -o "$OUT/ui_test" tests/ui_test.cpp SamplerUI.cpp -lpthread
HNL_PRESET_DIR="$PWD/$OUT/ui/presets" HOME="$PWD/$OUT/ui" USERPROFILE="$PWD/$OUT/ui" "$OUT/ui_test" "$OUT/ui"
