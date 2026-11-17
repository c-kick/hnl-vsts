#!/bin/sh
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
g++ -std=c++17 -O2 -Wall -Wextra -o "$OUT/presets_test" tests/presets_test.cpp
"$OUT/presets_test"
g++ -std=c++17 -O3 -ffast-math -Wall -Wextra -o "$OUT/presets_test_release" tests/presets_test.cpp
"$OUT/presets_test_release"
