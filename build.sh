#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink
# Cross-compiles the plugins as Windows VST3 and CLAP with zig. Results land in dist/.
#   ./build.sh               all plugins
#   ./build.sh hnl-master    one plugin
set -e
cd "$(dirname "$0")"
T="$PWD/toolchain"
[ $# -gt 0 ] || set -- $(ls plugins)
for p do
    make -C "plugins/$p" -j2 WINDOWS=true PKG_CONFIG=false CC="$T/zcc" CXX="$T/zc++" AR="$T/zar"
done
rm -f dist/*.vst3/Contents/x86_64-win/*.lib dist/*.lib
