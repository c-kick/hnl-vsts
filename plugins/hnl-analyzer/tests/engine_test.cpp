// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// The engine without DPF: when a capture begins and ends, what is kept and what is not, the
// running meter, files read from disk, the analysis as a song stores it, and the references.
// The test plays the part of the host: it hands the engine blocks with a transport that
// starts, stops and jumps.  engine_test <a folder it may fill and empty>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "../dsp/Engine.hpp"
#include "../../../tools/factory-sounds/FlacEncode.hpp"
#include "Demo.hpp"

using namespace hnl::analyzer;
namespace fs = std::filesystem;

static int failures = 0;
static void check(bool ok, const char* name, const char* fmt = "", ...) {
    char text[200];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    std::printf("%s  %-66s %s\n", ok ? "PASS" : "FAIL", name, text);
    if (!ok) ++failures;
}

static const double kFs = 44100.0;
static std::vector<float> songL, songR; // 30 seconds

/** A host: plays stretches of the song into an engine, with its transport. */
struct Host {
    Engine engine;
    uint64_t seen = 0;
    std::unique_ptr<Engine::Shown> shown{new Engine::Shown()};
    uint32_t block = 512;

    Host() { engine.prepare(kFs); }
    /** `seconds` of the song from `from`, the transport at that place, playing or standing still. */
    void run(double from, double seconds, bool playing) {
        const size_t first = static_cast<size_t>(from * kFs), n = static_cast<size_t>(seconds * kFs);
        std::vector<float> silence(block, 0.0f);
        for (size_t done = 0; done < n; done += block) {
            const uint32_t frames = static_cast<uint32_t>(std::min<size_t>(block, n - done));
            size_t at = first + done;
            if (at + frames > songL.size()) at %= songL.size() - block; // past the end of the song: any part of it will do
            if (playing) engine.process(songL.data() + at, songR.data() + at, frames, true, first + done, true);
            else engine.process(silence.data(), silence.data(), frames, false, first, true);
        }
    }
    const Analysis& track() {
        engine.fetch(seen, *shown);
        return shown->track;
    }
    const Analysis& previous() {
        engine.fetch(seen, *shown);
        return shown->previous;
    }
};

/** What the meter alone makes of a stretch of the song. */
static std::unique_ptr<Result> direct(double from, double seconds) {
    std::unique_ptr<Meter> m(new Meter());
    m->prepare(kFs);
    const size_t first = static_cast<size_t>(from * kFs), n = static_cast<size_t>(seconds * kFs);
    m->process(songL.data() + first, songR.data() + first, static_cast<uint32_t>(n));
    m->flush();
    std::unique_ptr<Result> r(new Result());
    m->snapshot(*r);
    return r;
}
static bool sameFigures(const Result& a, const Result& b) {
    return a.frames == b.frames && a.integrated == b.integrated && a.range == b.range && a.ch[0].truePeak == b.ch[0].truePeak && a.ch[1].truePeak == b.ch[1].truePeak &&
           a.ch[0].sumSquares == b.ch[0].sumSquares && a.rms[2].maxMs[1] == b.rms[2].maxMs[1] && a.spectrum[10] == b.spectrum[10] && a.momentaryMax == b.momentaryMax &&
           a.waveCount == b.waveCount && a.loudCount == b.loudCount;
}

static void session() {
    Host h;
    h.run(0, 0.5, false);
    check(h.track().empty() && h.track().capture.status == kEmpty, "before the transport starts there is nothing", "");
    h.run(0, 4, true);
    check(h.track().capture.status == kRunning && h.track().capture.automatic && std::fabs(h.track().r.seconds() - 4.0) < 0.06 && hasLoudness(h.track().r),
          "the transport starts: a capture runs, and its figures are there while it does", "%.2f s, I %.2f", h.track().r.seconds(), h.track().r.integrated);
    h.run(4, 6, true);
    h.run(10, 0.5, false);
    const std::unique_ptr<Result> whole = direct(0, 10);
    check(h.track().capture.status == kComplete && h.track().capture.ended == kTransportStopped && h.track().r.frames == 441000, "the transport stops: the capture is complete, and stays",
          "%llu frames", static_cast<unsigned long long>(h.track().r.frames));
    check(sameFigures(h.track().r, *whole), "its figures are the ones the meter gives for that stretch, to the last digit", "I %.4f against %.4f", h.track().r.integrated, whole->integrated);
    check(h.engine.lufsI.load() == static_cast<float>(whole->integrated) && h.engine.lufsM.load() == -100.0f && std::fabs(h.engine.plrNow.load() - plr(*whole)) < 1e-4,
          "the host can read them as parameters: the result, and no momentary loudness once it has ended", "I %.2f, PLR %.2f", h.engine.lufsI.load(), h.engine.plrNow.load());
    h.run(10, 2, false);
    check(h.track().capture.status == kComplete && h.track().r.frames == 441000, "standing still changes nothing", "");

    // A tap on play does not replace it.
    h.run(0, 1, true);
    check(h.track().capture.status == kRunning && h.track().r.seconds() < 1.1, "playing again: a new capture runs", "");
    h.run(1, 0.2, false);
    check(h.track().capture.status == kComplete && h.track().r.frames == 441000 && h.previous().empty(), "one of a second is not kept: the measured track is still there", "");

    // A second whole pass replaces it, and the first is the one before.
    h.run(5, 8, true);
    h.run(13, 0.2, false);
    check(h.track().r.frames == 8 * 44100 && h.track().capture.startFrame == 5 * 44100 && h.track().capture.positionKnown && sameFigures(h.track().r, *direct(5, 8)),
          "a second pass replaces it, and knows where in the song it began", "from %.1f s", h.track().capture.startFrame / kFs);
    check(h.previous().r.frames == 441000 && sameFigures(h.previous().r, *whole) && h.previous().capture.id != h.track().capture.id, "and the first is kept as the capture before it", "");

    // A jump while capturing: what was measured is cut short, and a new capture begins at the new place.
    h.run(0, 5, true);
    h.run(20, 4, true); // the transport is now at 20 s
    check(h.track().capture.status == kRunning && h.track().capture.startFrame == 20 * 44100 && std::fabs(h.track().r.seconds() - 4.0) < 0.06, "a jump of the transport begins a new capture there",
          "from %.1f s, %.2f s long", h.track().capture.startFrame / kFs, h.track().r.seconds());
    check(h.previous().r.frames == 8 * 44100, "(the stretch before the jump is the kept one now, the pass before it the one before)", "");
    h.run(24, 0.2, false);
    check(h.track().capture.status == kComplete && h.track().r.frames == 4 * 44100 && h.previous().capture.status == kPartial && h.previous().capture.ended == kSeek && h.previous().r.frames == 5 * 44100,
          "and the stretch before the jump is kept as incomplete, with the reason", "%s", describe(h.previous()).c_str());

    // By hand.
    h.engine.setAutomatic(false);
    h.run(0, 4, true);
    h.run(4, 0.2, false);
    check(h.track().r.frames == 4 * 44100 && h.track().capture.status == kComplete, "not following the transport: playing starts nothing", "");
    h.engine.start();
    h.run(0, 3, false); // silence, transport standing
    h.run(0, 2, true);
    h.run(10, 2, true); // a jump
    check(h.track().capture.status == kRunning && !h.track().capture.automatic && std::fabs(h.track().r.seconds() - 7.0) < 0.06 && h.track().capture.seeks == 1,
          "Start begins a capture that ignores the transport, and counts its jumps", "%.2f s, %u jump", h.track().r.seconds(), h.track().capture.seeks);
    h.engine.stop();
    h.run(12, 0.5, true);
    check(h.track().capture.status == kComplete && h.track().capture.ended == kManualStop && h.track().r.frames == 7 * 44100, "Stop ends it", "%s", describe(h.track()).c_str());
    h.engine.start();
    h.run(0, 0.5, true);
    h.engine.stop();
    h.run(0, 0.1, true);
    check(h.track().r.frames == static_cast<uint64_t>(0.5 * 44100) && h.track().capture.ended == kManualStop, "a capture by hand is kept however short", "%.2f s", h.track().r.seconds());

    // Stop by hand while the transport plays, following it: nothing starts again until the transport has stood still.
    h.engine.setAutomatic(true);
    h.run(0, 0.2, false);
    h.run(0, 4, true);
    h.engine.stop();
    h.run(4, 2, true);
    check(h.track().capture.status == kComplete && h.track().capture.ended == kManualStop && h.track().r.frames == 4 * 44100, "Stop during a capture the transport began ends it, and it does not begin again at once",
          "");
    h.run(6, 0.2, false);
    h.run(0, 3.5, true);
    check(h.track().capture.status == kRunning, "but the next start of the transport does", "");

    // Reset.
    h.engine.reset();
    check(h.track().empty() && !h.previous().empty(), "Reset forgets what is shown, at once, and keeps the capture before it", "");
    h.run(3.5, 1, true);
    check(h.track().empty() || h.track().capture.status == kRunning, "what was running does not come back", "");
    h.run(4.5, 0.2, false);
    check(h.track().empty(), "nor does it when the transport stops", "");
}

static void interruptions() {
    // The host stops running the plugin in the middle of a capture, and never says so.
    Host h;
    h.run(0, 4, true);
    check(h.track().capture.status == kRunning, "a capture runs", "");
    std::this_thread::sleep_for(std::chrono::milliseconds(1700));
    check(h.track().capture.status == kComplete && h.track().capture.ended == kHostStopped && h.track().r.frames >= 4 * 44100 - 4410,
          "the host stops running the plugin: after a moment the capture counts as ended", "%s", describe(h.track()).c_str());
    const uint32_t id = h.track().capture.id;
    h.run(0, 3.5, true);
    check(h.track().capture.status == kRunning && h.track().capture.id != id && std::fabs(h.track().r.seconds() - 3.5) < 0.06, "when it runs again that is a new capture, not more of the old one",
          "%.2f s", h.track().r.seconds());
    check(h.previous().empty(), "(the ended one was the same capture, not one before it)", "");
    h.run(3.5, 0.2, false);
    check(h.track().r.frames == static_cast<uint64_t>(3.5 * 44100) && h.previous().capture.id == id && h.previous().r.frames == 4 * 44100, "and both are kept in turn", "");

    Host d;
    d.run(0, 5, true);
    d.engine.deactivate();
    check(d.track().capture.status == kComplete && d.track().capture.ended == kHostStopped && d.track().r.frames == 5 * 44100 && sameFigures(d.track().r, *direct(0, 5)),
          "the host switches the plugin off: the capture ends there, whole", "");
    Host p;
    p.run(0, 5, true);
    p.engine.prepare(48000.0);
    check(p.track().capture.status == kPartial && p.track().capture.ended == kSwitched && p.track().r.frames == 5 * 44100 && p.track().r.sampleRate == kFs,
          "the sample rate changes: what was measured is kept as incomplete", "%s", describe(p.track()).c_str());
    p.run(0, 4, true);
    check(p.track().capture.status == kRunning && p.track().r.sampleRate == 48000.0, "and the next capture is at the new rate", "");

    // Blocks of any length, and a host whose position runs a little off.
    Host odd;
    odd.block = 37;
    odd.run(0, 6, true);
    odd.run(6, 0.1, false);
    check(sameFigures(odd.track().r, *direct(0, 6)), "blocks of 37 frames give the same capture", "");
    Host drift;
    std::vector<float> l(441, 0.1f);
    for (int i = 0; i < 500; ++i) drift.engine.process(l.data(), l.data(), 441, true, static_cast<uint64_t>(i * 441 + (i % 7) * 50), true); // up to 300 frames off
    check(drift.track().capture.status == kRunning && drift.track().r.frames > 500 * 441 - 4410, "a position that wanders by a few milliseconds is not a jump", "status %d, %llu frames, id %u",
          drift.track().capture.status, static_cast<unsigned long long>(drift.track().r.frames), drift.track().capture.id);
}

static void live() {
    Host h;
    h.engine.setMode(kContinuousMode);
    h.run(0, 2, false);
    check(h.track().capture.status == kRunning && h.track().capture.mode == kContinuousMode && std::fabs(h.track().r.seconds() - 2.0) < 0.06, "live: the meter runs without the transport", "%.2f s",
          h.track().r.seconds());
    h.run(0, 20, true);
    h.run(20, 1, false);
    check(h.track().capture.status == kRunning && std::fabs(h.track().r.seconds() - 23.0) < 0.06 && h.track().r.waveCount >= 228 && h.track().r.waveCount <= 230 && h.track().r.loudCount == h.track().r.waveCount && std::fabs(h.track().r.waveStep - 0.1) < 1e-12,
          "and goes on through starts and stops; its outline and its loudness cover the same stretch", "%.1f s", h.track().r.seconds());
    check(h.engine.lufsM.load() < -60.0f && h.engine.lufsI.load() > -30.0f, "the host reads the meter of the moment", "M %.1f, I %.1f", h.engine.lufsM.load(), h.engine.lufsI.load());
    h.engine.reset();
    h.run(0, 1, true);
    check(h.track().capture.status == kRunning && std::fabs(h.track().r.seconds() - 1.0) < 0.06, "Reset starts it again", "%.2f s", h.track().r.seconds());

    // Back to session: the running meter is not a result.
    Analysis kept = demo::measured("Kept", 1.0, 8.0);
    h.engine.setCompleted(kept);
    check(h.track().capture.status == kRunning, "(live shows the meter, not what is kept)", "");
    h.engine.setMode(kTrackMode);
    h.run(0, 0.2, false);
    check(h.track().name == "Kept" && h.track().capture.status == kComplete && h.previous().empty(), "back in session mode the kept analysis is shown; the meter left nothing behind", "");
    h.run(0, 4, true);
    h.engine.setMode(kContinuousMode);
    h.run(4, 0.2, true);
    h.engine.setMode(kTrackMode);
    h.run(4.2, 0.2, false);
    check(h.track().capture.status == kPartial && h.track().capture.ended == kSwitched && h.track().r.frames == 4 * 44100, "changing the mode during a capture ends it as incomplete", "");
    h.engine.setMode(kFile);
    check(h.engine.mode() == kTrackMode, "a file is not a mode of the meter: session takes its place", "");
}

static void state() {
    Host h;
    check(h.engine.saveState().empty(), "nothing measured: nothing to store", "");
    h.run(0, 6, true);
    h.run(6, 0.2, false);
    h.engine.setName("D\xC3\xA9j\xC3\xA0 des souvenirs");
    check(h.track().name == "D\xC3\xA9j\xC3\xA0 des souvenirs", "the track is given a name", "");
    const std::string stored = h.engine.saveState();
    Host other;
    check(other.engine.loadState(stored), "the song stores the analysis, and another instance takes it back", "%zu bytes", stored.size());
    const Analysis& back = other.track();
    check(back.name == h.track().name && back.capture.status == kComplete && back.r.frames == h.track().r.frames && std::fabs(back.r.integrated - h.track().r.integrated) < 1e-12 &&
              back.r.waveCount == h.track().r.waveCount && std::fabs(truePeakDb(back.r) - truePeakDb(h.track().r)) < 1e-12,
          "with its name and its figures", "I %.2f", back.r.integrated);
    check(!other.engine.loadState("") && !other.engine.loadState("garbage") && !other.engine.loadState("{\"format\":\"hnl-analysis\"}") && other.track().name == back.name,
          "what is not an analysis leaves the one there is", "");
    other.run(0, 4, true);
    other.run(4, 0.2, false);
    check(other.track().r.frames == 4 * 44100 && other.previous().r.frames == h.track().r.frames && other.track().name == "D\xC3\xA9j\xC3\xA0 des souvenirs",
          "a new capture replaces the one from the song, which becomes the one before; the name stays", "");

    // The window and the host read while the audio thread writes.
    Host busy;
    std::atomic<bool> done{false};
    std::atomic<int> reads{0};
    bool sane = true;
    std::thread reader([&] {
        uint64_t seen = 0;
        std::unique_ptr<Engine::Shown> shown(new Engine::Shown());
        uint64_t last = 0;
        while (!done.load()) {
            if (busy.engine.fetch(seen, *shown)) {
                const Result& r = shown->track.r;
                if (shown->track.capture.status == kRunning) {
                    sane = sane && r.frames >= last && r.waveCount <= kSeries && r.loudCount <= kSeries && r.ch[0].samplePeak <= 1.0;
                    last = r.frames;
                }
                ++reads;
            }
            busy.engine.saveState();
        }
    });
    busy.run(0, 25, true);
    done.store(true);
    reader.join();
    busy.run(25, 0.2, false);
    check(sane && reads.load() > 3 && sameFigures(busy.track().r, *direct(0, 25)), "another thread reads all the while: every copy is whole, and the capture is not disturbed", "%d copies",
          reads.load());
}

// ------------------------------------------------------------------ files

static void put(std::vector<uint8_t>& out, uint32_t v, int bytes, bool big) {
    for (int i = 0; i < bytes; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * (big ? bytes - 1 - i : i))));
}
static void write(const fs::path& file, const std::vector<uint8_t>& bytes) {
    std::ofstream(file, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}
/** The first `seconds` of the song as whole numbers of `bits` bits, and as the floats those stand for. */
static void quantise(double seconds, int bits, std::vector<int32_t>& il, std::vector<int32_t>& ir, std::vector<float>& l, std::vector<float>& r) {
    const size_t n = static_cast<size_t>(seconds * kFs);
    const double scale = static_cast<double>(1 << (bits - 1));
    il.resize(n); ir.resize(n); l.resize(n); r.resize(n);
    for (size_t i = 0; i < n; ++i) {
        il[i] = static_cast<int32_t>(std::lround(songL[i] * (scale - 1)));
        ir[i] = static_cast<int32_t>(std::lround(songR[i] * (scale - 1)));
        l[i] = static_cast<float>(il[i] / scale);
        r[i] = static_cast<float>(ir[i] / scale);
    }
}
static std::vector<uint8_t> wav(const std::vector<int32_t>& l, const std::vector<int32_t>& r, int bits, int channels) {
    std::vector<uint8_t> out;
    const uint32_t bytes = static_cast<uint32_t>(bits / 8), data = static_cast<uint32_t>(l.size()) * bytes * static_cast<uint32_t>(channels);
    out.insert(out.end(), {'R', 'I', 'F', 'F'});
    put(out, 36 + data, 4, false);
    out.insert(out.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    put(out, 16, 4, false);
    put(out, 1, 2, false);
    put(out, static_cast<uint32_t>(channels), 2, false);
    put(out, 44100, 4, false);
    put(out, 44100 * bytes * static_cast<uint32_t>(channels), 4, false);
    put(out, bytes * static_cast<uint32_t>(channels), 2, false);
    put(out, static_cast<uint32_t>(bits), 2, false);
    out.insert(out.end(), {'d', 'a', 't', 'a'});
    put(out, data, 4, false);
    for (size_t i = 0; i < l.size(); ++i) {
        put(out, static_cast<uint32_t>(l[i]), static_cast<int>(bytes), false);
        if (channels > 1) put(out, static_cast<uint32_t>(r[i]), static_cast<int>(bytes), false);
    }
    return out;
}
static std::vector<uint8_t> aiff(const std::vector<int32_t>& l, const std::vector<int32_t>& r) { // 16 bit, stereo
    std::vector<uint8_t> out;
    const uint32_t data = static_cast<uint32_t>(l.size()) * 4;
    out.insert(out.end(), {'F', 'O', 'R', 'M'});
    put(out, 4 + 26 + 16 + data, 4, true);
    out.insert(out.end(), {'A', 'I', 'F', 'F', 'C', 'O', 'M', 'M'});
    put(out, 18, 4, true);
    put(out, 2, 2, true);
    put(out, static_cast<uint32_t>(l.size()), 4, true);
    put(out, 16, 2, true);
    out.insert(out.end(), {0x40, 0x0E, 0xAC, 0x44, 0, 0, 0, 0, 0, 0}); // 44100 as an 80-bit number
    out.insert(out.end(), {'S', 'S', 'N', 'D'});
    put(out, 8 + data, 4, true);
    put(out, 0, 4, true);
    put(out, 0, 4, true);
    for (size_t i = 0; i < l.size(); ++i) {
        put(out, static_cast<uint32_t>(l[i]), 2, true);
        put(out, static_cast<uint32_t>(r[i]), 2, true);
    }
    return out;
}

static void files(const fs::path& dir) {
    std::vector<int32_t> il, ir;
    std::vector<float> l, r;
    quantise(12.0, 16, il, ir, l, r);
    std::unique_ptr<Meter> m(new Meter());
    m->prepare(kFs);
    m->process(l.data(), r.data(), static_cast<uint32_t>(l.size()));
    m->flush();
    std::unique_ptr<Result> expect(new Result());
    m->snapshot(*expect);

    write(dir / "Master 3.wav", wav(il, ir, 16, 2));
    write(dir / "master.aiff", aiff(il, ir));
    const int32_t* channels[2] = {il.data(), ir.data()};
    write(dir / "master.flac", hnl::flacenc::encode(channels, 2, il.size(), 16, 44100));
    Analysis a;
    std::string error;
    check(Engine::measureFile(dir / "Master 3.wav", a, error) && sameFigures(a.r, *expect) && a.capture.mode == kFile && a.capture.status == kComplete && a.capture.ended == kEndOfFile,
          "a WAV file gives the figures the meter gives for the same sound, to the last digit", "I %.4f, %s", a.r.integrated, error.c_str());
    check(a.name == "Master 3" && a.source == "WAV, 16 bit" && describe(a).find("WAV, 16 bit, 44.1 kHz stereo, 0:12.0") == 0, "with its name, and what kind of file it is", "%s", describe(a).c_str());
    Analysis b;
    check(Engine::measureFile(dir / "master.aiff", b, error) && sameFigures(b.r, *expect) && b.source == "AIFF, 16 bit", "an AIFF file of the same sound gives the same", "%s", error.c_str());
    Analysis c;
    check(Engine::measureFile(dir / "master.flac", c, error) && sameFigures(c.r, *expect) && c.source == "FLAC, 16 bit", "and so does a FLAC file", "%s", error.c_str());

    // The same sound captured in a session reads the same as its file.
    Host h;
    std::swap(songL, l);
    std::swap(songR, r);
    h.run(0, 12, true);
    h.run(12, 0.1, false);
    std::swap(songL, l);
    std::swap(songR, r);
    check(sameFigures(h.track().r, a.r), "captured in a session, the same sound reads the same as its file", "I %.4f and %.4f", h.track().r.integrated, a.r.integrated);

    // 24 bit, and mono.
    quantise(6.0, 24, il, ir, l, r);
    write(dir / "deep.wav", wav(il, ir, 24, 2));
    write(dir / "mono.wav", wav(il, ir, 24, 1));
    Analysis deep, mono;
    m->prepare(kFs, 1);
    m->process(l.data(), nullptr, static_cast<uint32_t>(l.size()));
    m->flush();
    m->snapshot(*expect);
    check(Engine::measureFile(dir / "deep.wav", deep, error) && deep.source == "WAV, 24 bit" && deep.r.frames == 6 * 44100 && std::fabs(deep.r.integrated - direct(0, 6)->integrated) < 0.001,
          "24 bit: the figures of the sound itself, within a thousandth of a LU", "I %.4f", deep.r.integrated);
    check(Engine::measureFile(dir / "mono.wav", mono, error) && mono.r.channels == 1 && sameFigures(mono.r, *expect) && describe(mono).find("mono") != std::string::npos,
          "a mono file is measured as one channel", "I %.2f against %.2f in stereo", mono.r.integrated, deep.r.integrated);

    // What cannot be read, and what can only partly.
    std::vector<uint8_t> cut = wav(il, ir, 24, 2);
    cut.resize(cut.size() / 2);
    write(dir / "cut.wav", cut);
    Analysis half;
    check(Engine::measureFile(dir / "cut.wav", half, error) && half.capture.status == kPartial && half.capture.ended == kFileCut && half.r.frames > 0 && half.r.frames < 6 * 44100 && !half.note.empty(),
          "a file that ends before its header says is measured as far as it goes, and marked incomplete", "%s / %s", describe(half).c_str(), error.c_str());
    write(dir / "song.mp3", {0xFF, 0xFB, 0x90, 0x00, 0, 0, 0, 0});
    Analysis none;
    check(!Engine::measureFile(dir / "song.mp3", none, error) && error.find("WAV, AIFF and FLAC") != std::string::npos, "an MP3 file is refused, and told why", "%s", error.c_str());
    check(!Engine::measureFile(dir / "not there.wav", none, error) && !error.empty(), "a file that is not there", "%s", error.c_str());
    write(dir / "text.wav", {'h', 'e', 'l', 'l', 'o'});
    check(!Engine::measureFile(dir / "text.wav", none, error) && !error.empty(), "a file that is no sound", "%s", error.c_str());
    check(!Engine::measureFile(dir, none, error), "a folder", "%s", error.c_str());
    std::vector<uint8_t> flacCut = hnl::flacenc::encode(channels, 2, il.size(), 24, 44100);
    flacCut.resize(flacCut.size() * 2 / 3);
    write(dir / "cut.flac", flacCut);
    Analysis flacHalf;
    const bool readCut = Engine::measureFile(dir / "cut.flac", flacHalf, error);
    check(!readCut || (flacHalf.r.frames > 0 && flacHalf.r.frames < 6 * 44100), "a FLAC file cut off in the middle: what it holds, or a refusal, never a wrong whole", "%s",
          readCut ? describe(flacHalf).c_str() : error.c_str());
    uint32_t seed = 99;
    bool survived = true;
    for (int round = 0; round < 60; ++round) { // damaged headers and bodies
        std::vector<uint8_t> bad = round % 2 ? wav(il, ir, 24, 2) : hnl::flacenc::encode(channels, 2, 44100, 24, 44100);
        bad.resize(std::min<size_t>(bad.size(), 200000));
        for (int k = 0; k < 1 + round % 5; ++k) {
            seed = seed * 1664525u + 1013904223u;
            bad[(seed >> 8) % (round < 30 ? 64 : bad.size())] = static_cast<uint8_t>(seed >> 24);
        }
        write(dir / "bad.bin", bad);
        Analysis d;
        if (Engine::measureFile(dir / "bad.bin", d, error)) survived = survived && d.r.frames > 0 && d.r.waveCount <= kSeries && d.r.ch[0].samplePeak <= 8.0 && d.r.integrated <= 30.0;
    }
    check(survived, "60 damaged files: each is refused or measured into figures that stay within bounds", "");

    // On a thread of its own, from the window.
    Host w;
    w.engine.setCompleted(demo::measured("Before", 1.0, 5.0));
    const auto wait = [&w] {
        for (int i = 0; i < 400 && w.engine.fileBusy(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    };
    std::unique_ptr<Analysis> got(new Analysis());
    std::string why;
    check(w.engine.takeFile(*got, why) == 0, "no file was asked for: no news", "");
    w.engine.measureReference(dir / "Master 3.wav");
    wait();
    check(!w.engine.fileBusy() && w.engine.takeFile(*got, why) == 1 && got->name == "Master 3" && got->capture.mode == kFile && sameFigures(got->r, a.r),
          "a file for a reference: read on its own thread and handed over, with the figures of a session capture of it", "");
    check(w.track().name == "Before" && w.previous().empty(), "the track that is shown is not touched by it", "");
    check(w.engine.takeFile(*got, why) == 0, "it is handed over once", "");
    w.engine.measureReference(dir / "song.mp3");
    wait();
    check(w.engine.takeFile(*got, why) == -1 && !why.empty() && w.track().name == "Before", "a file that cannot be read says why, and changes nothing", "%s", why.c_str());
    w.engine.measureReference(dir / "deep.wav");
    w.engine.cancelFile();
    const int cancelled = w.engine.takeFile(*got, why);
    check(!w.engine.fileBusy() && (cancelled == 0 || (cancelled == 1 && got->name == "deep")), "reading can be cancelled: nothing, or the whole file when it was already read", "%d", cancelled);
    w.engine.measureReference(dir / "deep.wav");
    w.engine.measureReference(dir / "master.flac"); // the first is dropped for the second
    wait();
    check(w.engine.takeFile(*got, why) == 1 && got->name == "master" && w.shown->fileProgress >= 0.0 && w.shown->fileProgress <= 1.0, "a second file takes the place of one still being read", "");
    w.engine.measureReference(dir / "Master 3.wav");
    w.engine.reset();
    wait();
    check(w.engine.takeFile(*got, why) == 1 && got->name == "Master 3" && w.track().empty(), "Reset forgets the track, not the reference that is being read", "");
}

static void references(const fs::path& dir) {
    References refs;
    check(References::folder() == dir / "refs" && refs.count() == 0, "the references have a folder of their own, empty at first", "");
    const View view;
    Analysis a = demo::measured("\xC3\x80 toute allure", 1.0, 6.0);
    const std::string name = refs.save(a, view);
    check(name == "\xC3\x80 toute allure" && refs.count() == 1 && fs::exists(dir / "refs" / References::path(name + ".json")), "an analysis is saved as a reference under its name", "%s", name.c_str());
    Analysis back;
    check(refs.load(0, back) && back.name == name && back.r.frames == a.r.frames && std::fabs(back.r.integrated - a.r.integrated) < 1e-12 && back.has(kHaveSpectrum | kHaveSeries | kHaveWindows),
          "and read back whole: figures, spectrum, histogram and loudness history", "");
    Analysis bad = a;
    bad.name = "  A/B: \"final\"? <mix>.  ";
    const std::string cleaned = refs.save(bad, view);
    check(cleaned == "AB final mix" && refs.count() == 2 && refs.find(cleaned) == 0, "a name is made fit for a file", "%s", cleaned.c_str());
    bad.name = "";
    check(refs.save(bad, view) == "Reference" && References::fileName("con") == "con_" && References::fileName(std::string(200, 'x')).size() == 60, "no name, a name the system keeps to itself, a long one", "");
    // A reference someone typed.
    { std::ofstream(dir / "typed.json") << demo::referenceJson(); }
    Analysis typed;
    check(References::read(dir / "typed.json", typed) && typed.name == "\xC3\x80 toute allure" && std::fabs(typed.r.integrated + 12.47) < 1e-9 && !typed.has(kHaveSpectrum),
          "a reference typed by hand is read from any place", "I %.2f", typed.r.integrated);
    { std::ofstream(dir / "junk.json") << "{\"hello\":1}"; }
    check(!References::read(dir / "junk.json", typed) && !References::read(dir / "none.json", typed), "what is not a reference is refused", "");
    check(refs.remove(refs.find("Reference")) && refs.count() == 2 && !refs.remove(7), "a reference can be deleted", "%d left", refs.count());
    References second;
    check(second.count() == 2 && second.name(1) == name, "another window sees the same references", "");
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("FAIL  no folder to work in\n");
        return 1;
    }
    const fs::path dir = fs::absolute(argv[1]);
    fs::remove_all(dir);
    fs::create_directories(dir);
#ifdef _WIN32
    _putenv_s("HNL_REFERENCE_DIR", (dir / "refs").string().c_str());
#else
    setenv("HNL_REFERENCE_DIR", (dir / "refs").string().c_str(), 1);
#endif
    demo::song(kFs, 30.0, songL, songR);
    session();
    interruptions();
    live();
    state();
    files(dir);
    references(dir);
    fs::remove_all(dir);
    std::printf("%s\n", failures == 0 ? "ALL PASS" : "FAILED");
    return failures == 0 ? 0 : 1;
}
