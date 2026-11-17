// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// Writes the plugin UI as an SVG file: preview <out.svg> [default|busy|all|limiter|closed|menu|save|help]
//   default  as a new instance opens      busy    meters moving, a preset edited
//   all      every stage switched on      closed  every stage switched off
//   limiter  only the limiter switched on
//   menu     the preset list open       help    help on, mouse on a knob
#include <cstring>
#include <fstream>
#include <string>

#include "DistrhoUI.hpp"
#include "../Params.hpp"
#include "../Presets.hpp"
#include "../../../common/ui/Widgets.hpp"

int main(int argc, char** argv) {
    const std::string mode = argc > 2 ? argv[2] : "default";
    DISTRHO::UI* ui = DISTRHO::createUI();
    if (mode != "default" && mode != "closed") {
        const float meters[] = {-9.5f, -11.0f, -1.2f, -1.6f, -1.0f, -4.2f, -2.1f, -6.8f, -3.4f, -9.8f, -10.4f, -11.2f};
        for (uint32_t i = 0; i < kParamCount - kInputParamCount; ++i) ui->parameterChanged(kInputParamCount + i, meters[i]);
        ui->parameterChanged(kInputGain, 3.0f);
        ui->parameterChanged(kLowMakeup, -4.0f);
        ui->parameterChanged(kLimGain, 7.5f);
    }
    if (mode == "busy") ui->parameterChanged(kWideOn, 1.0f);
    if (mode == "all" || mode == "closed") {
        for (uint32_t id : {kCompOn, kEqOn, kExcOn, kWideOn, kRevOn, kLimOn}) ui->parameterChanged(id, mode == "all" ? 1.0f : 0.0f);
        ui->parameterChanged(kEqLowGain, 1.5f);
        ui->parameterChanged(kEqMidGain, -2.0f);
        ui->parameterChanged(kExcMode, 0.0f);
    }
    if (mode == "limiter")
        for (uint32_t id : {kCompOn, kEqOn, kExcOn, kWideOn, kRevOn}) ui->parameterChanged(id, 0.0f);
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
    if (mode == "help") {
        DISTRHO::MouseEvent click; // the ? button
        click.pos.x = ui->getWidth() - 24;
        click.pos.y = 26;
        ui->onMouse(click);
        DISTRHO::MouseEvent release = click;
        release.press = false;
        ui->onMouse(release);
        DISTRHO::MotionEvent move; // the limiter's gain knob
        move.pos.x = 644;
        move.pos.y = 394;
        ui->onMotion(move);
    }
    if (mode == "save") { // the name box, as after Save as... at the end of the preset list
        DISTRHO::MouseEvent click; // the preset name
        click.pos.x = ui->getWidth() - 180;
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
