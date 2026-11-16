#!/bin/sh
# Writes the UI in its main states as SVG files into build/hnl-channel-spacer-preview/.
set -e
cd "$(dirname "$0")/.."
ROOT=../..
OUT=$ROOT/build/hnl-channel-spacer-preview
mkdir -p "$OUT"
g++ -std=gnu++17 -O1 -I$ROOT/tools/ui-preview -I. -o "$OUT/preview" tests/preview.cpp SpacerUI.cpp
for mode in default busy help menu; do "$OUT/preview" "$OUT/$mode.svg" $mode; done
ls "$OUT"/*.svg
