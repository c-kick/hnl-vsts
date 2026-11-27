// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// HNL Channel Spacer: user interface (NanoVG). All coordinates are in a fixed
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

// The classic look in this plugin's own colours; the other looks are the same for every plugin (common/ui/Skin.hpp).
// Blue is the plugin's colour: the ducker, and what is on. Orange is what is cut, in every look.
hnl::ui::Skin classicLook() {
    hnl::ui::Skin k = hnl::ui::classicSkin(0x13141a, 0x1b1d26, 0x2b2e3c, 0x343848, 0x0c0d11, 0xe4e7f2, 0x8a8fa6, 0x8fa8ff, 0x252836, 0x3e4257);
    k.knobHi = k.knobLo = k.knobEdge = 0x252836;
    return k;
}
const hnl::ui::Skin kClassic = classicLook();
const uint32_t kOrange = 0xffa24d;

typedef hnl::ui::Box Rect;

const Rect kGraphPanel = {12, 52, 628, 210}, kOutPanel = {648, 52, 160, 210};
const Rect kSwitchPanel = {12, 270, 796, 70}, kKnobPanel = {12, 348, 796, 120};
const Rect kWarning = {603, 303, 197, 24}; // "you are not hearing the output": a display of its own in the solid looks

// The display: 20 Hz to 20 kHz. Levels of the ducker rise from the bottom, cuts hang from the top.
const Rect kPlot = {52, 84, 574, 150};
const float kLevelFloor = -90.0f, kCutScale = 30.0f;

struct Knob { uint32_t id; float x, y, r; const char* label; };
const Knob kKnobs[] = {
    {kSensitivity, 60, 412, 22, "SENSITIVITY"}, {kDepth, 146, 412, 22, "DEPTH"},
    {kAttack, 232, 412, 22, "ATTACK"}, {kRelease, 318, 412, 22, "RELEASE"},
    {kLow, 420, 412, 22, "LOW"}, {kLowMid, 506, 412, 22, "LOW MID"},
    {kHighMid, 592, 412, 22, "HIGH MID"}, {kHigh, 678, 412, 22, "HIGH"},
    {kLink, 764, 412, 22, "LINK"},
};
const int kKnobCount = sizeof(kKnobs) / sizeof(kKnobs[0]);
const float kKnobDividers[] = {369, 721};

struct Selector { uint32_t id; Rect box; const char* label; const char* const* names; int count; };
const Selector kSelectors[] = {
    {kSource, {24, 302, 220, 26}, "WHAT MAKES THE ROOM", kSourceNames, 2},
    {kListen, {268, 302, 330, 26}, "LISTEN TO", kListenNames, 3},
};

const float kPresetX = 524, kHeaderY = 14; // the preset bar ends 10 px before the help button
const float kHelpX = UI_BASE_WIDTH - 52, kSkinX = UI_BASE_WIDTH - 24; // help, and right of it the choice of look

std::string upper(const char* s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
    return out;
}

} // namespace

class SpacerUI : public UI {
public:
    SpacerUI() : UI(UI_BASE_WIDTH, UI_BASE_HEIGHT, false) {
        for (uint32_t i = 0; i < kParamCount; ++i) values_[i] = paramInfo(i).def;
        loadSharedResources();
        size_.open(skin_); // the window opens at the size it was left at

        // Help: wide areas first, single controls on top.
        help_.setButton(kHelpX, kHeaderY + 12);
        skin_.setButton(kSkinX, kHeaderY + 12);
        help_.add(help_.buttonBox(), "HELP", help::kHelp);
        help_.add(skin_.buttonBox(), "LOOK AND SIZE", hnl::ui::SkinMenu::helpWithSizes());
        help_.add(presetBar_.nameBox(), "PRESETS", help::kPresets);
        help_.add({kGraphPanel.x, kGraphPanel.y, kGraphPanel.w, kGraphPanel.h}, "DISPLAY", help::kDisplay);
        help_.add({kOutPanel.x, kOutPanel.y, kOutPanel.w, kOutPanel.h}, "OUTPUT", help::kOutput);
        for (const Selector& sel : kSelectors)
            help_.add({sel.box.x, sel.box.y - 20, sel.box.w, sel.box.h + 20}, paramInfo(sel.id).name, help::forParam(sel.id));
        for (const Knob& k : kKnobs)
            help_.add({k.x - k.r - 12, k.y - k.r - 20, 2 * k.r + 24, 2 * k.r + 42}, paramInfo(k.id).name, help::forParam(k.id));
    }

protected:
    void parameterChanged(uint32_t index, float value) override {
        if (index >= kParamCount || values_[index] == value) return;
        values_[index] = value;
        repaint();
    }

    void stateChanged(const char* key, const char* value) override {
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

        hnl::ui::title(*this, "CHANNEL SPACER", rgb(k.plateText), rgb(k.plateDim));
        std::string presetText = presetName_;
        if (presetModified()) presetText += " *";
        if (!presetNote_.empty()) presetText = presetNote_;
        presetBar_.drawBar(*this, theme, presetText.c_str());
        help_.drawButton(*this, theme);
        skin_.drawButton(*this, theme);

        for (const Rect* p : {&kGraphPanel, &kOutPanel}) hnl::ui::displayPanel(*this, k, *p);
        for (const Rect* p : {&kSwitchPanel, &kKnobPanel}) hnl::ui::platePanel(*this, k, *p);

        drawDisplay();
        drawOutput();
        for (const Selector& sel : kSelectors) drawSelector(sel);
        // The warning is a light: on the plate of the solid looks it has a window, in which it is dark until it is needed.
        const bool warn = choice(kListen) != kListenOutput;
        if (k.solid) hnl::ui::displayPanel(*this, k, kWarning);
        if (warn || k.solid)
            label("YOU ARE NOT HEARING THE OUTPUT", kSwitchPanel.x + kSwitchPanel.w - 16, 315, 9, rgb(warn ? k.yellow : k.track), ALIGN_RIGHT | ALIGN_MIDDLE);

        for (float x : kKnobDividers) hnl::ui::groove(*this, k, x, kKnobPanel.y + 16, x, kKnobPanel.y + kKnobPanel.h - 16);
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
        if (help_.buttonHit(x, y)) {
            help_.on = !help_.on;
            help_.motion(x, y);
            repaint();
            return true;
        }

        for (const Selector& sel : kSelectors) {
            if (!sel.box.contains(x, y)) continue;
            const int choice = static_cast<int>((x - sel.box.x) / (sel.box.w / sel.count));
            change(sel.id, static_cast<float>(choice < 0 ? 0 : (choice >= sel.count ? sel.count - 1 : choice)), true);
            return true;
        }

        const int hit = knobAt(x, y);
        if (hit < 0) return false;
        const uint32_t id = kKnobs[hit].id;
        const bool doubleClick = hit == lastClick_ && ev.time - lastClickTime_ < 350;
        lastClick_ = hit;
        lastClickTime_ = ev.time;
        if (doubleClick || (ev.mod & kModifierControl)) {
            change(id, paramInfo(id).def, true);
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
            const float tolerance = 1e-4f * (paramInfo(id).max - paramInfo(id).min);
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

    /** The look: in every one of them what is on or lit has this plugin's own colour. */
    hnl::ui::Skin skin() const { return skin_.skin() == hnl::ui::kSkinClassic ? kClassic : hnl::ui::solidSkin(skin_.skin(), kClassic.accent); }

    /** A thin line inside a display: the edge colour of the flat look, a faint line on the dark glass of the solid ones. */
    static Color hairline(const hnl::ui::Skin& k, float flatAlpha) { return k.solid ? rgb(k.dim, 0.28f) : rgb(k.panelEdge, flatAlpha); }

    // ------------------------------------------------------------ display

    static float freqX(double f) { return kPlot.x + kPlot.w * static_cast<float>(std::log(f / 20.0) / std::log(1000.0)); }
    static float levelY(float db) {
        const float t = db < kLevelFloor ? 0.0f : (db > 0.0f ? 1.0f : 1.0f - db / kLevelFloor);
        return kPlot.y + kPlot.h * (1.0f - t);
    }
    static float cutY(float db) { // db: 0 or negative
        const float t = -db / kCutScale;
        return kPlot.y + kPlot.h * (t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t));
    }

    void drawDisplay() {
        const hnl::ui::Skin k = skin();
        const bool self = choice(kSource) == 1;
        const float bottom = kPlot.y + kPlot.h, right = kPlot.x + kPlot.w;
        label("WHERE ROOM IS MADE", 28, 68, 10, rgb(k.text), ALIGN_LEFT | ALIGN_MIDDLE);

        // Legend.
        float lx = kGraphPanel.x + kGraphPanel.w - 16;
        const auto legend = [&](const char* text, uint32_t colour, float width) {
            label(text, lx, 68, 8, rgb(k.dim), ALIGN_RIGHT | ALIGN_MIDDLE);
            lx -= width;
            beginPath();
            rect(lx, 65, 10, 6);
            fillColor(rgb(colour));
            fill();
            lx -= 16;
        };
        label("DEEPEST ALLOWED", lx, 68, 8, rgb(k.dim), ALIGN_RIGHT | ALIGN_MIDDLE);
        lx -= 96;
        beginPath();
        rect(lx, 67.5f, 10, 1);
        fillColor(rgb(kOrange, 0.6f));
        fill();
        lx -= 16;
        legend("CUT FROM THIS CHANNEL", kOrange, 124);
        legend(self ? "THIS CHANNEL" : "THE DUCKER", k.accent, self ? 76 : 66);

        beginPath();
        rect(kPlot.x, kPlot.y, kPlot.w, kPlot.h);
        fillColor(rgb(k.well));
        fill();

        // Grid: frequencies, and the scale of the cut on the left.
        struct Mark { double f; const char* text; };
        static const Mark kMarks[] = {{50, "50"}, {100, "100"}, {200, "200"}, {500, "500"}, {1000, "1k"},
                                      {2000, "2k"}, {5000, "5k"}, {10000, "10k"}};
        for (const Mark& m : kMarks) {
            const float x = freqX(m.f);
            beginPath();
            moveTo(x, kPlot.y);
            lineTo(x, bottom);
            strokeColor(hairline(k, 0.7f));
            strokeWidth(1);
            lineCap(BUTT);
            stroke();
            label(m.text, x, bottom + 12, 8, rgb(k.dim), ALIGN_CENTER | ALIGN_MIDDLE);
        }
        label("Hz", right, bottom + 12, 8, rgb(k.dim), ALIGN_RIGHT | ALIGN_MIDDLE);
        for (int db = 0; db >= -30; db -= 10) {
            const float y = cutY(static_cast<float>(db));
            if (db != 0 && db != -30) {
                beginPath();
                moveTo(kPlot.x, y);
                lineTo(right, y);
                strokeColor(hairline(k, 0.7f));
                strokeWidth(1);
                stroke();
            }
            char text[8];
            std::snprintf(text, sizeof(text), "%d", db);
            label(text, kPlot.x - 6, y + (db == 0 ? 4 : (db == -30 ? -4 : 0)), 8, rgb(kOrange, 0.8f), ALIGN_RIGHT | ALIGN_MIDDLE);
        }
        label("dB", kPlot.x - 6, bottom + 12, 8, rgb(kOrange, 0.8f), ALIGN_RIGHT | ALIGN_MIDDLE);

        // The ducker per band; the part above the line is what ducks.
        const float lineDb = static_cast<float>(thresholdDb(values_[kSensitivity])), lineY = levelY(lineDb);
        const float barW = kPlot.w / 3.0f / std::log2(1000.0f) * 3.0f * static_cast<float>(std::log2(bandFreq(1) / bandFreq(0))) * 0.72f;
        bool anySignal = false;
        for (int band = 0; band < kBands; ++band) {
            const float level = values_[kMeterLevel + band];
            if (level > -100.0f) anySignal = true;
            if (level <= kLevelFloor) continue;
            const float x = freqX(bandFreq(band)) - barW * 0.5f, y = levelY(level);
            if (x < kPlot.x || x + barW > right) continue;
            beginPath();
            rect(x, std::max(y, lineY), barW, bottom - std::max(y, lineY));
            fillColor(rgb(k.accent, 0.3f));
            fill();
            if (y < lineY) {
                beginPath();
                rect(x, y, barW, lineY - y);
                fillColor(rgb(k.accent, 0.9f));
                fill();
            }
        }
        for (float x = kPlot.x; x < right - 1; x += 9) {
            beginPath();
            moveTo(x, lineY);
            lineTo(std::min(x + 5, right), lineY);
            strokeColor(rgb(k.text, 0.75f));
            strokeWidth(1);
            stroke();
        }
        label("DUCKING STARTS HERE", kPlot.x + 6, lineY + (lineY > bottom - 14 ? -8 : 9), 8, rgb(k.text, 0.75f), ALIGN_LEFT | ALIGN_MIDDLE);

        // How deep each range may go, and what is being cut right now.
        double ranges[4];
        for (int i = 0; i < 4; ++i) ranges[i] = values_[kLow + i];
        beginPath();
        for (int i = 0; i <= 96; ++i) {
            const double f = 20.0 * std::pow(1000.0, i / 96.0);
            const float depth = static_cast<float>(std::min(kMaxCutDb, values_[kDepth] * rangeAmount(f, ranges)));
            if (i == 0) moveTo(freqX(f), cutY(-depth));
            else lineTo(freqX(f), cutY(-depth));
        }
        strokeColor(rgb(kOrange, 0.45f));
        strokeWidth(1);
        stroke();

        float cut[kBands], effective[kBands];
        bool cutting = false;
        for (int band = 0; band < kBands; ++band) {
            cut[band] = values_[kMeterCut + band];
            cutting = cutting || cut[band] > 0.05f;
        }
        if (cutting) {
            effectiveCut(cut, effective);
            const auto trace = [&](bool closed) {
                beginPath();
                if (closed) moveTo(kPlot.x, kPlot.y);
                for (int band = 0; band < kBands; ++band) {
                    const float x = std::min(right, std::max(kPlot.x, freqX(bandFreq(band)))), y = cutY(effective[band]);
                    if (band == 0) {
                        if (closed) lineTo(kPlot.x, y);
                        else moveTo(kPlot.x, y);
                    }
                    lineTo(x, y);
                    if (band == kBands - 1) lineTo(right, y);
                }
                if (closed) {
                    lineTo(right, kPlot.y);
                    closePath();
                }
            };
            trace(true);
            fillColor(rgb(kOrange, 0.22f));
            fill();
            trace(false);
            strokeColor(rgb(kOrange));
            strokeWidth(1.5f);
            stroke();
        }

        if (!self && !anySignal)
            label("NO SIGNAL AT THE SIDECHAIN INPUT", kPlot.x + kPlot.w * 0.5f, kPlot.y + kPlot.h * 0.72f, 10, rgb(k.dim), ALIGN_CENTER | ALIGN_MIDDLE);
    }

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
        static const int kMarks[] = {-48, -24, -12, 0};
        for (int db : kMarks) {
            char text[8];
            std::snprintf(text, sizeof(text), "%d", db);
            label(text, x + w * levelPos(static_cast<float>(db)), kOutPanel.y + 78, 8, rgb(k.dim), ALIGN_CENTER | ALIGN_MIDDLE);
        }

        const float peak = std::max(values_[kMeterOutL], values_[kMeterOutR]);
        char text[48];
        if (peak <= -99.0f) std::snprintf(text, sizeof(text), "--");
        else std::snprintf(text, sizeof(text), "%.1f", peak);
        label("PEAK", kOutPanel.x + 16, kOutPanel.y + 110, 8, rgb(k.dim), ALIGN_LEFT | ALIGN_MIDDLE);
        label(text, kOutPanel.x + kOutPanel.w - 44, kOutPanel.y + 109, 16, rgb(peak > -0.05f ? k.red : k.text), ALIGN_RIGHT | ALIGN_MIDDLE);
        label("dBFS", kOutPanel.x + kOutPanel.w - 16, kOutPanel.y + 110, 8, rgb(k.dim), ALIGN_RIGHT | ALIGN_MIDDLE);

        // The deepest cut the listener gets right now.
        float cut[kBands], effective[kBands], deepest = 0.0f;
        for (int band = 0; band < kBands; ++band) cut[band] = values_[kMeterCut + band];
        effectiveCut(cut, effective);
        for (int band = 0; band < kBands; ++band) deepest = std::min(deepest, effective[band]);
        beginPath();
        moveTo(kOutPanel.x + 16, kOutPanel.y + 134);
        lineTo(kOutPanel.x + kOutPanel.w - 16, kOutPanel.y + 134);
        strokeColor(hairline(k, 1.0f));
        strokeWidth(1);
        stroke();
        label("DEEPEST CUT NOW", kOutPanel.x + 16, kOutPanel.y + 152, 8, rgb(k.dim), ALIGN_LEFT | ALIGN_MIDDLE);
        if (deepest > -0.05f) std::snprintf(text, sizeof(text), "0.0");
        else std::snprintf(text, sizeof(text), "%.1f", deepest);
        label(text, kOutPanel.x + kOutPanel.w - 44, kOutPanel.y + 180, 22, rgb(deepest < -0.05f ? kOrange : k.dim), ALIGN_RIGHT | ALIGN_MIDDLE);
        label("dB", kOutPanel.x + kOutPanel.w - 16, kOutPanel.y + 182, 8, rgb(k.dim), ALIGN_RIGHT | ALIGN_MIDDLE);
    }

    // ----------------------------------------------------------- controls

    int choice(uint32_t id) const { return static_cast<int>(std::lround(values_[id])); }

    void drawSelector(const Selector& sel) {
        const hnl::ui::Skin k = skin();
        const Rect& b = sel.box;
        label(sel.label, b.x, b.y - 13, 9, rgb(k.plateDim), ALIGN_LEFT | ALIGN_MIDDLE);
        std::string text;
        hnl::ui::keys(*this, k, b, sel.count, choice(sel.id), [&](int i) {
            text = upper(sel.names[i]);
            return text.c_str();
        });
    }

    void drawKnob(const Knob& k) {
        const hnl::ui::Skin look = skin();
        hnl::ui::knob(*this, look, k.x, k.y, k.r, paramToNorm(k.id, values_[k.id]));
        label(k.label, k.x, k.y - k.r - 14, 9, rgb(look.plateDim), ALIGN_CENTER | ALIGN_MIDDLE);
        char text[32];
        const float v = values_[k.id];
        if (k.id == kDepth) std::snprintf(text, sizeof(text), "%.1f dB", v);
        else if (k.id == kAttack || k.id == kRelease) std::snprintf(text, sizeof(text), v < 10.0f ? "%.1f ms" : "%.0f ms", v);
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
    hnl::PresetList list_ = presets::list();
    std::string presetNote_; // shown in place of the name until the next click
    hnl::ui::HelpTips help_;
    hnl::ui::SkinMenu skin_;
    hnl::ui::WindowSize<SpacerUI> size_{*this, "channel-spacer", UI_BASE_WIDTH, UI_BASE_HEIGHT};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpacerUI)
};

UI* createUI() { return new SpacerUI(); }

END_NAMESPACE_DISTRHO
