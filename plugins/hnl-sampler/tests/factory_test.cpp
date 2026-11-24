// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// Tests of what comes with the sampler (factory/, FactoryData.cpp): every
// sound reads, every instrument is whole and names only sounds that are
// there, the keys are covered, and the instruments play: drums ring out and
// stop, held notes keep their level and pass their loop without a click.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "../dsp/Analysis.hpp"
#include "../dsp/Sampler.hpp"

using namespace hnl::sampler;

static int failures = 0, passes = 0;
static void check(bool ok, const std::string& what, double value = 0) {
    std::printf("%s  %-66s %.3f\n", ok ? "PASS" : "FAIL", what.c_str(), value);
    if (!ok) ++failures;
    else ++passes;
}

static const double kRate = 48000.0;

/** Loads an instrument into a sampler and waits until its sounds are read. */
static bool load(Sampler& s, const std::string& text) {
    if (!s.setText(text)) return false;
    for (int i = 0; i < 4000 && s.loading() > 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    s.begin();
    s.idle();
    return s.loading() == 0;
}

/** Plays one note for `held` seconds and `after` more, and returns the left channel of output 1-2. */
static std::vector<float> play(Sampler& s, int key, int velocity, double held, double after) {
    std::vector<float> ch[2 * kOutputs];
    float* outs[2 * kOutputs];
    std::vector<float> all;
    s.reset();
    s.engine.noteOn(0, key, velocity);
    const int block = 256, total = static_cast<int>((held + after) * kRate), off = static_cast<int>(held * kRate);
    bool released = false;
    for (int done = 0; done < total; done += block) {
        if (!released && done >= off) {
            s.engine.noteOff(0, key, 0);
            released = true;
        }
        for (int c = 0; c < 2 * kOutputs; ++c) {
            ch[c].assign(block, 0.0f);
            outs[c] = ch[c].data();
        }
        s.process(outs, block);
        all.insert(all.end(), ch[0].begin(), ch[0].end());
    }
    return all;
}

static double rms(const std::vector<float>& x, double from, double to) {
    const size_t a = std::min(x.size(), static_cast<size_t>(from * kRate)), b = std::min(x.size(), static_cast<size_t>(to * kRate));
    double sum = 0;
    for (size_t i = a; i < b; ++i) sum += x[i] * x[i];
    return b > a ? std::sqrt(sum / static_cast<double>(b - a)) : 0.0;
}
static double db(double v) { return v > 1e-12 ? 20.0 * std::log10(v) : -240.0; }
static bool finite(const std::vector<float>& x) {
    for (float v : x)
        if (!(std::fabs(v) < 100.0f)) return false;
    return true;
}

int main() {
    const factory::List& all = factory::list();
    check(all.count > 0, "the plugin carries sounds and instruments", static_cast<double>(all.count));
    const std::vector<std::string> names = factory::instruments();
    check(names == std::vector<std::string>({"Jazz Kit", "Strings", "Pipe Organ"}), "three instruments: Jazz Kit, Strings, Pipe Organ", static_cast<double>(names.size()));

    // Every sound: it reads, is mono, 16 bit, at a usual rate, and is not silent or over full scale.
    size_t bytes = 0, sounds = 0, bad = 0;
    std::set<std::string> soundNames;
    for (size_t i = 0; i < all.count; ++i) {
        const factory::Item& item = all.items[i];
        bytes += item.size;
        const std::string name = item.name;
        if (name.size() < 5 || name.compare(name.size() - 5, 5, ".flac") != 0) continue;
        ++sounds;
        soundNames.insert(name);
        AudioData d;
        std::string error;
        double peak = 0;
        const bool read = decodeAudio(item.data, item.size, d, error);
        for (size_t f = 0; read && f < d.frames(); ++f) peak = std::max(peak, static_cast<double>(std::fabs(d.samples[0][f])));
        if (!read || d.channels != 1 || d.bits != 16 || (d.sampleRate != 44100.0 && d.sampleRate != 48000.0) || d.frames() < 1000 || peak < 0.05 || peak > 1.0) {
            ++bad;
            std::printf("      %s: %s\n", item.name, read ? "not as a sound here should be" : error.c_str());
        }
    }
    check(sounds > 0 && bad == 0, "every sound reads: mono, 16 bit, 44.1 or 48 kHz, not silent", static_cast<double>(sounds));
    check(bytes < 6u * 1024u * 1024u, "all of it stays under 6 MB (it is in the download twice)", bytes / 1048576.0);

    // Every instrument: it is a document, names only sounds that are there, and every zone's loop lies in its sound.
    std::set<std::string> used;
    for (const std::string& name : names) {
        Instrument doc;
        const std::string text = factory::instrument(name);
        const bool parsed = fromText(text, doc);
        check(parsed && doc.name == name && toText(doc) == text, name + ": reads as the document it was saved from");
        int zones = 0, wrong = 0;
        for (const Part& p : doc.parts)
            for (const Zone& z : p.zones) {
                ++zones;
                const Sample* s = doc.findSample(z.sample);
                const factory::Item* item = s != nullptr && factory::isFactory(s->path) ? factory::find(s->path.substr(8)) : nullptr;
                if (item == nullptr) { ++wrong; continue; }
                used.insert(s->path.substr(8));
                AudioData d;
                std::string error;
                decodeAudio(item->data, item->size, d, error);
                if (z.v[kZLoop] != 0 && !(z.v[kZLoopStart] < z.v[kZLoopEnd] && z.v[kZLoopEnd] <= static_cast<double>(d.frames()))) ++wrong;
            }
        check(zones > 0 && wrong == 0, name + ": every zone has a sound that is there, and a loop inside it", zones);
        // The kit: a key strikes one piece, whatever the velocity and whichever take's turn it is.
        if (name == "Jazz Kit") {
            int doubles = 0, keys = 0;
            for (int key = 0; key < 128; ++key) {
                int most = 0;
                for (int velocity = 1; velocity <= 127; ++velocity)
                    for (int turn = 0; turn < 2; ++turn) {
                        int covering = 0;
                        for (const Zone& z : doc.parts[0].zones)
                            covering += key >= z.v[kZLoKey] && key <= z.v[kZHiKey] && velocity >= z.v[kZLoVel] && velocity <= z.v[kZHiVel] &&
                                        turn % static_cast<int>(z.v[kZSeqLength]) == static_cast<int>(z.v[kZSeqPosition]) - 1;
                        most = std::max(most, covering);
                        doubles += covering > 1;
                    }
                keys += most > 0;
            }
            check(doubles == 0 && keys == 17, name + ": 17 keys, and none strikes two sounds at once", keys);
        }
        // Strings and organ: one zone for every key from C1 to C7.
        if (name != "Jazz Kit") {
            int gaps = 0;
            for (int key = 24; key <= 96; ++key) {
                int covering = 0;
                for (const Zone& z : doc.parts[0].zones) covering += key >= z.v[kZLoKey] && key <= z.v[kZHiKey];
                gaps += covering != 1;
            }
            check(gaps == 0, name + ": every key from C1 to C7 has one zone", gaps);
            int far = 0;
            for (const Zone& z : doc.parts[0].zones) far += z.v[kZRoot] >= z.v[kZLoKey] && z.v[kZRoot] <= z.v[kZHiKey] && (z.v[kZHiKey] - z.v[kZRoot] > 4 || z.v[kZRoot] - z.v[kZLoKey] > 4);
            check(far <= 2, name + ": no key lies more than four semitones from its sound, the ends aside", far);
        }
    }
    check(used == soundNames, "every sound is used by an instrument", static_cast<double>(used.size()));

    Sampler s;
    s.prepare(kRate);

    // The kit: every piece sounds, rings out and stops by itself; a soft hit is quieter; the closed hat cuts the open one.
    check(load(s, factory::instrument("Jazz Kit")), "Jazz Kit loads");
    {
        int missing = 0;
        for (const Sample& sm : s.document()->samples) missing += sm.missing || sm.data == nullptr;
        check(missing == 0, "Jazz Kit: no sound is missing", missing);
        const int keys[] = {35, 36, 37, 38, 40, 41, 43, 45, 47, 48, 50, 42, 44, 46, 49, 51, 53};
        int silent = 0, ringing = 0, wild = 0;
        for (int key : keys) {
            const std::vector<float> x = play(s, key, 110, 0.05, 4.5);
            silent += rms(x, 0.0, 0.2) < 0.003;
            ringing += rms(x, 4.3, 4.5) > 1e-4;
            wild += !finite(x);
        }
        check(silent == 0, "Jazz Kit: every piece sounds when its key is struck", silent);
        check(ringing == 0 && wild == 0, "Jazz Kit: every piece has stopped after four seconds, and stays a number", ringing);
        const double loud = rms(play(s, 38, 120, 0.05, 0.5), 0.0, 0.3), soft = rms(play(s, 38, 30, 0.05, 0.5), 0.0, 0.3);
        check(db(loud) - db(soft) > 10.0, "Jazz Kit: a soft snare is more than 10 dB below a hard one", db(loud) - db(soft));
        std::vector<float> ch[2 * kOutputs];
        float* outs[2 * kOutputs];
        // The open hat rings for more than a second; a closed hat half a second in cuts it off.
        const std::vector<float> open = play(s, 46, 110, 0.05, 1.2);
        s.reset();
        s.engine.noteOn(0, 46, 110);
        std::vector<float> cut;
        for (int done = 0; done < static_cast<int>(1.25 * kRate); done += 256) {
            if (done == 256 * 94) s.engine.noteOn(0, 42, 20); // a soft one, about 0.5 s in
            for (int c = 0; c < 2 * kOutputs; ++c) {
                ch[c].assign(256, 0.0f);
                outs[c] = ch[c].data();
            }
            s.process(outs, 256);
            cut.insert(cut.end(), ch[0].begin(), ch[0].end());
        }
        check(db(rms(open, 0.9, 1.1)) - db(rms(cut, 0.9, 1.1)) > 12.0, "Jazz Kit: a closed hat cuts off the open one", db(rms(open, 0.9, 1.1)) - db(rms(cut, 0.9, 1.1)));
    }

    // Strings and organ: a held note keeps its level over many turns of its loop, without a click, and ends when released.
    for (const char* name : {"Strings", "Pipe Organ"}) {
        const std::string n = name;
        check(load(s, factory::instrument(n)), n + " loads");
        int missing = 0;
        for (const Sample& sm : s.document()->samples) missing += sm.missing || sm.data == nullptr;
        check(missing == 0, n + ": no sound is missing", missing);
        double worstSwing = 0, worstClick = 0, worstTail = -240, quietest = 0, loudest = -240;
        int wild = 0;
        for (int key = 24; key <= 96; key += 3) {
            const std::vector<float> x = play(s, key, 100, 12.0, 3.0);
            wild += !finite(x);
            // The level in windows of half a second, from the first second on: the loudest against the quietest.
            double lo = 1e9, hi = 0;
            for (double t = 1.0; t < 11.5; t += 0.5) {
                const double level = rms(x, t, t + 0.5);
                lo = std::min(lo, level);
                hi = std::max(hi, level);
            }
            worstSwing = std::max(worstSwing, db(hi) - db(lo));
            quietest = std::min(quietest, db(lo));
            loudest = std::max(loudest, db(hi));
            // A click is a step between two samples far larger than any the sound makes elsewhere: the largest
            // step of every 20 ms, the largest of those against the one that 95 in a 100 stay under.
            std::vector<double> steps;
            for (size_t at = static_cast<size_t>(1.0 * kRate); at + 960 < static_cast<size_t>(11.5 * kRate); at += 960) {
                double step = 0;
                for (size_t i = at + 1; i < at + 960; ++i) step = std::max(step, static_cast<double>(std::fabs(x[i] - x[i - 1])));
                steps.push_back(step);
            }
            std::sort(steps.begin(), steps.end());
            worstClick = std::max(worstClick, steps.back() / std::max(1e-9, steps[steps.size() * 95 / 100]));
            worstTail = std::max(worstTail, db(rms(x, 14.8, 15.0)) - db(hi));
        }
        check(wild == 0, n + ": every note stays a number", wild);
        check(quietest > -60.0, n + ": every third key sounds while it is held", quietest);
        check(worstSwing < 6.0, n + ": a note held for 12 s keeps its level within 6 dB", worstSwing);
        check(worstClick < 2.5, n + ": no step in a held note stands out from the rest (no click at the loop)", worstClick);
        check(worstTail < -50.0, n + ": 3 s after release a note is more than 50 dB down", worstTail);
        std::printf("      levels of the held notes: %.1f to %.1f dB\n", quietest, loudest);
    }

    // Pitch: the A above middle C of each sounds at 440 Hz.
    for (const char* name : {"Strings", "Pipe Organ"}) {
        load(s, factory::instrument(name));
        for (int key : {45, 57, 69, 81}) {
            const std::vector<float> x = play(s, key, 100, 3.0, 0.1);
            const PitchResult p = detectPitch(x.data() + static_cast<size_t>(1.0 * kRate), static_cast<size_t>(1.8 * kRate), kRate);
            const double cents = p.hz > 0 ? 1200.0 * std::log2(p.hz / (440.0 * std::pow(2.0, (key - 69) / 12.0))) : 9999.0;
            const double folded = cents - 1200.0 * std::round(cents / 1200.0); // an octave off in the measuring is not a wrong pitch
            check(std::fabs(folded) < 12.0, std::string(name) + ": key " + std::to_string(key) + " is in tune within 12 cents", folded);
        }
    }

    // A sound this version does not have: the instrument loads, that sound is marked as missing, the rest plays.
    {
        std::string text = factory::instrument("Pipe Organ");
        const size_t at = text.find("factory:organ/c4.flac");
        text.replace(at, 21, "factory:organ/zz.flac");
        check(load(s, text), "an instrument that names a sound this version lacks still loads");
        int missing = 0;
        std::string error;
        for (const Sample& sm : s.document()->samples)
            if (sm.missing) { ++missing; error = sm.error; }
        check(missing == 1 && error == "not in this version of the plugin", "and that one sound is marked as missing, with the reason", missing);
        check(rms(play(s, 48, 100, 0.5, 0.1), 0.1, 0.5) > 0.003 && finite(play(s, 60, 100, 0.5, 0.1)), "and the other keys play");
    }

    std::printf("%d checks passed, %d failed\n", passes, failures);
    return failures == 0 ? 0 : 1;
}
