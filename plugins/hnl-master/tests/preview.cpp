// Writes the plugin UI as an SVG file: preview <out.svg> [default|busy|all|closed|menu]
//   default  as a new instance opens      busy    meters moving, a preset edited
//   all      every stage switched on      closed  every stage switched off
//   menu     the preset list open
#include <cstring>
#include <fstream>
#include <string>

#include "DistrhoUI.hpp"
#include "../Params.hpp"

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
    if (mode == "menu") {
        DISTRHO::MouseEvent click;
        click.pos.x = 200;
        click.pos.y = 26;
        ui->onMouse(click);
        DISTRHO::MotionEvent move;
        move.pos.x = 220;
        move.pos.y = 118;
        ui->onMotion(move);
    }
    ui->onNanoDisplay();
    std::ofstream out(argv[1]);
    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << ui->getWidth() << "\" height=\"" << ui->getHeight()
        << "\" font-family=\"DejaVu Sans, Verdana, sans-serif\">\n" << ui->svg << "</svg>\n";
    delete ui;
    return 0;
}
