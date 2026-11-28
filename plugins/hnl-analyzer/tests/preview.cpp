// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// Writes the plugin UI as an SVG file: preview <out.svg> [default|busy|reference|masters|live|file|menu|size|help|master]
//   default    as a new instance opens          busy     a track captured, the advisor at work
//   reference  the track beside a saved reference, matched in loudness
//   masters    the figures of two real masters, typed in by hand, beside each other
//   live       the running meter                file     a file being read
//   menu       the list of targets open         help     help on, mouse on the advisor
//   master     as busy, with an HNL Master in the song: found, its meters in the strip at the bottom
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>

#include "DistrhoUI.hpp"
#include "../Params.hpp"
#include "../../../common/Link.hpp"
#include "Demo.hpp"

int main(int argc, char** argv) {
    using namespace hnl::analyzer;
    const std::string mode = argc > 2 ? argv[2] : "default";
    static Engine engine;
    static int token;
    Engine::of(&token, &engine);
    DISTRHO::UI::instancePointer() = &token;

    if (mode == "masters") {
        Analysis track, reference;
        fromJson(demo::currentJson(), track);
        fromJson(demo::referenceJson(), reference);
        engine.setCompleted(track);
        References().save(reference, View());
    } else if (mode != "default") {
        if (mode == "reference") References().save(demo::measured("Reference master", 0.8, 196.0, 3.2, 0.5), View());
        if (mode == "reference") engine.setCompleted(demo::measured("Earlier take", 0.9, 204.0));
        Analysis a = demo::measured("Demo track");
        if (mode == "live") { // the meter itself, running: the song played into it
            std::vector<float> l, r;
            demo::song(44100.0, 212.0, l, r);
            engine.setMode(kContinuousMode);
            engine.prepare(44100.0);
            for (size_t at = 0; at + 4410 <= l.size() * 3 / 4; at += 4410) engine.process(l.data() + at, r.data() + at, 4410, false, 0, true);
        }
        if (mode == "file") { // a released song, measured from its file, as the reference
            Analysis released = demo::measured("Released song (WAV)", 0.8, 196.0, 3.2, 0.5);
            released.capture.mode = kFile;
            released.source = "WAV, 24 bit";
            References().save(released, View());
        }
        if (mode != "live") engine.setCompleted(a);
    }
    std::unique_ptr<hnl::link::Publisher> master; // an HNL Master at work on a loud chorus
    if (mode == "master") {
        using namespace hnl::link;
        master.reset(new Publisher(kMaster, 9u << 8));
        const float figures[][2] = {{kMasterInL, -5.8f}, {kMasterInR, -6.1f}, {kMasterGrLow, -1.7f}, {kMasterGrMid, -0.6f}, {kMasterGrHigh, -0.2f}, {kMasterGrLimiter, -2.4f},
                                    {kMasterLimiterOn, 1.0f}, {kMasterLimiterGain, 4.5f}, {kMasterLimiterCeiling, -1.0f}, {kMasterLufsS, -9.6f}, {kMasterTruePeak, -1.0f}};
        for (const auto& f : figures) master->set(static_cast<int>(f[0]), f[1]);
        engine.lufsS.store(-9.6f);
    }
    DISTRHO::UI* ui = DISTRHO::createUI();
    if (master != nullptr) master->advance(512); // it processes
    if (mode == "busy" || mode == "help" || mode == "menu" || mode == "master") { // targets the track does not reach with gain alone
        ui->parameterChanged(kTargetLufs, -8.0f);
        ui->parameterChanged(kMaxTruePeak, -1.0f);
        ui->parameterChanged(kPrefPlr, 7.5f);
        ui->stateChanged("preset", "Electronic and Club");
    }
    if (mode == "live") ui->parameterChanged(kMode, 1.0f);
    if (mode == "file") ui->stateChanged("reference", "1|Released song (WAV)");
    if (mode == "reference") ui->stateChanged("reference", "1|Reference master");
    if (mode == "masters") {
        ui->parameterChanged(kTargetLufs, -12.5f);
        ui->parameterChanged(kMaxTruePeak, -2.0f);
        ui->parameterChanged(kPrefPlr, 10.5f);
        ui->stateChanged("preset", "Streaming Loud -12.5");
        ui->stateChanged("reference", "0|\xC3\x80 toute allure");
    }
    if (mode == "menu") {
        DISTRHO::MouseEvent click;
        click.pos.x = ui->getWidth() - 180; // the preset name, at the right
        click.pos.y = 26;
        ui->onMouse(click);
        DISTRHO::MotionEvent move;
        move.pos.x = ui->getWidth() - 160;
        move.pos.y = 118;
        ui->onMotion(move);
    }
    if (mode == "size") { // the cog: the looks, and under them the sizes of the window
        DISTRHO::MouseEvent click;
        click.pos.x = ui->getWidth() - 24;
        click.pos.y = 26;
        ui->onMouse(click);
    }
    if (mode == "help") {
        DISTRHO::MouseEvent click; // the ? button
        click.pos.x = ui->getWidth() - 52;
        click.pos.y = 26;
        ui->onMouse(click);
        DISTRHO::MouseEvent release = click;
        release.press = false;
        ui->onMouse(release);
        DISTRHO::MotionEvent move; // the advisor's figures
        move.pos.x = 700;
        move.pos.y = 560;
        ui->onMotion(move);
    }
    ui->uiIdle();
    ui->onNanoDisplay();
    std::ofstream out(argv[1]);
    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << ui->getWidth() << "\" height=\"" << ui->getHeight()
        << "\" font-family=\"DejaVu Sans, Verdana, sans-serif\">\n" << ui->svg << "</svg>\n";
    delete ui;
    Engine::of(&token, nullptr, true);
    return 0;
}
