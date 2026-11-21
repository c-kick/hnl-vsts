// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// The readers of SFZ, SoundFont 2 and Scala files (dsp/Import.hpp), on files
// written here: SFZ text, SoundFonts put together byte by byte, scales. Then
// the same files damaged at random, which must neither crash a reader nor
// give anything back that points outside what was read.
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include "../dsp/Import.hpp"

using namespace hnl::sampler;
namespace fs = std::filesystem;

static int failures = 0;

static void check(bool ok, const std::string& name, const char* fmt, double a = 0, double b = 0, double c = 0, double d = 0) {
    std::printf("%s  %-50s ", ok ? "PASS" : "FAIL", name.c_str());
    std::printf(fmt, a, b, c, d);
    std::printf("\n");
    if (!ok) ++failures;
}

static bool near(double a, double b, double tolerance = 1e-9) { return std::fabs(a - b) <= tolerance; }

static bool hasWarning(const ImportedInstrument& in, const std::string& text) {
    for (const std::string& w : in.warnings)
        if (w.find(text) != std::string::npos) return true;
    return false;
}

static void writeText(const fs::path& file, const std::string& text) {
#ifdef _WIN32
    std::FILE* f = _wfopen(file.c_str(), L"wb");
#else
    std::FILE* f = std::fopen(file.c_str(), "wb");
#endif
    if (f == nullptr) return;
    std::fwrite(text.data(), 1, text.size(), f);
    std::fclose(f);
}

static fs::path tempDir() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    fs::path dir = fs::temp_directory_path() / ("hnl-import-test-" + std::to_string(now));
    fs::create_directories(dir);
    return dir;
}

static std::string expectedPath(const fs::path& base, const std::string& relative) { return (base / fs::u8path(relative)).lexically_normal().u8string(); }

// ---------------------------------------------------------------- SFZ

static void testSfzInheritance(const fs::path& dir) {
    const std::string text =
        "<control>\n"
        "default_path=Samples\\Piano Set/\n"
        "#define $VEL 64\n"
        "#define $VELOCITY 99\n"
        "<global> volume=-6 ampeg_release=0.5\n"
        "<master> pan=10 ampeg_attack=0.01\n"
        "<group> group_label=Soft group tune=5 ampeg_attack=0.02 lovel=$VEL hivel=$VELOCITY\n"
        "<region> sample=C4 soft.wav key=c5\n"
        "<region> sample=other.wav lokey=a#3 hikey=Db4 pitch_keycenter=sample volume=-3 // the rest is a comment hikey=1\n"
        "/* a block comment\n"
        "<region> sample=hidden.wav\n"
        "*/\n"
        "<master> ampeg_attack=0.03\n"
        "<region> sample=..\\Other\\x.wav\n"
        "<global>\n"
        "<region>sample=y.wav lokey=1\n";
    ImportedInstrument in;
    std::string error;
    const bool ok = parseSfz(text, dir / "inst.sfz", in, error);
    check(ok && in.zones.size() == 4, "SFZ: four regions read", "%.0f zones", static_cast<double>(in.zones.size()));
    if (in.zones.size() != 4) return;
    const ImportedZone &a = in.zones[0], &b = in.zones[1], &c = in.zones[2], &d = in.zones[3];
    check(a.sample == expectedPath(dir, "Samples/Piano Set/C4 soft.wav"), "SFZ: default_path, spaces, backslashes", "");
    std::printf("      %s\n", a.sample.c_str());
    check(a.gainDb == -6 && a.ampRelease == 0.5, "SFZ: <global> reaches a region", "volume %.1f, release %.2f", a.gainDb, a.ampRelease);
    check(a.pan == 10 && a.ampAttack == 0.02, "SFZ: <master> reaches it, <group> overrides it", "pan %.1f, attack %.3f", a.pan, a.ampAttack);
    check(a.loVel == 64 && a.hiVel == 99 && a.tuneCents == 5, "SFZ: #define, the longest name first", "lovel %.0f hivel %.0f tune %.1f", a.loVel,
          a.hiVel, a.tuneCents);
    check(a.loKey == 72 && a.hiKey == 72 && a.rootKey == 72 && !a.rootFromFile, "SFZ: key=c5 sets lokey, hikey and the root", "%.0f %.0f %.0f",
          a.loKey, a.hiKey, a.rootKey);
    check(b.gainDb == -3 && b.ampAttack == 0.02 && b.loVel == 64, "SFZ: a region overrides, keeps the rest", "volume %.1f attack %.3f", b.gainDb,
          b.ampAttack);
    check(b.loKey == 58 && b.hiKey == 61 && b.rootFromFile, "SFZ: note names, pitch_keycenter=sample", "%.0f..%.0f", b.loKey, b.hiKey);
    check(c.ampAttack == 0.03 && c.pan == 0 && c.gainDb == -6 && c.tuneCents == 0 && c.loVel == 0, "SFZ: <master> clears master and group",
          "attack %.3f pan %.1f volume %.1f", c.ampAttack, c.pan, c.gainDb);
    check(c.sample == expectedPath(dir, "Samples/Other/x.wav"), "SFZ: .. in a path is resolved", "");
    check(d.gainDb == 0 && d.ampRelease < 0 && d.loKey == 1, "SFZ: <global> clears everything", "volume %.1f release %.1f", d.gainDb, d.ampRelease);
    check(in.name == "Soft group", "SFZ: group_label names the instrument", "");
}

static void testSfzOpcodes(const fs::path& dir) {
    const std::string text =
        "<region> sample=all.wav lokey=10 hikey=20 lovel=5 hivel=100 pitch_keycenter=15 pitch_keytrack=50 tune=-12.5 transpose=2\n"
        "volume=-3.5 pan=-40 offset=100 end=9999 loop_mode=loop_sustain loop_start=200 loop_end=899 loop_crossfade=0.05\n"
        "direction=reverse trigger=release group=7 off_by=8 seq_length=4 seq_position=3 lorand=0.25 hirand=0.75\n"
        "ampeg_delay=0.1 ampeg_attack=0.2 ampeg_hold=0.3 ampeg_decay=0.4 ampeg_sustain=55 ampeg_release=0.6 amp_veltrack=80\n"
        "cutoff=1234.5 resonance=6 fil_type=hpf_2p fileg_delay=0.01 fileg_attack=0.02 fileg_hold=0.03 fileg_decay=0.04\n"
        "fileg_sustain=45 fileg_release=0.05 fileg_depth=2400 fil_veltrack=1200 fil_keytrack=50 pitchlfo_freq=5.5 pitchlfo_depth=25\n"
        "bend_up=1200 bend_down=-1200 output=3 region_label=All of them\n"
        "<region> sample=b.wav loopmode=one_shot loopstart=1 loopend=10 offby=3 filtype=lpf_4p pitch=7 trigger=first\n"
        "<region> sample=c.wav loop_mode=loop_continuous fil_type=bpf_2p trigger=legato direction=forward\n"
        "<region> sample=d.wav loop_mode=no_loop fil_type=brf_2p\n"
        "<region> sample=e.wav pan=500 volume=-1000 lokey=-5 hikey=300 hivel=200 seq_length=0 lorand=-1 hirand=3 ampeg_sustain=150\n"
        "resonance=99 tune=99999 transpose=-500 bend_up=1e9 cutoff=-5 ampeg_attack=-1 offset=-10 output=99999\n"
        "<region> sample=f.wav volume=loud pan=1x tune=1,5 lokey=h4 hikey=c#\n"
        "<region> sample=My Sample 01.wav lokey=g9 hikey=c-1\n"
        "<region> sample=g.wav lokey=c-1 hikey=g9 pitch_keycenter=Eb4 hivel=1e2\n"
        "<region> sample=/abs/olute/h.wav key=B3\n";
    ImportedInstrument in;
    std::string error;
    parseSfz(text, dir / "ops.sfz", in, error);
    check(in.zones.size() == 8, "SFZ: opcode regions read", "%.0f zones (the empty key range one left out)", static_cast<double>(in.zones.size()));
    if (in.zones.size() != 8) return;
    const ImportedZone& z = in.zones[0];
    check(z.sample == expectedPath(dir, "all.wav") && z.label == "All of them", "SFZ: sample, region_label with spaces", "");
    check(z.loKey == 10 && z.hiKey == 20 && z.loVel == 5 && z.hiVel == 100, "SFZ: lokey hikey lovel hivel", "%.0f %.0f %.0f %.0f", z.loKey, z.hiKey,
          z.loVel, z.hiVel);
    check(z.rootKey == 15 && !z.rootFromFile && z.keyTrack == 50, "SFZ: pitch_keycenter, pitch_keytrack", "%.0f %.0f", z.rootKey, z.keyTrack);
    check(z.tuneCents == 187.5, "SFZ: tune + transpose*100", "%.2f cents", z.tuneCents);
    check(z.gainDb == -3.5 && z.pan == -40, "SFZ: volume, pan", "%.2f dB, %.0f", z.gainDb, z.pan);
    check(z.start == 100 && z.end == 10000, "SFZ: offset; end becomes one past the last frame", "%.0f..%.0f", static_cast<double>(z.start),
          static_cast<double>(z.end));
    check(z.loopMode == 2 && z.hasLoopPoints && z.loopStart == 200 && z.loopEnd == 900, "SFZ: loop_mode, loop_start, loop_end + 1",
          "mode %.0f, %.0f..%.0f", z.loopMode, static_cast<double>(z.loopStart), static_cast<double>(z.loopEnd));
    check(z.loopCrossfade == 0.05 && z.reverse && z.trigger == 1, "SFZ: loop_crossfade, direction, trigger", "%.3f %.0f %.0f", z.loopCrossfade,
          z.reverse, z.trigger);
    check(z.group == 7 && z.offBy == 8 && z.seqLength == 4 && z.seqPosition == 3, "SFZ: group, off_by, seq_length, seq_position",
          "%.0f %.0f %.0f %.0f", z.group, z.offBy, z.seqLength, z.seqPosition);
    check(z.loRand == 0.25 && z.hiRand == 0.75, "SFZ: lorand, hirand", "%.2f %.2f", z.loRand, z.hiRand);
    check(z.ampDelay == 0.1 && z.ampAttack == 0.2 && z.ampHold == 0.3 && z.ampDecay == 0.4, "SFZ: ampeg delay attack hold decay", "%.1f %.1f %.1f %.1f",
          z.ampDelay, z.ampAttack, z.ampHold, z.ampDecay);
    check(z.ampSustain == 55 && z.ampRelease == 0.6 && z.ampVelTrack == 80, "SFZ: ampeg sustain release, amp_veltrack", "%.0f %.1f %.0f",
          z.ampSustain, z.ampRelease, z.ampVelTrack);
    check(z.cutoff == 1234.5 && z.resonance == 6 && z.filterType == 2, "SFZ: cutoff, resonance, fil_type", "%.1f %.0f %.0f", z.cutoff, z.resonance,
          z.filterType);
    check(z.filDelay == 0.01 && z.filAttack == 0.02 && z.filHold == 0.03 && z.filDecay == 0.04, "SFZ: fileg delay attack hold decay", "%.2f %.2f",
          z.filDelay, z.filDecay);
    check(z.filSustain == 45 && z.filRelease == 0.05 && z.filDepth == 2400, "SFZ: fileg sustain release depth", "%.0f %.2f %.0f", z.filSustain,
          z.filRelease, z.filDepth);
    check(z.filVelTrack == 1200 && z.filKeyTrack == 50, "SFZ: fil_veltrack, fil_keytrack", "%.0f %.0f", z.filVelTrack, z.filKeyTrack);
    check(z.pitchLfoFreq == 5.5 && z.pitchLfoDepth == 25, "SFZ: pitchlfo_freq, pitchlfo_depth", "%.1f %.0f", z.pitchLfoFreq, z.pitchLfoDepth);
    check(z.bendUp == 1200 && z.bendDown == -1200 && z.outputBus == 3, "SFZ: bend_up, bend_down, output", "%.0f %.0f %.0f", z.bendUp, z.bendDown,
          z.outputBus);

    const ImportedZone& b = in.zones[1];
    check(b.loopMode == 3 && b.loopStart == 1 && b.loopEnd == 11 && b.offBy == 3 && b.filterType == 6 && b.tuneCents == 7 && b.trigger == 2,
          "SFZ: loopmode loopstart loopend offby filtype pitch", "mode %.0f end %.0f type %.0f", b.loopMode, static_cast<double>(b.loopEnd),
          b.filterType);
    const ImportedZone &c = in.zones[2], &d = in.zones[3];
    check(c.loopMode == 1 && c.filterType == 4 && c.trigger == 3 && !c.reverse && d.loopMode == 0 && d.filterType == 5,
          "SFZ: the other loop modes, filter types, triggers", "%.0f %.0f %.0f", c.loopMode, d.loopMode, d.filterType);
    check(!d.hasLoopPoints && d.end == 0 && d.loopMode == 0 && d.ampAttack < 0 && d.cutoff == 0, "SFZ: what a region does not set keeps its default",
          "%.0f", 0);

    const ImportedZone& e = in.zones[4];
    check(e.pan == 100 && e.gainDb == -144 && e.loKey == 0 && e.hiKey == 127 && e.hiVel == 127, "SFZ: out of range values clamped (1)",
          "pan %.0f volume %.0f keys %.0f..%.0f", e.pan, e.gainDb, e.loKey, e.hiKey);
    check(e.seqLength == 1 && e.loRand == 0 && e.hiRand == 1 && e.ampSustain == 100 && e.resonance == 40, "SFZ: out of range values clamped (2)",
          "seq %.0f rand %.2f..%.2f", e.seqLength, e.loRand, e.hiRand);
    check(e.tuneCents == 9600 - 12700 && e.bendUp == 9600 && e.cutoff == 0 && e.ampAttack == 0 && e.start == 0 && e.outputBus == 1023,
          "SFZ: out of range values clamped (3)", "tune %.0f bend %.0f", e.tuneCents, e.bendUp);
    const ImportedZone& f = in.zones[5];
    check(f.gainDb == 0 && f.pan == 0 && f.tuneCents == 0 && f.loKey == 0 && f.hiKey == 127, "SFZ: values that are not numbers are ignored",
          "%.1f %.1f %.1f", f.gainDb, f.pan, f.tuneCents);
    const ImportedZone& g = in.zones[6];
    check(g.loKey == 0 && g.hiKey == 127 && g.rootKey == 63 && g.hiVel == 100, "SFZ: c-1 = 0, g9 = 127, Eb4 = 63, 1e2", "%.0f %.0f %.0f", g.loKey,
          g.hiKey, g.rootKey);
    const ImportedZone& h = in.zones[7];
    check(h.loKey == 59 && h.hiKey == 59 && h.sample == fs::path("/abs/olute/h.wav").lexically_normal().u8string(), "SFZ: B3 = 59, absolute path kept",
          "%.0f", h.loKey);
    bool found = false;
    for (const ImportedZone& zz : in.zones) found = found || zz.sample == expectedPath(dir, "My Sample 01.wav");
    check(!found && hasWarning(in, "empty key or velocity range"), "SFZ: a region with lokey > hikey is left out", "");
}

static void testSfzOffsetsAndWarnings(const fs::path& dir) {
    const std::string text =
        "<control> note_offset=1 octave_offset=-1 set_cc1=64\n"
        "<group> amp_velcurve_1=0 amp_velcurve_127=1 eq1_freq=100\n"
        "<region> sample=a.wav lokey=c4 hikey=72 pitch_keycenter=60\n"
        "<region> sample=*sine\n"
        "<region> lokey=1\n"
        "<region> sample=b.wav hikey=-1\n"
        "<region> sample=c.wav end=-1\n"
        "<effect> type=fverb\n"
        "<curve> v000=0\n"
        "<region> sample=d.wav loop_mode=bounce eq2_freq=50\n";
    ImportedInstrument in;
    std::string error;
    parseSfz(text, dir / "Offset Test.sfz", in, error);
    check(in.zones.size() == 2, "SFZ: regions with *sine, no sample, hikey=-1, end=-1 left out", "%.0f zones", static_cast<double>(in.zones.size()));
    if (in.zones.size() != 2) return;
    const ImportedZone& a = in.zones[0];
    check(a.loKey == 49 && a.hiKey == 61 && a.rootKey == 49, "SFZ: note_offset and octave_offset", "%.0f..%.0f root %.0f", a.loKey, a.hiKey, a.rootKey);
    check(in.name == "Offset Test", "SFZ: without labels the file's stem names it", "");
    for (const std::string& w : in.warnings) std::printf("      warning: %s\n", w.c_str());
    check(hasWarning(in, "opcode amp_velcurve_N not taken over (2 times)"), "SFZ: unknown opcodes, one line per kind", "");
    check(hasWarning(in, "opcode eqN_freq not taken over (2 times)") && hasWarning(in, "opcode set_ccN not taken over"),
          "SFZ: numbered opcodes counted as one kind", "");
    check(hasWarning(in, "sample=*sine") && hasWarning(in, "without a sample") && hasWarning(in, "hikey=-1") && hasWarning(in, "end=-1"),
          "SFZ: each left-out region named", "");
    check(hasWarning(in, "<effect>") && hasWarning(in, "<curve>") && hasWarning(in, "loop_mode=bounce"), "SFZ: skipped headers and values named",
          "");

    // Many regions: no more than the cap.
    std::string many;
    for (int i = 0; i < 5000; ++i) many += "<region> sample=a.wav\n";
    parseSfz(many, dir / "many.sfz", in, error);
    check(in.zones.size() == 4096 && hasWarning(in, "more than 4096"), "SFZ: no more than 4096 zones", "%.0f", static_cast<double>(in.zones.size()));

    check(!parseSfz("<region> lokey=1\n", dir / "x.sfz", in, error) && !error.empty(), "SFZ: nothing playable is an error", "");
}

static void testSfzInclude(const fs::path& dir) {
    fs::create_directories(dir / "sub");
    // A non-ASCII folder and file name, in UTF-8.
    const std::string accented = "Fl\xc3\xbcgel";
    writeText(dir / "main.sfz",
              "#define $K 50\n"
              "<global> volume=-2\n"
              "<region> sample=" + accented + "\\\xc3\xa4 \xc3\xb6.wav key=$K\n"
              "#include \"sub/inc.sfz\"\n"
              "#include \"missing.sfz\"\n"
              "<region> sample=last.wav key=43\n");
    writeText(dir / "sub" / "inc.sfz",
              "<region> sample=inc.wav key=$K tune=1\n"
              "#include \"inc2.sfz\"\n");
    writeText(dir / "sub" / "inc2.sfz",
              "<region> sample=inc2.wav key=41\n"
              "#include \"../main.sfz\"\n"
              "#include \"inc2.sfz\"\n");
    ImportedInstrument in;
    std::string error;
    const bool ok = readSfzFile(dir / "main.sfz", in, error);
    check(ok && in.zones.size() == 4, "SFZ #include: four regions over three files", "%.0f zones", static_cast<double>(in.zones.size()));
    if (in.zones.size() != 4) return;
    check(in.zones[0].sample == expectedPath(dir, accented + "/\xc3\xa4 \xc3\xb6.wav"), "SFZ: non-ASCII path kept in UTF-8", "");
    std::printf("      %s\n", in.zones[0].sample.c_str());
    check(in.zones[1].loKey == 50 && in.zones[1].tuneCents == 1 && in.zones[1].gainDb == -2, "SFZ #include: #define and <global> carry into it",
          "key %.0f", in.zones[1].loKey);
    check(in.zones[1].sample == expectedPath(dir, "inc.wav"), "SFZ #include: samples relative to the opened .sfz", "");
    check(in.zones[2].loKey == 41 && in.zones[3].loKey == 43, "SFZ #include: nested, relative to the including file", "%.0f %.0f",
          in.zones[2].loKey, in.zones[3].loKey);
    check(hasWarning(in, "includes itself") && hasWarning(in, "not found: missing.sfz"), "SFZ #include: cycles and missing files", "");
}

// ---------------------------------------------------------------- SF2 writer

struct TGen {
    int oper;
    uint16_t amount;
};
static TGen G(int oper, int amount) { return {oper, static_cast<uint16_t>(amount)}; }
static TGen R(int oper, int lo, int hi) { return {oper, static_cast<uint16_t>(lo | (hi << 8))}; }
struct TMod {
    uint16_t src, dest;
    int16_t amount;
};
struct TZone {
    std::vector<TGen> gens;
    std::vector<TMod> mods;
};
struct TInst {
    std::string name;
    std::vector<TZone> zones;
};
struct TPreset {
    std::string name;
    int program, bank;
    std::vector<TZone> zones;
};
struct TSample {
    std::string name;
    std::vector<int32_t> data; // 24-bit values
    uint32_t rate;
    int pitch, correction, link, type;
    uint32_t loopStart, loopEnd; // relative to the sample
};
struct TBank {
    std::string name = "Test Bank";
    int major = 2, minor = 4;
    bool sm24 = true;
    bool brokenTable = false;
    std::vector<TPreset> presets;
    std::vector<TInst> insts;
    std::vector<TSample> samples;
};

struct Bytes {
    std::vector<uint8_t> b;
    void u8(int v) { b.push_back(static_cast<uint8_t>(v)); }
    void u16(int v) { u8(v & 0xFF); u8((v >> 8) & 0xFF); }
    void u32(uint32_t v) { u16(static_cast<int>(v & 0xFFFF)); u16(static_cast<int>(v >> 16)); }
    void tag(const char* t) { for (int i = 0; i < 4; ++i) u8(t[i]); }
    void name(const std::string& s, size_t length) { for (size_t i = 0; i < length; ++i) u8(i < s.size() ? s[i] : 0); }
    void add(const std::vector<uint8_t>& o) { b.insert(b.end(), o.begin(), o.end()); }
};

static std::vector<uint8_t> chunk(const char* id, const std::vector<uint8_t>& body) {
    Bytes c;
    c.tag(id);
    c.u32(static_cast<uint32_t>(body.size()));
    c.add(body);
    if (body.size() & 1) c.u8(0);
    return c.b;
}

static std::vector<uint8_t> list(const char* type, const std::vector<std::vector<uint8_t>>& chunks) {
    Bytes body;
    body.tag(type);
    for (const auto& c : chunks) body.add(c);
    return chunk("LIST", body.b);
}

template <typename Z>
static void zones(const std::vector<Z>& owners, Bytes& bag, Bytes& mod, Bytes& gen, std::vector<int>& firstBag) {
    int bags = 0, gens = 0, mods = 0;
    for (const auto& o : owners) {
        firstBag.push_back(bags);
        for (const TZone& z : o.zones) {
            bag.u16(gens);
            bag.u16(mods);
            ++bags;
            for (const TGen& g : z.gens) {
                gen.u16(g.oper);
                gen.u16(g.amount);
                ++gens;
            }
            for (const TMod& m : z.mods) {
                mod.u16(m.src);
                mod.u16(m.dest);
                mod.u16(m.amount);
                mod.u16(0);
                mod.u16(0);
                ++mods;
            }
        }
    }
    firstBag.push_back(bags);
    bag.u16(gens);
    bag.u16(mods);
    for (int i = 0; i < 10; ++i) mod.u8(0);
    gen.u32(0);
}

static std::vector<uint8_t> writeSf2(const TBank& bank) {
    Bytes ifil, inam;
    ifil.u16(bank.major);
    ifil.u16(bank.minor);
    inam.name(bank.name, bank.name.size() + 1);
    const std::vector<uint8_t> info = list("INFO", {chunk("ifil", ifil.b), chunk("INAM", inam.b)});

    Bytes smpl, sm24, shdr;
    std::vector<uint32_t> starts;
    for (const TSample& s : bank.samples) {
        starts.push_back(static_cast<uint32_t>(smpl.b.size() / 2));
        for (int32_t v : s.data) {
            smpl.u16(static_cast<int>(static_cast<uint32_t>(v) >> 8) & 0xFFFF); // the high 16 bits
            sm24.u8(v & 0xFF);
        }
        for (int i = 0; i < 46; ++i) {
            smpl.u16(0);
            sm24.u8(0);
        }
    }
    for (size_t i = 0; i < bank.samples.size(); ++i) {
        const TSample& s = bank.samples[i];
        shdr.name(s.name, 20);
        shdr.u32(starts[i]);
        shdr.u32(starts[i] + static_cast<uint32_t>(s.data.size()));
        shdr.u32(starts[i] + s.loopStart);
        shdr.u32(starts[i] + s.loopEnd);
        shdr.u32(s.rate);
        shdr.u8(s.pitch);
        shdr.u8(s.correction & 0xFF);
        shdr.u16(s.link);
        shdr.u16(s.type);
    }
    shdr.name("EOS", 46);
    std::vector<std::vector<uint8_t>> sdta = {chunk("smpl", smpl.b)};
    if (bank.sm24) sdta.push_back(chunk("sm24", sm24.b));

    Bytes phdr, pbag, pmod, pgen, inst, ibag, imod, igen;
    std::vector<int> presetBags, instBags;
    zones(bank.presets, pbag, pmod, pgen, presetBags);
    zones(bank.insts, ibag, imod, igen, instBags);
    for (size_t i = 0; i < bank.presets.size(); ++i) {
        phdr.name(bank.presets[i].name, 20);
        phdr.u16(bank.presets[i].program);
        phdr.u16(bank.presets[i].bank);
        phdr.u16(presetBags[i]);
        phdr.u32(0);
        phdr.u32(0);
        phdr.u32(0);
    }
    phdr.name("EOP", 24);
    phdr.u16(presetBags.back());
    for (int i = 0; i < 12; ++i) phdr.u8(0);
    for (size_t i = 0; i < bank.insts.size(); ++i) {
        inst.name(bank.insts[i].name, 20);
        inst.u16(instBags[i]);
    }
    inst.name("EOI", 20);
    inst.u16(instBags.back());
    if (bank.brokenTable) pgen.u8(0);
    const std::vector<uint8_t> pdta =
        list("pdta", {chunk("phdr", phdr.b), chunk("pbag", pbag.b), chunk("pmod", pmod.b), chunk("pgen", pgen.b), chunk("inst", inst.b),
                      chunk("ibag", ibag.b), chunk("imod", imod.b), chunk("igen", igen.b), chunk("shdr", shdr.b)});
    Bytes riff;
    riff.tag("sfbk");
    riff.add(info);
    riff.add(list("sdta", sdta));
    riff.add(pdta);
    return chunk("RIFF", riff.b);
}

static TBank testBank() {
    TBank bank;
    TSample mono{"Mono", {}, 44100, 60, -5, 0, 1, 20, 80};
    for (int i = 0; i < 100; ++i) mono.data.push_back(((i * 7919) % 65536 - 32768) * 256 + (i * 31) % 256);
    mono.data[1] = -8388608; // full scale, both ways
    mono.data[2] = 8388607;
    TSample left{"Left", {}, 48000, 64, 0, 2, 4, 0, 0}, right{"Right", {}, 48000, 64, 0, 1, 2, 0, 0};
    for (int i = 0; i < 50; ++i) {
        left.data.push_back(i * 1000);
        right.data.push_back(-i * 1000);
    }
    TSample rom{"Rom", std::vector<int32_t>(10, 0), 22050, 60, 0, 0, 0x8001, 0, 0};
    bank.samples = {mono, left, right, rom};

    TInst piano{"Piano", {}};
    piano.zones.push_back({{G(34, 0), G(48, 100), G(54, 1)}, {}}); // global: attack 1 s, 10 cB... (see below), loop
    piano.zones.push_back({{R(43, 0, 63), R(44, 0, 100), G(58, 62), G(51, 1), G(52, 10), G(0, 5), G(1, -10), G(2, 2), G(3, -3), G(17, -250),
                            G(57, 3), G(38, 1200), G(37, 60), G(8, 6900), G(9, 50), G(11, 2400), G(28, -1200), G(29, 500), G(6, 30),
                            G(56, 50), G(33, -12000), G(35, -2400), G(36, 2400), G(53, 0)},
                           {}});
    piano.zones.push_back({{G(17, 100)}, {}}); // not the first and without a sample: ignored
    piano.zones.push_back({{R(43, 64, 127), G(46, 60), G(54, 3), G(53, 0), G(17, 400)}, {}}); // pan after sampleID: ignored
    piano.zones.push_back({{R(43, 0, 127), G(53, 3)}, {}});                               // the ROM sample
    TInst stereo{"Stereo", {}};
    stereo.zones.push_back({{G(17, -500), G(53, 1)}, {{0x0081, 8, 1000}}});
    stereo.zones.push_back({{G(17, 500), G(53, 2)}, {{0x0502, 48, 960}}}); // a built-in modulator: not counted
    bank.insts = {piano, stereo};

    TPreset grand{"Grand", 0, 0, {}};
    grand.zones.push_back({{R(43, 10, 70), G(48, 50)}, {}});
    grand.zones.push_back({{G(51, 2), G(34, -1200), G(0, 1000), G(41, 0)}, {{0x0081, 17, 300}}}); // a sample offset is not allowed here
    grand.zones.push_back({{R(43, 100, 127), G(41, 0)}, {}});
    TPreset strings{"Strings", 5, 1, {}};
    strings.zones.push_back({{R(44, 20, 127), G(41, 1)}, {}});
    bank.presets = {grand, strings};
    return bank;
}

static void testSf2(const fs::path& dir) {
    const TBank bank = testBank();
    const std::vector<uint8_t> file = writeSf2(bank);
    Sf2File f;
    std::string error;
    const bool opened = openSf2(file.data(), file.size(), f, error);
    check(opened, "SF2: opens", "");
    if (!opened) {
        std::printf("      %s\n", error.c_str());
        return;
    }
    const std::vector<Sf2Preset> presets = sf2Presets(f);
    check(presets.size() == 2 && presets[0].name == "Grand" && presets[1].name == "Strings" && presets[1].bank == 1 && presets[1].program == 5,
          "SF2: two presets, EOP not among them", "%.0f presets", static_cast<double>(presets.size()));
    check(sf2SampleCount(f) == 4 && f.name == "Test Bank", "SF2: four samples, EOS not among them; INAM", "%.0f", sf2SampleCount(f));

    ImportedInstrument in;
    const bool ok = sf2Instrument(f, 0, in, error);
    check(ok && in.zones.size() == 3 && in.name == "Grand", "SF2: preset 0 has three zones", "%.0f zones", static_cast<double>(in.zones.size()));
    for (const std::string& w : in.warnings) std::printf("      warning: %s\n", w.c_str());
    if (in.zones.size() == 3) {
        const ImportedZone &a = in.zones[0], &b = in.zones[1], &c = in.zones[2];
        check(a.loKey == 10 && a.hiKey == 63 && a.loVel == 0 && a.hiVel == 100, "SF2: key ranges intersected with the preset's", "%.0f..%.0f",
              a.loKey, a.hiKey);
        check(b.loKey == 64 && b.hiKey == 70 && c.loKey == 100 && c.hiKey == 127, "SF2: a local preset range replaces its global one", "%.0f..%.0f",
              c.loKey, c.hiKey);
        check(a.sf2Sample == 0 && a.sf2Link == -1 && a.label == "Mono" && a.sample.empty(), "SF2: sample index and name", "%.0f", a.sf2Sample);
        check(a.rootKey == 62 && !a.rootFromFile && b.rootKey == 60, "SF2: overridingRootKey, else the sample's pitch", "%.0f %.0f", a.rootKey,
              b.rootKey);
        check(a.tuneCents == 305 && b.tuneCents == 195, "SF2: coarse and fine added over both levels, pitch correction", "%.0f %.0f", a.tuneCents,
              b.tuneCents);
        check(a.keyTrack == 50 && b.keyTrack == 100, "SF2: scaleTuning", "%.0f %.0f", a.keyTrack, b.keyTrack);
        check(near(a.gainDb, -0.4 * 15.0) && near(c.gainDb, -6.0), "SF2: attenuation added, 0.4 x centibels", "%.2f dB", a.gainDb);
        check(a.pan == -50 && b.pan == 0 && a.group == 3 && a.offBy == 3, "SF2: pan, exclusive class; a generator after sampleID ignored", "%.0f %.0f",
              a.pan, b.pan);
        check(a.start == 5 && a.end == 90 && b.start == 0 && b.end == 0, "SF2: start and end offsets (none from the preset)", "%.0f..%.0f",
              static_cast<double>(a.start), static_cast<double>(a.end));
        check(a.hasLoopPoints && a.loopStart == 22 && a.loopEnd == 77 && b.loopStart == 20 && b.loopEnd == 80, "SF2: loop points relative to the sample",
              "%.0f..%.0f", static_cast<double>(a.loopStart), static_cast<double>(a.loopEnd));
        check(a.loopMode == 1 && b.loopMode == 2, "SF2: sampleModes from the global zone, and 3", "%.0f %.0f", a.loopMode, b.loopMode);
        check(near(a.ampAttack, 0.5) && near(c.ampAttack, 1.0), "SF2: attack timecents added: 0 + -1200 = 0.5 s", "%.4f %.4f", a.ampAttack, c.ampAttack);
        check(near(a.ampRelease, 2.0) && near(a.ampHold, 0.25) && near(a.ampDecay, 4.0) && near(a.ampDelay, std::pow(2.0, -10.0)),
              "SF2: timecents to seconds", "%.4f %.4f %.4f", a.ampRelease, a.ampHold, a.ampDecay);
        check(near(a.ampSustain, 100.0 * std::pow(10.0, -0.3)) && b.ampSustain < 0 && b.ampRelease < 0, "SF2: sustain 60 cB = 50.1 %, unset stays -1",
              "%.4f", a.ampSustain);
        check(near(a.cutoff, 8.175798915643707 * std::pow(2.0, 5.75)) && a.resonance == 5 && b.cutoff == 0, "SF2: filter cents to Hz; 13500 = none",
              "%.2f Hz", a.cutoff);
        check(a.filDepth == 2400 && near(a.filDecay, 0.5) && a.filSustain == 50 && a.filAttack < 0, "SF2: modulation envelope to the filter", "%.0f %.2f",
              a.filDepth, a.filSustain);
        check(a.pitchLfoDepth == 30 && near(a.pitchLfoFreq, 8.175798915643707), "SF2: vibrato depth, frequency 0 cents = 8.176 Hz", "%.3f Hz",
              a.pitchLfoFreq);
        check(hasWarning(in, "ROM samples left out (2 times)") && hasWarning(in, "fixed key") && hasWarning(in, "modulators not taken over"),
              "SF2: warnings for ROM, keynum, modulators", "");
    }

    const bool ok1 = sf2Instrument(f, 1, in, error);
    check(ok1 && in.zones.size() == 2, "SF2: preset 1, a stereo pair", "%.0f zones", static_cast<double>(in.zones.size()));
    if (in.zones.size() == 2) {
        const ImportedZone &l = in.zones[0], &r = in.zones[1];
        check(l.sf2Sample == 1 && l.sf2Link == 2 && r.sf2Sample == 2 && r.sf2Link == 1, "SF2: left and right linked", "%.0f %.0f", l.sf2Link, r.sf2Link);
        check(l.pan == -100 && r.pan == 100 && l.loVel == 20 && r.loVel == 20 && l.rootKey == 64, "SF2: panned as the generators say", "%.0f %.0f",
              l.pan, r.pan);
        check(!l.hasLoopPoints && l.loopMode == 0, "SF2: no loop when the header has none", "");
        check(hasWarning(in, "modulators not taken over") && in.warnings.size() == 1, "SF2: built-in modulators not counted", "%.0f warnings",
              static_cast<double>(in.warnings.size()));
    }
    check(!sf2Instrument(f, 2, in, error) && !sf2Instrument(f, -1, in, error), "SF2: the EOP record is not a preset", "");

    // Sample data.
    AudioData a;
    const bool s0 = sf2Sample(f, file.data(), file.size(), 0, a, error);
    bool exact = s0 && a.frames() == 100;
    for (size_t i = 0; exact && i < 100; ++i) exact = a.samples[0][i] == static_cast<float>(bank.samples[0].data[i]) / 8388608.0f;
    check(exact && a.bits == 24 && a.channels == 1, "SF2: 24-bit data (smpl + sm24) exact", "%.0f bits", a.bits);
    check(a.samples[0][1] == -1.0f && a.sampleRate == 44100 && a.rootKey == 60 && a.tuneCents == -5 && a.format == "SF2",
          "SF2: rate, pitch, correction, format", "%.0f Hz", a.sampleRate);
    check(a.loops.size() == 1 && a.loops[0].start == 20 && a.loops[0].end == 80, "SF2: loop relative to the sample, end one past", "%.0f..%.0f",
          a.loops.empty() ? 0.0 : static_cast<double>(a.loops[0].start), a.loops.empty() ? 0.0 : static_cast<double>(a.loops[0].end));
    sf2Sample(f, file.data(), file.size(), 2, a, error);
    check(a.frames() == 50 && a.samples[0][3] == static_cast<float>(-3000) / 8388608.0f && a.loops.empty() && a.sampleRate == 48000,
          "SF2: the second channel of the pair", "%.0f frames", static_cast<double>(a.frames()));
    check(!sf2Sample(f, file.data(), file.size(), 3, a, error) && !sf2Sample(f, file.data(), file.size(), 4, a, error) &&
              !sf2Sample(f, file.data(), file.size(), -1, a, error),
          "SF2: ROM sample, EOS and -1 refused", "");

    TBank old = bank;
    old.major = 2;
    old.minor = 1;
    const std::vector<uint8_t> file16 = writeSf2(old);
    Sf2File f16;
    openSf2(file16.data(), file16.size(), f16, error);
    sf2Sample(f16, file16.data(), file16.size(), 0, a, error);
    exact = a.frames() == 100;
    for (size_t i = 0; exact && i < 100; ++i)
        exact = a.samples[0][i] == static_cast<float>(static_cast<int16_t>(static_cast<uint32_t>(bank.samples[0].data[i]) >> 8)) / 32768.0f;
    check(exact && a.bits == 16, "SF2: 16-bit data exact; sm24 ignored before version 2.04", "%.0f bits", a.bits);
    TBank no24 = bank;
    no24.sm24 = false;
    const std::vector<uint8_t> fileNo24 = writeSf2(no24);
    Sf2File fn;
    openSf2(fileNo24.data(), fileNo24.size(), fn, error);
    sf2Sample(fn, fileNo24.data(), fileNo24.size(), 0, a, error);
    check(a.bits == 16 && a.frames() == 100 && a.samples[0][2] == 32767.0f / 32768.0f, "SF2: without sm24, 16 bits", "%.0f bits", a.bits);

    TBank broken = bank;
    broken.brokenTable = true;
    const std::vector<uint8_t> fileBroken = writeSf2(broken);
    check(!openSf2(fileBroken.data(), fileBroken.size(), fn, error) && !error.empty(), "SF2: a table not a whole number of records refused", "");
    check(!openSf2(file.data(), 11, fn, error) && !openSf2(nullptr, 100, fn, error), "SF2: too short, or no data", "");

    // From a file: the hydra first, then one sample by seeking.
    const fs::path path = dir / "bank.sf2";
    writeText(path, std::string(file.begin(), file.end()));
    Sf2File ff;
    const bool fileOk = openSf2File(path, ff, error) && sf2SampleCount(ff) == 4 && sf2Presets(ff).size() == 2;
    AudioData b;
    const bool sampleOk = fileOk && sf2SampleFromFile(ff, path, 0, b, error) && b.frames() == 100;
    exact = sampleOk;
    for (size_t i = 0; exact && i < 100; ++i) exact = b.samples[0][i] == static_cast<float>(bank.samples[0].data[i]) / 8388608.0f;
    check(exact, "SF2: from a file, the same samples", "");
    check(!openSf2File(dir / "nothing.sf2", ff, error), "SF2: a missing file is an error", "");
}

// ---------------------------------------------------------------- Scala

static std::string scale(const std::string& name, const std::vector<std::string>& pitches) {
    std::string s = "! test.scl\n!\n" + name + "\n " + std::to_string(pitches.size()) + "\n!\n";
    for (const std::string& p : pitches) s += " " + p + "\n";
    return s;
}

static void testScala() {
    const Tuning eq = equalTuning();
    bool zero = true;
    for (double c : eq.cents) zero = zero && c == 0.0;
    check(zero, "Scala: equalTuning() is all zeros", "");

    std::vector<std::string> tet;
    for (int i = 1; i < 12; ++i) tet.push_back(std::to_string(i * 100) + ".0");
    tet.push_back("2/1");
    Tuning t;
    std::string error;
    bool ok = parseScala(scale("12 tone equal temperament", tet), "", t, error);
    double worst = 0.0;
    for (double c : t.cents) worst = std::max(worst, std::fabs(c));
    check(ok && worst < 1e-6 && t.name == "12 tone equal temperament", "Scala: 12-tone equal scale gives zeros", "largest %.2e cents", worst);

    // Five-limit just intonation, against cents worked out by hand (1200 log2 of each ratio).
    const std::vector<std::string> just = {"16/15", "9/8", "6/5", "5/4", "4/3", "45/32", "3/2", "8/5", "5/3", "9/5", "15/8", "2/1"};
    ok = parseScala(scale("just", just), "", t, error);
    const double expect[][2] = {{60, 0.0},
                                {61, 111.7312852697778 - 100},
                                {62, 203.9100017307748 - 200},
                                {64, 386.3137138648348 - 400},
                                {65, 498.0449991346125 - 500},
                                {67, 701.9550008653874 - 700},
                                {69, 884.3587129994474 - 900},
                                {71, 1088.268714730222 - 1100},
                                {72, 0.0},
                                {48, 0.0},
                                {59, 1088.268714730222 - 1100},
                                {0, 0.0},
                                {127, 701.9550008653874 - 700}};
    bool right = ok;
    for (const auto& e : expect) right = right && near(t.cents[static_cast<int>(e[0])], e[1], 1e-6);
    check(right, "Scala: just intonation against hand-worked cents", "key 67: %.6f, key 69: %.6f", t.cents[67], t.cents[69]);

    // 19-tone equal, mapped linearly with A4 = 440 Hz on key 69.
    std::vector<std::string> nineteen;
    for (int i = 1; i <= 19; ++i) {
        char text[32];
        std::snprintf(text, sizeof(text), "%.10f", i * 1200.0 / 19.0);
        nineteen.push_back(text);
    }
    const double step = 1200.0 / 19.0;
    ok = parseScala(scale("19-EDO", nineteen), "! linear\n0\n0\n127\n60\n69\n440.0\n19\n", t, error);
    right = ok;
    for (int k = 0; k < 128; ++k) right = right && near(t.cents[k], (k - 69) * (step - 100.0), 1e-6);
    check(right && near(t.cents[69], 0.0) && near(t.cents[60], 331.578947368, 1e-6), "Scala: 19-tone, kbm linear, reference 69 = 440",
          "key 60: %.6f, key 88: %.6f", t.cents[60], t.cents[88]);

    // The same scale on a 12-key pattern with two keys unmapped and a limited range.
    const std::string kbm = "! pattern\n12\n48\n84\n60\n69\n440\n19\n! the map\n0\nx\n3\n5\n6\n8\nx\n11\n13\n14\n16\n18\n";
    ok = parseScala(scale("19-EDO", nineteen), kbm, t, error);
    const int map[12] = {0, -1, 3, 5, 6, 8, -1, 11, 13, 14, 16, 18};
    right = ok;
    for (int k = 0; k < 128; ++k) {
        double want = 0.0;
        const int i = k - 60, q = i >= 0 ? i / 12 : -((-i + 11) / 12), r = i - q * 12;
        if (k >= 48 && k <= 84 && map[r] >= 0) want = q * 1200.0 + map[r] * step - 14 * step - (k - 69) * 100.0;
        right = right && near(t.cents[k], want, 1e-6);
    }
    check(right && t.cents[61] == 0.0 && t.cents[47] == 0.0 && t.cents[85] == 0.0 && near(t.cents[60], 900 - 14 * step, 1e-6),
          "Scala: 19-tone on a kbm pattern, unmapped keys at 0", "key 60: %.6f, key 62: %.6f", t.cents[60], t.cents[62]);

    // Ratios, cents, whole numbers and text after the value, mixed.
    const std::vector<std::string> mixed = {"100.0", "9/8", "300.", "5/4 major third", "500.0 cents", "45/32", "700.", "8/5", "900.0", "16/9",
                                            "1100.", "2"};
    ok = parseScala(scale("mixed", mixed), "", t, error);
    check(ok && near(t.cents[62], 3.9100017307748, 1e-6) && near(t.cents[63], 0) && near(t.cents[64], -13.6862861351652, 1e-6) &&
              near(t.cents[65], 0) && near(t.cents[72], 0) && near(t.cents[84], 0),
          "Scala: ratios, cents and whole numbers mixed", "key 62: %.6f, key 64: %.6f", t.cents[62], t.cents[64]);

    check(!parseScala(scale("short", {"100.0"}).replace(scale("short", {"100.0"}).find(" 1\n"), 3, " 3\n"), "", t, error),
          "Scala: fewer pitches than the count refused", "");
    check(!parseScala(scale("bad", {"abc"}), "", t, error) && !parseScala(scale("bad", {"-3/2"}), "", t, error) &&
              !parseScala(scale("bad", {"3/0"}), "", t, error),
          "Scala: unreadable pitches refused", "");
    check(!parseScala(scale("x", nineteen), "12\n0\n127\n60\n69\n440\n19\n0\n1\n2\n3\n4\n5\n6\n7\n8\nx\n", t, error),
          "Scala: an unmapped reference key refused", "");
    check(!parseScala(scale("x", nineteen), "12\n0\n127\n60\n", t, error), "Scala: a cut-off kbm refused", "");
}

// ---------------------------------------------------------------- fuzz

static std::vector<uint8_t> mutate(const std::vector<uint8_t>& in, std::mt19937& rng) {
    std::vector<uint8_t> out = in;
    const int kind = static_cast<int>(rng() % 4);
    if (kind == 0 || out.empty()) { // random bytes
        out.resize(rng() % 600);
        for (auto& b : out) b = static_cast<uint8_t>(rng());
    } else if (kind == 1) { // cut off
        out.resize(rng() % out.size());
    } else { // flips and overwrites
        const int n = 1 + static_cast<int>(rng() % 8);
        for (int i = 0; i < n; ++i) {
            const size_t at = rng() % out.size();
            out[at] = kind == 2 ? static_cast<uint8_t>(out[at] ^ (1u << (rng() % 8))) : static_cast<uint8_t>(rng());
        }
    }
    return out;
}

static bool zoneInBounds(const ImportedZone& z) {
    return z.loKey >= 0 && z.loKey <= z.hiKey && z.hiKey <= 127 && z.loVel >= 0 && z.loVel <= z.hiVel && z.hiVel <= 127 && z.rootKey >= 0 &&
           z.rootKey <= 127 && z.seqLength >= 1 && z.seqPosition >= 1 && z.loopMode >= -1 && z.loopMode <= 3 && z.filterType >= 0 &&
           z.filterType <= 7 && z.trigger >= 0 && z.trigger <= 3 && hnl::isFinite(z.tuneCents) && hnl::isFinite(z.gainDb) && z.pan >= -100 &&
           z.pan <= 100;
}

static void fuzzSfz(const fs::path& dir) {
    const std::string base =
        "<control> default_path=a\\b/ note_offset=2\n#define $X 40\n<global> volume=-3 global_label=G\n<master> pan=5\n"
        "<group> lokey=c3 hikey=$X /* c */ tune=3 // x\n<region> sample=s 1.wav key=e4 loop_mode=loop_sustain loop_end=99\n"
        "<region> sample=*saw\n<region> sample=t.wav lovel=9 hivel=8\n#include \"none.sfz\"\n<effect> x=1\n<region> sample=u.wav end=7\n";
    std::mt19937 rng(12345);
    const std::vector<uint8_t> bytes(base.begin(), base.end());
    int bad = 0, cases = 0;
    for (; cases < 20000; ++cases) {
        const std::vector<uint8_t> m = mutate(bytes, rng);
        ImportedInstrument in;
        std::string error;
        parseSfz(std::string(m.begin(), m.end()), dir / "fuzz.sfz", in, error);
        if (in.zones.size() > 4096 || in.warnings.size() > 65) ++bad;
        for (const ImportedZone& z : in.zones) bad += zoneInBounds(z) && !z.sample.empty() ? 0 : 1;
    }
    check(bad == 0, "fuzz SFZ: damaged text", "%.0f cases, %.0f out of bounds", cases, bad);
}

static void fuzzSf2() {
    const std::vector<uint8_t> base = writeSf2(testBank());
    std::mt19937 rng(54321);
    int bad = 0, opened = 0, cases = 0, zonesSeen = 0;
    for (; cases < 30000; ++cases) {
        std::vector<uint8_t> m = mutate(base, rng);
        if (cases % 3 == 0 && m.size() > 12) { // keep the RIFF header now and then, so that the damage reaches the tables
            std::memcpy(m.data(), base.data(), 12);
        }
        Sf2File f;
        std::string error;
        if (!openSf2(m.data(), m.size(), f, error)) continue;
        ++opened;
        const int count = sf2SampleCount(f);
        const std::vector<Sf2Preset> presets = sf2Presets(f);
        for (size_t p = 0; p < presets.size() && p < 8; ++p) {
            ImportedInstrument in;
            sf2Instrument(f, static_cast<int>(p), in, error);
            for (const ImportedZone& z : in.zones) {
                ++zonesSeen;
                if (!zoneInBounds(z) || z.sf2Sample < 0 || z.sf2Sample >= count || z.sf2Link < -1 || z.sf2Link >= count) {
                    ++bad;
                    continue;
                }
                const Sf2File::Sample& s = f.samples[static_cast<size_t>(z.sf2Sample)];
                const uint64_t length = s.end - s.start;
                if (z.start >= length || z.end > length || (z.hasLoopPoints && (z.loopStart >= z.loopEnd || z.loopEnd > length))) ++bad;
            }
        }
        for (int i = -1; i <= count && i < 8; ++i) {
            AudioData a;
            if (!sf2Sample(f, m.data(), m.size(), i, a, error)) continue;
            if (a.channels != 1 || a.frames() == 0) ++bad;
            for (const auto& loop : a.loops) bad += loop.start < loop.end && loop.end <= a.frames() ? 0 : 1;
            for (float v : a.samples[0]) bad += v >= -1.0f && v < 1.0f ? 0 : 1;
        }
    }
    check(bad == 0 && opened > 2000, "fuzz SF2: damaged banks", "%.0f cases, %.0f opened, %.0f zones, %.0f out of bounds", cases, opened, zonesSeen, bad);
}

static void fuzzScala() {
    std::vector<std::string> just = {"16/15", "9/8", "6/5", "5/4", "4/3", "45/32", "3/2", "8/5", "5/3", "9/5", "15/8", "2/1"};
    const std::string scl = scale("just", just), kbm = "12\n48\n84\n60\n69\n440\n12\n0\nx\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n";
    std::mt19937 rng(777);
    // Besides damage of any kind, characters that mean something in these files, so that more
    // of the damaged files are still read and reach the mapping.
    auto gentle = [&rng](const std::string& in) {
        static const char kinds[] = "0123456789./x-! \n";
        std::string out = in;
        const int n = 1 + static_cast<int>(rng() % 3);
        for (int i = 0; i < n; ++i) out[rng() % out.size()] = kinds[rng() % (sizeof(kinds) - 1)];
        return std::vector<uint8_t>(out.begin(), out.end());
    };
    int bad = 0, parsed = 0, cases = 0;
    for (; cases < 20000; ++cases) {
        const std::vector<uint8_t> a = cases % 4 < 2 ? gentle(scl) : mutate(std::vector<uint8_t>(scl.begin(), scl.end()), rng);
        const std::vector<uint8_t> b = cases % 4 == 1 || cases % 4 == 3 ? gentle(kbm) : mutate(std::vector<uint8_t>(kbm.begin(), kbm.end()), rng);
        Tuning t;
        std::string error;
        if (!parseScala(std::string(a.begin(), a.end()), std::string(b.begin(), b.end()), t, error)) continue;
        ++parsed;
        for (double c : t.cents) bad += hnl::isFinite(c) && std::fabs(c) <= 1e6 ? 0 : 1;
    }
    check(bad == 0, "fuzz Scala: damaged scales and maps", "%.0f cases, %.0f read, %.0f not finite", cases, parsed, bad);
}

int main() {
    const auto started = std::chrono::steady_clock::now();
    const fs::path dir = tempDir();
    testSfzInheritance(dir);
    testSfzOpcodes(dir);
    testSfzOffsetsAndWarnings(dir);
    testSfzInclude(dir);
    testSf2(dir);
    testScala();
    fuzzSfz(dir);
    fuzzSf2();
    fuzzScala();
    std::error_code ec;
    fs::remove_all(dir, ec);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    check(seconds < 60.0, "all of it in under a minute", "%.2f s", seconds);
    std::printf("%s\n", failures ? "FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
