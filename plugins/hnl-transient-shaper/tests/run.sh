#!/bin/sh
# Builds and runs this plugin's offline tests with the host compiler:
#   dsp_test     measurements of the shaper
#   plugin_test  the plugin driven through DPF's host-side wrapper
#   abuse_test   the same, by a host that does everything a host may do (common/tests/HostAbuse.hpp)
set -e
cd "$(dirname "$0")/.."
ROOT=../..
OUT=$ROOT/build/hnl-transient-shaper-tests
mkdir -p "$OUT"
g++ -std=c++17 -O2 -Wall -Wextra -o "$OUT/dsp_test" tests/dsp_test.cpp
"$OUT/dsp_test"
# Once more with the flags the plugin itself is built with (DPF: -O3 -ffast-math).
g++ -std=c++17 -O3 -ffast-math -Wall -Wextra -o "$OUT/dsp_test_release" tests/dsp_test.cpp
"$OUT/dsp_test_release"
g++ -std=gnu++17 -O2 -w -I$ROOT/dpf/distrho -I. -DDISTRHO_PLUGIN_TARGET_STATIC=1 -o "$OUT/plugin_test" \
    tests/plugin_test.cpp ShaperPlugin.cpp $ROOT/dpf/distrho/src/DistrhoPlugin.cpp
"$OUT/plugin_test"
g++ -std=gnu++17 -O2 -w -I$ROOT/dpf/distrho -I. -DDISTRHO_PLUGIN_TARGET_STATIC=1 -o "$OUT/abuse_test" \
    tests/abuse_test.cpp ShaperPlugin.cpp $ROOT/dpf/distrho/src/DistrhoPlugin.cpp
"$OUT/abuse_test"
