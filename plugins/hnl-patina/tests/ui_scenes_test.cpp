// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// The two scenes, through the plugin's real UI class on the drawing stand-in in tools/ui-preview:
// the edit switch chooses which scene the knobs change, the copy buttons copy and exchange them,
// and the switches of a module reach the scene being edited.
#include <cstdio>
#include <string>

#include "DistrhoUI.hpp"
#include "../Params.hpp"

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
static void wheel(DISTRHO::UI* ui, float x, float y, float notches) {
    DISTRHO::ScrollEvent ev;
    ev.pos.x = x;
    ev.pos.y = y;
    ev.delta.y = notches;
    ui->onScroll(ev);
}
static std::string draw(DISTRHO::UI* ui) {
    ui->svg.clear();
    ui->onNanoDisplay();
    return ui->svg;
}
static bool has(const std::string& svg, const char* text) { return svg.find(text) != std::string::npos; }
/** The last parameter the UI sent to the host, and its value. */
static bool sent(DISTRHO::UI* ui, uint32_t id, float value) {
    return !ui->sentParameters.empty() && ui->sentParameters.back().first == id && ui->sentParameters.back().second == value;
}

// Where things are: the same numbers as in PatinaUI.cpp.
static float stripX(int module) { return 12 + module * 133.8f; }
static const float kStripY = 256;

int main() {
    DISTRHO::UI* ui = DISTRHO::createUI();
    const float driveAmountX = stripX(kDrive) + 63.5f, amountY = kStripY + 110;

    wheel(ui, driveAmountX, amountY, 1);
    check(!ui->sentParameters.empty() && ui->sentParameters.back().first == moduleParam(0, kDrive, mAmount), "a new window edits scene A: Drive's Amount knob is A's");
    click(ui, 520 + 70, 96); // the B key of the edit switch
    check(ui->sentState == "edit=b", "the edit switch is stored with the song as state");
    wheel(ui, driveAmountX, amountY, 1);
    check(ui->sentParameters.back().first == moduleParam(1, kDrive, mAmount), "and the same knob is now scene B's");
    check(has(draw(ui), ">EDITING B<"), "the window says which scene it shows");

    DISTRHO::UI* song = DISTRHO::createUI(); // a song that was saved while B was shown
    song->stateChanged("edit", "b");
    wheel(song, driveAmountX, amountY, 1);
    check(song->sentParameters.back().first == moduleParam(1, kDrive, mAmount), "a song opens on the scene it was saved with");
    delete song;

    // A module's switches reach the scene being edited (B now).
    click(ui, stripX(kVinyl) + 20, kStripY + 17);
    check(sent(ui, moduleParam(1, kVinyl, mOn), 0.0f), "the on/off switch of a module");
    click(ui, stripX(kFilter) + 100, kStripY + 41);
    check(sent(ui, moduleParam(1, kFilter, mType), 1.0f), "the right half of the type steps on");
    click(ui, stripX(kFilter) + 20, kStripY + 41);
    click(ui, stripX(kFilter) + 20, kStripY + 41);
    check(sent(ui, moduleParam(1, kFilter, mType), 2.0f), "the left half steps back, round to the last type");
    click(ui, stripX(kNoise) + 90, kStripY + 282);
    check(sent(ui, sceneParam(1, sNoiseMode), 1.0f), "Noise's React switch");
    click(ui, stripX(kNoise) + 100, kStripY + 16);
    check(sent(ui, kMains, 1.0f), "the mains frequency, which belongs to no scene");
    click(ui, 104 + 80, 233);
    check(sent(ui, kIdle, 1.0f), "In Silence");
    wheel(ui, stripX(kMotion) + 127 - 34, kStripY + 210, 1);
    check(sent(ui, moduleParam(1, kMotion, mThird), 1.0f), "one notch of the wheel on Speed is one step: 45");

    // Copying. Scene A as the host has it differs from B in the amounts (the defaults) and now in what was clicked.
    ui->sentParameters.clear();
    click(ui, 566, 126); // A > B
    bool allB = !ui->sentParameters.empty();
    for (const auto& p : ui->sentParameters) allB = allB && p.first >= kSceneB && p.first < kSceneB + kSceneSize;
    check(allB, "A > B changes only scene B");
    ui->sentParameters.clear();
    click(ui, 566, 126);
    check(ui->sentParameters.empty(), "and nothing the second time: the scenes are the same");

    ui->parameterChanged(kSceneA + sDriveAmount, 11.0f);
    ui->parameterChanged(kSceneB + sDriveAmount, 77.0f);
    ui->sentParameters.clear();
    click(ui, 566, 174); // Swap
    bool swapped = ui->sentParameters.size() == 2;
    for (const auto& p : ui->sentParameters)
        swapped = swapped && ((p.first == kSceneA + sDriveAmount && p.second == 77.0f) || (p.first == kSceneB + sDriveAmount && p.second == 11.0f));
    check(swapped, "Swap exchanges what differs");
    ui->sentParameters.clear();
    click(ui, 566, 150); // B > A
    check(ui->sentParameters.size() == 1 && sent(ui, kSceneA + sDriveAmount, 11.0f), "B > A changes only scene A");

    // The window warns when the scene on the knobs is hardly heard.
    ui->parameterChanged(kMorph, 10.0f); // B is being edited, and has a tenth of the morph
    check(has(draw(ui), ">you hear mostly A<"), "Morph at 10% while B is edited: the window says so");
    ui->parameterChanged(kMorph, 60.0f);
    check(!has(draw(ui), ">you hear mostly"), "and not when the scene has enough of it");

    // No record when Motion and Vinyl are off in both scenes.
    for (int s = 0; s < 2; ++s) {
        ui->parameterChanged(moduleParam(s, kMotion, mOn), 0.0f);
        ui->parameterChanged(moduleParam(s, kVinyl, mOn), 0.0f);
    }
    check(has(draw(ui), ">NO RECORD: MOTION AND VINYL ARE OFF<"), "Motion and Vinyl off: the window shows an empty platter");

    delete ui;
    std::printf("%s\n", failures ? "FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
