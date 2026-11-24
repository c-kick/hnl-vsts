// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// Builds the sounds and instruments that come with HNL Sampler from the
// recordings named in plugins/hnl-sampler/factory/SOURCES.md:
//
//   make <folder with the recordings> <plugins/hnl-sampler>
//
// The recordings folder has three folders, drums, strings and organ, as
// fetch.sh fills them. Written are factory/<set>/<sound>.flac (mono, 16 bit),
// factory/<name>.hnlsampler and FactoryData.cpp, which puts all of those into
// the plugin. Every sound is read back after it is written and compared with
// what went in.
//
// Drums are cut to a length and faded out. Strings and organ notes are cut
// from where the note starts to the end of a loop that Analysis.hpp finds in
// the held part and that is closed here by a blend; their pitch is measured,
// and they are brought to one level.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "../../plugins/hnl-sampler/dsp/Analysis.hpp"
#include "../../plugins/hnl-sampler/dsp/AudioFile.hpp"
#include "../../plugins/hnl-sampler/dsp/Model.hpp"
#include "FlacEncode.hpp"

using namespace hnl::sampler;
namespace fs = std::filesystem;

static fs::path sources, plugin;
static std::vector<std::string> packed; // every file under factory/, in the order written
static size_t totalBytes = 0;
static double totalSeconds = 0;

[[noreturn]] static void fail(const std::string& why) {
    std::fprintf(stderr, "factory-sounds: %s\n", why.c_str());
    std::exit(1);
}

struct Mono {
    std::vector<float> x;
    double rate = 0;
};

/** A recording as one channel: the mean of left and right, or left alone when the two would cancel. */
static Mono read(const std::string& name) {
    AudioData d;
    std::string error;
    if (!readAudioFile(sources / fs::u8path(name), d, error)) fail(name + ": " + error);
    Mono m;
    m.rate = d.sampleRate;
    m.x = d.samples[0];
    if (d.channels > 1) {
        double ll = 0, rr = 0, lr = 0;
        for (size_t i = 0; i < d.frames(); ++i) {
            ll += d.samples[0][i] * d.samples[0][i];
            rr += d.samples[1][i] * d.samples[1][i];
            lr += d.samples[0][i] * d.samples[1][i];
        }
        if (lr / std::sqrt(ll * rr + 1e-30) > -0.2)
            for (size_t i = 0; i < d.frames(); ++i) m.x[i] = 0.5f * (d.samples[0][i] + d.samples[1][i]);
    }
    return m;
}

static double peakIn(const std::vector<float>& x) {
    double p = 0;
    for (float v : x) p = std::max(p, static_cast<double>(std::fabs(v)));
    return p;
}

static void writeBytes(const std::string& name, const void* data, size_t size) {
    const fs::path file = plugin / "factory" / fs::u8path(name);
    fs::create_directories(file.parent_path());
    std::FILE* f = std::fopen(file.c_str(), "wb");
    if (f == nullptr || std::fwrite(data, 1, size, f) != size || std::fclose(f) != 0) fail("cannot write " + file.string());
    packed.push_back(name);
    totalBytes += size;
}

/** Writes x as factory/<name>, 16 bit, and checks that it reads back the same. */
static void writeSound(const std::string& name, const std::vector<float>& x, double rate) {
    std::vector<int32_t> whole(x.size());
    for (size_t i = 0; i < x.size(); ++i) whole[i] = static_cast<int32_t>(std::clamp(std::lround(x[i] * 32768.0), -32768l, 32767l));
    const int32_t* channels[1] = {whole.data()};
    const std::vector<uint8_t> flac = hnl::flacenc::encode(channels, 1, whole.size(), 16, static_cast<uint32_t>(std::lround(rate)));
    if (flac.empty()) fail("cannot encode " + name);
    AudioData back;
    std::string error;
    if (!decodeAudio(flac.data(), flac.size(), back, error)) fail(name + " does not read back: " + error);
    if (back.frames() != whole.size() || back.channels != 1 || back.sampleRate != std::lround(rate)) fail(name + " reads back as another sound");
    for (size_t i = 0; i < whole.size(); ++i)
        if (back.samples[0][i] != static_cast<float>(whole[i] / 32768.0)) fail(name + " reads back with other samples");
    writeBytes(name, flac.data(), flac.size());
    totalSeconds += x.size() / rate;
    std::printf("  %-32s %6.2f s %7zu bytes  %4.1f bits a sample\n", name.c_str(), x.size() / rate, flac.size(), 8.0 * flac.size() / x.size());
}

static Zone& addZone(Instrument& doc, int part, const std::string& sound, int lo, int hi, int root) {
    Zone z;
    z.id = doc.nextId++;
    z.sample = doc.useSample("factory:" + sound);
    z.v[kZLoKey] = lo;
    z.v[kZHiKey] = hi;
    z.v[kZRoot] = root;
    doc.parts[part].zones.push_back(z);
    return doc.parts[part].zones.back();
}

static void writeInstrument(const Instrument& doc) {
    Instrument check;
    const std::string text = toText(doc);
    if (!fromText(text, check) || toText(check) != text) fail(doc.name + " does not read back as it was written");
    writeBytes(doc.name + ".hnlsampler", text.data(), text.size());
}

// ------------------------------------------------------------------- drums

/** The first `seconds` of a hit, faded out over its last 40 %, with its loudest sample at -1 dB. */
static std::vector<float> hit(const std::string& file, double seconds, double& rate) {
    Mono m = read("drums/" + file);
    rate = m.rate;
    const size_t n = std::min(m.x.size(), static_cast<size_t>(seconds * m.rate));
    m.x.resize(n);
    const size_t fade = n * 2 / 5;
    for (size_t i = 0; i < fade; ++i) {
        const double t = static_cast<double>(i + 1) / static_cast<double>(fade); // 0..1 towards the end
        m.x[n - fade + i] *= static_cast<float>(0.5 + 0.5 * std::cos(3.14159265358979 * t));
    }
    const double peak = peakIn(m.x);
    if (peak <= 0) fail(file + " is silent");
    for (float& v : m.x) v *= static_cast<float>(0.891 / peak);
    return m.x;
}

struct Piece {
    const char* name;     // of the sounds written
    std::vector<int> keys; // the keys it is on; the first is the one it sounds at as recorded
    double seconds, gain; // length kept, and level in dB against the other pieces
    int group, offBy;     // a piece with a group cuts off the pieces that are off by it
    // Layers from soft to loud; each layer is the takes that play in turn.
    std::vector<std::vector<const char*>> layers;
};

static void makeKit() {
    std::printf("Jazz Kit\n");
    // The General MIDI places. The two toms are on all six tom keys, each a little lower or higher than recorded.
    const std::vector<Piece> pieces = {
        {"kick", {36, 35}, 0.55, 0, 0, 0, {{"mid_kick_snoff_vl2_rr1", "mid_kick_snoff_vl2_rr2"}, {"mid_kick_snoff_vl3_rr1", "mid_kick_snoff_vl3_rr2"}, {"mid_kick_snoff_vl4_rr1", "mid_kick_snoff_vl4_rr2"}}},
        {"stick", {37}, 0.45, -6, 0, 0, {{"mid_snare_crossstick_vl8"}, {"mid_snare_crossstick_vl16"}}},
        {"snare", {38}, 0.85, -1, 0, 0, {{"mid_snare_center_vl9", "mid_snare_center_vl11"}, {"mid_snare_center_vl21", "mid_snare_center_vl23"}, {"mid_snare_center_vl34", "mid_snare_center_vl36"}}},
        {"rim", {40}, 0.85, -1, 0, 0, {{"mid_snare_rimshot_vl6"}, {"mid_snare_rimshot_vl12"}}},
        {"tom_low", {43, 41}, 1.1, -2, 0, 0, {{"mid_ltom_center_vl6"}, {"mid_ltom_center_vl11"}, {"mid_ltom_center_vl16"}}},
        {"tom_high", {48, 45, 47, 50}, 0.9, -2, 0, 0, {{"mid_htom_center_vl6"}, {"mid_htom_center_vl11"}, {"mid_htom_center_vl16"}}},
        {"hat_closed", {42}, 0.35, -9, 1, 0, {{"mid_hh_closed_vl2_rr1", "mid_hh_closed_vl2_rr2"}, {"mid_hh_closed_vl3_rr1", "mid_hh_closed_vl3_rr2"}, {"mid_hh_closed_vl4_rr1", "mid_hh_closed_vl4_rr2"}}},
        {"hat_pedal", {44}, 0.4, -10, 1, 0, {{"mid_hh_pedal_vl2_rr1"}, {"mid_hh_pedal_vl3_rr1"}}},
        {"hat_open", {46}, 1.8, -8, 0, 1, {{"mid_hh_open_vl3_rr1"}, {"mid_hh_open_vl4_rr1"}}},
        {"crash", {49}, 3.0, -5, 0, 0, {{"mid_crash_crash_vl2_rr1"}, {"mid_crash_crash_vl3_rr1"}}},
        {"ride", {51}, 2.4, -8, 0, 0, {{"mid_ride_ride_vl2_rr1"}, {"mid_ride_ride_vl3_rr1"}}},
        {"bell", {53}, 2.4, -8, 0, 0, {{"mid_ride_bell_vl3_rr1"}}},
    };
    Instrument doc;
    doc.name = "Jazz Kit";
    Part& part = doc.parts[0];
    part.name = "Kit";
    part.v[kPLevel] = -3; // every sound is stored near full scale: room for what sounds together
    part.v[kPVelocity] = 85;
    part.v[kPReverbSend] = 8;
    part.v[envParam(0, kEnvRelease)] = 200;
    doc.g[kGReverbDecay] = 0.9;
    for (const Piece& p : pieces) {
        const int layers = static_cast<int>(p.layers.size());
        for (int l = 0; l < layers; ++l) {
            const int takes = static_cast<int>(p.layers[static_cast<size_t>(l)].size());
            for (int t = 0; t < takes; ++t) {
                std::string name = std::string("kit/") + p.name;
                if (layers > 1) name += "_" + std::to_string(l + 1);
                if (takes > 1) name += static_cast<char>('a' + t);
                name += ".flac";
                double rate = 0;
                const std::vector<float> sound = hit(std::string(p.layers[static_cast<size_t>(l)][static_cast<size_t>(t)]) + ".flac", p.seconds, rate);
                writeSound(name, sound, rate);
                for (int key : p.keys) {
                    Zone& z = addZone(doc, 0, name, key, key, p.keys[0]);
                    z.v[kZLoVel] = l == 0 ? 1 : 1 + 127 * l / layers;
                    z.v[kZHiVel] = l == layers - 1 ? 127 : 127 * (l + 1) / layers;
                    z.v[kZGain] = p.gain;
                    z.v[kZOneShot] = 1;
                    z.v[kZGroup] = p.group;
                    z.v[kZOffBy] = p.offBy;
                    z.v[kZSeqLength] = takes;
                    z.v[kZSeqPosition] = t + 1;
                }
            }
        }
    }
    writeInstrument(doc);
}

// -------------------------------------------------------- strings and organ

struct Note {
    std::string file;
    int key = -1; // from the file's name where that can be trusted; -1 to take the measured one
    std::vector<float> x;
    double rate = 0, cents = 0, level = 0;
    size_t loopStart = 0, loopEnd = 0;
    double swing = 0; // how far the level moves from 0.4 s into the note to the loop's end, in dB
    double alike = 0; // how alike the two sides of the loop's joint were before they were blended, 0..1
};

static Note noteFrom(const std::string& file, int key = -1) {
    Note n;
    n.file = file;
    n.key = key;
    return n;
}

/**
 * Cuts a held note from just before it starts to the end of a loop found between `from` and `to` seconds
 * after that, at least `least` seconds long, and measures its pitch and its level inside the loop.
 *
 * The loop is closed in the sound itself: over the last `blend` seconds before the loop's end, what led up
 * to the loop's start fades in and what was there fades out, so that the end runs on into the start without
 * a joint. The two are weighed by how alike they are, so that the level stays the same through the blend
 * whether they are in step (an organ pipe) or not (a section of players).
 */
static void prepare(Note& n, double from, double to, double least, double blend) {
    Mono m = read(n.file);
    n.rate = m.rate;
    const double peak = peakIn(m.x);
    if (peak <= 0) fail(n.file + " is silent");
    size_t onset = 0;
    while (onset < m.x.size() && std::fabs(m.x[onset]) < 0.02 * peak) ++onset;
    onset -= std::min(onset, static_cast<size_t>(0.01 * m.rate));
    // A loop is looked for in stretches half a second longer than it must be, a quarter of a second apart, and the one is kept for which
    // the level moves least from 0.4 s into the note to the loop's end: a swell before the loop or inside it is
    // heard as a bump or a pulse. One that lies earlier wins from one that is less than 0.5 dB steadier, as it
    // makes a shorter sound.
    LoopResult loop;
    n.swing = 1e9;
    for (double start = from; start + least + 0.5 <= to; start += 0.25) {
        const size_t a = std::min(m.x.size(), onset + static_cast<size_t>(start * m.rate)), b = std::min(m.x.size(), onset + static_cast<size_t>((start + least + 0.5) * m.rate));
        const LoopResult found = findLoop(m.x.data(), nullptr, m.x.size(), m.rate, a, b, least);
        if (found.end <= found.start) continue;
        const size_t window = static_cast<size_t>(0.25 * m.rate);
        double lo = 1e9, hi = 0;
        for (size_t at = onset + static_cast<size_t>(0.4 * m.rate); at + window <= found.end; at += window / 2) {
            double sum = 0;
            for (size_t i = at; i < at + window; ++i) sum += m.x[i] * m.x[i];
            lo = std::min(lo, sum);
            hi = std::max(hi, sum);
        }
        const double swing = 10.0 * std::log10(hi / std::max(lo, 1e-30));
        if (swing < n.swing - 0.5) {
            n.swing = swing;
            loop = found;
        }
    }
    if (loop.end <= loop.start) fail(n.file + ": no loop found");
    const size_t mix = std::min({static_cast<size_t>(blend * m.rate), loop.start - onset, loop.end - loop.start});
    double xx = 0, yy = 0, xy = 0;
    for (size_t i = 0; i < mix; ++i) {
        const double x = m.x[loop.end - mix + i], y = m.x[loop.start - mix + i];
        xx += x * x;
        yy += y * y;
        xy += x * y;
    }
    n.alike = std::clamp(xy / std::sqrt(xx * yy + 1e-30), 0.0, 1.0);
    for (size_t i = 0; i < mix; ++i) {
        const double t = (static_cast<double>(i) + 0.5) / static_cast<double>(mix), out = 1.0 - t;
        const double mixed = (out * m.x[loop.end - mix + i] + t * m.x[loop.start - mix + i]) / std::sqrt(out * out + t * t + 2.0 * out * t * n.alike);
        m.x[loop.end - mix + i] = static_cast<float>(mixed);
    }
    const PitchResult pitch = detectPitch(m.x.data() + loop.start, loop.end - loop.start, m.rate);
    if (pitch.key < 0) fail(n.file + ": no pitch found");
    if (n.key < 0) n.key = pitch.key;
    else if ((pitch.key - n.key) % 12 != 0) fail(n.file + ": sounds as another note than its name says");
    n.cents = pitch.cents;
    n.loopStart = loop.start - onset;
    n.loopEnd = loop.end - onset;
    // The loop's first frames once more after its end: what the sampler reads between samples there is then what follows.
    n.x.assign(m.x.begin() + static_cast<long>(onset), m.x.begin() + static_cast<long>(loop.end));
    for (size_t i = 0; i < 64; ++i) n.x.push_back(m.x[loop.start + i]);
    for (size_t i = 0; i < std::min<size_t>(n.x.size(), static_cast<size_t>(0.003 * m.rate)); ++i) n.x[i] *= static_cast<float>(i / (0.003 * m.rate));
    double sum = 0;
    for (size_t i = n.loopStart; i < n.loopEnd; ++i) sum += n.x[i] * n.x[i];
    n.level = std::sqrt(sum / static_cast<double>(n.loopEnd - n.loopStart));
}

/** Writes the notes at one level and gives each the keys nearest to it; the lowest and highest reach `lowest` and `highest`. */
static void addNotes(Instrument& doc, const std::string& folder, std::vector<Note>& notes, int lowest, int highest) {
    std::sort(notes.begin(), notes.end(), [](const Note& a, const Note& b) { return a.key < b.key; });
    // The level all can have without any of them passing -1 dB.
    double level = 1e9;
    for (const Note& n : notes) level = std::min(level, n.level * 0.891 / peakIn(n.x));
    static const char* const kNames[] = {"c", "cs", "d", "ds", "e", "f", "fs", "g", "gs", "a", "as", "b"};
    for (size_t i = 0; i < notes.size(); ++i) {
        Note& n = notes[i];
        for (float& v : n.x) v *= static_cast<float>(level / n.level);
        const std::string name = folder + "/" + kNames[n.key % 12] + std::to_string(n.key / 12 - 1) + ".flac";
        writeSound(name, n.x, n.rate);
        const int lo = i == 0 ? lowest : (notes[i - 1].key + n.key) / 2 + 1, hi = i + 1 == notes.size() ? highest : (n.key + notes[i + 1].key) / 2;
        Zone& z = addZone(doc, 0, name, lo, hi, n.key);
        z.v[kZTune] = std::round(-n.cents);
        z.v[kZLoop] = 1;
        z.v[kZLoopStart] = static_cast<double>(n.loopStart);
        z.v[kZLoopEnd] = static_cast<double>(n.loopEnd);
        std::printf("    key %3d %+4.0f cents, loop %.2f to %.2f s, level moves %.1f dB, sides alike %.2f\n", n.key, n.cents, n.loopStart / n.rate, n.loopEnd / n.rate, n.swing, n.alike);
    }
}

static void makeStrings() {
    std::printf("Strings\n");
    std::vector<Note> notes;
    // The loud layer of the cello section below, of the violin section above; the names in the files lie an octave low.
    for (const char* n : {"C1", "E1", "G1", "B1", "D2", "F2"}) notes.push_back(noteFrom(std::string("strings/CelloEns_susvib_") + n + "_v3_1.wav"));
    for (const char* n : {"G2", "B2", "D3", "F#3", "A3", "C4", "E4", "G4", "B4", "D5"}) notes.push_back(noteFrom(std::string("strings/VlnEns_susVib_") + n + "_v2.wav"));
    for (Note& n : notes) prepare(n, 0.8, 4.5, 1.2, 0.4);
    Instrument doc;
    doc.name = "Strings";
    Part& part = doc.parts[0];
    part.name = "Strings";
    part.v[kPLevel] = -9; // room for a chord of six
    part.v[kPVelocity] = 55;
    part.v[kPReverbSend] = 30;
    part.v[envParam(0, kEnvAttack)] = 40;
    part.v[envParam(0, kEnvRelease)] = 380;
    part.v[kPFilterType] = 1; // low pass, 12 dB: a soft touch is darker, and macro 1 opens or closes it
    part.v[kPCutoff] = 7000;
    part.v[kPResonance] = 0;
    part.v[kPFilterVel] = 45;
    part.v[slotParam(0, kSlotSource)] = kSrcMacro1;
    part.v[slotParam(0, kSlotDest)] = kDstCutoff;
    part.v[slotParam(0, kSlotAmount)] = -40;
    doc.macroNames[0] = "Darker";
    doc.g[kGReverbDecay] = 2.4;
    addNotes(doc, "strings", notes, 24, 96);
    writeInstrument(doc);
}

static void makeOrgan() {
    std::printf("Pipe Organ\n");
    std::vector<Note> notes;
    int key = 24; // C1
    for (const char* n : {"C1", "F#1", "C2", "F#2", "C3", "F#3", "C4", "F#4", "C5", "F#5", "C6"}) {
        notes.push_back(noteFrom(std::string("organ/Rode_Man3Open_") + n + ".wav", key));
        key += 6;
    }
    for (Note& n : notes) prepare(n, 0.5, 3.0, 0.6, 0.15);
    Instrument doc;
    doc.name = "Pipe Organ";
    Part& part = doc.parts[0];
    part.name = "Organ";
    part.v[kPLevel] = -12; // room for a chord of six: every note is as loud as the next
    part.v[kPVelocity] = 0; // an organ does not answer to touch
    part.v[kPReverbSend] = 45;
    part.v[envParam(0, kEnvAttack)] = 4;
    part.v[envParam(0, kEnvRelease)] = 220;
    doc.g[kGReverbDecay] = 3.5;
    addNotes(doc, "organ", notes, 24, 96);
    writeInstrument(doc);
}

int main(int argc, char** argv) {
    if (argc != 3) fail("use: make <folder with the recordings> <plugins/hnl-sampler>");
    sources = fs::u8path(argv[1]);
    plugin = fs::u8path(argv[2]);
    std::error_code ec;
    for (const char* dir : {"kit", "strings", "organ"}) fs::remove_all(plugin / "factory" / dir, ec);
    makeKit();
    makeStrings();
    makeOrgan();

    // The file that puts them into the plugin.
    std::string cpp = "// SPDX-License-Identifier: GPL-3.0-or-later\n// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink\n\n"
                      "// The sounds and instruments that come with the sampler, as the files under factory/.\n"
                      "// Written by tools/factory-sounds: do not edit, run tools/factory-sounds/make.sh.\n//\n"
                      "// A compiler that knows #embed (GCC 15, Clang 19) takes the files in with that. An older one has the\n"
                      "// assembler do it (.incbin), which looks for the files from the folder the compiler is run in: this\n"
                      "// one, as the Makefile and tests/run.sh do. HNL_FACTORY_INCBIN asks for that way on any compiler.\n"
                      "#include \"dsp/Factory.hpp\"\n\n"
                      "#if defined(__has_embed) && !defined(HNL_FACTORY_INCBIN)\n\n"
                      "#if defined(__clang__)\n#pragma clang diagnostic ignored \"-Wc23-extensions\"\n#endif\n\nnamespace {\n\n";
    for (size_t i = 0; i < packed.size(); ++i)
        cpp += "const unsigned char k" + std::to_string(i) + "[] = {\n#embed \"factory/" + packed[i] + "\"\n};\n";
    cpp += "\n#define HNL_FILE(n) k##n, sizeof(k##n)\n\n#else\n\n"
           "#if defined(__APPLE__)\n#define HNL_SECTION \".section __TEXT,__const\"\n"
           "#elif defined(_WIN32)\n#define HNL_SECTION \".section .rdata,\\\"dr\\\"\"\n"
           "#else\n#define HNL_SECTION \".section .rodata\"\n#endif\n"
           "#define HNL_STR2(x) #x\n#define HNL_STR(x) HNL_STR2(x)\n#define HNL_NAME(n) HNL_STR(__USER_LABEL_PREFIX__) \"hnl_sampler_factory_\" #n\n"
           "#define HNL_BIN(n, file) \\\n"
           "    __asm__(HNL_SECTION \"\\n\" HNL_NAME(n) \":\\n.incbin \\\"\" file \"\\\"\\n\" HNL_NAME(n) \"_end:\\n.byte 0\\n.text\\n\"); \\\n"
           "    extern \"C\" const unsigned char hnl_sampler_factory_##n[], hnl_sampler_factory_##n##_end[];\n\n";
    for (size_t i = 0; i < packed.size(); ++i) cpp += "HNL_BIN(" + std::to_string(i) + ", \"factory/" + packed[i] + "\")\n";
    cpp += "\n#define HNL_FILE(n) hnl_sampler_factory_##n, static_cast<size_t>(hnl_sampler_factory_##n##_end - hnl_sampler_factory_##n)\n\n"
           "namespace {\n\n#endif\n\nconst hnl::sampler::factory::Item kItems[] = {\n";
    for (size_t i = 0; i < packed.size(); ++i) cpp += "    {\"" + packed[i] + "\", HNL_FILE(" + std::to_string(i) + ")},\n";
    cpp += "};\n\nconst hnl::sampler::factory::Install kInstall(kItems, sizeof(kItems) / sizeof(kItems[0]));\n\n} // namespace\n";
    const fs::path file = plugin / "FactoryData.cpp";
    std::FILE* f = std::fopen(file.c_str(), "wb");
    if (f == nullptr || std::fwrite(cpp.data(), 1, cpp.size(), f) != cpp.size() || std::fclose(f) != 0) fail("cannot write " + file.string());
    std::printf("%zu files, %.1f s of sound, %.2f MB\n", packed.size(), totalSeconds, totalBytes / 1048576.0);
    return 0;
}
