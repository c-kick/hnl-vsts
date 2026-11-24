#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink
# Makes the sounds and instruments that come with HNL Sampler again from the recordings (see fetch.sh):
#   tools/factory-sounds/make.sh ~/hnl-sampler-sources
# Writes plugins/hnl-sampler/factory/ and plugins/hnl-sampler/FactoryData.cpp. The result is in the
# repository, so this is only needed after a change to make.cpp or to the choice of recordings.
set -e
[ $# -eq 1 ] || { echo "use: $0 <folder with the recordings>" >&2; exit 1; }
cd "$(dirname "$0")/../.."
mkdir -p build/factory-sounds
g++ -std=c++17 -O2 -Wall -Wextra -o build/factory-sounds/make tools/factory-sounds/make.cpp -lpthread
build/factory-sounds/make "$1" plugins/hnl-sampler
