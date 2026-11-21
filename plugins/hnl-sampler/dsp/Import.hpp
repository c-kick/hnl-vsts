// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// Instruments made elsewhere, read into a neutral description that the
// sampler's own model is then filled from:
//
//   SFZ        text with headers and opcodes, #include and #define
//   SoundFont  2 (.sf2): the presets, their zones, and each sample's data
//   Scala      .scl scales with an optional .kbm keyboard mapping
//
// These files come from strangers, so every size and index is checked
// before it is used, what is allocated is capped, numbers are read without
// regard to the C locale, and nothing throws out of the functions below.
// Header-only, standard library only.
#pragma once

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <new>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "../../../common/dsp/Safety.hpp"
#include "AudioData.hpp"

namespace hnl {
namespace sampler {

/** One mapped sound of an imported instrument. Fields a file does not set keep these defaults. */
struct ImportedZone {
    std::string sample;       // SFZ: the path as an absolute, normalised UTF-8 path (default_path and the .sfz's folder applied). SF2: empty.
    int sf2Sample = -1;       // SF2: index into the file's sample headers (shdr)
    int sf2Link = -1;         // SF2: the other channel of a stereo pair, or -1
    int pan01 = 0;            // unused, leave 0
    int loKey = 0, hiKey = 127, loVel = 0, hiVel = 127;
    int rootKey = 60;         // pitch_keycenter / overriding root key
    bool rootFromFile = true; // true when the file did not set a root: use the sample's own, else 60
    double tuneCents = 0;     // tune + transpose*100 (+ SF2 coarse/fine and the sample's pitch correction)
    double keyTrack = 100;    // cents per key (pitch_keytrack / scale tuning)
    double gainDb = 0;        // volume (SF2: minus initial attenuation)
    double pan = 0;           // -100..100
    uint64_t start = 0, end = 0;          // offset, and end as one past the last frame; end 0 = to the end of the sound
    int loopMode = -1;        // -1: as the sample file says; 0 none; 1 loop while held and through the release; 2 loop while held, then play on; 3 one shot
    uint64_t loopStart = 0, loopEnd = 0;  // loopEnd one past the last frame; both 0 = as the sample file says
    bool hasLoopPoints = false;
    double loopCrossfade = 0; // seconds
    bool reverse = false;
    int trigger = 0;          // 0 attack, 1 release, 2 first, 3 legato
    int group = 0, offBy = 0;
    int seqLength = 1, seqPosition = 1; // round robin
    double loRand = 0, hiRand = 1;
    // Amplitude envelope, seconds and percent; negative = not set by the file.
    double ampDelay = -1, ampAttack = -1, ampHold = -1, ampDecay = -1, ampSustain = -1, ampRelease = -1;
    double ampVelTrack = 100; // percent
    // Filter; cutoff 0 = no filter.
    double cutoff = 0, resonance = 0; // Hz, dB
    int filterType = 0;       // 0 lpf_2p, 1 lpf_1p, 2 hpf_2p, 3 hpf_1p, 4 bpf_2p, 5 brf_2p, 6 lpf_4p, 7 hpf_4p
    double filDelay = -1, filAttack = -1, filHold = -1, filDecay = -1, filSustain = -1, filRelease = -1, filDepth = 0; // depth in cents
    double filVelTrack = 0, filKeyTrack = 0; // cents per full velocity, cents per key
    double pitchLfoFreq = 0, pitchLfoDepth = 0; // Hz, cents
    double bendUp = 200, bendDown = -200; // cents
    int outputBus = 0;
    std::string label;        // region_label or the SF2 sample name
};

struct ImportedInstrument {
    std::string name;
    std::vector<ImportedZone> zones;
    std::vector<std::string> warnings; // short, for showing to the user: what was not taken over, each kind once with a count
};

namespace importdetail {

namespace fs = std::filesystem;

constexpr size_t kMaxZones = 4096;
constexpr size_t kMaxSfzBytes = 8u << 20;   // one .sfz file
constexpr size_t kMaxSfzTotal = 32u << 20;  // the file with everything it includes
constexpr int kMaxIncludeDepth = 16;
constexpr size_t kMaxIncludes = 256;
constexpr size_t kMaxDefines = 1024;
constexpr size_t kMaxLine = 64u << 10;      // a line after #define substitution
constexpr size_t kMaxOpcodes = 4096;        // per header, so that a long <global> cannot fill the memory
constexpr size_t kMaxWarnings = 64;
constexpr uint64_t kMaxFrames = 1ull << 40; // offsets and loop points beyond this are nonsense
constexpr uint64_t kMaxSf2Table = 64u << 20; // one table of an SF2 hydra
constexpr size_t kMaxSf2Frames = 1u << 26;  // one SF2 sample: 64 M frames, 256 MB as floats
constexpr size_t kMaxScalaBytes = 1u << 20;
constexpr int kMaxScalaNotes = 1024;

inline char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c; }

inline std::string lowered(std::string s) {
    for (char& c : s) c = lower(c);
    return s;
}

inline bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; }
inline bool isWordChar(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }

inline std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && isSpace(s[a])) ++a;
    while (b > a && isSpace(s[b - 1])) --b;
    return s.substr(a, b - a);
}

/** The text up to the first space, leading spaces skipped. */
inline std::string firstWord(const std::string& s) {
    size_t a = 0;
    while (a < s.size() && isSpace(s[a])) ++a;
    size_t b = a;
    while (b < s.size() && !isSpace(s[b])) ++b;
    return s.substr(a, b - a);
}

/**
 * Reads a number written with a point, whatever the host has set the C locale to. Spaces around it
 * are allowed, anything else after it is not. False when the text is not a number, or not a finite one.
 */
inline bool parseNumber(const std::string& text, double& out) {
    size_t i = 0;
    const size_t n = text.size();
    while (i < n && isSpace(text[i])) ++i;
    double sign = 1.0;
    if (i < n && (text[i] == '-' || text[i] == '+')) sign = text[i++] == '-' ? -1.0 : 1.0;
    // All digits go into one whole number, divided by the power of ten at the end, so that 701.955 is
    // the double nearest to it and not the sum of rounded tenths.
    double mantissa = 0.0;
    int digits = 0, fraction = 0;
    for (; i < n && text[i] >= '0' && text[i] <= '9'; ++i, ++digits) mantissa = mantissa * 10.0 + (text[i] - '0');
    if (i < n && text[i] == '.') {
        ++i;
        for (; i < n && text[i] >= '0' && text[i] <= '9'; ++i, ++digits, ++fraction) mantissa = mantissa * 10.0 + (text[i] - '0');
    }
    if (digits == 0 || digits > 400) return false;
    int exponent = 0;
    if (i < n && (text[i] == 'e' || text[i] == 'E')) {
        ++i;
        int esign = 1, edigits = 0;
        if (i < n && (text[i] == '-' || text[i] == '+')) esign = text[i++] == '-' ? -1 : 1;
        for (; i < n && text[i] >= '0' && text[i] <= '9'; ++i, ++edigits)
            if (exponent < 10000) exponent = exponent * 10 + (text[i] - '0');
        if (edigits == 0) return false;
        exponent *= esign;
    }
    while (i < n && isSpace(text[i])) ++i;
    if (i != n) return false;
    // Powers of ten up to 1e22 are exact doubles; taken from this table and not from std::pow, which
    // under -ffast-math is not exact, so that the plugin reads 0.3 as the same number as its tests do.
    static const double ten[23] = {1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,  1e10, 1e11,
                                   1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};
    const int power = exponent - fraction;
    double value = mantissa;
    if (power > 400 || power < -800) value = power > 0 && mantissa != 0.0 ? HUGE_VAL : 0.0;
    else if (power > 0) value = power <= 22 ? mantissa * ten[power] : mantissa * std::pow(10.0, power);
    else if (power < 0) value = power >= -22 ? mantissa / ten[-power] : mantissa / 1e22 / std::pow(10.0, -power - 22);
    value *= sign;
    if (!isFinite(value)) return false;
    out = value;
    return true;
}

inline double clampTo(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline int roundTo(double v, int lo, int hi) { return static_cast<int>(std::lround(clampTo(v, lo, hi))); }
inline uint64_t toFrames(double v) { return static_cast<uint64_t>(std::llround(clampTo(v, 0.0, static_cast<double>(kMaxFrames)))); }

/** Text from a file as it can be shown in a warning: printable ASCII only, and not too long. */
inline std::string shown(const std::string& s, size_t limit = 40) {
    std::string out;
    for (char c : s) {
        if (out.size() >= limit) {
            out += "...";
            break;
        }
        out += c >= 0x20 && c < 0x7F ? c : '?';
    }
    return out;
}

/** True when the bytes are well-formed UTF-8. */
inline bool validUtf8(const std::string& s) {
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        const size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
        if (n == 0 || i + n > s.size()) return false;
        for (size_t k = 1; k < n; ++k)
            if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return false;
        i += n;
    }
    return true;
}

/** Counts each kind of thing not taken over, so that the user sees one line per kind. */
class Tally {
public:
    void add(const std::string& what, size_t count = 1) {
        auto it = index_.find(what);
        if (it != index_.end()) {
            items_[it->second].second += count;
        } else if (items_.size() < 1024) {
            index_.emplace(what, items_.size());
            items_.emplace_back(what, count);
        }
    }
    void into(std::vector<std::string>& out) const {
        for (const auto& item : items_) {
            if (out.size() >= kMaxWarnings) {
                out.push_back("and more");
                return;
            }
            out.push_back(item.second > 1 ? item.first + " (" + std::to_string(item.second) + " times)" : item.first);
        }
    }

private:
    std::vector<std::pair<std::string, size_t>> items_;
    std::map<std::string, size_t> index_;
};

inline std::FILE* openRead(const fs::path& file) {
#ifdef _WIN32
    return _wfopen(file.c_str(), L"rb");
#else
    return std::fopen(file.c_str(), "rb");
#endif
}

inline bool seekTo(std::FILE* f, uint64_t offset) {
#ifdef _WIN32
    if (offset > static_cast<uint64_t>(LLONG_MAX)) return false;
    return _fseeki64(f, static_cast<long long>(offset), SEEK_SET) == 0;
#else
    if (offset > static_cast<uint64_t>(LONG_MAX)) return false;
    return std::fseek(f, static_cast<long>(offset), SEEK_SET) == 0;
#endif
}

/** A whole file of at most `limit` bytes. */
inline bool readFile(const fs::path& file, size_t limit, std::string& out, std::string& error) {
    std::FILE* f = openRead(file);
    if (f == nullptr) {
        error = "cannot open " + file.filename().u8string();
        return false;
    }
    out.assign(limit + 1, '\0');
    out.resize(std::fread(&out[0], 1, out.size(), f));
    std::fclose(f);
    if (out.size() > limit) {
        out.clear();
        error = "file too large: " + file.filename().u8string();
        return false;
    }
    return true;
}

inline uint32_t rd16(const uint8_t* p) { return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8); }
inline uint32_t rd32(const uint8_t* p) { return rd16(p) | (rd16(p + 2) << 16); }

// ---------------------------------------------------------------- SFZ

enum SfzOpcode {
    kOpSample, kOpLoKey, kOpHiKey, kOpKey, kOpLoVel, kOpHiVel, kOpKeyCenter, kOpKeyTrack, kOpTune, kOpTranspose, kOpVolume, kOpPan,
    kOpOffset, kOpEnd, kOpLoopMode, kOpLoopStart, kOpLoopEnd, kOpLoopCrossfade, kOpDirection, kOpTrigger, kOpGroup, kOpOffBy,
    kOpSeqLength, kOpSeqPosition, kOpLoRand, kOpHiRand, kOpAmpDelay, kOpAmpAttack, kOpAmpHold, kOpAmpDecay, kOpAmpSustain,
    kOpAmpRelease, kOpAmpVelTrack, kOpCutoff, kOpResonance, kOpFilType, kOpFilDelay, kOpFilAttack, kOpFilHold, kOpFilDecay,
    kOpFilSustain, kOpFilRelease, kOpFilDepth, kOpFilVelTrack, kOpFilKeyTrack, kOpLfoFreq, kOpLfoDepth, kOpBendUp, kOpBendDown,
    kOpOutput, kOpRegionLabel, kOpGroupLabel, kOpMasterLabel, kOpGlobalLabel
};

/** The opcode a name stands for, or -1 for one that is not taken over. */
inline int sfzOpcode(const std::string& name) {
    static const std::pair<const char*, int> table[] = {
        {"sample", kOpSample}, {"lokey", kOpLoKey}, {"hikey", kOpHiKey}, {"key", kOpKey}, {"lovel", kOpLoVel}, {"hivel", kOpHiVel},
        {"pitch_keycenter", kOpKeyCenter}, {"pitch_keytrack", kOpKeyTrack}, {"tune", kOpTune}, {"pitch", kOpTune},
        {"transpose", kOpTranspose}, {"volume", kOpVolume}, {"pan", kOpPan}, {"offset", kOpOffset}, {"end", kOpEnd},
        {"loop_mode", kOpLoopMode}, {"loopmode", kOpLoopMode}, {"loop_start", kOpLoopStart}, {"loopstart", kOpLoopStart},
        {"loop_end", kOpLoopEnd}, {"loopend", kOpLoopEnd}, {"loop_crossfade", kOpLoopCrossfade}, {"direction", kOpDirection},
        {"trigger", kOpTrigger}, {"group", kOpGroup}, {"off_by", kOpOffBy}, {"offby", kOpOffBy}, {"seq_length", kOpSeqLength},
        {"seq_position", kOpSeqPosition}, {"lorand", kOpLoRand}, {"hirand", kOpHiRand}, {"ampeg_delay", kOpAmpDelay},
        {"ampeg_attack", kOpAmpAttack}, {"ampeg_hold", kOpAmpHold}, {"ampeg_decay", kOpAmpDecay}, {"ampeg_sustain", kOpAmpSustain},
        {"ampeg_release", kOpAmpRelease}, {"amp_veltrack", kOpAmpVelTrack}, {"cutoff", kOpCutoff}, {"resonance", kOpResonance},
        {"fil_type", kOpFilType}, {"filtype", kOpFilType}, {"fileg_delay", kOpFilDelay}, {"fileg_attack", kOpFilAttack},
        {"fileg_hold", kOpFilHold}, {"fileg_decay", kOpFilDecay}, {"fileg_sustain", kOpFilSustain}, {"fileg_release", kOpFilRelease},
        {"fileg_depth", kOpFilDepth}, {"fil_veltrack", kOpFilVelTrack}, {"fil_keytrack", kOpFilKeyTrack},
        {"pitchlfo_freq", kOpLfoFreq}, {"pitchlfo_depth", kOpLfoDepth}, {"bend_up", kOpBendUp}, {"bend_down", kOpBendDown},
        {"output", kOpOutput}, {"region_label", kOpRegionLabel}, {"group_label", kOpGroupLabel}, {"master_label", kOpMasterLabel},
        {"global_label", kOpGlobalLabel},
    };
    for (const auto& entry : table)
        if (name == entry.first) return entry.second;
    return -1;
}

/** An opcode name as one kind: amp_velcurve_64 and amp_velcurve_127 are both amp_velcurve_N. */
inline std::string opcodeKind(const std::string& name) {
    std::string out;
    for (char c : name) {
        if (c >= '0' && c <= '9') {
            if (out.empty() || out.back() != 'N') out += 'N';
        } else {
            out += c;
        }
    }
    return shown(out);
}

/** A key as a number or as a note name: c4 = 60, a#3, Db2, c-1 = 0. False when it is neither. */
inline bool parseKey(const std::string& text, int& out) {
    double v = 0.0;
    if (parseNumber(text, v)) {
        out = roundTo(v, -1000, 1000);
        return true;
    }
    const std::string s = lowered(trim(text));
    static const int steps[7] = {9, 11, 0, 2, 4, 5, 7}; // a b c d e f g
    if (s.size() < 2 || s[0] < 'a' || s[0] > 'g') return false;
    int key = steps[s[0] - 'a'];
    size_t i = 1;
    if (s[i] == '#') {
        ++key;
        ++i;
    } else if (s[i] == 'b' && i + 1 < s.size()) {
        --key;
        ++i;
    }
    if (!parseNumber(s.substr(i), v) || std::floor(v) != v) return false;
    out = static_cast<int>(clampTo(v, -20.0, 20.0) + 1.0) * 12 + key;
    return true;
}

/** The settings of one region, gathered from all header levels before note_offset and the paths are applied. */
struct SfzRegion {
    ImportedZone zone;
    std::string sample;
    int loKey = 0, hiKey = 127, root = 60;
    bool rootSet = false, silent = false;
    double tune = 0.0, transpose = 0.0;
};

/** Applies one opcode to a region. A value that cannot be read leaves the setting as it was. */
inline void applySfz(SfzRegion& r, int op, const std::string& value, Tally& tally) {
    ImportedZone& z = r.zone;
    double v = 0.0;
    const bool num = parseNumber(value, v);
    const std::string word = lowered(value);
    int key = 0;
    switch (op) {
    case kOpSample: r.sample = value; break;
    case kOpLoKey: if (parseKey(value, key)) r.loKey = key; break;
    case kOpHiKey: if (parseKey(value, key)) r.hiKey = key; break;
    case kOpKey:
        if (parseKey(value, key)) {
            r.loKey = r.hiKey = r.root = key;
            r.rootSet = true;
        }
        break;
    case kOpKeyCenter:
        if (word == "sample") {
            r.rootSet = false;
        } else if (parseKey(value, key)) {
            r.root = key;
            r.rootSet = true;
        }
        break;
    case kOpLoVel: if (num) z.loVel = roundTo(v, 0, 127); break;
    case kOpHiVel: if (num) z.hiVel = roundTo(v, 0, 127); break;
    case kOpKeyTrack: if (num) z.keyTrack = clampTo(v, -1200.0, 1200.0); break;
    case kOpTune: if (num) r.tune = clampTo(v, -9600.0, 9600.0); break;
    case kOpTranspose: if (num) r.transpose = roundTo(v, -127, 127); break;
    case kOpVolume: if (num) z.gainDb = clampTo(v, -144.0, 48.0); break;
    case kOpPan: if (num) z.pan = clampTo(v, -100.0, 100.0); break;
    case kOpOffset: if (num) z.start = toFrames(v); break;
    case kOpEnd:
        // end is the last frame that plays; -1 makes the region silent.
        if (num) {
            r.silent = v < 0.0;
            z.end = r.silent ? 0 : toFrames(v) + 1;
        }
        break;
    case kOpLoopMode:
        if (word == "no_loop") z.loopMode = 0;
        else if (word == "loop_continuous") z.loopMode = 1;
        else if (word == "loop_sustain") z.loopMode = 2;
        else if (word == "one_shot") z.loopMode = 3;
        else tally.add("loop_mode=" + shown(value, 20) + " not understood");
        break;
    case kOpLoopStart:
        if (num) {
            z.loopStart = toFrames(v);
            z.hasLoopPoints = true;
        }
        break;
    case kOpLoopEnd:
        // loop_end names the last frame of the loop; the zone keeps one past it.
        if (num) {
            z.loopEnd = toFrames(v) + 1;
            z.hasLoopPoints = true;
        }
        break;
    case kOpLoopCrossfade: if (num) z.loopCrossfade = clampTo(v, 0.0, 10.0); break;
    case kOpDirection:
        if (word == "reverse") z.reverse = true;
        else if (word == "forward") z.reverse = false;
        else tally.add("direction=" + shown(value, 20) + " not understood");
        break;
    case kOpTrigger:
        if (word == "attack") z.trigger = 0;
        else if (word == "release") z.trigger = 1;
        else if (word == "first") z.trigger = 2;
        else if (word == "legato") z.trigger = 3;
        else tally.add("trigger=" + shown(value, 20) + " not taken over");
        break;
    case kOpGroup: if (num) z.group = roundTo(v, INT_MIN + 1, INT_MAX); break;
    case kOpOffBy: if (num) z.offBy = roundTo(v, INT_MIN + 1, INT_MAX); break;
    case kOpSeqLength: if (num) z.seqLength = roundTo(v, 1, 100); break;
    case kOpSeqPosition: if (num) z.seqPosition = roundTo(v, 1, 100); break;
    case kOpLoRand: if (num) z.loRand = clampTo(v, 0.0, 1.0); break;
    case kOpHiRand: if (num) z.hiRand = clampTo(v, 0.0, 1.0); break;
    case kOpAmpDelay: if (num) z.ampDelay = clampTo(v, 0.0, 100.0); break;
    case kOpAmpAttack: if (num) z.ampAttack = clampTo(v, 0.0, 100.0); break;
    case kOpAmpHold: if (num) z.ampHold = clampTo(v, 0.0, 100.0); break;
    case kOpAmpDecay: if (num) z.ampDecay = clampTo(v, 0.0, 100.0); break;
    case kOpAmpSustain: if (num) z.ampSustain = clampTo(v, 0.0, 100.0); break;
    case kOpAmpRelease: if (num) z.ampRelease = clampTo(v, 0.0, 100.0); break;
    case kOpAmpVelTrack: if (num) z.ampVelTrack = clampTo(v, -100.0, 100.0); break;
    case kOpCutoff: if (num) z.cutoff = clampTo(v, 0.0, 100000.0); break;
    case kOpResonance: if (num) z.resonance = clampTo(v, 0.0, 40.0); break;
    case kOpFilType: {
        static const char* types[8] = {"lpf_2p", "lpf_1p", "hpf_2p", "hpf_1p", "bpf_2p", "brf_2p", "lpf_4p", "hpf_4p"};
        int found = -1;
        for (int i = 0; i < 8; ++i)
            if (word == types[i]) found = i;
        if (found >= 0) z.filterType = found;
        else tally.add("fil_type=" + shown(value, 20) + " not taken over");
        break;
    }
    case kOpFilDelay: if (num) z.filDelay = clampTo(v, 0.0, 100.0); break;
    case kOpFilAttack: if (num) z.filAttack = clampTo(v, 0.0, 100.0); break;
    case kOpFilHold: if (num) z.filHold = clampTo(v, 0.0, 100.0); break;
    case kOpFilDecay: if (num) z.filDecay = clampTo(v, 0.0, 100.0); break;
    case kOpFilSustain: if (num) z.filSustain = clampTo(v, 0.0, 100.0); break;
    case kOpFilRelease: if (num) z.filRelease = clampTo(v, 0.0, 100.0); break;
    case kOpFilDepth: if (num) z.filDepth = clampTo(v, -12000.0, 12000.0); break;
    case kOpFilVelTrack: if (num) z.filVelTrack = clampTo(v, -9600.0, 9600.0); break;
    case kOpFilKeyTrack: if (num) z.filKeyTrack = clampTo(v, 0.0, 1200.0); break;
    case kOpLfoFreq: if (num) z.pitchLfoFreq = clampTo(v, 0.0, 20.0); break;
    case kOpLfoDepth: if (num) z.pitchLfoDepth = clampTo(v, -1200.0, 1200.0); break;
    case kOpBendUp: if (num) z.bendUp = clampTo(v, -9600.0, 9600.0); break;
    case kOpBendDown: if (num) z.bendDown = clampTo(v, -9600.0, 9600.0); break;
    case kOpOutput: if (num) z.outputBus = roundTo(v, 0, 1023); break;
    case kOpRegionLabel: z.label = value; break;
    default: break; // the labels of groups and the like name the instrument, not a region
    }
}

// The text with line comments and block comments turned into spaces; line breaks stay where they were.
inline std::string stripComments(const std::string& text) {
    std::string out(text);
    const size_t n = out.size();
    for (size_t i = 0; i < n;) {
        if (out[i] == '/' && i + 1 < n && out[i + 1] == '/') {
            while (i < n && out[i] != '\n') out[i++] = ' ';
        } else if (out[i] == '/' && i + 1 < n && out[i + 1] == '*') {
            out[i] = out[i + 1] = ' ';
            i += 2;
            while (i < n && !(out[i] == '*' && i + 1 < n && out[i + 1] == '/')) {
                if (out[i] != '\n') out[i] = ' ';
                ++i;
            }
            if (i < n) {
                out[i] = out[i + 1] = ' ';
                i += 2;
            }
        } else {
            ++i;
        }
    }
    return out;
}

/**
 * Where an opcode's value ends: at a header, at the next "name=" after a space, or at the end of the
 * line. So a sample path may contain spaces.
 */
inline size_t valueEnd(const std::string& s, size_t from) {
    for (size_t k = from; k < s.size(); ++k) {
        if (s[k] == '<') return k;
        if (isSpace(s[k])) {
            size_t m = k;
            while (m < s.size() && isSpace(s[m])) ++m;
            size_t e = m;
            while (e < s.size() && isWordChar(s[e])) ++e;
            if (e > m && e < s.size() && s[e] == '=') return k;
            k = m - 1;
        }
    }
    return s.size();
}

/** Reads SFZ text header by header and turns its regions into zones. */
class SfzReader {
public:
    SfzReader(const fs::path& file, ImportedInstrument& out) : out_(out) {
        std::error_code ec;
        fs::path folder = file.parent_path();
        if (folder.empty()) folder = fs::current_path(ec);
        root_ = fs::absolute(folder, ec);
        if (ec) root_ = folder;
        root_ = root_.lexically_normal();
        stem_ = file.stem().u8string();
        const fs::path self = fs::absolute(file, ec);
        if (!ec) stack_.push_back(self.lexically_normal().u8string());
    }

    void run(const std::string& text) {
        readText(text, root_, 0);
        if (level_ == kRegion) finishRegion();
        if (!globalLabel_.empty()) out_.name = globalLabel_;
        else if (!masterLabel_.empty()) out_.name = masterLabel_;
        else if (!groupLabel_.empty()) out_.name = groupLabel_;
        else out_.name = stem_;
        if (overflow_ > 0) tally_.add("regions left out: more than " + std::to_string(kMaxZones), overflow_ - 1);
        tally_.into(out_.warnings);
    }

private:
    enum Level { kNone, kControl, kGlobal, kMaster, kGroup, kRegion, kSkip };
    typedef std::vector<std::pair<int, std::string>> Opcodes;

    void readText(const std::string& raw, const fs::path& folder, int depth) {
        const std::string text = stripComments(raw);
        for (size_t start = 0; start < text.size();) {
            size_t stop = text.find('\n', start);
            if (stop == std::string::npos) stop = text.size();
            line(text.substr(start, stop - start), folder, depth);
            start = stop + 1;
        }
    }

    void line(const std::string& raw, const fs::path& folder, int depth) {
        size_t i = 0;
        while (i < raw.size() && isSpace(raw[i])) ++i;
        if (raw.compare(i, 7, "#define") == 0) {
            i += 7;
            while (i < raw.size() && isSpace(raw[i])) ++i;
            if (i >= raw.size() || raw[i] != '$') return;
            size_t e = i + 1;
            while (e < raw.size() && isWordChar(raw[e])) ++e;
            if (e == i + 1) return;
            const std::string name = raw.substr(i + 1, e - i - 1);
            if (defines_.size() < kMaxDefines || defines_.count(name)) defines_[name] = trim(substitute(raw.substr(e)));
            return;
        }
        const std::string s = substitute(raw);
        i = 0;
        while (i < s.size() && isSpace(s[i])) ++i;
        if (s.compare(i, 8, "#include") == 0) {
            const size_t a = s.find('"', i + 8);
            const size_t b = a == std::string::npos ? a : s.find('"', a + 1);
            if (b == std::string::npos) tally_.add("unreadable #include line");
            else include(s.substr(a + 1, b - a - 1), folder, depth);
            return;
        }
        tokens(s);
    }

    std::string substitute(const std::string& s) const {
        if (defines_.empty() || s.find('$') == std::string::npos) return s;
        std::string out;
        for (size_t i = 0; i < s.size() && out.size() < kMaxLine;) {
            if (s[i] != '$') {
                out += s[i++];
                continue;
            }
            size_t e = i + 1;
            while (e < s.size() && isWordChar(s[e])) ++e;
            bool found = false;
            for (size_t k = e; k > i + 1 && !found; --k) { // the longest defined name wins: $AB before $A
                auto it = defines_.find(s.substr(i + 1, k - i - 1));
                if (it != defines_.end()) {
                    out += it->second;
                    i = k;
                    found = true;
                }
            }
            if (!found) out += s[i++];
        }
        return out;
    }

    void include(const std::string& name, const fs::path& folder, int depth) {
        if (depth >= kMaxIncludeDepth) {
            tally_.add("#include nested too deeply");
            return;
        }
        if (++includes_ > kMaxIncludes) {
            tally_.add("too many #include files");
            return;
        }
        std::string turned = name;
        std::replace(turned.begin(), turned.end(), '\\', '/');
        const fs::path rel = fs::u8path(turned);
        std::error_code ec;
        fs::path file = rel.is_absolute() ? rel : (folder / rel).lexically_normal();
        // Relative to the file that includes it; failing that, to the .sfz that was opened.
        if (!rel.is_absolute() && !fs::is_regular_file(file, ec)) file = (root_ / rel).lexically_normal();
        const std::string key = file.u8string();
        if (std::find(stack_.begin(), stack_.end(), key) != stack_.end()) {
            tally_.add("#include that includes itself ignored");
            return;
        }
        std::string text, error;
        if (!fs::is_regular_file(file, ec) || !readFile(file, kMaxSfzBytes, text, error)) {
            tally_.add("#include file not found: " + shown(turned));
            return;
        }
        if (text.size() > kMaxSfzTotal - std::min(total_, kMaxSfzTotal)) {
            tally_.add("#include files left out: too much text");
            return;
        }
        total_ += text.size();
        stack_.push_back(key);
        readText(text, file.parent_path(), depth + 1);
        stack_.pop_back();
    }

    void tokens(const std::string& s) {
        const size_t n = s.size();
        for (size_t i = 0; i < n;) {
            while (i < n && isSpace(s[i])) ++i;
            if (i >= n) break;
            if (s[i] == '<') {
                const size_t close = s.find('>', i + 1);
                if (close == std::string::npos) {
                    tally_.add("unreadable header");
                    break;
                }
                header(lowered(trim(s.substr(i + 1, close - i - 1))));
                i = close + 1;
                continue;
            }
            size_t e = i;
            while (e < n && isWordChar(s[e])) ++e;
            if (e > i && e < n && s[e] == '=') {
                const size_t stop = valueEnd(s, e + 1);
                opcode(lowered(s.substr(i, e - i)), trim(s.substr(e + 1, stop - e - 1)));
                i = stop;
                continue;
            }
            tally_.add("text that is not an opcode ignored");
            while (i < n && !isSpace(s[i]) && s[i] != '<') ++i;
        }
    }

    void header(const std::string& name) {
        if (level_ == kRegion) finishRegion();
        if (name == "control") {
            level_ = kControl;
        } else if (name == "global") {
            level_ = kGlobal;
            global_.clear();
            master_.clear();
            group_.clear();
        } else if (name == "master") {
            level_ = kMaster;
            master_.clear();
            group_.clear();
        } else if (name == "group") {
            level_ = kGroup;
            group_.clear();
        } else if (name == "region") {
            level_ = kRegion;
            region_.clear();
        } else if (name == "curve" || name == "effect" || name == "midi" || name == "sample") {
            level_ = kSkip;
            tally_.add("<" + name + "> sections not taken over");
        } else {
            level_ = kSkip;
            tally_.add("unknown header <" + shown(name, 20) + ">");
        }
    }

    void opcode(const std::string& name, const std::string& value) {
        if (level_ == kSkip) return;
        if (level_ == kNone) {
            tally_.add("opcodes before the first header ignored");
            return;
        }
        if (level_ == kControl) {
            double v = 0.0;
            if (name == "default_path") {
                defaultPath_ = value;
                std::replace(defaultPath_.begin(), defaultPath_.end(), '\\', '/');
            } else if (name == "note_offset") {
                if (parseNumber(value, v)) noteOffset_ = roundTo(v, -127, 127);
            } else if (name == "octave_offset") {
                if (parseNumber(value, v)) octaveOffset_ = roundTo(v, -10, 10);
            } else {
                tally_.add("opcode " + opcodeKind(name) + " not taken over");
            }
            return;
        }
        const int op = sfzOpcode(name);
        if (op < 0) {
            tally_.add("opcode " + opcodeKind(name) + " not taken over");
            return;
        }
        if (op == kOpGlobalLabel && globalLabel_.empty()) globalLabel_ = value;
        if (op == kOpMasterLabel && masterLabel_.empty()) masterLabel_ = value;
        if (op == kOpGroupLabel && groupLabel_.empty()) groupLabel_ = value;
        Opcodes& list = level_ == kGlobal ? global_ : level_ == kMaster ? master_ : level_ == kGroup ? group_ : region_;
        if (list.size() < kMaxOpcodes) list.emplace_back(op, value);
    }

    void finishRegion() {
        if (out_.zones.size() >= kMaxZones) {
            ++overflow_;
            return;
        }
        SfzRegion r;
        for (const Opcodes* list : {&global_, &master_, &group_, &region_})
            for (const auto& op : *list) applySfz(r, op.first, op.second, tally_);
        const std::string sample = trim(r.sample);
        if (sample.empty()) {
            tally_.add("regions without a sample left out");
            return;
        }
        if (sample[0] == '*') {
            tally_.add("generated sound sample=" + shown(sample, 20) + " left out");
            return;
        }
        if (r.hiKey < 0) {
            tally_.add("regions not played from keys (hikey=-1) left out");
            return;
        }
        if (r.silent) {
            tally_.add("silent regions (end=-1) left out");
            return;
        }
        ImportedZone z = r.zone;
        const int shift = noteOffset_ + 12 * octaveOffset_;
        z.loKey = std::clamp(r.loKey + shift, 0, 127);
        z.hiKey = std::clamp(r.hiKey + shift, 0, 127);
        if (r.rootSet) {
            z.rootKey = std::clamp(r.root + shift, 0, 127);
            z.rootFromFile = false;
        }
        if (z.loKey > z.hiKey || z.loVel > z.hiVel) {
            tally_.add("regions with an empty key or velocity range left out");
            return;
        }
        z.tuneCents = r.tune + r.transpose * 100.0;
        std::string path = defaultPath_ + sample;
        std::replace(path.begin(), path.end(), '\\', '/');
        fs::path p = fs::u8path(path);
        if (!p.is_absolute()) p = root_ / p;
        z.sample = p.lexically_normal().u8string();
        out_.zones.push_back(std::move(z));
    }

    ImportedInstrument& out_;
    Tally tally_;
    fs::path root_;
    std::string stem_, defaultPath_, globalLabel_, masterLabel_, groupLabel_;
    std::vector<std::string> stack_; // the files being read, for finding an #include that comes back to one
    std::map<std::string, std::string> defines_;
    Opcodes global_, master_, group_, region_;
    Level level_ = kNone;
    int noteOffset_ = 0, octaveOffset_ = 0;
    size_t includes_ = 0, total_ = 0, overflow_ = 0;
};

} // namespace importdetail

/** SFZ. `text` is the file's contents, `file` its own path (for relative sample paths and #include). */
inline bool parseSfz(const std::string& text, const std::filesystem::path& file, ImportedInstrument& out, std::string& error) {
    try {
        out = ImportedInstrument();
        if (text.size() > importdetail::kMaxSfzBytes) {
            error = "file too large";
            return false;
        }
        importdetail::SfzReader reader(file, out);
        reader.run(text);
        if (out.zones.empty()) {
            error = "no regions with a sample";
            return false;
        }
        return true;
    } catch (const std::bad_alloc&) {
        error = "out of memory";
    } catch (...) {
        error = "unreadable file";
    }
    out.zones.clear();
    return false;
}

inline bool readSfzFile(const std::filesystem::path& file, ImportedInstrument& out, std::string& error) {
    try {
        std::string text;
        if (!importdetail::readFile(file, importdetail::kMaxSfzBytes, text, error)) return false;
        return parseSfz(text, file, out, error);
    } catch (const std::bad_alloc&) {
        error = "out of memory";
    } catch (...) {
        error = "unreadable file";
    }
    return false;
}

// ---------------------------------------------------------------- SF2

struct Sf2Preset {
    std::string name;
    int bank = 0, program = 0;
};

/** A SoundFont's hydra and where its sample data lies in the file. Filled by openSf2; read only by the functions below. */
struct Sf2File {
    struct Preset { std::string name; int program = 0, bank = 0, bag = 0; };
    struct Bag { int gen = 0, mod = 0; };
    struct Gen { int oper = 0; uint16_t amount = 0; };
    struct Mod { uint16_t src = 0, dest = 0, amountSrc = 0, transform = 0; int amount = 0; };
    struct Inst { std::string name; int bag = 0; };
    struct Sample {
        std::string name;
        uint32_t start = 0, end = 0, loopStart = 0, loopEnd = 0, rate = 0;
        int pitch = 60, correction = 0, link = 0, type = 1;
    };

    std::string name;
    // Each table as in the file, its terminal record ("EOP", "EOI", "EOS", ...) included.
    std::vector<Preset> presets;
    std::vector<Bag> pbag, ibag;
    std::vector<Gen> pgen, igen;
    std::vector<Mod> pmod, imod;
    std::vector<Inst> insts;
    std::vector<Sample> samples;
    uint64_t smplOffset = 0, smplBytes = 0, sm24Offset = 0, sm24Bytes = 0;
    int versionMajor = 0, versionMinor = 0;
};

/** The number of samples in the file (shdr records without the terminal one). */
inline int sf2SampleCount(const Sf2File& f) { return f.samples.empty() ? 0 : static_cast<int>(f.samples.size() - 1); }

namespace importdetail {

/** Bytes from memory or from an open file, with every read checked against the size. */
struct ByteSource {
    const uint8_t* data = nullptr;
    std::FILE* file = nullptr;
    uint64_t size = 0;

    bool read(uint64_t offset, void* out, size_t n) const {
        if (offset > size || n > size - offset) return false;
        if (data != nullptr) {
            if (n > 0) std::memcpy(out, data + offset, n);
            return true;
        }
        if (file == nullptr || !seekTo(file, offset)) return false;
        return std::fread(out, 1, n, file) == n;
    }
};

/** A fixed-length name from an SF2 record, as text that can be shown. */
inline std::string sf2Name(const uint8_t* p, size_t length) {
    std::string s;
    for (size_t i = 0; i < length && p[i] != 0; ++i) s += static_cast<char>(p[i]);
    if (!validUtf8(s))
        for (char& c : s)
            if (static_cast<unsigned char>(c) >= 0x80) c = '?';
    for (char& c : s)
        if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) c = ' ';
    return trim(s);
}

/** Splits a table into records of its size, each read by `fill`. False when the size is not a whole number of records. */
template <typename T, typename Fill>
inline bool sf2Table(const std::vector<uint8_t>& d, size_t record, std::vector<T>& out, Fill fill) {
    if (d.empty() || d.size() % record != 0) return false;
    out.resize(d.size() / record);
    for (size_t i = 0; i < out.size(); ++i) fill(&d[i * record], out[i]);
    return true;
}

inline bool parseSf2(const ByteSource& src, Sf2File& out, std::string& error) {
    out = Sf2File();
    uint8_t h[12];
    if (!src.read(0, h, 12) || std::memcmp(h, "RIFF", 4) != 0 || std::memcmp(h + 8, "sfbk", 4) != 0) {
        error = "not a SoundFont 2 file";
        return false;
    }
    const uint64_t total = std::min<uint64_t>(8ull + rd32(h + 4), src.size);
    std::vector<uint8_t> tables[9];
    static const char* names[9] = {"phdr", "pbag", "pmod", "pgen", "inst", "ibag", "imod", "igen", "shdr"};
    bool found[9] = {}, haveSmpl = false;

    for (uint64_t pos = 12; pos + 8 <= total;) {
        uint8_t c[8];
        if (!src.read(pos, c, 8)) break;
        const uint64_t size = rd32(c + 4), body = pos + 8, next = body + size + (size & 1);
        uint8_t type[4];
        if (std::memcmp(c, "LIST", 4) == 0 && size >= 4 && src.read(body, type, 4)) {
            const uint64_t end = std::min(body + size, total);
            for (uint64_t sub = body + 4; sub + 8 <= end;) {
                uint8_t s[8];
                if (!src.read(sub, s, 8)) break;
                const uint64_t ssize = rd32(s + 4), sbody = sub + 8;
                const uint64_t avail = std::min(ssize, end - sbody); // a cut-off file keeps what is there
                if (std::memcmp(type, "INFO", 4) == 0) {
                    uint8_t text[256];
                    if (std::memcmp(s, "ifil", 4) == 0 && avail >= 4 && src.read(sbody, text, 4)) {
                        out.versionMajor = static_cast<int>(rd16(text));
                        out.versionMinor = static_cast<int>(rd16(text + 2));
                    } else if (std::memcmp(s, "INAM", 4) == 0) {
                        const size_t n = static_cast<size_t>(std::min<uint64_t>(avail, sizeof(text)));
                        if (src.read(sbody, text, n)) out.name = sf2Name(text, n);
                    }
                } else if (std::memcmp(type, "sdta", 4) == 0) {
                    if (std::memcmp(s, "smpl", 4) == 0) {
                        out.smplOffset = sbody;
                        out.smplBytes = avail;
                        haveSmpl = true;
                    } else if (std::memcmp(s, "sm24", 4) == 0) {
                        out.sm24Offset = sbody;
                        out.sm24Bytes = avail;
                    }
                } else if (std::memcmp(type, "pdta", 4) == 0) {
                    for (int t = 0; t < 9; ++t) {
                        if (std::memcmp(s, names[t], 4) != 0 || found[t]) continue;
                        if (avail > kMaxSf2Table) {
                            error = "SoundFont too large";
                            return false;
                        }
                        tables[t].resize(static_cast<size_t>(avail));
                        if (!src.read(sbody, tables[t].data(), tables[t].size())) tables[t].clear();
                        found[t] = true;
                    }
                }
                sub = sbody + ssize + (ssize & 1);
            }
        }
        pos = next;
    }

    using F = Sf2File;
    auto bag = [](const uint8_t* p, F::Bag& b) {
        b.gen = static_cast<int>(rd16(p));
        b.mod = static_cast<int>(rd16(p + 2));
    };
    auto gen = [](const uint8_t* p, F::Gen& g) {
        g.oper = static_cast<int>(rd16(p));
        g.amount = static_cast<uint16_t>(rd16(p + 2));
    };
    auto mod = [](const uint8_t* p, F::Mod& m) {
        m.src = static_cast<uint16_t>(rd16(p));
        m.dest = static_cast<uint16_t>(rd16(p + 2));
        m.amount = static_cast<int16_t>(rd16(p + 4));
        m.amountSrc = static_cast<uint16_t>(rd16(p + 6));
        m.transform = static_cast<uint16_t>(rd16(p + 8));
    };
    const bool ok = sf2Table(tables[0], 38, out.presets, [](const uint8_t* p, F::Preset& r) {
                        r.name = sf2Name(p, 20);
                        r.program = static_cast<int>(rd16(p + 20));
                        r.bank = static_cast<int>(rd16(p + 22));
                        r.bag = static_cast<int>(rd16(p + 24));
                    }) &&
                    sf2Table(tables[1], 4, out.pbag, bag) && sf2Table(tables[3], 4, out.pgen, gen) &&
                    sf2Table(tables[4], 22, out.insts, [](const uint8_t* p, F::Inst& r) {
                        r.name = sf2Name(p, 20);
                        r.bag = static_cast<int>(rd16(p + 20));
                    }) &&
                    sf2Table(tables[5], 4, out.ibag, bag) && sf2Table(tables[7], 4, out.igen, gen) &&
                    sf2Table(tables[8], 46, out.samples, [](const uint8_t* p, F::Sample& r) {
                        r.name = sf2Name(p, 20);
                        r.start = rd32(p + 20);
                        r.end = rd32(p + 24);
                        r.loopStart = rd32(p + 28);
                        r.loopEnd = rd32(p + 32);
                        r.rate = rd32(p + 36);
                        r.pitch = p[40];
                        r.correction = static_cast<int8_t>(p[41]);
                        r.link = static_cast<int>(rd16(p + 42));
                        r.type = static_cast<int>(rd16(p + 44));
                    });
    if (!ok) {
        error = "damaged SoundFont: a table is missing or of the wrong size";
        out = Sf2File();
        return false;
    }
    // The modulator tables only feed a warning, so a damaged one is left empty.
    if (!sf2Table(tables[2], 10, out.pmod, mod)) out.pmod.clear();
    if (!sf2Table(tables[6], 10, out.imod, mod)) out.imod.clear();
    if (out.presets.size() < 2) {
        error = "SoundFont without presets";
        out = Sf2File();
        return false;
    }
    if (!haveSmpl) {
        error = "SoundFont without sample data";
        out = Sf2File();
        return false;
    }
    if (out.name.empty()) out.name = "SoundFont";
    return true;
}

// Generator numbers of the SoundFont 2.04 specification.
enum Sf2Gen {
    kGenStartOffset = 0, kGenEndOffset = 1, kGenLoopStartOffset = 2, kGenLoopEndOffset = 3, kGenStartCoarse = 4,
    kGenVibLfoToPitch = 6, kGenFilterFc = 8, kGenFilterQ = 9, kGenModEnvToFilterFc = 11, kGenEndCoarse = 12, kGenPan = 17,
    kGenFreqVibLfo = 24, kGenDelayModEnv = 25, kGenAttackModEnv = 26, kGenHoldModEnv = 27, kGenDecayModEnv = 28,
    kGenSustainModEnv = 29, kGenReleaseModEnv = 30, kGenDelayVolEnv = 33, kGenAttackVolEnv = 34, kGenHoldVolEnv = 35,
    kGenDecayVolEnv = 36, kGenSustainVolEnv = 37, kGenReleaseVolEnv = 38, kGenInstrument = 41, kGenKeyRange = 43,
    kGenVelRange = 44, kGenLoopStartCoarse = 45, kGenKeynum = 46, kGenVelocity = 47, kGenAttenuation = 48,
    kGenLoopEndCoarse = 50, kGenCoarseTune = 51, kGenFineTune = 52, kGenSampleId = 53, kGenSampleModes = 54,
    kGenScaleTuning = 56, kGenExclusiveClass = 57, kGenRootKey = 58, kGenCount = 61
};

/** The generators of one zone: the amount as stored, and whether the zone sets it. */
struct GenSet {
    uint16_t amount[kGenCount] = {};
    bool set[kGenCount] = {};

    int value(int g, int def) const { return set[g] ? static_cast<int16_t>(amount[g]) : def; }
    /** This set with another laid over it: what the other sets wins. */
    void overlay(const GenSet& o) {
        for (int g = 0; g < kGenCount; ++g)
            if (o.set[g]) {
                set[g] = true;
                amount[g] = o.amount[g];
            }
    }
};

/**
 * The generators of a bag. True when the list ends in `last` (instrument for a preset zone, sampleID
 * for an instrument zone); generators after it are ignored, as the specification says.
 */
inline bool loadGens(const std::vector<Sf2File::Bag>& bags, size_t bag, const std::vector<Sf2File::Gen>& gens, int last, GenSet& out) {
    out = GenSet();
    if (bag + 1 >= bags.size()) return false;
    const size_t g0 = static_cast<size_t>(bags[bag].gen), g1 = static_cast<size_t>(bags[bag + 1].gen);
    if (g0 > g1 || g1 > gens.size()) return false;
    for (size_t g = g0; g < g1; ++g) {
        const int oper = gens[g].oper;
        if (oper < 0 || oper >= kGenCount) continue;
        out.set[oper] = true;
        out.amount[oper] = gens[g].amount;
        if (oper == last) return true;
    }
    return false;
}

/** True for a modulator that every SoundFont player has built in, or for an empty record. */
inline bool defaultModulator(const Sf2File::Mod& m) {
    if (m.src == 0 && m.dest == 0 && m.amount == 0) return true;
    static const uint16_t known[][3] = {{0x0502, 48, 960}, {0x0102, 8, static_cast<uint16_t>(-2400)}, {0x000D, 6, 50}, {0x0081, 6, 50},
                                        {0x0587, 48, 960}, {0x028A, 17, 1000}, {0x058B, 48, 960}, {0x00DB, 16, 200}, {0x00DD, 15, 200}};
    for (const auto& k : known)
        if (m.src == k[0] && m.dest == k[1] && static_cast<uint16_t>(m.amount) == k[2]) return true;
    return m.src == 0x020E; // the pitch wheel, to the pitch
}

/** How many modulators of a bag are not built in. */
inline size_t extraModulators(const std::vector<Sf2File::Bag>& bags, size_t bag, const std::vector<Sf2File::Mod>& mods) {
    if (bag + 1 >= bags.size()) return 0;
    const size_t m0 = static_cast<size_t>(bags[bag].mod), m1 = std::min(static_cast<size_t>(bags[bag + 1].mod), mods.size());
    size_t n = 0;
    for (size_t m = m0; m < m1; ++m) n += defaultModulator(mods[m]) ? 0 : 1;
    return n;
}

/** Timecents to seconds. */
inline double timecents(int tc) { return std::pow(2.0, tc / 1200.0); }
/** Absolute cents to Hz: 0 cents is 8.176 Hz, the pitch of MIDI key 0. */
inline double absoluteCents(int c) { return 8.175798915643707 * std::pow(2.0, c / 1200.0); }

/** One zone from an instrument zone (global zone already laid under it) and its preset zone. False when it is left out. */
inline bool sf2Zone(const Sf2File& f, const GenSet& inst, const GenSet& preset, ImportedZone& z, Tally& tally) {
    auto sum = [&](int g, int def, int lo, int hi) { return std::clamp(inst.value(g, def) + preset.value(g, 0), lo, hi); };
    auto any = [&](int g) { return inst.set[g] || preset.set[g]; };
    auto range = [](const GenSet& s, int g, int& lo, int& hi) {
        lo = 0;
        hi = 127;
        if (s.set[g]) {
            lo = s.amount[g] & 0xFF;
            hi = s.amount[g] >> 8;
        }
    };
    int ilo, ihi, plo, phi, vlo, vhi, qlo, qhi;
    range(inst, kGenKeyRange, ilo, ihi);
    range(preset, kGenKeyRange, plo, phi);
    range(inst, kGenVelRange, vlo, vhi);
    range(preset, kGenVelRange, qlo, qhi);
    z.loKey = std::max(ilo, plo);
    z.hiKey = std::min(std::min(ihi, phi), 127);
    z.loVel = std::max(vlo, qlo);
    z.hiVel = std::min(std::min(vhi, qhi), 127);
    if (z.loKey > z.hiKey || z.loVel > z.hiVel) return false; // the preset does not reach this zone

    const int index = static_cast<int>(inst.amount[kGenSampleId]);
    if (index >= sf2SampleCount(f)) {
        tally.add("zones with a missing sample left out");
        return false;
    }
    const Sf2File::Sample& s = f.samples[static_cast<size_t>(index)];
    if (s.type & 0x8000) {
        tally.add("ROM samples left out");
        return false;
    }
    if (s.end <= s.start || s.end > f.smplBytes / 2 || s.end - s.start > kMaxSf2Frames) {
        tally.add("zones with an unreadable sample left out");
        return false;
    }
    const int64_t length = static_cast<int64_t>(s.end - s.start);

    z.sf2Sample = index;
    z.sf2Link = -1;
    if ((s.type & 0x0E) != 0 && s.link < sf2SampleCount(f) && s.link != index) z.sf2Link = s.link;
    z.label = s.name;

    // Sample offsets: instrument level only, the specification does not allow them in a preset.
    const int64_t startOffset = inst.value(kGenStartOffset, 0) + 32768ll * inst.value(kGenStartCoarse, 0);
    const int64_t endOffset = inst.value(kGenEndOffset, 0) + 32768ll * inst.value(kGenEndCoarse, 0);
    const int64_t start = std::clamp<int64_t>(startOffset, 0, length - 1);
    z.start = static_cast<uint64_t>(start);
    z.end = endOffset != 0 ? static_cast<uint64_t>(std::clamp<int64_t>(length + endOffset, start + 1, length)) : 0;
    const int64_t loopStart = static_cast<int64_t>(s.loopStart) - s.start + inst.value(kGenLoopStartOffset, 0) +
                              32768ll * inst.value(kGenLoopStartCoarse, 0);
    const int64_t loopEnd = static_cast<int64_t>(s.loopEnd) - s.start + inst.value(kGenLoopEndOffset, 0) +
                            32768ll * inst.value(kGenLoopEndCoarse, 0);
    if (loopStart >= 0 && loopStart < loopEnd && loopEnd <= length) { // the header's loop end is already one past its last frame
        z.loopStart = static_cast<uint64_t>(loopStart);
        z.loopEnd = static_cast<uint64_t>(loopEnd);
        z.hasLoopPoints = true;
    }
    const int modes = inst.value(kGenSampleModes, 0) & 3;
    z.loopMode = modes == 1 ? 1 : modes == 3 ? 2 : 0;

    // The root and the sample's own pitch correction are both in the zone, so the engine need not
    // look at the sample's root or tune for an SF2 zone.
    const int root = inst.value(kGenRootKey, -1);
    z.rootKey = root >= 0 && root <= 127 ? root : (s.pitch <= 127 ? s.pitch : 60);
    z.rootFromFile = false;
    z.tuneCents = 100.0 * sum(kGenCoarseTune, 0, -120, 120) + sum(kGenFineTune, 0, -99, 99) + s.correction;
    z.keyTrack = sum(kGenScaleTuning, 100, 0, 1200);

    // Initial attenuation in centibels. SoundFont players (fluidsynth among them) scale it by 0.4, as
    // E-mu's hardware did, so 960 cB comes out as 38.4 dB and not 96 dB; banks are voiced for that.
    z.gainDb = -0.04 * sum(kGenAttenuation, 0, 0, 1440);
    z.pan = sum(kGenPan, 0, -500, 500) / 5.0;
    const int exclusive = std::clamp(inst.value(kGenExclusiveClass, 0), 0, 127);
    z.group = z.offBy = exclusive;

    if (any(kGenDelayVolEnv)) z.ampDelay = timecents(sum(kGenDelayVolEnv, -12000, -12000, 5000));
    if (any(kGenAttackVolEnv)) z.ampAttack = timecents(sum(kGenAttackVolEnv, -12000, -12000, 8000));
    if (any(kGenHoldVolEnv)) z.ampHold = timecents(sum(kGenHoldVolEnv, -12000, -12000, 5000));
    if (any(kGenDecayVolEnv)) z.ampDecay = timecents(sum(kGenDecayVolEnv, -12000, -12000, 8000));
    if (any(kGenReleaseVolEnv)) z.ampRelease = timecents(sum(kGenReleaseVolEnv, -12000, -12000, 8000));
    // Sustain is how far the level falls, in centibels; the zone wants the level left, in percent.
    if (any(kGenSustainVolEnv)) z.ampSustain = 100.0 * std::pow(10.0, -sum(kGenSustainVolEnv, 0, 0, 1440) / 200.0);

    const int fc = sum(kGenFilterFc, 13500, 1500, 13500), toFc = sum(kGenModEnvToFilterFc, 0, -12000, 12000);
    if (fc < 13500 || toFc < 0) { // 13500 cents is the open filter, unless the envelope closes it
        z.cutoff = absoluteCents(fc);
        z.resonance = sum(kGenFilterQ, 0, 0, 960) / 10.0;
    }
    z.filDepth = toFc;
    if (any(kGenDelayModEnv)) z.filDelay = timecents(sum(kGenDelayModEnv, -12000, -12000, 5000));
    if (any(kGenAttackModEnv)) z.filAttack = timecents(sum(kGenAttackModEnv, -12000, -12000, 8000));
    if (any(kGenHoldModEnv)) z.filHold = timecents(sum(kGenHoldModEnv, -12000, -12000, 5000));
    if (any(kGenDecayModEnv)) z.filDecay = timecents(sum(kGenDecayModEnv, -12000, -12000, 8000));
    if (any(kGenReleaseModEnv)) z.filRelease = timecents(sum(kGenReleaseModEnv, -12000, -12000, 8000));
    // The modulation envelope's sustain is a fall in tenths of a percent.
    if (any(kGenSustainModEnv)) z.filSustain = 100.0 - sum(kGenSustainModEnv, 0, 0, 1000) / 10.0;

    const int vibrato = sum(kGenVibLfoToPitch, 0, -12000, 12000);
    if (vibrato != 0) {
        z.pitchLfoDepth = vibrato;
        z.pitchLfoFreq = absoluteCents(sum(kGenFreqVibLfo, 0, -16000, 4500));
    }

    if (any(kGenKeynum) || any(kGenVelocity)) tally.add("fixed key or velocity generators ignored");
    static const std::pair<int, const char*> unused[] = {
        {5, "modLfoToPitch"}, {7, "modEnvToPitch"}, {10, "modLfoToFilterFc"}, {13, "modLfoToVolume"}, {15, "chorusEffectsSend"},
        {16, "reverbEffectsSend"}, {21, "delayModLFO"}, {22, "freqModLFO"}, {23, "delayVibLFO"}, {31, "keynumToModEnvHold"},
        {32, "keynumToModEnvDecay"}, {39, "keynumToVolEnvHold"}, {40, "keynumToVolEnvDecay"}};
    for (const auto& u : unused)
        if (any(u.first)) tally.add(std::string("generator ") + u.second + " not taken over");
    return true;
}

inline bool readSf2Sample(const Sf2File& f, const ByteSource& src, int index, AudioData& out, std::string& error) {
    out = AudioData();
    if (index < 0 || index >= sf2SampleCount(f)) {
        error = "no such sample";
        return false;
    }
    const Sf2File::Sample& s = f.samples[static_cast<size_t>(index)];
    if (s.type & 0x8000) {
        error = "ROM sample";
        return false;
    }
    const uint64_t points = f.smplBytes / 2;
    if (s.end <= s.start || s.end > points || s.end - s.start > kMaxSf2Frames) {
        error = "sample outside the sample data";
        return false;
    }
    if (s.rate < 100 || s.rate > 1000000) {
        error = "unusable sample rate";
        return false;
    }
    const size_t n = s.end - s.start;
    // sm24 holds the low byte of every sample point; it counts from version 2.04 on.
    const bool use24 = f.sm24Bytes >= points && (f.versionMajor > 2 || (f.versionMajor == 2 && f.versionMinor >= 4));
    out.samples[0].resize(n);
    uint8_t block[8192], low[4096];
    for (size_t done = 0; done < n;) {
        const size_t count = std::min<size_t>(n - done, sizeof(low));
        if (!src.read(f.smplOffset + 2ull * (s.start + done), block, 2 * count) ||
            (use24 && !src.read(f.sm24Offset + s.start + done, low, count))) {
            out = AudioData();
            error = "sample data cut off";
            return false;
        }
        for (size_t i = 0; i < count; ++i) {
            const int high = static_cast<int16_t>(rd16(block + 2 * i));
            out.samples[0][done + i] = use24 ? static_cast<float>(high * 256 + low[i]) / 8388608.0f : static_cast<float>(high) / 32768.0f;
        }
        done += count;
    }
    out.channels = 1;
    out.sampleRate = s.rate;
    out.rootKey = s.pitch <= 127 ? s.pitch : -1;
    out.tuneCents = s.correction;
    if (s.loopStart >= s.start && s.loopStart < s.loopEnd && s.loopEnd <= s.end) {
        AudioData::Loop loop;
        loop.start = s.loopStart - s.start;
        loop.end = s.loopEnd - s.start;
        out.loops.push_back(loop);
    }
    out.bits = use24 ? 24 : 16;
    out.format = "SF2";
    return true;
}

/** Opens a file as a ByteSource; the caller closes `source.file`. */
inline bool openSource(const fs::path& file, ByteSource& source, std::string& error) {
    std::error_code ec;
    const uintmax_t size = fs::file_size(file, ec);
    source.file = ec ? nullptr : openRead(file);
    if (source.file == nullptr) {
        error = "cannot open " + file.filename().u8string();
        return false;
    }
    source.size = size;
    return true;
}

} // namespace importdetail

/** From memory; keeps no pointer to `data`. */
inline bool openSf2(const uint8_t* data, size_t size, Sf2File& out, std::string& error) {
    try {
        importdetail::ByteSource src;
        src.data = data;
        src.size = data != nullptr ? size : 0;
        return importdetail::parseSf2(src, out, error);
    } catch (const std::bad_alloc&) {
        error = "out of memory";
    } catch (...) {
        error = "unreadable file";
    }
    out = Sf2File();
    return false;
}

/** Reads the hydra only, not the samples. */
inline bool openSf2File(const std::filesystem::path& file, Sf2File& out, std::string& error) {
    importdetail::ByteSource src;
    bool ok = false;
    try {
        if (importdetail::openSource(file, src, error)) ok = importdetail::parseSf2(src, out, error);
    } catch (const std::bad_alloc&) {
        error = "out of memory";
    } catch (...) {
        error = "unreadable file";
    }
    if (src.file != nullptr) std::fclose(src.file);
    if (!ok) out = Sf2File();
    return ok;
}

inline std::vector<Sf2Preset> sf2Presets(const Sf2File& f) {
    std::vector<Sf2Preset> list;
    try {
        for (size_t i = 0; i + 1 < f.presets.size(); ++i) {
            Sf2Preset p;
            p.name = f.presets[i].name;
            p.bank = f.presets[i].bank;
            p.program = f.presets[i].program;
            list.push_back(p);
        }
    } catch (...) {
        list.clear();
    }
    return list;
}

/** The zones of one preset (an index into sf2Presets), with the SoundFont's rules for global zones and for adding preset generators. */
inline bool sf2Instrument(const Sf2File& f, int presetIndex, ImportedInstrument& out, std::string& error) {
    using namespace importdetail;
    try {
        out = ImportedInstrument();
        if (presetIndex < 0 || static_cast<size_t>(presetIndex) + 1 >= f.presets.size()) {
            error = "no such preset";
            return false;
        }
        const size_t pb0 = static_cast<size_t>(f.presets[presetIndex].bag), pb1 = static_cast<size_t>(f.presets[presetIndex + 1].bag);
        if (pb0 > pb1 || pb1 >= f.pbag.size()) {
            error = "damaged preset";
            return false;
        }
        out.name = f.presets[presetIndex].name;
        Tally tally;
        size_t modulators = 0, overflow = 0;
        GenSet presetGlobal;
        for (size_t pb = pb0; pb < pb1; ++pb) {
            GenSet pz;
            modulators += extraModulators(f.pbag, pb, f.pmod);
            if (!loadGens(f.pbag, pb, f.pgen, kGenInstrument, pz)) {
                if (pb == pb0) presetGlobal = pz; // a first zone without an instrument holds the defaults of the others
                continue;
            }
            GenSet preset = presetGlobal;
            preset.overlay(pz);
            const size_t instIndex = pz.amount[kGenInstrument];
            if (instIndex + 1 >= f.insts.size()) {
                tally.add("zones with a missing instrument left out");
                continue;
            }
            const size_t ib0 = static_cast<size_t>(f.insts[instIndex].bag), ib1 = static_cast<size_t>(f.insts[instIndex + 1].bag);
            if (ib0 > ib1 || ib1 >= f.ibag.size()) {
                tally.add("damaged instruments left out");
                continue;
            }
            GenSet instGlobal;
            for (size_t ib = ib0; ib < ib1; ++ib) {
                GenSet iz;
                modulators += extraModulators(f.ibag, ib, f.imod);
                if (!loadGens(f.ibag, ib, f.igen, kGenSampleId, iz)) {
                    if (ib == ib0) instGlobal = iz;
                    continue;
                }
                GenSet inst = instGlobal;
                inst.overlay(iz);
                ImportedZone z;
                if (!sf2Zone(f, inst, preset, z, tally)) continue;
                if (out.zones.size() >= kMaxZones) {
                    ++overflow;
                    continue;
                }
                out.zones.push_back(std::move(z));
            }
        }
        if (modulators > 0) tally.add("modulators not taken over", modulators);
        if (overflow > 0) tally.add("zones left out: more than " + std::to_string(kMaxZones), overflow);
        tally.into(out.warnings);
        if (out.zones.empty()) {
            error = "preset without playable zones";
            return false;
        }
        return true;
    } catch (const std::bad_alloc&) {
        error = "out of memory";
    } catch (...) {
        error = "unreadable preset";
    }
    out.zones.clear();
    return false;
}

/**
 * One sample (shdr index) as AudioData: mono, its own rate, the loop from its header (relative to its
 * start, end one past the last frame), rootKey and tuneCents from originalPitch and pitchCorrection.
 * The 24-bit data in sm24 is used when the file has it.
 */
inline bool sf2Sample(const Sf2File& f, const uint8_t* data, size_t size, int sampleIndex, AudioData& out, std::string& error) {
    try {
        importdetail::ByteSource src;
        src.data = data;
        src.size = data != nullptr ? size : 0;
        return importdetail::readSf2Sample(f, src, sampleIndex, out, error);
    } catch (const std::bad_alloc&) {
        error = "out of memory";
    } catch (...) {
        error = "unreadable sample";
    }
    out = AudioData();
    return false;
}

/** The same from the file: seeks to the sample and reads only that. */
inline bool sf2SampleFromFile(const Sf2File& f, const std::filesystem::path& file, int sampleIndex, AudioData& out, std::string& error) {
    importdetail::ByteSource src;
    bool ok = false;
    try {
        if (importdetail::openSource(file, src, error)) ok = importdetail::readSf2Sample(f, src, sampleIndex, out, error);
    } catch (const std::bad_alloc&) {
        error = "out of memory";
    } catch (...) {
        error = "unreadable sample";
    }
    if (src.file != nullptr) std::fclose(src.file);
    if (!ok) out = AudioData();
    return ok;
}

// ---------------------------------------------------------------- Scala

struct Tuning {
    double cents[128] = {}; // for every MIDI key, how far it lies from equal temperament's pitch for that key, in cents
    std::string name;
};

inline Tuning equalTuning() {
    Tuning t;
    t.name = "12-tone equal temperament";
    return t;
}

namespace importdetail {

/** The lines of a text, comment lines (starting with '!') left out. */
inline std::vector<std::string> scalaLines(const std::string& text) {
    std::vector<std::string> lines;
    for (size_t start = 0; start < text.size();) {
        size_t stop = text.find('\n', start);
        if (stop == std::string::npos) stop = text.size();
        std::string line = text.substr(start, stop - start);
        start = stop + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty() && line[0] == '!') continue;
        lines.push_back(line);
    }
    return lines;
}

/** A whole number from the start of a line. */
inline bool scalaInt(const std::string& line, int lo, int hi, int& out) {
    double v = 0.0;
    if (!parseNumber(firstWord(line), v) || std::floor(v) != v || v < lo || v > hi) return false;
    out = static_cast<int>(v);
    return true;
}

/** One pitch of a .scl file, in cents: with a point it is cents, otherwise a ratio a/b or a whole number. */
inline bool scalaPitch(const std::string& line, double& cents) {
    const std::string word = firstWord(line);
    if (word.empty()) return false;
    if (word.find('.') != std::string::npos) return parseNumber(word, cents) && std::fabs(cents) <= 1e6;
    const size_t slash = word.find('/');
    double a = 0.0, b = 1.0;
    if (!parseNumber(word.substr(0, slash), a) || (slash != std::string::npos && !parseNumber(word.substr(slash + 1), b))) return false;
    if (!(a > 0.0) || !(b > 0.0)) return false;
    cents = 1200.0 * std::log2(a / b);
    return isFinite(cents) && std::fabs(cents) <= 1e6;
}

inline long floorDiv(long a, long b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

} // namespace importdetail

/** .scl text, and optionally .kbm text (empty: key 60 = 1/1 at its equal-tempered pitch, the scale mapped linearly). */
inline bool parseScala(const std::string& scl, const std::string& kbm, Tuning& out, std::string& error) {
    using namespace importdetail;
    try {
        out = Tuning();
        if (scl.size() > kMaxScalaBytes || kbm.size() > kMaxScalaBytes) {
            error = "file too large";
            return false;
        }
        const std::vector<std::string> lines = scalaLines(scl);
        int count = 0;
        if (lines.size() < 2 || !scalaInt(lines[1], 1, kMaxScalaNotes, count)) {
            error = "no number of notes in the scale";
            return false;
        }
        std::vector<double> degree; // cents of degrees 1..count; degree 0 is 1/1
        for (size_t i = 2; i < lines.size() && static_cast<int>(degree.size()) < count; ++i) {
            if (trim(lines[i]).empty()) continue;
            double cents = 0.0;
            if (!scalaPitch(lines[i], cents)) {
                error = "unreadable pitch: " + shown(trim(lines[i]), 24);
                return false;
            }
            degree.push_back(cents);
        }
        if (static_cast<int>(degree.size()) < count) {
            error = "fewer pitches than the scale says";
            return false;
        }
        const double period = degree.back();
        auto degreeCents = [&](long d) {
            const long q = floorDiv(d, count), r = d - q * count;
            return q * period + (r == 0 ? 0.0 : degree[static_cast<size_t>(r - 1)]);
        };

        int size = 0, first = 0, last = 127, middle = 60, reference = 60, octave = count;
        double frequency = 440.0 * std::pow(2.0, -9.0 / 12.0);
        std::vector<int> map; // a scale degree per key of the pattern, -1 for a key left unmapped
        if (!trim(kbm).empty()) {
            std::vector<std::string> k;
            for (const std::string& line : scalaLines(kbm))
                if (!trim(line).empty()) k.push_back(line);
            if (k.size() < 7 || !scalaInt(k[0], 0, 2048, size) || !scalaInt(k[1], 0, 127, first) || !scalaInt(k[2], 0, 127, last) ||
                !scalaInt(k[3], 0, 127, middle) || !scalaInt(k[4], 0, 127, reference) || !parseNumber(firstWord(k[5]), frequency) ||
                !(frequency > 0.0) || frequency > 1e6 || !scalaInt(k[6], 0, 1 << 20, octave)) {
                error = "unreadable keyboard mapping";
                return false;
            }
            for (int i = 0; i < size; ++i) {
                int d = -1;
                const size_t line = static_cast<size_t>(i) + 7;
                if (line < k.size() && lower(firstWord(k[line])[0]) != 'x' && !scalaInt(k[line], 0, 1 << 20, d)) d = -1;
                map.push_back(d); // missing lines count as unmapped
            }
        }
        // Cents above the middle note, or false for a key without a pitch of its own.
        const double formalOctave = degreeCents(octave);
        auto keyCents = [&](int key, double& cents) {
            const long i = key - middle;
            if (size == 0) {
                cents = degreeCents(i);
                return true;
            }
            const long q = floorDiv(i, size), r = i - q * size;
            const int d = map[static_cast<size_t>(r)];
            if (d < 0) return false;
            cents = q * formalOctave + degreeCents(d);
            return true;
        };
        double refCents = 0.0;
        if (!keyCents(reference, refCents)) {
            error = "the reference key is not mapped";
            return false;
        }
        const double base = 1200.0 * std::log2(frequency / 440.0) - refCents;
        for (int key = first; key <= last; ++key) {
            double cents = 0.0;
            if (!keyCents(key, cents)) continue; // unmapped keys keep equal temperament
            const double diff = base + cents - (key - 69) * 100.0;
            out.cents[key] = isFinite(diff) ? clampTo(diff, -1e6, 1e6) : 0.0;
        }
        out.name = trim(lines[0]);
        if (out.name.empty()) out.name = std::to_string(count) + "-note scale";
        return true;
    } catch (const std::bad_alloc&) {
        error = "out of memory";
    } catch (...) {
        error = "unreadable scale";
    }
    out = Tuning();
    return false;
}

} // namespace sampler
} // namespace hnl
