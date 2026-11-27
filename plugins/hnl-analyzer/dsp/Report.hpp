// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// An analysis with its name and where it came from, and everything made of one: the comparison
// with a reference, the JSON that a song and a reference file hold, and the reports as plain
// text and Markdown. No drawing code and nothing of DPF, so that the tests and the plugin write
// the same reports. Header-only, no dependencies.
//
// The JSON has two parts. "summary" holds the figures a person reads, in decibels, and is
// enough for a reference written by hand. "data" holds what was measured as it is kept in
// memory, so that an analysis read back is the one that was written; when it is there, it counts.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "Advisor.hpp"
#include "Measure.hpp"

namespace hnl {
namespace analyzer {

/** Where an analysis came from. Track and continuous are the two modes of the meter; a file is only ever a reference. */
enum Mode { kTrackMode, kContinuousMode, kFile, kModeCount };
constexpr int kMeterModes = 2;
enum Status {
    kEmpty,    // nothing measured
    kRunning,  // being measured now
    kComplete, // measured from its start to its end
    kPartial,  // cut short: see Ended
};
enum Ended {
    kNotEnded,
    kTransportStopped, // the host's transport stopped
    kManualStop,       // the Stop key
    kSeek,             // the transport jumped while capturing
    kHostStopped,      // the host stopped running the plugin without stopping its transport
    kSwitched,         // the mode, the sample rate or the plugin's state changed under it
    kEndOfFile,
    kFileCut,          // the file ended inside a frame, or is longer than can be read
    kCancelled,
    kEndedCount
};

/** How a capture stands: plain data, handed over by the audio thread together with the Result. */
struct Capture {
    uint32_t id = 0;       // a new number for every capture
    int mode = kTrackMode, status = kEmpty, ended = kNotEnded;
    uint64_t startFrame = 0; // where in the song it began, when the host said
    bool positionKnown = false, automatic = false;
    uint32_t seeks = 0;    // jumps of the transport during a capture started by hand
};

/** What an analysis holds, when it was not measured here but read from a file someone wrote. */
enum Have { kHaveLoudness = 1, kHaveMax = 2, kHaveRange = 4, kHavePeaks = 8, kHaveRms = 16, kHaveWindows = 32, kHaveStereo = 64, kHaveSpectrum = 128, kHaveSeries = 256, kHaveAll = 511 };

struct Analysis {
    Result r;
    Capture capture;
    std::string name;   // of the track
    std::string source; // "WAV, 24 bit", "FLAC, 16 bit", or empty for what was captured
    std::string note;   // why a file could not be read to its end
    unsigned have = kHaveAll;

    bool empty() const { return capture.status == kEmpty || (r.empty() && !(have & kHaveLoudness)); }
    bool has(unsigned what) const { return !empty() && (have & what) == what; }
};

/** How the RMS figures are shown. */
struct View {
    int window = 2;   // of kRmsWindowMs
    bool sine = true; // 0 dB is a sine at full scale, not a square wave
};

inline const char* modeName(int mode) {
    static const char* const kNames[kModeCount] = {"track", "continuous", "file"};
    return kNames[mode < 0 || mode >= kModeCount ? 0 : mode];
}
inline const char* statusName(int status) {
    static const char* const kNames[4] = {"empty", "running", "complete", "partial"};
    return kNames[status < 0 || status > 3 ? 0 : status];
}
inline const char* endedName(int ended) {
    static const char* const kNames[kEndedCount] = {"", "transport stopped", "stopped by hand", "transport jumped", "host stopped processing",
                                                    "settings changed", "end of file", "file cut short", "cancelled"};
    return kNames[ended < 0 || ended >= kEndedCount ? 0 : ended];
}

/** 3:45.2 */
inline std::string clock(double seconds, bool tenths = true) {
    if (!(seconds >= 0.0)) seconds = 0.0;
    char s[40];
    if (tenths) {
        const long long t = std::llround(std::min(seconds, 1e9) * 10.0);
        std::snprintf(s, sizeof(s), "%lld:%02lld.%lld", t / 600, (t % 600) / 10, t % 10);
    } else {
        const long long t = static_cast<long long>(std::min(seconds, 1e9));
        std::snprintf(s, sizeof(s), "%lld:%02lld", t / 60, t % 60);
    }
    return s;
}

/** One line that says what an analysis is of and whether it is whole. */
inline std::string describe(const Analysis& a) {
    if (a.empty()) return "nothing measured";
    std::string out;
    if (a.capture.mode == kFile) out = a.source.empty() ? "file" : a.source;
    else out = a.capture.mode == kContinuousMode ? "continuous" : "track capture";
    char s[96];
    if (a.r.sampleRate > 0.0) {
        std::snprintf(s, sizeof(s), ", %.1f kHz %s", a.r.sampleRate / 1000.0, a.r.channels == 1 ? "mono" : "stereo");
        for (char* c = s; *c; ++c)
            if (*c == ',' && c != s) *c = '.';
        out += s;
    }
    if (a.r.frames) out += ", " + clock(a.r.seconds());
    if (a.capture.mode == kTrackMode && a.capture.positionKnown && a.capture.startFrame > 0 && a.r.sampleRate > 0.0)
        out += " from " + clock(static_cast<double>(a.capture.startFrame) / a.r.sampleRate);
    if (a.capture.status == kRunning) out += ", running";
    else if (a.capture.status == kPartial) out += std::string(", incomplete: ") + endedName(a.capture.ended);
    else if (a.capture.status == kComplete && a.capture.mode == kTrackMode && a.capture.ended != kNotEnded) out += std::string(", ") + endedName(a.capture.ended);
    if (a.capture.seeks) {
        std::snprintf(s, sizeof(s), ", %u jump%s of the transport inside", a.capture.seeks, a.capture.seeks == 1 ? "" : "s");
        out += s;
    }
    if (!a.note.empty()) out += " (" + a.note + ")";
    return out;
}

// ------------------------------------------------------------- comparison

enum Match {
    kAsMeasured,
    kMatchLoudness, // the reference shifted to the loudness of the track
    kMatchPeak,     // the reference shifted to the true peak of the track
    kMatchCount
};

/** The gain, in dB, the reference is shown with. Zero when either lacks what is to be matched. */
inline double matchGain(const Analysis& track, const Analysis& reference, int match) {
    if (match == kMatchLoudness && track.has(kHaveLoudness) && reference.has(kHaveLoudness) && hasLoudness(track.r) && hasLoudness(reference.r))
        return track.r.integrated - reference.r.integrated;
    if (match == kMatchPeak && track.has(kHavePeaks) && reference.has(kHavePeaks) && truePeakDb(track.r) > kSilence && truePeakDb(reference.r) > kSilence)
        return truePeakDb(track.r) - truePeakDb(reference.r);
    return 0.0;
}

struct Row {
    const char* name;
    const char* unit;
    int decimals;
    bool haveTrack, haveReference;
    double track, reference; // the reference with the matching gain in it
    double delta() const { return track - reference; }
};

/** The figures of a track beside those of a reference (which may be null). `gain` from matchGain(). */
inline std::vector<Row> compare(const Analysis& track, const Analysis* reference, double gain, const View& view) {
    struct Metric {
        const char* name;
        const char* unit;
        int decimals;
        unsigned need;
        bool level; // moves with gain
        double (*get)(const Analysis&, const View&);
        bool (*silent)(const Analysis&); // for what is no level: true when there is nothing to work it out from
    };
    static const auto noLoudness = [](const Analysis& a) { return !hasLoudness(a.r); };
    static const auto noSound = [](const Analysis& a) { return a.r.ch[0].sumSquares + a.r.ch[1].sumSquares <= 0.0; };
    static const Metric kMetrics[] = {
        {"LUFS-I", "LUFS", 2, kHaveLoudness, true, [](const Analysis& a, const View&) { return a.r.integrated; }, nullptr},
        {"True peak", "dBTP", 2, kHavePeaks, true, [](const Analysis& a, const View&) { return truePeakDb(a.r); }, nullptr},
        {"PLR", "dB", 2, kHaveLoudness | kHavePeaks, false, [](const Analysis& a, const View&) { return plr(a.r); }, +noLoudness},
        {"LRA", "LU", 1, kHaveRange, false, [](const Analysis& a, const View&) { return a.r.range; }, +noLoudness},
        {"Short-term max", "LUFS", 1, kHaveMax, true, [](const Analysis& a, const View&) { return a.r.shortTermMax; }, nullptr},
        {"Momentary max", "LUFS", 1, kHaveMax, true, [](const Analysis& a, const View&) { return a.r.momentaryMax; }, nullptr},
        {"RMS L", "dB", 2, kHaveRms, true, [](const Analysis& a, const View& v) { return totalRmsDb(a.r, 0, v.sine); }, nullptr},
        {"RMS R", "dB", 2, kHaveRms, true, [](const Analysis& a, const View& v) { return totalRmsDb(a.r, a.r.channels > 1 ? 1 : 0, v.sine); }, nullptr},
        {"Sample peak", "dB", 2, kHavePeaks, true, [](const Analysis& a, const View&) { return samplePeakDb(a.r); }, nullptr},
        {"Crest factor", "dB", 2, kHavePeaks | kHaveRms, false, [](const Analysis& a, const View&) { return std::max(crestDb(a.r, 0), a.r.channels > 1 ? crestDb(a.r, 1) : 0.0); }, +noSound},
        {"Correlation", "", 2, kHaveStereo, false, [](const Analysis& a, const View&) { return correlation(a.r); }, +noSound},
        {"Side - mid", "dB", 1, kHaveStereo, false, [](const Analysis& a, const View&) {
             const double m = midDb(a.r, false), s = sideDb(a.r, false);
             return m <= kSilence ? 0.0 : std::max(-99.0, (s <= kSilence ? -120.0 : s) - m);
         }, +noSound},
        {"Clipped samples", "", 0, kHavePeaks, false, [](const Analysis& a, const View&) { return static_cast<double>(clipped(a.r)); }, nullptr},
    };
    std::vector<Row> rows;
    for (const Metric& m : kMetrics) {
        Row row = {m.name, m.unit, m.decimals, track.has(m.need), reference != nullptr && reference->has(m.need), 0.0, 0.0};
        if (row.haveTrack) row.track = m.get(track, view);
        if (row.haveReference) row.reference = m.get(*reference, view) + (m.level ? gain : 0.0);
        // A level that is not there (silence) is not a figure to compare, nor is anything worked out from one.
        if (m.level && row.haveTrack && row.track <= kSilence + 1.0) row.haveTrack = false;
        if (m.level && row.haveReference && row.reference - gain <= kSilence + 1.0) row.haveReference = false;
        if (!m.level && m.silent) {
            row.haveTrack = row.haveTrack && !m.silent(track);
            row.haveReference = row.haveReference && reference != nullptr && !m.silent(*reference);
        }
        rows.push_back(row);
    }
    return rows;
}

// ------------------------------------------------------------------- JSON

namespace json {

/** A number as text with a full stop, whatever the host made of the locale. */
inline std::string number(double v, const char* format = "%.17g") {
    if (!(v == v) || v > 1e300 || v < -1e300) return "0";
    char s[40];
    std::snprintf(s, sizeof(s), format, v);
    for (char* c = s; *c; ++c)
        if (*c == ',') *c = '.';
    return s;
}

inline std::string quote(const std::string& text) {
    std::string out = "\"";
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
        else if (c == '\n') out += "\\n";
        else if (c == '\t') out += "\\t";
        else if (c < 0x20) { char s[8]; std::snprintf(s, sizeof(s), "\\u%04x", c); out += s; }
        else out += static_cast<char>(c);
    }
    return out + "\"";
}

struct Value {
    enum Type { kNull, kBool, kNumber, kString, kArray, kObject };
    Type type = kNull;
    double number = 0.0;
    bool boolean = false;
    std::string string;
    std::vector<Value> array;
    std::vector<std::pair<std::string, Value>> object;

    const Value* get(const char* key) const {
        if (type != kObject) return nullptr;
        for (const auto& member : object)
            if (member.first == key) return &member.second;
        return nullptr;
    }
    /** The number under `key`, or `fallback` when it is not there or not a number. */
    double num(const char* key, double fallback) const {
        const Value* v = get(key);
        return v != nullptr && v->type == kNumber ? v->number : fallback;
    }
    std::string str(const char* key) const {
        const Value* v = get(key);
        return v != nullptr && v->type == kString ? v->string : std::string();
    }
    bool has(const char* key) const { return get(key) != nullptr; }
};

/** Reads JSON. Never throws and never recurses deeper than kMaxDepth; anything it does not take makes parse() false. */
class Parser {
public:
    static constexpr int kMaxDepth = 12;
    static constexpr size_t kMaxSize = 8u << 20, kMaxItems = 1u << 20;

    static bool parse(const std::string& text, Value& out) {
        if (text.size() > kMaxSize) return false;
        Parser p(text);
        p.space();
        if (!p.value(out, 0)) return false;
        p.space();
        return p.at_ == text.size();
    }

private:
    explicit Parser(const std::string& text) : s_(text) {}

    void space() {
        while (at_ < s_.size() && (s_[at_] == ' ' || s_[at_] == '\n' || s_[at_] == '\r' || s_[at_] == '\t')) ++at_;
    }
    bool word(const char* w) {
        size_t n = 0;
        while (w[n]) ++n;
        if (s_.compare(at_, n, w) != 0) return false;
        at_ += n;
        return true;
    }
    bool value(Value& out, int depth) {
        if (at_ >= s_.size() || depth > kMaxDepth || ++items_ > kMaxItems) return false;
        const char c = s_[at_];
        if (c == '{') {
            out.type = Value::kObject;
            ++at_;
            space();
            if (at_ < s_.size() && s_[at_] == '}') { ++at_; return true; }
            for (;;) {
                space();
                std::string key;
                if (!text(key)) return false;
                space();
                if (at_ >= s_.size() || s_[at_++] != ':') return false;
                space();
                out.object.emplace_back(std::move(key), Value());
                if (!value(out.object.back().second, depth + 1)) return false;
                space();
                if (at_ >= s_.size()) return false;
                if (s_[at_] == ',') { ++at_; continue; }
                return s_[at_++] == '}';
            }
        }
        if (c == '[') {
            out.type = Value::kArray;
            ++at_;
            space();
            if (at_ < s_.size() && s_[at_] == ']') { ++at_; return true; }
            for (;;) {
                space();
                out.array.emplace_back();
                if (!value(out.array.back(), depth + 1)) return false;
                space();
                if (at_ >= s_.size()) return false;
                if (s_[at_] == ',') { ++at_; continue; }
                return s_[at_++] == ']';
            }
        }
        if (c == '"') {
            out.type = Value::kString;
            return text(out.string);
        }
        if (word("true")) { out.type = Value::kBool; out.boolean = true; return true; }
        if (word("false")) { out.type = Value::kBool; return true; }
        if (word("null")) return true;
        out.type = Value::kNumber;
        return numeral(out.number);
    }
    bool text(std::string& out) {
        if (at_ >= s_.size() || s_[at_] != '"') return false;
        for (++at_; at_ < s_.size(); ++at_) {
            const char c = s_[at_];
            if (c == '"') { ++at_; return true; }
            if (c != '\\') { out += c; continue; }
            if (++at_ >= s_.size()) return false;
            switch (s_[at_]) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case 'b': case 'f': break;
            case 'u': {
                if (at_ + 4 >= s_.size()) return false;
                unsigned code = 0;
                for (int i = 1; i <= 4; ++i) {
                    const char h = s_[at_ + i];
                    const int d = h >= '0' && h <= '9' ? h - '0' : (h >= 'a' && h <= 'f' ? h - 'a' + 10 : (h >= 'A' && h <= 'F' ? h - 'A' + 10 : -1));
                    if (d < 0) return false;
                    code = code * 16 + static_cast<unsigned>(d);
                }
                at_ += 4;
                if (code >= 0xD800 && code <= 0xDFFF) code = '?'; // half of a pair: no name here needs one
                if (code < 0x80) out += static_cast<char>(code);
                else if (code < 0x800) { out += static_cast<char>(0xC0 | (code >> 6)); out += static_cast<char>(0x80 | (code & 0x3F)); }
                else { out += static_cast<char>(0xE0 | (code >> 12)); out += static_cast<char>(0x80 | ((code >> 6) & 0x3F)); out += static_cast<char>(0x80 | (code & 0x3F)); }
                break;
            }
            default: out += s_[at_]; break;
            }
        }
        return false;
    }
    /** Digits by hand: strtod reads a comma where the host has set such a locale. */
    bool numeral(double& out) {
        const size_t start = at_;
        bool negative = false;
        if (at_ < s_.size() && (s_[at_] == '-' || s_[at_] == '+')) negative = s_[at_++] == '-';
        // The digits as a whole number, then the power of ten in one step with room to spare:
        // what was written with 17 digits comes back as the number it was.
        uint64_t mantissa = 0;
        int scale = 0, digits = 0;
        for (; at_ < s_.size() && s_[at_] >= '0' && s_[at_] <= '9'; ++at_, ++digits) {
            if (mantissa < 1000000000000000000ull) mantissa = mantissa * 10 + static_cast<uint64_t>(s_[at_] - '0'); else ++scale;
        }
        if (at_ < s_.size() && s_[at_] == '.') {
            for (++at_; at_ < s_.size() && s_[at_] >= '0' && s_[at_] <= '9'; ++at_, ++digits) {
                if (mantissa < 1000000000000000000ull) { mantissa = mantissa * 10 + static_cast<uint64_t>(s_[at_] - '0'); --scale; }
            }
        }
        if (digits == 0) { at_ = start; return false; }
        if (at_ < s_.size() && (s_[at_] == 'e' || s_[at_] == 'E')) {
            ++at_;
            bool down = false;
            if (at_ < s_.size() && (s_[at_] == '-' || s_[at_] == '+')) down = s_[at_++] == '-';
            int e = 0, n = 0;
            for (; at_ < s_.size() && s_[at_] >= '0' && s_[at_] <= '9'; ++at_, ++n) e = std::min(e * 10 + (s_[at_] - '0'), 4000);
            if (n == 0) return false;
            scale += down ? -e : e;
        }
        scale = std::clamp(scale, -340, 340);
        long double wide = static_cast<long double>(mantissa);
        wide = scale < 0 ? wide / std::pow(10.0L, static_cast<long double>(-scale)) : wide * std::pow(10.0L, static_cast<long double>(scale));
        double v = wide < 1e300L ? static_cast<double>(wide) : 1e300;
        out = negative ? -v : v;
        return true;
    }

    const std::string& s_;
    size_t at_ = 0, items_ = 0;
};

} // namespace json

// ---------------------------------------------------- an analysis as JSON

namespace detail {

template <class T> std::string list(const T* v, int n, const char* format) {
    std::string out = "[";
    for (int i = 0; i < n; ++i) {
        if (i) out += ',';
        out += json::number(static_cast<double>(v[i]), format);
    }
    return out + "]";
}
inline std::string pair(double a, double b, const char* format = "%.17g") {
    return "[" + json::number(a, format) + "," + json::number(b, format) + "]";
}
/** A level for the summary: null when there is none. */
inline std::string db(double v, const char* format = "%.2f") { return v <= kSilence + 1.0 ? "null" : json::number(v, format); }

inline void readList(const json::Value* v, double* out, int n) {
    if (v == nullptr || v->type != json::Value::kArray) return;
    for (int i = 0; i < n && i < static_cast<int>(v->array.size()); ++i)
        if (v->array[i].type == json::Value::kNumber) out[i] = v->array[i].number;
}
template <class T> int readSeries(const json::Value* v, T* out, int max, double lo, double hi) {
    if (v == nullptr || v->type != json::Value::kArray) return 0;
    const int n = std::min(max, static_cast<int>(v->array.size()));
    for (int i = 0; i < n; ++i) {
        const double x = v->array[i].type == json::Value::kNumber ? v->array[i].number : 0.0;
        out[i] = static_cast<T>(std::clamp(x, lo, hi));
    }
    return n;
}
inline double within(double v, double lo, double hi) { return v == v ? std::clamp(v, lo, hi) : lo; }

} // namespace detail

/** The figures a person reads, as members of a JSON object (without its braces). */
inline std::string summaryJson(const Analysis& a, const View& view) {
    using detail::db;
    const Result& r = a.r;
    const int right = r.channels > 1 ? 1 : 0;
    std::string s;
    s += "\"integrated_lufs\":" + db(r.integrated);
    s += ",\"short_term_max_lufs\":" + db(r.shortTermMax);
    s += ",\"momentary_max_lufs\":" + db(r.momentaryMax);
    s += ",\"loudness_range_lu\":" + json::number(r.range, "%.2f");
    s += ",\"true_peak_dbtp\":" + db(truePeakDb(r));
    s += ",\"true_peak_left_dbtp\":" + db(truePeakDb(r, 0)) + ",\"true_peak_right_dbtp\":" + db(truePeakDb(r, right));
    s += ",\"plr_db\":" + json::number(plr(r), "%.2f");
    s += ",\"sample_peak_db\":" + db(samplePeakDb(r));
    s += ",\"sample_peak_left_db\":" + db(samplePeakDb(r, 0)) + ",\"sample_peak_right_db\":" + db(samplePeakDb(r, right));
    s += std::string(",\"rms_reference\":\"") + (view.sine ? "sine" : "square") + "\"";
    s += ",\"rms_window_ms\":" + json::number(kRmsWindowMs[view.window], "%.0f");
    s += ",\"rms_left_db\":" + db(totalRmsDb(r, 0, view.sine)) + ",\"rms_right_db\":" + db(totalRmsDb(r, right, view.sine));
    s += ",\"rms_max_left_db\":" + db(maxRmsDb(r, view.window, 0, view.sine)) + ",\"rms_max_right_db\":" + db(maxRmsDb(r, view.window, right, view.sine));
    s += ",\"rms_min_left_db\":" + db(minRmsDb(r, view.window, 0, view.sine)) + ",\"rms_min_right_db\":" + db(minRmsDb(r, view.window, right, view.sine));
    s += ",\"rms_average_left_db\":" + db(averageRmsDb(r, view.window, 0, view.sine)) + ",\"rms_average_right_db\":" + db(averageRmsDb(r, view.window, right, view.sine));
    s += ",\"crest_left_db\":" + json::number(crestDb(r, 0), "%.2f") + ",\"crest_right_db\":" + json::number(crestDb(r, right), "%.2f");
    s += ",\"dc_offset_left_percent\":" + json::number(100.0 * dcOffset(r, 0), "%.4f") + ",\"dc_offset_right_percent\":" + json::number(100.0 * dcOffset(r, right), "%.4f");
    s += ",\"clipped_samples\":" + json::number(static_cast<double>(clipped(r)), "%.0f");
    s += ",\"balance_db\":" + json::number(balanceDb(r), "%.2f");
    s += ",\"correlation\":" + json::number(correlation(r), "%.3f");
    s += ",\"mid_db\":" + db(midDb(r, view.sine)) + ",\"side_db\":" + db(sideDb(r, view.sine));
    return s;
}

/** An analysis as JSON: what a song keeps, what a reference file is, and the JSON report. `extra`: more members for the outer object. */
inline std::string toJson(const Analysis& a, const View& view, const std::string& extra = std::string()) {
    using detail::list;
    using detail::pair;
    const Result& r = a.r;
    std::string s = "{\"format\":\"hnl-analysis\",\"version\":1";
    s += ",\"name\":" + json::quote(a.name);
    s += ",\"source\":" + json::quote(a.source);
    if (!a.note.empty()) s += ",\"note\":" + json::quote(a.note);
    s += std::string(",\"mode\":\"") + modeName(a.capture.mode) + "\",\"status\":\"" + statusName(a.capture.status) + "\"";
    s += ",\"ended\":" + json::quote(endedName(a.capture.ended));
    s += ",\"sample_rate\":" + json::number(r.sampleRate, "%.0f") + ",\"channels\":" + json::number(r.channels, "%.0f");
    s += ",\"duration_seconds\":" + json::number(r.seconds(), "%.3f");
    s += ",\n\"summary\":{" + summaryJson(a, view) + "}";
    if (!extra.empty()) s += ",\n" + extra;
    s += ",\n\"data\":{\"have\":" + json::number(a.have, "%.0f");
    s += ",\"frames\":" + json::number(static_cast<double>(r.frames), "%.0f");
    s += ",\"capture\":[" + json::number(a.capture.ended, "%.0f") + "," + json::number(static_cast<double>(a.capture.startFrame), "%.0f") + "," +
         (a.capture.positionKnown ? "1," : "0,") + (a.capture.automatic ? "1," : "0,") + json::number(a.capture.seeks, "%.0f") + "]";
    s += ",\"loudness\":[" + json::number(r.integrated) + "," + json::number(r.shortTermMax) + "," + json::number(r.momentaryMax) + "," + json::number(r.range) + "," +
         json::number(r.rangeLow) + "," + json::number(r.rangeHigh) + "]";
    for (int c = 0; c < 2; ++c) {
        const ChannelStats& k = r.ch[c];
        s += std::string(",\"") + (c ? "right" : "left") + "\":[" + json::number(k.samplePeak) + "," + json::number(k.truePeak) + "," + json::number(k.minSample) + "," +
             json::number(k.maxSample) + "," + json::number(k.sum) + "," + json::number(k.sumSquares) + "," + json::number(static_cast<double>(k.clipped), "%.0f") + "]";
    }
    s += ",\"sum_lr\":" + json::number(r.sumLR);
    s += ",\n\"windows\":[";
    for (int w = 0; w < kRmsWindows; ++w) {
        const RmsWindowStats& k = r.rms[w];
        s += std::string(w ? "," : "") + "\n{\"ms\":" + json::number(kRmsWindowMs[w], "%.0f") + ",\"count\":" + json::number(static_cast<double>(k.count), "%.0f");
        s += ",\"min\":" + pair(k.minMs[0], k.minMs[1]) + ",\"max\":" + pair(k.maxMs[0], k.maxMs[1]) + ",\"sum\":" + pair(k.sumRms[0], k.sumRms[1]);
        // The histogram from its first to its last level in use.
        int first = kHistBins, last = -1;
        for (int b = 0; b < kHistBins; ++b)
            if (k.hist[0][b] || k.hist[1][b]) { first = std::min(first, b); last = b; }
        s += ",\"first\":" + json::number(last < 0 ? 0 : first, "%.0f");
        for (int c = 0; c < 2; ++c) s += std::string(",\"") + (c ? "right" : "left") + "\":" + (last < 0 ? std::string("[]") : list(k.hist[c] + first, last - first + 1, "%.0f"));
        s += "}";
    }
    s += "]";
    s += ",\n\"spectrum\":" + list(r.spectrum, kSpecBands, "%.9g") + ",\"spectra\":" + json::number(static_cast<double>(r.spectra), "%.0f");
    s += ",\n\"wave_step\":" + json::number(r.waveStep, "%.6g");
    for (int c = 0; c < 2; ++c) {
        s += std::string(",\n\"wave_min_") + (c ? "right" : "left") + "\":" + list(r.waveMin[c], r.waveCount, "%.4f");
        s += std::string(",\n\"wave_max_") + (c ? "right" : "left") + "\":" + list(r.waveMax[c], r.waveCount, "%.4f");
    }
    s += ",\n\"loud_step\":" + json::number(r.loudStep, "%.6g");
    s += ",\n\"loud_momentary\":" + list(r.loudM, r.loudCount, "%.2f") + ",\n\"loud_short_term\":" + list(r.loudS, r.loudCount, "%.2f");
    return s + "}}\n";
}

/**
 * Reads what toJson wrote, or a reference written by hand with only a "summary" (or its
 * members at the top). False when it is not an analysis at all. Every figure is brought within
 * what a measurement can give, so a damaged or made-up file cannot upset the window.
 */
inline bool fromJson(const std::string& text, Analysis& out) {
    using detail::within;
    json::Value root;
    if (!json::Parser::parse(text, root) || root.type != json::Value::kObject) return false;
    const json::Value* summary = root.get("summary");
    if (summary == nullptr || summary->type != json::Value::kObject) summary = &root;
    const json::Value* data = root.get("data");
    if (data != nullptr && data->type != json::Value::kObject) data = nullptr;
    if (data == nullptr && !summary->has("integrated_lufs") && !summary->has("true_peak_dbtp") && !summary->has("rms_left_db")) return false;

    Analysis a;
    a.name = root.str("name").substr(0, 200);
    a.source = root.str("source").substr(0, 200);
    a.note = root.str("note").substr(0, 200);
    const std::string mode = root.str("mode"), status = root.str("status");
    a.capture.mode = mode == "continuous" || mode == "live" ? kContinuousMode : (mode == "file" ? kFile : kTrackMode); // "live" and "session": the names before 0.1.0 was out
    a.capture.status = status == "partial" ? kPartial : kComplete; // what was running when it was written is not running now
    Result& r = a.r;
    r.sampleRate = within(root.num("sample_rate", 0.0), 0.0, 1e6);
    r.channels = root.num("channels", 2.0) == 1.0 ? 1 : 2;

    if (data == nullptr) { // by hand: levels in decibels
        const auto level = [&](const char* key, double& lin) {
            if (!summary->has(key) || summary->get(key)->type != json::Value::kNumber) return false;
            lin = std::pow(10.0, within(summary->num(key, 0.0), -200.0, 40.0) / 20.0);
            return true;
        };
        a.have = 0;
        const double seconds = within(root.num("duration_seconds", 0.0), 0.0, 1e6);
        r.frames = std::max<uint64_t>(1, static_cast<uint64_t>(seconds * r.sampleRate));
        if (summary->get("integrated_lufs") != nullptr && summary->get("integrated_lufs")->type == json::Value::kNumber) {
            r.integrated = within(summary->num("integrated_lufs", kSilence), kSilence, 20.0);
            a.have |= kHaveLoudness;
        }
        if (summary->has("short_term_max_lufs") && summary->has("momentary_max_lufs")) {
            r.shortTermMax = within(summary->num("short_term_max_lufs", kSilence), kSilence, 20.0);
            r.momentaryMax = within(summary->num("momentary_max_lufs", kSilence), kSilence, 20.0);
            a.have |= kHaveMax;
        }
        if (summary->has("loudness_range_lu")) {
            r.range = within(summary->num("loudness_range_lu", 0.0), 0.0, 100.0);
            a.have |= kHaveRange;
        }
        double tp = 0.0, sp = 0.0;
        const bool haveTp = level("true_peak_dbtp", tp);
        if (haveTp) {
            r.ch[0].truePeak = r.ch[1].truePeak = tp;
            level("true_peak_left_dbtp", r.ch[0].truePeak);
            level("true_peak_right_dbtp", r.ch[1].truePeak);
            // The sample peak, when it is not given, is not above the true peak.
            if (!level("sample_peak_db", sp)) sp = tp;
            r.ch[0].samplePeak = r.ch[1].samplePeak = sp;
            level("sample_peak_left_db", r.ch[0].samplePeak);
            level("sample_peak_right_db", r.ch[1].samplePeak);
            const double clips = within(summary->num("clipped_samples", 0.0), 0.0, 1e15);
            r.ch[0].clipped = static_cast<uint64_t>(clips);
            a.have |= kHavePeaks;
        }
        const double ref = summary->str("rms_reference") == "square" ? 1.0 : std::pow(10.0, -kSineRef / 20.0);
        double left = 0.0, right = 0.0;
        if (level("rms_left_db", left)) {
            if (!level("rms_right_db", right)) right = left;
            r.ch[0].sumSquares = left * left * ref * ref * static_cast<double>(r.frames);
            r.ch[1].sumSquares = right * right * ref * ref * static_cast<double>(r.frames);
            a.have |= kHaveRms;
        }
        if (a.have == 0) return false;
        out = a;
        return true;
    }

    a.have = static_cast<unsigned>(within(data->num("have", kHaveAll), 0.0, kHaveAll));
    r.frames = static_cast<uint64_t>(within(data->num("frames", 0.0), 0.0, 9e15));
    double capture[5] = {0, 0, 0, 0, 0};
    detail::readList(data->get("capture"), capture, 5);
    a.capture.ended = static_cast<int>(within(capture[0], 0.0, kEndedCount - 1));
    a.capture.startFrame = static_cast<uint64_t>(within(capture[1], 0.0, 9e15));
    a.capture.positionKnown = capture[2] > 0.5;
    a.capture.automatic = capture[3] > 0.5;
    a.capture.seeks = static_cast<uint32_t>(within(capture[4], 0.0, 4e9));
    double loud[6] = {kSilence, kSilence, kSilence, 0.0, kSilence, kSilence};
    detail::readList(data->get("loudness"), loud, 6);
    r.integrated = within(loud[0], kSilence, 20.0);
    r.shortTermMax = within(loud[1], kSilence, 20.0);
    r.momentaryMax = within(loud[2], kSilence, 20.0);
    r.range = within(loud[3], 0.0, 100.0);
    r.rangeLow = within(loud[4], kSilence, 20.0);
    r.rangeHigh = within(loud[5], kSilence, 20.0);
    const double frames = static_cast<double>(r.frames);
    for (int c = 0; c < 2; ++c) {
        double k[7] = {0, 0, 0, 0, 0, 0, 0};
        detail::readList(data->get(c ? "right" : "left"), k, 7);
        r.ch[c].samplePeak = within(k[0], 0.0, 100.0);
        r.ch[c].truePeak = within(k[1], 0.0, 100.0);
        r.ch[c].minSample = within(k[2], -100.0, 100.0);
        r.ch[c].maxSample = within(k[3], -100.0, 100.0);
        r.ch[c].sum = within(k[4], -100.0 * frames, 100.0 * frames);
        r.ch[c].sumSquares = within(k[5], 0.0, 1e4 * frames);
        r.ch[c].clipped = static_cast<uint64_t>(within(k[6], 0.0, 9e15));
    }
    const double energy = std::sqrt(r.ch[0].sumSquares * r.ch[1].sumSquares);
    r.sumLR = within(data->num("sum_lr", 0.0), -energy, energy);
    if (const json::Value* windows = data->get("windows"))
        for (size_t i = 0; windows->type == json::Value::kArray && i < windows->array.size(); ++i) {
            const json::Value& v = windows->array[i];
            int w = -1;
            for (int k = 0; k < kRmsWindows; ++k)
                if (v.num("ms", -1.0) == kRmsWindowMs[k]) w = k;
            if (w < 0) continue; // a length this version does not have
            RmsWindowStats& k = r.rms[w];
            k.count = static_cast<uint64_t>(within(v.num("count", 0.0), 0.0, 9e15));
            double lo[2] = {0, 0}, hi[2] = {0, 0}, sum[2] = {0, 0};
            detail::readList(v.get("min"), lo, 2);
            detail::readList(v.get("max"), hi, 2);
            detail::readList(v.get("sum"), sum, 2);
            const int first = static_cast<int>(within(v.num("first", 0.0), 0.0, kHistBins - 1));
            for (int c = 0; c < 2; ++c) {
                k.minMs[c] = within(lo[c], 0.0, 1e4);
                k.maxMs[c] = within(hi[c], k.minMs[c], 1e4);
                k.sumRms[c] = within(sum[c], 0.0, 100.0 * static_cast<double>(k.count));
                detail::readSeries(v.get(c ? "right" : "left"), k.hist[c] + first, kHistBins - first, 0.0, 4e9);
            }
        }
    detail::readSeries(data->get("spectrum"), r.spectrum, kSpecBands, 0.0, 1e4);
    r.spectra = static_cast<uint64_t>(within(data->num("spectra", 0.0), 0.0, 9e15));
    r.waveStep = within(data->num("wave_step", 0.0), 0.0, 1e6);
    r.waveCount = kSeries;
    for (int c = 0; c < 2; ++c) {
        r.waveCount = std::min(r.waveCount, detail::readSeries(data->get(c ? "wave_min_right" : "wave_min_left"), r.waveMin[c], kSeries, -100.0, 100.0));
        r.waveCount = std::min(r.waveCount, detail::readSeries(data->get(c ? "wave_max_right" : "wave_max_left"), r.waveMax[c], kSeries, -100.0, 100.0));
    }
    r.loudStep = within(data->num("loud_step", 0.0), 0.0, 1e6);
    r.loudCount = std::min(detail::readSeries(data->get("loud_momentary"), r.loudM, kSeries, kSilence, 20.0),
                           detail::readSeries(data->get("loud_short_term"), r.loudS, kSeries, kSilence, 20.0));
    if (r.frames == 0) a.capture.status = kEmpty;
    out = a;
    return true;
}

// ---------------------------------------------------------------- reports

struct ReportInput {
    const Analysis* track = nullptr;
    const Analysis* reference = nullptr; // or null
    int match = kAsMeasured;
    View view;
    Targets targets;
    const char* version = ""; // of the plugin
};

namespace detail {

inline std::string fixed(double v, int decimals, bool sign = false) {
    char format[16];
    std::snprintf(format, sizeof(format), sign ? "%%+.%df" : "%%.%df", decimals);
    return json::number(v, format);
}
inline std::string levelText(double v, int decimals = 2) { return v <= kSilence + 1.0 ? std::string("-inf") : fixed(v, decimals); }
inline std::string padLeft(const std::string& s, size_t width) { return s.size() >= width ? s : std::string(width - s.size(), ' ') + s; }
inline std::string padRight(const std::string& s, size_t width) { return s.size() >= width ? s : s + std::string(width - s.size(), ' '); }

struct Line {
    std::string name, left, right, unit; // `right` empty: one figure for both channels
};

/** The figures of a report in order, each group under its title. */
inline std::vector<std::pair<std::string, std::vector<Line>>> sections(const ReportInput& in) {
    const Analysis& a = *in.track;
    const Result& r = a.r;
    const View& v = in.view;
    const int R = r.channels > 1 ? 1 : 0;
    std::vector<std::pair<std::string, std::vector<Line>>> out;
    out.push_back({"Loudness", {
        {"Integrated", levelText(r.integrated), "", "LUFS"},
        {"Short-term max", levelText(r.shortTermMax), "", "LUFS"},
        {"Momentary max", levelText(r.momentaryMax), "", "LUFS"},
        {"Loudness range (LRA)", fixed(r.range, 2), "", "LU"},
        {"True peak", levelText(truePeakDb(r)), "", "dBTP"},
        {"Peak to loudness (PLR)", fixed(plr(r), 2), "", "dB"},
    }});
    char window[64];
    std::snprintf(window, sizeof(window), "Amplitude (RMS window %.0f ms, 0 dB = FS %s wave)", kRmsWindowMs[v.window], v.sine ? "sine" : "square");
    out.push_back({window, {
        {"Peak amplitude", levelText(samplePeakDb(r, 0)), levelText(samplePeakDb(r, R)), "dB"},
        {"True peak amplitude", levelText(truePeakDb(r, 0)), levelText(truePeakDb(r, R)), "dBTP"},
        {"Maximum sample value", fixed(r.ch[0].maxSample, 5), fixed(r.ch[R].maxSample, 5), ""},
        {"Minimum sample value", fixed(r.ch[0].minSample, 5), fixed(r.ch[R].minSample, 5), ""},
        {"Clipped samples", fixed(static_cast<double>(r.ch[0].clipped), 0), fixed(static_cast<double>(r.ch[R].clipped), 0), ""},
        {"Total RMS amplitude", levelText(totalRmsDb(r, 0, v.sine)), levelText(totalRmsDb(r, R, v.sine)), "dB"},
        {"Maximum RMS amplitude", levelText(maxRmsDb(r, v.window, 0, v.sine)), levelText(maxRmsDb(r, v.window, R, v.sine)), "dB"},
        {"Minimum RMS amplitude", levelText(minRmsDb(r, v.window, 0, v.sine)), levelText(minRmsDb(r, v.window, R, v.sine)), "dB"},
        {"Average RMS amplitude", levelText(averageRmsDb(r, v.window, 0, v.sine)), levelText(averageRmsDb(r, v.window, R, v.sine)), "dB"},
        {"Crest factor", fixed(crestDb(r, 0), 2), fixed(crestDb(r, R), 2), "dB"},
        {"DC offset", fixed(100.0 * dcOffset(r, 0), 3), fixed(100.0 * dcOffset(r, R), 3), "%"},
    }});
    out.push_back({"Stereo", {
        {"Balance (left - right)", fixed(balanceDb(r), 2, true), "", "dB"},
        {"Correlation", fixed(correlation(r), 2, true), "", ""},
        {"Mid RMS", levelText(midDb(r, v.sine)), "", "dB"},
        {"Side RMS", levelText(sideDb(r, v.sine)), "", "dB"},
    }});
    return out;
}

inline std::vector<Line> advisorLines(const ReportInput& in, const Advice& a) {
    std::vector<Line> lines = {
        {"Target loudness", fixed(in.targets.loudness, 2), "", "LUFS"},
        {"Maximum true peak", fixed(in.targets.truePeak, 2), "", "dBTP"},
        {"Preferred PLR", fixed(in.targets.plr, 1), "", "dB"},
    };
    if (a.kind == Advice::kNoMeasurement) return lines;
    lines.push_back({"Required gain", fixed(a.gain, 2, true), "", "dB"});
    lines.push_back({"Maximum gain under the ceiling", fixed(a.maxGain, 2, true), "", "dB"});
    lines.push_back({"Minimum peak reduction", fixed(a.peakReduction, 2), "", "dB"});
    lines.push_back({"PLR, measured", fixed(a.plr, 2), "", "dB"});
    lines.push_back({"PLR, of the targets", fixed(a.targetPlr, 2), "", "dB"});
    return lines;
}

inline const char* matchName(int match) {
    static const char* const kNames[kMatchCount] = {"as measured", "matched in loudness", "matched in true peak"};
    return kNames[match < 0 || match >= kMatchCount ? 0 : match];
}

} // namespace detail

inline Advice adviceFor(const Analysis& a, const Targets& t) {
    return advise(a.has(kHaveLoudness | kHavePeaks) && hasLoudness(a.r) && truePeakDb(a.r) > kSilence, a.r.integrated, truePeakDb(a.r), t);
}

/** The report as plain text: what Copy puts on the clipboard, and the .txt file. */
inline std::string textReport(const ReportInput& in) {
    using namespace detail;
    const Analysis& a = *in.track;
    std::string s = "HNL Analyzer " + std::string(in.version) + "\n";
    s += "Track:   " + (a.name.empty() ? std::string("(no name)") : a.name) + "\n";
    s += "Source:  " + describe(a) + "\n";
    if (a.empty()) return s;
    for (const auto& section : sections(in)) {
        const bool two = !section.second.empty() && !section.second[0].right.empty();
        s += "\n" + section.first + "\n";
        if (two) s += padRight("", 28) + padLeft("Left", 12) + padLeft("Right", 12) + "\n";
        for (const Line& l : section.second) {
            s += "  " + padRight(l.name, 26) + padLeft(l.left, 12);
            if (!l.right.empty()) s += padLeft(l.right, 12);
            s += (l.unit.empty() ? "" : " " + l.unit) + "\n";
        }
    }
    const Advice advice = adviceFor(a, in.targets);
    s += "\nMastering advisor\n";
    for (const Line& l : advisorLines(in, advice)) s += "  " + padRight(l.name, 32) + padLeft(l.left, 8) + " " + l.unit + "\n";
    s += std::string("  ") + adviceTitle(advice) + ": " + adviceText(advice, in.targets) + "\n";
    if (in.reference != nullptr && !in.reference->empty()) {
        const double gain = matchGain(a, *in.reference, in.match);
        s += "\nReference: " + (in.reference->name.empty() ? std::string("(no name)") : in.reference->name) + ", " + matchName(in.match);
        if (in.match != kAsMeasured) s += " (reference " + fixed(gain, 2, true) + " dB)";
        s += "\n" + padRight("", 20) + padLeft("Track", 12) + padLeft("Reference", 12) + padLeft("Difference", 12) + "\n";
        for (const Row& row : compare(a, in.reference, gain, in.view)) {
            if (!row.haveTrack && !row.haveReference) continue;
            s += "  " + padRight(row.name, 18) + padLeft(row.haveTrack ? fixed(row.track, row.decimals) : "-", 12) +
                 padLeft(row.haveReference ? fixed(row.reference, row.decimals) : "-", 12) +
                 padLeft(row.haveTrack && row.haveReference ? fixed(row.delta(), row.decimals, true) : "-", 12) + (row.unit[0] ? std::string(" ") + row.unit : "") + "\n";
        }
    }
    return s;
}

inline std::string markdownReport(const ReportInput& in) {
    using namespace detail;
    const Analysis& a = *in.track;
    std::string s = "# " + (a.name.empty() ? std::string("Analysis") : a.name) + "\n\n";
    s += "HNL Analyzer " + std::string(in.version) + " · " + describe(a) + "\n";
    if (a.empty()) return s;
    for (const auto& section : sections(in)) {
        const bool two = !section.second.empty() && !section.second[0].right.empty();
        s += "\n## " + section.first + "\n\n";
        s += two ? "| | Left | Right | |\n|---|---:|---:|---|\n" : "| | | |\n|---|---:|---|\n";
        for (const Line& l : section.second) s += "| " + l.name + " | " + l.left + (two ? " | " + l.right : "") + " | " + l.unit + " |\n";
    }
    const Advice advice = adviceFor(a, in.targets);
    s += "\n## Mastering advisor\n\n| | | |\n|---|---:|---|\n";
    for (const Line& l : advisorLines(in, advice)) s += "| " + l.name + " | " + l.left + " | " + l.unit + " |\n";
    s += std::string("\n**") + adviceTitle(advice) + ".** " + adviceText(advice, in.targets) + "\n";
    if (in.reference != nullptr && !in.reference->empty()) {
        const double gain = matchGain(a, *in.reference, in.match);
        s += "\n## Reference: " + (in.reference->name.empty() ? std::string("(no name)") : in.reference->name) + "\n\n";
        s += std::string("Compared ") + matchName(in.match) + (in.match != kAsMeasured ? " (reference " + fixed(gain, 2, true) + " dB)" : std::string()) + ".\n\n";
        s += "| Metric | Track | Reference | Difference | |\n|---|---:|---:|---:|---|\n";
        for (const Row& row : compare(a, in.reference, gain, in.view)) {
            if (!row.haveTrack && !row.haveReference) continue;
            s += std::string("| ") + row.name + " | " + (row.haveTrack ? fixed(row.track, row.decimals) : "-") + " | " + (row.haveReference ? fixed(row.reference, row.decimals) : "-") +
                 " | " + (row.haveTrack && row.haveReference ? fixed(row.delta(), row.decimals, true) : "-") + " | " + row.unit + " |\n";
        }
    }
    return s;
}

/** The report as JSON: the analysis, with the advisor and the comparison beside its summary. */
inline std::string jsonReport(const ReportInput& in) {
    using detail::fixed;
    const Analysis& a = *in.track;
    const Advice advice = adviceFor(a, in.targets);
    std::string extra = "\"advisor\":{\"target_lufs\":" + fixed(in.targets.loudness, 2) + ",\"max_true_peak_dbtp\":" + fixed(in.targets.truePeak, 2) +
                        ",\"preferred_plr_db\":" + fixed(in.targets.plr, 2);
    if (advice.kind != Advice::kNoMeasurement)
        extra += ",\"required_gain_db\":" + fixed(advice.gain, 2) + ",\"max_gain_under_ceiling_db\":" + fixed(advice.maxGain, 2) + ",\"min_peak_reduction_db\":" +
                 fixed(advice.peakReduction, 2) + ",\"plr_db\":" + fixed(advice.plr, 2) + ",\"plr_of_targets_db\":" + fixed(advice.targetPlr, 2);
    extra += ",\"verdict\":" + json::quote(adviceTitle(advice)) + ",\"advice\":" + json::quote(adviceText(advice, in.targets)) + "}";
    if (in.reference != nullptr && !in.reference->empty()) {
        const double gain = matchGain(a, *in.reference, in.match);
        extra += ",\n\"reference\":{\"name\":" + json::quote(in.reference->name) + ",\"match\":" + json::quote(detail::matchName(in.match)) + ",\"reference_gain_db\":" + fixed(gain, 2) + ",\"rows\":[";
        bool first = true;
        for (const Row& row : compare(a, in.reference, gain, in.view)) {
            if (!row.haveTrack || !row.haveReference) continue;
            extra += std::string(first ? "" : ",") + "\n{\"metric\":" + json::quote(row.name) + ",\"unit\":" + json::quote(row.unit) + ",\"track\":" + fixed(row.track, row.decimals) +
                     ",\"reference\":" + fixed(row.reference, row.decimals) + ",\"difference\":" + fixed(row.delta(), row.decimals) + "}";
            first = false;
        }
        extra += "]}";
    }
    return toJson(a, in.view, extra);
}

} // namespace analyzer
} // namespace hnl
