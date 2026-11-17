#!/bin/sh
# Zips the built plugins for a release: dist/<Plugin>-<version>-win64.zip holds <Plugin>.vst3 and <Plugin>.clap.
# Run ./build.sh first.
#   ./package.sh               all plugins
#   ./package.sh hnl-master    one plugin
set -e
cd "$(dirname "$0")"
[ $# -gt 0 ] || set -- $(ls plugins)
for p do
    name=$(sed -n 's/^NAME = //p' "plugins/$p/Makefile")
    version=$(sed -n 's/.*d_version(\([0-9]*\), \([0-9]*\), \([0-9]*\)).*/\1.\2.\3/p' plugins/$p/*Plugin.cpp)
    [ -n "$name" ] && [ -n "$version" ] || { echo "$p: no name or version found" >&2; exit 1; }
    for f in "dist/$name.vst3/Contents/x86_64-win/$name.vst3" "dist/$name.clap"; do
        [ -f "$f" ] || { echo "$p: $f is missing, run ./build.sh $p" >&2; exit 1; }
    done
    zip="$name-$version-win64.zip"
    rm -f "dist/$zip"
    (cd dist && python3 -m zipfile -c "$zip" "$name.vst3" "$name.clap")
    echo "dist/$zip"
done
