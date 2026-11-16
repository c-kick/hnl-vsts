// Writes the plugin UI as an SVG file: preview <out.svg> [default|busy|fet|help|menu]
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
        ui->parameterChanged(kThreshold, -24.0f);
        ui->parameterChanged(kRatio, 0.0f);
        ui->parameterChanged(kMakeup, 4.0f);
        ui->parameterChanged(kColour, 25.0f);
        ui->parameterChanged(kScHp, 80.0f);
    }
    if (mode == "fet") {
        ui->parameterChanged(kType, 1.0f);
        ui->parameterChanged(kThreshold, -26.0f);
        ui->parameterChanged(kAttack, 30.0f);
        ui->parameterChanged(kRelease, 30.0f);
        ui->parameterChanged(kMakeup, 6.0f);
        ui->parameterChanged(kColour, 30.0f);
        ui->parameterChanged(kMeterGr, 7.8f);
    }
    if (mode == "help") {
        DISTRHO::MouseEvent click; // the ? button
        click.pos.x = UI_BASE_WIDTH - 24;
        click.pos.y = 26;
        ui->onMouse(click);
        DISTRHO::MouseEvent release = click;
        release.press = false;
        ui->onMouse(release);
        DISTRHO::MotionEvent move; // the release knob
        move.pos.x = 296;
        move.pos.y = 372;
        ui->onMotion(move);
    }
    if (mode == "menu") {
        DISTRHO::MouseEvent click; // the preset name
        click.pos.x = 260;
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
