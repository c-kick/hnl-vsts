// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// Measures sound files with the plugin's own code and prints the report the plugin writes, to
// compare its figures with those of other programs (README, "How it was checked"):
//   measure [--md|--json] [--square] [--window <ms>] <file> ...
//   measure --demo <folder>   writes the test sounds that comparison was made with
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../DistrhoPluginInfo.h"
#include "../dsp/Engine.hpp"
#include "Demo.hpp"

using namespace hnl::analyzer;

static void writeDemo(const std::filesystem::path& dir) {
    std::filesystem::create_directories(dir);
    std::string error;
    const auto save = [&](const char* name, double fs, const std::vector<float>& l, const std::vector<float>& r, int bits) {
        hnl::sampler::AudioData data;
        data.channels = 2;
        data.sampleRate = fs;
        data.samples[0] = l;
        data.samples[1] = r;
        if (!hnl::sampler::writeWav(dir / name, data, bits, error)) std::printf("%s: %s\n", name, error.c_str());
    };
    std::vector<float> l, r;
    demo::song(44100.0, 60.0, l, r);
    save("song-44k-16.wav", 44100.0, l, r, 16);
    demo::song(48000.0, 60.0, l, r, 3.0, 0.5);
    save("song-48k-24.wav", 48000.0, l, r, 24);
    demo::song(96000.0, 30.0, l, r, 1.0, 0.1);
    for (size_t i = 0; i < l.size(); ++i) { l[i] = l[i] * 0.3f + 0.01f; r[i] *= 0.2f; } // quiet, off centre, with a DC offset
    save("song-96k-float.wav", 96000.0, l, r, 32);
    // Quiet and loud passages: something with a loudness range.
    demo::song(48000.0, 120.0, l, r);
    for (size_t i = 0; i < l.size(); ++i) {
        const double t = static_cast<double>(i) / 48000.0, g = t < 30 ? 0.1 : (t < 60 ? 0.5 : (t < 90 ? 0.03 : 1.0));
        l[i] *= static_cast<float>(g);
        r[i] *= static_cast<float>(g);
    }
    save("dynamic-48k-24.wav", 48000.0, l, r, 24);
    // A sine that peaks between its samples.
    l.assign(48000 * 10, 0.0f);
    for (size_t i = 0; i < l.size(); ++i) l[i] = static_cast<float>(0.5 * std::sin(6.283185307179586 * (i / 4.0 + 0.125)));
    r = l;
    save("intersample-48k-24.wav", 48000.0, l, r, 24);
}

int main(int argc, char** argv) {
    int kind = 0;
    View view;
    char version[32];
    std::snprintf(version, sizeof(version), "%d.%d.%d", HNL_VERSION_MAJOR, HNL_VERSION_MINOR, HNL_VERSION_PATCH);
    int failed = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--demo" && i + 1 < argc) { writeDemo(argv[++i]); continue; }
        if (arg == "--md") { kind = 1; continue; }
        if (arg == "--json") { kind = 2; continue; }
        if (arg == "--square") { view.sine = false; continue; }
        if (arg == "--window" && i + 1 < argc) {
            const double ms = std::atof(argv[++i]);
            for (int w = 0; w < kRmsWindows; ++w)
                if (kRmsWindowMs[w] == ms) view.window = w;
            continue;
        }
        Analysis a;
        std::string error;
        if (!Engine::measureFile(References::path(arg), a, error)) {
            std::printf("%s: %s\n", arg.c_str(), error.c_str());
            ++failed;
            continue;
        }
        ReportInput in;
        in.track = &a;
        in.view = view;
        in.version = version;
        std::printf("%s\n", (kind == 1 ? markdownReport(in) : (kind == 2 ? jsonReport(in) : textReport(in))).c_str());
    }
    if (argc < 2) std::printf("measure [--md|--json] [--square] [--window <ms>] <file> ...\n");
    return failed ? 1 : 0;
}
