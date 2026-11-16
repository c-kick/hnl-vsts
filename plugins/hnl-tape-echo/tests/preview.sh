#!/bin/sh
# Writes the UI in its main states as SVG files into build/hnl-tape-echo-preview/.
set -e
cd "$(dirname "$0")/.."
ROOT=../..
OUT=$ROOT/build/hnl-tape-echo-preview
mkdir -p "$OUT"
g++ -std=gnu++17 -O1 -I$ROOT/tools/ui-preview -I. -o "$OUT/preview" tests/preview.cpp EchoUI.cpp
for mode in default busy help menu; do "$OUT/preview" "$OUT/$mode.svg" $mode; done
ls "$OUT"/*.svg
