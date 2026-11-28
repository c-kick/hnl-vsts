// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// Drives the UI class itself, on the drawing stand-in of tools/ui-preview: clicks and drags as a
// user would, and looks at what that did to the sampler's instrument.
//   ui_test <an empty folder to work in>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include <sys/stat.h>

#include "DistrhoUI.hpp"
#include "../../../common/ui/Skin.hpp"
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
static void page(int p) { click(UI_BASE_WIDTH - 12.0 - (5 - p) * 76.0 + 36.0, 66.0); } // ZONES, SAMPLE, SYNTH, MOD, MIX
static void view(int v) { click(264.0 + v * 84.0 + 40.0, 96.0); }                          // KEYS, VELOCITY, MAP
static double wide(double x) { return 12.0 + (x - 12.0) * (UI_BASE_WIDTH - 24.0) / 976.0; } // SYNTH, MOD and MIX: a place of the 976 wide layout
static double fileRow(int r) { return 86.0 + 50.0 + r * 18.0 + 9.0; }                       // the files beside ZONES and SAMPLE
static std::string drawn() {
    ui->svg.clear();
    ui->onNanoDisplay();
    return ui->svg;
}
static bool shows(const char* text) { return drawn().find(text) != std::string::npos; }
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
    check(ui->svg.find("SAMPLER") != std::string::npos && ui->svg.find("No sounds in this part yet") != std::string::npos && ui->svg.find("Double-click a sound in the list") != std::string::npos,
          "opens on the ZONES page of the plugin's own, empty instrument, and says how to add a sound");
    check(ui->svg.find("a tone.wav") != std::string::npos && ui->svg.find("b hits.wav") != std::string::npos && ui->svg.find("ADD AS ZONE") == std::string::npos,
          "the files stand beside it, and nothing is offered before a file is chosen");
    page(2);

    // A knob: Osc level, dragged down by half its travel; a double click sets it back to its default.
    const double osc = wide(97);
    drag(osc, 138, osc, 38);
    check(std::fabs(sampler.document()->parts[0].v[kPOscLevel] - 50.0) < 0.5, "dragging a knob changes the instrument (osc level)", sampler.document()->parts[0].v[kPOscLevel]);
    twice(osc, 138);
    check(sampler.document()->parts[0].v[kPOscLevel] == 0.0, "a double click sets it back to its default", sampler.document()->parts[0].v[kPOscLevel]);
    drag(osc, 138, osc, 38);

    // A list: the filter type.
    click(200, 239);
    check(shows("Low 24") && shows("Morph"), "a click on a choice opens its list");
    click(200, 250 + 3 + 2 * 18 + 9);
    check(sampler.document()->parts[0].v[kPFilterType] == 2.0, "and a click in the list chooses (Low 24)", sampler.document()->parts[0].v[kPFilterType]);

    // The host's parameters: the volume knob in the header, and a macro.
    drag(UI_BASE_WIDTH - 258, 27, UI_BASE_WIDTH - 258, 77);
    bool sent = false;
    for (const auto& p : ui->sentParameters) sent = sent || (p.first == kVolume && p.second < -10.0f);
    check(sent, "the volume knob goes to the host as a parameter");

    // Another part: the same page shows that part's settings.
    click(12 + 56 + 26, 66);
    drag(osc, 138, osc, 38);
    check(std::fabs(sampler.document()->parts[1].v[kPOscLevel] - 50.0) < 0.5 && std::fabs(sampler.document()->parts[0].v[kPOscLevel] - 50.0) < 0.5, "a part tab makes the pages work on that part");
    click(12 + 26, 66);

    // The files: the folder with the two sounds; a double click on a sound adds it as a zone.
    page(0);
    twice(100, fileRow(1)); // the second row: the first is the folder of saved instruments
    const bool read = wait(sampler);
    std::shared_ptr<const Instrument> doc = sampler.document();
    check(read && doc->parts[0].zones.size() == 1 && doc->samples.size() == 1 && doc->samples[0].data != nullptr && doc->parts[0].zones[0].v[kZRoot] == 57,
          "a double click on a sound adds a zone, with the root key of its file", doc->parts[0].zones.empty() ? -1 : doc->parts[0].zones[0].v[kZRoot]);

    // ZONES, the KEYS view: a row with a bar that covers all keys. Its right end dragged to the middle, then the whole
    // moved up an octave, then the end of its fade line dragged ten keys inward.
    const auto zone0 = [&] { return sampler.document()->parts[0].zones[0]; };
    const double barX = 264 + 310, barW = 976 - 320, keyU = barW / 128.0, velU = barW / 127.0, row0 = 86 + 24 + 18; // where the bars are, and the top of the first row
    check(shows(">KEYS<") && shows(">a tone.wav<") && shows(">1 zone<"), "ZONES opens on the KEYS view: a row for the zone, with the name of its sound");
    drag(barX + barW - 2, row0 + 15, barX + 60.5 * keyU, row0 + 15);
    check(zone0().v[kZHiKey] == 60, "dragging the end of a zone's bar changes its range", zone0().v[kZHiKey]);
    drag(barX + 30.5 * keyU, row0 + 15, barX + 42.5 * keyU, row0 + 15);
    check(zone0().v[kZLoKey] == 12 && zone0().v[kZHiKey] == 72 && zone0().v[kZRoot] == 69, "dragging its middle moves it, root and all", zone0().v[kZLoKey]);
    drag(barX + 73 * keyU, row0 + 4, barX + 63 * keyU, row0 + 4);
    check(zone0().v[kZFadeHiKey] == 10 && zone0().v[kZHiKey] == 72, "dragging the end of the line above the bar sets a fade, and leaves the range", zone0().v[kZFadeHiKey]);
    // Six pixels beside the bar still takes hold of its end, and the mouse shows that before the press.
    DISTRHO::MotionEvent over;
    over.pos.x = barX + 12 * keyU - 5; over.pos.y = row0 + 15;
    ui->onMotion(over);
    const bool endShape = ui->window.cursor == DGL::kMouseCursorLeftRight;
    over.pos.x = barX + 40 * keyU;
    ui->onMotion(over);
    check(endShape && ui->window.cursor == DGL::kMouseCursorHand, "over the end of a bar the mouse becomes a left-right arrow, over its middle a hand", ui->window.cursor);
    drag(barX + 12 * keyU - 5, row0 + 15, barX + 10.5 * keyU, row0 + 15);
    check(zone0().v[kZLoKey] == 10, "the end of a bar can be taken hold of just beside it", zone0().v[kZLoKey]);
    drag(barX + 10.5 * keyU, row0 + 15, barX + 12.5 * keyU, row0 + 15);

    // The loudspeaker in a row plays that zone's sound through the plugin, without a note.
    {
        sampler.prepare(48000.0);
        std::vector<float> block(16 * 256, 0.0f);
        float* outs[16];
        for (int c = 0; c < 16; ++c) outs[c] = block.data() + c * 256;
        double before = 0, heard = 0;
        for (int i = 0; i < 300; ++i) sampler.process(outs, 256); // the file that was listened to before it was added plays out first
        sampler.process(outs, 256);
        for (int i = 0; i < 256; ++i) before = std::max(before, static_cast<double>(std::fabs(outs[0][i])));
        click(264 + 310 - 62 + 10, row0 + 11);
        for (int tries = 0; tries < 300 && heard < 0.01; ++tries) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            sampler.process(outs, 256);
            for (int i = 0; i < 256; ++i) heard = std::max(heard, static_cast<double>(std::fabs(outs[0][i])));
        }
        check(before == 0.0 && heard > 0.01, "the loudspeaker in a zone's row plays its sound", heard);
        sampler.reset();
    }

    // The VELOCITY view: the same row, its bar now the velocities.
    view(1);
    drag(barX + barW - 2, row0 + 15, barX + 64 * velU, row0 + 15);
    check(zone0().v[kZHiVel] == 64 && zone0().v[kZHiKey] == 72, "in the VELOCITY view the end of the bar sets the highest velocity", zone0().v[kZHiVel]);
    drag(barX, row0 + 4, barX + 8 * velU, row0 + 4);
    check(zone0().v[kZFadeLoVel] == 8, "and the line above it the fade over velocities", zone0().v[kZFadeLoVel]);

    // The MAP view: all zones in one picture. A corner changes keys and velocities together.
    view(2);
    const double mapX = 264, mapY = 86 + 24, mapH = 326, mapKey = 976.0 / 128.0;
    const auto velY = [&](double vel) { return mapY + mapH * (1.0 - vel / 127.0); };
    drag(mapX + 73 * mapKey - 2, velY(64) + 2, mapX + 60.5 * mapKey, velY(40));
    check(zone0().v[kZHiKey] == 60 && zone0().v[kZHiVel] == 40 && zone0().v[kZLoKey] == 12, "in the MAP view a corner of a zone changes its keys and its velocities together", zone0().v[kZHiVel]);
    drag(mapX + 61 * mapKey - 2, velY(40) + 2, mapX + 72.5 * mapKey, velY(64));
    // Zones on top of each other: each click on them chooses the next one, so that every zone can be reached.
    click(264 + 10 + 6 * 74 + 8 + 34, 86 + 356 + 80 + 11); // COPY: a second zone in exactly the same place, and the chosen one
    page(1);
    const bool copyChosen = shows(">2 of 2 ");
    page(0);
    click(mapX + 40 * mapKey, velY(30));
    page(1);
    const bool otherChosen = shows(">1 of 2 ");
    page(0);
    click(mapX + 40 * mapKey, velY(30));
    page(1);
    check(copyChosen && otherChosen && shows(">2 of 2 "), "where two zones lie on top of each other, a click chooses the other one, and the next click the first again");
    page(0);
    // FOLLOW: the zone a key plays becomes the chosen one. The engine says which zone the part's newest note plays.
    const int firstId = sampler.document()->parts[0].zones[0].id, copyId = sampler.document()->parts[0].zones[1].id;
    sampler.engine.meters.partZone[0].store(firstId);
    ui->uiIdle();
    page(1);
    const bool stays = shows(">2 of 2 ");
    page(0);
    check(stays && shows(">FOLLOW<"), "without FOLLOW a note leaves the chosen zone alone");
    sampler.engine.meters.partZone[0].store(0);
    ui->uiIdle();
    click(264 + 3 * 84 + 16 + 40, 86 + 10); // FOLLOW, beside the views
    sampler.engine.meters.partZone[0].store(firstId);
    ui->uiIdle();
    page(1);
    const bool followed = shows(">1 of 2 ");
    sampler.engine.meters.partZone[0].store(copyId);
    ui->uiIdle();
    check(followed && shows(">2 of 2 "), "with FOLLOW on, the zone the part plays becomes the chosen one, on SAMPLE as well");
    sampler.engine.meters.partZone[1].store(firstId); // another part plays: not this part's business
    sampler.engine.meters.partZone[0].store(0);
    ui->uiIdle();
    check(shows(">2 of 2 "), "and stays chosen when the note ends");
    page(0);
    click(264 + 3 * 84 + 16 + 40, 86 + 10); // off again
    sampler.engine.meters.partZone[0].store(firstId);
    ui->uiIdle();
    page(1);
    check(shows(">2 of 2 "), "switched off, it follows no more");
    sampler.engine.meters.partZone[0].store(0);
    sampler.engine.meters.partZone[1].store(0);
    page(0);
    click(264 + 10 + 6 * 74 + 8 + 74 + 34, 86 + 356 + 80 + 11); // DELETE
    check(sampler.document()->parts[0].zones.size() == 1, "DELETE removes the chosen zone", sampler.document()->parts[0].zones.size());
    // Fades in the MAP view: the chosen zone has a handle half way along each side. The zone covers C0 to C5, velocities 1 to 64.
    const double loKey = zone0().v[kZLoKey], hiKey = zone0().v[kZHiKey], hiVel = zone0().v[kZHiVel];
    sampler.edit([](Instrument& doc) { Zone& z = doc.parts[0].zones[0]; z.v[kZFadeLoKey] = z.v[kZFadeHiKey] = z.v[kZFadeLoVel] = z.v[kZFadeHiVel] = 0; });
    ui->uiIdle();
    drag(mapX + loKey * mapKey, velY(hiVel / 2), mapX + (loKey + 8) * mapKey, velY(hiVel / 2));
    check(zone0().v[kZFadeLoKey] == 8 && zone0().v[kZLoKey] == loKey && zone0().v[kZHiKey] == hiKey, "in the MAP view the handle on a zone's side sets its fade over the keys, and leaves its range", zone0().v[kZFadeLoKey]);
    drag(mapX + (hiKey + 1) * mapKey, velY(hiVel / 2), mapX + (hiKey + 1 - 5) * mapKey, velY(hiVel / 2));
    check(zone0().v[kZFadeHiKey] == 5 && zone0().v[kZHiKey] == hiKey, "also at its high end", zone0().v[kZFadeHiKey]);
    const double midX = mapX + (loKey + hiKey + 1) / 2 * mapKey;
    drag(midX, velY(hiVel) + 2, midX, velY(hiVel - 10)); // just inside the zone: its top edge is not a whole pixel
    check(zone0().v[kZFadeHiVel] == 10 && zone0().v[kZHiVel] == hiVel, "the handle on its top sets the fade over the velocities", zone0().v[kZFadeHiVel]);
    drag(midX, velY(0), midX, velY(12));
    check(zone0().v[kZFadeLoVel] == 12 && zone0().v[kZLoVel] == 1, "and the one on its bottom the fade at the soft end", zone0().v[kZFadeLoVel]);
    drag(mapX + loKey * mapKey + 2, velY(hiVel / 2) + 30, mapX + (loKey + 2) * mapKey + 2, velY(hiVel / 2) + 30);
    check(zone0().v[kZLoKey] == loKey + 2 && zone0().v[kZFadeLoKey] == 8, "beside the handle the side itself is still what changes the range", zone0().v[kZLoKey]);
    drag(mapX + (loKey + 2) * mapKey + 2, velY(hiVel / 2) + 30, mapX + loKey * mapKey + 2, velY(hiVel / 2) + 30);
    // Fade curve: Power as it always was, or Linear.
    check(zone0().v[kZFadeCurve] == 0, "a zone's fades keep the power unless told otherwise", zone0().v[kZFadeCurve]);
    click(264 + 10 + 5 * 74 + 30, 86 + 356 + 82 + 8);
    const bool offered = shows(">Power<") && shows(">Linear<");
    click(264 + 10 + 5 * 74 + 10, 86 + 356 + 82 + 20 + 3 + 18 + 9);
    check(offered && zone0().v[kZFadeCurve] == 1, "Fade curve offers Power and Linear", zone0().v[kZFadeCurve]);
    sampler.edit([](Instrument& doc) { Zone& z = doc.parts[0].zones[0]; z.v[kZFadeLoKey] = z.v[kZFadeHiKey] = z.v[kZFadeLoVel] = z.v[kZFadeHiVel] = z.v[kZFadeCurve] = 0; });
    ui->uiIdle();
    view(0);

    // SAMPLE: the end marker dragged to the middle of the sound; a loop found by the tool.
    page(1);
    const double waveX = 264, waveW = 976, toolX = 264 + 632, soundY = 86 + 350;
    drag(waveX + waveW - 3, 200, waveX + waveW / 2, 200);
    check(std::fabs(zone0().v[kZEnd] - 24000) < 100, "dragging the end marker sets the zone's end", zone0().v[kZEnd]);
    click(toolX + 53, soundY + 30 + 11);
    doc = sampler.document();
    const double span = doc->parts[0].zones[0].v[kZLoopEnd] - doc->parts[0].zones[0].v[kZLoopStart], period = 48000.0 / 220.0;
    check(doc->parts[0].zones[0].v[kZLoop] == 1 && span > 2000 && std::fabs(span / period - std::round(span / period)) < 0.05, "FIND LOOP sets a loop of whole periods",
          span / period);

    // The second sound on the next key, then cut at its hits. The files are beside this page too.
    click(100, fileRow(2));
    click(18 + 113 + 6 + 56, 86 + 372 + 12); // ON NEXT KEY
    wait(sampler);
    doc = sampler.document();
    check(doc->parts[0].zones.size() == 2 && doc->parts[0].zones[1].v[kZLoKey] == 73 && doc->parts[0].zones[1].v[kZHiKey] == 73 && doc->parts[0].zones[1].v[kZRoot] == 73,
          "ON NEXT KEY puts a sound on the key after the highest zone", doc->parts[0].zones.size());
    click(toolX + 112 + 53, soundY + 56 + 11);
    doc = sampler.document();
    bool sliced = doc->parts[0].zones.size() == 5;
    for (size_t i = 1; sliced && i < 5; ++i)
        sliced = doc->parts[0].zones[i].v[kZLoKey] == 35 + static_cast<int>(i) && doc->parts[0].zones[i].v[kZOneShot] == 1 && std::fabs(doc->parts[0].zones[i].v[kZStart] - (i - 1) * 12000.0) < 300;
    check(sliced, "SLICE AT HITS cuts it into one zone per hit, from C2 up", doc->parts[0].zones.size());
    // The slices are all on the page: the first is the chosen zone, the others are marked with their keys, and a click in one chooses it.
    std::string page1 = drawn();
    check(page1.find(">2 of 5 ") != std::string::npos && page1.find("b hits") != std::string::npos && page1.find(">C#2<") != std::string::npos && page1.find(">D2<") != std::string::npos &&
          page1.find(">D#2<") != std::string::npos, "after slicing, SAMPLE shows the first slice and marks the others with their keys");
    click(waveX + waveW * 2.5 / 4.0, 300); // in the third of the four
    check(shows(">4 of 5 "), "a click in another slice chooses that zone");
    // Zoomed in on the end of this sound, then on to a zone with another sound: that one is shown whole, not through the old zoom.
    DISTRHO::ScrollEvent in;
    in.pos.x = waveX + waveW - 40; in.pos.y = 300;
    in.delta.y = 1;
    for (int i = 0; i < 6; ++i) ui->onScroll(in);
    const bool zoomed = !shows(">0.00 s<");
    for (int i = 0; i < 3; ++i) click(264 + 62 + 10, soundY + 11); // the arrow to the zone before: three back is the tone
    page1 = drawn();
    check(zoomed && page1.find(">1 of 5 ") != std::string::npos && page1.find(">0.00 s<") != std::string::npos && page1.find(">E<") != std::string::npos,
          "a zone with another sound is shown whole, whatever the view of the last one was");
    // Another sound for a zone that is there: its keys stay.
    click(100, fileRow(2));
    click(18 + 56, 86 + 372 + 28 + 12); // REPLACE SOUND
    wait(sampler);
    doc = sampler.document();
    const Sample* now = doc->findSample(doc->parts[0].zones[0].sample);
    check(now != nullptr && now->path.find("b hits.wav") != std::string::npos && doc->parts[0].zones[0].v[kZLoKey] == 12 && doc->parts[0].zones[0].v[kZHiKey] == 72 &&
          doc->parts[0].zones[0].v[kZEnd] == 0 && doc->parts[0].zones.size() == 5 && doc->samples.size() == 1,
          "REPLACE SOUND gives the chosen zone the chosen file's sound and leaves its keys", doc->samples.size());
    click(100, fileRow(1));
    click(18 + 56, 86 + 372 + 28 + 12); // and the tone back again
    wait(sampler);

    // MOD: a step dragged up.
    page(3);
    drag(wide(12 + 176 + 8 + 12), 86 + 98 + 56, wide(12 + 176 + 8 + 12), 86 + 98 + 20);
    check(sampler.document()->parts[0].v[kPSeqValue] > 50, "dragging in the row of steps sets a step", sampler.document()->parts[0].v[kPSeqValue]);

    // A macro's name: a click on it, typing, Enter.
    click(wide(12 + 176 + 398 + 30), 86 + 98 + 62 - 21);
    type("Bright");
    key(0x0D);
    check(sampler.document()->macroNames[0] == "Bright", "a click on a macro's name and typing gives it another");

    // Saving the instrument under a name, starting again, and loading it back.
    const std::string before = sampler.text();
    const float kRow0 = 12 + 24 + 2 + 4 + 11;         // the middle of the list's first row; the rows are 22 apart
    click(400, 24);                                   // the instrument's name: the list opens (Init, the three that came with it, Save as...)
    click(400, kRow0 + 4 * 22);                       // Save as...
    for (int i = 0; i < 20; ++i) key(0x08);
    type("Test kit");
    key(0x0D);
    check(sampler.document()->name == "Test kit" && fs::exists(dir / "presets" / "Test kit.hnlsampler"), "Save as... writes the instrument under its name");
    click(400, 24);
    click(400, kRow0);                                // Init
    check(sampler.document()->parts[0].zones.empty() && sampler.document()->name == "Init", "Init starts again with an empty instrument");
    click(400, 24);
    click(400, kRow0 + 4 * 22);                       // the saved one, below the three
    wait(sampler);
    doc = sampler.document();
    check(doc->name == "Test kit" && doc->parts[0].zones.size() == 5 && doc->samples.size() == 2 && doc->samples[0].data != nullptr && doc->parts[0].v[kPFilterType] == 2,
          "and the saved instrument loads again, sounds and all", doc->parts[0].zones.size());

    // The instruments that came with the plugin: a click loads one, sounds and all, and it cannot be deleted.
    click(400, 24);
    click(400, kRow0 + 2 * 22);                       // Strings
    wait(sampler);
    doc = sampler.document();
    int unread = 0;
    for (const Sample& sm : doc->samples) unread += sm.data == nullptr;
    check(doc->name == "Strings" && doc->parts[0].zones.size() == 16 && unread == 0, "a click on Strings in the list loads it, with its sounds", doc->parts[0].zones.size());
    click(400, 24);
    click(298 + 170 - 15, kRow0 + 1 * 22);            // where a saved instrument has its cross: Jazz Kit has none, so this loads it
    wait(sampler);
    doc = sampler.document();
    check(doc->name == "Jazz Kit" && !doc->parts[0].zones.empty() && fs::exists(dir / "presets" / "Test kit.hnlsampler"), "an instrument that came with the plugin has no cross: a click there loads it");
    // A song keeps one of them by the names of its sounds, and gets it back from the plugin itself.
    const std::string kit = sampler.text();
    sampler.edit([](Instrument& d) { Sampler::initDocument(d); });
    check(kit.find("factory:kit/snare_1a.flac") != std::string::npos && sampler.setText(kit), "its text names the sounds as factory:..., and loads again");
    wait(sampler);
    unread = 0;
    for (const Sample& sm : sampler.document()->samples) unread += sm.data == nullptr;
    check(unread == 0, "and every sound is there again", unread);
    // Back to the saved instrument, which the checks below work on.
    click(400, 24);
    click(400, kRow0 + 4 * 22);
    wait(sampler);
    doc = sampler.document();

    // The size of the window: from the cog's list, by dragging its edge, and kept for the next time it opens.
    {
        hnl::ui::SkinMenu place; // the same place as the UI's own, for where to click
        place.setButton(UI_BASE_WIDTH - 24, 24);
        place.offerSizes(100);
        const auto middle = [&](int i, double zoom, bool y) { const hnl::ui::Box b = place.sizeBox(i); return zoom * (y ? b.y + b.h * 0.5 : b.x + b.w * 0.5); };
        check(ui->getWidth() == 1252 && ui->getHeight() == 640 && ui->leastWidth == 939 && ui->leastHeight == 480 && ui->keepsShape,
              "the window opens at its design size, can be dragged down to three quarters of it, and keeps its shape", ui->getWidth());
        click(UI_BASE_WIDTH - 24, 24);
        ui->svg.clear();
        ui->onNanoDisplay();
        check(ui->svg.find(">SIZE<") != std::string::npos && ui->svg.find(">150<") != std::string::npos, "the cog's list offers the sizes under the looks");
        click(middle(3, 1.0, false), middle(3, 1.0, true)); // 150
        check(ui->getWidth() == 1878 && ui->getHeight() == 960 && hnl::ui::storedSize("sampler") == 150, "a size from the list makes the window that large at once, and is kept", ui->getWidth());
        click(1.5 * (UI_BASE_WIDTH - 24), 1.5 * 24); // the cog again, where it is drawn now: the list closes
        click(1.5 * (UI_BASE_WIDTH - 12.0 - 4 * 76.0 + 36.0), 1.5 * 66.0);
        ui->svg.clear();
        ui->onNanoDisplay();
        check(ui->svg.find("FIND LOOP") != std::string::npos && ui->svg.find(">SIZE<") == std::string::npos, "in the larger window a click lands on what is drawn there (the SAMPLE tab)");
        ui->setSize(1565, 800); // the host, when the edge of the window is dragged
        ui->uiIdle();
        check(hnl::ui::storedSize("sampler") == 150, "while the window is being dragged nothing is written", hnl::ui::storedSize("sampler"));
        for (int i = 0; i <= 2 * hnl::ui::SkinMenu::kPollEvery; ++i) ui->uiIdle();
        check(hnl::ui::storedSize("sampler") == 125, "a second after the dragging stops, that size is kept", hnl::ui::storedSize("sampler"));
        DISTRHO::UI* const again = DISTRHO::createUI();
        check(again->getWidth() == 1565 && again->getHeight() == 800, "the window opens again at the size it was left at", again->getWidth());
        delete again;
        click(1.25 * (UI_BASE_WIDTH - 24), 1.25 * 24);
        click(middle(1, 1.25, false), middle(1, 1.25, true)); // 100
        click(UI_BASE_WIDTH - 24, 24);
        check(ui->getWidth() == 1252 && hnl::ui::storedSize("sampler") == 100, "and 100 in the list brings back the design size", ui->getWidth());
        hnl::settings::set("size.sampler", "9000");
        const bool high = hnl::ui::storedSize("sampler") == 100;
        hnl::settings::set("size.sampler", "12x");
        check(high && hnl::ui::storedSize("sampler") == 100, "a size in the settings that is no size reads as 100");
        hnl::settings::set("size.sampler", "100");
    }

    // Every page draws, with help on, without leaving the window.
    click(UI_BASE_WIDTH - 52, 24);
    bool allDrawn = true;
    for (int p = 0; p < 5; ++p) {
        page(p);
        DISTRHO::MotionEvent move;
        move.pos.x = 300; move.pos.y = 300;
        ui->onMotion(move);
        ui->svg.clear();
        ui->onNanoDisplay();
        allDrawn = allDrawn && ui->svg.size() > 5000;
        for (const char* bad : {"Mnan", "Lnan", " nan", "\"nan", "-nan", "Minf", "Linf", " inf ", "\"inf", "-inf"}) allDrawn = allDrawn && ui->svg.find(bad) == std::string::npos; // a coordinate that is not a number
    }
    check(allDrawn, "all five pages draw, with a help tip showing");

    // A host that sends values that are no values: the UI cleans them as the plugin does.
    page(0);
    for (uint32_t i = 0; i < kParamCount; ++i) ui->parameterChanged(i, std::nanf(""));
    ui->parameterChanged(kMeterVoices, -30.0f);
    ui->parameterChanged(kVolume, 1e30f);
    ui->svg.clear();
    ui->onNanoDisplay();
    bool clean = ui->svg.find("0 of 64") != std::string::npos && ui->svg.find("-30 of 64") == std::string::npos;
    for (const char* bad : {"Mnan", "Lnan", " nan", "\"nan", "-nan", "Minf", "Linf", "\"inf", "-inf"}) clean = clean && ui->svg.find(bad) == std::string::npos;
    check(clean, "values from the host that are not numbers, or out of range, are drawn as ones that are");
    delete ui;

    // FILES in a folder with more sounds than the page once read: all of them, in order, and no pipe among them.
    const fs::path many = dir / "0 many";
    fs::create_directories(many);
    for (int i = 0; i < 5200; ++i) {
        char name[40];
        std::snprintf(name, sizeof(name), "s %04d.wav", i);
        std::fclose(std::fopen((many / name).c_str(), "wb"));
    }
    writeWav(many / "zz wanted.wav", tone, 16, error);
    const bool piped = ::mkfifo((many / "a pipe.wav").c_str(), 0600) == 0;
    const std::string odd = (many / "zy new\nline.wav").string();
    writeWav(odd, tone, 16, error);
    ui = DISTRHO::createUI();
    twice(100, fileRow(0)); // the first row: the folder just made
    ui->svg.clear();
    ui->onNanoDisplay();
    const bool first = ui->svg.find("s 0000.wav") != std::string::npos && ui->svg.find("s 0014.wav") != std::string::npos && ui->svg.find("pipe") == std::string::npos;
    DISTRHO::ScrollEvent down;
    down.pos.x = 100; down.pos.y = 300;
    down.delta.y = -1;
    std::string last;
    for (int round = 0; round < 200; ++round) {
        for (int k = 0; k < 200; ++k) ui->onScroll(down);
        ui->svg.clear();
        ui->onNanoDisplay();
        if (ui->svg == last) break;
        last = ui->svg;
    }
    check(piped && first && last.find("zz wanted.wav") != std::string::npos && last.find("s 5199.wav") != std::string::npos && last.find("Too many") == std::string::npos,
          "the files show a folder of 5200 sounds whole and in order, without the pipe in it");
    // The last but one is a sound with a line end in its name: it cannot be saved, so it is not added, and the page says so.
    const size_t zonesBefore = sampler.document()->parts[0].zones.size();
    twice(100, fileRow(3)); // the list ends five rows from the top
    wait(sampler);
    ui->svg.clear();
    ui->onNanoDisplay();
    check(sampler.document()->parts[0].zones.size() == zonesBefore && ui->svg.find("Not added") != std::string::npos, "a sound with a line end in its name is not added, and the page says why");
    delete ui;

    // The window is closed and opened again: FILES is in the folder it was left in, though no sound was taken from it.
    ui = DISTRHO::createUI();
    ui->svg.clear();
    ui->onNanoDisplay();
    check(ui->svg.find("s 0000.wav") != std::string::npos && ui->svg.find("0 many") != std::string::npos, "the files open again in the folder they were left in");
    delete ui;
    // And the folder is on disk, for the next run of the host.
    std::string kept;
    if (std::FILE* f = std::fopen((fs::path(std::getenv("HNL_PRESET_DIR")) / "last-folder.txt").c_str(), "rb")) {
        char text[2000];
        kept.assign(text, std::fread(text, 1, sizeof(text), f));
        std::fclose(f);
    }
    std::error_code same;
    check(!kept.empty() && fs::equivalent(fs::path(kept), many, same), "the folder is kept in a file for the next run of the host");
    std::printf("%s\n", failures ? "FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
