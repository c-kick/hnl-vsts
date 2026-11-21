#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink
# Writes the UI in its main states as SVG files into build/hnl-sampler-preview/.
set -e
cd "$(dirname "$0")/.."
ROOT=../..
OUT=$ROOT/build/hnl-sampler-preview
mkdir -p "$OUT"
g++ -std=gnu++17 -O1 -I$ROOT/tools/ui-preview -I. -o "$OUT/preview" tests/preview.cpp SamplerUI.cpp -lpthread
# A folder of its own for saved instruments, and for the files page a folder with nothing but the sounds made here.
# That folder is outside the checkout, because its path is in the pictures: they should not show where the code lives.
export HNL_PRESET_DIR="$PWD/$OUT/presets"
SOUNDS=/tmp/Sounds
rm -rf "$SOUNDS"
mkdir -p "$HNL_PRESET_DIR" "$SOUNDS"
export HOME="$SOUNDS" USERPROFILE="$SOUNDS"
for mode in busy map sample slices mod mix files help menu empty emptymap emptysample emptyfiles; do "$OUT/preview" "$OUT/$mode.svg" "$SOUNDS" $mode; done
ls "$OUT"/*.svg
