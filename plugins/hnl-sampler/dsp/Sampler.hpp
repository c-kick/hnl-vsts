// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// The sampler as the plugin and its UI use it: the document, the engine that
// plays it, and the reading of sounds from disk.
//
// Three kinds of thread meet here. The audio thread only plays (process and
// the note functions); it never waits and never frees memory. The UI and the
// host change the document (edit, setText, the import functions) and read it
// (document). A thread of its own reads sound files, so that neither of the
// others waits for the disk.
//
// A document is never changed once it has been made: a change makes a copy
// with the change in it, and hands that to the audio thread, which takes it
// at the start of its next block. The audio thread says when it no longer
// uses a document, and only then is it freed, by one of the other threads.
// A note that still sounds keeps the sound it plays alive that way, also
// after its zone was deleted.
//
// Sounds are shared by everything in the process: two samplers that use the
// same file hold it in memory once (SampleCache).
#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <thread>

#include "AudioFile.hpp"
#include "Engine.hpp"
#include "Import.hpp"

namespace hnl {
namespace sampler {

namespace fs = std::filesystem;

/** Every sound in memory, by path. A sound stays as long as something uses it. */
class SampleCache {
public:
    static SampleCache& get() {
        static SampleCache cache;
        return cache;
    }

    /** The sound of this path if it is in memory already. */
    std::shared_ptr<const AudioData> find(const std::string& path) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = sounds_.find(path);
        return it == sounds_.end() ? nullptr : it->second.lock();
    }

    /** Reads a sound, or gives the one already in memory. Waits for the disk: never call it on the audio thread. Null when it cannot be read. */
    std::shared_ptr<const AudioData> load(const std::string& path, std::string& error) {
        if (std::shared_ptr<const AudioData> have = find(path)) return have;
        std::shared_ptr<AudioData> data = std::make_shared<AudioData>();
        const size_t mark = path.rfind('#');
        bool ok = false;
        try {
            if (mark != std::string::npos && mark > 4 && lower(path.substr(mark - 4, 4)) == ".sf2") {
                double index = 0.0;
                if (!text::parse(path.substr(mark + 1), index) || index < 0.0 || index > 1e6) { error = "bad sample number"; return nullptr; }
                const std::string file = path.substr(0, mark);
                std::lock_guard<std::mutex> lock(sf2Mutex_); // one SoundFont at a time, and its tables are kept for the next sample from it
                if (file != sf2Path_) {
                    sf2_ = Sf2File();
                    sf2Path_.clear();
                    if (!openSf2File(fs::u8path(file), sf2_, error)) return nullptr;
                    sf2Path_ = file;
                }
                ok = sf2SampleFromFile(sf2_, fs::u8path(file), static_cast<int>(index), *data, error);
            } else {
                ok = readAudioFile(fs::u8path(path), *data, error);
            }
        } catch (...) {
            error = "could not read";
            return nullptr;
        }
        if (!ok || data->frames() == 0 || data->sampleRate <= 0.0) {
            if (error.empty()) error = "no sound in it";
            return nullptr;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        std::weak_ptr<const AudioData>& slot = sounds_[path];
        if (std::shared_ptr<const AudioData> raced = slot.lock()) return raced;
        slot = data;
        for (auto it = sounds_.begin(); it != sounds_.end();) // forget the ones nobody uses any more
            it = it->second.expired() ? sounds_.erase(it) : std::next(it);
        return data;
    }

    static std::string lower(std::string s) {
        for (char& c : s) c = c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c;
        return s;
    }

private:
    std::mutex mutex_, sf2Mutex_;
    std::map<std::string, std::weak_ptr<const AudioData>> sounds_;
    Sf2File sf2_;
    std::string sf2Path_;
};

/** A new zone takes what its file says about itself: root key, loop, key range. */
inline void applyFileFacts(Zone& z, const AudioData& d) {
    if ((z.take & Zone::kTakeRoot) && d.rootKey >= 0) {
        z.v[kZRoot] = std::min(127, d.rootKey);
        z.v[kZTune] = cleanValue(zoneInfo()[kZTune], z.v[kZTune] + d.tuneCents);
    }
    if ((z.take & Zone::kTakeLoop) && !d.loops.empty() && d.loops[0].end > d.loops[0].start && d.loops[0].end <= d.frames()) {
        z.v[kZLoop] = d.loops[0].type == AudioData::kPingPong ? 2 : (d.loops[0].type == AudioData::kBackward ? 3 : 1);
        z.v[kZLoopStart] = static_cast<double>(d.loops[0].start);
        z.v[kZLoopEnd] = static_cast<double>(d.loops[0].end);
    }
    if ((z.take & Zone::kTakeRange) && d.loKey >= 0 && d.hiKey >= d.loKey && d.hiKey <= 127) {
        z.v[kZLoKey] = d.loKey;
        z.v[kZHiKey] = d.hiKey;
    }
    if (z.crossfadeSeconds > 0.0) z.v[kZCrossfade] = cleanValue(zoneInfo()[kZCrossfade], z.crossfadeSeconds * d.sampleRate);
    z.take = 0;
    z.crossfadeSeconds = 0.0;
}

/** Fills a part from an imported instrument (SFZ or SoundFont). `sf2` is the SoundFont's path, empty for SFZ. */
inline void applyImport(Instrument& doc, int partIndex, const ImportedInstrument& in, const std::string& sf2, std::vector<std::string>& notes) {
    Part& part = doc.parts[partIndex];
    const double* keep = part.v;
    Part fresh;
    for (int id : {kPChannel, kPOutput, kPLevel, kPPan, kPMute}) fresh.v[id] = keep[id];
    part = fresh;
    part.name = text::oneLine(in.name, text::kMaxName);
    bool settings = false;
    size_t skipped = 0;
    for (const ImportedZone& iz : in.zones) {
        if (static_cast<int>(part.zones.size()) >= kMaxPartZones) { ++skipped; continue; }
        const std::string path = sf2.empty() ? iz.sample : sf2 + "#" + std::to_string(iz.sf2Sample);
        const int sample = (sf2.empty() || iz.sf2Sample >= 0) ? doc.useSample(path) : 0;
        if (sample == 0) { ++skipped; continue; }
        Zone z;
        z.id = doc.nextId++;
        z.sample = sample;
        const std::vector<Info>& info = zoneInfo();
        const auto set = [&](int id, double value) { z.v[id] = cleanValue(info[id], value); };
        set(kZLoKey, iz.loKey); set(kZHiKey, iz.hiKey);
        set(kZLoVel, std::max(1, iz.loVel)); set(kZHiVel, std::max(1, iz.hiVel));
        if (z.v[kZLoKey] > z.v[kZHiKey] || z.v[kZLoVel] > z.v[kZHiVel]) { ++skipped; continue; }
        set(kZRoot, iz.rootKey);
        if (iz.rootFromFile) z.take |= Zone::kTakeRoot;
        set(kZTune, iz.tuneCents); set(kZKeyTrack, iz.keyTrack); set(kZGain, iz.gainDb); set(kZPan, iz.pan);
        set(kZStart, static_cast<double>(iz.start)); set(kZEnd, static_cast<double>(iz.end));
        if (iz.hasLoopPoints) { set(kZLoopStart, static_cast<double>(iz.loopStart)); set(kZLoopEnd, static_cast<double>(iz.loopEnd)); }
        if (iz.loopMode < 0) z.take |= Zone::kTakeLoop;
        else if (iz.loopMode == 1 || iz.loopMode == 2) {
            set(kZLoop, 1);
            set(kZLoopRelease, iz.loopMode == 1 ? 1 : 0);
            if (!iz.hasLoopPoints) z.take |= Zone::kTakeLoop;
        } else if (iz.loopMode == 3) set(kZOneShot, 1);
        z.crossfadeSeconds = std::max(0.0, iz.loopCrossfade);
        set(kZReverse, iz.reverse ? 1 : 0);
        set(kZTrigger, iz.trigger == 1 ? 1 : 0);
        set(kZGroup, iz.group > 0 && iz.group <= 64 ? iz.group : 0);
        set(kZOffBy, iz.offBy > 0 && iz.offBy <= 64 ? iz.offBy : 0);
        set(kZSeqLength, iz.seqLength); set(kZSeqPosition, iz.seqPosition);
        part.zones.push_back(z);

        if (settings) continue;
        // Envelopes, filter and vibrato belong to the part here, not to each zone: the first zone's are taken.
        settings = true;
        const std::vector<Info>& pi = partInfo();
        const auto setp = [&](int id, double value) { part.v[id] = cleanValue(pi[id], value); };
        const double amp[6] = {iz.ampDelay, iz.ampAttack, iz.ampHold, iz.ampDecay, iz.ampSustain, iz.ampRelease};
        const double fil[6] = {iz.filDelay, iz.filAttack, iz.filHold, iz.filDecay, iz.filSustain, iz.filRelease};
        for (int f = 0; f < 6; ++f) {
            const double scale = f == kEnvSustain ? 1.0 : 1000.0; // seconds to ms
            if (amp[f] >= 0.0) setp(envParam(0, f), amp[f] * scale);
            if (fil[f] >= 0.0) setp(envParam(1, f), fil[f] * scale);
        }
        setp(kPVelocity, iz.ampVelTrack);
        setp(kPBend, std::fabs(iz.bendUp) / 100.0);
        if (iz.cutoff > 0.0) {
            static const int kTypes[8] = {1, 1, 4, 4, 3, 6, 2, 5};
            setp(kPFilterType, kTypes[std::clamp(iz.filterType, 0, 7)]);
            setp(kPCutoff, iz.cutoff);
            setp(kPResonance, iz.resonance * 2.5); // dB of peak to percent, roughly
            setp(kPFilterEnv, iz.filDepth / 84.0); // cents to percent of seven octaves
            setp(kPFilterVel, iz.filVelTrack / 24.0);
            setp(kPKeyTrack, iz.filKeyTrack);
        }
        if (iz.pitchLfoDepth != 0.0 && iz.pitchLfoFreq > 0.0) {
            setp(lfoParam(0, kLfoRate), iz.pitchLfoFreq);
            setp(slotParam(0, kSlotSource), kSrcLfo1);
            setp(slotParam(0, kSlotDest), kDstPitch);
            setp(slotParam(0, kSlotAmount), iz.pitchLfoDepth / 24.0); // cents to percent of two octaves
        }
    }
    notes = in.warnings;
    if (skipped > 0) notes.push_back(std::to_string(skipped) + " zones left out");
    doc.dropUnusedSamples();
}

class Sampler {
public:
    Sampler() {
        std::shared_ptr<Instrument> doc = std::make_shared<Instrument>();
        initDocument(*doc);
        current_ = doc;
        alive_.push_back(doc);
        pending_.store(doc.get());
        worker_ = std::thread([this] { work(); });
    }
    ~Sampler() {
        {
            std::lock_guard<std::mutex> lock(jobMutex_);
            quit_ = true;
        }
        jobReady_.notify_all();
        worker_.join();
    }
    Sampler(const Sampler&) = delete;
    Sampler& operator=(const Sampler&) = delete;

    /**
     * The sampler that belongs to a plugin instance, for the UI, which is handed no more than a
     * pointer to that instance. The plugin enters itself with `owner` and its sampler, and takes
     * itself out again with a null sampler.
     */
    static Sampler* of(const void* owner, Sampler* enter = nullptr, bool leave = false) {
        static std::mutex mutex;
        static std::map<const void*, Sampler*> known;
        std::lock_guard<std::mutex> lock(mutex);
        if (leave) { known.erase(owner); return nullptr; }
        if (enter != nullptr) { known[owner] = enter; return enter; }
        const auto it = known.find(owner);
        return it == known.end() ? nullptr : it->second;
    }

    /** The document a sampler starts with: empty, and silent until a sound is added or an oscillator is turned up. */
    static void initDocument(Instrument& doc) {
        doc = Instrument();
        doc.name = "Init";
    }

    // ------------------------------------------- any thread but the audio one

    std::shared_ptr<const Instrument> document() const {
        std::lock_guard<std::mutex> lock(docMutex_);
        return current_;
    }
    /** Goes up with every change to the document, so that a UI knows when to look again. */
    uint64_t revision() const { return revision_.load(std::memory_order_acquire); }
    /** How many sounds are still to be read. */
    int loading() const { return loading_.load(std::memory_order_acquire); }
    /** What the last import or load had to say, for showing to the user. */
    std::vector<std::string> notes() const {
        std::lock_guard<std::mutex> lock(docMutex_);
        return notes_;
    }

    /** Changes the document: `change` is given a copy to alter, which then becomes the document. */
    void edit(const std::function<void(Instrument&)>& change) {
        std::vector<std::string> wanted;
        {
            std::lock_guard<std::mutex> lock(docMutex_);
            std::shared_ptr<Instrument> next = std::make_shared<Instrument>(*current_);
            change(*next);
            adopt(*next, wanted);
            publish(next);
        }
        request(wanted);
    }

    std::string text() const { return toText(*document()); }

    /** Replaces the document by one read from text. False, and nothing changes, when the text is not a document. */
    bool setText(const std::string& in) {
        std::shared_ptr<Instrument> next = std::make_shared<Instrument>();
        if (!fromText(in, *next)) return false;
        std::vector<std::string> wanted;
        {
            std::lock_guard<std::mutex> lock(docMutex_);
            adopt(*next, wanted);
            notes_.clear();
            publish(next);
        }
        request(wanted);
        return true;
    }

    /** A zone for a sound file over the given keys, at the end of a part. Root key and loop come from the file once it is read. */
    void addZone(int part, const std::string& path, int loKey, int hiKey, int root) {
        edit([&](Instrument& doc) {
            Part& p = doc.parts[std::clamp(part, 0, kParts - 1)];
            const int sample = doc.useSample(path);
            if (sample == 0 || static_cast<int>(p.zones.size()) >= kMaxPartZones) return;
            Zone z;
            z.id = doc.nextId++;
            z.sample = sample;
            z.v[kZLoKey] = std::clamp(std::min(loKey, hiKey), 0, 127);
            z.v[kZHiKey] = std::clamp(std::max(loKey, hiKey), 0, 127);
            z.v[kZRoot] = std::clamp(root, 0, 127);
            z.take = Zone::kTakeRoot | Zone::kTakeLoop;
            p.zones.push_back(z);
        });
    }

    /** Reads an SFZ file or a preset of a SoundFont into a part. False with a reason when it cannot. */
    bool import(int part, const std::string& path, int sf2Preset, std::string& error) {
        ImportedInstrument in;
        const bool sf2 = SampleCache::lower(path.size() >= 4 ? path.substr(path.size() - 4) : "") == ".sf2";
        try {
            if (sf2) {
                Sf2File file;
                if (!openSf2File(fs::u8path(path), file, error) || !sf2Instrument(file, sf2Preset, in, error)) return false;
            } else if (!readSfzFile(fs::u8path(path), in, error)) {
                return false;
            }
        } catch (...) {
            error = "could not read";
            return false;
        }
        if (in.zones.empty()) { error = "no zones in it"; return false; }
        std::vector<std::string> notes;
        edit([&](Instrument& doc) {
            applyImport(doc, std::clamp(part, 0, kParts - 1), in, sf2 ? path : std::string(), notes);
            if (doc.name == "Init" || doc.name == "Empty") doc.name = text::oneLine(in.name, text::kMaxName);
        });
        std::lock_guard<std::mutex> lock(docMutex_);
        notes_ = notes;
        return true;
    }

    /** Plays a sound file as it is, to hear it before using it. An empty path stops it. */
    void preview(const std::string& path) {
        if (path.empty()) { stopPreview_.store(true, std::memory_order_release); return; }
        std::lock_guard<std::mutex> lock(jobMutex_);
        previewPath_ = path;
        jobReady_.notify_all();
    }

    /** Frees what the audio thread has finished with. Called by every change; a UI calls it now and then as well. */
    void idle() {
        std::lock_guard<std::mutex> lock(docMutex_);
        collect();
    }

    // ------------------------------------------------------- the audio thread

    Engine engine;

    void prepare(double fs) { engine.prepare(fs); }
    void reset() { engine.reset(); drain(); }

    /** Takes up a new document or a sound to listen to. Call at the start of a block, before its notes. */
    void begin() {
        if (const Instrument* doc = pending_.exchange(nullptr, std::memory_order_acq_rel)) engine.setDocument(doc);
        if (const AudioData* sound = pendingPreview_.exchange(nullptr, std::memory_order_acq_rel)) engine.setPreview(sound);
        if (stopPreview_.exchange(false, std::memory_order_acq_rel)) engine.stopPreview();
    }

    void process(float* const* outs, int frames) {
        begin();
        engine.process(outs, frames);
        drain();
    }

private:
    /** A ring one thread writes and another reads, for handing pointers back from the audio thread. */
    template <class T, int N> struct Ring {
        std::atomic<const T*> slot[N];
        std::atomic<unsigned> head{0}, tail{0};
        Ring() { for (auto& s : slot) s.store(nullptr); }
        bool full() const { return head.load(std::memory_order_relaxed) - tail.load(std::memory_order_acquire) >= static_cast<unsigned>(N); }
        void push(const T* p) {
            const unsigned h = head.load(std::memory_order_relaxed);
            slot[h % N].store(p, std::memory_order_relaxed);
            head.store(h + 1, std::memory_order_release);
        }
        const T* pop() {
            const unsigned t = tail.load(std::memory_order_relaxed);
            if (t == head.load(std::memory_order_acquire)) return nullptr;
            const T* p = slot[t % N].load(std::memory_order_relaxed);
            tail.store(t + 1, std::memory_order_release);
            return p;
        }
    };

    /** Audio thread: passes on what the engine no longer uses. */
    void drain() {
        while (!retired_.full()) {
            const Instrument* doc = engine.takeReleased();
            if (doc == nullptr) break;
            retired_.push(doc);
        }
        if (!retiredSounds_.full())
            if (const AudioData* sound = engine.takeReleasedPreview()) retiredSounds_.push(sound);
    }

    /** With docMutex_ held: frees the documents and previewed sounds that came back. */
    void collect() {
        while (const Instrument* doc = retired_.pop())
            alive_.erase(std::remove_if(alive_.begin(), alive_.end(), [doc](const std::shared_ptr<const Instrument>& a) { return a.get() == doc; }), alive_.end());
        while (const AudioData* sound = retiredSounds_.pop())
            previews_.erase(std::remove_if(previews_.begin(), previews_.end(), [sound](const std::shared_ptr<const AudioData>& a) { return a.get() == sound; }),
                            previews_.end());
    }

    /** With docMutex_ held: gives a new document the sounds that are in memory already, and lists the ones to read. */
    void adopt(Instrument& doc, std::vector<std::string>& wanted) {
        for (Sample& s : doc.samples) {
            if (s.missing) continue;
            if (s.data == nullptr) s.data = SampleCache::get().find(s.path);
            if (s.data == nullptr) { wanted.push_back(s.path); continue; }
            for (Part& p : doc.parts)
                for (Zone& z : p.zones)
                    if (z.sample == s.id && (z.take != 0 || z.crossfadeSeconds > 0.0)) applyFileFacts(z, *s.data);
        }
    }

    /** With docMutex_ held: makes `next` the document. */
    void publish(const std::shared_ptr<const Instrument>& next) {
        collect();
        current_ = next;
        alive_.push_back(next);
        // The one that was waiting and that the audio thread never took can go at once.
        if (const Instrument* skipped = pending_.exchange(next.get(), std::memory_order_acq_rel))
            alive_.erase(std::remove_if(alive_.begin(), alive_.end(), [skipped, &next](const std::shared_ptr<const Instrument>& a) { return a.get() == skipped && a != next; }),
                         alive_.end());
        revision_.fetch_add(1, std::memory_order_release);
    }

    void request(const std::vector<std::string>& paths) {
        if (paths.empty()) return;
        std::lock_guard<std::mutex> lock(jobMutex_);
        for (const std::string& p : paths)
            if (std::find(jobs_.begin(), jobs_.end(), p) == jobs_.end()) { jobs_.push_back(p); loading_.fetch_add(1, std::memory_order_release); }
        jobReady_.notify_all();
    }

    /** The thread that reads sounds. */
    void work() {
        for (;;) {
            std::string path, preview;
            {
                std::unique_lock<std::mutex> lock(jobMutex_);
                jobReady_.wait(lock, [this] { return quit_ || !jobs_.empty() || !previewPath_.empty(); });
                if (quit_) return;
                if (!previewPath_.empty()) preview.swap(previewPath_); // listening comes before loading
                else { path = jobs_.front(); jobs_.pop_front(); }
            }
            std::string error;
            if (!preview.empty()) {
                if (std::shared_ptr<const AudioData> sound = SampleCache::get().load(preview, error)) {
                    std::lock_guard<std::mutex> lock(docMutex_);
                    collect();
                    previews_.push_back(sound);
                    if (const AudioData* skipped = pendingPreview_.exchange(sound.get(), std::memory_order_acq_rel))
                        if (skipped != sound.get())
                            previews_.erase(std::remove_if(previews_.begin(), previews_.end(), [skipped](const std::shared_ptr<const AudioData>& a) { return a.get() == skipped; }),
                                            previews_.end());
                }
                continue;
            }
            const std::shared_ptr<const AudioData> data = SampleCache::get().load(path, error);
            {
                std::lock_guard<std::mutex> lock(docMutex_);
                bool used = false;
                for (const Sample& s : current_->samples) used = used || (s.path == path && s.data == nullptr && !s.missing);
                if (used) {
                    std::shared_ptr<Instrument> next = std::make_shared<Instrument>(*current_);
                    for (Sample& s : next->samples) {
                        if (s.path != path || s.data != nullptr) continue;
                        s.data = data;
                        s.missing = data == nullptr;
                        if (data == nullptr) continue;
                        for (Part& p : next->parts)
                            for (Zone& z : p.zones)
                                if (z.sample == s.id && (z.take != 0 || z.crossfadeSeconds > 0.0)) applyFileFacts(z, *data);
                    }
                    publish(next);
                }
            }
            loading_.fetch_sub(1, std::memory_order_release);
        }
    }

    mutable std::mutex docMutex_;
    std::shared_ptr<const Instrument> current_;
    std::vector<std::shared_ptr<const Instrument>> alive_;   // every document the audio thread may still use
    std::vector<std::shared_ptr<const AudioData>> previews_; // and every previewed sound
    std::vector<std::string> notes_;
    std::atomic<const Instrument*> pending_{nullptr};
    std::atomic<const AudioData*> pendingPreview_{nullptr};
    std::atomic<bool> stopPreview_{false};
    std::atomic<uint64_t> revision_{1};
    std::atomic<int> loading_{0};
    Ring<Instrument, 64> retired_;
    Ring<AudioData, 8> retiredSounds_;

    std::mutex jobMutex_;
    std::condition_variable jobReady_;
    std::deque<std::string> jobs_;
    std::string previewPath_;
    bool quit_ = false;
    std::thread worker_;
};

} // namespace sampler
} // namespace hnl
