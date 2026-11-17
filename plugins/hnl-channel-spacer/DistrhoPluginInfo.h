#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

#define DISTRHO_PLUGIN_BRAND   "HNL Design"
#define DISTRHO_PLUGIN_NAME    "HNL Channel Spacer"
#define DISTRHO_PLUGIN_URI     "https://hnldesign.nl/plugins/hnl-channel-spacer"
#define DISTRHO_PLUGIN_CLAP_ID "nl.hnldesign.channel-spacer"

#define DISTRHO_PLUGIN_BRAND_ID  HnlD
#define DISTRHO_PLUGIN_UNIQUE_ID HnSp

// Inputs 1 and 2 are the signal that makes room; 3 and 4 are the sidechain: the signal it makes room for.
// The version: reported to the host, shown behind the title, and read by package.sh.
#define HNL_VERSION_MAJOR 0
#define HNL_VERSION_MINOR 2
#define HNL_VERSION_PATCH 0

#define DISTRHO_PLUGIN_HAS_UI       1
#define DISTRHO_PLUGIN_IS_RT_SAFE   1
#define DISTRHO_PLUGIN_NUM_INPUTS   4
#define DISTRHO_PLUGIN_NUM_OUTPUTS  2
#define DISTRHO_PLUGIN_WANT_LATENCY 0
#define DISTRHO_PLUGIN_WANT_STATE   1
#define DISTRHO_UI_FILE_BROWSER     0
#define DISTRHO_UI_USER_RESIZABLE   0
#define DISTRHO_UI_USE_NANOVG       1

#define DISTRHO_PLUGIN_VST3_CATEGORIES "Fx|Dynamics|EQ|Stereo"
#define DISTRHO_PLUGIN_CLAP_FEATURES   "audio-effect", "compressor", "equalizer", "stereo"

#define UI_BASE_WIDTH  820
#define UI_BASE_HEIGHT 480

#define DISTRHO_UI_DEFAULT_WIDTH  UI_BASE_WIDTH
#define DISTRHO_UI_DEFAULT_HEIGHT UI_BASE_HEIGHT

#endif // DISTRHO_PLUGIN_INFO_H_INCLUDED
