// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// The choice of look (common/ui/Skin.hpp): a new user gets Classic, the cog opens the list,
// a choice redraws the window, goes to the settings file and to nothing else, and other
// windows follow it. Runs a plugin's UI class on the drawing stand-in in tools/ui-preview,
// with HNL_SETTINGS_DIR set to a folder the test may empty (tests/run.sh does that).
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "DistrhoUI.hpp"
#include "Params.hpp"
#include "../../common/ui/Skin.hpp"

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

static std::string draw(DISTRHO::UI* ui) {
    ui->svg.clear();
    ui->onNanoDisplay();
    return ui->svg;
}
static bool has(const std::string& svg, const char* text) { return svg.find(text) != std::string::npos; }
/** Only the solid looks shade anything: a gradient in the drawing tells them from Classic. */
static bool solid(DISTRHO::UI* ui) { return has(draw(ui), "Gradient"); }
static void idle(DISTRHO::UI* ui) {
    for (int i = 0; i <= hnl::ui::SkinMenu::kPollEvery; ++i) ui->uiIdle();
}

int main() {
    namespace fs = std::filesystem;
    const char* env = std::getenv("HNL_SETTINGS_DIR");
    if (env == nullptr || env[0] == '\0') {
        std::printf("FAIL  HNL_SETTINGS_DIR is not set\n");
        return 1;
    }
    const fs::path dir(env), file = dir / "settings.txt";
    fs::remove_all(dir);

    DISTRHO::UI* ui = DISTRHO::createUI();
    const float cogX = ui->getWidth() - 24.0f, cogY = 26; // 24 from the right edge, right of the help button
    hnl::ui::SkinMenu place; // the same place as the UI's own, for where to click
    place.setButton(cogX, cogY);
    const auto row = [&](int i) { return place.rowBox(i); };
    const std::string fresh = draw(ui);
    check(!has(fresh, "Gradient") && !has(fresh, ">Hi-Fi Black<"), "a new user gets the classic look and no list");
    check(!fs::exists(file), "and nothing is written for looking at it");

    click(ui, cogX, cogY);
    const std::string open = draw(ui);
    check(has(open, ">Classic<") && has(open, ">Hi-Fi Black<") && has(open, ">Hi-Fi Silver<"), "the cog opens the list of looks");
    check(!has(open, "Gradient") && !fs::exists(file), "opening it changes nothing");

    click(ui, row(1).x + 40, row(1).y + 11);
    check(solid(ui) && hnl::settings::get("skin") == "hifi-black", "a click on a look changes the window and is stored");
    check(has(draw(ui), ">Classic<"), "the list stays open, to try the next one");
    check(ui->sentParameters.empty() && ui->sentState.empty(), "the host is told nothing: no parameter, no state");

    click(ui, place.nextBox().x + 8, place.nextBox().y + 7);
    check(hnl::settings::get("skin") == "hifi-silver", "the right arrow steps to the next look");
    click(ui, place.nextBox().x + 8, place.nextBox().y + 7);
    check(hnl::settings::get("skin") == "classic" && !solid(ui), "and round to Classic, which draws as before");
    click(ui, place.prevBox().x + 8, place.prevBox().y + 7);
    check(hnl::settings::get("skin") == "hifi-silver" && solid(ui), "the left arrow goes the other way round");

    click(ui, 200, 300); // beside the list, on a control
    check(!has(draw(ui), ">Classic<") && ui->sentParameters.empty(), "a click beside the list closes it and does not reach the control under it");

    DISTRHO::UI* second = DISTRHO::createUI(); // another plugin window opens
    check(solid(second), "a window opened later has the chosen look");
    click(second, cogX, cogY);
    click(second, row(0).x + 40, row(0).y + 11);
    check(!solid(second) && solid(ui), "a choice in one window does not reach the other at once");
    idle(ui);
    check(!solid(ui), "but within its idle time the other follows");
    delete second;

    { std::ofstream(file) << "skin = hologram\n"; }
    idle(ui);
    check(!solid(ui), "a look this version does not know gives Classic");
    { std::ofstream(file) << "skin = hifi-black\n"; }
    idle(ui);
    check(solid(ui), "a name written by hand is taken");

    fs::remove_all(dir); // the settings cannot be written: a file stands where the folder should be
    { std::ofstream(dir.string()) << "x"; }
    idle(ui);
    click(ui, cogX, cogY);
    click(ui, row(2).x + 40, row(2).y + 11);
    check(solid(ui), "when the choice cannot be stored it still holds for this window");
    idle(ui);
    check(!solid(ui), "until the settings are read again");
    fs::remove(dir);

    for (int skin = 0; skin < hnl::ui::kSkinCount; ++skin) { // every look draws in every state the meters can be in
        fs::create_directories(dir);
        { std::ofstream(file) << "skin = " << hnl::ui::skinName(skin) << "\n"; }
        idle(ui);
        for (uint32_t id = 0; id < kParamCount; ++id) {
#ifdef HNL_PARAM_INFO_FN // a plugin that describes its parameters through a function (as in ui_presets_test.cpp)
            const ParamInfo& info = paramInfo(id);
#else
            const ParamInfo& info = kParams[id];
#endif
            for (float v : {info.min, info.max}) {
                ui->parameterChanged(id, v);
                draw(ui);
            }
        }
    }
    check(true, "every look draws with every parameter at either end");
    delete ui;
    fs::remove_all(dir);

    std::printf("%s\n", failures == 0 ? "ALL PASS" : "FAILED");
    return failures == 0 ? 0 : 1;
}
