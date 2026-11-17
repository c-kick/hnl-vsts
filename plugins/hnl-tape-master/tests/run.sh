#!/bin/sh
# Builds and runs this plugin's offline tests with the host compiler:
#   dsp_test     measurements of the tape machine
#   plugin_test  the plugin driven through DPF's host-side wrapper
#   abuse_test   the same, by a host that does everything a host may do (common/tests/HostAbuse.hpp)
set -e
cd "$(dirname "$0")/.."
ROOT=../..
OUT=$ROOT/build/hnl-tape-master-tests
mkdir -p "$OUT"
g++ -std=c++17 -O2 -Wall -Wextra -o "$OUT/dsp_test" tests/dsp_test.cpp
"$OUT/dsp_test"
# Once more with the flags the plugin itself is built with (DPF: -O3 -ffast-math).
g++ -std=c++17 -O3 -ffast-math -Wall -Wextra -o "$OUT/dsp_test_release" tests/dsp_test.cpp
"$OUT/dsp_test_release"
g++ -std=gnu++17 -O2 -w -I$ROOT/dpf/distrho -I. -DDISTRHO_PLUGIN_TARGET_STATIC=1 -o "$OUT/plugin_test" \
    tests/plugin_test.cpp TapePlugin.cpp $ROOT/dpf/distrho/src/DistrhoPlugin.cpp
"$OUT/plugin_test"
g++ -std=gnu++17 -O2 -w -I$ROOT/dpf/distrho -I. -DDISTRHO_PLUGIN_TARGET_STATIC=1 -o "$OUT/abuse_test" \
    tests/abuse_test.cpp TapePlugin.cpp $ROOT/dpf/distrho/src/DistrhoPlugin.cpp
"$OUT/abuse_test"
# Saving, loading and deleting a preset through the UI class itself (common/tests/ui_presets_test.cpp).
PRESET_X=$(sed -n 's/.*kPresetX = \([0-9]*\).*/\1/p' TapeUI.cpp)
g++ -std=gnu++17 -O1 -Wall -Wextra -I$ROOT/tools/ui-preview -I. -DPRESET_X=$PRESET_X -o "$OUT/ui_presets_test" \
    $ROOT/common/tests/ui_presets_test.cpp TapeUI.cpp
"$OUT/ui_presets_test"
# The switchable meter design, through the UI class as well (common/tests/ui_meter_test.cpp).
g++ -std=gnu++17 -O1 -Wall -Wextra -I$ROOT/tools/ui-preview -I. -DMETER_LEFT=24 -DMETER_RIGHT=536 -DMETER_PARAM=kMeterVuL -o "$OUT/ui_meter_test" \
    $ROOT/common/tests/ui_meter_test.cpp TapeUI.cpp
"$OUT/ui_meter_test"
