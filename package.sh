#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink
# Zips the built plugins for the Downloads release: dist/<Plugin>-win64.zip holds <Plugin>.vst3, <Plugin>.clap
# and the licence (LICENSE.txt).
# The name carries no version, so that the link to a plugin's download never changes; release.sh uploads them.
# Run ./build.sh first.
#   ./package.sh               all plugins
#   ./package.sh hnl-master    one plugin
set -e
cd "$(dirname "$0")"
[ $# -gt 0 ] || set -- $(ls plugins)
for p do
    name=$(sed -n 's/^NAME = //p' "plugins/$p/Makefile")
    version=$(sed -n 's/^#define HNL_VERSION_[A-Z]* \([0-9]*\)$/\1/p' "plugins/$p/DistrhoPluginInfo.h" | paste -sd.)
    [ -n "$name" ] && echo "$version" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$' || { echo "$p: no name or version found" >&2; exit 1; }
    for f in "dist/$name.vst3/Contents/x86_64-win/$name.vst3" "dist/$name.clap"; do
        [ -f "$f" ] || { echo "$p: $f is missing, run ./build.sh $p" >&2; exit 1; }
    done
    zip="$name-win64.zip"
    rm -f "dist/$zip"
    cp LICENSE dist/LICENSE.txt
    (cd dist && python3 -m zipfile -c "$zip" "$name.vst3" "$name.clap" LICENSE.txt)
    echo "dist/$zip ($version)"
done
