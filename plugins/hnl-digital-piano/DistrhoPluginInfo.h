// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

#define DISTRHO_PLUGIN_BRAND   "HNL Design"
#define DISTRHO_PLUGIN_NAME    "HNL Digital Piano"
#define DISTRHO_PLUGIN_URI     "https://hnldesign.nl/plugins/hnl-digital-piano"
#define DISTRHO_PLUGIN_CLAP_ID "nl.hnldesign.digital-piano"

#define DISTRHO_PLUGIN_BRAND_ID  HnlD
#define DISTRHO_PLUGIN_UNIQUE_ID HnPi

// The version: reported to the host, shown behind the title, and read by package.sh.
#define HNL_VERSION_MAJOR 0
#define HNL_VERSION_MINOR 9
#define HNL_VERSION_PATCH 2

#define DISTRHO_PLUGIN_HAS_UI          1
#define DISTRHO_PLUGIN_IS_RT_SAFE      1
#define DISTRHO_PLUGIN_IS_SYNTH        1
#define DISTRHO_PLUGIN_NUM_INPUTS      0
#define DISTRHO_PLUGIN_NUM_OUTPUTS     2
#define DISTRHO_PLUGIN_WANT_MIDI_INPUT 1
#define DISTRHO_PLUGIN_WANT_LATENCY    0
#define DISTRHO_PLUGIN_WANT_STATE      1
#define DISTRHO_UI_FILE_BROWSER        0
#define DISTRHO_UI_USER_RESIZABLE      0
#define DISTRHO_UI_USE_NANOVG          1

#define DISTRHO_PLUGIN_VST3_CATEGORIES "Instrument|Piano|Stereo"
#define DISTRHO_PLUGIN_CLAP_FEATURES   "instrument", "synthesizer", "stereo"

#define UI_BASE_WIDTH  820
#define UI_BASE_HEIGHT 440

#define DISTRHO_UI_DEFAULT_WIDTH  UI_BASE_WIDTH
#define DISTRHO_UI_DEFAULT_HEIGHT UI_BASE_HEIGHT

#endif // DISTRHO_PLUGIN_INFO_H_INCLUDED
