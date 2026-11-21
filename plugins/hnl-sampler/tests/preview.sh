#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink
# Writes the UI in its main states as SVG files into build/hnl-sampler-preview/.
set -e
cd "$(dirname "$0")/.."
ROOT=../..
OUT=$ROOT/build/hnl-sampler-preview
mkdir -p "$OUT/Sounds"
g++ -std=gnu++17 -O1 -I$ROOT/tools/ui-preview -I. -o "$OUT/preview" tests/preview.cpp SamplerUI.cpp -lpthread
# A folder of its own for saved instruments, and for the files page a folder with nothing but the sounds made here.
export HNL_PRESET_DIR="$PWD/$OUT/presets"
mkdir -p "$HNL_PRESET_DIR"
export HOME="$PWD/$OUT/Sounds" USERPROFILE="$PWD/$OUT/Sounds"
for mode in busy map sample slices mod mix files help menu empty emptymap emptysample emptyfiles; do "$OUT/preview" "$OUT/$mode.svg" "$OUT/Sounds" $mode; done
ls "$OUT"/*.svg
