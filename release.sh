#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink
# Releases plugins: everything from the tests to the download on GitHub, in one go.
#   ./release.sh hnl-master hnl-leveller   these plugins
#   ./release.sh                           every plugin whose version has no tag yet
#   ./release.sh --dry-run [name ...]      builds and packages, and says what it would publish
#
# There is one release on GitHub, "Downloads" (tag: downloads). It always holds the current zip of every
# plugin under a name that never changes (<Plugin>-win64.zip), so the links in the READMEs stay good.
# A plugin's history is in its tags, <name>/v<version>; there is no release per version.
#
# For each plugin: tests, Windows build, zip. Then the versions next to the download links in README.md are
# brought up to date (committed and pushed as "Release: ..."), the plugins are tagged, the zips replace the old
# ones in the Downloads release and its notes (plugin, version, date) are written again.
#
# Needs: a clean working tree on main that is level with origin, zig (see build.sh) and gh, logged in.
set -e
cd "$(dirname "$0")"
REPO=https://github.com/c-kick/hnl-vsts
TAG=downloads
dry=""
[ "$1" = "--dry-run" ] && { dry=1; shift; }

version_of() { sed -n 's/^#define HNL_VERSION_[A-Z]* \([0-9]*\)$/\1/p' "plugins/$1/DistrhoPluginInfo.h" | paste -sd.; }
name_of() { sed -n 's/^NAME = //p' "plugins/$1/Makefile"; }
title_of() { sed -n 's/^#define DISTRHO_PLUGIN_NAME *"\(.*\)"$/\1/p' "plugins/$1/DistrhoPluginInfo.h"; }
# The newest version of a plugin that has a tag, or nothing.
released_of() { git tag -l "$1/v*" | sed 's|.*/v||' | sort -V | tail -1; }

if [ $# -eq 0 ]; then
    for p in $(ls plugins); do
        git rev-parse -q --verify "refs/tags/$p/v$(version_of "$p")" > /dev/null || set -- "$@" "$p"
    done
    [ $# -gt 0 ] || { echo "Nothing to release: every plugin's version has a tag."; exit 0; }
fi
for p do [ -d "plugins/$p" ] || { echo "$p: no such plugin" >&2; exit 1; }; done

if [ -z "$dry" ]; then
    [ -z "$(git status --porcelain)" ] || { echo "The working tree has changes: commit or stash them first." >&2; exit 1; }
    [ "$(git rev-parse --abbrev-ref HEAD)" = main ] || { echo "Not on main." >&2; exit 1; }
    git fetch -q origin
    [ "$(git rev-parse HEAD)" = "$(git rev-parse origin/main)" ] || { echo "main is not level with origin/main: push or pull first." >&2; exit 1; }
    for p do
        ! git rev-parse -q --verify "refs/tags/$p/v$(version_of "$p")" > /dev/null ||
            { echo "$p $(version_of "$p") has a tag already: raise HNL_VERSION_* in its DistrhoPluginInfo.h first." >&2; exit 1; }
    done
fi

echo "Releasing:"
for p do echo "  $(title_of "$p") $(version_of "$p")  (was: $(released_of "$p"))"; done

./test.sh "$@"
./build.sh "$@"
./package.sh "$@"

# The version next to each download link in README.md.
for p do
    sed -i "s|<!-- version:$p -->[^<]*<!-- /version -->|<!-- version:$p -->v$(version_of "$p")<!-- /version -->|" README.md
done

# The notes of the Downloads release: every plugin with its newest version; the ones released now count already.
notes=$(mktemp)
{
    echo "The current build of every plugin, for 64-bit Windows. Each zip holds the plugin as VST3 and as CLAP; a host"
    echo "needs only one of the two. Copy the \`.vst3\` folder to \`C:\\Program Files\\Common Files\\VST3\\\`, or the"
    echo "\`.clap\` file to \`C:\\Program Files\\Common Files\\CLAP\\\`."
    echo
    echo "| Plugin | Version | Released | Download |"
    echo "| --- | --- | --- | --- |"
    for p in $(ls plugins); do
        version=$(released_of "$p")
        date=""
        if [ -n "$version" ]; then date=$(git log -1 --format=%as "$p/v$version"); fi
        for q do
            if [ "$q" = "$p" ]; then version=$(version_of "$p"); date=$(date +%F); fi
        done
        if [ -z "$version" ]; then continue; fi
        echo "| [$(title_of "$p")]($REPO/tree/main/plugins/$p) | $version | $date | [$(name_of "$p")-win64.zip]($REPO/releases/download/$TAG/$(name_of "$p")-win64.zip) |"
    done
    echo
    echo "Older versions are not kept as downloads; every version has a tag (\`<plugin>/v<version>\`) to build it from."
} > "$notes"

if [ -n "$dry" ]; then
    echo "--- dry run: nothing committed, tagged, pushed or uploaded. README.md may have new versions in it. ---"
    for p do echo "would tag $p/v$(version_of "$p") and upload dist/$(name_of "$p")-win64.zip"; done
    echo "--- notes of the Downloads release ---"
    cat "$notes"
    rm -f "$notes"
    exit 0
fi

if [ -n "$(git status --porcelain README.md)" ]; then
    git add README.md
    git commit -q -m "Release: $(for p do printf '%s %s, ' "$(title_of "$p")" "$(version_of "$p")"; done | sed 's/, $//')"
    git push -q origin main
fi
for p do git tag "$p/v$(version_of "$p")"; done
git tag -f "$TAG" > /dev/null # the Downloads release goes with the newest code
git push -q origin $(for p do printf 'refs/tags/%s/v%s ' "$p" "$(version_of "$p")"; done)
git push -q -f origin "refs/tags/$TAG"

gh release view "$TAG" > /dev/null 2>&1 || gh release create "$TAG" --title "Downloads" --notes-file "$notes" --latest
gh release upload "$TAG" --clobber $(for p do printf 'dist/%s-win64.zip ' "$(name_of "$p")"; done)
gh release edit "$TAG" --notes-file "$notes" --latest > /dev/null
rm -f "$notes"
echo "Released. $REPO/releases/tag/$TAG"
