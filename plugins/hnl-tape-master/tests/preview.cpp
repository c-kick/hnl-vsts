// Writes the plugin UI as an SVG file: preview <out.svg> [default|busy]
#include <fstream>
#include <string>

#include "DistrhoUI.hpp"
#include "../Params.hpp"

int main(int argc, char** argv) {
    const std::string mode = argc > 2 ? argv[2] : "default";
    DISTRHO::UI* ui = DISTRHO::createUI();
    if (mode == "busy") {
        ui->parameterChanged(kMeterVuL, 1.2f);
        ui->parameterChanged(kMeterVuR, -1.5f);
        ui->parameterChanged(kMeterOutL, -7.5f);
        ui->parameterChanged(kMeterOutR, -9.0f);
        ui->parameterChanged(kRecord, 6.0f);
        ui->parameterChanged(kSpeed, 1.0f);
        ui->parameterChanged(kFormula, 0.0f);
        ui->parameterChanged(kBias, -25.0f);
        ui->parameterChanged(kNoise, 50.0f);
        ui->parameterChanged(kWow, 30.0f);
    }
    ui->onNanoDisplay();
    std::ofstream out(argv[1]);
    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << ui->getWidth() << "\" height=\"" << ui->getHeight()
        << "\" font-family=\"DejaVu Sans, Verdana, sans-serif\">\n" << ui->svg << "</svg>\n";
    delete ui;
    return 0;
}
