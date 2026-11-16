#!/bin/sh
# Writes the UI in its main states as SVG files into build/hnl-master-preview/.
set -e
cd "$(dirname "$0")/.."
ROOT=../..
OUT=$ROOT/build/hnl-master-preview
mkdir -p "$OUT"
g++ -std=gnu++17 -O1 -I$ROOT/tools/ui-preview -I. -o "$OUT/preview" tests/preview.cpp MasterUI.cpp
for mode in default busy all limiter closed menu help; do "$OUT/preview" "$OUT/$mode.svg" $mode; done
ls "$OUT"/*.svg
