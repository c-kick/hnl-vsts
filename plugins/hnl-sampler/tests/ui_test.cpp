// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// Drives the UI class itself, on the drawing stand-in of tools/ui-preview: clicks and drags as a
// user would, and looks at what that did to the sampler's instrument.
//   ui_test <an empty folder to work in>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include "DistrhoUI.hpp"
#include "../Params.hpp"
#include "../dsp/Sampler.hpp"

using namespace hnl::sampler;

static int failures = 0;
static void check(bool ok, const char* what, double value = 0) {
    std::printf("%s  %-66s %.3f\n", ok ? "PASS" : "FAIL", what, value);
    if (!ok) ++failures;
}

static DISTRHO::UI* ui;
static unsigned now = 1000;

static void press(double x, double y, bool down = true) {
    DISTRHO::MouseEvent ev;
    ev.pos.x = x; ev.pos.y = y;
    ev.press = down;
    ev.time = now;
    ui->onMouse(ev);
}
static void click(double x, double y) { now += 1000; press(x, y); press(x, y, false); }
static void twice(double x, double y) { now += 1000; press(x, y); press(x, y, false); now += 100; press(x, y); press(x, y, false); }
static void drag(double x0, double y0, double x1, double y1) {
    now += 1000;
    press(x0, y0);
    DISTRHO::MotionEvent move;
    for (int i = 1; i <= 10; ++i) {
        move.pos.x = x0 + (x1 - x0) * i / 10.0;
        move.pos.y = y0 + (y1 - y0) * i / 10.0;
        ui->onMotion(move);
    }
    press(x1, y1, false);
}
static void page(int p) { click(988.0 - (6 - p) * 76.0 + 36.0, 66.0); }
static void type(const char* text) {
    for (const char* c = text; *c; ++c) {
        DISTRHO::CharacterInputEvent ev;
        ev.character = static_cast<unsigned>(*c);
        ev.string[0] = *c;
        ui->onCharacterInput(ev);
    }
}
static void key(unsigned code) {
    DISTRHO::KeyboardEvent ev;
    ev.key = code;
    ui->onKeyboard(ev);
}
static bool wait(Sampler& s) {
    for (int i = 0; i < 500 && s.loading() > 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ui->uiIdle();
    return s.loading() == 0;
}

int main(int argc, char** argv) {
    if (argc < 2) return 1;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const fs::path dir = argv[1];
    std::string error;
    AudioData tone;
    tone.channels = 1;
    tone.sampleRate = 48000;
    tone.samples[0].resize(48000);
    for (int i = 0; i < 48000; ++i) tone.samples[0][i] = static_cast<float>(0.25 * std::sin(6.283185307 * 220.0 * i / 48000.0) * (1.0 - 0.3 * i / 48000.0));
    tone.rootKey = 57;
    writeWav(dir / "a tone.wav", tone, 16, error);
    AudioData hits;
    hits.channels = 1;
    hits.sampleRate = 48000;
    hits.samples[0].assign(48000, 0.0f);
    for (int h = 0; h < 4; ++h)
        for (int i = 0; i < 6000; ++i) hits.samples[0][h * 12000 + i] = static_cast<float>(0.8 * std::sin(6.283185307 * 90.0 * i / 48000.0) * std::exp(-i / 900.0));
    writeWav(dir / "b hits.wav", hits, 16, error);

    static Sampler sampler;
    static int token;
    Sampler::of(&token, &sampler);
    DISTRHO::UI::instancePointer() = &token;
    ui = DISTRHO::createUI();
    ui->onNanoDisplay();
    check(ui->svg.find("SAMPLER") != std::string::npos && ui->svg.find("No sounds in this part yet") != std::string::npos && ui->svg.find("ADD A SOUND") != std::string::npos,
          "opens on the MAP page of the plugin's own, empty instrument, with the way to add a sound");
    click(12 + 488, 86 + 199 + 25); // ADD A SOUND
    ui->svg.clear();
    ui->onNanoDisplay();
    check(ui->svg.find("a tone.wav") != std::string::npos && ui->svg.find("ADD AS ZONE") == std::string::npos, "ADD A SOUND leads to the files, where nothing is offered before a file is chosen");
    page(2);

    // A knob: Osc level, dragged down by half its travel; a double click sets it back to its default.
    drag(97, 138, 97, 38);
    check(std::fabs(sampler.document()->parts[0].v[kPOscLevel] - 50.0) < 0.5, "dragging a knob changes the instrument (osc level)", sampler.document()->parts[0].v[kPOscLevel]);
    twice(97, 138);
    check(sampler.document()->parts[0].v[kPOscLevel] == 0.0, "a double click sets it back to its default", sampler.document()->parts[0].v[kPOscLevel]);
    drag(97, 138, 97, 38);

    // A list: the filter type.
    click(160, 239);
    ui->svg.clear();
    ui->onNanoDisplay();
    check(ui->svg.find("Low 24") != std::string::npos && ui->svg.find("Morph") != std::string::npos, "a click on a choice opens its list");
    click(160, 250 + 3 + 2 * 18 + 9);
    check(sampler.document()->parts[0].v[kPFilterType] == 2.0, "and a click in the list chooses (Low 24)", sampler.document()->parts[0].v[kPFilterType]);

    // The host's parameters: the volume knob in the header, and a macro.
    drag(770, 27, 770, 77);
    bool sent = false;
    for (const auto& p : ui->sentParameters) sent = sent || (p.first == kVolume && p.second < -10.0f);
    check(sent, "the volume knob goes to the host as a parameter");

    // Another part: the same page shows that part's settings.
    click(12 + 56 + 26, 66);
    drag(97, 138, 97, 38);
    check(std::fabs(sampler.document()->parts[1].v[kPOscLevel] - 50.0) < 0.5 && std::fabs(sampler.document()->parts[0].v[kPOscLevel] - 50.0) < 0.5, "a part tab makes the pages work on that part");
    click(12 + 26, 66);

    // FILES: the folder with the two sounds; a double click on a sound adds it as a zone.
    page(5);
    ui->svg.clear();
    ui->onNanoDisplay();
    check(ui->svg.find("a tone.wav") != std::string::npos && ui->svg.find("b hits.wav") != std::string::npos, "FILES lists the sounds of the folder");
    twice(200, 86 + 32 + 20 + 10); // the second row: the first is the folder of saved instruments
    const bool read = wait(sampler);
    std::shared_ptr<const Instrument> doc = sampler.document();
    check(read && doc->parts[0].zones.size() == 1 && doc->samples.size() == 1 && doc->samples[0].data != nullptr && doc->parts[0].zones[0].v[kZRoot] == 57,
          "a double click on a sound adds a zone, with the root key of its file", doc->parts[0].zones.empty() ? -1 : doc->parts[0].zones[0].v[kZRoot]);

    // MAP: the zone covers all keys; its right edge dragged to the middle, then the whole moved up an octave.
    page(0);
    const double keyW = 976.0 / 128.0;
    drag(12 + 128 * keyW - 2, 250, 12 + 60.5 * keyW, 250);
    check(sampler.document()->parts[0].zones[0].v[kZHiKey] == 60, "dragging a zone's edge changes its range", sampler.document()->parts[0].zones[0].v[kZHiKey]);
    drag(12 + 30.5 * keyW, 250, 12 + 42.5 * keyW, 250);
    doc = sampler.document();
    check(doc->parts[0].zones[0].v[kZLoKey] == 12 && doc->parts[0].zones[0].v[kZHiKey] == 72 && doc->parts[0].zones[0].v[kZRoot] == 69, "dragging its middle moves it, root and all",
          doc->parts[0].zones[0].v[kZLoKey]);
    drag(12 + 40 * keyW, 86 + 2, 12 + 40 * keyW, 86 + 199);
    check(std::fabs(sampler.document()->parts[0].zones[0].v[kZHiVel] - 64) <= 1, "dragging its top edge sets the highest velocity", sampler.document()->parts[0].zones[0].v[kZHiVel]);

    // SAMPLE: the end marker dragged to the middle of the sound; a loop found by the tool.
    page(1);
    drag(12 + 976 - 3, 200, 12 + 488, 200);
    check(std::fabs(sampler.document()->parts[0].zones[0].v[kZEnd] - 24000) < 100, "dragging the end marker sets the zone's end", sampler.document()->parts[0].zones[0].v[kZEnd]);
    click(12 + 632 + 42, 86 + 396 + 11);
    doc = sampler.document();
    const double span = doc->parts[0].zones[0].v[kZLoopEnd] - doc->parts[0].zones[0].v[kZLoopStart], period = 48000.0 / 220.0;
    check(doc->parts[0].zones[0].v[kZLoop] == 1 && span > 2000 && std::fabs(span / period - std::round(span / period)) < 0.05, "FIND LOOP sets a loop of whole periods",
          span / period);

    // The second sound on the next key, then cut at its hits.
    page(5);
    click(200, 86 + 32 + 40 + 10);
    click(12 + 652 + 158 + 75, 86 + 150 + 12);
    wait(sampler);
    doc = sampler.document();
    check(doc->parts[0].zones.size() == 2 && doc->parts[0].zones[1].v[kZLoKey] == 73 && doc->parts[0].zones[1].v[kZHiKey] == 73 && doc->parts[0].zones[1].v[kZRoot] == 73,
          "ADD ON NEXT KEY puts a sound on the key after the highest zone", doc->parts[0].zones.size());
    page(1);
    click(12 + 632 + 90 + 42, 86 + 422 + 11);
    doc = sampler.document();
    bool sliced = doc->parts[0].zones.size() == 5;
    for (size_t i = 1; sliced && i < 5; ++i)
        sliced = doc->parts[0].zones[i].v[kZLoKey] == 35 + static_cast<int>(i) && doc->parts[0].zones[i].v[kZOneShot] == 1 && std::fabs(doc->parts[0].zones[i].v[kZStart] - (i - 1) * 12000.0) < 300;
    check(sliced, "SLICE AT HITS cuts it into one zone per hit, from C2 up", doc->parts[0].zones.size());

    // MOD: a step dragged up.
    page(3);
    drag(12 + 176 + 8 + 12, 86 + 98 + 56, 12 + 176 + 8 + 12, 86 + 98 + 20);
    check(sampler.document()->parts[0].v[kPSeqValue] > 50, "dragging in the row of steps sets a step", sampler.document()->parts[0].v[kPSeqValue]);

    // A macro's name: a click on it, typing, Enter.
    click(12 + 176 + 398 + 30, 86 + 98 + 62 - 21);
    type("Bright");
    key(0x0D);
    check(sampler.document()->macroNames[0] == "Bright", "a click on a macro's name and typing gives it another");

    // Saving the instrument under a name, starting again, and loading it back.
    const std::string before = sampler.text();
    click(400, 24);                                   // the instrument's name: the list opens (Init, Save as...)
    click(400, 12 + 24 + 2 + 4 + 22 + 11);            // Save as...
    for (int i = 0; i < 20; ++i) key(0x08);
    type("Test kit");
    key(0x0D);
    check(sampler.document()->name == "Test kit" && fs::exists(dir / "presets" / "Test kit.hnlsampler"), "Save as... writes the instrument under its name");
    click(400, 24);
    click(400, 12 + 24 + 2 + 4 + 11);                 // Init
    check(sampler.document()->parts[0].zones.empty() && sampler.document()->name == "Init", "Init starts again with an empty instrument");
    click(400, 24);
    click(400, 12 + 24 + 2 + 4 + 22 + 11);            // the saved one
    wait(sampler);
    doc = sampler.document();
    check(doc->name == "Test kit" && doc->parts[0].zones.size() == 5 && doc->samples.size() == 2 && doc->samples[0].data != nullptr && doc->parts[0].v[kPFilterType] == 2,
          "and the saved instrument loads again, sounds and all", doc->parts[0].zones.size());

    // Every page draws, with help on, without leaving the window.
    click(UI_BASE_WIDTH - 24, 24);
    bool drawn = true;
    for (int p = 0; p < 6; ++p) {
        page(p);
        DISTRHO::MotionEvent move;
        move.pos.x = 300; move.pos.y = 300;
        ui->onMotion(move);
        ui->svg.clear();
        ui->onNanoDisplay();
        drawn = drawn && ui->svg.size() > 5000;
        for (const char* bad : {"Mnan", "Lnan", " nan", "\"nan", "-nan", "Minf", "Linf", " inf ", "\"inf", "-inf"}) drawn = drawn && ui->svg.find(bad) == std::string::npos; // a coordinate that is not a number
    }
    check(drawn, "all six pages draw, with a help tip showing");

    delete ui;
    std::printf("%s\n", failures ? "FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
