// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// How plugins find each other (common/Link.hpp): an instance that enters is found with its
// figures, in this process and from another; one that leaves is gone; the slot of a process
// that died is free again after ten seconds; more instances than there are slots do no harm;
// and with the link switched off everything still runs and nobody is found.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <thread>
#include <vector>

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../Link.hpp"

using namespace hnl::link;

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}
static void pause(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

/** A child process that enters as a Master, shows 7.5 as its first figure, says so on the pipe and then stays until it is killed. */
static pid_t child(int& readEnd) {
    int fds[2];
    if (pipe(fds) != 0) return -1;
    const pid_t pid = fork();
    if (pid == 0) {
        close(fds[0]);
        Publisher p(kMaster, 0x000102);
        p.set(kMasterInL, 7.5f);
        p.advance(480);
        const char ok = p.entered() ? '1' : '0';
        if (write(fds[1], &ok, 1) != 1) _exit(2);
        for (;;) pause(1000);
    }
    close(fds[1]);
    readEnd = fds[0];
    return pid;
}

int main() {
    const std::string name = "hnl-link-test-" + std::to_string(getpid());
    setenv("HNL_LINK_NAME", name.c_str(), 1);
    detail::removeBlock();

    std::unique_ptr<Reader> held(new Reader());
    Reader& reader = *held;
    check(reader.find(kMaster).empty(), "nobody there at first");

    {
        Publisher master(kMaster, (2u << 16) | (3u << 8) | 4u);
        check(master.entered(), "an instance enters");
        master.set(kMasterLufsI, -9.25f);
        master.set(kMasterGrLimiter, -3.5f);
        master.set(-1, 1.0f);
        master.set(kValues, 1.0f); // outside: ignored
        master.advance(512);
        master.advance(512);
        const std::vector<Peer> found = reader.find(kMaster);
        check(found.size() == 1, "and is found");
        if (found.size() == 1) {
            const Peer& p = found[0];
            check(p.id == master.id() && p.sameProcess && p.pid == static_cast<uint32_t>(getpid()), "as an instance of this process");
            check(p.versionText() == "2.3.4", "with its version");
            check(p.values[kMasterLufsI] == -9.25f && p.values[kMasterGrLimiter] == -3.5f && p.values[kMasterInL] == 0.0f, "its figures");
            check(p.frames == 1024, "and how far it has run");
        }
        check(reader.find(kAnalyzer).empty(), "not as another kind of plugin");

        Publisher second(kMaster, 1u << 16), analyzer(kAnalyzer, 1u << 8);
        check(reader.find(kMaster).size() == 2 && reader.find(kAnalyzer).size() == 1, "three instances, each found as what it is");
        check(second.id() != master.id() && analyzer.id() != master.id(), "each under a name of its own");
    }
    check(reader.find(kMaster).empty() && reader.find(kAnalyzer).empty(), "an instance that is destroyed is gone at once");

    // It stays found while it only waits: the beat comes from a thread of its own, not from the audio.
    {
        Publisher master(kMaster, 1);
        pause(static_cast<int>(kFreshMs) + 700);
        check(reader.find(kMaster).size() == 1, "an instance that processes nothing stays found");
    }

    // More instances than slots: the ones too many are not found, and nothing breaks.
    {
        std::vector<std::unique_ptr<Publisher>> many;
        for (int i = 0; i < kSlots + 3; ++i) many.emplace_back(new Publisher(kMaster, 1));
        int entered = 0;
        for (const auto& p : many) entered += p->entered() ? 1 : 0;
        check(entered == kSlots && static_cast<int>(reader.find(kMaster).size()) == kSlots, "sixteen instances are found, the seventeenth is not");
        many.back()->set(0, 1.0f);
        many.back()->advance(64);
        many.erase(many.begin(), many.begin() + 4); // four leave: within a second the ones that waited are in
        pause(static_cast<int>(kBeatMs) * 3);
        entered = 0;
        for (const auto& p : many) entered += p->entered() ? 1 : 0;
        check(entered == kSlots - 1, "one that found no room enters when room comes");
    }
    check(reader.find(kMaster).empty(), "all gone again");

    // Another process.
    {
        int fd = -1;
        const pid_t pid = child(fd);
        char ok = '0';
        check(pid > 0 && read(fd, &ok, 1) == 1 && ok == '1', "an instance in another process enters");
        std::vector<Peer> found = reader.find(kMaster);
        check(found.size() == 1 && !found[0].sameProcess && found[0].pid == static_cast<uint32_t>(pid) && found[0].values[kMasterInL] == 7.5f && found[0].frames == 480,
              "and is found from here, as one of another process");

        Publisher mine(kMaster, 1);
        found = reader.find(kMaster);
        check(found.size() == 2 && found[0].sameProcess && !found[1].sameProcess, "the instance of this process comes first");

        // The process dies without leaving. Soon it counts as gone; after ten seconds its slot is free.
        kill(pid, SIGKILL);
        waitpid(pid, nullptr, 0);
        close(fd);
        pause(static_cast<int>(kFreshMs) + 300);
        found = reader.find(kMaster);
        check(found.size() == 1 && found[0].id == mine.id(), "an instance whose process died is no longer found");

        std::vector<std::unique_ptr<Publisher>> rest;
        for (int i = 0; i < kSlots - 2; ++i) rest.emplace_back(new Publisher(kMaster, 1));
        Publisher late(kMaster, 1);
        check(!late.entered(), "its slot is not given away at once");
        detail::clockSkew().store(kStaleMs + 1000); // ten seconds later, for this process
        pause(static_cast<int>(kBeatMs) * 3);
        check(late.entered(), "but after ten seconds it is");
        check(static_cast<int>(reader.find(kMaster).size()) == kSlots, "and every slot holds a living instance");
        detail::clockSkew().store(0);
    }

    // A slot taken from an instance that stood still (a machine that slept): it finds itself another.
    {
        Publisher first(kMaster, 1);
        check(first.entered(), "an instance enters");
        detail::clockSkew().store(kStaleMs + 1000); // to this process the beat of `first` is now old
        Publisher thief(kMaster, 2);                // and takes its slot, the first in the block
        detail::clockSkew().store(0);
        pause(static_cast<int>(kBeatMs) * 3);
        const std::vector<Peer> found = reader.find(kMaster);
        check(found.size() == 2 && first.entered() && thief.entered(), "an instance whose slot was taken takes another, and both are found");
    }

    // One asks something of another: it is there for the other to take, once, and the answer is seen.
    {
        Publisher master(kMaster, 1);
        Command got;
        check(!master.take(got), "nothing asked: nothing to take");
        Command c;
        c.set(kAskLimiterGain, 4.5f);
        c.set(kAskLimiterOn, 1.0f);
        const uint32_t count = reader.ask(master.id(), c);
        check(count != 0 && !reader.answered(master.id(), count), "something asked of an instance waits for it");
        check(master.take(got) && got.has(kAskLimiterGain) && got.value[kAskLimiterGain] == 4.5f && got.has(kAskLimiterOn) && !got.has(kAskLimiterCeiling),
              "the instance takes it as it was asked");
        check(reader.answered(master.id(), count), "and the one who asked sees that");
        check(!master.take(got), "it is taken once");
        Command d;
        d.set(kAskLimiterCeiling, -1.5f);
        const uint32_t next = reader.ask(master.id(), d);
        check(next != 0 && next != count && !reader.answered(master.id(), next) && master.take(got) && !got.has(kAskLimiterGain) && got.value[kAskLimiterCeiling] == -1.5f &&
                  reader.answered(master.id(), next) && !reader.answered(master.id(), count),
              "a second command follows the first");
        check(reader.ask(master.id() ^ 2u, c) == 0 && !reader.answered(0, 0), "nothing can be asked of an instance that is not there");
        const uint64_t was = master.id();
        {
            Publisher other(kMaster, 1);
            check(reader.ask(other.id(), c) != 0 && !master.take(got), "what is asked of one instance does not reach another");
        }
        check(reader.ask(was, c) != 0, "still there to be asked");
    }
    {
        Publisher fresh(kMaster, 1); // in the slot the last one left with a command in it
        Command got;
        check(!fresh.take(got), "an instance that enters finds no command of the one before it");
    }

    // Switched off. (The name is read when a process first maps the block, so nobody may hold it here.)
    held.reset();
    detail::removeBlock();
    setenv("HNL_LINK_NAME", "off", 1);
    {
        Reader none;
        Publisher master(kMaster, 1);
        master.set(0, 1.0f);
        master.advance(64);
        check(!master.entered() && none.find(kMaster).empty(), "with the link switched off nobody enters and nobody is found");
    }

    std::printf(failures == 0 ? "link: all checks passed\n" : "link: %d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
