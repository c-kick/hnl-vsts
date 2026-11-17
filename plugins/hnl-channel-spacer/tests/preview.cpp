// Writes the plugin UI as an SVG file: preview <out.svg> [default|busy|help|menu|save]
#include <fstream>
#include <string>

#include "DistrhoUI.hpp"
#include "../Params.hpp"
#include "../Presets.hpp"
#include "../../../common/ui/Widgets.hpp"

/** A voice-like ducker: body around 250 Hz, presence around 3 kHz, some air. */
static float voice(double f) {
    const double body = -22 - 14 * std::pow(std::log2(f / 250.0), 2) / 2.0;
    const double presence = -30 - 10 * std::pow(std::log2(f / 3000.0), 2);
    const double wobble = 4 * std::sin(std::log2(f) * 5.3);
    return static_cast<float>(std::max(std::max(body, presence) + wobble, -96.0));
}

int main(int argc, char** argv) {
    const std::string mode = argc > 2 ? argv[2] : "default";
    DISTRHO::UI* ui = DISTRHO::createUI();
    if (mode != "default") {
        double ranges[4] = {0, 100, 140, 80};
        const float depth = 12, sensitivity = 55;
        ui->parameterChanged(kDepth, depth);
        ui->parameterChanged(kSensitivity, sensitivity);
        for (int i = 0; i < 4; ++i) ui->parameterChanged(kLow + i, static_cast<float>(ranges[i]));
        ui->parameterChanged(kAttack, 8);
        ui->parameterChanged(kRelease, 160);
        for (int k = 0; k < kBands; ++k) {
            const float level = voice(bandFreq(k));
            ui->parameterChanged(kMeterLevel + k, level);
            ui->parameterChanged(kMeterCut + k, static_cast<float>(cutFor(level - thresholdDb(sensitivity), depth * rangeAmount(bandFreq(k), ranges))));
        }
        ui->parameterChanged(kMeterOutL, -9.5f);
        ui->parameterChanged(kMeterOutR, -11.0f);
    }
    if (mode == "help") {
        DISTRHO::MouseEvent click; // the ? button
        click.pos.x = UI_BASE_WIDTH - 24;
        click.pos.y = 26;
        ui->onMouse(click);
        DISTRHO::MouseEvent release = click;
        release.press = false;
        ui->onMouse(release);
        DISTRHO::MotionEvent move; // the link knob
        move.pos.x = 764;
        move.pos.y = 412;
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
