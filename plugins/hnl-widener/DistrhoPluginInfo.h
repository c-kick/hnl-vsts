// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

#define DISTRHO_PLUGIN_BRAND   "HNL Design"
#define DISTRHO_PLUGIN_NAME    "HNL Widener"
#define DISTRHO_PLUGIN_URI     "https://hnldesign.nl/plugins/hnl-widener"
#define DISTRHO_PLUGIN_CLAP_ID "nl.hnldesign.widener"

#define DISTRHO_PLUGIN_BRAND_ID  HnlD
#define DISTRHO_PLUGIN_UNIQUE_ID HnWd

// The version: reported to the host, shown behind the title, and read by package.sh.
#define HNL_VERSION_MAJOR 0
#define HNL_VERSION_MINOR 3
#define HNL_VERSION_PATCH 1

#define DISTRHO_PLUGIN_HAS_UI       1
#define DISTRHO_PLUGIN_IS_RT_SAFE   1
#define DISTRHO_PLUGIN_NUM_INPUTS   2
#define DISTRHO_PLUGIN_NUM_OUTPUTS  2
#define DISTRHO_PLUGIN_WANT_LATENCY 0
#define DISTRHO_PLUGIN_WANT_STATE   1
#define DISTRHO_UI_FILE_BROWSER     0
#define DISTRHO_UI_USER_RESIZABLE   1 // dragged by its edge, or a size from the cog's list; the drawing follows the window
#define DISTRHO_UI_USE_NANOVG       1

#define DISTRHO_PLUGIN_VST3_CATEGORIES "Fx|Spatial|Stereo"
#define DISTRHO_PLUGIN_CLAP_FEATURES   "audio-effect", "stereo", "chorus"

#define UI_BASE_WIDTH  820
#define UI_BASE_HEIGHT 592

#define DISTRHO_UI_DEFAULT_WIDTH  UI_BASE_WIDTH
#define DISTRHO_UI_DEFAULT_HEIGHT UI_BASE_HEIGHT

#endif // DISTRHO_PLUGIN_INFO_H_INCLUDED
