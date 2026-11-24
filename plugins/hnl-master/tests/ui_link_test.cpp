// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// The link of the compressor's bands and the limiter's mode in the real UI class, driven by
// clicks and drags as a host window would send them.
#include <cmath>
#include <cstdio>
#include <string>

#include "DistrhoUI.hpp"
#include "../Params.hpp"

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

static void button(DISTRHO::UI* ui, double x, double y, bool press) {
    DISTRHO::MouseEvent ev;
    ev.pos.x = x;
    ev.pos.y = y;
    ev.press = press;
    static uint clock = 0;
    ev.time = clock += 1000; // a second between them: never a double click
    ui->onMouse(ev);
}
static void click(DISTRHO::UI* ui, double x, double y) {
    button(ui, x, y, true);
    button(ui, x, y, false);
}
static void moveTo(DISTRHO::UI* ui, double x, double y) {
    DISTRHO::MotionEvent ev;
    ev.pos.x = x;
    ev.pos.y = y;
    ui->onMotion(ev);
}
/** The value last sent for a parameter, or `none`. */
static float sent(DISTRHO::UI* ui, uint32_t id, float none = -999.0f) {
    float v = none;
    for (const auto& s : ui->sentParameters) if (s.first == id) v = s.second;
    return v;
}
static bool near(float a, float b) { return std::fabs(a - b) < 0.01f; }

int main() {
    DISTRHO::UI* ui = DISTRHO::createUI();
    // The compressor stands at 108, 52; the knobs of a band are 162 apart, Threshold 40 into the band and 252 down.
    // The link is the key at 12, 134 in the module, 96 x 20. A knob goes its whole way in 200 pixels.
    const double comp = 108, top = 52, lowThr = comp + 40, midThr = lowThr + 162, thrY = top + 252, linkX = comp + 60, linkY = top + 144;

    ui->sentParameters.clear();
    button(ui, lowThr, thrY, true);
    moveTo(ui, lowThr, thrY - 20); // a tenth of the way: 6 dB
    button(ui, lowThr, thrY, false);
    check(near(sent(ui, kLowThreshold), -14.0f) && sent(ui, kMidThreshold) == -999.0f && sent(ui, kHighThreshold) == -999.0f,
          "not linked: a band's knob moves that band only");
    ui->parameterChanged(kLowThreshold, -20.0f);

    click(ui, linkX, linkY);
    check(ui->sentState == "link=1", "LINK BANDS links the bands and tells the host");
    ui->parameterChanged(kMidThreshold, -10.0f);
    ui->sentParameters.clear();
    button(ui, lowThr, thrY, true);
    moveTo(ui, lowThr, thrY - 20);
    check(near(sent(ui, kLowThreshold), -14.0f) && near(sent(ui, kMidThreshold), -4.0f) && near(sent(ui, kHighThreshold), -14.0f),
          "linked: the other two bands move along by as much");
    moveTo(ui, lowThr, thrY - 60); // 18 dB up: the mid band is at its end
    check(near(sent(ui, kLowThreshold), -2.0f) && near(sent(ui, kMidThreshold), 0.0f) && near(sent(ui, kHighThreshold), -2.0f),
          "a band that reaches its end stays there");
    moveTo(ui, lowThr, thrY - 20);
    button(ui, lowThr, thrY, false);
    check(near(sent(ui, kLowThreshold), -14.0f) && near(sent(ui, kMidThreshold), -4.0f), "and has its difference back when the knob is turned back");
    check(sent(ui, kLowRatio) == -999.0f && sent(ui, kXoverLow) == -999.0f, "only the same control of each band moves");

    // From another band, and with the wheel.
    ui->sentParameters.clear();
    DISTRHO::ScrollEvent wheel;
    wheel.pos.x = midThr;
    wheel.pos.y = thrY;
    wheel.delta.y = 1; // 0.02 of the way: 1.2 dB
    ui->onScroll(wheel);
    check(near(sent(ui, kLowThreshold), -12.8f) && near(sent(ui, kMidThreshold), -2.8f) && near(sent(ui, kHighThreshold), -12.8f), "the wheel moves all three");
    // A double click returns all three to their default.
    ui->sentParameters.clear();
    DISTRHO::MouseEvent twice;
    twice.pos.x = lowThr;
    twice.pos.y = thrY;
    twice.mod = DISTRHO::kModifierControl; // Ctrl-click does the same as a double click
    ui->onMouse(twice);
    check(near(sent(ui, kLowThreshold), -20.0f) && near(sent(ui, kMidThreshold), -20.0f) && near(sent(ui, kHighThreshold), -20.0f),
          "Ctrl-click returns all three to their default");

    click(ui, linkX, linkY);
    check(ui->sentState == "link=0", "the key unlinks them again");
    ui->stateChanged("link", "1");
    ui->sentParameters.clear();
    wheel.pos.x = lowThr;
    ui->onScroll(wheel);
    check(sent(ui, kMidThreshold) != -999.0f, "the link the host kept with the song comes back");
    ui->stateChanged("link", "0");

    // The limiter stands under the reverb in the second column: at 830, 52 + 114 + 12. Its mode is the box at 12, 34
    // (188 x 20), brickwall on the left; Attack is its third knob, 131 in and 94 down.
    const double lim = 830, limTop = 52 + 114 + 12, modeY = limTop + 44, attackX = lim + 131, attackY = limTop + 94;
    ui->sentParameters.clear();
    click(ui, lim + 12 + 140, modeY);
    check(sent(ui, kLimSoft) == 1.0f, "SOFT makes the limiter soft");
    ui->sentParameters.clear();
    button(ui, attackX, attackY, true);
    moveTo(ui, attackX, attackY - 400);
    button(ui, attackX, attackY, false);
    check(near(sent(ui, kLimAttack), 50.0f), "soft: Attack goes up to 50 ms");
    ui->parameterChanged(kLimAttack, 50.0f);
    ui->sentParameters.clear();
    click(ui, lim + 12 + 40, modeY);
    check(sent(ui, kLimSoft) == 0.0f, "BRICKWALL makes it a brickwall again");
    ui->sentParameters.clear();
    button(ui, attackX, attackY, true);
    moveTo(ui, attackX, attackY - 400);
    check(near(sent(ui, kLimAttack), kBrickwallMaxAttack), "brickwall: Attack stops at 5 ms");
    moveTo(ui, attackX, attackY - 400 + 20); // back a tenth of the way, from the stop and not from 50 ms
    button(ui, attackX, attackY, false);
    check(sent(ui, kLimAttack) < 3.5f && sent(ui, kLimAttack) > 2.5f, "and turns back from there");

    // Solo and mute beside a band's name (196 down): S 96 into the band, M 120, each 22 wide.
    ui->sentParameters.clear();
    click(ui, comp + 162 + 96 + 11, top + 196);
    check(sent(ui, kMidSolo) == 1.0f, "S soloes a band");
    click(ui, comp + 2 * 162 + 120 + 11, top + 196);
    check(sent(ui, kHighMute) == 1.0f, "M mutes one");
    click(ui, comp + 162 + 96 + 11, top + 196);
    check(sent(ui, kMidSolo) == 0.0f, "and S again lets all be heard");

    // The equaliser stands at 606: under its place, the box that says what it works on (12, 58; 188 x 20).
    ui->sentParameters.clear();
    click(ui, 606 + 12 + 94, top + 68);
    check(sent(ui, kEqMode) == 1.0f, "MID puts the equaliser on the mid part");
    click(ui, 606 + 12 + 170, top + 68);
    check(sent(ui, kEqMode) == 2.0f, "SIDE on the side part");

    // The output module is the last 182 before the right margin of 12: BYPASS at 20, 426 and MATCH at 93 (69 x 20),
    // dither OFF / 16 / 24 at 66, 452 (96 x 20).
    const double out = ui->getWidth() - 12 - 182;
    ui->sentParameters.clear();
    click(ui, out + 20 + 30, top + 436);
    check(sent(ui, kBypass) == 1.0f, "BYPASS gives the unprocessed sound");
    click(ui, out + 93 + 30, top + 436);
    check(sent(ui, kMatch) == 1.0f, "MATCH matches the loudness");
    click(ui, out + 66 + 48, top + 462);
    check(sent(ui, kDither) == 1.0f, "16 dithers to 16 bit");
    click(ui, out + 66 + 80, top + 462);
    check(sent(ui, kDither) == 2.0f, "24 to 24 bit");

    // A preset leaves what is for listening and for the export alone (the next arrow, 211 into the bar that starts 296 from the right).
    ui->sentParameters.clear();
    click(ui, ui->getWidth() - 296 + 211, 26);
    bool left = !ui->sentParameters.empty();
    for (const auto& s : ui->sentParameters) left = left && !(s.first >= kLowSolo && s.first <= kMatch);
    check(left, "a preset leaves solo, mute, bypass, match and dither as they are");

    delete ui;
    std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
