// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// Writes the plugin UI as an SVG file: preview <out.svg> [default|busy|arc|leds|help|menu|save|look]
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
    if (mode != "default") {
        ui->parameterChanged(kMeterBoost, 5.4f);
        ui->parameterChanged(kMeterCut, 1.2f);
        ui->parameterChanged(kMeterInL, -3.5f);
        ui->parameterChanged(kMeterInR, -4.8f);
        ui->parameterChanged(kMeterOutL, -5.6f);
        ui->parameterChanged(kMeterOutR, -6.9f);
    }
    if (mode == "busy" || mode == "arc" || mode == "leds") {
        ui->parameterChanged(kAttack, 50.0f);
        ui->parameterChanged(kSustain, -20.0f);
        ui->parameterChanged(kLength, 25.0f);
        ui->parameterChanged(kFocus, 1.0f);
        ui->parameterChanged(kOutput, -2.0f);
    }
    if (mode == "help") {
        DISTRHO::MouseEvent click; // the ? button
        click.pos.x = UI_BASE_WIDTH - 52;
        click.pos.y = 26;
        ui->onMouse(click);
        DISTRHO::MouseEvent release = click;
        release.press = false;
        ui->onMouse(release);
        DISTRHO::MotionEvent move; // the sustain knob
        move.pos.x = 251;
        move.pos.y = 372;
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
    if (mode == "arc" || mode == "leds") ui->stateChanged("meter", mode.c_str()); // the other two meter designs, as a song that stored one opens
    ui->onNanoDisplay();
    std::ofstream out(argv[1]);
    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << ui->getWidth() << "\" height=\"" << ui->getHeight()
        << "\" font-family=\"DejaVu Sans, Verdana, sans-serif\">\n" << ui->svg << "</svg>\n";
    delete ui;
    return 0;
}
