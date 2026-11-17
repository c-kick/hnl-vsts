// Writes the plugin UI as an SVG file: preview <out.svg> [default|busy|help|menu|save]
#include <fstream>
#include <string>

#include "DistrhoUI.hpp"
#include "../Params.hpp"
#include "../Presets.hpp"
#include "../../../common/ui/Widgets.hpp"

int main(int argc, char** argv) {
    const std::string mode = argc > 2 ? argv[2] : "default";
    DISTRHO::UI* ui = DISTRHO::createUI();
    if (mode == "busy") {
        ui->parameterChanged(kMeterTape, 1.5f);
        ui->parameterChanged(kMeterOutL, -7.5f);
        ui->parameterChanged(kMeterOutR, -9.0f);
        ui->parameterChanged(kMeterTime, 107.1f);
        ui->parameterChanged(kSync, 1.0f);
        ui->parameterChanged(kHead1, 1.0f);
        ui->parameterChanged(kHead2, 0.0f);
        ui->parameterChanged(kHead3, 1.0f);
        ui->parameterChanged(kIntensity, 48.0f);
        ui->parameterChanged(kReverb, 35.0f);
        ui->parameterChanged(kTreble, -4.0f);
        ui->parameterChanged(kBass, 2.0f);
        ui->parameterChanged(kInput, 6.0f);
        ui->parameterChanged(kWidth, 80.0f);
    }
    if (mode == "help") {
        DISTRHO::MouseEvent click; // the ? button
        click.pos.x = UI_BASE_WIDTH - 24;
        click.pos.y = 26;
        ui->onMouse(click);
        DISTRHO::MouseEvent release = click;
        release.press = false;
        ui->onMouse(release);
        DISTRHO::MotionEvent move; // the intensity knob
        move.pos.x = 204;
        move.pos.y = 372;
        ui->onMotion(move);
    }
    if (mode == "menu") {
        DISTRHO::MouseEvent click; // the preset name
        click.pos.x = 640;
        click.pos.y = 26;
        ui->onMouse(click);
    }
    if (mode == "save") { // the name box, as after Save as... at the end of the preset list
        DISTRHO::MouseEvent click; // the preset name
        click.pos.x = 640;
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
