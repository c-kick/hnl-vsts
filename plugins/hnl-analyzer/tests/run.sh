#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink
# Builds and runs this plugin's offline tests with the host compiler:
#   dsp_test     the meter against signals whose figures are known (EBU Tech 3341 and 3342 among them),
#                the advisor, the comparison with a reference, the JSON and the reports
#   engine_test  when a capture begins and ends and what is kept, the running meter, files measured for a reference
#   plugin_test  the plugin driven through DPF's host-side wrapper
#   abuse_test   the same, by a host that does everything a host may do (common/tests/HostAbuse.hpp)
#   ui_test      the UI class itself, clicked and typed in, on the drawing stand-in of tools/ui-preview; with an HNL Master to find
#   ui_presets_test, ui_skin_test  the presets and the choice of look, shared by all plugins
set -e
cd "$(dirname "$0")/.."
ROOT=../..
OUT=$ROOT/build/hnl-analyzer-tests
mkdir -p "$OUT"
# The plugins under test find each other under a name of their own, not the HNL plugins in a host that happens to run (common/Link.hpp).
export HNL_LINK_NAME="hnl-test-$$"
trap 'rm -f "/dev/shm/$HNL_LINK_NAME"' EXIT
# The engine tests plainly, and once more with the flags the plugin itself is built with (DPF: -O3 -ffast-math).
for t in dsp engine; do
    g++ -std=c++17 -O2 -Wall -Wextra -o "$OUT/${t}_test" tests/${t}_test.cpp -lpthread
    "$OUT/${t}_test" "$OUT/work" > "$OUT/${t}_test.log" || { grep -v '^PASS' "$OUT/${t}_test.log"; exit 1; }
    g++ -std=c++17 -O3 -ffast-math -Wall -Wextra -o "$OUT/${t}_test_release" tests/${t}_test.cpp -lpthread
    "$OUT/${t}_test_release" "$OUT/work" > "$OUT/${t}_test.log" || { grep -v '^PASS' "$OUT/${t}_test.log"; exit 1; }
    echo "$t: $(grep -c '^PASS' "$OUT/${t}_test.log") checks passed, in both builds"
done
# -DDGL_USE_FILE_BROWSER: what DPF's own makefile sets for a plugin that uses the system's file dialog.
g++ -std=gnu++17 -O2 -w -I$ROOT/dpf/distrho -I. -DDISTRHO_PLUGIN_TARGET_STATIC=1 -DDGL_USE_FILE_BROWSER -o "$OUT/plugin_test" \
    tests/plugin_test.cpp AnalyzerPlugin.cpp $ROOT/dpf/distrho/src/DistrhoPlugin.cpp -lpthread
"$OUT/plugin_test"
g++ -std=gnu++17 -O2 -w -I$ROOT/dpf/distrho -I. -DDISTRHO_PLUGIN_TARGET_STATIC=1 -DDGL_USE_FILE_BROWSER -o "$OUT/abuse_test" \
    tests/abuse_test.cpp AnalyzerPlugin.cpp $ROOT/dpf/distrho/src/DistrhoPlugin.cpp -lpthread
"$OUT/abuse_test"
# The UI tests see the settings, presets and references of a new user, not those of whoever runs them.
rm -rf "$OUT/ui"
mkdir -p "$OUT/ui/presets"
export HNL_SETTINGS_DIR="$PWD/$OUT/ui/settings"
export HNL_PRESET_DIR="$PWD/$OUT/ui/presets"
export HNL_REFERENCE_DIR="$PWD/$OUT/ui/references"
g++ -std=gnu++17 -O1 -Wall -Wextra -I$ROOT/tools/ui-preview -I. -o "$OUT/ui_test" tests/ui_test.cpp AnalyzerUI.cpp -lpthread
"$OUT/ui_test" "$OUT/ui/work"
# Saving, loading and deleting a preset through the UI class itself (common/tests/ui_presets_test.cpp).
rm -rf "$HNL_SETTINGS_DIR"
PRESET_X=$(sed -n 's/.*kPresetX = UI_BASE_WIDTH - \([0-9]*\).*/\1/p' AnalyzerUI.cpp)
WIDTH=$(sed -n 's/^#define UI_BASE_WIDTH *\([0-9]*\)$/\1/p' DistrhoPluginInfo.h)
g++ -std=gnu++17 -O1 -Wall -Wextra -I$ROOT/tools/ui-preview -I. -DPRESET_X=$((WIDTH - PRESET_X)) -o "$OUT/ui_presets_test" \
    $ROOT/common/tests/ui_presets_test.cpp AnalyzerUI.cpp -lpthread
"$OUT/ui_presets_test"
# The choice of look, shared by all plugins (common/tests/ui_skin_test.cpp).
rm -rf "$HNL_SETTINGS_DIR"
g++ -std=gnu++17 -O1 -Wall -Wextra -I$ROOT/tools/ui-preview -I. -o "$OUT/ui_skin_test" $ROOT/common/tests/ui_skin_test.cpp AnalyzerUI.cpp -lpthread
"$OUT/ui_skin_test"
# The size of the window, shared by all plugins (common/tests/ui_size_test.cpp).
g++ -std=gnu++17 -O1 -Wall -Wextra -I$ROOT/tools/ui-preview -I. -o "$OUT/ui_size_test" $ROOT/common/tests/ui_size_test.cpp AnalyzerUI.cpp -lpthread
"$OUT/ui_size_test"
