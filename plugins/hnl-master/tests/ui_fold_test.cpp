// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// Folding stages in the real UI class, driven by clicks as a host window would send them:
// what moves the window (the fold arrow, and switching a folded stage on) and what must not
// (switching a stage off, a preset, the host changing a switch).
#include <cstdio>
#include <string>

#include "DistrhoUI.hpp"
#include "../Params.hpp"

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

static void click(DISTRHO::UI* ui, double x, double y) {
    DISTRHO::MouseEvent ev;
    ev.pos.x = x;
    ev.pos.y = y;
    ui->onMouse(ev);
    ev.press = false;
    ui->onMouse(ev);
}

int main() {
    DISTRHO::UI* ui = DISTRHO::createUI();
    // With everything open and 12 between the modules: input 12..96, compressor 108..594, a column of 212 from
    // 606 (equaliser on top) and one from 830 (reverb on top), then the output. A module's switch is 28 from its
    // right edge, its fold arrow 62, both 15 under its top (52).
    const double eq = 606, eqSwitch = eq + 212 - 28, eqFold = eq + 212 - 62, compFold = 108 + 486 - 62, row = 52 + 15;
    const uint full = UI_BASE_WIDTH;

    check(ui->getWidth() == full, "a new window is full width, with stages off");
    for (uint32_t id : {kCompOn, kEqOn, kExcOn, kWideOn, kRevOn, kLimOn}) ui->parameterChanged(id, 0.0f);
    check(ui->getWidth() == full, "the host switching every stage off moves nothing");
    for (uint32_t id : {kCompOn, kEqOn, kExcOn, kWideOn, kRevOn, kLimOn}) ui->parameterChanged(id, 1.0f);
    check(ui->getWidth() == full, "nor does switching every stage on");

    // The equaliser's place: the left half of the box under its title puts it before the compressor.
    ui->sentParameters.clear();
    click(ui, eq + 12 + 40, 52 + 44);
    check(ui->sentParameters.size() == 1 && ui->sentParameters[0].first == kEqFirst && ui->sentParameters[0].second == 1.0f, "BEFORE COMP puts the equaliser first");
    ui->sentParameters.clear();
    click(ui, eq + 12 + 140, 52 + 44);
    check(ui->sentParameters.size() == 1 && ui->sentParameters[0].second == 0.0f, "AFTER COMP puts it back");
    check(ui->getWidth() == full, "and the window stays as it is");

    // The equaliser is on. Fold it, then switch it off: it stays folded.
    click(ui, eqFold, row);
    check(ui->sentState == "folded=010000", "the fold arrow folds a stage and tells the host");
    check(ui->getWidth() == full, "one folded stage leaves the columns as wide as they were");
    ui->sentParameters.clear();
    click(ui, eqSwitch, row);
    check(ui->sentParameters.size() == 1 && ui->sentParameters[0].first == kEqOn && ui->sentParameters[0].second == 0.0f, "the switch of a folded stage still works");
    check(ui->sentState == "folded=010000", "a folded stage that is switched off stays folded");
    // Switched on again by hand, it opens.
    click(ui, eqSwitch, row);
    check(ui->sentState == "folded=000000", "a folded stage that is switched on opens");

    // Folded and off, then switched on by the host (automation, a preset): nothing moves.
    click(ui, eqFold, row);
    click(ui, eqSwitch, row);
    ui->parameterChanged(kEqOn, 1.0f);
    check(ui->sentState == "folded=010000", "the host switching a folded stage on does not open it");

    // The compressor folds into the columns, and the window narrows by its width and gap.
    click(ui, compFold, row);
    check(ui->sentState == "folded=110000", "the compressor folds too");
    check(ui->getWidth() == full - 486 - 12, "and the window narrows by its width");

    // What the host kept with the song comes back.
    ui->stateChanged("folded", "000000");
    check(ui->getWidth() == full, "the saved state opens everything again");
    ui->stateChanged("folded", "111111");
    check(ui->getWidth() == 12 + 84 + 12 + 212 + 12 + 182 + 12, "everything folded is one column wide");
    // Everything folded and off, then through all the presets with the next arrow (211 into the bar, which starts
    // 268 from the right edge): they switch stages on, and still nothing opens or moves.
    for (uint32_t id : {kCompOn, kEqOn, kExcOn, kWideOn, kRevOn, kLimOn}) ui->parameterChanged(id, 0.0f);
    const uint narrow = ui->getWidth();
    bool still = true, switchedOn = false;
    for (int i = 0; i < 14; ++i) {
        ui->sentParameters.clear();
        click(ui, narrow - 268 + 211, 26);
        for (const auto& sent : ui->sentParameters)
            for (uint32_t id : {kCompOn, kEqOn, kExcOn, kWideOn, kRevOn, kLimOn}) switchedOn = switchedOn || (sent.first == id && sent.second > 0.5f);
        still = still && ui->getWidth() == narrow && ui->sentState.rfind("preset=", 0) == 0;
    }
    check(switchedOn, "stepping through the presets switches folded stages on");
    check(still, "and they stay folded, the window as wide as it was");
    ui->stateChanged("folded", "1");
    check(ui->getWidth() == full - 486 - 12, "a short or older state folds what it names");
    ui->stateChanged("folded", "");
    check(ui->getWidth() == full, "an empty state folds nothing");

    delete ui;
    std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
