// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// Tests of the sound file reader. Every file is built here in memory (the FLAC
// files by a small encoder below), so the repository holds no binary fixtures.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "../dsp/AudioFile.hpp"

using namespace hnl;
using namespace hnl::sampler;
namespace fs = std::filesystem;

static int failures = 0, passes = 0;

static void check(bool ok, const std::string& name, const char* fmt, double a = 0, double b = 0, double c = 0, double d = 0) {
    std::printf("%s  %-52s ", ok ? "PASS" : "FAIL", name.c_str());
    std::printf(fmt, a, b, c, d);
    std::printf("\n");
    if (!ok) ++failures;
    else ++passes;
}

typedef std::vector<uint8_t> Bytes;

struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed) {}
    uint64_t next() {
        uint64_t z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    uint64_t below(uint64_t n) { return n ? next() % n : 0; }
    int64_t range(int64_t lo, int64_t hi) { return lo + static_cast<int64_t>(below(static_cast<uint64_t>(hi - lo + 1))); }
};

// ---------------------------------------------------------------- building files

static void putLe(Bytes& b, uint64_t v, int n) {
    for (int i = 0; i < n; ++i) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
static void putBe(Bytes& b, uint64_t v, int n) {
    for (int i = n - 1; i >= 0; --i) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
static void putTag(Bytes& b, const char* t) { b.insert(b.end(), t, t + 4); }

static Bytes chunk(const char* id, const Bytes& body, bool big = false, int64_t sizeField = -1) {
    Bytes b;
    putTag(b, id);
    const uint64_t size = sizeField >= 0 ? static_cast<uint64_t>(sizeField) : body.size();
    if (big) putBe(b, size, 4);
    else putLe(b, size, 4);
    b.insert(b.end(), body.begin(), body.end());
    if (body.size() & 1) b.push_back(0);
    return b;
}

static Bytes container(const char* form, const char* kind, const std::vector<Bytes>& chunks, bool big = false) {
    Bytes b;
    putTag(b, form);
    size_t size = 4;
    for (const Bytes& c : chunks) size += c.size();
    if (big) putBe(b, size, 4);
    else putLe(b, size, 4);
    putTag(b, kind);
    for (const Bytes& c : chunks) b.insert(b.end(), c.begin(), c.end());
    return b;
}

static Bytes fmtChunk(int code, int channels, uint32_t rate, int bits, int blockAlign) {
    Bytes f;
    putLe(f, static_cast<uint64_t>(code), 2);
    putLe(f, static_cast<uint64_t>(channels), 2);
    putLe(f, rate, 4);
    putLe(f, static_cast<uint64_t>(rate) * static_cast<uint64_t>(blockAlign), 4);
    putLe(f, static_cast<uint64_t>(blockAlign), 2);
    putLe(f, static_cast<uint64_t>(bits), 2);
    return chunk("fmt ", f);
}

static Bytes extensibleChunk(int sub, int channels, uint32_t rate, int container, int valid, bool goodGuid = true) {
    Bytes f;
    const int align = channels * container / 8;
    putLe(f, 0xFFFE, 2);
    putLe(f, static_cast<uint64_t>(channels), 2);
    putLe(f, rate, 4);
    putLe(f, static_cast<uint64_t>(rate) * static_cast<uint64_t>(align), 4);
    putLe(f, static_cast<uint64_t>(align), 2);
    putLe(f, static_cast<uint64_t>(container), 2);
    putLe(f, 22, 2);
    putLe(f, static_cast<uint64_t>(valid), 2);
    putLe(f, 3, 4);
    putLe(f, static_cast<uint64_t>(sub), 2);
    const uint8_t tail[14] = {0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, static_cast<uint8_t>(goodGuid ? 0x71 : 0x72)};
    f.insert(f.end(), tail, tail + 14);
    return chunk("fmt ", f);
}

/** Integer samples, interleaved, as `bytes`-wide numbers. */
static Bytes pcm(const std::vector<int64_t>& v, int bytes, bool big) {
    Bytes b;
    for (int64_t x : v) {
        if (big) putBe(b, static_cast<uint64_t>(x), bytes);
        else putLe(b, static_cast<uint64_t>(x), bytes);
    }
    return b;
}

static Bytes floats(const std::vector<double>& v, int bytes, bool big) {
    Bytes b;
    for (double x : v) {
        uint64_t u;
        if (bytes == 4) {
            const float f = static_cast<float>(x);
            uint32_t w;
            std::memcpy(&w, &f, 4);
            u = w;
        } else {
            std::memcpy(&u, &x, 8);
        }
        if (big) putBe(b, u, bytes);
        else putLe(b, u, bytes);
    }
    return b;
}

static Bytes smplChunk(int note, uint32_t fraction, const std::vector<std::array<uint32_t, 3>>& loops, uint32_t countField = 0xFFFFFFFFu) {
    Bytes s;
    putLe(s, 0, 4);
    putLe(s, 0, 4);
    putLe(s, 22675, 4);
    putLe(s, static_cast<uint64_t>(note), 4);
    putLe(s, fraction, 4);
    putLe(s, 0, 4);
    putLe(s, 0, 4);
    putLe(s, countField == 0xFFFFFFFFu ? loops.size() : countField, 4);
    putLe(s, 0, 4);
    for (size_t i = 0; i < loops.size(); ++i) {
        putLe(s, i, 4);
        putLe(s, loops[i][0], 4);
        putLe(s, loops[i][1], 4);
        putLe(s, loops[i][2], 4);
        putLe(s, 0, 4);
        putLe(s, 0, 4);
    }
    return chunk("smpl", s);
}

static Bytes cueChunk(const std::vector<uint32_t>& at, uint32_t countField = 0xFFFFFFFFu) {
    Bytes c;
    putLe(c, countField == 0xFFFFFFFFu ? at.size() : countField, 4);
    for (size_t i = 0; i < at.size(); ++i) {
        putLe(c, i + 1, 4);
        putLe(c, at[i], 4);
        putTag(c, "data");
        putLe(c, 0, 4);
        putLe(c, 0, 4);
        putLe(c, at[i], 4);
    }
    return chunk("cue ", c);
}

static void putExtended(Bytes& b, double rate) {
    int e = 0;
    const double f = std::frexp(rate, &e);
    putBe(b, static_cast<uint64_t>(e + 16382), 2);
    putBe(b, static_cast<uint64_t>(std::ldexp(f, 64)), 8);
}

static Bytes commChunk(int channels, uint32_t frames, int bits, double rate, const char* type = nullptr) {
    Bytes c;
    putBe(c, static_cast<uint64_t>(channels), 2);
    putBe(c, frames, 4);
    putBe(c, static_cast<uint64_t>(bits), 2);
    putExtended(c, rate);
    if (type) {
        putTag(c, type);
        c.push_back(3);
        c.insert(c.end(), {'a', 'b', 'c'});
    }
    return chunk("COMM", c, true);
}

static Bytes ssndChunk(const Bytes& data, uint32_t offset = 0, int64_t sizeField = -1) {
    Bytes s;
    putBe(s, offset, 4);
    putBe(s, 0, 4);
    for (uint32_t i = 0; i < offset; ++i) s.push_back(0xEE);
    s.insert(s.end(), data.begin(), data.end());
    return chunk("SSND", s, true, sizeField);
}

static float pcmValue(int64_t v, int bytes) { return static_cast<float>(static_cast<double>(v) / static_cast<double>(int64_t(1) << (8 * bytes - 1))); }

/** True when `d` holds exactly these channels of samples. */
static bool same(const AudioData& d, const std::vector<std::vector<float>>& want, size_t* bad = nullptr) {
    size_t wrong = 0;
    if (d.channels != static_cast<int>(want.size()) || d.samples[0].size() != want[0].size()) wrong = 1000000;
    else
        for (size_t c = 0; c < want.size(); ++c)
            for (size_t i = 0; i < want[c].size(); ++i) wrong += d.samples[c][i] != want[c][i] ? 1 : 0;
    if (want.size() == 1 && !d.samples[1].empty()) ++wrong;
    if (bad) *bad = wrong;
    return wrong == 0;
}

static bool decode(const Bytes& b, AudioData& d, std::string& err) { return decodeAudio(b.data(), b.size(), d, err); }

// ---------------------------------------------------------------- WAV

static void testWavFormats() {
    Rng rng(1);
    std::string err;
    // PCM of every width, stereo, with the extremes.
    for (int bytes = 1; bytes <= 4; ++bytes) {
        const int64_t lo = -(int64_t(1) << (8 * bytes - 1)), hi = (int64_t(1) << (8 * bytes - 1)) - 1;
        std::vector<int64_t> v = {lo, hi, 0, -1, 1, lo + 1, hi - 1};
        while (v.size() < 2 * 301) v.push_back(rng.range(lo, hi));
        std::vector<std::vector<float>> want(2);
        for (size_t i = 0; i < v.size(); ++i) want[i & 1].push_back(pcmValue(v[i], bytes));
        std::vector<int64_t> stored = v;
        if (bytes == 1)
            for (int64_t& x : stored) x += 128; // 8-bit WAV is unsigned
        const Bytes wav = container("RIFF", "WAVE", {fmtChunk(1, 2, 44100, 8 * bytes, 2 * bytes), chunk("data", pcm(stored, bytes, false))});
        AudioData d;
        const bool ok = decode(wav, d, err);
        size_t bad = 0;
        check(ok && same(d, want, &bad) && d.bits == 8 * bytes && !d.isFloat && d.sampleRate == 44100 && d.format == "WAV",
              "WAV PCM " + std::to_string(8 * bytes) + " bit, exact", "%.0f of %.0f samples differ", static_cast<double>(bad),
              static_cast<double>(v.size()));
        AudioInfo info;
        probeAudio(wav.data(), wav.size(), info, nullptr, err);
        check(info.raw && info.dataOffset == 12 + 24 + 8 && info.bytesPerSample == bytes && info.frames == 301 && info.isUnsigned8 == (bytes == 1) &&
                  !info.bigEndian,
              "  probe gives where the samples lie", "offset %.0f, %.0f bytes per sample", static_cast<double>(info.dataOffset), info.bytesPerSample);
    }
    // Float 32 and 64; what is not a number becomes 0, beyond full scale is held at it.
    for (int bytes : {4, 8}) {
        std::vector<double> v = {0.5, -0.25, 1.0, -1.0, 0.0, 1e-40, 0.0, 0.0, 0.0, 2.0, -3.0, 0.123456789};
        while (v.size() < 200) v.push_back(static_cast<double>(rng.range(-1000000, 1000000)) / 1000000.0);
        std::vector<std::vector<float>> want(1);
        for (size_t i = 0; i < v.size(); ++i) {
            const double x = i >= 6 && i <= 8 ? 0.0 : v[i]; // these three become NaN and infinities below
            const double y = bytes == 4 ? static_cast<double>(static_cast<float>(x)) : x;
            want[0].push_back(static_cast<float>(std::max(-1.0, std::min(1.0, y))));
        }
        // NaN and infinity are put in by their bits, so that -ffast-math cannot change them.
        Bytes data = floats(v, bytes, false);
        auto setBits = [&](size_t i, uint64_t bits) {
            for (int k = 0; k < bytes; ++k) data[i * bytes + k] = static_cast<uint8_t>(bits >> (8 * k));
        };
        setBits(6, bytes == 4 ? 0x7FC00000u : 0x7FF8000000000000ull);
        setBits(7, bytes == 4 ? 0x7F800000u : 0x7FF0000000000000ull);
        setBits(8, bytes == 4 ? 0xFF800000u : 0xFFF0000000000000ull);
        want[0][6] = want[0][7] = want[0][8] = 0.0f;
        const Bytes wav = container("RIFF", "WAVE", {fmtChunk(3, 1, 48000, 8 * bytes, bytes), chunk("data", data)});
        AudioData d;
        const bool ok = decode(wav, d, err);
        size_t bad = 0;
        check(ok && same(d, want, &bad) && d.isFloat && d.bits == 8 * bytes, "WAV float " + std::to_string(8 * bytes) + " bit, NaN and inf become 0",
              "%.0f samples differ", static_cast<double>(bad));
    }
    // WAVE_FORMAT_EXTENSIBLE: 24 bits valid in 32, 20 valid in 24, float.
    {
        std::vector<int64_t> v;
        std::vector<std::vector<float>> want(2);
        for (int i = 0; i < 200; ++i) {
            const int64_t x = rng.range(-(int64_t(1) << 23), (int64_t(1) << 23) - 1) * 256;
            v.push_back(x);
            want[i & 1].push_back(pcmValue(x, 4));
        }
        AudioData d;
        bool ok = decode(container("RIFF", "WAVE", {extensibleChunk(1, 2, 96000, 32, 24), chunk("data", pcm(v, 4, false))}), d, err);
        check(ok && same(d, want) && d.bits == 24 && d.sampleRate == 96000, "WAV extensible PCM 24 in 32", "bits %.0f", d.bits);
        std::vector<int64_t> w;
        std::vector<std::vector<float>> want2(1);
        for (int i = 0; i < 100; ++i) {
            const int64_t x = rng.range(-(int64_t(1) << 19), (int64_t(1) << 19) - 1) * 16;
            w.push_back(x);
            want2[0].push_back(pcmValue(x, 3));
        }
        ok = decode(container("RIFF", "WAVE", {extensibleChunk(1, 1, 44100, 24, 20), chunk("data", pcm(w, 3, false))}), d, err);
        check(ok && same(d, want2) && d.bits == 20, "WAV extensible PCM 20 in 24", "bits %.0f", d.bits);
        ok = decode(container("RIFF", "WAVE", {extensibleChunk(3, 1, 44100, 32, 32), chunk("data", floats({0.25, -0.5}, 4, false))}), d, err);
        check(ok && d.isFloat && d.frames() == 2 && d.samples[0][0] == 0.25f && d.samples[0][1] == -0.5f, "WAV extensible float", "");
        ok = decode(container("RIFF", "WAVE", {extensibleChunk(1, 1, 44100, 16, 16, false), chunk("data", pcm({1, 2}, 2, false))}), d, err);
        check(!ok && !err.empty(), "WAV extensible of an unknown kind is refused", "");
    }
    // More than two channels keep the first two.
    {
        std::vector<int64_t> v;
        std::vector<std::vector<float>> want(2);
        for (int i = 0; i < 4 * 50; ++i) {
            v.push_back(rng.range(-32768, 32767));
            if (i % 4 < 2) want[i % 4].push_back(pcmValue(v.back(), 2));
        }
        const Bytes wav = container("RIFF", "WAVE", {fmtChunk(1, 4, 44100, 16, 8), chunk("data", pcm(v, 2, false))});
        AudioData d;
        AudioInfo info;
        const bool ok = decode(wav, d, err) && probeAudio(wav.data(), wav.size(), info, nullptr, err);
        check(ok && same(d, want) && info.channels == 4 && info.frames == 50, "WAV with 4 channels keeps the first two", "");
    }
}

static void testWavChunks() {
    std::string err;
    Rng rng(2);
    std::vector<int64_t> v;
    std::vector<std::vector<float>> want(1);
    for (int i = 0; i < 101; ++i) { // odd byte count at 8 bits: the data chunk is padded
        v.push_back(rng.range(0, 255));
        want[0].push_back(pcmValue(v.back() - 128, 1));
    }
    const Bytes data = chunk("data", pcm(v, 1, false));
    const Bytes junk = chunk("JUNK", Bytes{1, 2, 3});
    const Bytes list = chunk("LIST", Bytes{'I', 'N', 'F', 'O', 'I', 'N', 'A', 'M', 1, 0, 0, 0, 'x'});
    AudioData d;
    bool ok = decode(container("RIFF", "WAVE", {junk, data, list, fmtChunk(1, 1, 22050, 8, 1), smplChunk(64, 0, {{0, 10, 50}})}), d, err);
    check(ok && same(d, want) && d.sampleRate == 22050 && d.loops.size() == 1 && d.loops[0].start == 10 && d.loops[0].end == 51,
          "WAV: data before fmt, odd chunks padded", "%.0f frames, %.0f loops", static_cast<double>(d.frames()), static_cast<double>(d.loops.size()));

    // smpl: root, fraction, loops of each type; one starts beyond the sound (dropped), one ends beyond it (clamped),
    // one has its end before its start (dropped).
    std::vector<int64_t> s(2 * 1000, 0);
    const Bytes data16 = chunk("data", pcm(s, 2, false));
    const Bytes fmt16 = fmtChunk(1, 2, 44100, 16, 4);
    ok = decode(container("RIFF", "WAVE",
                          {fmt16, data16, smplChunk(64, 0x40000000u, {{0, 100, 199}, {1, 200, 299}, {2, 300, 5000}, {0, 2000, 2100}, {0, 50, 40}})}),
                d, err);
    check(ok && d.rootKey == 64 && std::fabs(d.tuneCents - 25.0) < 1e-9 && d.loops.size() == 3 && d.loops[0].start == 100 && d.loops[0].end == 200 &&
              d.loops[0].type == AudioData::kForward && d.loops[1].type == AudioData::kPingPong && d.loops[2].type == AudioData::kBackward &&
              d.loops[2].end == 1000,
          "WAV smpl: root, cents, loops of each type, clamped", "root %.0f, %.1f cents, %.0f loops", d.rootKey, d.tuneCents, static_cast<double>(d.loops.size()));

    // inst alone, then inst with smpl (smpl's root wins, inst keeps its ranges).
    const Bytes inst = chunk("inst", Bytes{50, static_cast<uint8_t>(-10), static_cast<uint8_t>(-3), 40, 60, 1, 127});
    ok = decode(container("RIFF", "WAVE", {fmt16, inst, data16}), d, err);
    check(ok && d.rootKey == 50 && d.tuneCents == -10 && d.gainDb == -3 && d.loKey == 40 && d.hiKey == 60 && d.loVel == 1 && d.hiVel == 127,
          "WAV inst: root, tune, gain, key and velocity range", "root %.0f, tune %.0f, gain %.0f", d.rootKey, d.tuneCents, d.gainDb);
    ok = decode(container("RIFF", "WAVE", {fmt16, inst, data16, smplChunk(70, 0, {})}), d, err);
    check(ok && d.rootKey == 70 && d.tuneCents == 0 && d.loKey == 40 && d.gainDb == -3, "WAV inst and smpl: smpl gives the root", "root %.0f", d.rootKey);

    // cue: unsorted, a duplicate, one outside the sound.
    ok = decode(container("RIFF", "WAVE", {fmt16, data16, cueChunk({500, 20, 999, 1000, 20, 300000})}), d, err);
    check(ok && d.cues == std::vector<size_t>{20, 500, 999}, "WAV cue: sorted, within the sound", "%.0f cues", static_cast<double>(d.cues.size()));

    // acid: tempo and root note.
    Bytes acid;
    putLe(acid, 0x02, 4);
    putLe(acid, 57, 2);
    putLe(acid, 0x8000, 2);
    putLe(acid, 0, 4);
    putLe(acid, 8, 4);
    putLe(acid, 4, 2);
    putLe(acid, 4, 2);
    const float tempo = 128.5f;
    uint32_t tb;
    std::memcpy(&tb, &tempo, 4);
    putLe(acid, tb, 4);
    ok = decode(container("RIFF", "WAVE", {fmt16, chunk("acid", acid), data16}), d, err);
    check(ok && d.tempo == 128.5 && d.rootKey == 57, "WAV acid: tempo and root", "tempo %.1f, root %.0f", d.tempo, d.rootKey);

    // probeAudio fills the same metadata without the samples.
    AudioInfo info;
    AudioData meta;
    ok = probeAudio(nullptr, 0, info, &meta, err);
    check(!ok, "probe of nothing is refused", "");
    const Bytes withMeta = container("RIFF", "WAVE", {fmt16, data16, smplChunk(64, 0, {{0, 100, 199}}), cueChunk({5})});
    ok = probeAudio(withMeta.data(), withMeta.size(), info, &meta, err);
    check(ok && meta.frames() == 0 && meta.loops.size() == 1 && meta.cues.size() == 1 && meta.rootKey == 64 && meta.channels == 2 && info.frames == 1000,
          "probe fills loops, cues and root, no samples", "");

    // RF64: sizes in ds64.
    Bytes ds64;
    putLe(ds64, 0, 8);
    putLe(ds64, s.size() * 2, 8);
    putLe(ds64, 1000, 8);
    putLe(ds64, 0, 4);
    Bytes rf = container("RF64", "WAVE", {chunk("ds64", ds64), fmt16, chunk("data", pcm(s, 2, false), false, 0xFFFFFFFFll), smplChunk(61, 0, {})});
    ok = decode(rf, d, err);
    check(ok && d.frames() == 1000 && d.rootKey == 61, "RF64 with its sizes in ds64", "%.0f frames", static_cast<double>(d.frames()));
}

static void testWavDamage() {
    std::string err;
    std::vector<int64_t> s;
    for (int i = 0; i < 2 * 1000; ++i) s.push_back((i * 37) % 2000 - 1000);
    const Bytes fmt16 = fmtChunk(1, 2, 44100, 16, 4);
    AudioData d;

    Bytes wav = container("RIFF", "WAVE", {fmt16, chunk("data", pcm(s, 2, false), false, 8000)});
    wav.resize(wav.size() - 2000 - 3); // half the data and then part of a frame
    bool ok = decode(wav, d, err);
    check(ok && d.frames() == 499 && d.samples[1][498] == pcmValue(s[2 * 498 + 1], 2), "WAV cut short gives the whole frames there are", "%.0f frames",
          static_cast<double>(d.frames()));
    ok = decode(container("RIFF", "WAVE", {fmt16, chunk("data", pcm(s, 2, false), false, 0)}), d, err);
    check(ok && d.frames() == 1000, "WAV with a data size of 0 reads to the end", "%.0f frames", static_cast<double>(d.frames()));
    ok = decode(container("RIFF", "WAVE", {fmt16, chunk("data", pcm(s, 2, false), false, 0xFFFFFFF0ll)}), d, err);
    check(ok && d.frames() == 1000, "WAV with a data size beyond the file", "%.0f frames", static_cast<double>(d.frames()));

    struct Bad {
        const char* name;
        Bytes file;
    };
    const Bytes data = chunk("data", pcm(s, 2, false));
    Bytes hugeLoops = smplChunk(60, 0, {{0, 1, 2}}, 0xFFFFFFF0u);
    Bytes riffHuge = container("RIFF", "WAVE", {fmt16, data});
    riffHuge[4] = riffHuge[5] = riffHuge[6] = riffHuge[7] = 0xFF;
    const std::vector<Bad> bad = {
        {"no channels", container("RIFF", "WAVE", {fmtChunk(1, 0, 44100, 16, 0), data})},
        {"300 channels", container("RIFF", "WAVE", {fmtChunk(1, 300, 44100, 16, 600), data})},
        {"sample rate 0", container("RIFF", "WAVE", {fmtChunk(1, 2, 0, 16, 4), data})},
        {"0 bits", container("RIFF", "WAVE", {fmtChunk(1, 2, 44100, 0, 4), data})},
        {"40-bit PCM", container("RIFF", "WAVE", {fmtChunk(1, 2, 44100, 40, 10), data})},
        {"16-bit float", container("RIFF", "WAVE", {fmtChunk(3, 2, 44100, 16, 4), data})},
        {"ADPCM", container("RIFF", "WAVE", {fmtChunk(2, 2, 44100, 4, 4), data})},
        {"no data chunk", container("RIFF", "WAVE", {fmt16})},
        {"no fmt chunk", container("RIFF", "WAVE", {data})},
        {"short fmt chunk", container("RIFF", "WAVE", {chunk("fmt ", Bytes{1, 0, 2, 0}), data})},
        {"fmt chunk beyond the end", container("RIFF", "WAVE", {chunk("fmt ", Bytes(16, 0), false, 0x7FFFFFFF)})},
        {"just RIFF", Bytes{'R', 'I', 'F', 'F'}},
        {"nothing", Bytes{}},
        {"not a sound", Bytes(100, 'x')},
    };
    for (const Bad& b : bad) {
        AudioInfo info;
        ok = decode(b.file, d, err);
        const bool probed = probeAudio(b.file.data(), b.file.size(), info, &d, err);
        check(!ok && !probed && !err.empty() && d.frames() == 0, std::string("WAV refused: ") + b.name, "");
    }
    ok = decode(container("RIFF", "WAVE", {fmt16, data, hugeLoops, cueChunk({1, 2}, 0xFFFFFFF0u)}), d, err);
    check(ok && d.loops.size() == 1 && d.cues.size() == 2, "WAV with absurd loop and cue counts", "");
    ok = decode(riffHuge, d, err);
    check(ok && d.frames() == 1000, "WAV with an absurd RIFF size", "");
    ok = decode(container("RIFF", "WAVE", {fmtChunk(1, 2, 44100, 16, 0), data}), d, err);
    check(ok && d.frames() == 1000, "WAV with a block align of 0", "");
    ok = decodeAudio(nullptr, 1000, d, err);
    check(!ok, "a null pointer is refused", "");
}

// ---------------------------------------------------------------- AIFF

static Bytes markChunk(const std::vector<std::pair<int, uint32_t>>& marks) {
    Bytes m;
    putBe(m, marks.size(), 2);
    for (size_t i = 0; i < marks.size(); ++i) {
        putBe(m, static_cast<uint64_t>(marks[i].first), 2);
        putBe(m, marks[i].second, 4);
        const std::string name = i % 2 ? "loop end" : "begin"; // odd and even name lengths
        m.push_back(static_cast<uint8_t>(name.size()));
        m.insert(m.end(), name.begin(), name.end());
        if ((name.size() + 1) & 1) m.push_back(0);
    }
    return chunk("MARK", m, true);
}

static Bytes instChunk(int base, int detune, int mode, int begin, int end, int gain = -6) {
    Bytes b = {static_cast<uint8_t>(base), static_cast<uint8_t>(detune), 36, 84, 10, 120};
    putBe(b, static_cast<uint64_t>(static_cast<uint16_t>(gain)), 2);
    putBe(b, static_cast<uint64_t>(mode), 2);
    putBe(b, static_cast<uint64_t>(begin), 2);
    putBe(b, static_cast<uint64_t>(end), 2);
    putBe(b, 0, 6);
    return chunk("INST", b, true);
}

static void testAiff() {
    std::string err;
    Rng rng(3);
    // Plain AIFF of several widths, including 12 bits in two bytes, and its rates.
    for (int bits : {8, 12, 16, 24, 32}) {
        const int bytes = (bits + 7) / 8;
        std::vector<int64_t> v;
        std::vector<std::vector<float>> want(2);
        for (int i = 0; i < 2 * 257; ++i) {
            const int64_t x = rng.range(-(int64_t(1) << (bits - 1)), (int64_t(1) << (bits - 1)) - 1) * (int64_t(1) << (8 * bytes - bits));
            v.push_back(x);
            want[i & 1].push_back(pcmValue(x, bytes));
        }
        const double rate = bits == 16 ? 22050.0 : 44100.0;
        const Bytes aiff = container("FORM", "AIFF", {commChunk(2, 257, bits, rate), ssndChunk(pcm(v, bytes, true))}, true);
        AudioData d;
        AudioInfo info;
        const bool ok = decode(aiff, d, err) && probeAudio(aiff.data(), aiff.size(), info, nullptr, err);
        size_t bad = 0;
        check(ok && same(d, want, &bad) && d.sampleRate == rate && d.bits == bits && d.format == "AIFF" && info.bigEndian && info.raw,
              "AIFF " + std::to_string(bits) + " bit at " + std::to_string(static_cast<int>(rate)) + " Hz, exact", "%.0f samples differ", static_cast<double>(bad));
    }
    // AIFF-C kinds.
    struct Kind {
        const char* type;
        int bits, bytes;
        bool big, isFloat;
    };
    for (const Kind& k : {Kind{"NONE", 16, 2, true, false}, Kind{"twos", 16, 2, true, false}, Kind{"sowt", 16, 2, false, false},
                          Kind{"sowt", 24, 3, false, false}, Kind{"in24", 24, 3, true, false}, Kind{"raw ", 8, 1, true, false},
                          Kind{"fl32", 32, 4, true, true}, Kind{"fl64", 64, 8, true, true}}) {
        std::vector<std::vector<float>> want(1);
        Bytes data;
        for (int i = 0; i < 300; ++i) {
            if (k.isFloat) {
                const double x = static_cast<double>(rng.range(-100000, 100000)) / 100000.0;
                const Bytes one = floats({x}, k.bytes, true);
                data.insert(data.end(), one.begin(), one.end());
                want[0].push_back(k.bytes == 4 ? static_cast<float>(x) : static_cast<float>(x));
            } else {
                const int64_t x = rng.range(-(int64_t(1) << (k.bits - 1)), (int64_t(1) << (k.bits - 1)) - 1);
                const Bytes one = pcm({std::string(k.type) == "raw " ? x + 128 : x}, k.bytes, k.big);
                data.insert(data.end(), one.begin(), one.end());
                want[0].push_back(pcmValue(x, k.bytes));
            }
        }
        Bytes fver;
        putBe(fver, 0xA2805140u, 4);
        const Bytes aifc = container("FORM", "AIFC", {chunk("FVER", fver, true), commChunk(1, 300, k.bits, 48000.0, k.type), ssndChunk(data, 4)}, true);
        AudioData d;
        AudioInfo info;
        const bool ok = decode(aifc, d, err) && probeAudio(aifc.data(), aifc.size(), info, nullptr, err);
        size_t bad = 0;
        check(ok && same(d, want, &bad) && d.isFloat == k.isFloat && d.sampleRate == 48000 && info.bigEndian == k.big,
              std::string("AIFF-C '") + k.type + "' " + std::to_string(k.bits) + " bit", "%.0f samples differ", static_cast<double>(bad));
    }
    AudioData d;
    bool ok = decode(container("FORM", "AIFC", {commChunk(1, 10, 16, 44100.0, "ima4"), ssndChunk(Bytes(20, 0))}, true), d, err);
    check(!ok && err.find("ima4") != std::string::npos, "AIFF-C compressed is refused", "");

    // MARK and INST: sustain loops forward and ping-pong, base note, detune, ranges, gain.
    std::vector<int64_t> s(2000, 0);
    const Bytes comm = commChunk(1, 2000, 16, 44100.0), ssnd = ssndChunk(pcm(s, 2, true));
    ok = decode(container("FORM", "AIFF", {comm, instChunk(61, -12, 1, 7, 9), markChunk({{7, 100}, {9, 900}, {3, 1500}, {4, 99999}}), ssnd}, true), d, err);
    check(ok && d.rootKey == 61 && d.tuneCents == -12 && d.gainDb == -6 && d.loKey == 36 && d.hiKey == 84 && d.loVel == 10 && d.hiVel == 120 &&
              d.loops.size() == 1 && d.loops[0].start == 100 && d.loops[0].end == 900 && d.loops[0].type == AudioData::kForward &&
              d.cues == std::vector<size_t>{100, 900, 1500},
          "AIFF MARK and INST: forward loop, root, ranges", "%.0f loops, %.0f cues", static_cast<double>(d.loops.size()), static_cast<double>(d.cues.size()));
    ok = decode(container("FORM", "AIFF", {comm, ssnd, markChunk({{1, 10}, {2, 20}}), instChunk(60, 0, 2, 1, 2)}, true), d, err);
    check(ok && d.loops.size() == 1 && d.loops[0].type == AudioData::kPingPong && d.loops[0].end == 20, "AIFF ping-pong sustain loop", "");
    ok = decode(container("FORM", "AIFF", {comm, ssnd, markChunk({{1, 10}, {2, 20}}), instChunk(60, 0, 1, 2, 1)}, true), d, err);
    check(ok && d.loops.empty(), "AIFF loop with its end before its start is dropped", "");
    ok = decode(container("FORM", "AIFF", {comm, ssnd, markChunk({{1, 10}}), instChunk(60, 0, 1, 1, 5)}, true), d, err);
    check(ok && d.loops.empty(), "AIFF loop on a missing marker is dropped", "");
    ok = decode(container("FORM", "AIFF", {comm, ssnd, instChunk(200, 0, 0, 0, 0)}, true), d, err);
    check(ok && d.rootKey == -1, "AIFF base note out of range is ignored", "");

    // Damage.
    Bytes cut = container("FORM", "AIFF", {comm, ssnd}, true);
    cut.resize(cut.size() - 1001);
    ok = decode(cut, d, err);
    check(ok && d.frames() == 1499, "AIFF cut short gives the whole frames there are", "%.0f frames", static_cast<double>(d.frames()));
    ok = decode(container("FORM", "AIFF", {commChunk(1, 500, 16, 44100.0), ssnd}, true), d, err);
    check(ok && d.frames() == 500, "AIFF reads no more frames than COMM says", "%.0f frames", static_cast<double>(d.frames()));
    ok = decode(container("FORM", "AIFF", {comm, ssndChunk(pcm(s, 2, true), 0, 0x7FFFFFFF)}, true), d, err);
    check(ok && d.frames() == 2000, "AIFF with an SSND size beyond the file", "");
    for (const auto& b : std::vector<std::pair<const char*, Bytes>>{
             {"no COMM", container("FORM", "AIFF", {ssnd}, true)},
             {"no SSND", container("FORM", "AIFF", {comm}, true)},
             {"rate 0", container("FORM", "AIFF", {commChunk(1, 2000, 16, 0.0), ssnd}, true)},
             {"rate 1e300", container("FORM", "AIFF", {commChunk(1, 2000, 16, 1e300), ssnd}, true)},
             {"0 channels", container("FORM", "AIFF", {commChunk(0, 2000, 16, 44100.0), ssnd}, true)},
             {"33 bits", container("FORM", "AIFF", {commChunk(1, 2000, 33, 44100.0), ssnd}, true)},
         }) {
        ok = decode(b.second, d, err);
        check(!ok && !err.empty(), std::string("AIFF refused: ") + b.first, "");
    }
    // A bad SSND offset field: points past the chunk.
    Bytes badOffset = container("FORM", "AIFF", {comm, ssnd}, true);
    const size_t ssndAt = 12 + comm.size() + 8;
    badOffset[ssndAt] = 0x7F;
    ok = decode(badOffset, d, err);
    check(!ok, "AIFF refused: SSND offset field past the chunk", "");
}

// ---------------------------------------------------------------- files on disk

static void testFiles() {
    std::string err;
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path() / fs::u8path(u8"hnl-sampler-test-Klänge ♪");
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    check(!ec && fs::is_directory(dir), "temp folder with a non-ASCII name", "");

    Rng rng(4);
    AudioData a;
    a.channels = 2;
    a.sampleRate = 44100;
    for (int i = 0; i < 20000; ++i) {
        a.samples[0].push_back(static_cast<float>(rng.range(-32768, 32767)) / 32768.0f);
        a.samples[1].push_back(static_cast<float>(rng.range(-32768, 32767)) / 32768.0f);
    }
    a.samples[0][5] = 1.0f; // clipped to 32767 at 16 bit
    a.rootKey = 62;
    a.tuneCents = 25;
    AudioData::Loop l;
    l.start = 1000;
    l.end = 19000;
    l.type = AudioData::kPingPong;
    a.loops.push_back(l);
    for (int bits : {16, 24, 32}) {
        const fs::path file = dir / fs::u8path(u8"naïve " + std::to_string(bits) + ".wav");
        bool ok = writeWav(file, a, bits, err);
        AudioData b;
        ok = ok && readAudioFile(file, b, err);
        size_t differ = 0;
        for (size_t i = 0; ok && i < a.frames(); ++i) {
            const float clipped = bits == 16 ? 32767.0f / 32768.0f : 8388607.0f / 8388608.0f;
            const float want = (bits != 32 && i == 5) ? clipped : a.samples[0][i];
            differ += (b.samples[0][i] != want) + (b.samples[1][i] != a.samples[1][i]);
        }
        check(ok && differ == 0 && b.frames() == a.frames() && b.rootKey == 62 && std::fabs(b.tuneCents - 25) < 1e-6 && b.loops.size() == 1 &&
                  b.loops[0].start == 1000 && b.loops[0].end == 19000 && b.loops[0].type == AudioData::kPingPong && b.isFloat == (bits == 32),
              "writeWav " + std::to_string(bits) + " bit, then readAudioFile", "%.0f samples differ", static_cast<double>(differ));
    }
    {
        AudioData m;
        m.channels = 1;
        m.sampleRate = 8000;
        m.samples[0] = {0.5f, -0.5f, 0.0f, 2.0f, -2.0f};
        const uint32_t nan = 0x7FC00000u;
        std::memcpy(&m.samples[0][2], &nan, 4);
        m.rootKey = 60;
        m.tuneCents = -10;
        const fs::path file = dir / "mono.wav";
        AudioData b;
        bool ok = writeWav(file, m, 24, err) && readAudioFile(file, b, err);
        check(ok && b.channels == 1 && b.samples[1].empty() && b.samples[0] == std::vector<float>{0.5f, -0.5f, 0.0f, 8388607.0f / 8388608.0f, -1.0f} &&
                  b.rootKey == 59 && std::fabs(b.tuneCents - 90) < 1e-6,
              "writeWav mono 24 bit, NaN and overs, tune below the note", "root %.0f, %.1f cents", b.rootKey, b.tuneCents);
        ok = writeWav(file, m, 20, err);
        check(!ok, "writeWav refuses 20 bits", "");
        AudioData none;
        ok = writeWav(dir / "none.wav", none, 16, err);
        check(!ok, "writeWav refuses an empty sound", "");
        ok = readAudioFile(dir / "missing.wav", b, err);
        check(!ok && !err.empty(), "readAudioFile of a missing file", "");
    }

    // readRawFrames against decodeAudio, for WAV and AIFF of several kinds.
    struct Case {
        const char* name;
        Bytes file;
    };
    std::vector<int64_t> v16, v24, v8, v4;
    std::vector<double> vf;
    for (int i = 0; i < 2 * 5000; ++i) {
        v16.push_back(rng.range(-32768, 32767));
        v24.push_back(rng.range(-8388608, 8388607));
        v8.push_back(rng.range(0, 255));
        vf.push_back(static_cast<double>(rng.range(-1000, 1000)) / 1000.0);
    }
    for (int i = 0; i < 3 * 5000; ++i) v4.push_back(rng.range(-32768, 32767));
    const std::vector<Case> cases = {
        {"WAV 16 stereo", container("RIFF", "WAVE", {fmtChunk(1, 2, 44100, 16, 4), chunk("data", pcm(v16, 2, false))})},
        {"WAV 24 mono", container("RIFF", "WAVE", {fmtChunk(1, 1, 44100, 24, 3), chunk("data", pcm(v24, 3, false))})},
        {"WAV 8 stereo", container("RIFF", "WAVE", {fmtChunk(1, 2, 44100, 8, 2), chunk("data", pcm(v8, 1, false))})},
        {"WAV float 64 stereo", container("RIFF", "WAVE", {fmtChunk(3, 2, 44100, 64, 16), chunk("data", floats(vf, 8, false))})},
        {"WAV 16, 3 channels", container("RIFF", "WAVE", {fmtChunk(1, 3, 44100, 16, 6), chunk("data", pcm(v4, 2, false))})},
        {"AIFF 24 stereo", container("FORM", "AIFF", {commChunk(2, 5000, 24, 44100.0), ssndChunk(pcm(v24, 3, true), 8)}, true)},
        {"AIFF-C sowt 16 mono", container("FORM", "AIFC", {commChunk(1, 10000, 16, 44100.0, "sowt"), ssndChunk(pcm(v16, 2, false))}, true)},
        {"AIFF-C fl32 stereo", container("FORM", "AIFC", {commChunk(2, 5000, 32, 44100.0, "fl32"), ssndChunk(floats(vf, 4, true))}, true)},
    };
    for (const Case& c : cases) {
        const fs::path file = dir / (std::string(c.name) + ".snd");
        std::FILE* f = audiofile::openFile(file, true);
        if (f) {
            std::fwrite(c.file.data(), 1, c.file.size(), f);
            std::fclose(f);
        }
        AudioData full;
        AudioInfo info;
        AudioData meta;
        bool ok = decode(c.file, full, err) && probeAudioFile(file, info, &meta, err) && info.raw;
        AudioData fromDisk;
        ok = ok && readAudioFile(file, fromDisk, err) && fromDisk.samples[0] == full.samples[0] && fromDisk.samples[1] == full.samples[1];
        size_t differ = 0, ranges = 0;
        f = audiofile::openFile(file, false);
        for (int k = 0; ok && f && k < 50; ++k) {
            const uint64_t first = rng.below(info.frames + 10);
            const size_t count = static_cast<size_t>(rng.below(3000));
            std::vector<float> L(count + 1, 9.0f), R(count + 1, 9.0f);
            const size_t got = readRawFrames(f, info, first, count, L.data(), k % 5 == 0 ? nullptr : R.data());
            const size_t expect = first >= info.frames ? 0 : static_cast<size_t>(std::min<uint64_t>(count, info.frames - first));
            if (got != expect) ++differ;
            for (size_t i = 0; i < got && got == expect; ++i) {
                const size_t at = static_cast<size_t>(first) + i;
                differ += L[i] != full.samples[0][at];
                if (k % 5 != 0) differ += R[i] != (full.channels > 1 ? full.samples[1][at] : full.samples[0][at]);
            }
            differ += L[got] != 9.0f; // nothing written past what it returned
            ++ranges;
        }
        if (f) std::fclose(f);
        check(ok && f && differ == 0 && ranges == 50 && info.frames == full.frames(), std::string("readRawFrames matches decodeAudio: ") + c.name,
              "%.0f differences", static_cast<double>(differ));
    }
    check(isAudioFileName("a.WAV") && isAudioFileName("b.wave") && isAudioFileName("c.Aif") && isAudioFileName("d.AIFF") && isAudioFileName("e.aifc") &&
              isAudioFileName("f.FlAc") && !isAudioFileName("g.mp3") && !isAudioFileName("wav") && !isAudioFileName("h.wav.txt") && !isAudioFileName(""),
          "isAudioFileName", "");
    fs::remove_all(dir, ec);
    check(!fs::exists(dir), "temp folder removed", "");
}

// ---------------------------------------------------------------- a FLAC encoder for the tests

struct BitWriter {
    Bytes out;
    unsigned acc = 0;
    int fill = 0;
    void bit(unsigned b) {
        acc = (acc << 1) | (b & 1);
        if (++fill == 8) {
            out.push_back(static_cast<uint8_t>(acc));
            acc = 0;
            fill = 0;
        }
    }
    void put(uint64_t v, int n) {
        for (int i = n - 1; i >= 0; --i) bit(static_cast<unsigned>(v >> i));
    }
    void putSigned(int64_t v, int n) { put(static_cast<uint64_t>(v), n); }
    void unary(uint64_t q) {
        for (uint64_t i = 0; i < q; ++i) bit(0);
        bit(1);
    }
    void align() {
        while (fill) bit(0);
    }
};

static uint8_t testCrc8(const uint8_t* p, size_t n) {
    unsigned c = 0;
    for (size_t i = 0; i < n; ++i) {
        c ^= p[i];
        for (int k = 0; k < 8; ++k) c = (c & 0x80) ? ((c << 1) ^ 0x07) & 0xFF : (c << 1) & 0xFF;
    }
    return static_cast<uint8_t>(c);
}

static uint16_t testCrc16(const uint8_t* p, size_t n) {
    unsigned c = 0;
    for (size_t i = 0; i < n; ++i) {
        c ^= static_cast<unsigned>(p[i]) << 8;
        for (int k = 0; k < 8; ++k) c = (c & 0x8000) ? ((c << 1) ^ 0x8005) & 0xFFFF : (c << 1) & 0xFFFF;
    }
    return static_cast<uint16_t>(c);
}

/** Which parts of the format the encoder has used, so that the test can show they were all decoded. */
struct FlacUse {
    int constant = 0, verbatim = 0, fixed[5] = {}, lpc = 0, lpcHigh = 0, method[2] = {}, escape = 0, escapeZero = 0, partitions = 0, wasted = 0;
    int assign[4] = {}; // independent, left/side, right/side, mid/side
    int bits[33] = {};
    int rateCodes[16] = {}, blockCodes[16] = {}, longNumber = 0, shortLast = 0;
};

struct SubPlan {
    int kind;  // 1 verbatim, 2 fixed, 3 LPC
    int order; // for fixed and LPC
    int method, porder;
    bool escape;
};

static const SubPlan kPlans[] = {
    {1, 0, 0, 0, false}, {2, 0, 0, 1, false}, {2, 1, 1, 2, true}, {2, 2, 0, 3, false}, {2, 3, 1, 4, true},  {2, 4, 0, 0, false},
    {3, 1, 1, 1, false}, {3, 2, 0, 2, true},  {3, 8, 1, 3, false}, {3, 32, 0, 0, true}, {3, 12, 1, 5, false}, {2, 2, 1, 2, true},
};
constexpr size_t kPlanCount = sizeof(kPlans) / sizeof(kPlans[0]);

static int signedBits(int64_t v) {
    int n = 1;
    while (v < -(int64_t(1) << (n - 1)) || v >= (int64_t(1) << (n - 1))) ++n;
    return n;
}

static void encodeResidual(BitWriter& w, const std::vector<int64_t>& e, size_t n, size_t order, int method, int porder, bool forceEscape, FlacUse& use) {
    while (porder > 0 && ((n % (size_t(1) << porder)) != 0 || (n >> porder) < order || (n >> porder) == 0)) --porder;
    w.put(static_cast<uint64_t>(method), 2);
    w.put(static_cast<uint64_t>(porder), 4);
    ++use.method[method];
    if (porder > 0) ++use.partitions;
    const size_t per = n >> porder, parts = size_t(1) << porder;
    const int maxK = method ? 30 : 14, paramBits = method ? 5 : 4;
    size_t i = order;
    for (size_t p = 0; p < parts; ++p) {
        const size_t count = p == 0 ? per - order : per;
        uint64_t bestBits = UINT64_MAX;
        int best = 0, raw = 0;
        for (size_t k = 0; k < count; ++k) raw = std::max(raw, e[i + k] == 0 ? 0 : signedBits(e[i + k]));
        for (int k = 0; k <= maxK; ++k) {
            uint64_t b = 0;
            for (size_t j = 0; j < count; ++j) {
                const uint64_t u = (static_cast<uint64_t>(e[i + j]) << 1) ^ static_cast<uint64_t>(e[i + j] >> 63);
                b += (u >> k) + 1 + static_cast<uint64_t>(k);
            }
            if (b < bestBits) {
                bestBits = b;
                best = k;
            }
        }
        const bool escape = raw <= 31 && ((forceEscape && p == parts - 1) || 5 + static_cast<uint64_t>(raw) * count < bestBits);
        if (escape) {
            w.put(method ? 31u : 15u, paramBits);
            w.put(static_cast<uint64_t>(raw), 5);
            for (size_t j = 0; j < count; ++j) w.putSigned(e[i + j], raw);
            ++use.escape;
            if (raw == 0) ++use.escapeZero;
        } else {
            w.put(static_cast<uint64_t>(best), paramBits);
            for (size_t j = 0; j < count; ++j) {
                const uint64_t u = (static_cast<uint64_t>(e[i + j]) << 1) ^ static_cast<uint64_t>(e[i + j] >> 63);
                w.unary(u >> best);
                w.put(u & ((uint64_t(1) << best) - 1), best);
            }
        }
        i += count;
    }
}

static void encodeSubframe(BitWriter& w, std::vector<int64_t> x, int bits, const SubPlan& plan, FlacUse& use) {
    const size_t n = x.size();
    bool constant = true;
    for (int64_t v : x) constant = constant && v == x[0];
    if (constant) {
        w.put(0, 8);
        w.putSigned(x[0], bits);
        ++use.constant;
        return;
    }
    uint64_t any = 0;
    for (int64_t v : x) any |= static_cast<uint64_t>(v);
    int wasted = 0;
    while (!(any & 1) && wasted < bits - 1) {
        any >>= 1;
        ++wasted;
    }
    for (int64_t& v : x) v /= (int64_t(1) << wasted);
    const int b = bits - wasted;
    int kind = plan.kind, method = b > 24 ? 1 : plan.method; // 4-bit Rice parameters cannot code residuals that wide
    size_t order = static_cast<size_t>(plan.order);
    if (order > n) kind = 1;
    unsigned type = kind == 1 ? 1u : kind == 2 ? 8u + static_cast<unsigned>(order) : 31u + static_cast<unsigned>(order);
    w.put(0, 1);
    w.put(type, 6);
    if (wasted) {
        w.put(1, 1);
        w.unary(static_cast<uint64_t>(wasted - 1));
        ++use.wasted;
    } else {
        w.put(0, 1);
    }
    if (kind == 1) {
        for (int64_t v : x) w.putSigned(v, b);
        ++use.verbatim;
        return;
    }
    for (size_t i = 0; i < order; ++i) w.putSigned(x[i], b);
    std::vector<int64_t> e(n, 0);
    if (kind == 2) {
        ++use.fixed[order];
        for (size_t i = order; i < n; ++i) {
            int64_t p = 0;
            switch (order) {
            case 1: p = x[i - 1]; break;
            case 2: p = 2 * x[i - 1] - x[i - 2]; break;
            case 3: p = 3 * x[i - 1] - 3 * x[i - 2] + x[i - 3]; break;
            case 4: p = 4 * x[i - 1] - 6 * x[i - 2] + 4 * x[i - 3] - x[i - 4]; break;
            default: break;
            }
            e[i] = x[i] - p;
        }
    } else {
        ++use.lpc;
        if (order >= 16) ++use.lpcHigh;
        const int precision = 15, shift = 12;
        std::vector<int64_t> q(order, 0);
        if (order == 1) q[0] = 3891;
        else {
            q[0] = 8000;
            q[1] = -3950;
            for (size_t j = 2; j < order; ++j) q[j] = static_cast<int64_t>((j * 7) % 5) - 2;
        }
        w.put(static_cast<uint64_t>(precision - 1), 4);
        w.putSigned(shift, 5);
        for (int64_t c : q) w.putSigned(c, precision);
        for (size_t i = order; i < n; ++i) {
            int64_t s = 0;
            for (size_t j = 0; j < order; ++j) s += q[j] * x[i - 1 - j];
            e[i] = x[i] - (s >> shift);
        }
    }
    encodeResidual(w, e, n, order, method, plan.porder, plan.escape, use);
}

struct FlacFile {
    Bytes bytes;
    std::vector<std::pair<size_t, size_t>> frames; // where each frame starts, and the length of its header without the CRC-8
    std::vector<size_t> frameEnd;                  // one past each frame's CRC-16
    std::vector<std::vector<int64_t>> pcm;         // what it holds, per channel
    int bits = 16, channels = 2;
};

struct FlacOptions {
    int channels = 2, bits = 16;
    uint32_t rate = 44100;
    std::vector<size_t> blocks;
    bool variable = false;     // number frames by their first sample
    bool totalKnown = true;    // STREAMINFO gives the length
    bool id3 = false;          // an ID3v2 tag before the stream
    bool riffLoop = false;     // an APPLICATION "riff" block with a smpl chunk
    bool bitsInHeader = true;  // or "from STREAMINFO"
};

/** Builds the signal of one block: tone, silence, a value repeated, coarse steps (wasted bits), noise or a ramp. */
static void signal(std::vector<std::vector<int64_t>>& out, size_t n, int bits, int style, Rng& rng, size_t at) {
    const int64_t hi = (int64_t(1) << (bits - 1)) - 1, lo = -hi - 1;
    for (size_t c = 0; c < out.size(); ++c) {
        out[c].resize(n);
        for (size_t i = 0; i < n; ++i) {
            const double t = static_cast<double>(at + i);
            double v = 0;
            int64_t x = 0;
            switch (style) {
            case 0:
                v = 0.6 * std::sin(t * (0.03 + 0.01 * static_cast<double>(c))) * static_cast<double>(hi);
                x = static_cast<int64_t>(v) + rng.range(-3, 3);
                break;
            case 1: x = c == 0 ? hi / 3 : -hi / 5; break;
            case 2:
                v = 0.5 * std::sin(t * 0.02) * static_cast<double>(hi);
                x = (static_cast<int64_t>(v) / 8) * 8;
                break;
            case 3: x = rng.range(lo, hi); break;
            case 4: x = lo / 2 + static_cast<int64_t>(i) * (c + 1); break;
            default: x = c == 1 ? rng.range(lo, hi) : static_cast<int64_t>(0.3 * std::sin(t * 0.05) * static_cast<double>(hi)); break;
            }
            out[c][i] = std::max(lo, std::min(hi, x));
        }
    }
    if (style == 2 && out.size() > 1) out[1] = out[0]; // so that the side channel is 0 and mid has wasted bits
}

static void putUtf8(BitWriter& w, uint64_t v, FlacUse& use) {
    if (v < 0x80) {
        w.put(v, 8);
        return;
    }
    ++use.longNumber;
    int len = 2;
    while (len < 7 && v >= (uint64_t(1) << (5 * len + 1))) ++len;
    const unsigned lead = (0xFF00u >> len) & 0xFFu;
    w.put(lead | (v >> (6 * (len - 1))), 8);
    for (int k = len - 2; k >= 0; --k) w.put(0x80 | ((v >> (6 * k)) & 0x3F), 8);
}

static FlacFile encodeFlac(const FlacOptions& o, Rng& rng, FlacUse& use, uint64_t seedStyle = 0) {
    FlacFile f;
    f.bits = o.bits;
    f.channels = o.channels;
    f.pcm.assign(static_cast<size_t>(o.channels), {});
    size_t total = 0;
    for (size_t b : o.blocks) total += b;
    Bytes& out = f.bytes;
    if (o.id3) {
        out.insert(out.end(), {'I', 'D', '3', 3, 0, 0, 0, 0, 1, 5});
        out.insert(out.end(), 133, 0x55);
    }
    putTag(out, "fLaC");
    size_t maxBlock = 16;
    for (size_t b : o.blocks) maxBlock = std::max(maxBlock, b);
    out.push_back(static_cast<uint8_t>(o.riffLoop ? 0 : 0x80));
    putBe(out, 34, 3);
    putBe(out, 16, 2);
    putBe(out, maxBlock, 2);
    putBe(out, 0, 3);
    putBe(out, 0, 3);
    const uint64_t packed = (static_cast<uint64_t>(o.rate) << 44) | (static_cast<uint64_t>(o.channels - 1) << 41) | (static_cast<uint64_t>(o.bits - 1) << 36) |
                            (o.totalKnown ? total : 0);
    putBe(out, packed, 8);
    out.insert(out.end(), 16, 0); // MD5 not computed: allowed, it means "not known"
    if (o.riffLoop) {
        Bytes app;
        putTag(app, "riff");
        const Bytes smpl = smplChunk(65, 0, {{0, 50, 149}});
        app.insert(app.end(), smpl.begin(), smpl.end());
        Bytes pad(7, 0);
        out.push_back(0x02);
        putBe(out, app.size(), 3);
        out.insert(out.end(), app.begin(), app.end());
        out.push_back(0x81); // a PADDING block as the last
        putBe(out, pad.size(), 3);
        out.insert(out.end(), pad.begin(), pad.end());
    }
    static const int kRateCodes[] = {0, 9, 12, 13, 14};
    size_t at = 0, number = 0;
    for (size_t fi = 0; fi < o.blocks.size(); ++fi) {
        const size_t n = o.blocks[fi];
        std::vector<std::vector<int64_t>> ch(static_cast<size_t>(o.channels));
        signal(ch, n, o.bits, static_cast<int>((fi + seedStyle) % 6), rng, at);
        for (size_t c = 0; c < ch.size(); ++c) f.pcm[c].insert(f.pcm[c].end(), ch[c].begin(), ch[c].end());
        int assign = 0; // 0 independent, 8, 9, 10
        if (o.channels == 2) assign = fi % 4 == 0 ? 0 : static_cast<int>(7 + fi % 4);
        ++use.assign[assign ? assign - 7 : 0];
        if (fi + 1 == o.blocks.size() && fi > 0 && n < o.blocks[0]) ++use.shortLast;
        ++use.bits[o.bits];
        BitWriter w;
        w.put(0x3FFE, 14);
        w.put(0, 1);
        w.put(o.variable ? 1 : 0, 1);
        unsigned bs = n <= 256 ? 6 : 7;
        if (n == 192) bs = 1;
        for (unsigned k = 2; k <= 5; ++k)
            if (n == (size_t(576) << (k - 2))) bs = k;
        for (unsigned k = 8; k <= 15; ++k)
            if (n == (size_t(256) << (k - 8))) bs = k;
        if (fi + 1 == o.blocks.size()) bs = n <= 256 ? 6 : 7; // a last block may not be given by a standard code
        ++use.blockCodes[bs];
        int sr = kRateCodes[fi % 5];
        if (sr == 9 && o.rate != 44100) sr = 0;
        ++use.rateCodes[sr];
        w.put(bs, 4);
        w.put(static_cast<uint64_t>(sr), 4);
        w.put(static_cast<uint64_t>(assign ? assign : o.channels - 1), 4);
        int bc = 0;
        if (o.bitsInHeader) bc = o.bits == 8 ? 1 : o.bits == 12 ? 2 : o.bits == 16 ? 4 : o.bits == 20 ? 5 : o.bits == 24 ? 6 : o.bits == 32 ? 7 : 0;
        w.put(static_cast<uint64_t>(bc), 3);
        w.put(0, 1);
        putUtf8(w, o.variable ? at : number, use);
        if (bs == 6) w.put(n - 1, 8);
        if (bs == 7) w.put(n - 1, 16);
        if (sr == 12) w.put(o.rate / 1000, 8);
        if (sr == 13) w.put(o.rate & 0xFFFF, 16);
        if (sr == 14) w.put((o.rate / 10) & 0xFFFF, 16);
        const size_t headerLen = w.out.size();
        w.put(testCrc8(w.out.data(), w.out.size()), 8);
        std::vector<std::vector<int64_t>> sub = ch;
        std::vector<int> subBits(ch.size(), o.bits);
        if (assign == 8) {
            for (size_t i = 0; i < n; ++i) sub[1][i] = ch[0][i] - ch[1][i];
            subBits[1] = o.bits + 1;
        } else if (assign == 9) {
            for (size_t i = 0; i < n; ++i) sub[0][i] = ch[0][i] - ch[1][i];
            subBits[0] = o.bits + 1;
        } else if (assign == 10) {
            for (size_t i = 0; i < n; ++i) {
                sub[0][i] = (ch[0][i] + ch[1][i]) >> 1;
                sub[1][i] = ch[0][i] - ch[1][i];
            }
            subBits[1] = o.bits + 1;
        }
        for (size_t c = 0; c < sub.size(); ++c) encodeSubframe(w, sub[c], subBits[c], kPlans[(fi * 5 + c * 7 + seedStyle) % kPlanCount], use);
        w.align();
        const uint16_t crc = testCrc16(w.out.data(), w.out.size());
        w.put(crc, 16);
        f.frames.push_back({out.size(), headerLen});
        out.insert(out.end(), w.out.begin(), w.out.end());
        f.frameEnd.push_back(out.size());
        at += n;
        ++number;
    }
    return f;
}

static bool sameFlac(const AudioData& d, const FlacFile& f, size_t frames, size_t* bad = nullptr) {
    std::vector<std::vector<float>> want(static_cast<size_t>(std::min(f.channels, 2)));
    const double scale = 1.0 / static_cast<double>(int64_t(1) << (f.bits - 1));
    for (size_t c = 0; c < want.size(); ++c)
        for (size_t i = 0; i < frames; ++i) want[c].push_back(static_cast<float>(static_cast<double>(f.pcm[c][i]) * scale));
    return same(d, want, bad);
}

/** The integers as the decoder makes them, all channels. */
static bool decodeInts(const Bytes& b, std::vector<std::vector<int64_t>>& out, std::string& err) {
    const audiofile::Source src{b.data(), nullptr, b.size()};
    AudioInfo info;
    audiofile::FlacStream si;
    uint64_t start = 0;
    if (!audiofile::parseFlac(src, info, nullptr, si, start, err)) return false;
    out.assign(static_cast<size_t>(si.channels), {});
    auto sink = [&](const int64_t* const* ch, int channels, size_t n, int) {
        for (int c = 0; c < channels; ++c) out[static_cast<size_t>(c)].insert(out[static_cast<size_t>(c)].end(), ch[c], ch[c] + n);
        return true;
    };
    return audiofile::decodeFlacFrames(b.data(), b.size(), static_cast<size_t>(start), si, sink, err);
}

static void testFlac(FlacUse& use) {
    std::string err;
    Rng rng(5);
    struct Case {
        const char* name;
        FlacOptions o;
    };
    std::vector<Case> cases;
    auto add = [&](const char* name, int channels, int bits, std::vector<size_t> blocks) {
        FlacOptions o;
        o.channels = channels;
        o.bits = bits;
        o.blocks = blocks;
        cases.push_back({name, o});
    };
    const std::vector<size_t> mixed = {4096, 1152, 192, 576, 1000, 256, 2304, 4608, 512, 300, 1024, 2048, 37};
    add("FLAC 16 bit stereo", 2, 16, mixed);
    add("FLAC 24 bit stereo", 2, 24, mixed);
    add("FLAC 8 bit stereo", 2, 8, mixed);
    add("FLAC 8 bit mono", 1, 8, {1024, 1024, 4096, 333});
    add("FLAC 12 bit stereo", 2, 12, {1152, 1152, 999});
    add("FLAC 20 bit stereo", 2, 20, {4096, 4096, 4096, 4096, 100});
    add("FLAC 16 bit, 3 channels", 3, 16, {1024, 2048, 1000});
    add("FLAC 32 bit stereo", 2, 32, {1024, 1024, 1024, 1024, 1024, 1024, 77});
    add("FLAC 4 bit mono", 1, 4, {256, 256, 100});
    cases[0].o.id3 = true;
    cases[1].o.riffLoop = true;
    cases[2].o.bitsInHeader = false;
    cases[4].o.totalKnown = false;
    {
        FlacOptions o;
        o.blocks.assign(120, 16);
        o.blocks.push_back(5);
        o.variable = true;
        o.rate = 48000;
        cases.push_back({"FLAC 16 bit, 121 blocks of 16, variable", o});
    }
    std::vector<FlacFile> files;
    for (size_t k = 0; k < cases.size(); ++k) {
        const Case& c = cases[k];
        FlacFile f = encodeFlac(c.o, rng, use, k);
        AudioData d;
        bool ok = decode(f.bytes, d, err);
        size_t bad = 0;
        ok = ok && sameFlac(d, f, f.pcm[0].size(), &bad) && d.format == "FLAC" && d.bits == c.o.bits && d.sampleRate == c.o.rate;
        std::vector<std::vector<int64_t>> ints;
        const bool intsOk = decodeInts(f.bytes, ints, err) && ints == f.pcm;
        check(ok && intsOk, std::string(c.name) + ", sample for sample", "%.0f frames, %.0f differ", static_cast<double>(f.pcm[0].size()), static_cast<double>(bad));
        if (c.o.riffLoop)
            check(d.rootKey == 65 && d.loops.size() == 1 && d.loops[0].start == 50 && d.loops[0].end == 150, "FLAC smpl in an APPLICATION riff block", "");
        AudioInfo info;
        ok = probeAudio(f.bytes.data(), f.bytes.size(), info, nullptr, err);
        check(ok && !info.raw && info.channels == c.o.channels && info.frames == (c.o.totalKnown ? f.pcm[0].size() : 0u), std::string("  probe of ") + c.name, "");
        files.push_back(f);
    }
    // Damage: a bit flipped inside a frame's audio, in its header, in its checksum.
    const FlacFile& f = files[0];
    for (int which = 0; which < 3; ++which) {
        Bytes b = f.bytes;
        const size_t frame = 2, start = f.frames[frame].first, end = f.frameEnd[frame];
        const size_t at = which == 0 ? (start + end) / 2 : which == 1 ? start + 2 : end - 1;
        b[at] ^= 0x10;
        AudioData d;
        const bool ok = decode(b, d, err);
        check(!ok && d.frames() == 0 && !err.empty(),
              std::string("FLAC with a bad CRC is refused: ") + (which == 0 ? "audio" : which == 1 ? "header" : "checksum"), "");
    }
    {
        Bytes b = f.bytes;
        b.resize((f.frames[3].first + f.frameEnd[3]) / 2);
        AudioData d;
        bool ok = decode(b, d, err);
        const size_t three = 4096 + 1152 + 192;
        check(ok && d.frames() == three && sameFlac(d, f, three), "FLAC cut inside a frame keeps the frames before it", "%.0f frames",
              static_cast<double>(d.frames()));
        b.resize(f.frames[0].first + 10);
        ok = decode(b, d, err);
        check(!ok, "FLAC cut inside its first frame is refused", "");
        b.resize(f.frames[0].first);
        ok = decode(b, d, err);
        check(!ok, "FLAC with no frames but a length is refused", "");
        b = f.bytes;
        b.insert(b.begin() + static_cast<long>(f.frames[1].first), 5, 0x00);
        ok = decode(b, d, err);
        check(!ok, "FLAC with junk between frames is refused", "");
        b = f.bytes;
        b.insert(b.end(), {'T', 'A', 'G', 0, 0});
        ok = decode(b, d, err);
        check(ok && d.frames() == f.pcm[0].size(), "FLAC with a tag after its last frame", "");
        b = f.bytes;
        const size_t rate = 143 + 4 + 4 + 10; // the sample rate in STREAMINFO, after the ID3 tag of this file
        b[rate] = 0;
        b[rate + 1] = 0;
        b[rate + 2] &= 0x0F;
        ok = decode(b, d, err);
        check(!ok, "FLAC with sample rate 0 is refused", "");
    }
}

// ---------------------------------------------------------------- fuzz

static bool sane(const AudioData& d, size_t fileSize, bool raw) {
    if (d.channels < 1 || d.channels > 2) return false;
    if (d.channels == 1 && !d.samples[1].empty()) return false;
    if (d.channels == 2 && d.samples[1].size() != d.samples[0].size()) return false;
    if (d.frames() > audiofile::kMaxFrames || (raw && d.frames() > fileSize)) return false;
    for (int c = 0; c < d.channels; ++c)
        for (float v : d.samples[c])
            if (!isFinite(v) || v < -1.01f || v > 1.01f) return false;
    for (const AudioData::Loop& l : d.loops)
        if (l.end <= l.start || l.end > d.frames()) return false;
    for (size_t i = 0; i < d.cues.size(); ++i)
        if (d.cues[i] >= d.frames() || (i && d.cues[i] <= d.cues[i - 1])) return false;
    return d.rootKey >= -1 && d.rootKey <= 127 && isFinite(d.sampleRate) && d.sampleRate > 0;
}

static void fuzz(FlacUse& use) {
    Rng rng(6);
    std::vector<int64_t> v;
    std::vector<double> vf;
    for (int i = 0; i < 2 * 600; ++i) {
        v.push_back(rng.range(-30000, 30000));
        vf.push_back(static_cast<double>(rng.range(-1000, 1000)) / 1000.0);
    }
    std::vector<int64_t> v24(v), v8;
    for (int64_t& x : v24) x *= 200;
    for (int64_t x : v) v8.push_back((x >> 8) + 128);
    const std::vector<Bytes> wavs = {
        container("RIFF", "WAVE", {fmtChunk(1, 2, 44100, 16, 4), chunk("data", pcm(v, 2, false)), smplChunk(60, 1000, {{0, 10, 300}, {1, 20, 30}}),
                                   cueChunk({1, 50, 500}), chunk("inst", Bytes{60, 0, 0, 0, 127, 0, 127})}),
        container("RIFF", "WAVE", {extensibleChunk(1, 2, 48000, 24, 24), chunk("data", pcm(v24, 3, false))}),
        container("RIFF", "WAVE", {fmtChunk(3, 1, 44100, 32, 4), chunk("data", floats(vf, 4, false))}),
        container("RIFF", "WAVE", {fmtChunk(3, 2, 44100, 64, 16), chunk("data", floats(vf, 8, false))}),
        container("RIFF", "WAVE", {fmtChunk(1, 1, 22050, 8, 1), chunk("data", pcm(v8, 1, false))}),
    };
    const std::vector<Bytes> aiffs = {
        container("FORM", "AIFF", {commChunk(2, 600, 16, 44100.0), markChunk({{1, 10}, {2, 500}}), instChunk(60, 3, 1, 1, 2), ssndChunk(pcm(v, 2, true))}, true),
        container("FORM", "AIFC", {commChunk(1, 1200, 32, 48000.0, "fl32"), ssndChunk(floats(vf, 4, true), 4)}, true),
        container("FORM", "AIFC", {commChunk(2, 600, 16, 22050.0, "sowt"), ssndChunk(pcm(v, 2, false))}, true),
        container("FORM", "AIFF", {commChunk(2, 400, 24, 96000.0), ssndChunk(pcm(v24, 3, true))}, true),
    };
    std::vector<FlacFile> flacs;
    for (int k = 0; k < 6; ++k) {
        FlacOptions o;
        o.channels = k == 3 ? 1 : 2;
        o.bits = k == 1 ? 24 : k == 2 ? 8 : 16;
        o.blocks = {1024, 576, 192, 1000, 256, 37};
        o.riffLoop = k == 4;
        o.variable = k == 5;
        flacs.push_back(encodeFlac(o, rng, use, static_cast<uint64_t>(k)));
    }
    std::vector<Bytes> flacBytes;
    for (const FlacFile& f : flacs) flacBytes.push_back(f.bytes);

    struct Family {
        const char* name;
        const std::vector<Bytes>* bases;
        bool raw;
    };
    const Family families[] = {{"WAV", &wavs, true}, {"AIFF", &aiffs, true}, {"FLAC", &flacBytes, false}};
    const char* modes[] = {"bytes flipped", "cut short", "flipped and cut", "random after a real header", "pure random"};
    const int kRuns = 2000;
    for (const Family& fam : families) {
        for (int mode = 0; mode < 5; ++mode) {
            size_t insane = 0, accepted = 0;
            for (int run = 0; run < kRuns; ++run) {
                const Bytes& base = (*fam.bases)[rng.below(fam.bases->size())];
                Bytes b = base;
                if (mode == 0 || mode == 2) {
                    const int flips = 1 + static_cast<int>(rng.below(8));
                    for (int k = 0; k < flips; ++k) {
                        const size_t at = rng.below(4) == 0 ? rng.below(std::min<size_t>(b.size(), 200)) : rng.below(b.size());
                        b[at] = rng.below(3) == 0 ? static_cast<uint8_t>(rng.next()) : static_cast<uint8_t>(b[at] ^ (1u << rng.below(8)));
                    }
                }
                if (mode == 1 || mode == 2) b.resize(rng.below(b.size() + 1));
                if (mode == 3) {
                    b.resize(std::min<size_t>(b.size(), 12 + rng.below(80)));
                    const size_t extra = rng.below(3000);
                    for (size_t k = 0; k < extra; ++k) b.push_back(static_cast<uint8_t>(rng.next()));
                }
                if (mode == 4) {
                    b.resize(rng.below(4000));
                    for (uint8_t& x : b) x = static_cast<uint8_t>(rng.next());
                    if (run % 2 && b.size() >= 4) std::memcpy(b.data(), fam.raw ? (fam.bases == &wavs ? "RIFF" : "FORM") : "fLaC", 4);
                }
                AudioData d;
                std::string err;
                const bool ok = decodeAudio(b.data(), b.size(), d, err);
                AudioInfo info;
                AudioData meta;
                const bool probed = probeAudio(b.data(), b.size(), info, &meta, err);
                if (ok) {
                    ++accepted;
                    if (!sane(d, b.size(), fam.raw)) ++insane;
                } else if (d.frames() != 0 || err.empty()) {
                    ++insane;
                }
                if (probed && (info.raw && (info.dataOffset > b.size() || info.frames * static_cast<uint64_t>(info.bytesPerSample) * info.channels >
                                                                            b.size() - info.dataOffset)))
                    ++insane;
            }
            check(insane == 0, std::string("fuzz ") + fam.name + ", " + modes[mode], "%.0f runs, %.0f read, %.0f out of bounds", kRuns,
                  static_cast<double>(accepted), static_cast<double>(insane));
        }
    }
    // FLAC with bytes changed inside its frames and the checksums made right again, so the decoder sees the damage.
    size_t insane = 0, accepted = 0;
    const int kDeep = 4000;
    for (int run = 0; run < kDeep; ++run) {
        const FlacFile& f = flacs[rng.below(flacs.size())];
        Bytes b = f.bytes;
        const size_t frame = rng.below(f.frames.size());
        const size_t start = f.frames[frame].first, header = f.frames[frame].second, end = f.frameEnd[frame];
        const int flips = 1 + static_cast<int>(rng.below(4));
        for (int k = 0; k < flips; ++k) {
            const size_t at = start + 2 + rng.below(end - 2 - start - 2);
            if (at == start + header) continue;
            b[at] = rng.below(2) ? static_cast<uint8_t>(rng.next()) : static_cast<uint8_t>(b[at] ^ (1u << rng.below(8)));
        }
        b[start + header] = testCrc8(b.data() + start, header);
        const uint16_t crc = testCrc16(b.data() + start, end - 2 - start);
        b[end - 2] = static_cast<uint8_t>(crc >> 8);
        b[end - 1] = static_cast<uint8_t>(crc);
        AudioData d;
        std::string err;
        const bool ok = decodeAudio(b.data(), b.size(), d, err);
        if (ok) {
            ++accepted;
            if (!sane(d, b.size(), false)) ++insane;
        } else if (d.frames() != 0 || err.empty()) {
            ++insane;
        }
    }
    check(insane == 0, "fuzz FLAC, frames changed with their CRCs made right", "%.0f runs, %.0f read, %.0f out of bounds", kDeep, static_cast<double>(accepted),
          static_cast<double>(insane));
}

int main() {
    const auto t0 = std::chrono::steady_clock::now();
    testWavFormats();
    testWavChunks();
    testWavDamage();
    testAiff();
    testFiles();
    FlacUse use;
    testFlac(use);
    check(use.constant > 0 && use.verbatim > 0 && use.fixed[0] > 0 && use.fixed[1] > 0 && use.fixed[2] > 0 && use.fixed[3] > 0 && use.fixed[4] > 0 &&
              use.lpc > 0 && use.lpcHigh > 0,
          "FLAC encoder used CONSTANT, VERBATIM, FIXED 0-4, LPC up to 32", "%.0f constant, %.0f verbatim, %.0f LPC", use.constant, use.verbatim, use.lpc);
    check(use.method[0] > 0 && use.method[1] > 0 && use.escape > 0 && use.escapeZero > 0 && use.partitions > 0 && use.wasted > 0,
          "  both Rice parameter sizes, partitions, escapes, wasted bits", "%.0f escapes (%.0f of 0 bits), %.0f with wasted bits", use.escape, use.escapeZero,
          use.wasted);
    check(use.assign[0] > 0 && use.assign[1] > 0 && use.assign[2] > 0 && use.assign[3] > 0 && use.bits[8] > 0 && use.bits[16] > 0 && use.bits[24] > 0 &&
              use.shortLast > 0 && use.longNumber > 0,
          "  all channel assignments, 8/16/24 bit, a short last block", "");
    bool codes = use.rateCodes[0] && use.rateCodes[9] && use.rateCodes[12] && use.rateCodes[13] && use.rateCodes[14] && use.blockCodes[1] &&
                 use.blockCodes[6] && use.blockCodes[7];
    for (int k = 2; k <= 5; ++k) codes = codes && use.blockCodes[k];
    for (int k = 8; k <= 12; ++k) codes = codes && use.blockCodes[k];
    check(codes, "  sample rate and block size codes", "");
    fuzz(use);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    check(seconds < 60.0, "whole test in under a minute", "%.1f s", seconds);
    std::printf("%d passed, %d failed\n", passes, failures);
    std::printf("%s\n", failures ? "FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
