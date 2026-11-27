// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// The size of a window (common/ui/WindowSize.hpp): it opens at its design size, the cog's list
// offers the sizes, a choice resizes the window at once and is kept for the next time, a window
// dragged by its edge is kept once it stands still, and the host is told nothing. Runs a
// plugin's UI class on the drawing stand-in in tools/ui-preview, with HNL_SETTINGS_DIR set to
// a folder the test may empty (tests/run.sh does that).
#include <cmath>
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
static bool shows(DISTRHO::UI* ui, const char* text) {
    ui->svg.clear();
    ui->onNanoDisplay();
    return ui->svg.find(text) != std::string::npos;
}
/** Whether the cog's list is open: the names of the looks stand only there. */
static bool listed(DISTRHO::UI* ui) { return shows(ui, ">Hi-Fi Black<"); }
/** How often a text is drawn: a plugin may have a control of its own that is called SIZE. */
static int count(DISTRHO::UI* ui, const char* text) {
    ui->svg.clear();
    ui->onNanoDisplay();
    int n = 0;
    for (size_t at = ui->svg.find(text); at != std::string::npos; at = ui->svg.find(text, at + 1)) ++n;
    return n;
}
/** The size that is kept, whatever name the plugin keeps it under: 0 when there is none, -1 when there are several. */
static int kept(const std::filesystem::path& file) {
    std::ifstream in(file);
    std::string line;
    int found = 0;
    while (std::getline(in, line)) {
        if (line.compare(0, 5, "size.") != 0) continue;
        const size_t at = line.find('=');
        if (at == std::string::npos) continue;
        found = found == 0 ? std::atoi(line.c_str() + at + 1) : -1;
    }
    return found;
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
    const double w = ui->getWidth(), h = ui->getHeight(); // the design, as a new user has it
    const auto times = [](double of, double zoom) { return static_cast<unsigned>(std::lround(of * zoom)); };
    const float cogX = static_cast<float>(w) - 24.0f, cogY = 26;
    hnl::ui::SkinMenu place; // the same place as the UI's own, for where to click
    place.setButton(cogX, cogY);
    place.offerSizes(100);
    const auto size = [&](int i, float zoom, bool y) { const hnl::ui::Box b = place.sizeBox(i); return zoom * (y ? b.y + b.h * 0.5f : b.x + b.w * 0.5f); };

    check(ui->leastWidth == times(w, 0.75) && ui->leastHeight == times(h, 0.75) && ui->keepsShape, "the window can be dragged down to three quarters of its design size, and keeps its shape");
    const int own = count(ui, ">SIZE<");
    check(!listed(ui) && !fs::exists(file), "a new user gets the design size, no list, and nothing is written for looking at it");
    click(ui, cogX, cogY);
    check(listed(ui) && count(ui, ">SIZE<") == own + 1 && shows(ui, ">75<") && shows(ui, ">200<"), "the cog's list offers the sizes under the looks");
    check(ui->getWidth() == w && !fs::exists(file), "opening it changes nothing");

    click(ui, size(3, 1.0f, false), size(3, 1.0f, true)); // 150
    check(ui->getWidth() == times(w, 1.5) && ui->getHeight() == times(h, 1.5) && kept(file) == 150, "a size from the list makes the window that large at once, and is kept");
    check(listed(ui), "the list stays open, to try the next one");
    click(ui, 1.5f * cogX, 1.5f * cogY); // the cog, where it is drawn now
    check(!listed(ui), "in the larger window a click lands on what is drawn there: the cog closes its list");
    check(ui->sentParameters.empty() && ui->sentState.empty(), "the host is told nothing: no parameter, no state");

    ui->setSize(times(w, 1.25), times(h, 1.25)); // the host, when the edge of the window is dragged
    ui->uiIdle();
    check(kept(file) == 150, "while the window is being dragged nothing is written");
    for (int i = 0; i <= 2 * hnl::ui::SkinMenu::kPollEvery; ++i) ui->uiIdle();
    check(kept(file) == 125, "a second after the dragging stops, that size is kept");
    click(ui, 1.25f * cogX, 1.25f * cogY);
    check(listed(ui) && shows(ui, ">125<"), "and the list shows it");
    click(ui, 1.25f * cogX, 1.25f * cogY);

    DISTRHO::UI* const again = DISTRHO::createUI();
    check(again->getWidth() == times(w, 1.25) && again->getHeight() == times(h, 1.25), "a window opened later has the size this one was left at");
    delete again;

    click(ui, 1.25f * cogX, 1.25f * cogY);
    click(ui, size(1, 1.25f, false), size(1, 1.25f, true)); // 100
    check(ui->getWidth() == w && ui->getHeight() == h && kept(file) == 100, "100 in the list brings back the design size");
    delete ui;

    DISTRHO::UI* const plain = DISTRHO::createUI();
    check(plain->getWidth() == w && plain->getHeight() == h, "a new window opens at the design size");
    delete plain;

    fs::remove_all(dir);
    std::printf("%s\n", failures == 0 ? "ALL PASS" : "FAILED");
    return failures == 0 ? 0 : 1;
}
