// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// The instrument a sampler plays, as a document: eight parts, each with its
// zones (which sound sounds on which keys) and its settings (oscillator,
// filter, envelopes, LFOs, modulation), and a few settings for all parts.
//
// Every setting is a number in a table: the tables below give each its name,
// range and default. That one description serves the engine, the file a
// document is saved as, the knobs of the UI and the modulation matrix.
//
// A document is saved as text, one "symbol = value" per line, and only what
// differs from the default. A setting a file does not name gets its default
// and one it names that no longer exists is skipped, so documents written by
// an older or a newer version still load. No DPF, drawing or audio code here.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "../../../common/dsp/Safety.hpp"
#include "AudioData.hpp"

namespace hnl {
namespace sampler {

constexpr int kParts = 8;
constexpr int kMacros = 8;
constexpr int kOutputs = 8; // stereo pairs
constexpr int kEnvs = 3;    // 1: level, 2: filter, 3: free
constexpr int kLfos = 3;
constexpr int kSteps = 16;
constexpr int kSlots = 12;  // rows of the modulation matrix
constexpr int kMaxPartZones = 2048; // per part
constexpr int kMaxSamples = 4096;

enum Kind { kLinear, kLog, kToggle, kChoice, kInteger };

struct Info {
    std::string name, symbol, unit;
    double min, max, def;
    Kind kind;
    const char* const* choices; // for kChoice: max + 1 names
};

// ------------------------------------------------------------- choice names

static const char* const kOffOn[] = {"Off", "On"};
static const char* const kModeNames[] = {"Poly", "Mono", "Legato"};
static const char* const kWaveNames[] = {"Sine", "Triangle", "Saw", "Square", "Pulse"};
static const char* const kNoiseNames[] = {"White", "Pink"};
static const char* const kModOscNames[] = {"Off", "FM", "AM", "Ring"};
static const char* const kSatNames[] = {"Off", "Soft", "Hard", "Fold", "Tube"};
static const char* const kFilterNames[] = {"Off", "Low 12", "Low 24", "Band", "High 12", "High 24", "Notch", "Peak", "Morph"};
static const char* const kLfoWaveNames[] = {"Sine", "Triangle", "Saw", "Ramp", "Square", "Steps", "Drift"};
static const char* const kSyncNames[] = {"Off", "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/16", "1/32",
                                         "1/2 T", "1/4 T", "1/8 T", "1/16 T", "1/2 .", "1/4 .", "1/8 .", "1/16 ."};
constexpr int kSyncChoices = sizeof(kSyncNames) / sizeof(kSyncNames[0]);
/** The length of a sync choice in beats (quarter notes); 0 for Off. */
inline double syncBeats(int choice) {
    static const double kBeats[kSyncChoices] = {0, 16, 8, 4, 2, 1, 0.5, 0.25, 0.125, 4.0 / 3, 2.0 / 3, 1.0 / 3, 1.0 / 6, 3, 1.5, 0.75, 0.375};
    return choice >= 0 && choice < kSyncChoices ? kBeats[choice] : 0.0;
}
static const char* const kRetrigNames[] = {"Free", "Note"};
static const char* const kLoopNames[] = {"Off", "Forward", "Ping-pong", "Backward"};
static const char* const kTriggerNames[] = {"Attack", "Release"};
static const char* const kChannelNames[] = {"All", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16"};
static const char* const kOutputNames[] = {"1-2", "3-4", "5-6", "7-8", "9-10", "11-12", "13-14", "15-16"};

/** What can move a setting. */
enum Source {
    kSrcNone, kSrcVelocity, kSrcKey, kSrcRandom, kSrcEnv1, kSrcEnv2, kSrcEnv3, kSrcLfo1, kSrcLfo2, kSrcLfo3, kSrcSteps,
    kSrcWheel, kSrcPressure, kSrcBend, kSrcTimbre, kSrcMacro1,
    kSourceCount = kSrcMacro1 + kMacros
};
static const char* const kSourceNames[kSourceCount] = {"None", "Velocity", "Key", "Random", "Env 1", "Env 2", "Env 3", "LFO 1", "LFO 2", "LFO 3", "Steps",
                                                       "Mod wheel", "Pressure", "Pitch bend", "Timbre", "Macro 1", "Macro 2", "Macro 3", "Macro 4",
                                                       "Macro 5", "Macro 6", "Macro 7", "Macro 8"};

/** What can be moved. `kDestRange` says how far a full source moves each at an amount of 100%. */
enum Dest {
    kDstNone, kDstPitch, kDstLevel, kDstPan, kDstStart, kDstLoopStart, kDstLoopLength, kDstCutoff, kDstResonance, kDstMorph, kDstDrive,
    kDstOscLevel, kDstNoiseLevel, kDstSampleLevel, kDstModAmount, kDstModRatio, kDstDetune, kDstWidth,
    kDstLfo1Rate, kDstLfo2Rate, kDstLfo3Rate, kDstSlot1, kDstSlot2, kDstSlot3, kDstSlot4,
    kDestCount
};
static const char* const kDestNames[kDestCount] = {"None", "Pitch", "Level", "Pan", "Sample start", "Loop start", "Loop length", "Cutoff", "Resonance",
                                                   "Morph", "Drive", "Osc level", "Noise level", "Sample level", "Mod amount", "Mod ratio", "Detune",
                                                   "Pulse width", "LFO 1 rate", "LFO 2 rate", "LFO 3 rate", "Row 1 amount", "Row 2 amount",
                                                   "Row 3 amount", "Row 4 amount"};

// ------------------------------------------------------------ part settings

enum EnvField { kEnvDelay, kEnvAttack, kEnvHold, kEnvDecay, kEnvSustain, kEnvRelease, kEnvCurve, kEnvFields };
enum LfoField { kLfoWave, kLfoRate, kLfoSync, kLfoFade, kLfoPhase, kLfoRetrig, kLfoFields };
enum SlotField { kSlotSource, kSlotVia, kSlotDest, kSlotAmount, kSlotFields };

enum PartParam : int {
    kPLevel, kPPan, kPTranspose, kPFine, kPBend, kPMode, kPGlide, kPVoices, kPVelocity, kPChannel, kPOutput, kPMpe, kPMute,
    kPDelaySend, kPReverbSend, kPChorusSend,
    kPStart, kPLoopStart, kPLoopLength, kPSampleLevel,
    kPOscWave, kPOscLevel, kPOscCoarse, kPOscFine, kPOscWidth, kPNoiseLevel, kPNoiseColour,
    kPModMode, kPModRatio, kPModFine, kPModAmount,
    kPUnison, kPDetune, kPSpread, kPChord1, kPChord2, kPChord3,
    kPSatType, kPDrive,
    kPFilterType, kPCutoff, kPResonance, kPMorph, kPKeyTrack, kPFilterEnv, kPFilterVel,
    kPEnv,                                 // kEnvs x kEnvFields
    kPLfo = kPEnv + kEnvs * kEnvFields,    // kLfos x kLfoFields
    kPSeqSteps = kPLfo + kLfos * kLfoFields, kPSeqSync, kPSeqSmooth,
    kPSeqValue,                            // kSteps values
    kPSlot = kPSeqValue + kSteps,          // kSlots x kSlotFields
    kPartParamCount = kPSlot + kSlots * kSlotFields
};
constexpr int envParam(int env, int field) { return kPEnv + env * kEnvFields + field; }
constexpr int lfoParam(int lfo, int field) { return kPLfo + lfo * kLfoFields + field; }
constexpr int slotParam(int slot, int field) { return kPSlot + slot * kSlotFields + field; }

inline const std::vector<Info>& partInfo() {
    static const std::vector<Info> table = [] {
        std::vector<Info> t = {
            {"Level", "level", "dB", -60, 12, 0, kLinear, nullptr},
            {"Pan", "pan", "%", -100, 100, 0, kLinear, nullptr},
            {"Transpose", "transpose", "st", -48, 48, 0, kInteger, nullptr},
            {"Fine", "fine", "cents", -100, 100, 0, kLinear, nullptr},
            {"Bend range", "bend", "st", 0, 48, 2, kInteger, nullptr},
            {"Mode", "mode", "", 0, 2, 0, kChoice, kModeNames},
            {"Glide", "glide", "ms", 1, 5000, 1, kLog, nullptr},
            {"Voices", "voices", "", 1, 64, 32, kInteger, nullptr},
            {"Velocity", "velocity", "%", 0, 100, 70, kLinear, nullptr},
            {"Channel", "channel", "", 0, 16, 0, kChoice, kChannelNames},
            {"Output", "output", "", 0, kOutputs - 1, 0, kChoice, kOutputNames},
            {"MPE", "mpe", "", 0, 1, 0, kToggle, kOffOn},
            {"Mute", "mute", "", 0, 1, 0, kToggle, kOffOn},
            {"Delay send", "delay_send", "%", 0, 100, 0, kLinear, nullptr},
            {"Reverb send", "reverb_send", "%", 0, 100, 0, kLinear, nullptr},
            {"Chorus send", "chorus_send", "%", 0, 100, 0, kLinear, nullptr},
            {"Start", "start", "%", 0, 100, 0, kLinear, nullptr},
            {"Loop start", "loop_start", "%", -100, 100, 0, kLinear, nullptr},
            {"Loop length", "loop_length", "%", 1, 100, 100, kLog, nullptr},
            {"Sample level", "sample_level", "%", 0, 100, 100, kLinear, nullptr},
            {"Osc wave", "osc_wave", "", 0, 4, 2, kChoice, kWaveNames},
            {"Osc level", "osc_level", "%", 0, 100, 0, kLinear, nullptr},
            {"Osc coarse", "osc_coarse", "st", -36, 36, 0, kInteger, nullptr},
            {"Osc fine", "osc_fine", "cents", -100, 100, 0, kLinear, nullptr},
            {"Pulse width", "osc_width", "%", 5, 95, 50, kLinear, nullptr},
            {"Noise level", "noise_level", "%", 0, 100, 0, kLinear, nullptr},
            {"Noise colour", "noise_colour", "", 0, 1, 0, kChoice, kNoiseNames},
            {"Mod osc", "mod_mode", "", 0, 3, 0, kChoice, kModOscNames},
            {"Mod ratio", "mod_ratio", "", 0.25, 16, 1, kLog, nullptr},
            {"Mod fine", "mod_fine", "cents", -100, 100, 0, kLinear, nullptr},
            {"Mod amount", "mod_amount", "%", 0, 100, 0, kLinear, nullptr},
            {"Unison", "unison", "", 1, 8, 1, kInteger, nullptr},
            {"Detune", "detune", "cents", 0, 100, 12, kLinear, nullptr},
            {"Spread", "spread", "%", 0, 100, 70, kLinear, nullptr},
            {"Chord 1", "chord1", "st", -24, 24, 0, kInteger, nullptr},
            {"Chord 2", "chord2", "st", -24, 24, 0, kInteger, nullptr},
            {"Chord 3", "chord3", "st", -24, 24, 0, kInteger, nullptr},
            {"Saturation", "sat_type", "", 0, 4, 0, kChoice, kSatNames},
            {"Drive", "drive", "dB", 0, 36, 6, kLinear, nullptr},
            {"Filter", "filter_type", "", 0, 8, 0, kChoice, kFilterNames},
            {"Cutoff", "cutoff", "Hz", 20, 20000, 20000, kLog, nullptr},
            {"Resonance", "resonance", "%", 0, 100, 10, kLinear, nullptr},
            {"Morph", "morph", "%", 0, 100, 0, kLinear, nullptr},
            {"Key track", "key_track", "%", 0, 100, 0, kLinear, nullptr},
            {"Filter env", "filter_env", "%", -100, 100, 0, kLinear, nullptr},
            {"Filter velocity", "filter_vel", "%", -100, 100, 0, kLinear, nullptr},
        };
        for (int e = 0; e < kEnvs; ++e) {
            const std::string n = "Env " + std::to_string(e + 1) + " ", s = "env" + std::to_string(e + 1) + "_";
            t.push_back({n + "delay", s + "delay", "ms", 0, 5000, 0, kLinear, nullptr});
            t.push_back({n + "attack", s + "attack", "ms", 0.1, 10000, e == 0 ? 1.0 : 5.0, kLog, nullptr});
            t.push_back({n + "hold", s + "hold", "ms", 0, 5000, 0, kLinear, nullptr});
            t.push_back({n + "decay", s + "decay", "ms", 1, 20000, 600, kLog, nullptr});
            t.push_back({n + "sustain", s + "sustain", "%", 0, 100, e == 0 ? 100.0 : 0.0, kLinear, nullptr});
            t.push_back({n + "release", s + "release", "ms", 1, 20000, e == 0 ? 80.0 : 300.0, kLog, nullptr});
            t.push_back({n + "curve", s + "curve", "%", 0, 100, 60, kLinear, nullptr});
        }
        for (int l = 0; l < kLfos; ++l) {
            const std::string n = "LFO " + std::to_string(l + 1) + " ", s = "lfo" + std::to_string(l + 1) + "_";
            t.push_back({n + "wave", s + "wave", "", 0, 6, 0, kChoice, kLfoWaveNames});
            t.push_back({n + "rate", s + "rate", "Hz", 0.02, 40, l == 0 ? 5.0 : 1.0, kLog, nullptr});
            t.push_back({n + "sync", s + "sync", "", 0, kSyncChoices - 1, 0, kChoice, kSyncNames});
            t.push_back({n + "fade", s + "fade", "ms", 0, 5000, 0, kLinear, nullptr});
            t.push_back({n + "phase", s + "phase", "deg", 0, 360, 0, kLinear, nullptr});
            t.push_back({n + "retrigger", s + "retrig", "", 0, 1, 1, kChoice, kRetrigNames});
        }
        t.push_back({"Steps", "seq_steps", "", 1, kSteps, kSteps, kInteger, nullptr});
        t.push_back({"Step length", "seq_sync", "", 1, kSyncChoices - 1, 7, kChoice, kSyncNames});
        t.push_back({"Step smooth", "seq_smooth", "%", 0, 100, 0, kLinear, nullptr});
        for (int i = 0; i < kSteps; ++i)
            t.push_back({"Step " + std::to_string(i + 1), "seq" + std::to_string(i + 1), "%", -100, 100, 0, kLinear, nullptr});
        for (int i = 0; i < kSlots; ++i) {
            const std::string n = "Row " + std::to_string(i + 1) + " ", s = "mod" + std::to_string(i + 1) + "_";
            t.push_back({n + "source", s + "source", "", 0, kSourceCount - 1, 0, kChoice, kSourceNames});
            t.push_back({n + "via", s + "via", "", 0, kSourceCount - 1, 0, kChoice, kSourceNames});
            t.push_back({n + "target", s + "dest", "", 0, kDestCount - 1, 0, kChoice, kDestNames});
            t.push_back({n + "amount", s + "amount", "%", -100, 100, 0, kLinear, nullptr});
        }
        return t;
    }();
    return table;
}

// ------------------------------------------------------------ zone settings

enum ZoneParam : int {
    kZLoKey, kZHiKey, kZLoVel, kZHiVel, kZRoot, kZTune, kZKeyTrack, kZGain, kZPan,
    kZStart, kZEnd, kZLoop, kZLoopStart, kZLoopEnd, kZCrossfade, kZLoopRelease, kZReverse, kZOneShot,
    kZTrigger, kZGroup, kZOffBy, kZSeqLength, kZSeqPosition,
    kZoneParamCount
};
/** Positions (start, end, loop) are in frames of the sound; an end of 0 means its last frame. */
constexpr double kMaxPosition = 4294967296.0;

inline const std::vector<Info>& zoneInfo() {
    static const std::vector<Info> table = {
        {"Low key", "lokey", "", 0, 127, 0, kInteger, nullptr},
        {"High key", "hikey", "", 0, 127, 127, kInteger, nullptr},
        {"Low velocity", "lovel", "", 1, 127, 1, kInteger, nullptr},
        {"High velocity", "hivel", "", 1, 127, 127, kInteger, nullptr},
        {"Root key", "root", "", 0, 127, 60, kInteger, nullptr},
        {"Tune", "tune", "cents", -1200, 1200, 0, kLinear, nullptr},
        {"Key track", "keytrack", "%", 0, 200, 100, kLinear, nullptr},
        {"Gain", "gain", "dB", -48, 24, 0, kLinear, nullptr},
        {"Pan", "pan", "%", -100, 100, 0, kLinear, nullptr},
        {"Start", "start", "", 0, kMaxPosition, 0, kInteger, nullptr},
        {"End", "end", "", 0, kMaxPosition, 0, kInteger, nullptr},
        {"Loop", "loop", "", 0, 3, 0, kChoice, kLoopNames},
        {"Loop start", "loop_start", "", 0, kMaxPosition, 0, kInteger, nullptr},
        {"Loop end", "loop_end", "", 0, kMaxPosition, 0, kInteger, nullptr},
        {"Crossfade", "crossfade", "", 0, 1048576, 0, kInteger, nullptr},
        {"Loop in release", "loop_release", "", 0, 1, 1, kToggle, kOffOn},
        {"Reverse", "reverse", "", 0, 1, 0, kToggle, kOffOn},
        {"One shot", "one_shot", "", 0, 1, 0, kToggle, kOffOn},
        {"Trigger", "trigger", "", 0, 1, 0, kChoice, kTriggerNames},
        {"Group", "group", "", 0, 64, 0, kInteger, nullptr},
        {"Off by", "off_by", "", 0, 64, 0, kInteger, nullptr},
        {"Round robin of", "seq_length", "", 1, 16, 1, kInteger, nullptr},
        {"Round robin turn", "seq_position", "", 1, 16, 1, kInteger, nullptr},
    };
    return table;
}

// ---------------------------------------------------------- shared settings

enum GlobalParam : int {
    kGDelayTime, kGDelaySync, kGDelayFeedback, kGDelayTone, kGDelayWidth, kGDelayLevel,
    kGReverbDecay, kGReverbDamp, kGReverbLevel,
    kGChorusRate, kGChorusDepth, kGChorusLevel,
    kGlobalParamCount
};

inline const std::vector<Info>& globalInfo() {
    static const std::vector<Info> table = {
        {"Delay time", "delay_time", "ms", 10, 2000, 350, kLog, nullptr},
        {"Delay sync", "delay_sync", "", 0, kSyncChoices - 1, 0, kChoice, kSyncNames},
        {"Delay feedback", "delay_feedback", "%", 0, 95, 35, kLinear, nullptr},
        {"Delay tone", "delay_tone", "Hz", 500, 20000, 6000, kLog, nullptr},
        {"Delay width", "delay_width", "%", 0, 100, 60, kLinear, nullptr},
        {"Delay level", "delay_level", "dB", -40, 6, 0, kLinear, nullptr},
        {"Reverb decay", "reverb_decay", "s", 0.2, 12, 1.8, kLog, nullptr},
        {"Reverb damping", "reverb_damp", "Hz", 1000, 16000, 6000, kLog, nullptr},
        {"Reverb level", "reverb_level", "dB", -40, 6, 0, kLinear, nullptr},
        {"Chorus rate", "chorus_rate", "Hz", 0.05, 8, 0.6, kLog, nullptr},
        {"Chorus depth", "chorus_depth", "%", 0, 100, 40, kLinear, nullptr},
        {"Chorus level", "chorus_level", "dB", -40, 6, 0, kLinear, nullptr},
    };
    return table;
}

/** A value as a setting may hold it: within its range, whole for a choice or a count, the default when it is not a number. */
inline double cleanValue(const Info& info, double value) {
    if (!isFinite(value)) return info.def;
    value = value < info.min ? info.min : (value > info.max ? info.max : value);
    return info.kind == kLinear || info.kind == kLog ? value : std::floor(value + 0.5);
}

/** Knob position 0..1 for a value, and back. */
inline double toNorm(const Info& p, double value) {
    if (p.kind == kLog) return std::log(value / p.min) / std::log(p.max / p.min);
    return (value - p.min) / (p.max - p.min);
}
inline double fromNorm(const Info& p, double norm) {
    norm = norm < 0.0 ? 0.0 : (norm > 1.0 ? 1.0 : norm);
    return cleanValue(p, p.kind == kLog ? p.min * std::pow(p.max / p.min, norm) : p.min + norm * (p.max - p.min));
}

// ----------------------------------------------------------------- document

/** A sound a document uses. `data` is null until it has been read from disk, and stays null when it cannot be. */
struct Sample {
    int id = 0;        // what zones refer to; never reused within a document
    std::string path;  // UTF-8; "<file>.sf2#<n>" names sample n of a SoundFont
    std::shared_ptr<const AudioData> data;
    bool missing = false; // reading it was tried and failed
};

struct Zone {
    int id = 0;     // stays the same while the zone lives, so that a sounding note can find it again
    int sample = 0; // Sample::id, 0 for none
    double v[kZoneParamCount];
    // Until the sound has been read: what the zone then takes from its file. Not saved.
    enum { kTakeRoot = 1, kTakeLoop = 2, kTakeRange = 4 };
    int take = 0;
    double crossfadeSeconds = 0.0; // a crossfade given in seconds, for when the sample rate is known

    Zone() {
        const std::vector<Info>& info = zoneInfo();
        for (int i = 0; i < kZoneParamCount; ++i) v[i] = info[i].def;
    }
};

struct Part {
    std::string name;
    double v[kPartParamCount];
    std::vector<Zone> zones;

    Part() {
        const std::vector<Info>& info = partInfo();
        for (int i = 0; i < kPartParamCount; ++i) v[i] = info[i].def;
    }
};

struct Instrument {
    std::string name = "Empty";
    Part parts[kParts];
    double g[kGlobalParamCount];
    std::string macroNames[kMacros];
    std::vector<Sample> samples;
    /** For every key, how far it lies from its pitch in equal temperament, in cents. */
    double tuning[128];
    std::string tuningName;
    int nextId = 1; // for samples and zones

    Instrument() {
        const std::vector<Info>& info = globalInfo();
        for (int i = 0; i < kGlobalParamCount; ++i) g[i] = info[i].def;
        for (double& t : tuning) t = 0.0;
    }

    const Sample* findSample(int id) const {
        if (id == 0) return nullptr;
        for (const Sample& s : samples)
            if (s.id == id) return &s;
        return nullptr;
    }
    /** The id of the sample with this path, added to the document when it is not in it yet. 0 when there is no room. */
    int useSample(const std::string& path) {
        for (const Sample& s : samples)
            if (s.path == path) return s.id;
        if (static_cast<int>(samples.size()) >= kMaxSamples || path.empty()) return 0;
        Sample s;
        s.id = nextId++;
        s.path = path;
        samples.push_back(s);
        return s.id;
    }
    /** Forgets the samples no zone uses. */
    void dropUnusedSamples() {
        samples.erase(std::remove_if(samples.begin(), samples.end(), [this](const Sample& s) {
            for (const Part& p : parts)
                for (const Zone& z : p.zones)
                    if (z.sample == s.id) return false;
            return true;
        }), samples.end());
    }
};

// --------------------------------------------------------------------- text

namespace text {

inline std::string number(double value) {
    char s[48];
    std::snprintf(s, sizeof(s), "%.10g", value);
    for (char* c = s; *c; ++c)
        if (*c == ',') *c = '.'; // whatever the host set the C locale to
    return s;
}

/** Reads a number written with a point or a comma. False when the text is not one. */
inline bool parse(const std::string& s, double& out) {
    size_t i = 0;
    const size_t n = s.size();
    while (i < n && (s[i] == ' ' || s[i] == '\t')) ++i;
    double sign = 1.0, value = 0.0, scale = 1.0;
    if (i < n && (s[i] == '-' || s[i] == '+')) sign = s[i++] == '-' ? -1.0 : 1.0;
    int digits = 0;
    for (; i < n && s[i] >= '0' && s[i] <= '9'; ++i, ++digits) value = value * 10.0 + (s[i] - '0');
    if (i < n && (s[i] == '.' || s[i] == ',')) {
        for (++i; i < n && s[i] >= '0' && s[i] <= '9'; ++i, ++digits) value += (s[i] - '0') * (scale *= 0.1);
    }
    if (digits == 0) return false;
    if (i < n && (s[i] == 'e' || s[i] == 'E')) {
        int esign = 1, exponent = 0, edigits = 0;
        if (++i < n && (s[i] == '-' || s[i] == '+')) esign = s[i++] == '-' ? -1 : 1;
        for (; i < n && s[i] >= '0' && s[i] <= '9' && exponent < 1000; ++i, ++edigits) exponent = exponent * 10 + (s[i] - '0');
        if (edigits == 0) return false;
        value *= std::pow(10.0, esign * exponent);
    }
    while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r')) ++i;
    if (i != n || !isFinite(sign * value)) return false;
    out = sign * value;
    return true;
}

inline std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return s.substr(a, b - a);
}

/** A name or path as one line of text: no line ends, no other control characters. */
inline std::string oneLine(const std::string& s, size_t limit) {
    std::string out;
    for (unsigned char c : s)
        if (c >= 0x20 && c != 0x7F && out.size() < limit) out += static_cast<char>(c);
    return trim(out);
}

constexpr const char* kFirstLine = "HNL Sampler instrument";
constexpr size_t kMaxText = 8u * 1024u * 1024u;
constexpr size_t kMaxName = 64, kMaxPath = 2048;

inline void writeValues(std::string& out, const char* prefix, const std::vector<Info>& info, const double* v) {
    for (size_t i = 0; i < info.size(); ++i)
        if (v[i] != info[i].def) out += std::string(prefix) + info[i].symbol + " = " + number(v[i]) + "\n";
}

inline int find(const std::vector<Info>& info, const std::string& symbol) {
    for (size_t i = 0; i < info.size(); ++i)
        if (info[i].symbol == symbol) return static_cast<int>(i);
    return -1;
}

} // namespace text

/** The document as text. Sample data is not part of it: a sample is named by its path. */
inline std::string toText(const Instrument& doc) {
    std::string out = std::string(text::kFirstLine) + "\nname = " + text::oneLine(doc.name, text::kMaxName) + "\n";
    for (int m = 0; m < kMacros; ++m)
        if (!doc.macroNames[m].empty()) out += "macro" + std::to_string(m + 1) + " = " + text::oneLine(doc.macroNames[m], text::kMaxName) + "\n";
    text::writeValues(out, "g.", globalInfo(), doc.g);
    bool tuned = false;
    for (double t : doc.tuning) tuned = tuned || t != 0.0;
    if (tuned) {
        out += "tuning_name = " + text::oneLine(doc.tuningName, text::kMaxName) + "\ntuning =";
        for (double t : doc.tuning) out += " " + text::number(t);
        out += "\n";
    }
    for (const Sample& s : doc.samples) out += "sample " + std::to_string(s.id) + " = " + text::oneLine(s.path, text::kMaxPath) + "\n";
    for (int p = 0; p < kParts; ++p) {
        const Part& part = doc.parts[p];
        std::string body;
        if (!part.name.empty()) body += "part_name = " + text::oneLine(part.name, text::kMaxName) + "\n";
        text::writeValues(body, "p.", partInfo(), part.v);
        for (const Zone& z : part.zones) {
            body += "zone\nz.sample = " + std::to_string(z.sample) + "\n";
            text::writeValues(body, "z.", zoneInfo(), z.v);
        }
        if (!body.empty()) out += "part " + std::to_string(p + 1) + "\n" + body;
    }
    return out;
}

/**
 * Reads a document from text. False when the text is not one (its first line is missing). Values are
 * brought into their ranges, lines that say nothing known are skipped, zones are put in order
 * (low key not above high key and so on) and given fresh ids.
 */
inline bool fromText(const std::string& in, Instrument& doc) {
    if (in.size() > text::kMaxText) return false;
    doc = Instrument();
    Part* part = nullptr;
    Zone* zone = nullptr;
    bool first = true;
    int highestSample = 0;
    for (size_t start = 0; start < in.size();) {
        size_t stop = in.find('\n', start);
        if (stop == std::string::npos) stop = in.size();
        const std::string line = text::trim(in.substr(start, stop - start));
        start = stop + 1;
        if (first) {
            if (line.compare(0, std::string(text::kFirstLine).size(), text::kFirstLine) != 0) return false;
            first = false;
            continue;
        }
        if (line.empty()) continue;
        double value = 0.0;
        if (line.compare(0, 5, "part ") == 0 && line.find('=') == std::string::npos) {
            part = text::parse(line.substr(5), value) && value >= 1 && value <= kParts ? &doc.parts[static_cast<int>(value) - 1] : nullptr;
            zone = nullptr;
            continue;
        }
        if (line == "zone") {
            zone = nullptr;
            if (part != nullptr && static_cast<int>(part->zones.size()) < kMaxPartZones) {
                part->zones.emplace_back();
                zone = &part->zones.back();
            }
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = text::trim(line.substr(0, eq)), rest = text::trim(line.substr(eq + 1));
        if (key == "name") {
            doc.name = text::oneLine(rest, text::kMaxName);
        } else if (key == "tuning_name") {
            doc.tuningName = text::oneLine(rest, text::kMaxName);
        } else if (key == "tuning") {
            size_t at = 0;
            for (int k = 0; k < 128 && at < rest.size(); ++k) {
                size_t end = rest.find(' ', at);
                if (end == std::string::npos) end = rest.size();
                if (text::parse(rest.substr(at, end - at), value)) doc.tuning[k] = std::max(-12800.0, std::min(12800.0, value));
                at = end + 1;
            }
        } else if (key.compare(0, 5, "macro") == 0 && text::parse(key.substr(5), value) && value >= 1 && value <= kMacros) {
            doc.macroNames[static_cast<int>(value) - 1] = text::oneLine(rest, text::kMaxName);
        } else if (key.compare(0, 7, "sample ") == 0) {
            const std::string path = text::oneLine(rest, text::kMaxPath);
            if (text::parse(key.substr(7), value) && value >= 1 && value < 1e9 && !path.empty() && static_cast<int>(doc.samples.size()) < kMaxSamples &&
                doc.findSample(static_cast<int>(value)) == nullptr) {
                Sample s;
                s.id = static_cast<int>(value);
                s.path = path;
                doc.samples.push_back(s);
                highestSample = std::max(highestSample, s.id);
            }
        } else if (key == "part_name" && part != nullptr) {
            part->name = text::oneLine(rest, text::kMaxName);
        } else if (key.compare(0, 2, "g.") == 0) {
            const int id = text::find(globalInfo(), key.substr(2));
            if (id >= 0 && text::parse(rest, value)) doc.g[id] = cleanValue(globalInfo()[id], value);
        } else if (key.compare(0, 2, "p.") == 0 && part != nullptr) {
            const int id = text::find(partInfo(), key.substr(2));
            if (id >= 0 && text::parse(rest, value)) part->v[id] = cleanValue(partInfo()[id], value);
        } else if (key == "z.sample" && zone != nullptr) {
            if (text::parse(rest, value) && value >= 0 && value < 1e9) zone->sample = static_cast<int>(value);
        } else if (key.compare(0, 2, "z.") == 0 && zone != nullptr) {
            const int id = text::find(zoneInfo(), key.substr(2));
            if (id >= 0 && text::parse(rest, value)) zone->v[id] = cleanValue(zoneInfo()[id], value);
        }
    }
    if (first) return false;
    doc.nextId = highestSample + 1;
    for (Part& p : doc.parts)
        for (Zone& z : p.zones) {
            z.id = doc.nextId++;
            if (doc.findSample(z.sample) == nullptr) z.sample = 0;
            if (z.v[kZLoKey] > z.v[kZHiKey]) std::swap(z.v[kZLoKey], z.v[kZHiKey]);
            if (z.v[kZLoVel] > z.v[kZHiVel]) std::swap(z.v[kZLoVel], z.v[kZHiVel]);
        }
    return true;
}

} // namespace sampler
} // namespace hnl
