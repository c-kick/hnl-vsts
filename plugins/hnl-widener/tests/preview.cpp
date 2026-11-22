// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// Writes the plugin UI as an SVG file: preview <out.svg> [default|busy|haas|wide|stereo|help|menu|save|look]
// The look is the one in the settings file of HNL_SETTINGS_DIR (preview.sh writes one per look).
#include <fstream>
#include <string>

#include "DistrhoUI.hpp"
#include "../Params.hpp"
#include "../Presets.hpp"
#include "../../../common/ui/Widgets.hpp"

int main(int argc, char** argv) {
    const std::string mode = argc > 2 ? argv[2] : "default";
    DISTRHO::UI* ui = DISTRHO::createUI();
    if (mode != "default") { // a mono sound going in
        ui->parameterChanged(kMeterInL, -6.5f);
        ui->parameterChanged(kMeterInR, -6.5f);
        ui->parameterChanged(kMeterOutL, -4.1f);
        ui->parameterChanged(kMeterOutR, -4.6f);
        ui->parameterChanged(kMeterCorrelation, 0.61f);
        ui->parameterChanged(kMeterSideOut, -6.1f);
        ui->parameterChanged(kMeterBalance, -0.4f);
    }
    if (mode == "haas") { // the Haas Classic preset: the right channel late, and what that does to mono
        for (uint32_t id = 0; id < kInputParamCount; ++id) ui->parameterChanged(id, presets::value(7, id));
        ui->parameterChanged(kMeterOutR, -6.4f);
        ui->parameterChanged(kMeterCorrelation, 0.08f);
        ui->parameterChanged(kMeterSideOut, -0.6f);
        ui->parameterChanged(kMeterBalance, -1.1f);
    }
    if (mode == "wide") { // the Everything preset
        for (uint32_t id = 0; id < kInputParamCount; ++id) ui->parameterChanged(id, presets::value(15, id));
        ui->parameterChanged(kMeterOutL, -2.2f);
        ui->parameterChanged(kMeterOutR, -2.9f);
        ui->parameterChanged(kMeterCorrelation, -0.12f);
        ui->parameterChanged(kMeterSideOut, 1.4f);
        ui->parameterChanged(kMeterBalance, 0.5f);
    }
    if (mode == "stereo") { // the Stereo Mix Wider preset on a stereo mix: nothing for a sound in the middle
        for (uint32_t id = 0; id < kInputParamCount; ++id) ui->parameterChanged(id, presets::value(13, id));
        ui->parameterChanged(kMeterInR, -7.9f);
        ui->parameterChanged(kMeterOutL, -5.8f);
        ui->parameterChanged(kMeterOutR, -7.1f);
        ui->parameterChanged(kMeterCorrelation, 0.52f);
        ui->parameterChanged(kMeterSideIn, -9.8f);
        ui->parameterChanged(kMeterSideOut, -7.2f);
        ui->parameterChanged(kMeterBalance, -0.9f);
    }
    if (mode == "help") {
        DISTRHO::MouseEvent click; // the ? button
        click.pos.x = UI_BASE_WIDTH - 52;
        click.pos.y = 26;
        ui->onMouse(click);
        DISTRHO::MouseEvent release = click;
        release.press = false;
        ui->onMouse(release);
        DISTRHO::MotionEvent move; // the Drift knob
        move.pos.x = 609;
        move.pos.y = 524;
        ui->onMotion(move);
    }
    if (mode == "look") {
        DISTRHO::MouseEvent click; // the cog
        click.pos.x = UI_BASE_WIDTH - 24;
        click.pos.y = 26;
        ui->onMouse(click);
    }
    if (mode == "menu") {
        DISTRHO::MouseEvent click; // the preset name
        click.pos.x = 612;
        click.pos.y = 26;
        ui->onMouse(click);
    }
    if (mode == "save") { // the name box, as after Save as... at the end of the preset list
        DISTRHO::MouseEvent click; // the preset name
        click.pos.x = 612;
        click.pos.y = 26;
        ui->onMouse(click);
        const int row = presets::list().count(); // Save as... follows the last preset
        // The last row stands in the last column, which is the one under the name (further columns lie to its left).
        click.pos.y = 44 + (row % hnl::ui::PresetBar::rowsPerColumn(row + 1)) * hnl::ui::PresetBar::kRow + 11;
        ui->onMouse(click);
    }
    ui->onNanoDisplay();
    std::ofstream out(argv[1]);
    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << ui->getWidth() << "\" height=\"" << ui->getHeight()
        << "\" font-family=\"DejaVu Sans, Verdana, sans-serif\">\n" << ui->svg << "</svg>\n";
    delete ui;
    return 0;
}
