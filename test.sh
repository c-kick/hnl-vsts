#!/bin/sh
# Runs the offline tests of all plugins (or the ones named) with the host compiler.
set -e
cd "$(dirname "$0")"
[ $# -gt 0 ] || set -- $(ls plugins)
for p do
    echo "=== $p ==="
    "plugins/$p/tests/run.sh"
done
