#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink
# Runs the offline tests of the shared code and of all plugins (or the ones named) with the
# host compiler. A plugin whose tests take longer than TEST_TIMEOUT seconds counts as failed.
set -e
cd "$(dirname "$0")"
T=${TEST_TIMEOUT:-900}
echo "=== common ==="
timeout "$T" common/tests/run.sh
[ $# -gt 0 ] || set -- $(ls plugins)
for p do
    echo "=== $p ==="
    timeout "$T" "plugins/$p/tests/run.sh"
done
