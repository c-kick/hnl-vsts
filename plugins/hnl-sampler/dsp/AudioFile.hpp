// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// Reads the sound files a user drags onto the sampler: WAV (and RF64), AIFF
// and AIFF-C, and FLAC, with what they say about loops, root key and markers.
// Writes WAV. Whatever is in the file, every read is checked against the end
// of the data and every size taken from the file is capped, so a damaged
// file gives an error or the part of it that could be read: never a crash,
// an exception or an allocation larger than the file could fill.
//
// WAV and AIFF hold their samples as plain numbers, so the engine can also
// play them straight from disk (probeAudioFile, then readRawFrames). FLAC is
// decoded whole.
// Header-only, no dependencies.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <new>
#include <string>
#include <vector>

#include "AudioData.hpp"
#include "Files.hpp"
#include "../../../common/dsp/Safety.hpp"

namespace hnl {
namespace sampler {

/** What a file is, without decoding it. */
struct AudioInfo {
    std::string format; // "WAV", "AIFF", "FLAC"
    int channels = 0;   // as in the file (may be more than 2)
    double sampleRate = 0;
    uint64_t frames = 0; // for FLAC what STREAMINFO says, 0 when it does not say
    int bits = 0;
    bool isFloat = false;
    // For streaming from disk: set only when the samples lie in the file as plain PCM or float
    // (WAV and AIFF, not FLAC): where they start, bytes per sample (a frame is channels times that),
    // and their byte order.
    bool raw = false;
    uint64_t dataOffset = 0;
    int bytesPerSample = 0;
    bool bigEndian = false;
    bool isUnsigned8 = false; // 8-bit WAV, or AIFF-C 'raw '
};

namespace audiofile {

namespace fs = std::filesystem;

constexpr uint64_t kMaxFrames = uint64_t(1) << 28; // per channel: 101 minutes at 44.1 kHz
// What one sound may take once it is read into memory, as floats: 512 MiB, which is 50 minutes of mono or 25 of
// stereo at 44.1 kHz. A FLAC file of a few kilobytes can claim any length, so the length alone says nothing.
constexpr uint64_t kMaxSoundValues = uint64_t(1) << 27; // frames times the channels that are kept
constexpr const char* kTooLarge = "sound too large: over 512 MB once it is in memory";
inline bool fitsInMemory(uint64_t frames, int channels) { return frames <= kMaxSoundValues / static_cast<uint64_t>(channels > 1 ? 2 : 1); }
constexpr int kMaxChannels = 256;
constexpr size_t kMaxMetaChunk = size_t(1) << 20; // bytes of a metadata chunk that are read, the rest is ignored
constexpr size_t kMaxChunks = 100000;
constexpr size_t kMaxLoops = 256;
constexpr size_t kMaxCues = 4096;
constexpr uint64_t kMaxFlacFile = uint64_t(1) << 31; // a FLAC file is read whole
constexpr float kFloatLimit = 1.0f; // samples of float files are held within this

// ---------------------------------------------------------------- bytes

inline uint16_t le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
inline uint32_t le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}
inline uint64_t le64(const uint8_t* p) { return le32(p) | (static_cast<uint64_t>(le32(p + 4)) << 32); }
inline uint16_t be16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }
inline uint32_t be32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) | (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}
inline uint64_t be64(const uint8_t* p) { return (static_cast<uint64_t>(be32(p)) << 32) | be32(p + 4); }
inline bool tag(const uint8_t* p, const char* t) { return std::memcmp(p, t, 4) == 0; }

/** The 80-bit extended number AIFF gives its sample rate in; 0 when it is not a rate between 1 Hz and 2 GHz. */
inline double extended80(const uint8_t* p) {
    const int exponent = ((p[0] & 0x7f) << 8) | p[1];
    const uint64_t mantissa = be64(p + 2);
    if ((p[0] & 0x80) || mantissa == 0 || exponent < 16383 || exponent > 16383 + 30) return 0.0;
    return std::ldexp(static_cast<double>(mantissa), exponent - 16383 - 63);
}

// ---------------------------------------------------------------- files

inline std::FILE* openFile(const fs::path& file, bool write) {
    if (!write) return openPlainFile(file); // never a pipe or a device, which may not answer
#ifdef _WIN32
    return _wfopen(file.c_str(), L"wb");
#else
    return std::fopen(file.c_str(), "wb");
#endif
}

inline bool seekTo(std::FILE* f, uint64_t pos) {
    if (pos > static_cast<uint64_t>(INT64_MAX)) return false;
#ifdef _WIN32
    return _fseeki64(f, static_cast<__int64>(pos), SEEK_SET) == 0;
#else
    return fseeko(f, static_cast<off_t>(pos), SEEK_SET) == 0;
#endif
}

inline bool fileSize(std::FILE* f, uint64_t& size) {
#ifdef _WIN32
    if (_fseeki64(f, 0, SEEK_END) != 0) return false;
    const __int64 end = _ftelli64(f);
#else
    if (fseeko(f, 0, SEEK_END) != 0) return false;
    const off_t end = ftello(f);
#endif
    if (end < 0) return false;
    size = static_cast<uint64_t>(end);
    return seekTo(f, 0);
}

/** Bytes to parse, from memory or from an open file. */
struct Source {
    const uint8_t* mem = nullptr;
    std::FILE* file = nullptr;
    uint64_t size = 0;

    /** Copies n bytes from pos; false when the source does not hold them all. */
    bool read(uint64_t pos, void* dst, size_t n) const {
        if (pos > size || n > size - pos) return false;
        if (n == 0) return true;
        if (mem) {
            std::memcpy(dst, mem + pos, n);
            return true;
        }
        return file && seekTo(file, pos) && std::fread(dst, 1, n, file) == n;
    }
};

/** Reads the body of a chunk, as far as the file holds it and up to kMaxMetaChunk bytes. */
inline void readBody(const Source& src, uint64_t pos, uint64_t size, std::vector<uint8_t>& out) {
    const uint64_t avail = pos <= src.size ? src.size - pos : 0;
    const size_t n = static_cast<size_t>(std::min<uint64_t>(std::min<uint64_t>(size, avail), kMaxMetaChunk));
    out.resize(n);
    if (!src.read(pos, out.data(), n)) out.clear();
}

// ---------------------------------------------------------------- metadata

/** Copies what the info says about the sound into the fields of AudioData that describe it. */
inline void describe(AudioData& d, const AudioInfo& in) {
    d.channels = std::min(in.channels, 2);
    d.sampleRate = in.sampleRate;
    d.bits = in.bits;
    d.isFloat = in.isFloat;
    d.format = in.format;
}

/** Brings what a file said about itself within reason, and loops and cues within the sound when its length is known. */
inline void finishMeta(AudioData& d, uint64_t frames, bool known) {
    auto midi = [](int v) { return v >= 0 && v <= 127 ? v : -1; };
    d.rootKey = midi(d.rootKey);
    d.loKey = midi(d.loKey);
    d.hiKey = midi(d.hiKey);
    d.loVel = midi(d.loVel);
    d.hiVel = midi(d.hiVel);
    if (d.loKey >= 0 && d.hiKey >= 0 && d.loKey > d.hiKey) d.loKey = d.hiKey = -1;
    if (d.loVel >= 0 && d.hiVel >= 0 && d.loVel > d.hiVel) d.loVel = d.hiVel = -1;
    d.tuneCents = isFinite(d.tuneCents) ? std::clamp(d.tuneCents, -100.0, 100.0) : 0.0;
    d.gainDb = isFinite(d.gainDb) ? std::clamp(d.gainDb, -96.0, 96.0) : 0.0;
    if (!isFinite(d.tempo) || d.tempo < 1.0 || d.tempo > 1000.0) d.tempo = 0.0;

    std::vector<AudioData::Loop> loops;
    for (AudioData::Loop l : d.loops) {
        if (known) {
            if (l.start >= frames) continue;
            l.end = static_cast<size_t>(std::min<uint64_t>(l.end, frames));
        }
        if (l.end <= l.start) continue;
        if (l.type < AudioData::kForward || l.type > AudioData::kBackward) l.type = AudioData::kForward;
        if (loops.size() < kMaxLoops) loops.push_back(l);
    }
    d.loops.swap(loops);

    if (known) d.cues.erase(std::remove_if(d.cues.begin(), d.cues.end(), [&](size_t c) { return c >= frames; }), d.cues.end());
    std::sort(d.cues.begin(), d.cues.end());
    d.cues.erase(std::unique(d.cues.begin(), d.cues.end()), d.cues.end());
    if (d.cues.size() > kMaxCues) d.cues.resize(kMaxCues);
}

/** WAV 'smpl': root key and fine tune, and the loops (whose end in the file is their last frame). */
inline void readSmpl(const std::vector<uint8_t>& b, AudioData& d) {
    if (b.size() < 36) return;
    const uint8_t* p = b.data();
    const uint32_t note = le32(p + 12), fraction = le32(p + 16);
    if (note <= 127) {
        d.rootKey = static_cast<int>(note);
        d.tuneCents = fraction / 4294967296.0 * 100.0;
    }
    const size_t count = std::min<size_t>(std::min<size_t>(le32(p + 28), (b.size() - 36) / 24), kMaxLoops);
    for (size_t i = 0; i < count; ++i) {
        const uint8_t* q = p + 36 + 24 * i;
        const uint32_t type = le32(q + 4), start = le32(q + 8), last = le32(q + 12);
        if (last < start) continue;
        AudioData::Loop l;
        l.start = start;
        l.end = static_cast<size_t>(last) + 1;
        l.type = type == 1 ? AudioData::kPingPong : type == 2 ? AudioData::kBackward : AudioData::kForward;
        d.loops.push_back(l);
    }
}

/** WAV 'inst': root key, fine tune in cents, gain in dB, key and velocity range. */
inline void readInst(const std::vector<uint8_t>& b, AudioData& d) {
    if (b.size() < 7) return;
    d.rootKey = b[0];
    d.tuneCents = static_cast<int8_t>(b[1]);
    d.gainDb = static_cast<int8_t>(b[2]);
    d.loKey = b[3];
    d.hiKey = b[4];
    d.loVel = b[5];
    d.hiVel = b[6];
}

/** WAV 'cue ': marker positions in frames. */
inline void readCue(const std::vector<uint8_t>& b, AudioData& d) {
    if (b.size() < 4) return;
    const size_t count = std::min<size_t>(std::min<size_t>(le32(b.data()), (b.size() - 4) / 24), kMaxCues);
    for (size_t i = 0; i < count; ++i) {
        const uint8_t* q = b.data() + 4 + 24 * i;
        const uint32_t position = le32(q + 4), offset = le32(q + 20);
        d.cues.push_back(offset != 0 || position == 0 ? offset : position);
    }
}

/** WAV 'acid': tempo, and the root note when its flags say there is one. */
inline void readAcid(const std::vector<uint8_t>& b, AudioData& d) {
    if (b.size() < 24) return;
    const uint32_t flags = le32(b.data());
    if ((flags & 0x02) && d.rootKey < 0) d.rootKey = le16(b.data() + 4);
    const uint32_t bits = le32(b.data() + 20);
    float tempo;
    std::memcpy(&tempo, &bits, sizeof(tempo));
    if (isFinite(tempo)) d.tempo = tempo;
}

// ---------------------------------------------------------------- WAV

/** Reads the chunks of a WAV or RF64 file: the format, where the samples are, and (into meta when not null) the metadata. */
inline bool parseWav(const Source& src, AudioInfo& info, AudioData* meta, std::string& error) {
    uint8_t h[12];
    if (!src.read(0, h, 12)) {
        error = "too short for a WAV file";
        return false;
    }
    const bool rf64 = tag(h, "RF64") || tag(h, "BW64");
    std::vector<uint8_t> fmt, smpl, inst, cue, acid;
    bool haveData = false, haveDs64 = false;
    uint64_t dataPos = 0, dataSize = 0, ds64Data = 0;
    uint64_t pos = 12;
    for (size_t n = 0; n < kMaxChunks && pos <= src.size && src.size - pos >= 8; ++n) {
        uint8_t c[8];
        if (!src.read(pos, c, 8)) break;
        uint64_t size = le32(c + 4);
        const uint64_t body = pos + 8, avail = src.size - body;
        if (rf64 && haveDs64 && size == 0xFFFFFFFFu && tag(c, "data")) size = ds64Data;
        bool toEnd = size > avail; // a chunk that claims more than the file holds ends the walk
        if (tag(c, "data")) {
            if (size == 0) toEnd = true; // written while recording, never filled in
            if (!haveData) {
                haveData = true;
                dataPos = body;
                dataSize = toEnd ? avail : size;
            }
        } else if (tag(c, "fmt ")) {
            if (fmt.empty()) readBody(src, body, size, fmt);
        } else if (tag(c, "ds64")) {
            std::vector<uint8_t> d;
            readBody(src, body, size, d);
            if (d.size() >= 16) {
                ds64Data = le64(d.data() + 8);
                haveDs64 = true;
            }
        } else if (meta) {
            if (tag(c, "smpl") && smpl.empty()) readBody(src, body, size, smpl);
            else if (tag(c, "inst") && inst.empty()) readBody(src, body, size, inst);
            else if (tag(c, "cue ") && cue.empty()) readBody(src, body, size, cue);
            else if (tag(c, "acid") && acid.empty()) readBody(src, body, size, acid);
        }
        if (toEnd) break;
        pos = body + size + (size & 1);
    }
    if (fmt.size() < 16) {
        error = "WAV file without a format chunk";
        return false;
    }
    if (!haveData) {
        error = "WAV file without a data chunk";
        return false;
    }
    const uint8_t* f = fmt.data();
    unsigned code = le16(f);
    const int channels = le16(f + 2);
    const uint32_t rate = le32(f + 4);
    const unsigned blockAlign = le16(f + 12), container = le16(f + 14);
    unsigned bits = container;
    if (code == 0xFFFE) {
        static const uint8_t kGuidTail[14] = {0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
        if (fmt.size() < 40 || std::memcmp(f + 26, kGuidTail, 14) != 0) {
            error = "WAV extensible format of an unknown kind";
            return false;
        }
        code = le16(f + 24);
        const unsigned valid = le16(f + 18);
        if (valid > 0 && valid <= container) bits = valid;
    }
    if (code != 1 && code != 3) {
        error = "WAV format " + std::to_string(code) + " is compressed, which this reader does not take";
        return false;
    }
    if (channels < 1 || channels > kMaxChannels) {
        error = "WAV file with " + std::to_string(channels) + " channels";
        return false;
    }
    if (rate < 1 || rate > 10000000) {
        error = "WAV file with an impossible sample rate";
        return false;
    }
    const bool isFloat = code == 3;
    auto fits = [&](unsigned bytes) { return isFloat ? bytes == 4 || bytes == 8 : bytes >= 1 && bytes <= 4; };
    unsigned bytes = (container + 7) / 8;
    if (blockAlign % channels == 0 && blockAlign / channels >= bytes && fits(blockAlign / channels)) bytes = blockAlign / channels;
    if (container == 0 || !fits(bytes)) {
        error = "WAV file with " + std::to_string(container) + (isFloat ? "-bit float samples" : "-bit samples");
        return false;
    }
    const uint64_t frameBytes = static_cast<uint64_t>(bytes) * channels;
    const uint64_t frames = dataSize / frameBytes;
    if (frames > kMaxFrames) {
        error = "sound too long";
        return false;
    }
    info.format = "WAV";
    info.channels = channels;
    info.sampleRate = rate;
    info.frames = frames;
    info.bits = static_cast<int>(bits);
    info.isFloat = isFloat;
    info.raw = true;
    info.dataOffset = dataPos;
    info.bytesPerSample = static_cast<int>(bytes);
    info.bigEndian = false;
    info.isUnsigned8 = !isFloat && bytes == 1;
    if (meta) {
        describe(*meta, info);
        readAcid(acid, *meta);
        readInst(inst, *meta);
        readSmpl(smpl, *meta);
        readCue(cue, *meta);
    }
    return true;
}

// ---------------------------------------------------------------- AIFF

/** Reads the chunks of an AIFF or AIFF-C file, as parseWav does. */
inline bool parseAiff(const Source& src, AudioInfo& info, AudioData* meta, std::string& error) {
    uint8_t h[12];
    if (!src.read(0, h, 12)) {
        error = "too short for an AIFF file";
        return false;
    }
    const bool aifc = tag(h + 8, "AIFC");
    std::vector<uint8_t> comm, mark, inst;
    bool haveData = false;
    uint64_t dataPos = 0, dataSize = 0;
    uint64_t pos = 12;
    for (size_t n = 0; n < kMaxChunks && pos <= src.size && src.size - pos >= 8; ++n) {
        uint8_t c[8];
        if (!src.read(pos, c, 8)) break;
        const uint64_t size = be32(c + 4);
        const uint64_t body = pos + 8, avail = src.size - body;
        bool toEnd = size > avail;
        if (tag(c, "SSND")) {
            if (size == 0) toEnd = true;
            uint8_t o[8];
            if (!haveData && src.read(body, o, 8)) {
                const uint64_t offset = be32(o), held = toEnd ? avail : size;
                if (held >= 8 && held - 8 >= offset) {
                    haveData = true;
                    dataPos = body + 8 + offset;
                    dataSize = held - 8 - offset;
                }
            }
        } else if (tag(c, "COMM")) {
            if (comm.empty()) readBody(src, body, size, comm);
        } else if (meta) {
            if (tag(c, "MARK") && mark.empty()) readBody(src, body, size, mark);
            else if (tag(c, "INST") && inst.empty()) readBody(src, body, size, inst);
        }
        if (toEnd) break;
        pos = body + size + (size & 1);
    }
    if (comm.size() < 18) {
        error = "AIFF file without a COMM chunk";
        return false;
    }
    const uint8_t* p = comm.data();
    const int channels = be16(p);
    const uint64_t commFrames = be32(p + 2);
    const int bits = be16(p + 6);
    const double rate = extended80(p + 8);
    char type[5] = "NONE";
    if (aifc) {
        if (comm.size() < 22) {
            error = "AIFF-C file without a compression type";
            return false;
        }
        std::memcpy(type, p + 18, 4);
    }
    if (channels < 1 || channels > kMaxChannels) {
        error = "AIFF file with " + std::to_string(channels) + " channels";
        return false;
    }
    if (rate < 1.0 || rate > 10000000.0) {
        error = "AIFF file with an impossible sample rate";
        return false;
    }
    bool big = true, isFloat = false, unsigned8 = false;
    int bytes = (bits + 7) / 8;
    const uint8_t* t = reinterpret_cast<const uint8_t*>(type);
    if (tag(t, "NONE") || tag(t, "twos")) {
    } else if (tag(t, "sowt")) {
        big = false;
    } else if (tag(t, "raw ")) {
        unsigned8 = true; // offset binary, 8 bit only
        if (bits != 8) bytes = 0;
    } else if (tag(t, "in24")) {
        bytes = 3;
    } else if (tag(t, "in32")) {
        bytes = 4;
    } else if (tag(t, "fl32") || tag(t, "FL32")) {
        isFloat = true;
        bytes = 4;
    } else if (tag(t, "fl64") || tag(t, "FL64")) {
        isFloat = true;
        bytes = 8;
    } else {
        for (char& ch : type) ch = ch >= 0x20 && ch < 0x7f ? ch : '?';
        type[4] = 0;
        error = std::string("AIFF-C compression '") + type + "' is not supported";
        return false;
    }
    if (!isFloat && (bits < 1 || bits > 32 || bytes < 1 || bytes > 4)) {
        error = "AIFF file with " + std::to_string(bits) + "-bit samples";
        return false;
    }
    if (!haveData) {
        error = "AIFF file without sound data";
        return false;
    }
    const uint64_t frameBytes = static_cast<uint64_t>(bytes) * channels;
    uint64_t frames = dataSize / frameBytes;
    if (commFrames > 0 && commFrames < frames) frames = commFrames;
    if (frames > kMaxFrames) {
        error = "sound too long";
        return false;
    }
    info.format = "AIFF";
    info.channels = channels;
    info.sampleRate = rate;
    info.frames = frames;
    info.bits = isFloat ? bytes * 8 : bits;
    info.isFloat = isFloat;
    info.raw = true;
    info.dataOffset = dataPos;
    info.bytesPerSample = bytes;
    info.bigEndian = big;
    info.isUnsigned8 = unsigned8;
    if (meta) {
        describe(*meta, info);
        // MARK: id, position (frames before the marker), name as a Pascal string padded to an even length.
        struct Marker {
            int id;
            uint32_t position;
        };
        std::vector<Marker> markers;
        if (mark.size() >= 2) {
            const size_t count = be16(mark.data());
            size_t at = 2;
            for (size_t i = 0; i < count && markers.size() < kMaxCues && at + 7 <= mark.size(); ++i) {
                markers.push_back({be16(&mark[at]), be32(&mark[at + 2])});
                const size_t name = 1 + mark[at + 6];
                at += 6 + name + (name & 1);
            }
        }
        for (const Marker& m : markers) meta->cues.push_back(m.position);
        if (inst.size() >= 14) {
            const uint8_t* q = inst.data();
            meta->rootKey = q[0];
            meta->tuneCents = static_cast<int8_t>(q[1]);
            meta->loKey = q[2];
            meta->hiKey = q[3];
            meta->loVel = q[4];
            meta->hiVel = q[5];
            meta->gainDb = static_cast<int16_t>(be16(q + 6));
            const int mode = be16(q + 8), begin = be16(q + 10), end = be16(q + 12);
            const Marker* b = nullptr;
            const Marker* e = nullptr;
            for (const Marker& m : markers) {
                if (m.id == begin && !b) b = &m;
                if (m.id == end && !e) e = &m;
            }
            if ((mode == 1 || mode == 2) && b && e && e->position > b->position) {
                AudioData::Loop l;
                l.start = b->position;
                l.end = e->position;
                l.type = mode == 2 ? AudioData::kPingPong : AudioData::kForward;
                meta->loops.push_back(l);
            }
        }
    }
    return true;
}

// ---------------------------------------------------------------- PCM and float samples

/** One sample as it lies in a WAV or AIFF file, scaled so that full scale is -1..1. */
inline float rawValue(const uint8_t* p, int bytes, bool isFloat, bool big, bool unsigned8) {
    if (isFloat) {
        if (bytes == 4) {
            const uint32_t bits = big ? be32(p) : le32(p);
            float v;
            std::memcpy(&v, &bits, sizeof(v));
            if (!isFinite(v)) return 0.0f;
            return std::clamp(v, -kFloatLimit, kFloatLimit);
        }
        if (bytes == 8) {
            const uint64_t bits = big ? be64(p) : le64(p);
            double v;
            std::memcpy(&v, &bits, sizeof(v));
            if (!isFinite(v)) return 0.0f;
            return static_cast<float>(std::clamp(v, -static_cast<double>(kFloatLimit), static_cast<double>(kFloatLimit)));
        }
        return 0.0f;
    }
    switch (bytes) {
    case 1: return unsigned8 ? (static_cast<int>(p[0]) - 128) * (1.0f / 128.0f) : static_cast<int8_t>(p[0]) * (1.0f / 128.0f);
    case 2: return static_cast<int16_t>(big ? be16(p) : le16(p)) * (1.0f / 32768.0f);
    case 3: {
        const int32_t u = big ? (p[0] << 16) | (p[1] << 8) | p[2] : (p[2] << 16) | (p[1] << 8) | p[0];
        return static_cast<float>(((u ^ 0x800000) - 0x800000) * (1.0 / 8388608.0));
    }
    case 4: {
        const uint32_t u = big ? be32(p) : le32(p);
        const int64_t v = static_cast<int64_t>(u ^ 0x80000000u) - 0x80000000ll;
        return static_cast<float>(static_cast<double>(v) * (1.0 / 2147483648.0));
    }
    default: return 0.0f;
    }
}

/** Decodes whole frames of raw samples into L (and R when not null; a mono file is copied into both). */
inline void decodeRaw(const uint8_t* src, size_t frames, const AudioInfo& in, float* L, float* R) {
    const int b = in.bytesPerSample;
    const size_t stride = static_cast<size_t>(b) * static_cast<size_t>(in.channels);
    for (size_t i = 0; i < frames; ++i, src += stride) {
        L[i] = rawValue(src, b, in.isFloat, in.bigEndian, in.isUnsigned8);
        if (R) R[i] = in.channels > 1 ? rawValue(src + b, b, in.isFloat, in.bigEndian, in.isUnsigned8) : L[i];
    }
}

// ---------------------------------------------------------------- FLAC

inline uint8_t crc8(const uint8_t* p, size_t n) {
    static const std::array<uint8_t, 256> table = [] {
        std::array<uint8_t, 256> t{};
        for (unsigned i = 0; i < 256; ++i) {
            unsigned c = i;
            for (int k = 0; k < 8; ++k) c = (c & 0x80) ? ((c << 1) ^ 0x07) : (c << 1);
            t[i] = static_cast<uint8_t>(c);
        }
        return t;
    }();
    uint8_t c = 0;
    for (size_t i = 0; i < n; ++i) c = table[c ^ p[i]];
    return c;
}

inline uint16_t crc16(const uint8_t* p, size_t n) {
    static const std::array<uint16_t, 256> table = [] {
        std::array<uint16_t, 256> t{};
        for (unsigned i = 0; i < 256; ++i) {
            unsigned c = i << 8;
            for (int k = 0; k < 8; ++k) c = (c & 0x8000) ? ((c << 1) ^ 0x8005) : (c << 1);
            t[i] = static_cast<uint16_t>(c);
        }
        return t;
    }();
    uint16_t c = 0;
    for (size_t i = 0; i < n; ++i) c = static_cast<uint16_t>((c << 8) ^ table[(c >> 8) ^ p[i]]);
    return c;
}

/** Reads bits most significant first. Reading past the end gives zeros and sets `bad`. */
struct BitReader {
    const uint8_t* data;
    size_t size;
    uint64_t pos = 0; // in bits
    bool bad = false;

    BitReader(const uint8_t* d, size_t n) : data(d), size(n) {}

    /** n from 0 to 57 bits. */
    uint64_t bits(int n) {
        if (n <= 0) return 0;
        const uint64_t end = static_cast<uint64_t>(size) * 8;
        if (pos + static_cast<uint64_t>(n) > end) {
            bad = true;
            pos = end;
            return 0;
        }
        const size_t byte = static_cast<size_t>(pos >> 3);
        const int off = static_cast<int>(pos & 7);
        uint64_t w = 0;
        if (byte + 8 <= size) w = be64(data + byte);
        else
            for (size_t k = 0; k < 8; ++k) w = (w << 8) | (byte + k < size ? data[byte + k] : 0u);
        pos += static_cast<uint64_t>(n);
        return (w << off) >> (64 - n);
    }

    /** A two's complement number of n bits (0 to 57). */
    int64_t sbits(int n) {
        if (n <= 0) return 0;
        const uint64_t v = bits(n) << (64 - n);
        return static_cast<int64_t>(v) >> (64 - n);
    }

    /** The number of 0 bits before the next 1. */
    uint64_t unary() {
        uint64_t q = 0;
        const uint64_t end = static_cast<uint64_t>(size) * 8;
        while (pos < end) {
            const unsigned b = static_cast<uint8_t>(data[pos >> 3] << (pos & 7));
            if (b) {
                unsigned zeros = 0;
                for (unsigned m = 0x80; !(b & m); m >>= 1) ++zeros;
                pos += zeros + 1;
                return q + zeros;
            }
            const unsigned left = 8 - static_cast<unsigned>(pos & 7);
            q += left;
            pos += left;
        }
        bad = true;
        return 0;
    }

    void align() { pos = (pos + 7) & ~static_cast<uint64_t>(7); }
};

/** What STREAMINFO says. */
struct FlacStream {
    int channels = 0;
    int bits = 0;
    uint32_t sampleRate = 0;
    uint64_t total = 0; // frames, 0 when not known
    uint8_t md5[16] = {};
};

/** Reads the metadata of a FLAC stream; `start` becomes where its first frame begins. */
inline bool parseFlac(const Source& src, AudioInfo& info, AudioData* meta, FlacStream& si, uint64_t& start, std::string& error) {
    uint64_t pos = 0;
    uint8_t h[10];
    if (src.read(0, h, 10) && h[0] == 'I' && h[1] == 'D' && h[2] == '3') {
        if ((h[6] | h[7] | h[8] | h[9]) & 0x80) {
            error = "damaged ID3 tag before the FLAC stream";
            return false;
        }
        const uint64_t tagSize = (static_cast<uint64_t>(h[6]) << 21) | (h[7] << 14) | (h[8] << 7) | h[9];
        pos = 10 + tagSize + ((h[5] & 0x10) ? 10 : 0);
    }
    uint8_t magic[4];
    if (!src.read(pos, magic, 4) || !tag(magic, "fLaC")) {
        error = "not a FLAC file";
        return false;
    }
    pos += 4;
    bool haveInfo = false;
    for (size_t n = 0;; ++n) {
        uint8_t b[4];
        if (n >= kMaxChunks || !src.read(pos, b, 4)) {
            error = "FLAC file that ends inside its metadata";
            return false;
        }
        const bool last = (b[0] & 0x80) != 0;
        const int type = b[0] & 0x7f;
        const uint64_t length = (static_cast<uint64_t>(b[1]) << 16) | (b[2] << 8) | b[3];
        pos += 4;
        if (pos > src.size || length > src.size - pos) {
            error = "FLAC file that ends inside its metadata";
            return false;
        }
        if (n == 0 && (type != 0 || length < 34)) {
            error = "FLAC file without STREAMINFO";
            return false;
        }
        if (type == 127) {
            error = "damaged FLAC metadata";
            return false;
        }
        if (type == 0 && !haveInfo) {
            uint8_t s[34];
            if (!src.read(pos, s, 34)) {
                error = "FLAC file that ends inside its metadata";
                return false;
            }
            si.sampleRate = (static_cast<uint32_t>(s[10]) << 12) | (s[11] << 4) | (s[12] >> 4);
            si.channels = ((s[12] >> 1) & 7) + 1;
            si.bits = (((s[12] & 1) << 4) | (s[13] >> 4)) + 1;
            si.total = (static_cast<uint64_t>(s[13] & 0x0f) << 32) | be32(s + 14);
            std::memcpy(si.md5, s + 18, 16);
            haveInfo = true;
        } else if (type == 2 && meta && length >= 12) {
            // APPLICATION "riff": a chunk kept from the WAV file the FLAC was made from.
            std::vector<uint8_t> app;
            readBody(src, pos, length, app);
            if (app.size() >= 12 && tag(app.data(), "riff")) {
                const uint8_t* c = app.data() + 4;
                std::vector<uint8_t> body(app.begin() + 12, app.begin() + 12 + std::min<size_t>(le32(c + 4), app.size() - 12));
                if (tag(c, "smpl")) readSmpl(body, *meta);
                else if (tag(c, "inst")) readInst(body, *meta);
                else if (tag(c, "cue ")) readCue(body, *meta);
                else if (tag(c, "acid")) readAcid(body, *meta);
            }
        }
        pos += length;
        if (last) break;
    }
    if (si.sampleRate < 1 || si.bits < 4) {
        error = "FLAC file with an impossible sample rate or sample size";
        return false;
    }
    if (si.total > kMaxFrames) {
        error = "sound too long";
        return false;
    }
    start = pos;
    info.format = "FLAC";
    info.channels = si.channels;
    info.sampleRate = si.sampleRate;
    info.frames = si.total;
    info.bits = si.bits;
    info.isFloat = false;
    info.raw = false;
    if (meta) describe(*meta, info);
    return true;
}

enum FlacResult { kFlacOk = 0, kFlacShort, kFlacBad };

/** Decodes one subframe of n samples, each `bits` wide. Values that do not fit in `bits` make the stream invalid. */
inline FlacResult flacSubframe(BitReader& br, int64_t* out, size_t n, int bits) {
    if (br.bits(1) != 0) return kFlacBad;
    const unsigned type = static_cast<unsigned>(br.bits(6));
    int wasted = 0;
    if (br.bits(1)) {
        const uint64_t k = br.unary();
        if (k >= 64) return br.bad ? kFlacShort : kFlacBad;
        wasted = static_cast<int>(k) + 1;
    }
    if (br.bad) return kFlacShort;
    if (wasted >= bits) return kFlacBad;
    const int b = bits - wasted;
    const int64_t limit = static_cast<int64_t>(1) << (b - 1);
    size_t order = 0;
    if (type == 0) {
        const int64_t v = br.sbits(b);
        std::fill(out, out + n, v);
    } else if (type == 1) {
        for (size_t i = 0; i < n; ++i) out[i] = br.sbits(b);
    } else if ((type >= 8 && type <= 12) || type >= 32) {
        const bool lpc = type >= 32;
        order = lpc ? type - 31 : type - 8;
        if (order > n) return kFlacBad;
        for (size_t i = 0; i < order; ++i) out[i] = br.sbits(b);
        int64_t coef[32] = {};
        int shift = 0;
        if (lpc) {
            const int precision = static_cast<int>(br.bits(4)) + 1;
            if (precision == 16) return br.bad ? kFlacShort : kFlacBad;
            shift = static_cast<int>(br.sbits(5));
            if (shift < 0) return br.bad ? kFlacShort : kFlacBad;
            for (size_t j = 0; j < order; ++j) coef[j] = br.sbits(precision);
        }
        // Residual: Rice coded in 2^porder partitions, the first one shorter by the predictor order.
        const unsigned method = static_cast<unsigned>(br.bits(2));
        if (method > 1) return br.bad ? kFlacShort : kFlacBad;
        const int paramBits = method ? 5 : 4;
        const unsigned escape = method ? 31u : 15u;
        const unsigned porder = static_cast<unsigned>(br.bits(4));
        if (br.bad) return kFlacShort;
        const size_t per = n >> porder;
        if ((per << porder) != n || per < order) return kFlacBad;
        size_t i = order;
        for (size_t part = 0; part < (static_cast<size_t>(1) << porder); ++part) {
            const size_t count = part == 0 ? per - order : per;
            const unsigned param = static_cast<unsigned>(br.bits(paramBits));
            if (param == escape) {
                const int rb = static_cast<int>(br.bits(5));
                for (size_t k = 0; k < count; ++k) out[i++] = br.sbits(rb);
            } else {
                for (size_t k = 0; k < count; ++k) {
                    const uint64_t q = br.unary();
                    const uint64_t u = (q << param) | br.bits(static_cast<int>(param));
                    out[i++] = static_cast<int64_t>(u >> 1) ^ -static_cast<int64_t>(u & 1);
                }
            }
            if (br.bad) return kFlacShort;
        }
        // Prediction. Every sample is checked to fit in b bits, so with coefficients of at most 15 bits
        // and 32 of them the sums stay far inside 64 bits; the residual is added without overflow.
        for (size_t j = 0; j < order; ++j)
            if (out[j] < -limit || out[j] >= limit) return kFlacBad;
        for (size_t k = order; k < n; ++k) {
            int64_t p = 0;
            if (lpc) {
                for (size_t j = 0; j < order; ++j) p += coef[j] * out[k - 1 - j];
                p >>= shift;
            } else {
                switch (order) {
                case 1: p = out[k - 1]; break;
                case 2: p = 2 * out[k - 1] - out[k - 2]; break;
                case 3: p = 3 * out[k - 1] - 3 * out[k - 2] + out[k - 3]; break;
                case 4: p = 4 * out[k - 1] - 6 * out[k - 2] + 4 * out[k - 3] - out[k - 4]; break;
                default: break;
                }
            }
            const int64_t s = static_cast<int64_t>(static_cast<uint64_t>(out[k]) + static_cast<uint64_t>(p));
            if (s < -limit || s >= limit) return kFlacBad;
            out[k] = s;
        }
    } else {
        return kFlacBad;
    }
    if (br.bad) return kFlacShort;
    if (wasted)
        for (size_t i = 0; i < n; ++i) out[i] *= static_cast<int64_t>(1) << wasted;
    return kFlacOk;
}

/**
 * Decodes the frames of a FLAC stream from `start`, handing each block to
 * sink(const int64_t* const* channels, int channelCount, size_t blockSize, int bits), which returns false to stop
 * with an error. A stream cut off inside a frame keeps the frames before it; a frame that fails its checks is an
 * error.
 */
template <typename Sink>
inline bool decodeFlacFrames(const uint8_t* data, size_t size, size_t start, const FlacStream& si, Sink&& sink, std::string& error) {
    std::vector<int64_t> buf[8];
    const int64_t* ptr[8];
    size_t pos = start, frames = 0;
    uint64_t decoded = 0;
    static const uint8_t kBits[8] = {0, 8, 12, 0, 16, 20, 24, 32};
    auto cutShort = [&]() {
        if (frames > 0) return true;
        error = "FLAC file that ends before its first frame is complete";
        return false;
    };
    while (pos < size) {
        if (si.total && decoded >= si.total) break;
        if (size - pos < 2 || data[pos] != 0xFF || (data[pos + 1] & 0xFE) != 0xF8) {
            if (frames > 0 && (si.total == 0 || size - pos < 2)) break; // whatever follows the last frame
            error = frames == 0 ? "FLAC file without a frame where its sound should start" : "damaged FLAC file: a frame is missing";
            return false;
        }
        BitReader br(data + pos, size - pos);
        br.bits(16);
        const unsigned bsCode = static_cast<unsigned>(br.bits(4)), srCode = static_cast<unsigned>(br.bits(4));
        const unsigned chCode = static_cast<unsigned>(br.bits(4)), bpsCode = static_cast<unsigned>(br.bits(3));
        const unsigned reserved = static_cast<unsigned>(br.bits(1));
        const unsigned first = static_cast<unsigned>(br.bits(8));
        int follow = -1;
        if (first < 0x80) follow = 0;
        else if ((first & 0xE0) == 0xC0) follow = 1;
        else if ((first & 0xF0) == 0xE0) follow = 2;
        else if ((first & 0xF8) == 0xF0) follow = 3;
        else if ((first & 0xFC) == 0xF8) follow = 4;
        else if ((first & 0xFE) == 0xFC) follow = 5;
        else if (first == 0xFE) follow = 6;
        bool valid = follow >= 0 && reserved == 0 && bsCode != 0 && srCode != 15 && chCode <= 10 && bpsCode != 3;
        for (int k = 0; k < follow; ++k)
            if ((br.bits(8) & 0xC0) != 0x80) valid = false;
        size_t blockSize = 0;
        if (bsCode == 1) blockSize = 192;
        else if (bsCode >= 2 && bsCode <= 5) blockSize = static_cast<size_t>(576) << (bsCode - 2);
        else if (bsCode == 6) blockSize = static_cast<size_t>(br.bits(8)) + 1;
        else if (bsCode == 7) blockSize = static_cast<size_t>(br.bits(16)) + 1;
        else if (bsCode >= 8) blockSize = static_cast<size_t>(256) << (bsCode - 8);
        if (srCode == 12) br.bits(8);
        else if (srCode == 13 || srCode == 14) br.bits(16);
        if (br.bad) return cutShort();
        const size_t headerBytes = static_cast<size_t>(br.pos >> 3);
        const unsigned crc = static_cast<unsigned>(br.bits(8));
        if (br.bad) return cutShort();
        if (!valid || crc != crc8(data + pos, headerBytes)) {
            error = "damaged FLAC frame header";
            return false;
        }
        const int channels = chCode <= 7 ? static_cast<int>(chCode) + 1 : 2;
        const int bits = bpsCode == 0 ? si.bits : kBits[bpsCode];
        if (channels != si.channels) {
            error = "FLAC stream that changes its number of channels";
            return false;
        }
        for (int c = 0; c < channels; ++c) {
            if (buf[c].size() < blockSize) buf[c].resize(blockSize);
            ptr[c] = buf[c].data();
        }
        for (int c = 0; c < channels; ++c) {
            const bool side = (chCode == 8 && c == 1) || (chCode == 9 && c == 0) || (chCode == 10 && c == 1);
            const FlacResult r = flacSubframe(br, buf[c].data(), blockSize, bits + (side ? 1 : 0));
            if (r == kFlacShort) return cutShort();
            if (r == kFlacBad) {
                error = "damaged FLAC frame";
                return false;
            }
        }
        br.align();
        const size_t frameBytes = static_cast<size_t>(br.pos >> 3);
        const unsigned check = static_cast<unsigned>(br.bits(16));
        if (br.bad) return cutShort();
        if (check != crc16(data + pos, frameBytes)) {
            error = "damaged FLAC frame (its checksum does not match)";
            return false;
        }
        if (chCode >= 8) {
            int64_t* a = buf[0].data();
            int64_t* b = buf[1].data();
            for (size_t i = 0; i < blockSize; ++i) {
                if (chCode == 8) b[i] = a[i] - b[i];      // left, side: right = left - side
                else if (chCode == 9) a[i] = a[i] + b[i]; // side, right: left = side + right
                else {                                    // mid, side
                    const int64_t mid = a[i] * 2 + (b[i] & 1), s = b[i];
                    a[i] = (mid + s) >> 1;
                    b[i] = (mid - s) >> 1;
                }
            }
        }
        if (!sink(static_cast<const int64_t* const*>(ptr), channels, blockSize, bits)) return false;
        decoded += blockSize;
        ++frames;
        pos += frameBytes + 2;
    }
    if (frames == 0 && si.total > 0) {
        error = "FLAC file without the frames its STREAMINFO announces";
        return false;
    }
    return true;
}

/** Decodes a whole FLAC file in memory into `out` (which is cleared first). */
inline bool decodeFlac(const uint8_t* data, size_t size, AudioData& out, std::string& error) {
    const Source src{data, nullptr, size};
    AudioInfo info;
    FlacStream si;
    uint64_t start = 0;
    if (!parseFlac(src, info, &out, si, start, error)) return false;
    const int keep = std::min(si.channels, 2);
    if (!fitsInMemory(si.total, keep)) {
        error = kTooLarge;
        return false;
    }
    const size_t guess = static_cast<size_t>(std::min<uint64_t>(si.total, std::min<uint64_t>(kMaxFrames, size)));
    for (int c = 0; c < keep; ++c) out.samples[c].reserve(guess);
    auto sink = [&](const int64_t* const* ch, int, size_t n, int bits) {
        const size_t at = out.samples[0].size();
        if (!fitsInMemory(at + n, keep)) { // also when the file gave no length, or a wrong one
            error = kTooLarge;
            return false;
        }
        const double scale = 1.0 / static_cast<double>(static_cast<int64_t>(1) << (bits - 1));
        for (int c = 0; c < keep; ++c) {
            out.samples[c].resize(at + n);
            float* dst = out.samples[c].data() + at;
            for (size_t i = 0; i < n; ++i) dst[i] = static_cast<float>(std::clamp(static_cast<double>(ch[c][i]) * scale, -1.0, 1.0));
        }
        return true;
    };
    if (!decodeFlacFrames(data, size, static_cast<size_t>(start), si, sink, error)) {
        out = AudioData{};
        return false;
    }
    finishMeta(out, out.frames(), true);
    return true;
}

enum Format { kUnknown, kWav, kAiff, kFlac };

inline Format detect(const Source& src) {
    uint8_t h[12];
    if (!src.read(0, h, 4)) return kUnknown;
    if (tag(h, "fLaC") || (h[0] == 'I' && h[1] == 'D' && h[2] == '3')) return kFlac;
    if (!src.read(0, h, 12)) return kUnknown;
    if ((tag(h, "RIFF") || tag(h, "RF64") || tag(h, "BW64")) && tag(h + 8, "WAVE")) return kWav;
    if (tag(h, "FORM") && (tag(h + 8, "AIFF") || tag(h + 8, "AIFC"))) return kAiff;
    return kUnknown;
}

/** Probes any of the formats. `meta`, when not null, gets the metadata (finished against info.frames). */
inline bool probe(const Source& src, AudioInfo& info, AudioData* meta, std::string& error) {
    info = AudioInfo{};
    if (meta) *meta = AudioData{};
    bool ok = false;
    switch (detect(src)) {
    case kWav: ok = parseWav(src, info, meta, error); break;
    case kAiff: ok = parseAiff(src, info, meta, error); break;
    case kFlac: {
        FlacStream si;
        uint64_t start = 0;
        ok = parseFlac(src, info, meta, si, start, error);
        break;
    }
    default: error = src.size < 12 ? "file too short to be a sound" : "not a WAV, AIFF or FLAC file"; break;
    }
    if (!ok) {
        info = AudioInfo{};
        if (meta) *meta = AudioData{};
        return false;
    }
    if (meta) finishMeta(*meta, info.frames, info.raw || info.frames > 0);
    return true;
}

} // namespace audiofile

/** True when the name ends in an extension this reader takes (.wav .wave .aif .aiff .aifc .flac), any capitals. */
inline bool isAudioFileName(const std::string& name) {
    const size_t dot = name.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string ext = name.substr(dot + 1);
    for (char& c : ext) c = c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c;
    return ext == "wav" || ext == "wave" || ext == "aif" || ext == "aiff" || ext == "aifc" || ext == "flac";
}

/** The same as decodeAudio without decoding the samples (metadata such as loops and root key is still filled in `meta` when not null). */
inline bool probeAudio(const uint8_t* data, size_t size, AudioInfo& info, AudioData* meta, std::string& error) {
    try {
        if (!data) size = 0;
        return audiofile::probe(audiofile::Source{data, nullptr, size}, info, meta, error);
    } catch (const std::bad_alloc&) {
        error = "not enough memory to read this file";
    } catch (...) {
        error = "could not read this file";
    }
    info = AudioInfo{};
    if (meta) *meta = AudioData{};
    return false;
}

/** Decodes a whole file from memory. False with a short reason in `error` when it cannot. Never throws, never crashes on bad data. */
inline bool decodeAudio(const uint8_t* data, size_t size, AudioData& out, std::string& error) {
    try {
        out = AudioData{};
        if (!data) size = 0;
        const audiofile::Source src{data, nullptr, size};
        if (audiofile::detect(src) == audiofile::kFlac) return audiofile::decodeFlac(data, size, out, error);
        AudioInfo info;
        if (!audiofile::probe(src, info, &out, error)) return false;
        if (!audiofile::fitsInMemory(info.frames, info.channels)) {
            error = audiofile::kTooLarge;
            out = AudioData{};
            return false;
        }
        const size_t frames = static_cast<size_t>(info.frames);
        out.samples[0].resize(frames);
        if (info.channels > 1) out.samples[1].resize(frames);
        audiofile::decodeRaw(data + info.dataOffset, frames, info, out.samples[0].data(), info.channels > 1 ? out.samples[1].data() : nullptr);
        return true;
    } catch (const std::bad_alloc&) {
        error = "not enough memory to read this file";
    } catch (...) {
        error = "could not read this file";
    }
    out = AudioData{};
    return false;
}

/** Decodes frames [first, first+count) of a raw (AudioInfo::raw) file into outL and outR (outR may be null); for streaming. Returns frames read. */
inline size_t readRawFrames(std::FILE* f, const AudioInfo& info, uint64_t first, size_t count, float* outL, float* outR) {
    if (!f || !outL || !info.raw || info.channels < 1 || info.bytesPerSample < 1 || info.bytesPerSample > 8 || first >= info.frames) return 0;
    count = static_cast<size_t>(std::min<uint64_t>(count, info.frames - first));
    uint8_t buf[16384];
    const size_t stride = static_cast<size_t>(info.bytesPerSample) * static_cast<size_t>(info.channels);
    if (stride > sizeof(buf) || first > (UINT64_MAX - info.dataOffset) / stride) return 0;
    if (!audiofile::seekTo(f, info.dataOffset + first * stride)) return 0;
    const size_t per = sizeof(buf) / stride;
    size_t done = 0;
    while (done < count) {
        const size_t want = std::min(per, count - done);
        const size_t got = std::fread(buf, stride, want, f);
        audiofile::decodeRaw(buf, got, info, outL + done, outR ? outR + done : nullptr);
        done += got;
        if (got < want) break;
    }
    return done;
}

/** From disk; reads only as much of the file as it needs where the format allows (WAV and AIFF; FLAC up to its first frame). */
inline bool probeAudioFile(const std::filesystem::path& file, AudioInfo& info, AudioData* meta, std::string& error) {
    info = AudioInfo{};
    if (meta) *meta = AudioData{};
    std::FILE* f = nullptr;
    try {
        f = audiofile::openFile(file, false);
        if (!f) {
            error = "could not open the file";
            return false;
        }
        audiofile::Source src{nullptr, f, 0};
        bool ok = audiofile::fileSize(f, src.size);
        if (!ok) error = "could not read the file";
        else ok = audiofile::probe(src, info, meta, error);
        std::fclose(f);
        return ok;
    } catch (const std::bad_alloc&) {
        error = "not enough memory to read this file";
    } catch (...) {
        error = "could not read this file";
    }
    if (f) std::fclose(f);
    info = AudioInfo{};
    if (meta) *meta = AudioData{};
    return false;
}

/** From disk. */
inline bool readAudioFile(const std::filesystem::path& file, AudioData& out, std::string& error) {
    out = AudioData{};
    std::FILE* f = nullptr;
    try {
        f = audiofile::openFile(file, false);
        if (!f) {
            error = "could not open the file";
            return false;
        }
        audiofile::Source src{nullptr, f, 0};
        bool ok = audiofile::fileSize(f, src.size);
        if (!ok) error = "could not read the file";
        else if (audiofile::detect(src) == audiofile::kFlac) {
            if (src.size > audiofile::kMaxFlacFile) {
                error = "FLAC file too large";
                ok = false;
            } else {
                std::vector<uint8_t> bytes(static_cast<size_t>(src.size));
                ok = src.read(0, bytes.data(), bytes.size());
                if (!ok) error = "could not read the file";
                else ok = audiofile::decodeFlac(bytes.data(), bytes.size(), out, error);
            }
        } else {
            AudioInfo info;
            ok = audiofile::probe(src, info, &out, error);
            if (ok && !audiofile::fitsInMemory(info.frames, info.channels)) {
                error = audiofile::kTooLarge;
                ok = false;
            }
            if (ok) {
                const size_t frames = static_cast<size_t>(info.frames);
                out.samples[0].resize(frames);
                if (info.channels > 1) out.samples[1].resize(frames);
                const size_t got = readRawFrames(f, info, 0, frames, out.samples[0].data(), info.channels > 1 ? out.samples[1].data() : nullptr);
                if (got < frames) {
                    out.samples[0].resize(got);
                    if (info.channels > 1) out.samples[1].resize(got);
                    audiofile::finishMeta(out, got, true);
                }
            }
        }
        std::fclose(f);
        if (!ok) out = AudioData{};
        return ok;
    } catch (const std::bad_alloc&) {
        error = "not enough memory to read this file";
    } catch (...) {
        error = "could not read this file";
    }
    if (f) std::fclose(f);
    out = AudioData{};
    return false;
}

/** Writes a WAV file: 16 or 24 bit PCM or 32 bit float, with a smpl chunk when the data has a loop or a root key. */
inline bool writeWav(const std::filesystem::path& file, const AudioData& data, int bits, std::string& error) {
    using namespace audiofile;
    if (bits != 16 && bits != 24 && bits != 32) {
        error = "WAV files are written with 16, 24 or 32 bits";
        return false;
    }
    if (data.channels < 1 || !isFinite(data.sampleRate) || data.sampleRate < 1.0 || data.sampleRate > 4.0e9) {
        error = "nothing to write: no channels or no sample rate";
        return false;
    }
    const int channels = data.channels >= 2 && data.samples[1].size() == data.samples[0].size() ? 2 : 1;
    const uint64_t frames = data.samples[0].size();
    const uint32_t bytes = static_cast<uint32_t>(bits / 8), blockAlign = bytes * static_cast<uint32_t>(channels);
    const uint32_t rate = static_cast<uint32_t>(data.sampleRate + 0.5);
    std::vector<AudioData::Loop> loops;
    for (const AudioData::Loop& l : data.loops)
        if (l.end > l.start && l.end <= frames && loops.size() < kMaxLoops) loops.push_back(l);
    const bool smpl = !loops.empty() || (data.rootKey >= 0 && data.rootKey <= 127);
    const uint64_t dataBytes = frames * blockAlign;
    const uint32_t smplBytes = smpl ? static_cast<uint32_t>(36 + 24 * loops.size()) : 0;
    const uint32_t fmtBytes = bits == 32 ? 18 : 16, factBytes = bits == 32 ? 4 : 0;
    const uint64_t riff = 4 + 8 + fmtBytes + (factBytes ? 8 + factBytes : 0) + 8 + dataBytes + (dataBytes & 1) + (smpl ? 8 + smplBytes : 0);
    if (riff > 0xFFFFFFFFu) {
        error = "sound too long for a WAV file";
        return false;
    }
    std::vector<uint8_t> out;
    auto put16 = [&](uint32_t v) {
        out.push_back(static_cast<uint8_t>(v));
        out.push_back(static_cast<uint8_t>(v >> 8));
    };
    auto put32 = [&](uint32_t v) {
        put16(v & 0xFFFF);
        put16(v >> 16);
    };
    auto putTag = [&](const char* t) { out.insert(out.end(), t, t + 4); };
    std::FILE* f = nullptr;
    try {
        putTag("RIFF");
        put32(static_cast<uint32_t>(riff));
        putTag("WAVE");
        putTag("fmt ");
        put32(fmtBytes);
        put16(bits == 32 ? 3 : 1);
        put16(static_cast<uint32_t>(channels));
        put32(rate);
        put32(rate * blockAlign);
        put16(blockAlign);
        put16(static_cast<uint32_t>(bits));
        if (bits == 32) {
            put16(0);
            putTag("fact");
            put32(4);
            put32(static_cast<uint32_t>(frames));
        }
        putTag("data");
        put32(static_cast<uint32_t>(dataBytes));
        f = openFile(file, true);
        if (!f) {
            error = "could not create the file";
            return false;
        }
        bool ok = std::fwrite(out.data(), 1, out.size(), f) == out.size();
        std::vector<uint8_t> block;
        block.reserve(4096 * blockAlign);
        const double full = bits == 16 ? 32768.0 : 8388608.0;
        for (uint64_t i = 0; ok && i < frames; i += 4096) {
            block.clear();
            const uint64_t end = std::min<uint64_t>(frames, i + 4096);
            for (uint64_t k = i; k < end; ++k) {
                for (int c = 0; c < channels; ++c) {
                    float v = data.samples[c][static_cast<size_t>(k)];
                    if (!isFinite(v)) v = 0.0f;
                    if (bits == 32) {
                        uint32_t u;
                        std::memcpy(&u, &v, sizeof(u));
                        for (int s = 0; s < 32; s += 8) block.push_back(static_cast<uint8_t>(u >> s));
                    } else {
                        const double x = std::floor(std::clamp(static_cast<double>(v), -1.0, 1.0) * full + 0.5);
                        const int32_t s = static_cast<int32_t>(std::clamp(x, -full, full - 1.0));
                        const uint32_t u = static_cast<uint32_t>(s);
                        for (uint32_t b = 0; b < bytes; ++b) block.push_back(static_cast<uint8_t>(u >> (8 * b)));
                    }
                }
            }
            ok = std::fwrite(block.data(), 1, block.size(), f) == block.size();
        }
        out.clear();
        if (dataBytes & 1) out.push_back(0);
        if (smpl) {
            int note = data.rootKey >= 0 && data.rootKey <= 127 ? data.rootKey : 60;
            double cents = isFinite(data.tuneCents) ? std::clamp(data.tuneCents, -99.0, 99.0) : 0.0;
            if (cents < 0.0) {
                if (note > 0) {
                    --note;
                    cents += 100.0;
                } else {
                    cents = 0.0;
                }
            }
            putTag("smpl");
            put32(smplBytes);
            put32(0);
            put32(0);
            put32(static_cast<uint32_t>(1.0e9 / data.sampleRate + 0.5));
            put32(static_cast<uint32_t>(note));
            put32(static_cast<uint32_t>(std::min(cents / 100.0 * 4294967296.0, 4294967295.0)));
            put32(0);
            put32(0);
            put32(static_cast<uint32_t>(loops.size()));
            put32(0);
            for (size_t i = 0; i < loops.size(); ++i) {
                put32(static_cast<uint32_t>(i));
                put32(loops[i].type == AudioData::kPingPong ? 1u : loops[i].type == AudioData::kBackward ? 2u : 0u);
                put32(static_cast<uint32_t>(loops[i].start));
                put32(static_cast<uint32_t>(loops[i].end - 1));
                put32(0);
                put32(0);
            }
        }
        if (ok && !out.empty()) ok = std::fwrite(out.data(), 1, out.size(), f) == out.size();
        if (std::fclose(f) != 0) ok = false;
        f = nullptr;
        if (!ok) {
            std::error_code ec;
            std::filesystem::remove(file, ec);
            error = "could not write the file";
        }
        return ok;
    } catch (const std::bad_alloc&) {
        error = "not enough memory to write this file";
    } catch (...) {
        error = "could not write the file";
    }
    if (f) {
        std::fclose(f);
        std::error_code ec;
        std::filesystem::remove(file, ec);
    }
    return false;
}

} // namespace sampler
} // namespace hnl
