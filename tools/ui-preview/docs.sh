#!/bin/sh
# Renders the screenshots in plugins/<name>/docs/ again from the plugins' drawing code.
#   tools/ui-preview/docs.sh <playwright index.mjs>               all plugins
#   tools/ui-preview/docs.sh <playwright index.mjs> hnl-master    one plugin
# Each state that tests/preview.sh writes goes to the picture of the same name, with these exceptions:
# "busy" (the meters at work) is preview.png, and HNL Master's "all" and "limiter" are everything-on.png
# and limiter-only.png. A state without a picture in docs/ is skipped, so a new picture is added by hand once.
# Every picture shows the plugin's version behind its title: run this after changing a version number.
set -e
cd "$(dirname "$0")/../.."
pw=$1
[ -f "$pw" ] || { echo "usage: $0 <playwright index.mjs> [plugin ...]" >&2; exit 1; }
shift
[ $# -gt 0 ] || set -- $(ls plugins)
jobs=""
for p do
    "plugins/$p/tests/preview.sh" > /dev/null
    for svg in "$PWD/build/$p-preview"/*.svg; do
        state=$(basename "$svg" .svg)
        case $state in
            busy) png=preview ;;
            all) png=everything-on ;;
            limiter) png=limiter-only ;;
            *) png=$state ;;
        esac
        [ -f "plugins/$p/docs/$png.png" ] && jobs="$jobs $svg $PWD/plugins/$p/docs/$png.png"
    done
done
node tools/ui-preview/shot.mjs "$pw" $jobs
