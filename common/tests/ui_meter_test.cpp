// The meter design: the arrows step through the three, the host is told by name, and a
// song that stored a name gets that design back. Runs a plugin's UI class on the drawing
// stand-in in tools/ui-preview, with -DMETER_LEFT and -DMETER_RIGHT (the ends of the strip
// under the meter, as given to the UI's MeterSwitch) and -DMETER_PARAM (the meter's reading).
#include <cstdio>
#include <string>

#include "DistrhoUI.hpp"
#include "Params.hpp"
#include "../../common/ui/Widgets.hpp"

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

static void click(DISTRHO::UI* ui, float x, float y) {
    DISTRHO::MouseEvent ev;
    ev.pos.x = x;
    ev.pos.y = y;
    ui->onMouse(ev);
    ev.press = false;
    ui->onMouse(ev);
}

static bool shows(DISTRHO::UI* ui, const char* text) {
    ui->svg.clear();
    ui->onNanoDisplay();
    return ui->svg.find(std::string(">") + text + "<") != std::string::npos;
}

int main() {
    const hnl::ui::MeterSwitch place(METER_LEFT, METER_RIGHT, 204); // the same place as the UI's own, for where to click
    const float prevX = place.prevBox().x + 8, nextX = place.nextBox().x + 8, y = place.prevBox().y + 7;
    DISTRHO::UI* ui = DISTRHO::createUI();
    check(shows(ui, "CLASSIC") && ui->sentState.empty(), "a new instance shows the classic meter and tells the host nothing");
    click(ui, nextX, y);
    check(ui->sentState == "meter=arc" && shows(ui, "ARC"), "the right arrow goes to the arc and the host is told");
    click(ui, nextX, y);
    check(ui->sentState == "meter=leds" && shows(ui, "LEDS"), "then to the lights");
    click(ui, nextX, y);
    check(ui->sentState == "meter=classic" && shows(ui, "CLASSIC"), "and round to the classic one");
    click(ui, prevX, y);
    check(ui->sentState == "meter=leds" && shows(ui, "LEDS"), "the left arrow goes the other way round");
    check(ui->sentParameters.empty(), "choosing a design changes no parameter");
    delete ui;

    ui = DISTRHO::createUI(); // a song opens
    ui->stateChanged("meter", "arc");
    check(shows(ui, "ARC"), "a stored design comes back");
    ui->stateChanged("meter", "hologram");
    check(shows(ui, "ARC"), "an unknown name leaves the design as it is");
    ui->stateChanged("preset", "Default");
    check(shows(ui, "ARC"), "the preset name does not touch the design");
    for (float reading : {-200.0f, -5.0f, 0.0f, 13.0f, 40.0f}) { // every design draws at any reading
        ui->parameterChanged(METER_PARAM, reading);
        for (const char* name : {"classic", "arc", "leds"}) {
            ui->stateChanged("meter", name);
            ui->svg.clear();
            ui->onNanoDisplay();
            for (const char* bad : {"nan ", "nan\"", "inf ", "inf\""}) // as a coordinate is written: before a space or a quote
                if (ui->svg.find(bad) != std::string::npos) ++failures;
        }
    }
    check(failures == 0, "all designs draw finite shapes at any reading");
    delete ui;
    return failures ? 1 : 0;
}
