// Writes the plugin UI as an SVG file: preview <out.svg> [default|busy|help|menu]
#include <fstream>
#include <string>

#include "DistrhoUI.hpp"
#include "../Params.hpp"

int main(int argc, char** argv) {
    const std::string mode = argc > 2 ? argv[2] : "default";
    DISTRHO::UI* ui = DISTRHO::createUI();
    if (mode == "busy") {
        ui->parameterChanged(kMeterOutL, -9.5f);
        ui->parameterChanged(kMeterOutR, -10.4f);
        ui->parameterChanged(kMeterVoices, 6.0f);
        ui->parameterChanged(kTone, 25.0f);
        ui->parameterChanged(kDetune, 6.0f);
        ui->parameterChanged(kReverb, 28.0f);
        ui->parameterChanged(kDecay, 60.0f);
    }
    if (mode == "fm") {
        ui->parameterChanged(kVoice, 1.0f);
        ui->parameterChanged(kReverb, 20.0f);
        ui->parameterChanged(kMeterOutL, -12.5f);
        ui->parameterChanged(kMeterOutR, -11.8f);
        ui->parameterChanged(kMeterVoices, 4.0f);
        DISTRHO::MouseEvent click; // a key: E4
        click.pos.x = 24 + 16.5 * 772.0 / 36.0;
        click.pos.y = 390;
        ui->onMouse(click);
    }
    if (mode == "help") {
        DISTRHO::MouseEvent click; // the ? button
        click.pos.x = UI_BASE_WIDTH - 24;
        click.pos.y = 26;
        ui->onMouse(click);
        DISTRHO::MouseEvent release = click;
        release.press = false;
        ui->onMouse(release);
        DISTRHO::MotionEvent move; // the width knob
        move.pos.x = 444;
        move.pos.y = 114;
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
