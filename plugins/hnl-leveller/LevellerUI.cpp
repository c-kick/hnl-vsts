// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// HNL Leveller: user interface (NanoVG). All coordinates are in a fixed
// UI_BASE_WIDTH x UI_BASE_HEIGHT design space, scaled by the host's UI scale.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "DistrhoUI.hpp"

#include "../../common/ui/Widgets.hpp"
#include "dsp/Leveller.hpp"
#include "Help.hpp"
#include "Params.hpp"
#include "Presets.hpp"

START_NAMESPACE_DISTRHO

using DGL_NAMESPACE::Color;

namespace {

Color rgb(uint32_t hex, float alpha = 1.0f) {
    return Color(((hex >> 16) & 255) / 255.0f, ((hex >> 8) & 255) / 255.0f, (hex & 255) / 255.0f, alpha);
}

const uint32_t kBg = 0x151816, kPanel = 0x1d221f, kPanelEdge = 0x2d3531, kTrack = 0x35403a, kWell = 0x0c0f0d;
const uint32_t kText = 0xe6ebe8, kDim = 0x8a9690, kAccent = 0x63d2b4;
const uint32_t kFace = 0xe6e2d6, kFaceEdge = 0xb3ad9c, kInk = 0x22252a, kFaceRed = 0xb83a2b;
const uint32_t kGreen = 0x4cd27a, kYellow = 0xe8d44d, kRed = 0xef5b5b;

const float kPi = 3.14159265f;
const float kMeterTop = 20.0f; // dB of reduction at the right end of the scale

struct Rect {
    float x, y, w, h;
    bool contains(float px, float py) const { return px >= x && px <= x + w && py >= y && py <= y + h; }
};

const Rect kMeterPanel = {12, 52, 300, 170}, kLevelPanel = {320, 52, 488, 170};
const Rect kTimePanel = {12, 230, 796, 70}, kKnobPanel = {12, 308, 796, 120};
const Rect kFace_ = {24, 62, 276, 138};

const float kMeterSwitchY = 204; // the strip between the face and the lower edge of its panel
const hnl::ui::MeterColours kMeterColours = {kFace, kFaceEdge, kInk, kFaceRed, kWell, kPanelEdge, kTrack, kText, kDim, kAccent, kYellow, kRed};

/** 0 to 20 dB of reduction: half dB marks where the needle usually is, red from 12 dB. */
hnl::ui::MeterScale reductionScale() {
    hnl::ui::MeterScale s;
    for (int half = 0; half <= static_cast<int>(kMeterTop) * 2; ++half) {
        if (half % 2 != 0 && half > 16) continue;
        s.marks.push_back({half / (kMeterTop * 2), half % 8 == 0 ? 2 : (half % 2 == 0 ? 1 : 0), std::to_string(half / 2)});
    }
    s.hot = 12.0f / kMeterTop;
    s.warn = 8.0f / kMeterTop;
    s.hotLabel = "HARD";
    return s;
}

struct Knob { uint32_t id; float x, y, r; const char* label; };
const Knob kKnobs[] = {
    {kThreshold, 69, 372, 22, "THRESHOLD"}, {kAttack, 183, 372, 22, "ATTACK"}, {kRelease, 296, 372, 22, "RELEASE"},
    {kMakeup, 410, 372, 22, "MAKE-UP"}, {kColour, 524, 372, 22, "COLOUR"},
    {kScHp, 637, 372, 22, "SIDECHAIN HP"}, {kMix, 751, 372, 22, "MIX"},
};
const int kKnobCount = sizeof(kKnobs) / sizeof(kKnobs[0]);

struct Selector { uint32_t id; Rect box; const char* label; };
const Selector kSelectors[] = {
    {kType, {24, 262, 200, 26}, "TYPE"}, {kRatio, {236, 262, 280, 26}, "RATIO"},
};

const float kPresetX = 552, kHeaderY = 14; // the preset bar ends 10 px before the help button
const hnl::ui::Theme kTheme = {kBg, kPanel, kPanelEdge, kTrack, 0x262d29, 0x414c46, kText, kDim, kAccent};

std::string upper(const char* s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
    return out;
}

} // namespace

class LevellerUI : public UI {
public:
    LevellerUI() : UI(UI_BASE_WIDTH, UI_BASE_HEIGHT, false) {
        for (uint32_t i = 0; i < kParamCount; ++i) values_[i] = kParams[i].def;
        loadSharedResources();
        const double s = getScaleFactor();
        if (s != 1.0) setSize(static_cast<uint>(std::lround(UI_BASE_WIDTH * s)), static_cast<uint>(std::lround(UI_BASE_HEIGHT * s)));

        // Help: wide areas first, single controls on top.
        help_.setButton(UI_BASE_WIDTH - 24, kHeaderY + 12);
        help_.add(help_.buttonBox(), "HELP", help::kHelp);
        help_.add(presetBar_.nameBox(), "PRESETS", help::kPresets);
        help_.add({kFace_.x, kFace_.y, kFace_.w, kFace_.h}, "GAIN REDUCTION", help::kGr);
        help_.add(meter_.area(), "METER DESIGN", hnl::ui::MeterSwitch::help());
        help_.add({kLevelPanel.x, kLevelPanel.y, kLevelPanel.w, kLevelPanel.h}, "LEVELS", help::kLevels);
        for (const Selector& sel : kSelectors)
            help_.add({sel.box.x, sel.box.y - 20, sel.box.w, sel.box.h + 20}, kParams[sel.id].name, help::forParam(sel.id));
        for (const Knob& k : kKnobs)
            help_.add({k.x - k.r - 12, k.y - k.r - 20, 2 * k.r + 24, 2 * k.r + 42}, kParams[k.id].name, help::forParam(k.id));
    }

protected:
    void parameterChanged(uint32_t index, float value) override {
        if (index >= kParamCount || values_[index] == value) return;
        values_[index] = value;
        repaint();
    }

    void stateChanged(const char* key, const char* value) override {
        if (std::strcmp(key, "meter") == 0) {
            meter_.set(value);
            repaint();
            return;
        }
        if (std::strcmp(key, "preset") != 0) return;
        presetName_ = value;
        list_.rescan();
        preset_ = list_.find(presetName_);
        repaint();
    }

    // ------------------------------------------------------------ drawing

    void onNanoDisplay() override {
        const float s = static_cast<float>(getScaleFactor());
        scale(s, s);
        fontFace(NANOVG_DEJAVU_SANS_TTF);

        beginPath();
        rect(0, 0, UI_BASE_WIDTH, UI_BASE_HEIGHT);
        fillColor(rgb(kBg));
        fill();

        hnl::ui::title(*this, "LEVELLER", rgb(kText), rgb(kDim));
        std::string presetText = presetName_;
        if (presetModified()) presetText += " *";
        if (!presetNote_.empty()) presetText = presetNote_;
        presetBar_.drawBar(*this, kTheme, presetText.c_str());
        help_.drawButton(*this, kTheme);

        for (const Rect* p : {&kMeterPanel, &kLevelPanel, &kTimePanel, &kKnobPanel}) panel(*p);

        char figure[16];
        std::snprintf(figure, sizeof(figure), "%.1f", values_[kMeterGr] < 0.0f ? 0.0f : values_[kMeterGr]);
        hnl::ui::drawMeter(*this, meter_.style(), {kFace_.x, kFace_.y, kFace_.w, kFace_.h}, scale_, kMeterColours, values_[kMeterGr] / kMeterTop, figure, "GAIN REDUCTION");
        meter_.draw(*this, kTheme);
        drawLevels();

        for (const Selector& sel : kSelectors) drawSelector(sel);
        drawCharacter();

        for (float x : {353.0f, 580.5f}) {
            beginPath();
            moveTo(x, kKnobPanel.y + 16);
            lineTo(x, kKnobPanel.y + kKnobPanel.h - 16);
            strokeColor(rgb(kPanelEdge));
            strokeWidth(1);
            stroke();
        }
        for (const Knob& k : kKnobs) drawKnob(k);

        presetBar_.drawMenu(*this, kTheme, list_.count(), preset_, list_.factoryCount(), canSavePreset(), [this](int i) { return list_.name(i); });
        if (!presetBar_.isOpen() && drag_ < 0) help_.drawTip(*this, kTheme, UI_BASE_WIDTH, UI_BASE_HEIGHT);
    }

    // -------------------------------------------------------- interaction

    bool onMouse(const MouseEvent& ev) override {
        if (ev.button != 1) return false;
        const float s = static_cast<float>(getScaleFactor());
        const float x = static_cast<float>(ev.pos.getX()) / s, y = static_cast<float>(ev.pos.getY()) / s;

        if (!ev.press) {
            if (drag_ < 0) return false;
            editParameter(kKnobs[drag_].id, false);
            drag_ = -1;
            return true;
        }

        presetNote_.clear();
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
        if (meter_.mouseDown(x, y)) {
            setState("meter", meter_.name());
            repaint();
            return true;
        }
        if (help_.buttonHit(x, y)) {
            help_.on = !help_.on;
            help_.motion(x, y);
            repaint();
            return true;
        }

        for (const Selector& sel : kSelectors) {
            if (!sel.box.contains(x, y)) continue;
            const int count = static_cast<int>(kParams[sel.id].max) + 1;
            const int choice = static_cast<int>((x - sel.box.x) / (sel.box.w / count));
            change(sel.id, static_cast<float>(choice < 0 ? 0 : (choice >= count ? count - 1 : choice)), true);
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
        const float s = static_cast<float>(getScaleFactor());
        const float x = static_cast<float>(ev.pos.getX()) / s, y = static_cast<float>(ev.pos.getY()) / s;
        if (presetBar_.isOpen()) {
            if (presetBar_.motion(x, y, list_.count())) repaint();
            return true;
        }
        if (drag_ < 0) {
            if (help_.motion(x, y)) repaint();
            return false;
        }
        const float range = (ev.mod & kModifierShift) ? 1200.0f : 200.0f;
        dragNorm_ += (dragY_ - y) / range;
        dragNorm_ = dragNorm_ < 0.0f ? 0.0f : (dragNorm_ > 1.0f ? 1.0f : dragNorm_);
        dragY_ = y;
        change(kKnobs[drag_].id, normToParam(kKnobs[drag_].id, dragNorm_), false);
        return true;
    }

    bool onKeyboard(const KeyboardEvent& ev) override {
        if (!ev.press) return presetBar_.isEditing();
        const int result = presetBar_.key(ev.key);
        if (result == hnl::ui::PresetBar::kNotMine) return false;
        presetAction(result);
        repaint();
        return true;
    }

    bool onCharacterInput(const CharacterInputEvent& ev) override {
        if (!presetBar_.character(ev.string, ev.character)) return false;
        repaint();
        return true;
    }

    bool onScroll(const ScrollEvent& ev) override {
        if (presetBar_.isBusy() || drag_ >= 0) return false;
        const float s = static_cast<float>(getScaleFactor());
        const int hit = knobAt(static_cast<float>(ev.pos.getX()) / s, static_cast<float>(ev.pos.getY()) / s);
        if (hit < 0) return false;
        const uint32_t id = kKnobs[hit].id;
        const float step = (ev.mod & kModifierShift) ? 0.004f : 0.02f;
        change(id, normToParam(id, paramToNorm(id, values_[id]) + step * static_cast<float>(ev.delta.getY())), true);
        return true;
    }

private:
    int knobAt(float x, float y) const {
        for (int i = 0; i < kKnobCount; ++i)
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

    /** True when a control was moved since the preset was chosen. */
    bool presetModified() const {
        if (preset_ < 0) return false;
        for (uint32_t id = 0; id < kInputParamCount; ++id) {
            const float tolerance = 1e-4f * (kParams[id].max - kParams[id].min);
            if (presets::inPreset(id) && std::fabs(values_[id] - list_.value(preset_, id)) > tolerance) return true;
        }
        return false;
    }

    void label(const char* text, float x, float y, float size, const Color& colour, int align) {
        fontSize(size);
        textAlign(align);
        fillColor(colour);
        this->text(x, y, text, nullptr);
    }

    void panel(const Rect& p) {
        beginPath();
        roundedRect(p.x, p.y, p.w, p.h, 6);
        fillColor(rgb(kPanel));
        fill();
        strokeColor(rgb(kPanelEdge));
        strokeWidth(1);
        stroke();
    }

    // ---------------------------------------------- gain reduction meter

    /** A needle that rests at 0 on the left and swings right with the reduction, linear in dB. */
    // ------------------------------------------------------------- levels

    static float levelPos(float db) { // -48..+6 dB -> 0..1
        const float t = (db + 48.0f) / 54.0f;
        return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    }

    void drawLevels() {
        const Rect& p = kLevelPanel;
        const float x = p.x + 62, w = p.w - 78;
        label("LEVELS", p.x + 16, p.y + 16, 10, rgb(kText), ALIGN_LEFT | ALIGN_MIDDLE);
        label("dBFS", p.x + p.w - 16, p.y + 16, 8, rgb(kDim), ALIGN_RIGHT | ALIGN_MIDDLE);
        static const char* const kRows[4] = {"IN L", "IN R", "OUT L", "OUT R"};
        static const uint32_t kMeters[4] = {kMeterInL, kMeterInR, kMeterOutL, kMeterOutR};
        for (int row = 0; row < 4; ++row) {
            const float y = p.y + 32 + row * 15 + (row >= 2 ? 8 : 0), top = levelPos(values_[kMeters[row]]);
            label(kRows[row], p.x + 16, y + 5, 8, rgb(kDim), ALIGN_LEFT | ALIGN_MIDDLE);
            beginPath();
            rect(x, y, w, 10);
            fillColor(rgb(kWell));
            fill();
            struct Zone { float from, to; uint32_t colour; };
            const Zone zones[3] = {{-48, -12, kGreen}, {-12, -3, kYellow}, {-3, 6, kRed}};
            for (const Zone& z : zones) {
                const float a = levelPos(z.from), b = std::min(levelPos(z.to), top);
                if (b <= a) continue;
                beginPath();
                rect(x + w * a, y, w * (b - a), 10);
                fillColor(rgb(z.colour));
                fill();
            }
        }
        static const int kMarks[] = {-48, -36, -24, -12, -6, 0, 6};
        for (int db : kMarks) {
            char text[8];
            std::snprintf(text, sizeof(text), "%d", db);
            label(text, x + w * levelPos(static_cast<float>(db)), p.y + 108, 8, rgb(kDim), ALIGN_CENTER | ALIGN_MIDDLE);
        }

        // Peaks in and out, and the reduction as a number.
        const float in = std::max(values_[kMeterInL], values_[kMeterInR]), out = std::max(values_[kMeterOutL], values_[kMeterOutR]);
        readout("IN PEAK", in, in <= -99.0f, in > -0.05f, p.x + 16);
        readout("OUT PEAK", out, out <= -99.0f, out > -0.05f, p.x + 176);
        readout("REDUCTION", values_[kMeterGr], false, false, p.x + 336);
    }

    void readout(const char* name, float value, bool none, bool hot, float x) {
        const float y = kLevelPanel.y + 144;
        char text[24];
        if (none) std::snprintf(text, sizeof(text), "--");
        else std::snprintf(text, sizeof(text), "%.1f", value);
        label(name, x, y - 12, 8, rgb(kDim), ALIGN_LEFT | ALIGN_MIDDLE);
        label(text, x, y + 4, 16, rgb(hot ? kRed : kText), ALIGN_LEFT | ALIGN_MIDDLE);
        label("dB", x + 56, y + 6, 8, rgb(kDim), ALIGN_LEFT | ALIGN_MIDDLE);
    }

    // ----------------------------------------------------------- controls

    int choice(uint32_t id) const { return static_cast<int>(std::lround(values_[id])); }

    void drawSelector(const Selector& sel) {
        const Rect& b = sel.box;
        const int count = static_cast<int>(kParams[sel.id].max) + 1, active = choice(sel.id);
        const float w = b.w / count;
        label(sel.label, b.x, b.y - 13, 9, rgb(kDim), ALIGN_LEFT | ALIGN_MIDDLE);
        beginPath();
        roundedRect(b.x, b.y, b.w, b.h, 4);
        fillColor(rgb(kTrack));
        fill();
        for (int i = 0; i < count; ++i) {
            if (i == active) {
                beginPath();
                roundedRect(b.x + i * w + 2, b.y + 2, w - 4, b.h - 4, 3);
                fillColor(rgb(kAccent));
                fill();
            }
            label(upper(kParams[sel.id].choices[i]).c_str(), b.x + (i + 0.5f) * w, b.y + b.h * 0.5f, 9, rgb(i == active ? kBg : kText),
                  ALIGN_CENTER | ALIGN_MIDDLE);
        }
    }

    /** In words, what the chosen type does with the current Attack and Release. */
    void drawCharacter() {
        const int type = choice(kType);
        const hnl::LevellerTiming t = hnl::levellerTiming(type, values_[kAttack], values_[kRelease]);
        char text[96];
        if (type == hnl::kOpto) std::snprintf(text, sizeof(text), "RELEASE IN TWO STAGES: %.0f MS, THEN UP TO %.1f S", t.releaseMs, t.tailMs * 0.001);
        else std::snprintf(text, sizeof(text), "ONE RELEASE TIME, TIGHT KNEE");
        const float x = 540;
        label(type == hnl::kOpto ? "OPTO: SLOW AND SMOOTH" : "FET: FAST AND FIRM", x, kTimePanel.y + 28, 10, rgb(kAccent), ALIGN_LEFT | ALIGN_MIDDLE);
        label(text, x, kTimePanel.y + 46, 8, rgb(kDim), ALIGN_LEFT | ALIGN_MIDDLE);
    }

    static void formatMs(char* text, size_t size, double ms) {
        if (ms < 1.0) std::snprintf(text, size, "%.2f ms", ms);
        else if (ms < 10.0) std::snprintf(text, size, "%.1f ms", ms);
        else if (ms < 1000.0) std::snprintf(text, size, "%.0f ms", ms);
        else std::snprintf(text, size, "%.2f s", ms * 0.001);
    }

    void drawKnob(const Knob& k) {
        const float a0 = 0.75f * kPi, a1 = 2.25f * kPi;
        const float angle = a0 + paramToNorm(k.id, values_[k.id]) * (a1 - a0);

        beginPath();
        arc(k.x, k.y, k.r, a0, a1, CW);
        strokeColor(rgb(kTrack));
        strokeWidth(3.5f);
        lineCap(ROUND);
        stroke();

        if (angle - a0 > 0.01f) {
            beginPath();
            arc(k.x, k.y, k.r, a0, angle, CW);
            strokeColor(rgb(kAccent));
            strokeWidth(3.5f);
            stroke();
        }

        beginPath();
        circle(k.x, k.y, k.r - 6);
        fillColor(rgb(0x2a2e35));
        fill();
        beginPath();
        moveTo(k.x + std::cos(angle) * (k.r - 14), k.y + std::sin(angle) * (k.r - 14));
        lineTo(k.x + std::cos(angle) * (k.r - 7), k.y + std::sin(angle) * (k.r - 7));
        strokeColor(rgb(kText));
        strokeWidth(2);
        stroke();

        label(k.label, k.x, k.y - k.r - 14, 9, rgb(kDim), ALIGN_CENTER | ALIGN_MIDDLE);
        char text[32];
        const float v = values_[k.id];
        const hnl::LevellerTiming t = hnl::levellerTiming(choice(kType), values_[kAttack], values_[kRelease]);
        if (k.id == kThreshold) std::snprintf(text, sizeof(text), "%.1f dB", v);
        else if (k.id == kAttack) formatMs(text, sizeof(text), t.attackMs);
        else if (k.id == kRelease) formatMs(text, sizeof(text), t.releaseMs);
        else if (k.id == kMakeup) std::snprintf(text, sizeof(text), "+%.1f dB", v);
        else if (k.id == kScHp && v < 20.5f) std::snprintf(text, sizeof(text), "OFF");
        else if (k.id == kScHp) std::snprintf(text, sizeof(text), "%.0f Hz", v);
        else std::snprintf(text, sizeof(text), "%.0f %%", v);
        label(text, k.x, k.y + k.r + 15, 11, rgb(kText), ALIGN_CENTER | ALIGN_MIDDLE);
    }

    float values_[kParamCount];
    int drag_ = -1, lastClick_ = -1;
    uint lastClickTime_ = 0;
    float dragY_ = 0.0f, dragNorm_ = 0.0f;

    int preset_ = 0;
    std::string presetName_ = "Default";
    hnl::ui::PresetBar presetBar_{kPresetX, kHeaderY};
    const hnl::ui::MeterScale scale_ = reductionScale();
    hnl::ui::MeterSwitch meter_{kFace_.x, kFace_.x + kFace_.w, kMeterSwitchY};
    hnl::PresetList list_ = presets::list();
    std::string presetNote_; // shown in place of the name until the next click
    hnl::ui::HelpTips help_;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LevellerUI)
};

UI* createUI() { return new LevellerUI(); }

END_NAMESPACE_DISTRHO
