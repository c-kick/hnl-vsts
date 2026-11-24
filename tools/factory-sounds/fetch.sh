#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink
# Fetches the recordings that the sounds of HNL Sampler are made from (plugins/hnl-sampler/factory/SOURCES.md)
# into a folder of your choice, about 80 MB:
#   tools/factory-sounds/fetch.sh ~/hnl-sampler-sources
# Needs curl. A file that is there already is not fetched again.
set -e
[ $# -eq 1 ] || { echo "use: $0 <folder>" >&2; exit 1; }
S=$1
mkdir -p "$S/drums" "$S/strings" "$S/organ"
get() { [ -s "$2" ] || curl -sfL --retry 3 -o "$2" "$1" || { echo "could not fetch $1" >&2; exit 1; }; }

D=https://github.com/sfzinstruments/virtuosity_drums/raw/master/Samples/mid
for v in 2 3 4; do for r in 1 2; do
    get "$D/kick/mid_kick_snoff_vl${v}_rr$r.flac" "$S/drums/mid_kick_snoff_vl${v}_rr$r.flac"
    get "$D/hh/mid_hh_closed_vl${v}_rr$r.flac" "$S/drums/mid_hh_closed_vl${v}_rr$r.flac"
done; done
for f in snare/mid_snare_crossstick_vl8 snare/mid_snare_crossstick_vl16 snare/mid_snare_center_vl9 snare/mid_snare_center_vl11 \
    snare/mid_snare_center_vl21 snare/mid_snare_center_vl23 snare/mid_snare_center_vl34 snare/mid_snare_center_vl36 \
    snare/mid_snare_rimshot_vl6 snare/mid_snare_rimshot_vl12 ltom/mid_ltom_center_vl6 ltom/mid_ltom_center_vl11 ltom/mid_ltom_center_vl16 \
    htom/mid_htom_center_vl6 htom/mid_htom_center_vl11 htom/mid_htom_center_vl16 hh/mid_hh_pedal_vl2_rr1 hh/mid_hh_pedal_vl3_rr1 \
    hh/mid_hh_open_vl3_rr1 hh/mid_hh_open_vl4_rr1 crash/mid_crash_crash_vl2_rr1 crash/mid_crash_crash_vl3_rr1 \
    ride/mid_ride_ride_vl2_rr1 ride/mid_ride_ride_vl3_rr1 ride/mid_ride_bell_vl3_rr1; do
    get "$D/$f.flac" "$S/drums/$(basename "$f").flac"
done

V=https://github.com/sgossner/VSCO-2-CE/raw/master/Strings
for n in C1 E1 G1 B1 D2 F2; do get "$V/Cello%20Section/susvib/susvib_${n}_v3_1.wav" "$S/strings/CelloEns_susvib_${n}_v3_1.wav"; done
for n in G2 B2 D3 F%233 A3 C4 E4 G4 B4 D5; do
    get "$V/Violin%20Section/susVib/VlnEns_susVib_${n}_v2.wav" "$S/strings/VlnEns_susVib_$(printf %s "$n" | sed 's/%23/#/')_v2.wav"
done

O=https://github.com/sgossner/VCSL/raw/master/Aerophones/Edge-blown%20Aerophones/Pipe%20Organ/Loud
for n in C1 F%231 C2 F%232 C3 F%233 C4 F%234 C5 F%235 C6; do
    get "$O/Rode_Man3Open_${n}.wav" "$S/organ/Rode_Man3Open_$(printf %s "$n" | sed 's/%23/#/').wav"
done
echo "The recordings are in $S"
