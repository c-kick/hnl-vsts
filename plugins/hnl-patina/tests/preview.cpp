// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// Writes the plugin UI as an SVG file: preview <out.svg> [default|busy|sceneb|norecord|help|menu|save|look]
// The look is the one in the settings file of HNL_SETTINGS_DIR (preview.sh writes one per look).
#include <cmath>
#include <fstream>
#include <string>

#include "DistrhoUI.hpp"
#include "../Params.hpp"
#include "../Presets.hpp"
#include "../../../common/ui/Widgets.hpp"

/** The record turning for a few seconds, with a hit now and then: what the window is told while music plays. */
static void play(DISTRHO::UI* ui, float depth) {
    for (int i = 0; i < 128; ++i) {
        const float turn = i / 54.0f; // 30 pictures a second at 33 1/3
        const bool hit = i % 27 == 20;
        const float sag = i % 27 >= 20 ? 14.0f * std::exp(-(i % 27 - 20) / 4.0f) : 0.0f;
        ui->parameterChanged(kMeterAngle, turn - std::floor(turn));
        ui->parameterChanged(kMeterCents, depth * std::sin(6.2831853f * turn) - sag);
        ui->parameterChanged(kMeterHits, hit ? 0.9f : 0.1f);
        ui->uiIdle();
    }
    ui->parameterChanged(kMeterLevel, 2.5f);
    ui->parameterChanged(kMeterHits, 0.35f);
    ui->parameterChanged(kMeterGaps, 0.08f);
    static const float react[kModuleCount] = {0.4f, 0.3f, 0.7f, 0.2f, 0.5f, 0.55f};
    for (int m = 0; m < kModuleCount; ++m) ui->parameterChanged(kMeterReact + m, react[m]);
    ui->parameterChanged(kMeterOutL, -7.5f);
    ui->parameterChanged(kMeterOutR, -8.4f);
}

static void click(DISTRHO::UI* ui, float x, float y) {
    DISTRHO::MouseEvent ev;
    ev.pos.x = x;
    ev.pos.y = y;
    ui->onMouse(ev);
    ev.press = false;
    ui->onMouse(ev);
}

int main(int argc, char** argv) {
    const std::string mode = argc > 2 ? argv[2] : "default";
    DISTRHO::UI* ui = DISTRHO::createUI();
    if (mode == "busy" || mode == "sceneb" || mode == "help" || mode == "look") { // the Charity Shop 45 preset, playing
        for (uint32_t id = 0; id < kInputParamCount; ++id)
            if (presets::inPreset(id)) ui->parameterChanged(id, presets::value(2, id));
        ui->stateChanged("preset", presets::kAll[2].name);
        ui->parameterChanged(kSceneA + sNoiseOn, 1.0f);
        play(ui, 22.0f);
    } else if (mode != "norecord") {
        play(ui, 14.0f);
    }
    if (mode == "sceneb") { // the other scene on the knobs, and Morph most of the way to A
        ui->stateChanged("edit", "b");
        ui->parameterChanged(kMorph, 15.0f);
    }
    if (mode == "norecord") { // Motion and Vinyl off in both scenes
        for (int s = 0; s < 2; ++s) {
            ui->parameterChanged(moduleParam(s, kMotion, mOn), 0.0f);
            ui->parameterChanged(moduleParam(s, kVinyl, mOn), 0.0f);
        }
    }
    if (mode == "help") {
        click(ui, UI_BASE_WIDTH - 52, 26); // the ? button
        DISTRHO::MotionEvent move; // Vinyl's React knob
        move.pos.x = 12 + 2 * 133.8f + 34;
        move.pos.y = 256 + 210;
        ui->onMotion(move);
    }
    if (mode == "look") {
        DISTRHO::MouseEvent press; // the cog
        press.pos.x = UI_BASE_WIDTH - 24;
        press.pos.y = 26;
        ui->onMouse(press);
    }
    if (mode == "menu" || mode == "save") {
        DISTRHO::MouseEvent press; // the preset name
        press.pos.x = 612;
        press.pos.y = 26;
        ui->onMouse(press);
        if (mode == "save") { // the name box, as after Save as... at the end of the preset list
            const int row = presets::list().count(); // Save as... follows the last preset
            press.pos.y = 44 + (row % hnl::ui::PresetBar::rowsPerColumn(row + 1)) * hnl::ui::PresetBar::kRow + 11;
            ui->onMouse(press);
        }
    }
    ui->svg.clear();
    ui->onNanoDisplay();
    std::ofstream out(argv[1]);
    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << ui->getWidth() << "\" height=\"" << ui->getHeight()
        << "\" font-family=\"DejaVu Sans, Verdana, sans-serif\">\n" << ui->svg << "</svg>\n";
    delete ui;
    return 0;
}
