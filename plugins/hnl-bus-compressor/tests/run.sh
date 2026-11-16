#!/bin/sh
# Builds and runs this plugin's offline tests with the host compiler:
#   dsp_test     measurements of the compressor
#   plugin_test  the plugin driven through DPF's host-side wrapper
set -e
cd "$(dirname "$0")/.."
ROOT=../..
OUT=$ROOT/build/hnl-bus-compressor-tests
mkdir -p "$OUT"
g++ -std=c++17 -O2 -Wall -Wextra -o "$OUT/dsp_test" tests/dsp_test.cpp
"$OUT/dsp_test"
g++ -std=gnu++17 -O2 -w -I$ROOT/dpf/distrho -I. -DDISTRHO_PLUGIN_TARGET_STATIC=1 -o "$OUT/plugin_test" \
    tests/plugin_test.cpp BusCompPlugin.cpp $ROOT/dpf/distrho/src/DistrhoPlugin.cpp
"$OUT/plugin_test"
