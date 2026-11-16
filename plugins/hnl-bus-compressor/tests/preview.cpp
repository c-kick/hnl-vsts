// Writes the plugin UI as an SVG file: preview <out.svg> [default|busy|help|menu]
#include <fstream>
#include <string>

#include "DistrhoUI.hpp"
#include "../Params.hpp"

int main(int argc, char** argv) {
    const std::string mode = argc > 2 ? argv[2] : "default";
    DISTRHO::UI* ui = DISTRHO::createUI();
    if (mode != "default") {
        ui->parameterChanged(kMeterGr, 4.2f);
        ui->parameterChanged(kMeterInL, -3.5f);
        ui->parameterChanged(kMeterInR, -4.8f);
        ui->parameterChanged(kMeterOutL, -5.6f);
        ui->parameterChanged(kMeterOutR, -6.9f);
    }
    if (mode == "busy") {
        ui->parameterChanged(kThreshold, -14.0f);
        ui->parameterChanged(kRatio, 1.0f);
        ui->parameterChanged(kAttack, 5.0f);
        ui->parameterChanged(kMakeup, 2.0f);
        ui->parameterChanged(kScHp, 60.0f);
        ui->parameterChanged(kMix, 80.0f);
    }
    if (mode == "help") {
        DISTRHO::MouseEvent click; // the ? button
        click.pos.x = UI_BASE_WIDTH - 24;
        click.pos.y = 26;
        ui->onMouse(click);
        DISTRHO::MouseEvent release = click;
        release.press = false;
        ui->onMouse(release);
        DISTRHO::MotionEvent move; // the release selector
        move.pos.x = 670;
        move.pos.y = 272;
        ui->onMotion(move);
    }
    if (mode == "menu") {
        DISTRHO::MouseEvent click; // the preset name
        click.pos.x = 300;
        click.pos.y = 26;
        ui->onMouse(click);
    }
    ui->onNanoDisplay();
    std::ofstream out(argv[1]);
    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << ui->getWidth() << "\" height=\"" << ui->getHeight()
        << "\" font-family=\"DejaVu Sans, Verdana, sans-serif\">\n" << ui->svg << "</svg>\n";
    delete ui;
    return 0;
}
