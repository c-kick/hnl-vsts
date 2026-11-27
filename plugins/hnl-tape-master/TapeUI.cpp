// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// HNL Tape Master: user interface (NanoVG). All coordinates are in a fixed
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
#include "Help.hpp"
#include "Params.hpp"
#include "Presets.hpp"

START_NAMESPACE_DISTRHO

using DGL_NAMESPACE::Color;

namespace {

Color rgb(uint32_t hex, float alpha = 1.0f) {
    return Color(((hex >> 16) & 255) / 255.0f, ((hex >> 8) & 255) / 255.0f, (hex & 255) / 255.0f, alpha);
}

/** The classic look in this plugin's own colours; the other looks are the same for every plugin (common/ui/Skin.hpp). */
hnl::ui::Skin classicLook() {
    hnl::ui::Skin k = hnl::ui::classicSkin(0x161514, 0x201e1c, 0x302d29, 0x38342f, 0x0f0e0d, 0xece6dc, 0x968e82, 0xe0a458, 0x2b2825, 0x45403a);
    k.knobHi = k.knobLo = k.knobEdge = 0x2b2825;
    k.face = 0xe9dfc4; // the meter card is a warmer paper than that of the other plugins
    k.faceEdge = 0xb8ad90;
    k.ink = 0x24211d;
    return k;
}
const hnl::ui::Skin kClassic = classicLook();

typedef hnl::ui::Box Rect;

const Rect kMeterPanel = {12, 52, 536, 170}, kOutPanel = {556, 52, 252, 170};
const Rect kTapePanel = {12, 230, 796, 70}, kKnobPanel = {12, 308, 796, 120};
const Rect kFaces[2] = {{24, 62, 250, 138}, {286, 62, 250, 138}};

const float kMeterSwitchY = 204; // the strip between the faces and the lower edge of their panel

/** Needle position 0..1 for a level in VU: linear in voltage, like the real thing. */
float vuPos(float db) {
    const float t = std::pow(10.0f, db / 20.0f) / 1.4125f; // +3 VU at the end of the scale
    return t < 0.0f ? 0.0f : (t > 1.06f ? 1.06f : t);
}

/** -20 to +3 VU, red above 0. */
hnl::ui::MeterScale vuScale() {
    hnl::ui::MeterScale s;
    for (int mark : {-20, -10, -7, -5, -3, -2, -1, 0, 1, 2, 3}) {
        const bool major = mark == -20 || mark == -10 || mark == -7 || mark == -5 || mark == -3 || mark == 0 || mark == 3;
        char text[8];
        std::snprintf(text, sizeof(text), mark > 0 ? "+%d" : "%d", mark < 0 ? -mark : mark);
        s.marks.push_back({vuPos(static_cast<float>(mark)), major ? 2 : 1, text});
    }
    s.hot = vuPos(0);
    s.warn = s.hot; // no yellow lights: they would hardly differ from the copper ones
    s.inkFrom = vuPos(-20);
    s.high = 1.06f;
    s.span = 0.66f;
    s.unit = "VU";
    return s;
}

struct Knob { uint32_t id; float x, y, r; const char* label; };
const Knob kKnobs[] = {
    {kRecord, 84, 372, 22, "RECORD"}, {kReproduce, 236, 372, 22, "REPRODUCE"},
    {kCalibration, 384, 372, 22, "CALIBRATION"}, {kBias, 478, 372, 22, "BIAS"},
    {kWow, 592, 372, 22, "WOW"}, {kFlutter, 676, 372, 22, "FLUTTER"}, {kNoise, 760, 372, 22, "NOISE"},
};
const int kKnobCount = sizeof(kKnobs) / sizeof(kKnobs[0]);

struct Selector { uint32_t id; Rect box; const char* label; };
const Selector kSelectors[] = {
    {kSpeed, {24, 262, 150, 26}, "SPEED (IPS)"}, {kFormula, {186, 262, 296, 26}, "TAPE FORMULA"},
    {kHead, {494, 262, 140, 26}, "HEAD WIDTH"}, {kPath, {646, 262, 150, 26}, "PATH"},
};

const float kLinkX = 160, kLinkY = 372;
const float kPresetX = 524, kHeaderY = 14; // the preset bar ends 10 px before the help button
const float kHelpX = UI_BASE_WIDTH - 52, kSkinX = UI_BASE_WIDTH - 24; // help, and right of it the choice of look

std::string upper(const char* s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
    return out;
}

} // namespace

class TapeUI : public UI {
public:
    TapeUI() : UI(UI_BASE_WIDTH, UI_BASE_HEIGHT, false) {
        for (uint32_t i = 0; i < kParamCount; ++i) values_[i] = kParams[i].def;
        loadSharedResources();
        size_.open(skin_); // the window opens at the size it was left at

        // Help: wide areas first, single controls on top.
        using hnl::ui::Box;
        help_.setButton(kHelpX, kHeaderY + 12);
        skin_.setButton(kSkinX, kHeaderY + 12);
        help_.add(help_.buttonBox(), "HELP", help::kHelp);
        help_.add(skin_.buttonBox(), "LOOK AND SIZE", hnl::ui::SkinMenu::helpWithSizes());
        help_.add(presetBar_.nameBox(), "PRESETS", help::kPresets);
        help_.add(meter_.area(), "METER DESIGN", hnl::ui::MeterSwitch::help());
        for (const Rect& f : kFaces) help_.add({f.x, f.y, f.w, f.h}, "VU METER", help::kVu);
        help_.add({kOutPanel.x, kOutPanel.y, kOutPanel.w, kOutPanel.h}, "OUTPUT", help::kOutput);
        for (const Selector& sel : kSelectors)
            help_.add({sel.box.x, sel.box.y - 20, sel.box.w, sel.box.h + 20}, kParams[sel.id].name, help::forParam(sel.id));
        help_.add({kLinkX - 22, kLinkY - 28, 44, 40}, kParams[kLink].name, help::forParam(kLink));
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

        hnl::ui::title(*this, "TAPE MASTER", rgb(k.plateText), rgb(k.plateDim));
        std::string presetText = presetName_;
        if (presetModified()) presetText += " *";
        if (!presetNote_.empty()) presetText = presetNote_;
        presetBar_.drawBar(*this, theme, presetText.c_str());
        help_.drawButton(*this, theme);
        skin_.drawButton(*this, theme);

        for (const Rect* p : {&kMeterPanel, &kOutPanel}) hnl::ui::displayPanel(*this, k, *p);
        for (const Rect* p : {&kTapePanel, &kKnobPanel}) hnl::ui::platePanel(*this, k, *p);

        for (int ch = 0; ch < 2; ++ch) {
            const float vu = values_[ch == 0 ? kMeterVuL : kMeterVuR];
            char figure[16] = "--"; // nothing on tape
            if (vu > -40.0f) std::snprintf(figure, sizeof(figure), "%+.1f", vu);
            hnl::ui::drawMeter(*this, meter_.style(), kFaces[ch], scale_, k.meter(), vuPos(vu), figure, ch == 0 ? "LEFT" : "RIGHT");
            if (meter_.style() == hnl::ui::kMeterClassic) hnl::ui::lamp(*this, k, kFaces[ch]);
        }
        meter_.draw(*this, theme);
        drawOutput();

        for (const Selector& sel : kSelectors) drawSelector(sel);

        for (float x : {322.0f, 536.0f}) hnl::ui::groove(*this, k, x, kKnobPanel.y + 16, x, kKnobPanel.y + kKnobPanel.h - 16);
        for (const Knob& knob : kKnobs) drawKnob(knob);
        drawLink();

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
        if (std::fabs(x - kLinkX) <= 20 && std::fabs(y - kLinkY) <= 12) {
            change(kLink, values_[kLink] > 0.5f ? 0.0f : 1.0f, true);
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

    // ----------------------------------------------------------- VU meter

    // ------------------------------------------------------------- output

    static float levelPos(float db) { // -48..+6 dB -> 0..1
        const float t = (db + 48.0f) / 54.0f;
        return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    }

    void drawOutput() {
        const hnl::ui::Skin k = skin();
        const float x = kOutPanel.x + 32, w = kOutPanel.w - 48;
        label("OUTPUT", kOutPanel.x + 16, kOutPanel.y + 16, 10, rgb(k.text), ALIGN_LEFT | ALIGN_MIDDLE);
        for (int c = 0; c < 2; ++c) {
            const float y = kOutPanel.y + 38 + c * 18, top = levelPos(values_[kMeterOutL + c]);
            label(c == 0 ? "L" : "R", kOutPanel.x + 16, y + 5, 9, rgb(k.dim), ALIGN_LEFT | ALIGN_MIDDLE);
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
            label(text, x + w * levelPos(static_cast<float>(db)), kOutPanel.y + 78, 8, rgb(k.dim), ALIGN_CENTER | ALIGN_MIDDLE);
        }

        const float peak = std::max(values_[kMeterOutL], values_[kMeterOutR]);
        char text[48];
        if (peak <= -99.0f) std::snprintf(text, sizeof(text), "--");
        else std::snprintf(text, sizeof(text), "%.1f", peak);
        label("PEAK", kOutPanel.x + 16, kOutPanel.y + 112, 8, rgb(k.dim), ALIGN_LEFT | ALIGN_MIDDLE);
        label(text, kOutPanel.x + 54, kOutPanel.y + 111, 16, rgb(peak > -0.05f ? k.red : k.text), ALIGN_LEFT | ALIGN_MIDDLE);
        label("dBFS", kOutPanel.x + kOutPanel.w - 16, kOutPanel.y + 112, 8, rgb(k.dim), ALIGN_RIGHT | ALIGN_MIDDLE);

        // What is threaded on the machine right now.
        const int path = choice(kPath);
        std::string line;
        if (path == 2) line = "ELECTRONICS ONLY, NO TAPE";
        else line = std::string(kSpeedNames[choice(kSpeed)]) + " IPS  /  " + upper(kFormulaNames[choice(kFormula)]) + "  /  " + kHeadNames[choice(kHead)];
        label(line.c_str(), kOutPanel.x + 16, kOutPanel.y + 148, 9, rgb(k.accent), ALIGN_LEFT | ALIGN_MIDDLE);
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

    void drawLink() {
        const hnl::ui::Skin k = skin();
        const bool active = values_[kLink] > 0.5f;
        label("LINK", kLinkX, kLinkY - 20, 9, rgb(k.plateDim), ALIGN_CENTER | ALIGN_MIDDLE);
        hnl::ui::toggle(*this, k, kLinkX, kLinkY, active, k.accent);
        // Lines from the switch to the two knobs it ties together: lit when linked, in the solid looks printed on the plate.
        for (float dir : {-1.0f, 1.0f}) {
            beginPath();
            moveTo(kLinkX + dir * 20, kLinkY);
            lineTo(kLinkX + dir * 40, kLinkY);
            if (k.solid) strokeColor(active ? rgb(k.ring) : rgb(k.plateDim, 0.45f));
            else strokeColor(rgb(active ? k.accent : k.track, active ? 0.6f : 1.0f));
            strokeWidth(1.5f);
            stroke();
        }
    }

    void drawKnob(const Knob& knob) {
        const hnl::ui::Skin k = skin();
        const ParamInfo& p = kParams[knob.id];
        // Parameters centred on zero grow from the middle.
        const float from = p.min < 0.0f ? paramToNorm(knob.id, 0.0f) : 0.0f;
        hnl::ui::knob(*this, k, knob.x, knob.y, knob.r, paramToNorm(knob.id, values_[knob.id]), from, k.ring);

        label(knob.label, knob.x, knob.y - knob.r - 14, 9, rgb(k.plateDim), ALIGN_CENTER | ALIGN_MIDDLE);
        char text[32];
        const float v = values_[knob.id];
        if (knob.id == kCalibration) std::snprintf(text, sizeof(text), "%.1f dBFS", v);
        else if (p.unit[0] == 'd') std::snprintf(text, sizeof(text), "%+.1f dB", v);
        else if (knob.id == kBias) std::snprintf(text, sizeof(text), "%+.0f %%", v);
        else if (knob.id == kNoise && v < 0.5f) std::snprintf(text, sizeof(text), "OFF");
        else std::snprintf(text, sizeof(text), "%.0f %%", v);
        label(text, knob.x, knob.y + knob.r + 15, 11, rgb(k.plateText), ALIGN_CENTER | ALIGN_MIDDLE);
    }

    float values_[kParamCount];
    int drag_ = -1, lastClick_ = -1;
    uint lastClickTime_ = 0;
    float dragY_ = 0.0f, dragNorm_ = 0.0f;

    int preset_ = 0;
    std::string presetName_ = "Default";
    hnl::ui::PresetBar presetBar_{kPresetX, kHeaderY};
    const hnl::ui::MeterScale scale_ = vuScale();
    hnl::ui::MeterSwitch meter_{kFaces[0].x, kFaces[1].x + kFaces[1].w, kMeterSwitchY};
    hnl::PresetList list_ = presets::list();
    std::string presetNote_; // shown in place of the name until the next click
    hnl::ui::HelpTips help_;
    hnl::ui::SkinMenu skin_;
    hnl::ui::WindowSize<TapeUI> size_{*this, "tape-master", UI_BASE_WIDTH, UI_BASE_HEIGHT};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TapeUI)
};

UI* createUI() { return new TapeUI(); }

END_NAMESPACE_DISTRHO
