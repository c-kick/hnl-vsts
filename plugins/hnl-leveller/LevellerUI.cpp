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

#include "../../common/ui/Skin.hpp"
#include "../../common/ui/WindowSize.hpp"
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

// The classic look in this plugin's own colours; the other looks are the same for every plugin (common/ui/Skin.hpp).
const hnl::ui::Skin kClassic = hnl::ui::classicSkin(0x151816, 0x1d221f, 0x2d3531, 0x35403a, 0x0c0f0d, 0xe6ebe8, 0x8a9690, 0x63d2b4, 0x262d29, 0x414c46);

const float kMeterTop = 20.0f; // dB of reduction at the right end of the scale

typedef hnl::ui::Box Rect;

const Rect kMeterPanel = {12, 52, 300, 170}, kLevelPanel = {320, 52, 488, 170};
const Rect kTimePanel = {12, 230, 796, 70}, kKnobPanel = {12, 308, 796, 120};
const Rect kFace_ = {24, 62, 276, 138};
const Rect kCharacter = {528, 240, 270, 50}; // what the type does, in words: a display of its own in the solid looks

const float kMeterSwitchY = 204; // the strip between the face and the lower edge of its panel

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

const float kPresetX = 524, kHeaderY = 14; // the preset bar ends 10 px before the help button
const float kHelpX = UI_BASE_WIDTH - 52, kSkinX = UI_BASE_WIDTH - 24; // help, and right of it the choice of look

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
        size_.open(skin_); // the window opens at the size it was left at

        // Help: wide areas first, single controls on top.
        help_.setButton(kHelpX, kHeaderY + 12);
        skin_.setButton(kSkinX, kHeaderY + 12);
        help_.add(help_.buttonBox(), "HELP", help::kHelp);
        help_.add(skin_.buttonBox(), "LOOK AND SIZE", hnl::ui::SkinMenu::helpWithSizes());
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

    void uiIdle() override {
        if (skin_.poll()) repaint(); // another HNL window chose a look
        if (size_.follow(skin_)) repaint(); // the window was dragged to another size
    }

    // ------------------------------------------------------------ drawing

    void onNanoDisplay() override {
        const float s = size_.zoom();
        scale(s, s);
        fontFace(NANOVG_DEJAVU_SANS_TTF);

        const hnl::ui::Skin k = skin();
        const hnl::ui::Theme theme = k.theme();
        hnl::ui::faceplate(*this, k, UI_BASE_WIDTH, UI_BASE_HEIGHT);

        hnl::ui::title(*this, "LEVELLER", rgb(k.plateText), rgb(k.plateDim));
        std::string presetText = presetName_;
        if (presetModified()) presetText += " *";
        if (!presetNote_.empty()) presetText = presetNote_;
        presetBar_.drawBar(*this, theme, presetText.c_str());
        help_.drawButton(*this, theme);
        skin_.drawButton(*this, theme);

        for (const Rect* p : {&kMeterPanel, &kLevelPanel}) hnl::ui::displayPanel(*this, k, *p);
        for (const Rect* p : {&kTimePanel, &kKnobPanel}) hnl::ui::platePanel(*this, k, *p);

        char figure[16];
        std::snprintf(figure, sizeof(figure), "%.1f", values_[kMeterGr] < 0.0f ? 0.0f : values_[kMeterGr]);
        hnl::ui::drawMeter(*this, meter_.style(), kFace_, scale_, k.meter(), values_[kMeterGr] / kMeterTop, figure, "GAIN REDUCTION");
        if (meter_.style() == hnl::ui::kMeterClassic) hnl::ui::lamp(*this, k, kFace_);
        meter_.draw(*this, theme);
        drawLevels();

        for (const Selector& sel : kSelectors) drawSelector(sel);
        drawCharacter();

        for (float x : {353.0f, 580.5f}) hnl::ui::groove(*this, k, x, kKnobPanel.y + 16, x, kKnobPanel.y + kKnobPanel.h - 16);
        for (const Knob& knob : kKnobs) drawKnob(knob);

        presetBar_.drawMenu(*this, theme, list_.count(), preset_, list_.factoryCount(), canSavePreset(), [this](int i) { return list_.name(i); });
        skin_.drawMenu(*this, theme);
        if (!presetBar_.isOpen() && !skin_.isOpen() && drag_ < 0) help_.drawTip(*this, theme, UI_BASE_WIDTH, UI_BASE_HEIGHT);
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
        const float s = size_.zoom();
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

    /** The look: in every one of them what is on or lit has this plugin's own colour. */
    hnl::ui::Skin skin() const { return skin_.skin() == hnl::ui::kSkinClassic ? kClassic : hnl::ui::solidSkin(skin_.skin(), kClassic.accent); }

    void label(const char* text, float x, float y, float size, const Color& colour, int align) {
        fontSize(size);
        textAlign(align);
        fillColor(colour);
        this->text(x, y, text, nullptr);
    }

    // ---------------------------------------------- gain reduction meter

    /** A needle that rests at 0 on the left and swings right with the reduction, linear in dB. */
    // ------------------------------------------------------------- levels

    static float levelPos(float db) { // -48..+6 dB -> 0..1
        const float t = (db + 48.0f) / 54.0f;
        return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    }

    void drawLevels() {
        const hnl::ui::Skin k = skin();
        const Rect& p = kLevelPanel;
        const float x = p.x + 62, w = p.w - 78;
        label("LEVELS", p.x + 16, p.y + 16, 10, rgb(k.text), ALIGN_LEFT | ALIGN_MIDDLE);
        label("dBFS", p.x + p.w - 16, p.y + 16, 8, rgb(k.dim), ALIGN_RIGHT | ALIGN_MIDDLE);
        static const char* const kRows[4] = {"IN L", "IN R", "OUT L", "OUT R"};
        static const uint32_t kMeters[4] = {kMeterInL, kMeterInR, kMeterOutL, kMeterOutR};
        for (int row = 0; row < 4; ++row) {
            const float y = p.y + 32 + row * 15 + (row >= 2 ? 8 : 0), top = levelPos(values_[kMeters[row]]);
            label(kRows[row], p.x + 16, y + 5, 8, rgb(k.dim), ALIGN_LEFT | ALIGN_MIDDLE);
            beginPath();
            rect(x, y, w, 10);
            fillColor(rgb(k.solid ? k.track : k.well)); // solid: the segments that are not lit
            fill();
            struct Zone { float from, to; uint32_t colour; };
            const Zone zones[3] = {{-48, -12, k.green}, {-12, -3, k.yellow}, {-3, 6, k.red}};
            for (const Zone& z : zones) {
                const float a = levelPos(z.from), b = std::min(levelPos(z.to), top);
                if (b <= a) continue;
                beginPath();
                rect(x + w * a, y, w * (b - a), 10);
                fillColor(rgb(z.colour));
                fill();
            }
            hnl::ui::segments(*this, k, x, y, w, 10);
        }
        static const int kMarks[] = {-48, -36, -24, -12, -6, 0, 6};
        for (int db : kMarks) {
            char text[8];
            std::snprintf(text, sizeof(text), "%d", db);
            label(text, x + w * levelPos(static_cast<float>(db)), p.y + 108, 8, rgb(k.dim), ALIGN_CENTER | ALIGN_MIDDLE);
        }

        // Peaks in and out, and the reduction as a number.
        const float in = std::max(values_[kMeterInL], values_[kMeterInR]), out = std::max(values_[kMeterOutL], values_[kMeterOutR]);
        readout("IN PEAK", in, in <= -99.0f, in > -0.05f, p.x + 16);
        readout("OUT PEAK", out, out <= -99.0f, out > -0.05f, p.x + 176);
        readout("REDUCTION", values_[kMeterGr], false, false, p.x + 336);
    }

    void readout(const char* name, float value, bool none, bool hot, float x) {
        const hnl::ui::Skin k = skin();
        const float y = kLevelPanel.y + 144;
        char text[24];
        if (none) std::snprintf(text, sizeof(text), "--");
        else std::snprintf(text, sizeof(text), "%.1f", value);
        label(name, x, y - 12, 8, rgb(k.dim), ALIGN_LEFT | ALIGN_MIDDLE);
        label(text, x, y + 4, 16, rgb(hot ? k.red : k.text), ALIGN_LEFT | ALIGN_MIDDLE);
        label("dB", x + 56, y + 6, 8, rgb(k.dim), ALIGN_LEFT | ALIGN_MIDDLE);
    }

    // ----------------------------------------------------------- controls

    int choice(uint32_t id) const { return static_cast<int>(std::lround(values_[id])); }

    void drawSelector(const Selector& sel) {
        const hnl::ui::Skin k = skin();
        const Rect& b = sel.box;
        label(sel.label, b.x, b.y - 13, 9, rgb(k.plateDim), ALIGN_LEFT | ALIGN_MIDDLE);
        std::string text;
        hnl::ui::keys(*this, k, b, static_cast<int>(kParams[sel.id].max) + 1, choice(sel.id), [&](int i) {
            text = upper(kParams[sel.id].choices[i]);
            return text.c_str();
        });
    }

    /** In words, what the chosen type does with the current Attack and Release. */
    void drawCharacter() {
        const int type = choice(kType);
        const hnl::LevellerTiming t = hnl::levellerTiming(type, values_[kAttack], values_[kRelease]);
        char text[96];
        if (type == hnl::kOpto) std::snprintf(text, sizeof(text), "RELEASE IN TWO STAGES: %.0f MS, THEN UP TO %.1f S", t.releaseMs, t.tailMs * 0.001);
        else std::snprintf(text, sizeof(text), "ONE RELEASE TIME, TIGHT KNEE");
        const hnl::ui::Skin k = skin();
        if (k.solid) hnl::ui::displayPanel(*this, k, kCharacter); // light on dark needs its own window in the plate
        const float x = 540;
        label(type == hnl::kOpto ? "OPTO: SLOW AND SMOOTH" : "FET: FAST AND FIRM", x, kTimePanel.y + 28, 10, rgb(k.accent), ALIGN_LEFT | ALIGN_MIDDLE);
        label(text, x, kTimePanel.y + 46, 8, rgb(k.dim), ALIGN_LEFT | ALIGN_MIDDLE);
    }

    static void formatMs(char* text, size_t size, double ms) {
        if (ms < 1.0) std::snprintf(text, size, "%.2f ms", ms);
        else if (ms < 10.0) std::snprintf(text, size, "%.1f ms", ms);
        else if (ms < 1000.0) std::snprintf(text, size, "%.0f ms", ms);
        else std::snprintf(text, size, "%.2f s", ms * 0.001);
    }

    void drawKnob(const Knob& k) {
        const hnl::ui::Skin look = skin();
        hnl::ui::knob(*this, look, k.x, k.y, k.r, paramToNorm(k.id, values_[k.id]));
        label(k.label, k.x, k.y - k.r - 14, 9, rgb(look.plateDim), ALIGN_CENTER | ALIGN_MIDDLE);
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
        label(text, k.x, k.y + k.r + 15, 11, rgb(look.plateText), ALIGN_CENTER | ALIGN_MIDDLE);
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
    hnl::ui::SkinMenu skin_;
    hnl::ui::WindowSize<LevellerUI> size_{*this, "leveller", UI_BASE_WIDTH, UI_BASE_HEIGHT};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LevellerUI)
};

UI* createUI() { return new LevellerUI(); }

END_NAMESPACE_DISTRHO
