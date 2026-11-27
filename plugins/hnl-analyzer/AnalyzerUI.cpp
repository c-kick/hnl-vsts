// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// HNL Analyzer: user interface (NanoVG).
//
// Three rows. At the top the four figures a master is judged by, and the capture: its keys,
// how it stands and the reports. In the middle the whole track: its outline, left over right,
// and under it the loudness over the same stretch of time. At the bottom the amplitude
// statistics, the histogram and the spectrum, the advisor and the comparison with a reference.
//
// What is shown is read from the plugin itself (dsp/Engine.hpp), about thirty times a second;
// only the settings are parameters. Coordinates are design pixels, drawn as large as the window
// is: the host's UI scale times the size the user chose (common/ui/WindowSize.hpp).
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "DistrhoUI.hpp"

#include "../../common/ui/Skin.hpp"
#include "../../common/ui/Widgets.hpp"
#include "../../common/ui/WindowSize.hpp"
#include "dsp/Engine.hpp"
#include "Help.hpp"
#include "Params.hpp"
#include "Presets.hpp"

#if defined(_WIN32) && !defined(HNL_UI_PREVIEW)
#include <windows.h>
#include <shellapi.h>
#define HNL_FILE_DROP 1
#endif

START_NAMESPACE_DISTRHO

using DGL_NAMESPACE::Color;
using namespace hnl::analyzer;

namespace {

Color rgb(uint32_t hex, float alpha = 1.0f) {
    return Color(((hex >> 16) & 255) / 255.0f, ((hex >> 8) & 255) / 255.0f, (hex & 255) / 255.0f, alpha);
}

const uint32_t kBg = 0x14161a, kPanel = 0x1d2027, kPanelEdge = 0x2a2e38, kTrack = 0x30353f, kWell = 0x0e1013;
const uint32_t kText = 0xe6e9ef, kDim = 0x8a93a3, kSky = 0x5bc0f2;

typedef hnl::ui::Box Rect;

const float kWidth = UI_BASE_WIDTH, kHeight = UI_BASE_HEIGHT;
const float kPresetX = UI_BASE_WIDTH - 296, kHeaderY = 14;
const float kHelpX = UI_BASE_WIDTH - 52, kSkinX = UI_BASE_WIDTH - 24;

// The panels.
const Rect kLoudPanel = {12, 52, 716, 120}, kCapPanel = {740, 52, 448, 120};
const Rect kWavePanel = {12, 184, 1176, 220};
const Rect kStatPanel = {12, 416, 300, 292}, kHistPanel = {324, 416, 280, 140}, kSpecPanel = {324, 568, 280, 140};
const Rect kAdvPanel = {616, 416, 280, 292}, kRefPanel = {908, 416, 280, 292};

// The keys, in window coordinates.
const Rect kModeBox = {250, 15, 200, 22};
const Rect kAutoBox = {750, 82, 176, 22}, kStartBox = {932, 82, 120, 22}, kResetBox = {1058, 82, 120, 22};
const Rect kStatusBox = {750, 110, 428, 26};
const Rect kNameBox = {790, 142, 180, 22};
const Rect kReportBox = {1018, 142, 160, 22}; // COPY, TXT, MD, JSON
const Rect kWaveBox = {22, 212, 1156, 182};
const Rect kStatBox = {22, 444, 280, 206}, kWindowBox = {72, 656, 230, 20}, kRefKindBox = {172, 682, 130, 20};
const Rect kHistBox = {334, 444, 260, 102}, kSpecBox = {334, 596, 260, 102};
const Rect kAdvBox = {626, 524, 260, 174};
const Rect kRefPrev = {918, 444, 22, 22}, kRefName = {944, 444, 208, 22}, kRefNext = {1156, 444, 22, 22};
const Rect kMatchBox = {918, 472, 260, 20}, kRefBox = {918, 498, 260, 172};
const Rect kRefKeys = {918, 678, 260, 22}; // + TRACK, + FILE, EXPORT, DELETE

struct Knob {
    uint32_t id;
    float x, y, r;
    const char* label;
};
const Knob kKnobs[3] = {{kTargetLufs, 664, 476, 16, "TARGET"}, {kMaxTruePeak, 756, 476, 16, "MAX TRUE PEAK"}, {kPrefPlr, 848, 476, 16, "PREFERRED PLR"}};

/** The classic look in this plugin's own colours; the other looks are the same for every plugin (common/ui/Skin.hpp). */
hnl::ui::Skin classicLook() {
    hnl::ui::Skin k = hnl::ui::classicSkin(kBg, kPanel, kPanelEdge, kTrack, kWell, kText, kDim, kSky, 0x262a33, 0x3a404c);
    k.knobHi = k.knobLo = k.knobEdge = 0x262a33;
    return k;
}
const hnl::ui::Skin kClassic = classicLook();

/** A text box of one line: the name of the track. */
class TextField {
public:
    static constexpr size_t kMaxCharacters = 60;
    bool editing = false;
    std::string text;

    void start(const std::string& from) {
        editing = true;
        text = from;
    }
    /** A key went down. 1: done, take the text; -1: left without; 0: still typing. */
    int key(unsigned code) {
        if (code == 0x1B) { editing = false; return -1; }
        if (code == 0x0D) { editing = false; return 1; }
        if (code == 0x08) {
            while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0) == 0x80) text.pop_back();
            if (!text.empty()) text.pop_back();
        }
        return 0;
    }
    /** One character, as UTF-8 or (from a host that hands on only that) as its Unicode number. */
    void character(const char* utf8, unsigned code) {
        char made[5] = {0};
        if (utf8[0] == '\0' && code != 0 && code <= 0x10FFFF && (code < 0xD800 || code > 0xDFFF)) {
            if (code < 0x80) {
                made[0] = static_cast<char>(code);
            } else if (code < 0x800) {
                made[0] = static_cast<char>(0xC0 | (code >> 6));
                made[1] = static_cast<char>(0x80 | (code & 0x3F));
            } else if (code < 0x10000) {
                made[0] = static_cast<char>(0xE0 | (code >> 12));
                made[1] = static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                made[2] = static_cast<char>(0x80 | (code & 0x3F));
            } else {
                made[0] = static_cast<char>(0xF0 | (code >> 18));
                made[1] = static_cast<char>(0x80 | ((code >> 12) & 0x3F));
                made[2] = static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                made[3] = static_cast<char>(0x80 | (code & 0x3F));
            }
            utf8 = made;
        }
        const unsigned char c = static_cast<unsigned char>(utf8[0]);
        if (c < 0x20 || c == 0x7F) return; // Enter, Backspace and the like also arrive as text
        size_t characters = 0;
        for (unsigned char b : text) characters += (b & 0xC0) != 0x80 ? 1 : 0;
        if (characters < kMaxCharacters && !(text.empty() && c == ' ')) text += utf8;
    }
};

/** The first `max` characters of a text, with three dots when there were more. */
std::string fit(const std::string& s, size_t max) {
    size_t characters = 0, at = 0;
    for (; at < s.size(); ++at) {
        if ((static_cast<unsigned char>(s[at]) & 0xC0) == 0x80) continue;
        if (++characters > max) break;
    }
    return at >= s.size() ? s : s.substr(0, at) + "...";
}

std::string number(double v, int decimals, bool sign = false) {
    char format[16], s[40];
    std::snprintf(format, sizeof(format), sign ? "%%+.%df" : "%%.%df", decimals);
    std::snprintf(s, sizeof(s), format, v);
    for (char* c = s; *c; ++c)
        if (*c == ',') *c = '.';
    return s;
}
/** A level, or two dashes when there is none. */
std::string level(double db, int decimals = 2) { return db <= kSilence + 1.0 ? std::string("--") : number(db, decimals); }

#ifdef HNL_FILE_DROP
/**
 * Files dropped on the window. DPF passes none on, so the window's own procedure is given one
 * in front of it that takes the drop and hands everything else through.
 */
class FileDrop {
public:
    ~FileDrop() { detach(); }
    void attach(HWND window) {
        if (hwnd_ != nullptr || window == nullptr) return;
        hwnd_ = window;
        SetPropW(hwnd_, L"HnlAnalyzerDrop", static_cast<HANDLE>(this));
        old_ = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(hwnd_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&FileDrop::proc)));
        DragAcceptFiles(hwnd_, TRUE);
    }
    void detach() {
        if (hwnd_ == nullptr) return;
        if (IsWindow(hwnd_)) {
            DragAcceptFiles(hwnd_, FALSE);
            // Only when ours is still the one in front: otherwise taking it out would cut off whoever came after.
            if (reinterpret_cast<WNDPROC>(GetWindowLongPtrW(hwnd_, GWLP_WNDPROC)) == &FileDrop::proc)
                SetWindowLongPtrW(hwnd_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(old_));
            RemovePropW(hwnd_, L"HnlAnalyzerDrop");
        }
        hwnd_ = nullptr;
    }
    /** The file dropped since the last call, or an empty path. */
    std::wstring take() {
        std::wstring out;
        out.swap(dropped_);
        return out;
    }

private:
    static LRESULT CALLBACK proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
        FileDrop* const self = static_cast<FileDrop*>(GetPropW(hwnd, L"HnlAnalyzerDrop"));
        if (self == nullptr) return DefWindowProcW(hwnd, message, wparam, lparam);
        if (message == WM_DROPFILES) {
            const HDROP drop = reinterpret_cast<HDROP>(wparam);
            const UINT length = DragQueryFileW(drop, 0, nullptr, 0);
            if (length > 0 && length < 32768) {
                std::wstring path(length + 1, L'\0');
                path.resize(DragQueryFileW(drop, 0, &path[0], length + 1));
                self->dropped_ = path;
            }
            DragFinish(drop);
            return 0;
        }
        const WNDPROC old = self->old_;
        if (message == WM_NCDESTROY) self->detach();
        return CallWindowProcW(old, hwnd, message, wparam, lparam);
    }
    HWND hwnd_ = nullptr;
    WNDPROC old_ = nullptr;
    std::wstring dropped_;
};
#endif

} // namespace

class AnalyzerUI : public UI {
public:
    AnalyzerUI() : UI(UI_BASE_WIDTH, UI_BASE_HEIGHT, false) {
        for (uint32_t i = 0; i < kParamCount; ++i) values_[i] = kParams[i].def;
        loadSharedResources();
        engine_ = Engine::of(getPluginInstancePointer());
        shown_.reset(new Engine::Shown());
        loaded_.reset(new Analysis());
        taken_.reset(new Analysis());
        help_.setButton(kHelpX, kHeaderY + 12);
        skin_.setButton(kSkinX, kHeaderY + 12);
        size_.open(skin_); // the window opens at the size it was left at
        placeHelp();
        refresh();
    }

protected:
    void parameterChanged(uint32_t index, float value) override {
        if (index >= kParamCount || values_[index] == value) return;
        values_[index] = value;
        repaint();
    }

    void stateChanged(const char* key, const char* value) override {
        if (std::strcmp(key, "preset") == 0) {
            presetName_ = value;
            list_.rescan();
            preset_ = list_.find(presetName_);
            repaint();
        } else if (std::strcmp(key, "reference") == 0) {
            // "<how it is matched>|<name>": an empty name for none, "*" for the capture before this one.
            const char* bar = std::strchr(value, '|');
            match_ = bar != nullptr && value[0] >= '0' && value[0] < '0' + kMatchCount ? value[0] - '0' : kAsMeasured;
            const std::string name = bar != nullptr ? bar + 1 : "";
            refs_.rescan();
            const int saved = refs_.find(name);
            reference_ = name == "*" ? kPrevious : (saved >= 0 ? kFirstSaved + saved : kNoReference); // also for one that is no longer there
            loadReference();
            repaint();
        } else if (std::strcmp(key, "analysis") == 0) {
            refresh();
            repaint();
        }
    }

    void uiIdle() override {
        if (skin_.poll()) repaint(); // another HNL window chose a look
        if (size_.follow(skin_)) repaint(); // the window was dragged to another size
#ifdef HNL_FILE_DROP
        drop_.attach(reinterpret_cast<HWND>(getWindow().getNativeWindowHandle()));
        const std::wstring dropped = drop_.take();
        if (!dropped.empty()) openPath(std::filesystem::path(dropped));
#endif
        if (refresh() || shown_->fileBusy) repaint();
    }

    /** The system's file dialog answered: a path, or null when it was closed without one. */
    void uiFileBrowserSelected(const char* filename) override {
        const int what = browse_;
        browse_ = kBrowseNone;
        if (filename == nullptr || filename[0] == '\0') return;
        const std::filesystem::path file = References::path(filename);
        switch (what) {
        case kBrowseFile: openPath(file); break;
        case kBrowseExport: {
            const Analysis* ref = reference();
            writeFile(withExtension(file, ".json"), ref != nullptr ? toJson(*ref, view()) : std::string());
            break;
        }
        case kBrowseTxt: writeFile(withExtension(file, ".txt"), textReport(report())); break;
        case kBrowseMd: writeFile(withExtension(file, ".md"), markdownReport(report())); break;
        case kBrowseJson: writeFile(withExtension(file, ".json"), jsonReport(report())); break;
        default: break;
        }
        repaint();
    }

    // ------------------------------------------------------------ drawing

    void onNanoDisplay() override {
        const float s = size_.zoom();
        scale(s, s);
        fontFace(NANOVG_DEJAVU_SANS_TTF);

        const hnl::ui::Skin k = skin();
        const hnl::ui::Theme theme = k.theme();
        hnl::ui::faceplate(*this, k, std::max(kWidth, getWidth() / s), std::max(kHeight, getHeight() / s));

        hnl::ui::title(*this, "ANALYZER", rgb(k.plateText), rgb(k.plateDim));
        static const char* const kModes[kMeterModes] = {"TRACK", "CONTINUOUS"};
        keys(kModeBox, kModes, kMeterModes, mode());
        std::string presetText = presetName_;
        if (presetModified()) presetText += " *";
        if (!presetNote_.empty()) presetText = presetNote_;
        presetBar_.drawBar(*this, theme, presetText.c_str());
        help_.drawButton(*this, theme);
        skin_.drawButton(*this, theme);

        drawLoudness();
        drawCapture();
        drawWave();
        drawStatistics();
        drawHistogram();
        drawSpectrum();
        drawAdvisor();
        drawReference();

        presetBar_.drawMenu(*this, theme, list_.count(), preset_, list_.factoryCount(), canSavePreset(), [this](int i) { return list_.name(i); });
        skin_.drawMenu(*this, theme);
        if (!presetBar_.isOpen() && !skin_.isOpen() && drag_ < 0) help_.drawTip(*this, theme, kWidth, kHeight);
    }

    // -------------------------------------------------------- interaction

    bool onMouse(const MouseEvent& ev) override {
        if (ev.button != 1) return false;
        const float s = size_.zoom();
        const float x = static_cast<float>(ev.pos.getX()) / s, y = static_cast<float>(ev.pos.getY()) / s;

        if (!ev.press) {
            if (drag_ < 0) return false;
            editParameter(kKnobs[drag_].id, false);
            drag_ = -1;
            return true;
        }

        presetNote_.clear();
        note_.clear();
        const bool wasSure = sure_;
        sure_ = false;
        if (name_.editing) { // a click anywhere takes the name as it stands
            name_.editing = false;
            setName(name_.text);
            repaint();
            if (kNameBox.contains(x, y)) return true;
        }
        if (const int cog = presetBar_.isBusy() ? hnl::ui::SkinMenu::kNotMine : skin_.mouseDown(x, y)) { // the cog, or its list while it is open
            if (cog == hnl::ui::SkinMenu::kSized) size_.choose(skin_.size()); // a size from the list: the window follows at once
            help_.hide();
            repaint();
            return true;
        }
        if (!presetBar_.isBusy() && presetBar_.nameBox().contains(x, y)) { // another window may have saved or deleted one
            list_.rescan();
            preset_ = list_.find(presetName_);
        }
        const int picked = presetBar_.mouseDown(x, y, preset_, list_.count(), list_.factoryCount(), canSavePreset());
        if (picked != hnl::ui::PresetBar::kNotMine) {
            help_.hide();
            presetAction(picked);
            repaint();
            return true;
        }
        if (help_.buttonHit(x, y)) {
            help_.on = !help_.on;
            help_.motion(x, y);
            repaint();
            return true;
        }

        int part = 0;
        if (keyAt(kModeBox, kMeterModes, x, y, part)) {
            change(kMode, static_cast<float>(part), true);
            refresh();
            return true;
        }
        if (kAutoBox.contains(x, y)) {
            if (mode() == kTrackMode) change(kAuto, on(kAuto) ? 0.0f : 1.0f, true);
            return true;
        }
        if (kStartBox.contains(x, y)) {
            if (engine_ != nullptr && mode() == kTrackMode) {
                if (capturing()) engine_->stop(); else engine_->start();
            }
            return true;
        }
        if (kResetBox.contains(x, y)) {
            if (engine_ != nullptr) engine_->reset();
            refresh();
            repaint();
            return true;
        }
        if (kNameBox.contains(x, y)) {
            name_.start(shown_->track.name);
            getWindow().focus(); // so that the typing comes to this window
            repaint();
            return true;
        }
        if (keyAt(kReportBox, 4, x, y, part)) {
            exportReport(part);
            repaint();
            return true;
        }
        if (keyAt(kWindowBox, kRmsWindows, x, y, part)) {
            change(kRmsWindow, static_cast<float>(part), true);
            return true;
        }
        if (keyAt(kRefKindBox, 2, x, y, part)) {
            change(kRmsRef, static_cast<float>(part), true);
            return true;
        }
        if (kRefPrev.contains(x, y) || kRefNext.contains(x, y)) {
            refs_.rescan();
            const int count = refs_.count() + 2; // none, the one before, the saved ones
            const int at = reference_ - kNoReference;
            selectReference(kNoReference + (at + (kRefNext.contains(x, y) ? 1 : count - 1)) % count);
            return true;
        }
        if (keyAt(kMatchBox, kMatchCount, x, y, part)) {
            match_ = part;
            storeReference();
            repaint();
            return true;
        }
        if (keyAt(kRefKeys, 4, x, y, part)) {
            referenceKey(part, wasSure);
            repaint();
            return true;
        }

        const int hit = knobAt(x, y);
        if (hit < 0) return false;
        const uint32_t id = kKnobs[hit].id;
        const bool doubleClick = hit == lastClick_ && ev.time - lastClickTime_ < 350;
        lastClick_ = hit;
        lastClickTime_ = ev.time;
        if (doubleClick || (ev.mod & kModifierControl)) {
            change(id, kParams[id].def, true);
            return true;
        }
        drag_ = hit;
        dragY_ = y;
        dragNorm_ = paramToNorm(id, values_[id]);
        editParameter(id, true);
        return true;
    }

    bool onMotion(const MotionEvent& ev) override {
        const float s = size_.zoom();
        const float x = static_cast<float>(ev.pos.getX()) / s, y = static_cast<float>(ev.pos.getY()) / s;
        if (presetBar_.isOpen()) {
            if (presetBar_.motion(x, y, list_.count())) repaint();
            return true;
        }
        if (skin_.isOpen()) {
            if (skin_.motion(x, y)) repaint();
            return true;
        }
        if (drag_ < 0) {
            if (help_.motion(x, y)) repaint();
            return false;
        }
        const float range = (ev.mod & kModifierShift) ? 1200.0f : 200.0f;
        const uint32_t id = kKnobs[drag_].id;
        dragNorm_ = std::max(0.0f, std::min(1.0f, dragNorm_ + (dragY_ - y) / range));
        dragY_ = y;
        change(id, stepped(id, normToParam(id, dragNorm_)), false);
        return true;
    }

    bool onKeyboard(const KeyboardEvent& ev) override {
        if (name_.editing) {
            if (!ev.press) return true;
            if (name_.key(ev.key) > 0) setName(name_.text);
            repaint();
            return true;
        }
        if (!ev.press) return presetBar_.isEditing();
        const int result = presetBar_.key(ev.key);
        if (result == hnl::ui::PresetBar::kNotMine) return false;
        presetAction(result);
        repaint();
        return true;
    }

    bool onCharacterInput(const CharacterInputEvent& ev) override {
        if (name_.editing) {
            name_.character(ev.string, ev.character);
            repaint();
            return true;
        }
        if (!presetBar_.character(ev.string, ev.character)) return false;
        repaint();
        return true;
    }

    bool onScroll(const ScrollEvent& ev) override {
        if (presetBar_.isBusy() || drag_ >= 0) return false;
        const float s = size_.zoom();
        const int hit = knobAt(static_cast<float>(ev.pos.getX()) / s, static_cast<float>(ev.pos.getY()) / s);
        if (hit < 0) return false;
        const uint32_t id = kKnobs[hit].id;
        const float step = (ev.mod & kModifierShift) ? 0.1f : 0.5f; // decibels
        change(id, std::max(kParams[id].min, std::min(kParams[id].max, stepped(id, values_[id]) + step * static_cast<float>(ev.delta.getY() > 0 ? 1 : -1))), true);
        return true;
    }

private:
    enum { kNoReference = -2, kPrevious = -1, kFirstSaved = 0 };
    enum { kBrowseNone, kBrowseFile, kBrowseExport, kBrowseTxt, kBrowseMd, kBrowseJson };

    bool on(uint32_t id) const { return values_[id] > 0.5f; }
    int mode() const { return std::max(0, std::min(kMeterModes - 1, static_cast<int>(std::lround(values_[kMode])))); }
    View view() const { return {std::max(0, std::min(kRmsWindows - 1, static_cast<int>(std::lround(values_[kRmsWindow])))), values_[kRmsRef] < 0.5f}; }
    Targets targets() const { return {values_[kTargetLufs], values_[kMaxTruePeak], values_[kPrefPlr]}; }
    bool capturing() const { return shown_->track.capture.status == kRunning; }
    /** A target as a knob leaves it: in tenths of a decibel. */
    static float stepped(uint32_t, float v) { return std::round(v * 10.0f) / 10.0f; }

    /** Brings what is shown up to date with the plugin. True when it changed. */
    bool refresh() {
        if (engine_ == nullptr) return false;
        const bool changed = engine_->fetch(seen_, *shown_);
        std::string error;
        const int outcome = engine_->takeFile(*taken_, error); // a file that was being read for a reference
        if (outcome > 0) {
            const std::string why = taken_->capture.status == kPartial ? taken_->note : std::string();
            keepReference(*taken_, "Reference: ");
            if (!why.empty() && reference_ >= kFirstSaved) note_ = "Reference is incomplete: " + why;
        } else if (outcome < 0) {
            note_ = error;
        }
        return changed || outcome != 0;
    }

    /** The reference in use: the capture before this one, a saved one, or none. */
    const Analysis* reference() const {
        if (reference_ == kPrevious) return shown_->previous.empty() ? nullptr : &shown_->previous;
        return reference_ >= kFirstSaved && !loaded_->empty() ? loaded_.get() : nullptr;
    }
    std::string referenceName() const {
        if (reference_ == kNoReference) return "No reference";
        if (reference_ == kPrevious) return shown_->previous.empty() ? "Previous capture (none yet)" : "Previous: " + (shown_->previous.name.empty() ? clock(shown_->previous.r.seconds()) : shown_->previous.name);
        return reference_ < refs_.count() ? refs_.name(reference_) : std::string("?");
    }
    void loadReference() {
        *loaded_ = Analysis();
        loaded_->capture.status = kEmpty;
        if (reference_ >= kFirstSaved && !refs_.load(reference_, *loaded_)) {
            note_ = "Could not read that reference";
            reference_ = kNoReference;
        }
    }
    void selectReference(int which) {
        reference_ = which;
        loadReference();
        storeReference();
        repaint();
    }
    /** Tells the host which reference is in use, which keeps it with the song. */
    void storeReference() {
        const std::string name = reference_ == kPrevious ? "*" : (reference_ >= kFirstSaved && reference_ < refs_.count() ? refs_.name(reference_) : "");
        const std::string value = std::string(1, static_cast<char>('0' + match_)) + "|" + name;
        setState("reference", value.c_str());
    }

    /** + TRACK, + FILE, EXPORT, DELETE under the comparison. */
    void referenceKey(int key, bool wasSure) {
        if (key == 0) {
            if (shown_->track.empty()) {
                note_ = "Nothing measured to save";
                return;
            }
            const std::string name = refs_.save(shown_->track, view());
            if (name.empty()) {
                note_ = "Could not save the reference";
                return;
            }
            note_ = "Saved as reference: " + name;
        } else if (key == 1) {
            if (shown_->fileBusy && engine_ != nullptr) {
                engine_->cancelFile();
                refresh();
            } else {
                browse(kBrowseFile, "A reference: a sound file (WAV, AIFF, FLAC) or a saved reference (JSON)", nullptr);
            }
        } else if (key == 2) {
            const Analysis* ref = reference();
            if (ref == nullptr) {
                note_ = "Choose a reference to export";
                return;
            }
            browse(kBrowseExport, "Export the reference as JSON", (References::fileName(ref->name) + ".json").c_str());
        } else if (reference_ >= kFirstSaved) {
            if (!wasSure) { // asks once more, as the presets do
                sure_ = true;
                return;
            }
            if (!refs_.remove(reference_)) note_ = "Could not delete";
            selectReference(kNoReference);
        }
    }
    void importReference(const std::filesystem::path& file) {
        Analysis a;
        if (!References::read(file, a)) {
            note_ = "Not a reference this plugin can read";
            return;
        }
        keepReference(a, "Imported: ");
    }
    /** Puts an analysis with the saved references and compares with it from now on. */
    void keepReference(const Analysis& a, const char* said) {
        const std::string name = refs_.save(a, view());
        if (name.empty()) {
            note_ = "Could not save the reference";
            return;
        }
        note_ = said + name;
        selectReference(refs_.find(name) + kFirstSaved);
    }

    // -------------------------------------------------------------- files

    void browse(int what, const char* title, const char* defaultName) {
        FileBrowserOptions options;
        options.saving = defaultName != nullptr;
        options.title = title;
        options.defaultName = defaultName;
        const std::string dir = hnl::settings::get("analyzer_folder");
        if (!dir.empty()) options.startDir = dir.c_str();
        browse_ = what;
        if (!openFileBrowser(options)) {
            browse_ = kBrowseNone;
            note_ = "The file dialog could not be opened";
        }
    }
    void openPath(const std::filesystem::path& file) {
        hnl::settings::set("analyzer_folder", References::text(file.parent_path()));
        std::string ext = References::text(file.extension());
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext == ".json") { // a reference that was saved or exported before
            importReference(file);
            repaint();
            return;
        }
        if (engine_ == nullptr) return;
        engine_->measureReference(file);
        refresh();
        repaint();
    }
    static std::filesystem::path withExtension(std::filesystem::path file, const char* extension) {
        if (!file.has_extension()) file += extension;
        return file;
    }
    void writeFile(const std::filesystem::path& file, const std::string& content) {
        if (content.empty() || !References::write(file, content)) {
            note_ = "Could not write " + fit(References::text(file.filename()), 40);
            return;
        }
        hnl::settings::set("analyzer_folder", References::text(file.parent_path()));
        note_ = "Written: " + fit(References::text(file.filename()), 44);
    }
    ReportInput report() const {
        ReportInput in;
        in.track = &shown_->track;
        in.reference = reference();
        in.match = match_;
        in.view = view();
        in.targets = targets();
        static char version[32];
        std::snprintf(version, sizeof(version), "%d.%d.%d", HNL_VERSION_MAJOR, HNL_VERSION_MINOR, HNL_VERSION_PATCH);
        in.version = version;
        return in;
    }
    /** COPY, TXT, MD, JSON. */
    void exportReport(int key) {
        if (shown_->track.empty()) {
            note_ = "Nothing measured to report";
            return;
        }
        const std::string stem = References::fileName(shown_->track.name.empty() ? "Analysis" : shown_->track.name);
        if (key == 0) {
            const std::string text = textReport(report());
            note_ = getWindow().setClipboard("text/plain", text.c_str(), text.size() + 1) ? "Copied to the clipboard" : "Could not reach the clipboard";
        } else if (key == 1) {
            browse(kBrowseTxt, "Save the report as text", (stem + ".txt").c_str());
        } else if (key == 2) {
            browse(kBrowseMd, "Save the report as Markdown", (stem + ".md").c_str());
        } else {
            browse(kBrowseJson, "Save the report as JSON", (stem + ".json").c_str());
        }
    }
    void setName(const std::string& name) {
        if (engine_ != nullptr) engine_->setName(name);
        refresh();
    }

    // ----------------------------------------------------------- controls

    static bool keyAt(const Rect& box, int count, float x, float y, int& part) {
        if (!box.contains(x, y)) return false;
        part = std::max(0, std::min(count - 1, static_cast<int>((x - box.x) / (box.w / count))));
        return true;
    }
    int knobAt(float x, float y) const {
        for (int i = 0; i < 3; ++i)
            if (std::hypot(x - kKnobs[i].x, y - kKnobs[i].y) <= kKnobs[i].r + 8) return i;
        return -1;
    }

    /** Applies a value locally and sends it to the host. */
    void change(uint32_t id, float value, bool gesture) {
        if (gesture) editParameter(id, true);
        values_[id] = value;
        setParameterValue(id, value);
        if (gesture) editParameter(id, false);
        repaint();
    }

    void placeHelp() {
        help_.clear();
        help_.add(kLoudPanel, "LOUDNESS AND PEAK", help::kLoudness);
        help_.add(kCapPanel, "CAPTURE", help::kCapture);
        help_.add(kWavePanel, "TRACK", help::kWave);
        help_.add(kStatPanel, "AMPLITUDE STATISTICS", help::kStatistics);
        help_.add(kHistPanel, "RMS HISTOGRAM", help::kHistogram);
        help_.add(kSpecPanel, "SPECTRUM", help::kSpectrum);
        help_.add(kAdvPanel, "MASTERING ADVISOR", help::kAdvisor);
        help_.add(kRefPanel, "REFERENCE", help::kReference);
        help_.add(help_.buttonBox(), "HELP", help::kHelp);
        help_.add(skin_.buttonBox(), "LOOK AND SIZE", hnl::ui::SkinMenu::helpWithSizes());
        help_.add(presetBar_.nameBox(), "TARGETS", help::kPresets);
        help_.add(kModeBox, "MODE", help::forParam(kMode));
        help_.add(kAutoBox, "FOLLOW TRANSPORT", help::forParam(kAuto));
        help_.add(kStartBox, "START AND STOP", help::kStart);
        help_.add(kResetBox, "RESET", help::kReset);
        help_.add(kNameBox, "NAME", help::kName);
        help_.add(kReportBox, "REPORT", help::kReport);
        help_.add(kWindowBox, "RMS WINDOW", help::forParam(kRmsWindow));
        help_.add(kRefKindBox, "0 dB", help::forParam(kRmsRef));
        help_.add({kRefPrev.x, kRefPrev.y, kRefNext.x + kRefNext.w - kRefPrev.x, kRefPrev.h}, "REFERENCE", help::kReferenceChoice);
        help_.add(kMatchBox, "MATCH", help::kMatch);
        help_.add(kRefKeys, "REFERENCES", help::kReferenceKeys);
        for (const Knob& c : kKnobs) help_.add({c.x - c.r - 26, c.y - c.r - 18, 2 * c.r + 52, 2 * c.r + 38}, kParams[c.id].name, help::forParam(c.id));
    }

    // ------------------------------------------------------------ presets

    bool canSavePreset() const { return list_.canSave() && !list_.full(); }

    /** What the preset bar asks for: load a preset, start typing a name, save under it, or delete one. */
    void presetAction(int picked) {
        typedef hnl::ui::PresetBar Bar;
        if (picked >= 0) {
            applyPreset(picked);
        } else if (picked == Bar::kEdit) {
            presetBar_.startEdit(list_.suggestName(preset_));
            getWindow().focus(); // so that the typing comes to this window
        } else if (picked == Bar::kSave) {
            const int saved = list_.save(presetBar_.text(), values_);
            if (saved < 0) {
                presetNote_ = "Could not save";
                return;
            }
            preset_ = saved;
            presetName_ = list_.name(saved);
            setState("preset", presetName_.c_str());
        } else if (picked == Bar::kDelete) {
            if (!list_.remove(presetBar_.target())) presetNote_ = "Could not delete";
            preset_ = list_.find(presetName_);
        }
    }

    void applyPreset(int index) {
        preset_ = index;
        presetName_ = list_.name(index);
        for (uint32_t id = 0; id < kInputParamCount; ++id) {
            const float v = list_.value(index, id);
            if (!presets::inPreset(id) || values_[id] == v) continue;
            editParameter(id, true);
            values_[id] = v;
            setParameterValue(id, v);
            editParameter(id, false);
        }
        setState("preset", presetName_.c_str());
        repaint();
    }

    /** True when a target was moved since the preset was chosen. */
    bool presetModified() const {
        if (preset_ < 0) return false;
        for (uint32_t id = 0; id < kInputParamCount; ++id) {
            const float tolerance = 1e-4f * (kParams[id].max - kParams[id].min);
            if (presets::inPreset(id) && std::fabs(values_[id] - list_.value(preset_, id)) > tolerance) return true;
        }
        return false;
    }

    // -------------------------------------------------------------- parts

    hnl::ui::Skin skin() const { return skin_.skin() == hnl::ui::kSkinClassic ? kClassic : hnl::ui::solidSkin(skin_.skin(), kClassic.accent); }

    void label(const char* text, float x, float y, float size, const Color& colour, int align) {
        fontSize(size);
        textAlign(align);
        fillColor(colour);
        this->text(x, y, text, nullptr);
    }
    void label(const std::string& text, float x, float y, float size, const Color& colour, int align) { label(text.c_str(), x, y, size, colour, align); }

    /** A group on the plate, with its title. */
    void panel(const Rect& p, const char* title) {
        const hnl::ui::Skin k = skin();
        hnl::ui::platePanel(*this, k, p);
        if (k.solid) {
            beginPath();
            roundedRect(p.x + 9, p.y + 10, 5, 10, 2);
            fillColor(rgb(0x000000, 0.6f));
            fill();
        }
        beginPath();
        roundedRect(p.x + 10, p.y + 11, 3, 8, 1.5f);
        fillColor(rgb(k.accent));
        fill();
        label(title, p.x + 20, p.y + 15, 10, rgb(k.plateText), ALIGN_LEFT | ALIGN_MIDDLE);
    }
    /** Where figures and graphs are shown: a window of dark glass in the solid looks; in the flat one a darker field, for a graph only. */
    void screen(const Rect& r, bool graph) {
        const hnl::ui::Skin k = skin();
        if (k.solid) {
            hnl::ui::displayPanel(*this, k, r);
        } else if (graph) {
            beginPath();
            roundedRect(r.x, r.y, r.w, r.h, 4);
            fillColor(rgb(k.well));
            fill();
        }
    }
    void keys(const Rect& box, const char* const* names, int count, int chosen, bool enabled = true) {
        const hnl::ui::Skin k = skin();
        // A key that does nothing now keeps its name, faintly.
        const auto half = [](uint32_t a, uint32_t b) { return ((((a >> 16) & 255) + ((b >> 16) & 255)) / 2 << 16) | ((((a >> 8) & 255) + ((b >> 8) & 255)) / 2 << 8) | (((a & 255) + (b & 255)) / 2); };
        hnl::ui::keys(*this, k, box, count, chosen, [&](int i) { return names[i]; }, k.accent, enabled ? k.dim : half(k.dim, k.track));
        if (!enabled && k.solid) {
            beginPath();
            roundedRect(box.x, box.y, box.w, box.h, 4);
            fillColor(rgb(k.bgLow, 0.55f));
            fill();
        }
    }
    void line(float x0, float y0, float x1, float y1, const Color& colour, float width = 1.0f) {
        beginPath();
        moveTo(x0, y0);
        lineTo(x1, y1);
        strokeColor(colour);
        strokeWidth(width);
        stroke();
    }

    // ------------------------------------------------- loudness and peak

    void drawLoudness() {
        const hnl::ui::Skin k = skin();
        const Analysis& a = shown_->track;
        const Result& r = a.r;
        const Rect& p = kLoudPanel;
        panel(p, "LOUDNESS AND PEAK");
        const Rect d = {p.x + 10, p.y + 28, p.w - 20, p.h - 38};
        screen(d, false);
        const bool have = !a.empty(), loud = have && hasLoudness(r);
        const Targets t = targets();
        const double tp = truePeakDb(r);
        const float w = d.w / 4.0f;
        struct Cell { const char* title; std::string value; const char* unit; std::string sub; uint32_t colour; };
        const int R = r.channels > 1 ? 1 : 0;
        const Cell cells[4] = {
            {"INTEGRATED", loud ? number(r.integrated, 2) : "--", "LUFS", "S max " + level(a.has(kHaveMax) ? r.shortTermMax : kSilence, 1) + "   M max " + level(a.has(kHaveMax) ? r.momentaryMax : kSilence, 1),
             k.text},
            {"TRUE PEAK", have ? level(tp) : "--", "dBTP", "L " + level(have ? truePeakDb(r, 0) : kSilence) + "   R " + level(have ? truePeakDb(r, R) : kSilence),
             have && tp > t.truePeak + 0.005 ? k.red : k.text},
            {"PLR", loud ? number(plr(r), 2) : "--", "dB", "preferred " + number(t.plr, 1), k.text},
            {"LRA", loud && a.has(kHaveRange) ? number(r.range, 1) : "--", "LU", loud && a.has(kHaveRange) && r.rangeLow > kSilence + 1.0 ? number(r.rangeLow, 1) + " to " + number(r.rangeHigh, 1) + " LUFS" : std::string("loudness range"), k.text},
        };
        for (int i = 0; i < 4; ++i) {
            const float x = d.x + i * w;
            if (i > 0) line(x, d.y + 10, x, d.y + d.h - 10, rgb(k.dim, 0.25f));
            label(cells[i].title, x + 14, d.y + 14, 8, rgb(k.dim), ALIGN_LEFT | ALIGN_MIDDLE);
            label(cells[i].unit, x + w - 14, d.y + 14, 8, rgb(k.dim), ALIGN_RIGHT | ALIGN_MIDDLE);
            label(cells[i].value, x + w * 0.5f, d.y + 42, 30, rgb(cells[i].colour), ALIGN_CENTER | ALIGN_MIDDLE);
            label(cells[i].sub, x + w * 0.5f, d.y + 70, 9, rgb(k.dim), ALIGN_CENTER | ALIGN_MIDDLE);
        }
    }

    // ------------------------------------------------------------ capture

    /** How the capture stands, in a few words, and whether that is something to notice (1) or wrong (2). */
    std::string status(int& tone) const {
        tone = 0;
        const Analysis& a = shown_->track;
        if (engine_ == nullptr) return "NO PLUGIN";
        const int m = mode();
        if (a.capture.status == kRunning) {
            tone = 1;
            return std::string(m == kContinuousMode ? "MEASURING  " : "CAPTURING  ") + clock(a.r.seconds());
        }
        if (a.empty()) {
            if (m == kContinuousMode) return "WAITING FOR SOUND";
            return on(kAuto) ? "WAITING FOR THE TRANSPORT" : "PRESS START TO CAPTURE";
        }
        if (!a.has(kHaveSeries)) return "FIGURES ONLY, NO SOUND MEASURED";
        if (a.capture.status == kPartial) {
            tone = 2;
            return "INCOMPLETE  " + clock(a.r.seconds()) + "  " + endedName(a.capture.ended);
        }
        return "CAPTURED  " + clock(a.r.seconds());
    }

    void drawCapture() {
        const hnl::ui::Skin k = skin();
        const Analysis& a = shown_->track;
        panel(kCapPanel, "CAPTURE");
        const int m = mode();
        static const char* const kAutoName[1] = {"FOLLOW TRANSPORT"}, *const kStart[1] = {"START"}, *const kStop[1] = {"STOP"};
        keys(kAutoBox, kAutoName, 1, on(kAuto) && m == kTrackMode ? 0 : -1, m == kTrackMode);
        keys(kStartBox, capturing() && m == kTrackMode ? kStop : kStart, 1, capturing() && m == kTrackMode ? 0 : -1, m == kTrackMode);
        hnl::ui::button(*this, k, kResetBox, "RESET");

        screen(kStatusBox, true);
        int tone = 0;
        const std::string text = status(tone);
        const float my = kStatusBox.y + kStatusBox.h * 0.5f;
        beginPath();
        circle(kStatusBox.x + 12, my, 3.5f);
        fillColor(rgb(tone == 2 ? k.red : (tone == 1 ? k.accent : (a.empty() ? k.dim : k.green))));
        fill();
        if (!note_.empty()) {
            label(fit(note_, 58), kStatusBox.x + 24, my, 10, rgb(k.yellow), ALIGN_LEFT | ALIGN_MIDDLE);
        } else {
            label(text, kStatusBox.x + 24, my, 10, rgb(k.text), ALIGN_LEFT | ALIGN_MIDDLE);
            std::string detail;
            if (capturing()) detail = "M " + level(a.r.momentary, 1) + "   S " + level(a.r.shortTerm, 1) + " LUFS";
            else if (!a.empty() && a.r.sampleRate > 0.0 && a.has(kHaveSeries)) detail = (a.source.empty() ? std::string() : a.source + ", ") + number(a.r.sampleRate / 1000.0, 1) + " kHz " + (a.r.channels == 1 ? "mono" : "stereo");
            label(fit(detail, 36), kStatusBox.x + kStatusBox.w - 10, my, 9, rgb(k.dim), ALIGN_RIGHT | ALIGN_MIDDLE);
        }

        label("NAME", kCapPanel.x + 10, kNameBox.y + kNameBox.h * 0.5f, 9, rgb(k.plateDim), ALIGN_LEFT | ALIGN_MIDDLE);
        const hnl::ui::Theme t = k.theme();
        beginPath();
        roundedRect(kNameBox.x, kNameBox.y, kNameBox.w, kNameBox.h, 4);
        fillColor(rgb(t.panel));
        fill();
        strokeColor(rgb(name_.editing ? t.accent : t.edge));
        strokeWidth(1);
        stroke();
        const std::string name = name_.editing ? fit(name_.text, 26) + "|" : (a.name.empty() ? std::string("Click to name the track") : fit(a.name, 26));
        label(name, kNameBox.x + 8, kNameBox.y + kNameBox.h * 0.5f, 10, rgb(name_.editing || !a.name.empty() ? t.text : t.dim), ALIGN_LEFT | ALIGN_MIDDLE);

        static const char* const kReports[4] = {"COPY", "TXT", "MD", "JSON"};
        label("REPORT", kReportBox.x - 8, kReportBox.y + kReportBox.h * 0.5f, 9, rgb(k.plateDim), ALIGN_RIGHT | ALIGN_MIDDLE);
        keys(kReportBox, kReports, 4, -1);
    }

    // ------------------------------------------- the outline and loudness

    /** A round figure for the distance between two marks on the time axis. */
    static double timeStep(double seconds) {
        static const double kSteps[] = {1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 1200, 1800, 3600};
        for (double s : kSteps)
            if (seconds / s <= 12.0) return s;
        return 7200;
    }

    void drawWave() {
        const hnl::ui::Skin k = skin();
        const Analysis& a = shown_->track;
        const Result& r = a.r;
        panel(kWavePanel, "TRACK");
        screen(kWaveBox, true);
        const float x0 = kWaveBox.x + 40, w = kWaveBox.w - 48;
        const float waveTop = kWaveBox.y + 6, waveH = 44, loudTop = kWaveBox.y + 104, loudH = 60, axisY = kWaveBox.y + kWaveBox.h - 9;
        const double top = -3.0, bottom = -39.0; // LUFS shown
        const auto loudY = [&](double lufs) { return loudTop + loudH * static_cast<float>(std::clamp((top - lufs) / (top - bottom), 0.0, 1.0)); };

        // What stretch of time the width stands for.
        const double waveSpan = r.waveCount * r.waveStep, loudSpan = r.loudCount * r.loudStep;
        const double span = std::max(std::max(waveSpan, loudSpan), 1e-9);
        const bool rolling = a.capture.mode == kContinuousMode;
        double start = rolling ? std::max(0.0, r.seconds() - span) : 0.0;
        if (a.capture.mode == kTrackMode && a.capture.positionKnown && r.sampleRate > 0.0) start = static_cast<double>(a.capture.startFrame) / r.sampleRate;

        static const char* const kSide[2] = {"L", "R"};
        for (int c = 0; c < 2; ++c) {
            const float mid = waveTop + c * (waveH + 4) + waveH * 0.5f;
            label(kSide[c], kWaveBox.x + 12, mid, 9, rgb(k.dim), ALIGN_LEFT | ALIGN_MIDDLE);
            line(x0, mid, x0 + w, mid, rgb(k.dim, 0.3f));
            if (a.empty() || r.waveCount < 1 || (c == 1 && r.channels < 2)) continue;
            const int ch = c;
            beginPath();
            const int columns = static_cast<int>(w);
            for (int px = 0; px < columns; ++px) {
                const int from = static_cast<int>(static_cast<double>(px) / columns * waveSpan / span * r.waveCount);
                const int to = std::max(from + 1, static_cast<int>(static_cast<double>(px + 1) / columns * waveSpan / span * r.waveCount));
                if (from >= r.waveCount) break;
                float lo = r.waveMin[ch][from], hi = r.waveMax[ch][from];
                for (int i = from + 1; i < to && i < r.waveCount; ++i) {
                    lo = std::min(lo, r.waveMin[ch][i]);
                    hi = std::max(hi, r.waveMax[ch][i]);
                }
                lo = std::max(-1.0f, lo);
                hi = std::min(1.0f, hi);
                moveTo(x0 + px + 0.5f, mid - hi * waveH * 0.5f - 0.5f);
                lineTo(x0 + px + 0.5f, mid - lo * waveH * 0.5f + 0.5f);
            }
            strokeColor(rgb(k.accent));
            strokeWidth(1);
            stroke();
        }

        // The loudness: marks every 12 LU, the target, short-term as a line over momentary as a field.
        for (double lufs = -12.0; lufs >= -36.0; lufs -= 12.0) {
            line(x0, loudY(lufs), x0 + w, loudY(lufs), rgb(k.dim, 0.2f));
            label(number(lufs, 0), x0 - 6, loudY(lufs), 7, rgb(k.dim), ALIGN_RIGHT | ALIGN_MIDDLE);
        }
        label("LUFS", kWaveBox.x + 8, loudTop + 2, 7, rgb(k.dim), ALIGN_LEFT | ALIGN_MIDDLE);
        const float targetY = loudY(values_[kTargetLufs]);
        beginPath();
        for (float x = x0; x < x0 + w; x += 8) {
            moveTo(x, targetY);
            lineTo(std::min(x + 4, x0 + w), targetY);
        }
        strokeColor(rgb(k.green, 0.8f));
        strokeWidth(1);
        stroke();

        const auto curve = [&](const Result& from, const float* values, double gain, double fromSpan, bool field, const Color& colour) {
            if (from.loudCount < 2) return;
            const int columns = static_cast<int>(w * std::min(1.0, fromSpan / span));
            if (columns < 2) return;
            beginPath();
            bool begun = false;
            float lastX = x0;
            for (int px = 0; px < columns; ++px) {
                const int at = static_cast<int>(static_cast<double>(px) / columns * from.loudCount);
                const int to = std::max(at + 1, static_cast<int>(static_cast<double>(px + 1) / columns * from.loudCount));
                float v = values[at];
                for (int i = at + 1; i < to && i < from.loudCount; ++i) v = std::max(v, values[i]);
                const float y = loudY(v + gain);
                lastX = x0 + px + 0.5f;
                if (!begun) {
                    if (field) { moveTo(lastX, loudTop + loudH); lineTo(lastX, y); } else moveTo(lastX, y);
                    begun = true;
                } else {
                    lineTo(lastX, y);
                }
            }
            if (field) {
                lineTo(lastX, loudTop + loudH);
                closePath();
                fillColor(colour);
                fill();
            } else {
                strokeColor(colour);
                strokeWidth(1.4f);
                stroke();
            }
        };
        if (!a.empty() && a.has(kHaveSeries)) curve(r, r.loudM, 0.0, loudSpan, true, rgb(k.accent, 0.25f));
        if (const Analysis* ref = reference())
            if (ref->has(kHaveSeries)) curve(ref->r, ref->r.loudS, matchGain(a, *ref, match_), ref->r.loudCount * ref->r.loudStep, false, rgb(k.yellow, 0.9f));
        if (!a.empty() && a.has(kHaveSeries)) curve(r, r.loudS, 0.0, loudSpan, false, rgb(k.accent));

        // Time along the bottom.
        if (!a.empty() && span > 0.5) {
            const double step = timeStep(span);
            for (double t = std::ceil(start / step) * step; t <= start + span + 1e-6; t += step) {
                const float x = x0 + w * static_cast<float>((t - start) / span);
                line(x, loudTop + loudH, x, loudTop + loudH + 4, rgb(k.dim, 0.6f));
                label(clock(t, false), x, axisY, 7, rgb(k.dim), ALIGN_CENTER | ALIGN_MIDDLE);
            }
        }
        // What the lines are.
        const float keyY = kWavePanel.y + 15;
        float kx = kWavePanel.x + kWavePanel.w - 14;
        const auto legend = [&](const char* name, const Color& colour) {
            label(name, kx, keyY, 8, rgb(k.plateDim), ALIGN_RIGHT | ALIGN_MIDDLE);
            kx -= 7.0f * static_cast<float>(std::strlen(name)) * 0.72f + 8;
            line(kx - 12, keyY, kx, keyY, colour, 2);
            kx -= 26;
        };
        if (reference() != nullptr) legend("REFERENCE", rgb(k.yellow));
        legend("TARGET", rgb(k.green));
        legend("MOMENTARY", rgb(k.accent, 0.45f));
        legend("SHORT-TERM", rgb(k.accent));
    }

    // --------------------------------------------------------- statistics

    void drawStatistics() {
        const hnl::ui::Skin k = skin();
        const Analysis& a = shown_->track;
        const Result& r = a.r;
        const View v = view();
        panel(kStatPanel, "AMPLITUDE STATISTICS");
        screen(kStatBox, false);
        const bool have = !a.empty(), peaks = a.has(kHavePeaks), rms = a.has(kHaveRms), windows = a.has(kHaveWindows), whole = a.has(kHaveAll);
        const int R = r.channels > 1 ? 1 : 0;
        const float nameX = kStatBox.x + 10, leftX = kStatBox.x + 196, rightX = kStatBox.x + 270, row = 14.6f;
        float y = kStatBox.y + 11;
        label("LEFT", leftX, y, 8, rgb(k.dim), ALIGN_RIGHT | ALIGN_MIDDLE);
        label("RIGHT", rightX, y, 8, rgb(k.dim), ALIGN_RIGHT | ALIGN_MIDDLE);
        struct Line { const char* name; std::string left, right; bool alarm; };
        const auto db = [&](bool there, double value) { return there ? level(value) : std::string("--"); };
        const Line lines[] = {
            {"Peak amplitude", db(peaks, samplePeakDb(r, 0)), db(peaks, samplePeakDb(r, R)), false},
            {"True peak", db(peaks, truePeakDb(r, 0)), db(peaks, truePeakDb(r, R)), false},
            {"Total RMS", db(rms, totalRmsDb(r, 0, v.sine)), db(rms, totalRmsDb(r, R, v.sine)), false},
            {"Maximum RMS", db(windows, maxRmsDb(r, v.window, 0, v.sine)), db(windows, maxRmsDb(r, v.window, R, v.sine)), false},
            {"Minimum RMS", db(windows, minRmsDb(r, v.window, 0, v.sine)), db(windows, minRmsDb(r, v.window, R, v.sine)), false},
            {"Average RMS", db(windows, averageRmsDb(r, v.window, 0, v.sine)), db(windows, averageRmsDb(r, v.window, R, v.sine)), false},
            {"Crest factor", peaks && rms && totalRmsDb(r, 0, false) > kSilence ? number(crestDb(r, 0), 2) : "--", peaks && rms && totalRmsDb(r, R, false) > kSilence ? number(crestDb(r, R), 2) : "--", false},
            {"DC offset  %", whole ? number(100.0 * dcOffset(r, 0), 3) : "--", whole ? number(100.0 * dcOffset(r, R), 3) : "--", false},
            {"Clipped samples", whole ? number(static_cast<double>(r.ch[0].clipped), 0) : (peaks ? number(static_cast<double>(clipped(r)), 0) : "--"),
             whole ? number(static_cast<double>(r.ch[R].clipped), 0) : "", peaks && clipped(r) > 0},
        };
        for (const Line& l : lines) {
            y += row;
            label(l.name, nameX, y, 9.5f, rgb(k.dim), ALIGN_LEFT | ALIGN_MIDDLE);
            label(l.left, leftX, y, 10, rgb(l.alarm && r.ch[0].clipped ? k.red : k.text), ALIGN_RIGHT | ALIGN_MIDDLE);
            label(l.right, rightX, y, 10, rgb(l.alarm && r.ch[R].clipped ? k.red : k.text), ALIGN_RIGHT | ALIGN_MIDDLE);
        }
        y += row * 0.5f + 2;
        line(kStatBox.x + 8, y, kStatBox.x + kStatBox.w - 8, y, rgb(k.dim, 0.25f));
        y -= 2;
        const bool stereo = have && r.channels > 1 && a.has(kHaveStereo) && r.ch[0].sumSquares + r.ch[1].sumSquares > 0.0;
        const double corr = correlation(r), mid = midDb(r, false), side = sideDb(r, false);
        const Line single[] = {
            {"Balance  L - R", stereo ? number(balanceDb(r), 2, true) + " dB" : "--", "", false},
            {"Correlation", stereo ? number(corr, 2, true) : "--", "", stereo && corr < 0.0},
            {"Side - mid", stereo && mid > kSilence ? (side <= kSilence ? std::string("-inf") : number(side - mid, 1, true)) + " dB" : "--", "", false},
        };
        for (const Line& l : single) {
            y += row;
            label(l.name, nameX, y, 9.5f, rgb(k.dim), ALIGN_LEFT | ALIGN_MIDDLE);
            label(l.left, rightX, y, 10, rgb(l.alarm ? k.red : k.text), ALIGN_RIGHT | ALIGN_MIDDLE);
        }
        static const char* const kWindows[kRmsWindows] = {"10", "25", "50", "100", "300", "1 s"}, *const kRefs[2] = {"SINE", "SQUARE"};
        label("WINDOW", kStatPanel.x + 10, kWindowBox.y + kWindowBox.h * 0.5f, 9, rgb(k.plateDim), ALIGN_LEFT | ALIGN_MIDDLE);
        keys(kWindowBox, kWindows, kRmsWindows, v.window);
        label("0 dB = FULL SCALE", kStatPanel.x + 10, kRefKindBox.y + kRefKindBox.h * 0.5f, 9, rgb(k.plateDim), ALIGN_LEFT | ALIGN_MIDDLE);
        keys(kRefKindBox, kRefs, 2, v.sine ? 0 : 1);
    }

    /** How often each RMS level occurred, both channels together: 60 dB across, the loudest at the right. */
    void drawHistogram() {
        const hnl::ui::Skin k = skin();
        const Analysis& a = shown_->track;
        const View v = view();
        panel(kHistPanel, "RMS HISTOGRAM");
        screen(kHistBox, true);
        const float x0 = kHistBox.x + 6, w = kHistBox.w - 12, y0 = kHistBox.y + 6, h = kHistBox.h - 22, floor = y0 + h;
        const double offset = v.sine ? kSineRef : 0.0, low = -60.0, high = 3.0; // as shown
        const auto at = [&](double db) { return x0 + w * static_cast<float>(std::clamp((db - low) / (high - low), 0.0, 1.0)); };
        for (double db = -48.0; db <= 0.0; db += 12.0) {
            line(at(db), y0, at(db), floor, rgb(k.dim, 0.2f));
            label(number(db, 0), at(db), floor + 8, 7, rgb(k.dim), ALIGN_CENTER | ALIGN_MIDDLE);
        }
        label("dB", x0 + w, floor + 8, 7, rgb(k.dim), ALIGN_RIGHT | ALIGN_MIDDLE);
        const auto shape = [&](const Analysis& from, double gain, bool bars, const Color& colour) {
            if (!from.has(kHaveWindows)) return;
            const RmsWindowStats& s = from.r.rms[v.window];
            uint32_t most = 0;
            for (int b = 0; b < kHistBins; ++b) most = std::max(most, s.hist[0][b] + s.hist[1][b]);
            if (most == 0) return;
            beginPath();
            bool begun = false;
            for (int b = 0; b < kHistBins; ++b) {
                const uint32_t n = s.hist[0][b] + s.hist[1][b];
                const double db = kHistFloorDb + 0.5 * b + offset + gain;
                if (db + 0.5 < low || db > high) continue;
                const float top = floor - h * static_cast<float>(n) / static_cast<float>(most);
                if (bars) {
                    if (n == 0) continue;
                    rect(at(db), top, std::max(1.0f, at(db + 0.5) - at(db) - 0.3f), floor - top);
                } else {
                    if (!begun) moveTo(at(db + 0.25), top); else lineTo(at(db + 0.25), top);
                    begun = true;
                }
            }
            if (bars) {
                fillColor(colour);
                fill();
            } else {
                strokeColor(colour);
                strokeWidth(1.3f);
                stroke();
            }
        };
        if (!a.empty()) shape(a, 0.0, true, rgb(k.accent, 0.85f));
        if (const Analysis* ref = reference()) shape(*ref, matchGain(a, *ref, match_), false, rgb(k.yellow, 0.95f));
    }

    /** The mean spectrum in third octaves, 20 Hz to 20 kHz across and 60 dB up. */
    void drawSpectrum() {
        const hnl::ui::Skin k = skin();
        const Analysis& a = shown_->track;
        panel(kSpecPanel, "SPECTRUM");
        screen(kSpecBox, true);
        const float x0 = kSpecBox.x + 6, w = kSpecBox.w - 12, y0 = kSpecBox.y + 6, h = kSpecBox.h - 12, floor = y0 + h;
        const double top = -12.0, range = 60.0;
        const auto at = [&](float hz) { return x0 + w * std::log(hz / 20.0f) / std::log(1000.0f); };
        static const struct { float hz; const char* name; } kMarks[] = {{100, "100"}, {1000, "1k"}, {10000, "10k"}};
        for (const auto& m : kMarks) {
            line(at(m.hz), y0, at(m.hz), floor, rgb(k.dim, 0.2f));
            label(m.name, at(m.hz) - 3, y0 + 4, 7, rgb(k.dim), ALIGN_RIGHT | ALIGN_MIDDLE);
        }
        const auto shape = [&](const Analysis& from, double gain, bool field, const Color& colour) {
            if (!from.has(kHaveSpectrum) || from.r.spectra == 0) return;
            float ys[kSpecBands];
            for (int b = 0; b < kSpecBands; ++b) ys[b] = floor - h * static_cast<float>(std::clamp((spectrumDb(from.r, b) + gain - (top - range)) / range, 0.0, 1.0));
            for (int pass = field ? 0 : 1; pass < 2; ++pass) {
                beginPath();
                moveTo(x0, pass == 0 ? floor : ys[0]);
                if (pass == 0) lineTo(x0, ys[0]);
                for (int b = 0; b < kSpecBands; ++b) lineTo(x0 + w * (b + 0.5f) / kSpecBands, ys[b]);
                lineTo(x0 + w, ys[kSpecBands - 1]);
                if (pass == 0) {
                    lineTo(x0 + w, floor);
                    closePath();
                    fillColor(rgb(k.accent, 0.25f));
                    fill();
                } else {
                    strokeColor(colour);
                    strokeWidth(1.4f);
                    stroke();
                }
            }
        };
        if (!a.empty()) shape(a, 0.0, true, rgb(k.accent));
        if (const Analysis* ref = reference()) shape(*ref, matchGain(a, *ref, match_), false, rgb(k.yellow, 0.95f));
    }

    // ------------------------------------------------------------ advisor

    void drawAdvisor() {
        const hnl::ui::Skin k = skin();
        const Analysis& a = shown_->track;
        panel(kAdvPanel, "MASTERING ADVISOR");
        static const char* const kUnits[3] = {"LUFS", "dBTP", "dB"};
        for (int i = 0; i < 3; ++i) {
            const Knob& c = kKnobs[i];
            hnl::ui::knob(*this, k, c.x, c.y, c.r, paramToNorm(c.id, values_[c.id]), 0.0f, k.ring, 13);
            label(c.label, c.x, c.y - c.r - 12, 8, rgb(k.plateDim), ALIGN_CENTER | ALIGN_MIDDLE);
            label(number(values_[c.id], 1) + " " + kUnits[i], c.x, c.y + c.r + 13, 10, rgb(k.plateText), ALIGN_CENTER | ALIGN_MIDDLE);
        }
        screen(kAdvBox, false);
        const Targets t = targets();
        const Advice advice = adviceFor(a, t);
        const bool have = advice.kind != Advice::kNoMeasurement;
        const float nameX = kAdvBox.x + 10, valueX = kAdvBox.x + kAdvBox.w - 10, row = 14.6f;
        float y = kAdvBox.y + 12;
        struct Line { const char* name; std::string value; uint32_t colour; };
        const Line lines[4] = {
            {"Required gain", have ? number(advice.gain, 2, true) + " dB" : "--", k.text},
            {"Maximum gain under the ceiling", have ? number(advice.maxGain, 2, true) + " dB" : "--", have && advice.maxGain < -kOnTarget ? k.red : k.text},
            {"Minimum peak reduction", have ? number(advice.peakReduction, 2) + " dB" : "--", have && advice.peakReduction > kOnTarget ? k.yellow : k.text},
            {"PLR, measured / of the targets", have ? number(advice.plr, 2) + " / " + number(advice.targetPlr, 2) : "-- / " + number(t.truePeak - t.loudness, 2), k.text},
        };
        for (const Line& l : lines) {
            label(l.name, nameX, y, 9.5f, rgb(k.dim), ALIGN_LEFT | ALIGN_MIDDLE);
            label(l.value, valueX, y, 10, rgb(l.colour), ALIGN_RIGHT | ALIGN_MIDDLE);
            y += row;
        }
        line(kAdvBox.x + 8, y - 4, kAdvBox.x + kAdvBox.w - 8, y - 4, rgb(k.dim, 0.25f));
        const uint32_t tone = advice.kind == Advice::kAchieved ? k.green : (advice.kind == Advice::kGain ? k.accent : (advice.kind == Advice::kNoMeasurement ? k.dim : k.yellow));
        label(adviceTitle(advice), nameX, y + 8, 10, rgb(tone), ALIGN_LEFT | ALIGN_MIDDLE);
        fontSize(9.5f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        textLineHeight(1.3f);
        fillColor(rgb(k.text));
        textBox(nameX, y + 18, kAdvBox.w - 20, adviceText(advice, t).c_str(), nullptr);
    }

    // ---------------------------------------------------------- reference

    void drawReference() {
        const hnl::ui::Skin k = skin();
        const hnl::ui::Theme t = k.theme();
        const Analysis& a = shown_->track;
        const Analysis* ref = reference();
        panel(kRefPanel, "REFERENCE");
        for (const Rect* b : {&kRefPrev, &kRefName, &kRefNext}) {
            beginPath();
            roundedRect(b->x, b->y, b->w, b->h, 4);
            fillColor(rgb(b == &kRefName ? t.panel : t.track));
            fill();
        }
        beginPath();
        roundedRect(kRefName.x, kRefName.y, kRefName.w, kRefName.h, 4);
        strokeColor(rgb(t.edge));
        strokeWidth(1);
        stroke();
        const float my = kRefPrev.y + kRefPrev.h * 0.5f;
        hnl::ui::triangle(*this, kRefPrev.x + 14, my - 5, kRefPrev.x + 14, my + 5, kRefPrev.x + 8, my, rgb(t.text));
        hnl::ui::triangle(*this, kRefNext.x + 8, my - 5, kRefNext.x + 8, my + 5, kRefNext.x + 14, my, rgb(t.text));
        if (shown_->fileBusy) { // a file is being measured to be the reference: how far it is read
            beginPath();
            roundedRect(kRefName.x + 2, kRefName.y + 2, (kRefName.w - 4) * static_cast<float>(shown_->fileProgress), kRefName.h - 4, 3);
            fillColor(rgb(k.accent, 0.3f));
            fill();
            label("Reading the file  " + number(100.0 * shown_->fileProgress, 0) + " %", kRefName.x + 10, my, 10, rgb(t.text), ALIGN_LEFT | ALIGN_MIDDLE);
        } else {
            label(fit(referenceName(), 30), kRefName.x + 10, my, 10, rgb(ref != nullptr ? t.text : t.dim), ALIGN_LEFT | ALIGN_MIDDLE);
        }

        static const char* const kMatches[kMatchCount] = {"AS MEASURED", "LOUDNESS", "TRUE PEAK"};
        keys(kMatchBox, kMatches, kMatchCount, match_, ref != nullptr);

        screen(kRefBox, false);
        const double gain = ref != nullptr ? matchGain(a, *ref, match_) : 0.0;
        const float nameX = kRefBox.x + 10, trackX = kRefBox.x + 144, refX = kRefBox.x + 198, deltaX = kRefBox.x + 252, row = 14.6f;
        float y = kRefBox.y + 11;
        label("TRACK", trackX, y, 8, rgb(k.dim), ALIGN_RIGHT | ALIGN_MIDDLE);
        label(ref != nullptr && match_ != kAsMeasured ? "REF " + number(gain, 1, true) : std::string("REF"), refX, y, 8, rgb(k.yellow), ALIGN_RIGHT | ALIGN_MIDDLE);
        label("DIFF", deltaX, y, 8, rgb(k.dim), ALIGN_RIGHT | ALIGN_MIDDLE);
        static const char* const kShown[] = {"LUFS-I", "True peak", "PLR", "LRA", "Short-term max", "RMS L", "RMS R", "Sample peak", "Correlation", "Clipped samples"};
        for (const Row& r : compare(a, ref, gain, view())) {
            bool shown = false;
            for (const char* name : kShown) shown = shown || std::strcmp(name, r.name) == 0;
            if (!shown) continue;
            y += row;
            label(r.name, nameX, y, 9.5f, rgb(k.dim), ALIGN_LEFT | ALIGN_MIDDLE);
            label(r.haveTrack ? number(r.track, r.decimals) : "--", trackX, y, 10, rgb(k.text), ALIGN_RIGHT | ALIGN_MIDDLE);
            label(r.haveReference ? number(r.reference, r.decimals) : "--", refX, y, 10, rgb(r.haveReference ? k.yellow : k.dim), ALIGN_RIGHT | ALIGN_MIDDLE);
            label(r.haveTrack && r.haveReference ? number(r.delta(), r.decimals, r.decimals > 0 || r.delta() != 0.0) : "--", deltaX, y, 10, rgb(k.text), ALIGN_RIGHT | ALIGN_MIDDLE);
        }
        const float w = kRefKeys.w / 4.0f;
        const char* const names[4] = {"+ TRACK", shown_->fileBusy ? "CANCEL" : "+ FILE...", "EXPORT", sure_ ? "SURE?" : "DELETE"};
        for (int i = 0; i < 4; ++i) hnl::ui::button(*this, k, {kRefKeys.x + i * w + (i ? 2 : 0), kRefKeys.y, w - (i ? 2 : 0), kRefKeys.h}, names[i]);
    }

    float values_[kParamCount];
    Engine* engine_ = nullptr;
    std::unique_ptr<Engine::Shown> shown_; // what the plugin measured, as of the last look
    uint64_t seen_ = 0;
    References refs_;
    std::unique_ptr<Analysis> loaded_; // the saved reference in use
    std::unique_ptr<Analysis> taken_;  // room for a file the plugin measured, on its way to the references
    int reference_ = kNoReference, match_ = kAsMeasured, browse_ = kBrowseNone;
    bool sure_ = false;  // Delete was clicked once
    std::string note_;   // shown in place of the status until the next click
    TextField name_;

    int drag_ = -1, lastClick_ = -1;
    uint lastClickTime_ = 0;
    float dragY_ = 0.0f, dragNorm_ = 0.0f;

    int preset_ = 0;
    std::string presetName_ = presets::kAll[0].name;
    hnl::ui::PresetBar presetBar_{kPresetX, kHeaderY};
    hnl::PresetList list_ = presets::list();
    std::string presetNote_; // shown in place of the name until the next click
    hnl::ui::HelpTips help_;
    hnl::ui::SkinMenu skin_;
    hnl::ui::WindowSize<AnalyzerUI> size_{*this, "analyzer", UI_BASE_WIDTH, UI_BASE_HEIGHT};
#ifdef HNL_FILE_DROP
    FileDrop drop_;
#endif

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnalyzerUI)
};

UI* createUI() { return new AnalyzerUI(); }

END_NAMESPACE_DISTRHO
