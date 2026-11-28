#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink
# Writes the UI in its main states as SVG files into build/hnl-analyzer-preview/.
set -e
cd "$(dirname "$0")/.."
ROOT=../..
OUT=$ROOT/build/hnl-analyzer-preview
mkdir -p "$OUT"
g++ -std=gnu++17 -O2 -I$ROOT/tools/ui-preview -I. -o "$OUT/preview" tests/preview.cpp AnalyzerUI.cpp -lpthread
# One saved preset, so that the list looks as it does in use; the references in a folder of their own.
export HNL_PRESET_DIR="$PWD/$OUT/presets"
export HNL_REFERENCE_DIR="$PWD/$OUT/references"
rm -rf "$HNL_REFERENCE_DIR"
mkdir -p "$HNL_PRESET_DIR"
printf 'HNL preset\ntarget_lufs = -11\nmax_true_peak = -1.5\npref_plr = 9.5\n' > "$HNL_PRESET_DIR/My album.hnlpreset"
# No HNL Master but the one the "master" state makes itself (common/Link.hpp).
export HNL_LINK_NAME="hnl-preview-$$"
trap 'rm -f "/dev/shm/$HNL_LINK_NAME"' EXIT
# The classic look from an empty settings folder, as a new user has it.
export HNL_SETTINGS_DIR="$PWD/$OUT/settings"
rm -rf "$HNL_SETTINGS_DIR"
for mode in default busy reference masters live file menu help master; do "$OUT/preview" "$OUT/$mode.svg" $mode; done
# The other looks, as someone who chose one has them.
mkdir -p "$HNL_SETTINGS_DIR"
for look in hifi-black hifi-silver; do
    printf 'skin = %s\n' $look > "$HNL_SETTINGS_DIR/settings.txt"
    for mode in busy reference; do "$OUT/preview" "$OUT/$look-$mode.svg" $mode; done
done
rm -rf "$HNL_SETTINGS_DIR"
ls "$OUT"/*.svg
