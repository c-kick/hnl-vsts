// Writes the plugin UI as an SVG file: preview <out.svg> [default|busy|crush|body|help|menu|save]
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
        ui->parameterChanged(kMeterInL, -6.5f);
        ui->parameterChanged(kMeterInR, -7.8f);
        ui->parameterChanged(kMeterOutL, -2.6f);
        ui->parameterChanged(kMeterOutR, -4.9f);
        ui->parameterChanged(kMeterCorrelation, 0.93f);
    }
    if (mode == "crush") { // the Parallel Crush preset at work, with Level Match on
        for (uint32_t id = 0; id < kInputParamCount; ++id) ui->parameterChanged(id, presets::value(7, id));
        ui->parameterChanged(kMatch, 1.0f);
        ui->parameterChanged(kMeterCrush, 11.4f);
        ui->parameterChanged(kMeterMatch, -2.8f);
        ui->parameterChanged(kMeterCorrelation, 0.78f);
    }
    if (mode == "body") { // the Body Only preset
        for (uint32_t id = 0; id < kInputParamCount; ++id) ui->parameterChanged(id, presets::value(6, id));
        ui->parameterChanged(kMeterCorrelation, 0.86f);
    }
    if (mode == "help") {
        DISTRHO::MouseEvent click; // the ? button
        click.pos.x = UI_BASE_WIDTH - 24;
        click.pos.y = 26;
        ui->onMouse(click);
        DISTRHO::MouseEvent release = click;
        release.press = false;
        ui->onMouse(release);
        DISTRHO::MotionEvent move; // the delay knob
        move.pos.x = 183;
        move.pos.y = 396;
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
        click.pos.x -= (row / hnl::ui::PresetBar::kMaxRows) * 170; // further columns lie to the left
        click.pos.y = 44 + (row % hnl::ui::PresetBar::kMaxRows) * hnl::ui::PresetBar::kRow + 11;
        ui->onMouse(click);
    }
    ui->onNanoDisplay();
    std::ofstream out(argv[1]);
    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << ui->getWidth() << "\" height=\"" << ui->getHeight()
        << "\" font-family=\"DejaVu Sans, Verdana, sans-serif\">\n" << ui->svg << "</svg>\n";
    delete ui;
    return 0;
}
