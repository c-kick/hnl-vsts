// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// How HNL plugins in one song find each other and read each other's figures: a small block of
// memory that every instance maps under the same name, with a place ("slot") for each instance
// that wants to be found. HNL Master enters itself and writes its meters there; HNL Analyzer
// looks who is there and shows them.
//
// Every plugin is a library of its own, so they share nothing in the host's process but what
// the system gives by name. Shared memory also holds when a host runs plugins in processes of
// their own, and it costs the audio thread a few stores and nothing else.
//
//   Windows:   a named file mapping, Local\hnl-link-1 (for the session of the user)
//   elsewhere: POSIX shared memory, /hnl-link-1-<user id>
//   HNL_LINK_NAME, when set, is the name instead (the tests use it; "off" switches the link off).
//
// The "1" is the layout below. A layout that changes gets a new number, so that plugins with
// different layouts do not see each other instead of misreading each other.
//
// Nothing here waits and nothing is locked across processes:
//   - a slot is taken by putting a token in its `owner` (compare and swap) and given back by
//     putting 0 there;
//   - an instance says it is alive by writing the time in its slot twice a second, from a
//     thread of the plugin's library (not from the audio thread, which a host may stop). A slot
//     that was not written for ten seconds belongs to a process that is gone and may be taken;
//   - every figure is an atomic of its own. A reader may see figures of two successive blocks
//     beside each other; they are meters, so that does not matter.
//
// When the block cannot be made (a sandbox that forbids it) everything here does nothing, and
// a reader finds nobody. No DPF or drawing code here.
#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#endif

namespace hnl {
namespace link {

constexpr int kSlots = 16;  // instances that can be found at once, on the whole machine
constexpr int kValues = 48; // figures an instance can show
constexpr int64_t kBeatMs = 500;    // how often an instance says it is alive
constexpr int64_t kFreshMs = 2500;  // heard from within this: it is there
constexpr int64_t kStaleMs = 10000; // not heard from for this long: its slot is free

enum Kind : uint32_t { kNobody = 0, kMaster = 1, kAnalyzer = 2 };

/** What HNL Master writes in `values`, in dB or LUFS unless it says otherwise. New figures go at the end. */
enum MasterValue {
    kMasterInL, kMasterInR,   // peak level before the chain
    kMasterOutL, kMasterOutR, // and after it
    kMasterTruePeak,          // highest true peak at the output since its meters were reset
    kMasterGrLow, kMasterGrMid, kMasterGrHigh, // gain reduction of the compressor's bands (<= 0)
    kMasterGrLimiter,                          // and of the limiter
    kMasterLufsM, kMasterLufsS, kMasterLufsI,  // loudness at the output
    kMasterMatchGain,                          // what Match turns the output by, 0 when it is off
    kMasterInputGain, kMasterLimiterGain, kMasterLimiterCeiling, // the knobs
    kMasterLimiterOn, kMasterBypass, kMasterMatch,               // 0 or 1
    kMasterSampleRate,                                           // Hz
    kMasterValueCount
};
static_assert(kMasterValueCount <= kValues, "HNL Master's figures fit in a slot");

struct Slot {
    std::atomic<uint64_t> owner;   // 0: free. Otherwise the token of the instance that has it
    std::atomic<int64_t> beat;     // when it last said it was alive, in milliseconds of now()
    std::atomic<uint32_t> ready;   // 1 once the fields below are filled in
    std::atomic<uint32_t> kind;    // Kind
    std::atomic<uint32_t> version; // of the plugin: major << 16 | minor << 8 | patch
    std::atomic<uint32_t> pid;     // the process it lives in
    std::atomic<uint64_t> frames;  // samples it has processed since it entered: stands still when the host does not run it
    std::atomic<float> values[kValues];
    std::atomic<uint32_t> reserved[70]; // zero. Room for what one instance asks of another
};

struct Block {
    std::atomic<uint32_t> magic; // 0 in a block nobody wrote in yet, then kMagic
    uint32_t unused[15];
    Slot slots[kSlots];
};

constexpr uint32_t kMagic = 0x4c4e4831u; // "1HNL"

static_assert(std::atomic<uint64_t>::is_always_lock_free && std::atomic<uint32_t>::is_always_lock_free && std::atomic<float>::is_always_lock_free,
              "what two processes share cannot hide a lock");
static_assert(sizeof(std::atomic<uint64_t>) == 8 && sizeof(std::atomic<uint32_t>) == 4 && sizeof(std::atomic<float>) == 4, "the layout is the same for every compiler");
static_assert(sizeof(Slot) == 512 && sizeof(Block) == 64 + kSlots * 512, "the layout is version 1");

namespace detail {

/** Added to the clock of this process: the tests let ten seconds pass with it. */
inline std::atomic<int64_t>& clockSkew() {
    static std::atomic<int64_t> skew{0};
    return skew;
}

/** Milliseconds on a clock that is the same for every process on the machine and never jumps. */
inline int64_t now() {
#ifdef _WIN32
    return static_cast<int64_t>(GetTickCount64()) + clockSkew().load(std::memory_order_relaxed);
#else
    timespec t{};
    clock_gettime(CLOCK_MONOTONIC, &t);
    return static_cast<int64_t>(t.tv_sec) * 1000 + t.tv_nsec / 1000000 + clockSkew().load(std::memory_order_relaxed);
#endif
}

inline uint32_t processId() {
#ifdef _WIN32
    return static_cast<uint32_t>(GetCurrentProcessId());
#else
    return static_cast<uint32_t>(getpid());
#endif
}

/** The name the block is known by; empty when the link is switched off. */
inline std::string blockName() {
    if (const char* name = std::getenv("HNL_LINK_NAME")) {
        if (std::strcmp(name, "off") == 0) return std::string();
        if (name[0] != '\0') {
            std::string clean; // letters, digits, - and _ only: it becomes part of a system name
            for (const char* c = name; *c != '\0' && clean.size() < 24; ++c)
                if ((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '-' || *c == '_') clean += *c;
            if (!clean.empty()) return clean;
        }
    }
#ifdef _WIN32
    return "hnl-link-1";
#else
    return "hnl-link-1-" + std::to_string(static_cast<unsigned long>(getuid()));
#endif
}

/** The block, mapped into this process for as long as this lives. */
class Mapping {
public:
    Mapping() {
        const std::string name = blockName();
        if (name.empty()) return;
#ifdef _WIN32
        const std::wstring wide = L"Local\\" + std::wstring(name.begin(), name.end());
        handle_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, static_cast<DWORD>(sizeof(Block)), wide.c_str());
        if (handle_ == nullptr) return;
        block_ = static_cast<Block*>(MapViewOfFile(handle_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Block)));
#else
        const std::string path = "/" + name;
        const int fd = shm_open(path.c_str(), O_RDWR | O_CREAT, 0600);
        if (fd < 0) return;
        // New memory is all zeros, which is a block with every slot free. Only the first to come
        // gives it its size (macOS refuses to do that twice).
        struct stat st{};
        bool sized = fstat(fd, &st) == 0 && st.st_size >= static_cast<off_t>(sizeof(Block));
        if (!sized) sized = ftruncate(fd, static_cast<off_t>(sizeof(Block))) == 0 || (fstat(fd, &st) == 0 && st.st_size >= static_cast<off_t>(sizeof(Block)));
        if (sized) {
            void* at = mmap(nullptr, sizeof(Block), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            if (at != MAP_FAILED) block_ = static_cast<Block*>(at);
        }
        close(fd);
#endif
        if (block_ == nullptr) return;
        // Someone else's memory under our name is left alone.
        uint32_t none = 0;
        if (!block_->magic.compare_exchange_strong(none, kMagic) && none != kMagic) unmap();
    }
    ~Mapping() { unmap(); }
    Mapping(const Mapping&) = delete;
    Mapping& operator=(const Mapping&) = delete;

    Block* block() const { return block_; }

    /** The mapping of this process (of this plugin's library, to be exact), made when first asked for. Null when it cannot be made. */
    static std::shared_ptr<Mapping> get() {
        static std::mutex mutex;
        static std::weak_ptr<Mapping> known;
        std::lock_guard<std::mutex> lock(mutex);
        std::shared_ptr<Mapping> m = known.lock();
        if (m == nullptr) {
            m = std::make_shared<Mapping>();
            known = m;
        }
        return m->block() != nullptr ? m : nullptr;
    }

private:
    void unmap() {
#ifdef _WIN32
        if (block_ != nullptr) UnmapViewOfFile(block_);
        if (handle_ != nullptr) CloseHandle(handle_);
        handle_ = nullptr;
#else
        if (block_ != nullptr) munmap(block_, sizeof(Block));
#endif
        block_ = nullptr;
    }

    Block* block_ = nullptr;
#ifdef _WIN32
    HANDLE handle_ = nullptr;
#endif
};

/** Takes the block away from the system, where that can be done (not on Windows, where it goes with its last user). For the tests. */
inline void removeBlock() {
#ifndef _WIN32
    const std::string name = blockName();
    if (!name.empty()) shm_unlink(("/" + name).c_str());
#endif
}

inline bool fresh(const Slot& s, int64_t at) {
    const int64_t age = at - s.beat.load(std::memory_order_relaxed);
    return age < kFreshMs && age > -kFreshMs;
}

} // namespace detail

/**
 * An instance that wants to be found. Enters itself when made and leaves when destroyed; set()
 * and advance() are for the audio thread and only store numbers.
 */
class Publisher {
public:
    Publisher(Kind kind, uint32_t version) : kind_(kind), version_(version) {
        static std::atomic<uint32_t> count{0};
        // A token no other instance has: the process, the moment and a count, stirred.
        uint64_t z = (static_cast<uint64_t>(detail::processId()) << 32) ^ static_cast<uint64_t>(detail::now()) ^ (static_cast<uint64_t>(count.fetch_add(1) + 1) << 20) ^
                     static_cast<uint64_t>(reinterpret_cast<uintptr_t>(this));
        z += 0x9e3779b97f4a7c15ull;
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        token_ = (z ^ (z >> 31)) | 1u;
        for (auto& v : last_) v.store(0.0f, std::memory_order_relaxed);
        enter();
        Beater::get().add(this);
    }
    ~Publisher() {
        Beater::get().remove(this);
        leave();
    }
    Publisher(const Publisher&) = delete;
    Publisher& operator=(const Publisher&) = delete;

    /** One of the figures. Kept here as well, so that it is there again after the slot was lost and another taken. */
    void set(int index, float value) {
        if (index < 0 || index >= kValues) return;
        last_[index].store(value, std::memory_order_relaxed);
        if (Slot* s = slot_.load(std::memory_order_acquire)) s->values[index].store(value, std::memory_order_relaxed);
    }
    /** So many samples were processed. */
    void advance(uint32_t frames) {
        const uint64_t total = frames_.load(std::memory_order_relaxed) + frames;
        frames_.store(total, std::memory_order_relaxed);
        if (Slot* s = slot_.load(std::memory_order_acquire)) s->frames.store(total, std::memory_order_relaxed);
    }

    bool entered() const { return slot_.load(std::memory_order_acquire) != nullptr; }
    /** What others know this instance by. */
    uint64_t id() const { return token_; }

private:
    /** Takes a free slot, or one whose owner is gone. Not from the audio thread. */
    void enter() {
        if (mapping_ == nullptr) mapping_ = detail::Mapping::get();
        if (mapping_ == nullptr) return;
        Block* block = mapping_->block();
        const int64_t at = detail::now();
        for (int i = 0; i < kSlots; ++i) {
            Slot& s = block->slots[i];
            uint64_t owner = s.owner.load(std::memory_order_acquire);
            if (owner != 0 && at - s.beat.load(std::memory_order_relaxed) < kStaleMs) continue;
            if (!s.owner.compare_exchange_strong(owner, token_)) continue;
            s.beat.store(at, std::memory_order_relaxed);
            s.ready.store(0, std::memory_order_release);
            fill(s);
            for (int v = 0; v < kValues; ++v) s.values[v].store(last_[v].load(std::memory_order_relaxed), std::memory_order_relaxed);
            for (auto& r : s.reserved) r.store(0, std::memory_order_relaxed);
            s.ready.store(1, std::memory_order_release);
            slot_.store(&s, std::memory_order_release);
            return;
        }
    }
    void fill(Slot& s) {
        s.kind.store(kind_, std::memory_order_relaxed);
        s.version.store(version_, std::memory_order_relaxed);
        s.pid.store(detail::processId(), std::memory_order_relaxed);
        s.frames.store(frames_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }
    void leave() {
        Slot* s = slot_.exchange(nullptr, std::memory_order_acq_rel);
        if (s == nullptr) return;
        uint64_t mine = token_;
        if (s->owner.load(std::memory_order_acquire) != mine) return; // taken from us: it is the other's now
        s->ready.store(0, std::memory_order_release);
        s->owner.compare_exchange_strong(mine, 0);
    }
    /** Twice a second, from the beater's thread: still here. Looks for a slot when there is none, or when ours was taken while this process stood still. */
    void beat() {
        Slot* s = slot_.load(std::memory_order_acquire);
        if (s != nullptr && s->owner.load(std::memory_order_acquire) == token_) {
            fill(*s); // again, should another have written here in the moment it took the slot for free
            s->ready.store(1, std::memory_order_release);
            s->beat.store(detail::now(), std::memory_order_relaxed);
            return;
        }
        slot_.store(nullptr, std::memory_order_release);
        enter();
    }

    /** One thread for all the instances of this plugin in this process. It runs only while there is one. */
    class Beater {
    public:
        static Beater& get() {
            static Beater beater;
            return beater;
        }
        void add(Publisher* p) {
            std::lock_guard<std::mutex> lock(mutex_);
            members_.push_back(p);
            if (members_.size() == 1) {
                stop_ = false;
                thread_ = std::thread([this] { run(); });
            }
        }
        void remove(Publisher* p) {
            std::thread ended;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                members_.erase(std::remove(members_.begin(), members_.end(), p), members_.end());
                if (members_.empty() && thread_.joinable()) {
                    stop_ = true;
                    ended = std::move(thread_);
                }
            }
            wake_.notify_all();
            if (ended.joinable()) ended.join();
        }

    private:
        void run() {
            std::unique_lock<std::mutex> lock(mutex_);
            while (!stop_) {
                for (Publisher* p : members_) p->beat();
                wake_.wait_for(lock, std::chrono::milliseconds(kBeatMs), [this] { return stop_; });
            }
        }
        std::mutex mutex_;
        std::condition_variable wake_;
        std::vector<Publisher*> members_;
        std::thread thread_;
        bool stop_ = false;
    };

    const Kind kind_;
    const uint32_t version_;
    uint64_t token_ = 1;
    std::shared_ptr<detail::Mapping> mapping_;
    std::atomic<Slot*> slot_{nullptr};
    std::atomic<uint64_t> frames_{0};
    std::atomic<float> last_[kValues];
};

/** Another instance, as it stood when it was looked at. */
struct Peer {
    uint64_t id = 0;
    uint32_t kind = kNobody, version = 0, pid = 0;
    bool sameProcess = false;
    uint64_t frames = 0;
    float values[kValues] = {};

    std::string versionText() const { return std::to_string(version >> 16) + "." + std::to_string((version >> 8) & 255) + "." + std::to_string(version & 255); }
};

/** Looks who is there. For a window: one thread, as often as it likes. */
class Reader {
public:
    /** The instances of a kind that are alive: those in this process first, each group in the order of their slots. */
    std::vector<Peer> find(Kind kind) {
        std::vector<Peer> here, elsewhere;
        if (mapping_ == nullptr) {
            // A block that could not be had is asked for again now and then, not thirty times a second.
            const int64_t at = detail::now();
            if (tried_ && at - lastTry_ < 2000 && at >= lastTry_) return here;
            tried_ = true;
            lastTry_ = at;
            mapping_ = detail::Mapping::get();
            if (mapping_ == nullptr) return here;
        }
        const Block* block = mapping_->block();
        const int64_t at = detail::now();
        const uint32_t pid = detail::processId();
        for (int i = 0; i < kSlots; ++i) {
            const Slot& s = block->slots[i];
            const uint64_t owner = s.owner.load(std::memory_order_acquire);
            if (owner == 0 || s.ready.load(std::memory_order_acquire) == 0 || s.kind.load(std::memory_order_relaxed) != kind || !detail::fresh(s, at)) continue;
            Peer p;
            p.id = owner;
            p.kind = kind;
            p.version = s.version.load(std::memory_order_relaxed);
            p.pid = s.pid.load(std::memory_order_relaxed);
            p.sameProcess = p.pid == pid;
            p.frames = s.frames.load(std::memory_order_relaxed);
            for (int v = 0; v < kValues; ++v) p.values[v] = s.values[v].load(std::memory_order_relaxed);
            if (s.owner.load(std::memory_order_acquire) != owner) continue; // it left, or another came, while we read
            (p.sameProcess ? here : elsewhere).push_back(p);
        }
        here.insert(here.end(), elsewhere.begin(), elsewhere.end());
        return here;
    }

private:
    std::shared_ptr<detail::Mapping> mapping_;
    bool tried_ = false;
    int64_t lastTry_ = 0;
};

} // namespace link
} // namespace hnl
