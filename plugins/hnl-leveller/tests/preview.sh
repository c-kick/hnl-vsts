#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink
# Writes the UI in its main states as SVG files into build/hnl-leveller-preview/.
set -e
cd "$(dirname "$0")/.."
ROOT=../..
OUT=$ROOT/build/hnl-leveller-preview
mkdir -p "$OUT"
g++ -std=gnu++17 -O1 -I$ROOT/tools/ui-preview -I. -o "$OUT/preview" tests/preview.cpp LevellerUI.cpp
# One saved preset, so that the list and the name box look as they do in use.
export HNL_PRESET_DIR="$PWD/$OUT/presets"
mkdir -p "$HNL_PRESET_DIR"
printf 'HNL preset\n' > "$HNL_PRESET_DIR/My preset.hnlpreset"
# The classic look from an empty settings folder, as a new user has it.
export HNL_SETTINGS_DIR="$PWD/$OUT/settings"
rm -rf "$HNL_SETTINGS_DIR"
for mode in default busy fet arc leds help menu save look; do "$OUT/preview" "$OUT/$mode.svg" $mode; done
# The other looks, as someone who chose one has them.
mkdir -p "$HNL_SETTINGS_DIR"
for look in hifi-black hifi-silver; do
    printf 'skin = %s\n' $look > "$HNL_SETTINGS_DIR/settings.txt"
    for mode in busy fet leds look; do "$OUT/preview" "$OUT/$look-$mode.svg" $mode; done
done
rm -rf "$HNL_SETTINGS_DIR"
ls "$OUT"/*.svg
