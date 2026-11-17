#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

#define DISTRHO_PLUGIN_BRAND   "HNL Design"
#define DISTRHO_PLUGIN_NAME    "HNL Master"
#define DISTRHO_PLUGIN_URI     "https://hnldesign.nl/plugins/hnl-master"
#define DISTRHO_PLUGIN_CLAP_ID "nl.hnldesign.master"

#define DISTRHO_PLUGIN_BRAND_ID  HnlD
#define DISTRHO_PLUGIN_UNIQUE_ID HnMs

// The version: reported to the host, shown behind the title, and read by package.sh.
#define HNL_VERSION_MAJOR 0
#define HNL_VERSION_MINOR 4
#define HNL_VERSION_PATCH 0

#define DISTRHO_PLUGIN_HAS_UI       1
#define DISTRHO_PLUGIN_IS_RT_SAFE   1
#define DISTRHO_PLUGIN_NUM_INPUTS   2
#define DISTRHO_PLUGIN_NUM_OUTPUTS  2
#define DISTRHO_PLUGIN_WANT_LATENCY 1
#define DISTRHO_PLUGIN_WANT_STATE   1
#define DISTRHO_UI_FILE_BROWSER     0
#define DISTRHO_UI_USER_RESIZABLE   0
#define DISTRHO_UI_USE_NANOVG       1

#define DISTRHO_PLUGIN_VST3_CATEGORIES "Fx|Mastering|Dynamics|Stereo"
#define DISTRHO_PLUGIN_CLAP_FEATURES   "audio-effect", "mastering", "compressor", "limiter", "stereo"

// Window size with the default settings. The UI resizes itself when stages
// are switched on or off (see layout() in MasterUI.cpp).
#define UI_BASE_WIDTH  1012
#define UI_BASE_HEIGHT 560

#define DISTRHO_UI_DEFAULT_WIDTH  UI_BASE_WIDTH
#define DISTRHO_UI_DEFAULT_HEIGHT UI_BASE_HEIGHT

#endif // DISTRHO_PLUGIN_INFO_H_INCLUDED
