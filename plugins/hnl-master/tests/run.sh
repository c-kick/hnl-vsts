#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink
# Builds and runs this plugin's offline tests with the host compiler:
#   dsp_test     measurements of the audio engine
#   plugin_test  the plugin driven through DPF's host-side wrapper
#   abuse_test   the same, by a host that does everything a host may do (common/tests/HostAbuse.hpp)
set -e
cd "$(dirname "$0")/.."
ROOT=../..
OUT=$ROOT/build/hnl-master-tests
mkdir -p "$OUT"
g++ -std=c++17 -O2 -Wall -Wextra -o "$OUT/dsp_test" tests/dsp_test.cpp
"$OUT/dsp_test"
# Once more with the flags the plugin itself is built with (DPF: -O3 -ffast-math).
g++ -std=c++17 -O3 -ffast-math -Wall -Wextra -o "$OUT/dsp_test_release" tests/dsp_test.cpp
"$OUT/dsp_test_release"
g++ -std=gnu++17 -O2 -w -I$ROOT/dpf/distrho -I. -DDISTRHO_PLUGIN_TARGET_STATIC=1 -o "$OUT/plugin_test" \
    tests/plugin_test.cpp MasterPlugin.cpp $ROOT/dpf/distrho/src/DistrhoPlugin.cpp
"$OUT/plugin_test"
g++ -std=gnu++17 -O2 -w -I$ROOT/dpf/distrho -I. -DDISTRHO_PLUGIN_TARGET_STATIC=1 -o "$OUT/abuse_test" \
    tests/abuse_test.cpp MasterPlugin.cpp $ROOT/dpf/distrho/src/DistrhoPlugin.cpp
"$OUT/abuse_test"
# Saving, loading and deleting a preset through the UI class itself (common/tests/ui_presets_test.cpp).
PRESET_RIGHT=$(sed -n 's/.*kPresetRight = \([0-9]*\).*/\1/p' MasterUI.cpp)
g++ -std=gnu++17 -O1 -Wall -Wextra -I$ROOT/tools/ui-preview -I. -DPRESET_FROM_RIGHT=$PRESET_RIGHT -o "$OUT/ui_presets_test" \
    $ROOT/common/tests/ui_presets_test.cpp MasterUI.cpp
"$OUT/ui_presets_test"
