// Writes the plugin UI as an SVG file: preview <out.svg> [idle|busy]
#include <cstring>
#include <fstream>

#include "DistrhoUI.hpp"
#include "../Params.hpp"

int main(int argc, char** argv) {
    DISTRHO::UI* ui = DISTRHO::createUI();
    if (argc > 2 && std::strcmp(argv[2], "busy") == 0) {
        const float meters[] = {-9.5f, -11.0f, -1.2f, -1.6f, -1.0f, -4.2f, -2.1f, -6.8f, -3.4f, -9.8f, -10.4f, -11.2f};
        for (uint32_t i = 0; i < kParamCount - kInputParamCount; ++i) ui->parameterChanged(kInputParamCount + i, meters[i]);
        ui->parameterChanged(kInputGain, 3.0f);
        ui->parameterChanged(kLowMakeup, -4.0f);
        ui->parameterChanged(kLimGain, 7.5f);
        ui->parameterChanged(kExcOn, 0.0f);
    }
    ui->onNanoDisplay();
    std::ofstream out(argv[1]);
    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << UI_BASE_WIDTH << "\" height=\"" << UI_BASE_HEIGHT
        << "\" font-family=\"DejaVu Sans, Verdana, sans-serif\">\n" << ui->svg << "</svg>\n";
    delete ui;
    return 0;
}
