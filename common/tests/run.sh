#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink
# Builds and runs the tests of the shared code with the host compiler, plainly
# and with the flags the plugins are built with (DPF: -O3 -ffast-math).
set -e
cd "$(dirname "$0")/.."
OUT=../build/common-tests
mkdir -p "$OUT"
g++ -std=c++17 -O2 -Wall -Wextra -o "$OUT/safety_test" tests/safety_test.cpp
"$OUT/safety_test"
g++ -std=c++17 -O3 -ffast-math -Wall -Wextra -o "$OUT/safety_test_release" tests/safety_test.cpp
"$OUT/safety_test_release"
# And with clang, when zig is here: it builds the plugins for Windows, and with -ffast-math it removes
# tests on NaN that g++ leaves in.
ZIG=${ZIG:-$HOME/.local/opt/zig-x86_64-linux-0.17.1/zig}
if [ -x "$ZIG" ]; then
    "$ZIG" c++ -std=c++17 -O3 -ffast-math -Wall -Wextra -o "$OUT/safety_test_clang" tests/safety_test.cpp
    "$OUT/safety_test_clang"
fi
g++ -std=c++17 -O2 -Wall -Wextra -o "$OUT/presets_test" tests/presets_test.cpp
"$OUT/presets_test"
g++ -std=c++17 -O3 -ffast-math -Wall -Wextra -o "$OUT/presets_test_release" tests/presets_test.cpp
"$OUT/presets_test_release"
g++ -std=c++17 -O2 -Wall -Wextra -o "$OUT/settings_test" tests/settings_test.cpp
"$OUT/settings_test"
# How the plugins find each other, with a second process as the other plugin.
g++ -std=c++17 -O2 -Wall -Wextra -o "$OUT/link_test" tests/link_test.cpp -lpthread
"$OUT/link_test" > "$OUT/link_test.log" || { grep -v '^PASS' "$OUT/link_test.log"; exit 1; }
echo "link: $(grep -c '^PASS' "$OUT/link_test.log") checks passed"
