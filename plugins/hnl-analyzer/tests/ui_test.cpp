// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// The window itself, clicked and typed in as a host would have it, on the drawing stand-in of
// tools/ui-preview: the mode keys, the capture keys, the name, the reports, opening a file, the
// targets, and the references.  ui_test <a folder it may fill and empty>
// HNL_PRESET_DIR, HNL_SETTINGS_DIR and HNL_REFERENCE_DIR point into that folder (tests/run.sh).
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

#include "DistrhoUI.hpp"
#include "../Params.hpp"
#include "../../../common/UserSettings.hpp"
#include "../../../common/ui/Skin.hpp"
#include "Demo.hpp"

using namespace hnl::analyzer;
namespace fs = std::filesystem;

static int failures = 0;
static std::string lastDrawn, lastState;
static void check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (ok) return;
    ++failures;
    if (std::getenv("HNL_UI_TEST_SHOW") == nullptr) return; // what the window showed when it went wrong
    std::printf("      state sent: %s\n      shown:", lastState.c_str());
    for (size_t at = 0; (at = lastDrawn.find("\">", at)) != std::string::npos;) {
        const size_t end = lastDrawn.find("</text>", at);
        if (end == std::string::npos) break;
        if (lastDrawn.compare(at + 2, 1, "<") != 0 && lastDrawn.compare(at + 2, 1, "\n") != 0) std::printf(" [%s]", lastDrawn.substr(at + 2, end - at - 2).c_str());
        at = end;
    }
    std::printf("\n");
}

// Where things are (AnalyzerUI.cpp).
struct Spot { float x, y; };
static const Spot kContinuousKey = {400, 26}, kTrackKey = {300, 26};
static const Spot kAutoKey = {812, 93}, kStartKey = {992, 93}, kResetKey = {1118, 93}, kNameBox = {880, 153};
static const Spot kCopy = {1038, 153}, kTxt = {1078, 153}, kMd = {1118, 153}, kJson = {1158, 153};
static const Spot kWindow300 = {72 + 230 * 4.5f / 6, 666}, kSquare = {270, 692};
static const Spot kTargetKnob = {664, 476}, kPlrKnob = {848, 476};
static const Spot kRefPrev = {929, 455}, kRefNext = {1167, 455}, kMatchNone = {960, 482}, kMatchLoud = {1048, 482}, kMatchTp = {1134, 482};
static const Spot kSave = {950, 689}, kAddFile = {1015, 689}, kExport = {1080, 689}, kDelete = {1145, 689};
static const Spot kHelpButton = {UI_BASE_WIDTH - 52, 26};

static void press(DISTRHO::UI* ui, Spot at, bool down = true, unsigned time = 0, unsigned mod = 0) {
    DISTRHO::MouseEvent ev;
    ev.pos.x = at.x;
    ev.pos.y = at.y;
    ev.press = down;
    ev.time = time;
    ev.mod = mod;
    ui->onMouse(ev);
}
static unsigned now = 1000;
static void click(DISTRHO::UI* ui, Spot at) {
    now += 1000; // never a double click by accident
    press(ui, at, true, now);
    press(ui, at, false, now);
}
static void type(DISTRHO::UI* ui, const char* text) {
    for (const char* c = text; *c; ++c) {
        DISTRHO::CharacterInputEvent ev;
        ev.character = static_cast<unsigned char>(*c);
        ev.string[0] = *c;
        ui->onCharacterInput(ev);
    }
}
static void key(DISTRHO::UI* ui, unsigned code) {
    DISTRHO::KeyboardEvent ev;
    ev.key = code;
    ui->onKeyboard(ev);
    ev.press = false;
    ui->onKeyboard(ev);
}
static std::string draw(DISTRHO::UI* ui) {
    ui->uiIdle();
    ui->svg.clear();
    ui->onNanoDisplay();
    lastDrawn = ui->svg;
    lastState = ui->sentState;
    return ui->svg;
}
static bool shows(DISTRHO::UI* ui, const std::string& text) { return draw(ui).find(">" + text + "<") != std::string::npos; }
static bool showsPart(DISTRHO::UI* ui, const std::string& text) { return draw(ui).find(text) != std::string::npos; }
static bool sent(DISTRHO::UI* ui, uint32_t id, float value) {
    for (const auto& p : ui->sentParameters)
        if (p.first == id && std::fabs(p.second - value) < 1e-4f) return true;
    return false;
}
static std::string slurp(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}
static std::string fixed(double v, int decimals) {
    char s[32];
    std::snprintf(s, sizeof(s), "%.*f", decimals, v);
    return s;
}
static void waitForFile(Engine& engine) {
    for (int i = 0; i < 500 && engine.fileBusy(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("FAIL  no folder to work in\n");
        return 1;
    }
    const fs::path dir = fs::absolute(argv[1]);
    fs::create_directories(dir);

    // A window without a plugin behind it (a host that gives none) still draws.
    {
        DISTRHO::UI* lone = DISTRHO::createUI();
        check(shows(lone, "NO PLUGIN") && shows(lone, "ANALYZER") && shows(lone, "MASTERING ADVISOR"), "a window that finds no plugin draws all the same");
        click(lone, kResetKey);
        click(lone, kStartKey);
        click(lone, kCopy);
        click(lone, kSave);
        check(shows(lone, "Nothing measured to save"), "and its keys do no harm");
        delete lone;
    }

    static Engine engine;
    static int token;
    Engine::of(&token, &engine);
    DISTRHO::UI::instancePointer() = &token;
    engine.prepare(44100.0);
    DISTRHO::UI* ui = DISTRHO::createUI();

    check(shows(ui, "WAITING FOR THE TRANSPORT") && shows(ui, "--") && shows(ui, "NO MEASUREMENT") && shows(ui, "Streaming -14"), "a new instance: waiting, no figures, the first set of targets");

    // The mode keys.
    click(ui, kContinuousKey);
    check(sent(ui, kMode, 1.0f) && shows(ui, "WAITING FOR SOUND"), "CONTINUOUS sets the mode");
    check(!shows(ui, "FILE") && shows(ui, "+ FILE..."), "a file is not a mode: it is added in the reference panel");
    click(ui, kTrackKey);
    click(ui, kAutoKey);
    check(sent(ui, kAuto, 0.0f) && shows(ui, "PRESS START TO CAPTURE"), "FOLLOW TRANSPORT off: captures are by hand");

    // A capture by hand: Start, sound, Stop.
    engine.setMode(kTrackMode);
    engine.setAutomatic(false);
    std::vector<float> l, r;
    demo::song(44100.0, 8.0, l, r);
    click(ui, kStartKey);
    engine.process(l.data(), r.data(), 44100 * 4, false, 0, true);
    check(showsPart(ui, ">CAPTURING  0:04.0<") && shows(ui, "STOP") && showsPart(ui, " LUFS<"), "START begins a capture, which shows as it runs");
    click(ui, kStartKey);
    engine.process(l.data() + 44100 * 4, r.data() + 44100 * 4, 4410, false, 0, true);
    std::unique_ptr<Engine::Shown> shown(new Engine::Shown());
    uint64_t seen = 0;
    engine.fetch(seen, *shown);
    const Result first = shown->track.r;
    check(showsPart(ui, ">CAPTURED  0:04.0<") && shows(ui, "START") && shows(ui, fixed(first.integrated, 2)) && shows(ui, fixed(truePeakDb(first), 2)) && shows(ui, fixed(plr(first), 2)),
          "the same key stops it: integrated loudness, true peak and PLR are shown");
    check(shows(ui, fixed(totalRmsDb(first, 0, true), 2)) && shows(ui, fixed(maxRmsDb(first, 2, 0, true), 2)) && shows(ui, fixed(crestDb(first, 1), 2)), "and the amplitude statistics");

    // How the RMS figures are shown.
    click(ui, kWindow300);
    check(sent(ui, kRmsWindow, 4.0f) && shows(ui, fixed(maxRmsDb(first, 4, 0, true), 2)), "the window keys choose the length the RMS figures are of");
    click(ui, kSquare);
    check(sent(ui, kRmsRef, 1.0f) && shows(ui, fixed(totalRmsDb(first, 0, false), 2)) && !shows(ui, fixed(totalRmsDb(first, 0, true), 2)), "SQUARE shows them 3.01 dB lower");
    ui->parameterChanged(kRmsRef, 0.0f);
    ui->parameterChanged(kRmsWindow, 2.0f);

    // The name.
    click(ui, kNameBox);
    check(ui->window.focused && showsPart(ui, ">|<"), "a click on the name starts typing, and asks for the keyboard");
    type(ui, "Deja  vu?");
    key(ui, 0x08);
    key(ui, 0x0D);
    engine.fetch(seen, *shown);
    check(shown->track.name == "Deja  vu" && shows(ui, "Deja  vu"), "typed, corrected and kept with Enter");
    click(ui, kNameBox);
    type(ui, " (x)");
    key(ui, 0x1B);
    engine.fetch(seen, *shown);
    check(shown->track.name == "Deja  vu", "Escape leaves the name as it was");
    click(ui, kNameBox);
    type(ui, " 2");
    click(ui, kResetKey); // a click elsewhere takes the name, and then does what it is for
    engine.fetch(seen, *shown);
    check(shown->track.empty() && shows(ui, "PRESS START TO CAPTURE") && !shows(ui, fixed(first.integrated, 2)), "RESET forgets what is shown");

    // Reports.
    Analysis demoTrack = demo::measured("Demo: \"track\"", 1.0, 20.0);
    engine.setCompleted(demoTrack);
    draw(ui);
    click(ui, kCopy);
    check(ui->window.clipboard.find("Integrated") != std::string::npos && ui->window.clipboard.find("Total RMS amplitude") != std::string::npos &&
              ui->window.clipboard.find("Mastering advisor") != std::string::npos && shows(ui, "Copied to the clipboard"),
          "COPY puts the report on the clipboard as text");
    const int before = ui->browsersOpened;
    click(ui, kTxt);
    check(ui->browsersOpened == before + 1 && ui->browserSaving && ui->browserName == "Demo track.txt", "TXT asks where to save, with the track's name made fit for a file");
    ui->uiFileBrowserSelected((dir / "report").string().c_str());
    check(slurp(dir / "report.txt").find("Loudness range (LRA)") != std::string::npos && shows(ui, "Written: report.txt"), "and writes the report there, with .txt behind the name");
    click(ui, kMd);
    ui->uiFileBrowserSelected((dir / "report.md").string().c_str());
    check(ui->browserName == "Demo track.md" && slurp(dir / "report.md").find("## Mastering advisor") != std::string::npos, "MD writes it as Markdown");
    click(ui, kJson);
    ui->uiFileBrowserSelected((dir / "report.json").string().c_str());
    Analysis fromReport;
    check(fromJson(slurp(dir / "report.json"), fromReport) && fromReport.r.frames == demoTrack.r.frames && slurp(dir / "report.json").find("\"advisor\"") != std::string::npos,
          "JSON writes it as data, the advisor included");
    click(ui, kTxt);
    ui->uiFileBrowserSelected(nullptr);
    click(ui, kTxt);
    ui->uiFileBrowserSelected((dir / "no such folder" / "x.txt").string().c_str());
    check(showsPart(ui, ">Could not write x.txt<"), "a dialog closed without a name writes nothing; a place that cannot be written says so");

    // The targets: the advisor follows the knobs.
    check(shows(ui, "GAIN ADJUSTMENT") || shows(ui, "DYNAMIC PROCESSING") || shows(ui, "TARGET ACHIEVED"), "the advisor has a verdict on the track");
    ui->sentParameters.clear();
    press(ui, kTargetKnob, true, 50000);
    DISTRHO::MotionEvent move;
    move.pos.x = kTargetKnob.x;
    move.pos.y = kTargetKnob.y - 40; // a fifth of the way up
    ui->onMotion(move);
    press(ui, {kTargetKnob.x, kTargetKnob.y - 40}, false, 50000);
    check(sent(ui, kTargetLufs, -9.0f) && shows(ui, "-9.0 LUFS"), "dragging the target knob sets the loudness target, in tenths");
    check(shows(ui, fixed(-9.0 - demoTrack.r.integrated, 2).insert(0, -9.0 - demoTrack.r.integrated >= 0 ? "+" : "") + " dB"), "and the required gain follows");
    press(ui, kTargetKnob, true, 60000);
    press(ui, kTargetKnob, false, 60000);
    press(ui, kTargetKnob, true, 60100);
    press(ui, kTargetKnob, false, 60100);
    check(sent(ui, kTargetLufs, -14.0f) && shows(ui, "-14.0 LUFS"), "a double click returns it to its default");
    DISTRHO::ScrollEvent wheel;
    wheel.pos.x = kPlrKnob.x;
    wheel.pos.y = kPlrKnob.y;
    wheel.delta.y = 1;
    ui->onScroll(wheel);
    check(sent(ui, kPrefPlr, 13.5f), "the wheel moves a target by half a decibel");
    ui->parameterChanged(kTargetLufs, -12.5f);
    ui->parameterChanged(kMaxTruePeak, -2.0f);
    ui->parameterChanged(kPrefPlr, 10.5f);
    const Advice advice = adviceFor(demoTrack, {-12.5, -2.0, 10.5});
    check(shows(ui, adviceTitle(advice)) && shows(ui, fixed(advice.peakReduction, 2) + " dB"), "targets set by the host (a preset, automation) are followed too");

    // References.
    check(shows(ui, "No reference"), "no reference at first");
    click(ui, kSave);
    check(showsPart(ui, ">Saved as reference: Demo track<") && fs::exists(References::folder() / "Demo track.json"), "SAVE keeps the analysis as a reference, a file in the references folder");
    Analysis other = demo::measured("Other master", 0.7, 16.0, 2.5, 0.4);
    engine.setCompleted(other); // a second track; the first is now also the capture before it
    click(ui, kRefNext);
    check(ui->sentState == "reference=0|*" && showsPart(ui, ">Previous: Demo") && shows(ui, fixed(demoTrack.r.integrated, 2)), "the arrow steps to the capture before this one, and the host is told");
    click(ui, kRefNext);
    check(ui->sentState == "reference=0|Demo track" && shows(ui, "Demo track") && shows(ui, "REFERENCE") && shows(ui, fixed(other.r.integrated - demoTrack.r.integrated, 2)),
          "then to the saved reference: its figures, and the difference");
    click(ui, kMatchLoud);
    const double gain = other.r.integrated - demoTrack.r.integrated;
    check(ui->sentState == "reference=1|Demo track" && shows(ui, std::string("REF ") + (gain >= 0 ? "+" : "") + fixed(gain, 1)) && shows(ui, "+0.00") &&
              shows(ui, fixed(truePeakDb(demoTrack.r) + gain, 2)),
          "LOUDNESS moves the reference to the loudness of the track: no difference there, and the peaks compared at equal loudness");
    click(ui, kMatchTp);
    check(ui->sentState == "reference=2|Demo track" && shows(ui, fixed(demoTrack.r.integrated + truePeakDb(other.r) - truePeakDb(demoTrack.r), 2)), "TRUE PEAK moves it to the true peak of the track");
    click(ui, kCopy);
    check(ui->window.clipboard.find("Reference: Demo track, matched in true peak") != std::string::npos && ui->window.clipboard.find("Difference") != std::string::npos,
          "the report holds the comparison");

    // Export, import.
    click(ui, kExport);
    check(ui->browserSaving && ui->browserName == "Demo track.json", "EXPORT asks where to write the reference");
    ui->uiFileBrowserSelected((dir / "exported.json").string().c_str());
    Analysis exported;
    check(References::read(dir / "exported.json", exported) && exported.name == "Demo track" && exported.r.frames == demoTrack.r.frames, "and writes it as JSON that reads back whole");
    { std::ofstream(dir / "typed.json") << demo::referenceJson(); }
    click(ui, kMatchNone);
    click(ui, kAddFile);
    check(!ui->browserSaving, "+ FILE asks for a file");
    ui->uiFileBrowserSelected((dir / "typed.json").string().c_str());
    check(showsPart(ui, "toute allure<") && shows(ui, "-12.47") && ui->sentState.find("toute allure") != std::string::npos && fs::exists(References::folder() / References::path("\xC3\x80 toute allure.json")),
          "a reference typed by hand is imported, chosen and shown");
    { std::ofstream(dir / "junk.json") << "{}"; }
    click(ui, kAddFile);
    ui->uiFileBrowserSelected((dir / "junk.json").string().c_str());
    check(shows(ui, "Not a reference this plugin can read") && showsPart(ui, "toute allure<"), "a file that is no reference is refused, and the choice stays");

    // What the song remembers.
    ui->stateChanged("reference", "1|Demo track");
    check(shows(ui, "Demo track") && shows(ui, "+0.00"), "a song opens with its reference and how it was matched");
    ui->stateChanged("reference", "1|Gone since");
    check(shows(ui, "No reference"), "a reference that is no longer there: none");
    ui->stateChanged("reference", "nonsense");
    check(shows(ui, "No reference"), "nonsense: none");

    // Delete asks once.
    ui->stateChanged("reference", "0|Demo track");
    click(ui, kDelete);
    check(shows(ui, "SURE?") && fs::exists(References::folder() / "Demo track.json"), "DELETE asks first");
    click(ui, kCopy);
    check(!shows(ui, "SURE?") && fs::exists(References::folder() / "Demo track.json"), "a click elsewhere takes the question back");
    click(ui, kDelete);
    click(ui, kDelete);
    check(!fs::exists(References::folder() / "Demo track.json") && shows(ui, "No reference") && ui->sentState == "reference=0|", "a second click deletes it");
    click(ui, kRefPrev);
    check(showsPart(ui, "toute allure<"), "the left arrow goes round the other way, to the last one");

    // A sound file as the reference.
    std::vector<uint8_t> wavBytes;
    {
        hnl::sampler::AudioData data;
        data.channels = 2;
        data.sampleRate = 44100.0;
        data.samples[0] = l;
        data.samples[1] = r;
        std::string error;
        hnl::sampler::writeWav(dir / "Final mix.wav", data, 24, error);
    }
    click(ui, kTrackKey);
    click(ui, kMatchNone);
    engine.fetch(seen, *shown);
    const std::string trackName = shown->track.name;
    const double trackLoudness = shown->track.r.integrated;
    ui->sentParameters.clear();
    click(ui, kAddFile);
    check(!ui->browserSaving && ui->browserTitle.find("WAV") != std::string::npos, "+ FILE asks for a sound file");
    ui->uiFileBrowserSelected((dir / "Final mix.wav").string().c_str());
    waitForFile(engine);
    Analysis fromFile;
    check(shows(ui, "Final mix") && shows(ui, "Reference: Final mix") && ui->sentState == "reference=0|Final mix" && References::read(References::folder() / "Final mix.json", fromFile) &&
              fromFile.capture.mode == kFile && fromFile.r.frames == l.size() && fromFile.has(kHaveAll),
          "the file is measured, kept with the references under its name, and compared with at once");
    engine.fetch(seen, *shown);
    check(ui->sentParameters.empty() && shown->track.name == trackName && shown->track.r.integrated == trackLoudness && shows(ui, fixed(fromFile.r.integrated, 2)),
          "the track that is shown stays as it was, the mode too; the file's figures stand beside it");
    check(hnl::settings::get("analyzer_folder") == dir.string(), "the folder is remembered for the next time");
    { std::ofstream(dir / "song.mp3") << "ID3"; }
    click(ui, kAddFile);
    ui->uiFileBrowserSelected((dir / "song.mp3").string().c_str());
    waitForFile(engine);
    check(showsPart(ui, "reads WAV, AIFF and FLAC, not mp3") && shows(ui, "Final mix"), "a file it cannot read says why, and the reference stays");
    click(ui, kHelpButton);
    check(shows(ui, "Final mix") && !showsPart(ui, "not mp3"), "said once, until the next click");

    // Help.
    DISTRHO::MotionEvent hover;
    hover.pos.x = 700;
    hover.pos.y = 600;
    ui->onMotion(hover);
    check(showsPart(ui, "Required gain: to reach") || showsPart(ui, "What it takes to reach"), "with help on, resting on the advisor says what it shows");
    hover.pos.x = 100;
    hover.pos.y = 300;
    ui->onMotion(hover);
    check(showsPart(ui, "The whole track:"), "and on the track, what the lines are");
    click(ui, kHelpButton);

    // Every kind of content draws: partial figures, a mono file, silence, a long live run.
    Analysis typed;
    fromJson(demo::currentJson(), typed);
    engine.setCompleted(typed);
    check(shows(ui, "FIGURES ONLY, NO SOUND MEASURED") && shows(ui, "-12.56") && shows(ui, "10.49"), "figures typed by hand are shown for what they are");
    Analysis silent;
    {
        std::unique_ptr<Meter> m(new Meter());
        m->prepare(48000.0, 1);
        std::vector<float> zero(48000 * 5, 0.0f);
        m->process(zero.data(), nullptr, static_cast<uint32_t>(zero.size()));
        m->snapshot(silent.r);
        silent.capture.status = kComplete;
        silent.capture.mode = kFile;
    }
    engine.setCompleted(silent);
    check(showsPart(ui, ">CAPTURED  0:05.0<") && shows(ui, "NO MEASUREMENT") && !showsPart(ui, "nan ") && !showsPart(ui, "nan<") && !showsPart(ui, "inf ") && !showsPart(ui, "inf<"), "five seconds of mono silence: no loudness, and no figure made up for it");

    // The size of the window: from the cog's list, by dragging its edge, and kept for the next time it opens.
    {
        const Spot cog = {UI_BASE_WIDTH - 24, 26};
        hnl::ui::SkinMenu place; // the same place as the UI's own, for where to click
        place.setButton(UI_BASE_WIDTH - 24, 26);
        place.offerSizes(100);
        const auto size = [&](int i, float zoom) { const hnl::ui::Box b = place.sizeBox(i); return Spot{zoom * (b.x + b.w * 0.5f), zoom * (b.y + b.h * 0.5f)}; };
        check(ui->getWidth() == 1200 && ui->getHeight() == 720 && ui->leastWidth == 900 && ui->leastHeight == 540 && ui->keepsShape,
              "the window opens at its design size, can be dragged down to three quarters of it, and keeps its shape");
        click(ui, cog);
        check(shows(ui, "SIZE") && shows(ui, "150"), "the cog's list offers the sizes under the looks");
        click(ui, size(3, 1.0f)); // 150
        check(ui->getWidth() == 1800 && ui->getHeight() == 1080 && hnl::ui::storedSize("analyzer") == 150, "a size from the list makes the window that large at once, and is kept");
        click(ui, {1.5f * cog.x, 1.5f * cog.y}); // the cog again, where it is drawn now: the list closes
        ui->sentParameters.clear();
        click(ui, {1.5f * kContinuousKey.x, 1.5f * kContinuousKey.y});
        check(sent(ui, kMode, 1.0f) && !shows(ui, "SIZE"), "in the larger window a click lands on what is drawn there (the CONTINUOUS key)");
        click(ui, {1.5f * kTrackKey.x, 1.5f * kTrackKey.y});
        ui->setSize(1500, 900); // the host, when the edge of the window is dragged
        ui->uiIdle();
        check(hnl::ui::storedSize("analyzer") == 150, "while the window is being dragged nothing is written");
        for (int i = 0; i <= 2 * hnl::ui::SkinMenu::kPollEvery; ++i) ui->uiIdle();
        check(hnl::ui::storedSize("analyzer") == 125, "a second after the dragging stops, that size is kept");
        DISTRHO::UI* const again = DISTRHO::createUI();
        check(again->getWidth() == 1500 && again->getHeight() == 900, "the window opens again at the size it was left at");
        delete again;
        click(ui, {1.25f * cog.x, 1.25f * cog.y});
        click(ui, size(1, 1.25f)); // 100
        check(ui->getWidth() == 1200 && hnl::ui::storedSize("analyzer") == 100, "and 100 in the list brings back the design size");
    }

    delete ui;
    Engine::of(&token, nullptr, true);
    fs::remove_all(dir);
    std::printf("%s\n", failures == 0 ? "ALL PASS" : "FAILED");
    return failures == 0 ? 0 : 1;
}
