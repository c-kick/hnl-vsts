// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// HNL Analyzer without its window and without DPF: when a capture begins and ends, how what
// the audio thread measures reaches the other threads, the analysis that is kept, and the
// measuring of a sound file to compare with.
//
// Two modes:
//   track       a capture runs from the moment the host's transport starts until it stops (or
//               between Start and Stop by hand) and is kept afterwards
//   continuous  the meter runs all the time, whatever the transport does; its outline and loudness history show the last 3.4 minutes
//
// What is measured is always the sound that passes through the plugin. A sound file is measured
// only to be a reference: it is read from disk on a thread of its own, as fast as it will go,
// and handed to the window, which keeps it with the other references.
//
// The audio thread only measures and never waits: it hands a copy of the figures to the others
// when the lock happens to be free, about twenty times a second. Everything that is kept (the
// finished analysis, the one before it, the name) lives on the other side of that lock.
#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../../../common/UserPresets.hpp"
#include "../../../common/dsp/Safety.hpp"
#include "../../hnl-sampler/dsp/AudioFile.hpp"
#include "Report.hpp"

namespace hnl {
namespace analyzer {

/** A capture the transport started that is shorter than this is not kept: a tap on the play key does not replace a measured track. */
constexpr double kMinAutoSeconds = 3.0;

class Engine {
public:
    Engine() { shared_.reset(new Shared()); ended_.reset(new Result()); }
    ~Engine() { cancelFile(); }
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    /**
     * The engine of a plugin instance, for its window: a window is handed the address of its
     * plugin, and looks up the engine here. The plugin enters itself with `enter` and leaves with `leave`.
     */
    static Engine* of(const void* owner, Engine* enter = nullptr, bool leave = false) {
        static std::mutex mutex;
        static std::map<const void*, Engine*> known;
        std::lock_guard<std::mutex> lock(mutex);
        if (leave) { known.erase(owner); return nullptr; }
        if (enter != nullptr) { known[owner] = enter; return enter; }
        const auto it = known.find(owner);
        return it == known.end() ? nullptr : it->second;
    }

    // ------------------------------------------------------------ settings

    void setMode(int mode) { mode_.store(mode < 0 || mode >= kMeterModes ? kTrackMode : mode, std::memory_order_relaxed); }
    void setAutomatic(bool on) { automatic_.store(on, std::memory_order_relaxed); }
    void setView(int window, bool sine) {
        window_.store(window < 0 || window >= kRmsWindows ? 2 : window, std::memory_order_relaxed);
        sine_.store(sine, std::memory_order_relaxed);
    }
    int mode() const { return mode_.load(std::memory_order_relaxed); }
    View view() const { return {window_.load(std::memory_order_relaxed), sine_.load(std::memory_order_relaxed)}; }

    // ------------------------------------------------------- the audio side

    /** Allocates. Never while process() runs. A capture that was running ends here. */
    void prepare(double sampleRate) {
        if (capturing_) end(kSwitched, kPartial);
        deliver(true);
        meter_.prepare(sampleRate);
        publishEvery_ = std::max<uint32_t>(1, static_cast<uint32_t>(meter_.sampleRate() / 20.0));
        jump_ = std::max<uint64_t>(2048, static_cast<uint64_t>(meter_.sampleRate() / 20.0));
        armed_ = true;
        lastMode_ = -1;
    }

    /** The host stops running the plugin: a capture ends as if the transport had stopped. */
    void deactivate() {
        if (capturing_ && cap_.mode == kTrackMode) end(kHostStopped, kComplete);
        deliver(true);
        armed_ = true;
    }

    /**
     * One block. `playing` and `frame` are the host's transport; `positionKnown` false when the
     * host gives none. Measures only; the sound itself is not touched.
     */
    void process(const float* l, const float* r, uint32_t n, bool playing, uint64_t frame, bool positionKnown) {
        const int mode = mode_.load(std::memory_order_relaxed);
        const bool automatic = automatic_.load(std::memory_order_relaxed);
        const auto now = std::chrono::steady_clock::now();

        // The host left the plugin alone for a while (it stopped its engine, or finished a bounce): what was running ended there.
        if (capturing_ && cap_.mode == kTrackMode && now - lastRun_ > std::chrono::milliseconds(1000)) {
            end(kHostStopped, kComplete);
            armed_ = true;
        }
        lastRun_ = now;

        if (reset_.exchange(false, std::memory_order_relaxed)) {
            capturing_ = false;
            pendingEnd_ = false;
            cap_ = Capture();
            cap_.mode = mode;
            meter_.reset(mode == kContinuousMode);
            armed_ = true;
            lastMode_ = -1;
            publishNow_ = true;
        }
        if (mode != lastMode_) {
            if (capturing_) end(kSwitched, kPartial);
            lastMode_ = mode;
            armed_ = true;
            if (mode == kContinuousMode) begin(false, frame, positionKnown);
            publishNow_ = true;
        }
        const bool start = start_.exchange(false, std::memory_order_relaxed), stop = stop_.exchange(false, std::memory_order_relaxed);
        if (mode == kTrackMode) {
            if (!playing) armed_ = true;
            if (stop && capturing_) end(kManualStop, kComplete);
            if (start) {
                if (capturing_) end(kManualStop, kComplete);
                begin(false, frame, positionKnown);
            }
            if (capturing_) {
                const uint64_t off = frame > expected_ ? frame - expected_ : expected_ - frame;
                const bool jumped = positionKnown && cap_.positionKnown && playing && off > jump_;
                if (cap_.automatic) {
                    if (!playing) {
                        end(kTransportStopped, kComplete);
                    } else if (jumped) { // a loop or a click in the song: what was measured is not one stretch of it any more
                        end(kSeek, kPartial);
                        begin(true, frame, positionKnown);
                    }
                } else if (jumped) {
                    ++cap_.seeks;
                }
            } else if (automatic && playing && armed_) {
                begin(true, frame, positionKnown);
            }
        }
        expected_ = frame + n;

        if (capturing_) {
            meter_.process(l, r, n);
            sincePublish_ += n;
        }
        deliver(false);
    }

    // What the host may read as output parameters: the meter of the moment.
    std::atomic<float> lufsM{-100.0f}, lufsS{-100.0f}, lufsI{-100.0f}, truePeak{-100.0f}, plrNow{0.0f}, lra{0.0f};

    // ------------------------------------------------- the keys of a window

    void start() { start_.store(true, std::memory_order_relaxed); }
    void stop() { stop_.store(true, std::memory_order_relaxed); }
    /** Forgets the analysis that is shown. The one before it stays. */
    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        completed_ = Analysis();
        completed_.capture.status = kEmpty;
        reset_.store(true, std::memory_order_relaxed);
        { // what the audio thread still hands over of the captures up to now does not count
            std::lock_guard<std::mutex> inner(shared_->mutex);
            ignoreUpTo_ = std::max(std::max(ignoreUpTo_, live_.capture.id), std::max(shared_->liveCap.id, shared_->endCap.id));
        }
        live_ = Analysis();
        ++changes_;
    }
    void setName(const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex_);
        name_ = name.substr(0, 120);
        completed_.name = name_;
        ++changes_;
    }

    // ------------------------------------------------------- what is shown

    struct Shown {
        Analysis track, previous;
        bool fileBusy = false;     // a reference file is being read
        double fileProgress = 0.0; // 0..1
    };

    /** Brings `out` up to date. True when something changed since the last call with this `seen`. */
    bool fetch(uint64_t& seen, Shown& out) {
        std::lock_guard<std::mutex> lock(mutex_);
        sync();
        out.fileBusy = fileBusy_.load(std::memory_order_relaxed);
        const uint64_t total = fileTotal_.load(std::memory_order_relaxed);
        out.fileProgress = total ? std::min(1.0, static_cast<double>(fileDone_.load(std::memory_order_relaxed)) / static_cast<double>(total)) : 0.0;
        if (seen == changes_) return false;
        seen = changes_;
        const bool running = live_.capture.status == kRunning && live_.capture.mode == mode_.load(std::memory_order_relaxed);
        out.track = running ? live_ : completed_;
        if (running) out.track.name = name_;
        out.previous = previous_;
        return true;
    }
    /** The analysis that is kept, as the song stores it. */
    std::string saveState() {
        std::lock_guard<std::mutex> lock(mutex_);
        sync();
        return completed_.empty() ? std::string() : toJson(completed_, view());
    }
    /** Takes back what saveState() gave. Anything else leaves things as they are. */
    bool loadState(const std::string& text) {
        Analysis a;
        if (text.empty() || !fromJson(text, a)) return false;
        setCompleted(a);
        return true;
    }
    /** Puts an analysis in the place of the kept one, as a finished capture does. */
    void setCompleted(const Analysis& a) {
        std::lock_guard<std::mutex> lock(mutex_);
        accept(a);
    }

    // ----------------------------------------------- a file as a reference

    /**
     * Reads and measures a sound file on a thread of its own, to be a reference. What is shown of
     * the track is not touched. A file that was still being read is dropped. The outcome waits
     * for takeFile(), also when no window is open at the time.
     */
    void measureReference(const std::filesystem::path& file) {
        cancelFile();
        std::lock_guard<std::mutex> lock(mutex_);
        fileOutcome_ = 0;
        fileError_.clear();
        fileDone_.store(0, std::memory_order_relaxed);
        fileTotal_.store(0, std::memory_order_relaxed);
        cancel_.store(false, std::memory_order_relaxed);
        fileBusy_.store(true, std::memory_order_relaxed);
        ++changes_;
        worker_ = std::thread([this, file] {
            std::unique_ptr<Analysis> a(new Analysis());
            std::string error;
            const bool ok = readFile(file, *a, error);
            std::lock_guard<std::mutex> inner(mutex_);
            if (ok) {
                fileResult_ = std::move(a);
                fileOutcome_ = 1;
            } else if (!cancel_.load(std::memory_order_relaxed)) {
                fileError_ = error.empty() ? "could not read this file" : error;
                fileOutcome_ = -1;
            }
            fileBusy_.store(false, std::memory_order_relaxed);
            ++changes_;
        });
    }
    /** What the reading of a file came to, given once: 1 with the analysis, -1 with the reason it could not be read, 0 when there is no news. */
    int takeFile(Analysis& out, std::string& error) {
        std::lock_guard<std::mutex> lock(mutex_);
        const int outcome = fileOutcome_;
        fileOutcome_ = 0;
        if (outcome > 0 && fileResult_) out = *fileResult_;
        if (outcome < 0) error = fileError_;
        fileResult_.reset();
        fileError_.clear();
        return outcome;
    }
    void cancelFile() {
        cancel_.store(true, std::memory_order_relaxed);
        if (worker_.joinable()) worker_.join();
    }
    bool fileBusy() const { return fileBusy_.load(std::memory_order_relaxed); }

    /** Measures a whole file on the calling thread; what measureReference does on its own. For the tests and for tools. */
    static bool measureFile(const std::filesystem::path& file, Analysis& out, std::string& error) {
        std::atomic<bool> cancel{false};
        std::atomic<uint64_t> done{0}, total{0};
        return readFile(file, out, error, cancel, done, total);
    }

private:
    /** What passes from the audio thread to the others. */
    struct Shared {
        std::mutex mutex;
        Capture liveCap, endCap;
        Result live, ended;
        std::atomic<uint64_t> liveSerial{0};
        uint64_t endSerial = 0;
    };

    // ------------------------------------------------- audio thread only

    void begin(bool automatic, uint64_t frame, bool positionKnown) {
        meter_.reset(lastMode_ == kContinuousMode);
        cap_ = Capture();
        cap_.id = ++nextId_ & 0x7fffffffu;
        cap_.mode = lastMode_ < 0 ? kTrackMode : lastMode_;
        cap_.status = kRunning;
        cap_.startFrame = frame;
        cap_.positionKnown = positionKnown;
        cap_.automatic = automatic;
        capturing_ = true;
        armed_ = false;
        publishNow_ = true;
    }

    /** The figures of what ends go into a place of their own, so that the next capture can begin in the same block. */
    void end(int reason, int status) {
        if (cap_.mode == kContinuousMode) { // the running meter leaves no result, and must not take the place of one that waits to be fetched
            capturing_ = false;
            cap_.status = status;
            cap_.ended = reason;
            publishNow_ = true;
            return;
        }
        meter_.flush();
        meter_.snapshot(*ended_);
        endedCap_ = cap_;
        endedCap_.status = status;
        endedCap_.ended = reason;
        pendingEnd_ = true;
        capturing_ = false;
        cap_.status = status;
        cap_.ended = reason;
        publishNow_ = true;
    }

    /** Hands over what there is, when the lock is free (or, with `wait`, when it becomes so: never from process()). */
    void deliver(bool wait) {
        if (!pendingEnd_ && !publishNow_ && sincePublish_ < publishEvery_) return;
        std::unique_lock<std::mutex> lock(shared_->mutex, std::defer_lock);
        if (wait) lock.lock();
        else if (!lock.try_lock()) return; // the next block will do
        if (pendingEnd_) {
            shared_->ended = *ended_;
            shared_->endCap = endedCap_;
            ++shared_->endSerial;
            pendingEnd_ = false;
        }
        shared_->liveCap = cap_;
        if (capturing_) meter_.snapshot(shared_->live);
        const Result& r = capturing_ ? shared_->live : shared_->ended;
        if (capturing_ || shared_->endSerial) {
            lufsM.store(static_cast<float>(capturing_ ? r.momentary : kSilence), std::memory_order_relaxed);
            lufsS.store(static_cast<float>(capturing_ ? r.shortTerm : kSilence), std::memory_order_relaxed);
            lufsI.store(static_cast<float>(r.integrated), std::memory_order_relaxed);
            truePeak.store(static_cast<float>(truePeakDb(r)), std::memory_order_relaxed);
            plrNow.store(static_cast<float>(plr(r)), std::memory_order_relaxed);
            lra.store(static_cast<float>(r.range), std::memory_order_relaxed);
        }
        shared_->liveSerial.fetch_add(1, std::memory_order_relaxed);
        publishNow_ = false;
        sincePublish_ = 0;
    }

    Meter meter_;
    Capture cap_, endedCap_;
    std::unique_ptr<Result> ended_;
    bool capturing_ = false, armed_ = true, pendingEnd_ = false, publishNow_ = false;
    int lastMode_ = -1;
    uint32_t nextId_ = 0, sincePublish_ = 0, publishEvery_ = 2400;
    uint64_t expected_ = 0, jump_ = 2400;
    std::chrono::steady_clock::time_point lastRun_ = std::chrono::steady_clock::now();

    // ------------------------------------------------------- between them

    std::unique_ptr<Shared> shared_;
    std::atomic<int> mode_{kTrackMode}, window_{2};
    std::atomic<bool> automatic_{true}, sine_{true};
    std::atomic<bool> start_{false}, stop_{false}, reset_{false};

    // ------------------------------------- the other threads, under mutex_

    /** Takes over what the audio thread handed in, and decides what is kept. */
    void sync() {
        const auto now = std::chrono::steady_clock::now();
        {
            std::lock_guard<std::mutex> lock(shared_->mutex);
            const uint64_t serial = shared_->liveSerial.load(std::memory_order_relaxed);
            if (serial != liveSeen_) {
                liveSeen_ = serial;
                lastNews_ = now;
                if (shared_->liveCap.id > ignoreUpTo_ || shared_->liveCap.status == kEmpty) {
                    live_.capture = shared_->liveCap;
                    if (shared_->liveCap.status == kRunning) live_.r = shared_->live;
                    live_.have = kHaveAll;
                    ++changes_;
                }
            }
            if (shared_->endSerial != endSeen_) {
                endSeen_ = shared_->endSerial;
                if (shared_->endCap.id > ignoreUpTo_) {
                    Analysis a;
                    a.r = shared_->ended;
                    a.capture = shared_->endCap;
                    take(a);
                }
            }
        }
        // The audio thread has gone quiet in the middle of a capture: the host stopped running the
        // plugin. What it had measured is kept as it stands; the audio thread ends it properly if it ever comes back.
        if (live_.capture.status == kRunning && live_.capture.mode == kTrackMode && now - lastNews_ > std::chrono::milliseconds(1500) && live_.capture.id != staleId_) {
            staleId_ = live_.capture.id;
            Analysis a = live_;
            a.capture.status = kComplete;
            a.capture.ended = kHostStopped;
            live_.capture = a.capture;
            take(a);
        }
    }

    /** A capture that ended: kept, unless the transport started it and it lasted only a moment. */
    void take(Analysis& a) {
        if (a.capture.mode == kContinuousMode) return; // the running meter is not a finished analysis
        if (a.capture.automatic && a.r.seconds() < kMinAutoSeconds) {
            ++changes_;
            return;
        }
        accept(a);
    }
    void accept(const Analysis& a) {
        const bool same = a.capture.id != 0 && a.capture.id == completed_.capture.id; // the same capture, ended properly after all
        if (!same && !completed_.empty()) previous_ = completed_;
        const std::string keep = a.name.empty() ? name_ : a.name;
        completed_ = a;
        if (completed_.capture.id == 0) completed_.capture.id = 0x80000000u | ++otherId_;
        completed_.name = keep;
        name_ = keep;
        ++changes_;
    }

    static std::string utf8(const std::filesystem::path& p) {
#if defined(__cpp_char8_t)
        const std::u8string s = p.u8string();
        return std::string(s.begin(), s.end());
#else
        return p.u8string();
#endif
    }

    bool readFile(const std::filesystem::path& file, Analysis& out, std::string& error) { return readFile(file, out, error, cancel_, fileDone_, fileTotal_); }

    /** Reads a file block by block through a meter. False with a reason when nothing of it could be measured. */
    static bool readFile(const std::filesystem::path& file, Analysis& out, std::string& error, std::atomic<bool>& cancel, std::atomic<uint64_t>& done,
                         std::atomic<uint64_t>& total) {
        namespace af = sampler::audiofile;
        std::FILE* f = nullptr;
        try {
            std::string ext = utf8(file.extension());
            for (char& c : ext) c = c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c;
            if (ext == ".mp3" || ext == ".ogg" || ext == ".m4a" || ext == ".aac" || ext == ".opus" || ext == ".wma") {
                error = "this version reads WAV, AIFF and FLAC, not " + ext.substr(1);
                return false;
            }
            sampler::AudioInfo info;
            if (!sampler::probeAudioFile(file, info, nullptr, error)) return false;
            if (info.channels < 1 || !(info.sampleRate >= 1000.0) || info.sampleRate > 768000.0) {
                error = "not a sound this plugin can measure";
                return false;
            }
            const int channels = info.channels > 1 ? 2 : 1;
            std::unique_ptr<Meter> meter(new Meter());
            meter->prepare(info.sampleRate, channels);
            total.store(info.frames, std::memory_order_relaxed);
            int ended = kEndOfFile;
            std::string note;
            const size_t kBlock = 16384;
            std::vector<float> l(kBlock), r(kBlock);
            const auto feed = [&](size_t n) {
                hnl::cleanInput(l.data(), static_cast<uint32_t>(n));
                if (channels > 1) hnl::cleanInput(r.data(), static_cast<uint32_t>(n));
                meter->process(l.data(), channels > 1 ? r.data() : nullptr, static_cast<uint32_t>(n));
                done.store(meter->frames(), std::memory_order_relaxed);
            };
            if (info.raw) {
                f = af::openFile(file, false);
                if (f == nullptr) {
                    error = "could not open the file";
                    return false;
                }
                for (uint64_t at = 0; at < info.frames && !cancel.load(std::memory_order_relaxed);) {
                    const size_t want = static_cast<size_t>(std::min<uint64_t>(kBlock, info.frames - at));
                    const size_t got = sampler::readRawFrames(f, info, at, want, l.data(), channels > 1 ? r.data() : nullptr);
                    if (got) feed(got);
                    at += got;
                    if (got < want) {
                        ended = kFileCut;
                        note = "the file ends before its header says";
                        break;
                    }
                }
                // A RIFF file says how long it is: when the file is shorter, sound is missing at its end.
                uint8_t head[8];
                uint64_t size = 0;
                const af::Source whole{nullptr, f, 0};
                if (ended == kEndOfFile && af::fileSize(f, size) && size >= 8) {
                    af::Source src = whole;
                    src.size = size;
                    if (src.read(0, head, 8) && af::tag(head, "RIFF") && af::le32(head + 4) != 0xFFFFFFFFu && static_cast<uint64_t>(af::le32(head + 4)) + 8 > size + 1) {
                        ended = kFileCut;
                        note = "the file is shorter than its header says";
                    }
                }
                std::fclose(f);
                f = nullptr;
            } else {
                f = af::openFile(file, false);
                uint64_t size = 0;
                if (f == nullptr || !af::fileSize(f, size)) {
                    error = "could not open the file";
                    if (f) std::fclose(f);
                    return false;
                }
                if (size > af::kMaxFlacFile) {
                    std::fclose(f);
                    error = "FLAC file too large";
                    return false;
                }
                std::vector<uint8_t> bytes(static_cast<size_t>(size));
                const af::Source src{nullptr, f, size};
                const bool read = src.read(0, bytes.data(), bytes.size());
                std::fclose(f);
                f = nullptr;
                if (!read) {
                    error = "could not read the file";
                    return false;
                }
                const af::Source mem{bytes.data(), nullptr, size};
                sampler::AudioInfo flacInfo;
                af::FlacStream si;
                uint64_t first = 0;
                if (!af::parseFlac(mem, flacInfo, nullptr, si, first, error)) return false;
                std::string flacError;
                const auto sink = [&](const int64_t* const* ch, int, size_t n, int bits) {
                    const double scale = 1.0 / static_cast<double>(static_cast<int64_t>(1) << (bits - 1));
                    for (size_t at = 0; at < n;) {
                        if (cancel.load(std::memory_order_relaxed)) return false;
                        const size_t part = std::min(kBlock, n - at);
                        for (size_t i = 0; i < part; ++i) l[i] = static_cast<float>(static_cast<double>(ch[0][at + i]) * scale);
                        if (channels > 1)
                            for (size_t i = 0; i < part; ++i) r[i] = static_cast<float>(static_cast<double>(ch[1][at + i]) * scale);
                        feed(part);
                        at += part;
                    }
                    return true;
                };
                if (!af::decodeFlacFrames(bytes.data(), bytes.size(), static_cast<size_t>(first), si, sink, flacError) && !cancel.load(std::memory_order_relaxed)) {
                    ended = kFileCut;
                    note = flacError.empty() ? "the file is damaged" : flacError;
                } else if (si.total != 0 && meter->frames() < si.total && !cancel.load(std::memory_order_relaxed)) {
                    ended = kFileCut;
                    note = "the file is shorter than its header says";
                }
            }
            if (cancel.load(std::memory_order_relaxed)) return false;
            if (meter->frames() == 0) {
                error = note.empty() ? "the file holds no sound" : note;
                return false;
            }
            meter->flush();
            meter->snapshot(out.r);
            out.capture = Capture();
            out.capture.mode = kFile;
            out.capture.status = ended == kEndOfFile ? kComplete : kPartial;
            out.capture.ended = ended;
            out.have = kHaveAll;
            out.name = utf8(file.stem()).substr(0, 120);
            out.note = note;
            char bits[48];
            std::snprintf(bits, sizeof(bits), "%s, %d bit%s", info.format.c_str(), info.bits, info.isFloat ? " float" : "");
            out.source = bits;
            return true;
        } catch (const std::bad_alloc&) {
            error = "not enough memory to read this file";
        } catch (...) {
            error = "could not read this file";
        }
        if (f) std::fclose(f);
        return false;
    }

    std::mutex mutex_;
    Analysis live_, completed_, previous_;
    std::string name_, fileError_;
    std::unique_ptr<Analysis> fileResult_; // a file that was measured, until a window takes it
    int fileOutcome_ = 0;                  // 1: fileResult_ waits, -1: fileError_ does
    uint64_t changes_ = 1, liveSeen_ = 0, endSeen_ = 0;
    uint32_t staleId_ = 0, otherId_ = 0, ignoreUpTo_ = 0;
    std::chrono::steady_clock::time_point lastNews_ = std::chrono::steady_clock::now();

    std::thread worker_;
    std::atomic<bool> cancel_{false}, fileBusy_{false};
    std::atomic<uint64_t> fileDone_{0}, fileTotal_{0};
};

// ------------------------------------------------------------- references

/**
 * The references a user keeps: one JSON file each, beside the presets of the plugins
 * (%APPDATA%\HNL\HNL Analyzer\References\ on Windows). HNL_REFERENCE_DIR, when set, is the
 * folder instead (the tests use it). Used from the window's thread only.
 */
class References {
public:
    static constexpr size_t kMaxFile = 8u << 20;
    static constexpr int kMaxShown = 200;

    References() { rescan(); }

    static std::filesystem::path folder() {
        namespace fs = std::filesystem;
#ifdef _WIN32
        if (const wchar_t* dir = _wgetenv(L"HNL_REFERENCE_DIR"))
            if (dir[0] != L'\0') return fs::path(dir);
#else
        if (const char* dir = std::getenv("HNL_REFERENCE_DIR"))
            if (dir[0] != '\0') return fs::path(dir);
#endif
        const fs::path presets = presetfile::folder("HNL Analyzer");
        return presets.empty() ? fs::path() : presets.parent_path() / "References";
    }

    void rescan() {
        namespace fs = std::filesystem;
        names_.clear();
        const fs::path dir = folder();
        if (dir.empty()) return;
        std::error_code ec;
        fs::directory_iterator it(dir, ec), last;
        for (int seen = 0; !ec && it != last && seen < 2000; it.increment(ec), ++seen) {
            const fs::path& p = it->path();
            if (p.extension() != ".json") continue;
            const std::string name = text(p.stem());
            if (!name.empty() && static_cast<int>(names_.size()) < kMaxShown) names_.push_back(name);
        }
        std::sort(names_.begin(), names_.end());
    }
    int count() const { return static_cast<int>(names_.size()); }
    const std::string& name(int i) const { return names_[static_cast<size_t>(i)]; }
    int find(const std::string& name) const {
        for (int i = 0; i < count(); ++i)
            if (names_[static_cast<size_t>(i)] == name) return i;
        return -1;
    }

    bool load(int i, Analysis& out) const { return i >= 0 && i < count() && read(folder() / path(names_[static_cast<size_t>(i)] + ".json"), out); }
    /** Keeps an analysis under its name (made fit for a file name). Returns the name it got, empty when it could not be written. */
    std::string save(const Analysis& a, const View& view) {
        namespace fs = std::filesystem;
        const fs::path dir = folder();
        std::string name = fileName(a.name);
        if (dir.empty()) return std::string();
        std::error_code ec;
        fs::create_directories(dir, ec);
        Analysis named = a;
        named.name = name;
        if (named.capture.status == kRunning) named.capture.status = kPartial;
        if (!write(dir / path(name + ".json"), toJson(named, view))) return std::string();
        rescan();
        return name;
    }
    bool remove(int i) {
        if (i < 0 || i >= count()) return false;
        std::error_code ec;
        const bool ok = std::filesystem::remove(folder() / path(names_[static_cast<size_t>(i)] + ".json"), ec) && !ec;
        rescan();
        return ok;
    }

    /** A track's name as a file name: without what a file name may not hold, and not longer than 60 characters. */
    static std::string fileName(const std::string& raw) {
        std::string out;
        size_t characters = 0;
        if (presetfile::validUtf8(raw))
            for (unsigned char c : raw) {
                if (!presetfile::allowed(c)) continue;
                if ((c & 0xC0) != 0x80 && ++characters > 60) break;
                out += static_cast<char>(c);
            }
        while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
        size_t first = 0;
        while (first < out.size() && (out[first] == ' ' || out[first] == '.')) ++first;
        out.erase(0, first);
        const std::string stem = out.substr(0, out.find('.'));
        for (const char* device : {"con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "com5", "com6", "com7", "com8", "com9",
                                   "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"})
            if (presetfile::sameName(stem, device)) return out + "_";
        return out.empty() ? std::string("Reference") : out;
    }

    /** Any file with an analysis in it. */
    static bool read(const std::filesystem::path& file, Analysis& out) {
        std::FILE* f = sampler::audiofile::openFile(file, false);
        if (f == nullptr) return false;
        std::string textOfFile;
        char buf[65536];
        size_t got;
        while ((got = std::fread(buf, 1, sizeof(buf), f)) > 0 && textOfFile.size() <= kMaxFile) textOfFile.append(buf, got);
        std::fclose(f);
        if (textOfFile.size() > kMaxFile) return false;
        if (!fromJson(textOfFile, out)) return false;
        if (out.name.empty()) out.name = text(file.stem());
        return true;
    }
    static bool write(const std::filesystem::path& file, const std::string& content) {
        std::FILE* f = sampler::audiofile::openFile(file, true);
        if (f == nullptr) return false;
        const bool ok = std::fwrite(content.data(), 1, content.size(), f) == content.size();
        return std::fclose(f) == 0 && ok;
    }
    static std::filesystem::path path(const std::string& utf8) {
#if defined(__cpp_char8_t)
        return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
#else
        return std::filesystem::u8path(utf8);
#endif
    }
    static std::string text(const std::filesystem::path& p) {
#if defined(__cpp_char8_t)
        const std::u8string s = p.u8string();
        return std::string(s.begin(), s.end());
#else
        return p.u8string();
#endif
    }

private:
    std::vector<std::string> names_;
};

} // namespace analyzer
} // namespace hnl
