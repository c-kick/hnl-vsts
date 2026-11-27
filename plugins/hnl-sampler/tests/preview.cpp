// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// Writes the plugin UI as an SVG file: preview <out.svg> <folder with sounds> [busy|map|velocity|picture|sample|slices|mod|mix|help|menu|look]; busy is the SYNTH page,
// map, velocity and picture the three views of ZONES
// The look is the one in the settings file of HNL_SETTINGS_DIR (preview.sh writes one per look).
// The instrument shown is made here: a looped tone over the keyboard in part 1 and a sliced drum pattern in part 2.
#include <chrono>
#include <fstream>
#include <string>
#include <thread>

#include "DistrhoUI.hpp"
#include "../Params.hpp"
#include "../dsp/Sampler.hpp"

using namespace hnl::sampler;

static void click(DISTRHO::UI* ui, double x, double y, bool release = true) {
    DISTRHO::MouseEvent ev;
    ev.pos.x = x;
    ev.pos.y = y;
    ev.time = 1000;
    ui->onMouse(ev);
    if (!release) return;
    ev.press = false;
    ui->onMouse(ev);
}

int main(int argc, char** argv) {
    if (argc < 3) return 1;
    const std::string mode = argc > 3 ? argv[3] : "busy";
    const fs::path dir = fs::absolute(argv[2]).lexically_normal();
    std::string error;

    // A plucked tone with a loop in its tail, and four bars of drum hits.
    AudioData tone;
    tone.channels = 2;
    tone.sampleRate = 48000;
    for (int c = 0; c < 2; ++c) {
        tone.samples[c].resize(96000);
        for (int i = 0; i < 96000; ++i) {
            const double t = i / 48000.0, f = 220.0 * (c == 0 ? 1.0 : 1.003);
            tone.samples[c][i] = static_cast<float>(std::exp(-1.6 * t) * (0.6 * std::sin(6.2832 * f * t) + 0.25 * std::sin(6.2832 * 2 * f * t) * std::exp(-3 * t) +
                                                                          0.12 * std::sin(6.2832 * 3 * f * t) * std::exp(-6 * t)));
        }
    }
    tone.rootKey = 57;
    AudioData::Loop loop;
    loop.start = 48000; loop.end = 72000;
    tone.loops.push_back(loop);
    writeWav(dir / "Plucked A3.wav", tone, 24, error);
    AudioData drums;
    drums.channels = 1;
    drums.sampleRate = 48000;
    drums.samples[0].assign(96000, 0.0f);
    uint32_t seed = 12345u;
    for (int hit = 0; hit < 16; ++hit) {
        const int at = hit * 6000, kind = hit % 4 == 0 ? 0 : (hit % 4 == 2 ? 1 : 2);
        for (int i = 0; i < 5600; ++i) {
            const double t = i / 48000.0;
            seed = seed * 1664525u + 1013904223u;
            const double noise = (seed >> 8) / 8388608.0 - 1.0;
            const double v = kind == 0 ? 0.9 * std::sin(6.2832 * (50 + 90 * std::exp(-30 * t)) * t) * std::exp(-9 * t)
                           : kind == 1 ? 0.6 * noise * std::exp(-18 * t) + 0.3 * std::sin(6.2832 * 190 * t) * std::exp(-25 * t) : 0.3 * noise * std::exp(-60 * t);
            drums.samples[0][at + i] += static_cast<float>(v);
        }
    }
    writeWav(dir / "Drum pattern.wav", drums, 16, error);

    static Sampler sampler;
    static int token;
    Sampler::of(&token, &sampler);
    DISTRHO::UI::instancePointer() = &token;
    const bool empty = mode.rfind("empty", 0) == 0; // as a new instance opens: nothing loaded
    if (!empty) {
    sampler.edit([](Instrument& doc) {
        doc.name = "Plucked and drums";
        doc.parts[0].name = "Plucked";
        doc.parts[1].name = "Drums";
        double* P = doc.parts[0].v;
        P[kPOscLevel] = 0; P[kPFilterType] = 2; P[kPCutoff] = 2400; P[kPResonance] = 28; P[kPFilterEnv] = 45; P[kPUnison] = 3; P[kPReverbSend] = 30;
        P[envParam(0, kEnvRelease)] = 420; P[envParam(1, kEnvDecay)] = 900; P[envParam(1, kEnvAttack)] = 2;
        P[slotParam(0, kSlotSource)] = kSrcLfo1; P[slotParam(0, kSlotVia)] = kSrcWheel; P[slotParam(0, kSlotDest)] = kDstPitch; P[slotParam(0, kSlotAmount)] = 2;
        P[slotParam(1, kSlotSource)] = kSrcMacro1; P[slotParam(1, kSlotDest)] = kDstCutoff; P[slotParam(1, kSlotAmount)] = 60;
        P[slotParam(2, kSlotSource)] = kSrcSteps; P[slotParam(2, kSlotDest)] = kDstPan; P[slotParam(2, kSlotAmount)] = 35;
        for (int i = 0; i < kSteps; ++i) P[kPSeqValue + i] = std::floor(100.0 * std::sin(i * 0.9) + 0.5);
        doc.macroNames[0] = "Bright";
        doc.parts[1].v[kPChannel] = 10;
        doc.parts[1].v[kPOscLevel] = 0;
    });
    sampler.addZone(0, (dir / "Plucked A3.wav").u8string(), 0, 77, 57);
    sampler.addZone(0, (dir / "Plucked A3.wav").u8string(), 66, 127, 57);
    sampler.addZone(1, (dir / "Drum pattern.wav").u8string(), 36, 60, 48);
    for (int i = 0; i < 400 && sampler.loading() > 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    sampler.edit([](Instrument& doc) {
        // The two halves of the keyboard cross over into each other, and so do the soft and the hard layer of the lower half.
        doc.parts[0].zones[0].v[kZFadeHiKey] = 12;
        doc.parts[0].zones[1].v[kZFadeLoKey] = 12;
        doc.parts[0].zones[0].v[kZHiVel] = 100;
        doc.parts[0].zones[0].v[kZFadeHiVel] = 20;
        Zone hard = doc.parts[0].zones[0];
        hard.id = doc.nextId++;
        hard.v[kZLoVel] = 81; hard.v[kZHiVel] = 127; hard.v[kZFadeLoVel] = 20; hard.v[kZFadeHiVel] = 0; hard.v[kZGain] = 3;
        doc.parts[0].zones.push_back(hard);
    });
    }

    DISTRHO::UI* ui = DISTRHO::createUI();
    if (!empty) {
        ui->parameterChanged(kMeterOutL, -9.5f);
        ui->parameterChanged(kMeterOutR, -10.4f);
        ui->parameterChanged(kMeterVoices, 6.0f);
        ui->parameterChanged(kMacro1, 40.0f);
    }
    const auto page = [ui](int p) { click(ui, UI_BASE_WIDTH - 12.0 - (5 - p) * 76.0 + 36.0, 66.0); };
    const auto view = [ui](int v) { click(ui, 264.0 + v * 84.0 + 40.0, 96.0); };
    if (mode == "busy" || mode == "help" || mode == "menu" || mode == "look") page(2);
    if (mode == "map" || mode == "velocity" || mode == "picture" || mode == "emptymap") page(0);
    if (mode == "map") click(ui, 100, 86 + 50 + 18 + 9); // a sound chosen in the files: what can be done with it
    if (mode == "velocity") view(1);
    if (mode == "picture") view(2);
    if (mode == "emptysample") page(1);
    if (mode == "sample") { page(0); click(ui, 264 + 100, 86 + 24 + 18 + 11); page(1); }
    if (mode == "mod") page(3);
    if (mode == "mix") page(4);
    if (mode == "slices") { click(ui, 12 + 56 + 26, 66); page(1); click(ui, 264 + 632 + 112 + 50, 86 + 350 + 56 + 11); }
    if (mode == "help") {
        click(ui, UI_BASE_WIDTH - 52, 24);
        DISTRHO::MotionEvent move; // the filter panel
        move.pos.x = 380;
        move.pos.y = 230;
        ui->onMotion(move);
    }
    if (mode == "look") click(ui, UI_BASE_WIDTH - 24, 24); // the cog
    if (mode == "menu") click(ui, 200, 86 + 98 + 46 + 9, false); // the list of filters
    ui->onNanoDisplay();
    std::ofstream out(argv[1]);
    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << ui->getWidth() << "\" height=\"" << ui->getHeight()
        << "\" font-family=\"DejaVu Sans, Verdana, sans-serif\">\n" << ui->svg << "</svg>\n";
    delete ui;
    return 0;
}
