// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// Something to show and to test with: a made-up track of three and a half minutes measured by
// the plugin's own meter, and two sets of figures of real masters, as a mastering engineer
// would note them down (README, "Reference matching").
#pragma once

#include <cmath>
#include <memory>
#include <vector>

#include "../dsp/Engine.hpp"

namespace demo {

/** A stand-in for a song: a beat, a bass and chords, with a quiet verse, louder choruses and a break. */
inline void song(double fs, double seconds, std::vector<float>& l, std::vector<float>& r, double drive = 1.5, double bright = 0.22) {
    const size_t n = static_cast<size_t>(fs * seconds);
    l.resize(n);
    r.resize(n);
    uint32_t seed = 0x1234567u;
    const auto noise = [&]() {
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
        return static_cast<double>(seed >> 8) * (2.0 / 16777216.0) - 1.0;
    };
    double hatL = 0.0, hatR = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / fs, part = t / seconds;
        // Intro, verse, chorus, verse, chorus, break, chorus, end.
        const double level = part < 0.06 ? 0.35 : part < 0.25 ? 0.6 : part < 0.42 ? 1.0 : part < 0.58 ? 0.62 : part < 0.74 ? 1.0 : part < 0.81 ? 0.3 : part < 0.96 ? 1.05 : 0.5 * (1.0 - part) / 0.04;
        const double beat = std::fmod(t, 0.5), bar = std::fmod(t, 2.0);
        const double kick = std::exp(-beat * 14.0) * std::sin(6.283185307 * (48.0 * beat + 28.0 * (1.0 - std::exp(-beat * 30.0)) / 30.0 * 6.0));
        const double bass = 0.5 * std::sin(6.283185307 * (bar < 1.0 ? 55.0 : 65.4) * t) * (1.0 - std::exp(-beat * 40.0));
        const double chord = 0.16 * (std::sin(6.283185307 * 220.0 * t) + std::sin(6.283185307 * 277.2 * t + 0.3) + std::sin(6.283185307 * 329.6 * t + 0.7));
        const double off = std::fmod(t + 0.25, 0.5);
        const double hiss = std::exp(-off * 45.0);
        hatL += 0.6 * (noise() - hatL);
        hatR += 0.6 * (noise() - hatR);
        const double sideChord = 0.08 * std::sin(6.283185307 * 440.0 * t + 1.1);
        const double mid = 0.52 * kick + 0.3 * bass + chord;
        double a = level * (mid + sideChord + bright * hiss * (noise() - hatL));
        double b = level * (mid - sideChord + bright * hiss * (noise() - hatR));
        // As a master is: held under a ceiling.
        a = 0.79 * std::tanh(drive * a) * 1.5 / drive;
        b = 0.79 * std::tanh(drive * b) * 1.5 / drive;
        l[i] = static_cast<float>(a);
        r[i] = static_cast<float>(b);
    }
}

/** The song measured: what a capture of it gives. The other arguments make a second, different master of it. */
inline hnl::analyzer::Analysis measured(const char* name, double gain = 1.0, double seconds = 212.0, double drive = 1.5, double bright = 0.22) {
    using namespace hnl::analyzer;
    std::vector<float> l, r;
    song(44100.0, seconds, l, r, drive, bright);
    for (size_t i = 0; i < l.size(); ++i) { l[i] *= static_cast<float>(gain); r[i] *= static_cast<float>(gain); }
    std::unique_ptr<Meter> meter(new Meter());
    meter->prepare(44100.0);
    meter->process(l.data(), r.data(), static_cast<uint32_t>(l.size()));
    meter->flush();
    Analysis a;
    meter->snapshot(a.r);
    a.capture.mode = kTrackMode;
    a.capture.status = kComplete;
    a.capture.ended = kTransportStopped;
    a.capture.positionKnown = true;
    a.capture.automatic = true;
    a.name = name;
    return a;
}

/** The figures of two real masters, as JSON a person could have typed: only what was noted down. */
inline const char* currentJson() {
    return "{\"name\":\"D\\u00e9j\\u00e0 des souvenirs\",\"integrated_lufs\":-12.56,\"true_peak_dbtp\":-2.07,\"rms_reference\":\"sine\","
           "\"rms_left_db\":-15.56,\"rms_right_db\":-15.52,\"sample_peak_db\":-2.11,\"clipped_samples\":0}";
}
inline const char* referenceJson() {
    return "{\"name\":\"\\u00c0 toute allure\",\"integrated_lufs\":-12.47,\"true_peak_dbtp\":-2.04,\"rms_reference\":\"sine\","
           "\"rms_left_db\":-15.13,\"rms_right_db\":-15.20,\"sample_peak_db\":-2.10,\"clipped_samples\":0}";
}

} // namespace demo
