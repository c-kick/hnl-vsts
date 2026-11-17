#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink
# Writes the UI in its main states as SVG files into build/hnl-tape-master-preview/.
set -e
cd "$(dirname "$0")/.."
ROOT=../..
OUT=$ROOT/build/hnl-tape-master-preview
mkdir -p "$OUT"
g++ -std=gnu++17 -O1 -I$ROOT/tools/ui-preview -I. -o "$OUT/preview" tests/preview.cpp TapeUI.cpp
# One saved preset, so that the list and the name box look as they do in use.
export HNL_PRESET_DIR="$PWD/$OUT/presets"
mkdir -p "$HNL_PRESET_DIR"
printf 'HNL preset\n' > "$HNL_PRESET_DIR/My preset.hnlpreset"
for mode in default busy arc leds help menu save; do "$OUT/preview" "$OUT/$mode.svg" $mode; done
ls "$OUT"/*.svg
