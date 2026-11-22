// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// HNL Master: user interface (NanoVG).
//
// The window is a row of modules. A stage that is switched off stays where it
// is and goes dim. The arrow next to a stage's switch folds it to a title bar,
// and the window shrinks to fit: only that arrow moves anything, so that a
// switch or a preset never moves what is under the mouse. Thin lines in the
// gaps between the modules show the way the sound goes. Coordinates are in
// design pixels (kHeight high, width from layout()), scaled by the host's UI scale.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "DistrhoUI.hpp"

#include "../../common/ui/Skin.hpp"
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

const uint32_t kBg = 0x14161a, kPanel = 0x1d2027, kPanelEdge = 0x2a2e38, kTrack = 0x30353f, kWell = 0x0e1013;
const uint32_t kText = 0xe6e9ef, kDim = 0x8a93a3;
const uint32_t kAmber = 0xf2a33a, kTeal = 0x37c8b4, kRed = 0xef5b5b, kBlue = 0x5b9cf2;
const uint32_t kViolet = 0xa98bf5, kSky = 0x5bc0f2, kRose = 0xf27ba8;

// In signal order. Input and output are always open.
enum Module { kModInput, kModComp, kModEq, kModExc, kModWide, kModRev, kModLim, kModOutput, kModCount };

struct ModuleInfo {
    const char* title;
    const char* shortTitle; // used when folded
    uint32_t accent;
    uint32_t toggle; // on/off parameter, or kParamCount for none
    float height;    // when open, for the modules stacked in a column
};

const float kHeight = UI_BASE_HEIGHT, kTop = 52, kBottom = 556, kMargin = 12, kGap = 12; // the gaps hold the lines of the signal path
const float kInputW = 84, kCompW = 486, kColumnW = 212, kOutputW = 182, kBarH = 30;

const ModuleInfo kModules[kModCount] = {
    {"INPUT", "INPUT", kBlue, kParamCount, 0},
    {"MULTIBAND COMPRESSOR", "COMPRESSOR", kAmber, kCompOn, 0},
    {"EQUALISER", "EQUALISER", kViolet, kEqOn, 218}, // with the two modules under it, this fills the column
    {"EXCITER", "EXCITER", kTeal, kExcOn, 148},
    {"STEREO WIDTH", "STEREO WIDTH", kSky, kWideOn, 114},
    {"REVERB", "REVERB", kRose, kRevOn, 114},
    {"LIMITER", "LIMITER", kRed, kLimOn, 140},
    {"OUTPUT", "OUTPUT", kBlue, kParamCount, 0},
};

typedef hnl::ui::Box Rect;

struct Control {
    uint32_t id;
    Module module;
    float dx, dy, r; // knob centre and radius, relative to the module
    const char* label;
    bool toggle;
};

const float kBandWidth = 162.0f;
const float kPresetRight = 296, kHeaderY = 14; // the preset bar starts this far from the right edge, which moves as modules are folded
const float kHelpRight = 52, kSkinRight = 24;  // help, and right of it the choice of look

/** The classic look in this plugin's own colours; the other looks are the same for every plugin (common/ui/Skin.hpp). */
hnl::ui::Skin classicLook() {
    hnl::ui::Skin k = hnl::ui::classicSkin(kBg, kPanel, kPanelEdge, kTrack, kWell, kText, kDim, kAmber, 0x262a33, 0x3a404c);
    k.knobHi = k.knobLo = k.knobEdge = 0x262a33;
    return k;
}
const hnl::ui::Skin kClassic = classicLook();
const Rect kModeBox = {12, 36, 188, 22}; // exciter character, relative to its module
const Rect kOrderBox = {12, 34, 188, 20}; // equaliser before or after the compressor, relative to its module
const uint32_t kFlow = 0x646c7d;          // the lines of the signal path
const Rect kResetBox = {20, 448, 142, 28};
const float kFoldX = 62, kFoldY = 15; // the fold arrow of a module: this far from its right edge, and from its top

} // namespace

class MasterUI : public UI {
public:
    MasterUI() : UI(UI_BASE_WIDTH, UI_BASE_HEIGHT, false) {
        for (uint32_t i = 0; i < kParamCount; ++i) values_[i] = kParams[i].def;
        loadSharedResources();

        knob(kInputGain, kModInput, 42, 448, 19, "GAIN");

        knob(kXoverLow, kModComp, kBandWidth, 64, 15, "LOW X-OVER");
        knob(kXoverHigh, kModComp, 2 * kBandWidth, 64, 15, "HIGH X-OVER");
        for (int b = 0; b < 3; ++b) {
            const float k1 = b * kBandWidth + 40, k2 = b * kBandWidth + 100;
            const uint32_t base = kLowThreshold + 5 * b;
            knob(base + 0, kModComp, k1, 182, 20, "THRESHOLD");
            knob(base + 1, kModComp, k2, 182, 20, "RATIO");
            knob(base + 2, kModComp, k1, 294, 20, "ATTACK");
            knob(base + 3, kModComp, k2, 294, 20, "RELEASE");
            knob(base + 4, kModComp, k1, 406, 20, "GAIN");
        }

        const float four[4] = {31, 81, 131, 181}, three[3] = {46, 106, 166};
        knob(kEqLowFreq, kModEq, four[0], 94, 16, "LOW FREQ");
        knob(kEqLowGain, kModEq, four[1], 94, 16, "LOW GAIN");
        knob(kEqHighFreq, kModEq, four[2], 94, 16, "HIGH FREQ");
        knob(kEqHighGain, kModEq, four[3], 94, 16, "HIGH GAIN");
        knob(kEqMidFreq, kModEq, three[0], 176, 16, "PEAK FREQ");
        knob(kEqMidGain, kModEq, three[1], 176, 16, "PEAK GAIN");
        knob(kEqMidQ, kModEq, three[2], 176, 16, "PEAK Q");

        knob(kExcFreq, kModExc, four[0], 106, 16, "FREQ");
        knob(kExcDrive, kModExc, four[1], 106, 16, "DRIVE");
        knob(kExcColour, kModExc, four[2], 106, 16, "COLOUR");
        knob(kExcMix, kModExc, four[3], 106, 16, "MIX");

        knob(kWideWidth, kModWide, 66, 72, 16, "WIDTH");
        knob(kWideFreq, kModWide, 146, 72, 16, "ABOVE");

        knob(kRevDecay, kModRev, three[0], 72, 16, "DECAY");
        knob(kRevDamping, kModRev, three[1], 72, 16, "DAMPING");
        knob(kRevMix, kModRev, three[2], 72, 16, "AMOUNT");

        knob(kLimGain, kModLim, three[0], 72, 16, "GAIN");
        knob(kLimCeiling, kModLim, three[1], 72, 16, "CEILING");
        knob(kLimRelease, kModLim, three[2], 72, 16, "RELEASE");

        for (int m = 0; m < kModCount; ++m)
            if (kModules[m].toggle != kParamCount)
                controls_.push_back({kModules[m].toggle, static_cast<Module>(m), 0, 15, 0, "", true});

        layout();
    }

protected:
    void parameterChanged(uint32_t index, float value) override {
        if (index >= kParamCount || values_[index] == value) return;
        values_[index] = value;
        if (index == kEqFirst) layout();
        repaint();
    }

    void stateChanged(const char* key, const char* value) override {
        if (std::strcmp(key, "folded") == 0) {
            for (int m = kModComp; m <= kModLim; ++m) {
                const size_t i = static_cast<size_t>(m - kModComp);
                folded_[m] = i < std::strlen(value) && value[i] == '1';
            }
            layout();
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
    }

    // ------------------------------------------------------------ drawing

    void onNanoDisplay() override {
        const float s = static_cast<float>(getScaleFactor());
        scale(s, s);
        fontFace(NANOVG_DEJAVU_SANS_TTF);

        const hnl::ui::Skin k = skin();
        const hnl::ui::Theme theme = k.theme();
        hnl::ui::faceplate(*this, k, std::max(width_, getWidth() / s), std::max(kHeight, getHeight() / s));

        hnl::ui::title(*this, "MASTER", rgb(k.plateText), rgb(k.plateDim));
        std::string presetText = presetName_;
        if (presetModified()) presetText += " *";
        if (!presetNote_.empty()) presetText = presetNote_;
        presetBar_.drawBar(*this, theme, presetText.c_str());
        help_.drawButton(*this, theme);
        skin_.drawButton(*this, theme);

        for (int m = 0; m < kModCount; ++m) drawModule(static_cast<Module>(m));
        drawFlow();

        drawInput(rects_[kModInput]);
        if (open(kModComp)) drawCompressor(rects_[kModComp]);
        if (open(kModExc)) drawExciterMode(rects_[kModExc]);
        if (open(kModEq)) drawEqOrder(rects_[kModEq]);
        if (open(kModLim)) drawLimiter(rects_[kModLim]);
        drawOutput(rects_[kModOutput]);

        for (const Control& c : controls_) {
            if (c.toggle) drawToggle(c);
            else if (open(c.module)) drawKnob(c);
        }
        for (int m = kModComp; m <= kModLim; ++m) { // a stage that is switched off is dim, under its title
            const Rect& p = rects_[m];
            if (!open(static_cast<Module>(m)) || active(static_cast<Module>(m))) continue;
            beginPath();
            roundedRect(p.x + 1, p.y + kBarH, p.w - 2, p.h - kBarH - 1, 5);
            fillColor(k.solid ? rgb(k.bgLow, 0.62f) : rgb(k.panel, 0.68f));
            fill();
        }

        presetBar_.drawMenu(*this, theme, list_.count(), preset_, list_.factoryCount(), canSavePreset(), [this](int i) { return list_.name(i); });
        skin_.drawMenu(*this, theme);
        if (!presetBar_.isOpen() && !skin_.isOpen() && drag_ < 0) help_.drawTip(*this, theme, width_, kHeight);
    }

    // -------------------------------------------------------- interaction

    bool onMouse(const MouseEvent& ev) override {
        if (ev.button != 1) return false;
        const float s = static_cast<float>(getScaleFactor());
        const float x = static_cast<float>(ev.pos.getX()) / s, y = static_cast<float>(ev.pos.getY()) / s;

        if (!ev.press) {
            if (drag_ < 0) return false;
            editParameter(controls_[drag_].id, false);
            drag_ = -1;
            return true;
        }

        presetNote_.clear();
        if (!presetBar_.isBusy() && skin_.mouseDown(x, y) != hnl::ui::SkinMenu::kNotMine) { // the cog, or its list while it is open
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

        if (inModule(kModOutput, kResetBox, x, y)) {
            setState("reset", "");
            return true;
        }
        if (open(kModExc) && inModule(kModExc, kModeBox, x, y)) {
            const int mode = static_cast<int>((x - rects_[kModExc].x - kModeBox.x) / (kModeBox.w / 3.0f));
            change(kExcMode, static_cast<float>(mode < 0 ? 0 : (mode > 2 ? 2 : mode)), true);
            return true;
        }

        for (int m = kModComp; m <= kModLim; ++m) {
            if (!foldBox(static_cast<Module>(m)).contains(x, y)) continue;
            setFolded(static_cast<Module>(m), !folded_[m]);
            return true;
        }

        if (open(kModEq) && inModule(kModEq, kOrderBox, x, y)) {
            change(kEqFirst, x - rects_[kModEq].x - kOrderBox.x < kOrderBox.w * 0.5f ? 1.0f : 0.0f, true);
            return true;
        }

        const int hit = controlAt(x, y);
        if (hit < 0) return false;
        const Control& c = controls_[hit];

        if (c.toggle) {
            const bool switchOn = values_[c.id] <= 0.5f;
            change(c.id, switchOn ? 1.0f : 0.0f, true);
            if (switchOn && folded_[c.module]) setFolded(c.module, false); // switched on by hand: show what it does
            return true;
        }
        const bool doubleClick = hit == lastClick_ && ev.time - lastClickTime_ < 350;
        lastClick_ = hit;
        lastClickTime_ = ev.time;
        if (doubleClick || (ev.mod & kModifierControl)) {
            change(c.id, kParams[c.id].def, true);
            return true;
        }
        drag_ = hit;
        dragY_ = y;
        dragNorm_ = paramToNorm(c.id, values_[c.id]);
        editParameter(c.id, true);
        return true;
    }

    bool onMotion(const MotionEvent& ev) override {
        const float s = static_cast<float>(getScaleFactor());
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
        change(controls_[drag_].id, normToParam(controls_[drag_].id, dragNorm_), false);
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
        const int hit = controlAt(static_cast<float>(ev.pos.getX()) / s, static_cast<float>(ev.pos.getY()) / s);
        if (hit < 0 || controls_[hit].toggle) return false;
        const uint32_t id = controls_[hit].id;
        const float step = (ev.mod & kModifierShift) ? 0.004f : 0.02f;
        const float norm = paramToNorm(id, values_[id]) + step * static_cast<float>(ev.delta.getY());
        change(id, normToParam(id, norm), true);
        return true;
    }

private:
    void knob(uint32_t id, Module module, float dx, float dy, float r, const char* text) {
        controls_.push_back({id, module, dx, dy, r, text, false});
    }

    bool on(uint32_t id) const { return values_[id] > 0.5f; }
    /** A module works when it has no on/off switch or is switched on. */
    bool active(Module m) const { return kModules[m].toggle == kParamCount || on(kModules[m].toggle); }
    /** A module is open, showing its controls, unless it was folded. Switching it on or off does not change that. */
    bool open(Module m) const { return !folded_[m]; }

    /** The modules in the order the sound passes them: the equaliser stands before or after the compressor. */
    void signalPath(Module* path) const {
        for (int m = 0; m < kModCount; ++m) path[m] = static_cast<Module>(m);
        if (on(kEqFirst)) std::swap(path[kModComp], path[kModEq]);
    }

    Rect foldBox(Module m) const { return {rects_[m].x + rects_[m].w - kFoldX - 12, rects_[m].y + kFoldY - 12, 24, 24}; }

    /** Folds or unfolds a module and tells the host, which keeps it with the song. Presets leave it alone. */
    void setFolded(Module m, bool folded) {
        folded_[m] = folded;
        char text[kModLim - kModComp + 2] = {0};
        for (int i = kModComp; i <= kModLim; ++i) text[i - kModComp] = folded_[i] ? '1' : '0';
        setState("folded", text);
        layout();
        repaint();
    }

    // ------------------------------------------------------------- layout

    /** Places the modules and resizes the window to fit them. */
    void layout() {
        float x = kMargin;
        rects_[kModInput] = {x, kTop, kInputW, kBottom - kTop};
        x += kInputW + kGap;
        if (open(kModComp)) {
            rects_[kModComp] = {x, kTop, kCompW, kBottom - kTop};
            x += kCompW + kGap;
        }
        // Everything else stacks in columns in signal order; a folded compressor joins them.
        float y = kTop;
        Module path[kModCount];
        signalPath(path);
        for (int i = 1; i < kModCount - 1; ++i) {
            const Module m = path[i];
            if (m == kModComp && open(m)) continue;
            const float h = open(m) ? kModules[m].height : kBarH;
            if (y + h > kBottom && y > kTop) {
                x += kColumnW + kGap;
                y = kTop;
            }
            rects_[m] = {x, y, kColumnW, h};
            y += h + kGap;
        }
        x += kColumnW + kGap;
        rects_[kModOutput] = {x, kTop, kOutputW, kBottom - kTop};
        width_ = x + kOutputW + kMargin;

        const double s = getScaleFactor();
        const uint w = static_cast<uint>(std::lround(width_ * s)), h = static_cast<uint>(std::lround(kHeight * s));
        if (getWidth() != w || getHeight() != h) setSize(w, h);
        presetBar_.moveTo(width_ - kPresetRight, width_);
        help_.setButton(width_ - kHelpRight, kHeaderY + 12);
        skin_.setButton(width_ - kSkinRight, kHeaderY + 12);
        placeHelp();
    }

    /** Tells the help tips where everything is. Wide areas first, single controls on top. */
    void placeHelp() {
        using hnl::ui::Box;
        help_.clear();
        help_.add(help_.buttonBox(), "HELP", help::kHelp);
        help_.add(skin_.buttonBox(), "LOOK", hnl::ui::SkinMenu::help());
        help_.add(presetBar_.nameBox(), "PRESETS", help::kPresets);
        const Rect& in = rects_[kModInput];
        help_.add({in.x + 16, in.y + 34, 56, 372}, "INPUT LEVEL", help::kInputMeter);
        const Rect& out = rects_[kModOutput];
        help_.add({out.x + 14, out.y + 34, 60, 402}, "OUTPUT LEVEL", help::kOutputMeter);
        help_.add({out.x + 84, out.y + 40, 96, 210}, "LOUDNESS", help::kLoudness);
        help_.add({out.x + 84, out.y + 264, 96, 64}, "TRUE PEAK", help::kTruePeak);
        help_.add({out.x + kResetBox.x, out.y + kResetBox.y, kResetBox.w, kResetBox.h}, "RESET METERS", help::kReset);
        if (open(kModComp)) {
            const Rect& p = rects_[kModComp];
            for (int b = 0; b < 3; ++b) help_.add({p.x + b * kBandWidth + 70, p.y + 380, 86, 104}, "GAIN REDUCTION", help::kGainReduction);
        }
        if (open(kModLim)) help_.add({rects_[kModLim].x + 8, rects_[kModLim].y + 110, kColumnW - 16, 24}, "GAIN REDUCTION", help::kGainReduction);
        if (open(kModExc)) {
            const Rect& p = rects_[kModExc];
            help_.add({p.x + kModeBox.x, p.y + kModeBox.y, kModeBox.w, kModeBox.h}, "EXCITER MODE", help::forParam(kExcMode));
        }
        if (open(kModEq)) {
            const Rect& p = rects_[kModEq];
            help_.add({p.x + kOrderBox.x, p.y + kOrderBox.y, kOrderBox.w, kOrderBox.h}, "EQUALISER PLACE", help::forParam(kEqFirst));
        }
        for (int m = kModComp; m <= kModLim; ++m) {
            const Rect f = foldBox(static_cast<Module>(m));
            help_.add({f.x, f.y, f.w, f.h}, "FOLD", help::kFold);
        }
        for (const Control& c : controls_) {
            if (c.toggle) help_.add({cx(c) - 16, cy(c) - 9, 32, 18}, kParams[c.id].name, help::forParam(c.id));
            else if (open(c.module)) help_.add({cx(c) - c.r - 12, cy(c) - c.r - 18, 2 * c.r + 24, 2 * c.r + 38}, kParams[c.id].name, help::forParam(c.id));
        }
    }

    bool inModule(Module m, const Rect& box, float x, float y) const {
        return box.contains(x - rects_[m].x, y - rects_[m].y);
    }

    float cx(const Control& c) const { return rects_[c.module].x + (c.toggle ? rects_[c.module].w - 28 : c.dx); }
    float cy(const Control& c) const { return rects_[c.module].y + c.dy; }

    int controlAt(float x, float y) const {
        for (size_t i = 0; i < controls_.size(); ++i) {
            const Control& c = controls_[i];
            if (c.toggle) {
                if (std::fabs(x - cx(c)) <= 20 && std::fabs(y - cy(c)) <= 12) return static_cast<int>(i);
            } else if (open(c.module) && std::hypot(x - cx(c), y - cy(c)) <= c.r + 8) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    /** Applies a value locally and sends it to the host. */
    void change(uint32_t id, float value, bool gesture) {
        if (gesture) editParameter(id, true);
        values_[id] = value;
        setParameterValue(id, value);
        if (gesture) editParameter(id, false);
        if (id == kEqFirst) layout();
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
            if (values_[id] == v) continue;
            editParameter(id, true);
            values_[id] = v;
            setParameterValue(id, v);
            editParameter(id, false);
        }
        setState("preset", presetName_.c_str());
        layout(); // a preset may put the equaliser on the other side of the compressor
        repaint();
    }

    /** True when a control was moved since the preset was chosen. */
    bool presetModified() const {
        if (preset_ < 0) return false;
        for (uint32_t id = 0; id < kInputParamCount; ++id) {
            const float tolerance = 1e-4f * (kParams[id].max - kParams[id].min);
            if (std::fabs(values_[id] - list_.value(preset_, id)) > tolerance) return true;
        }
        return false;
    }

    // ------------------------------------------------------------ modules

    /** The look: in every one of them what is on or lit has its module's colour, by which the modules are told apart. */
    hnl::ui::Skin skin() const { return skin_.skin() == hnl::ui::kSkinClassic ? kClassic : hnl::ui::solidSkin(skin_.skin(), kClassic.accent); }
    /** A display window in a module, where the solid looks show meters and figures. */
    void window(const Rect& p, float dx, float dy, float w, float h) {
        if (skin().solid) hnl::ui::displayPanel(*this, skin(), {p.x + dx, p.y + dy, w, h});
    }

    void label(const char* text, float x, float y, float size, const Color& colour, int align) {
        fontSize(size);
        textAlign(align);
        fillColor(colour);
        this->text(x, y, text, nullptr);
    }

    void drawModule(Module m) {
        const Rect& p = rects_[m];
        const ModuleInfo& info = kModules[m];
        const bool active = this->active(m);
        const hnl::ui::Skin k = skin();
        hnl::ui::platePanel(*this, k, p);
        if (k.solid) { // a light in its own little window: on, or dark
            beginPath();
            roundedRect(p.x + 9, p.y + 10, 5, 10, 2);
            fillColor(rgb(0x000000, 0.6f));
            fill();
        }
        if (!k.solid || active) {
            beginPath();
            roundedRect(p.x + 10, p.y + 11, 3, 8, 1.5f);
            fillColor(rgb(info.accent, active || k.solid ? 1.0f : 0.4f));
            fill();
        }
        label(open(m) ? info.title : info.shortTitle, p.x + 20, p.y + 15, 10, rgb(active ? k.plateText : k.plateDim), ALIGN_LEFT | ALIGN_MIDDLE);
        if (info.toggle == kParamCount) return;
        const float ax = p.x + p.w - kFoldX, ay = p.y + kFoldY; // the fold arrow: down when open, to the right when folded
        beginPath();
        if (open(m)) {
            moveTo(ax - 4, ay - 2);
            lineTo(ax, ay + 2);
            lineTo(ax + 4, ay - 2);
        } else {
            moveTo(ax - 2, ay - 4);
            lineTo(ax + 2, ay);
            lineTo(ax - 2, ay + 4);
        }
        strokeColor(rgb(k.plateDim));
        strokeWidth(1.5f);
        stroke();
    }

    void drawInput(const Rect& p) {
        window(p, 12, 30, 60, 380);
        levelMeter(p.x + 22, p.y + 40, 12, 360, values_[kMeterInL]);
        levelMeter(p.x + 38, p.y + 40, 12, 360, values_[kMeterInR]);
        levelScale(p.x + 54, p.y + 40, 360);
    }

    void drawCompressor(const Rect& p) {
        static const char* const kBandNames[3] = {"LOW", "MID", "HIGH"};
        for (int b = 0; b < 3; ++b) {
            const float x = p.x + b * kBandWidth;
            const hnl::ui::Skin k = skin();
            label(kBandNames[b], x + kBandWidth * 0.5f, p.y + 120, 12, rgb(k.solid ? k.plateText : kAmber), ALIGN_CENTER | ALIGN_MIDDLE);
            if (b > 0) hnl::ui::groove(*this, k, x, p.y + 108, x, p.y + 484);
            const float gr = values_[kMeterGrLow + b];
            window(p, b * kBandWidth + 132, 148, 18, 328);
            reductionMeter(x + 136, p.y + 152, 10, 320, gr, 20.0f, rgb(kModules[kModComp].accent), false);
            label("GR", x + 100, p.y + 394, 9, rgb(k.plateDim), ALIGN_CENTER | ALIGN_MIDDLE);
            char text[24];
            std::snprintf(text, sizeof(text), "%.1f dB", gr);
            label(text, x + 100, p.y + 412, 12, rgb(k.plateText), ALIGN_CENTER | ALIGN_MIDDLE);
        }
    }

    /** A row of choices in a box, the chosen one in the module's colour. */
    void drawChoice(const Rect& p, const Rect& box, const char* const* names, int count, int chosen, uint32_t accent) {
        hnl::ui::keys(*this, skin(), {p.x + box.x, p.y + box.y, box.w, box.h}, count, chosen, [&](int i) { return names[i]; }, accent, kDim);
    }

    void drawExciterMode(const Rect& p) {
        static const char* const kNames[3] = {"TAPE", "TUBE", "RETRO"}; // kExciterModes, as the panel writes them
        drawChoice(p, kModeBox, kNames, 3, static_cast<int>(std::lround(values_[kExcMode])), kTeal);
    }

    void drawEqOrder(const Rect& p) {
        static const char* const kNames[2] = {"BEFORE COMP", "AFTER COMP"};
        drawChoice(p, kOrderBox, kNames, 2, on(kEqFirst) ? 0 : 1, kViolet);
    }

    // -------------------------------------------------------- signal path

    /** A thin line through the points. */
    void flowLine(const float* xy, int points) {
        beginPath();
        moveTo(xy[0], xy[1]);
        for (int i = 1; i < points; ++i) lineTo(xy[2 * i], xy[2 * i + 1]);
        strokeColor(rgb(skin().solid ? skin().plateDim : kFlow));
        strokeWidth(1);
        stroke();
    }

    /** The way the sound goes: a line from each module to the next, in the gaps between them. */
    void drawFlow() {
        Module path[kModCount];
        signalPath(path);
        for (int i = 0; i + 1 < kModCount; ++i) {
            const Rect &a = rects_[path[i]], &b = rects_[path[i + 1]];
            const float ya = a.y + kFoldY, yb = b.y + kFoldY; // at the height of a title
            if (a.x == b.x) { // the next one down in a column
                const float x = a.x + a.w * 0.5f, line[4] = {x, a.y + a.h + 1, x, b.y - 1};
                flowLine(line, 2);
                continue;
            }
            const bool right = b.x > a.x;
            const float from = right ? a.x + a.w + 1 : a.x - 1, to = right ? b.x - 1 : b.x + b.w + 1;
            if (std::fabs(to - from) > kGap) { // something stands between them: over its top
                const float lane = kTop - 5, line[8] = {a.x + a.w * 0.5f, a.y - 1, a.x + a.w * 0.5f, lane, b.x + b.w * 0.5f, lane, b.x + b.w * 0.5f, b.y - 1};
                flowLine(line, 4);
            } else if (ya >= b.y && ya <= b.y + b.h) { // neighbours: straight across where both stand
                const float line[4] = {from, ya, to, ya};
                flowLine(line, 2);
            } else if (yb >= a.y && yb <= a.y + a.h) {
                const float line[4] = {from, yb, to, yb};
                flowLine(line, 2);
            } else { // from the foot of one column to the head of the next, through the gap
                const float mid = (from + to) * 0.5f, line[8] = {from, ya, mid, ya, mid, yb, to, yb};
                flowLine(line, 4);
            }
        }
    }

    void drawLimiter(const Rect& p) {
        const float gr = values_[kMeterGrLim];
        const hnl::ui::Skin k = skin();
        label("GR", p.x + 12, p.y + 122, 9, rgb(k.plateDim), ALIGN_LEFT | ALIGN_MIDDLE);
        window(p, 28, 114, 116, 16);
        reductionMeter(p.x + 32, p.y + 118, 108, 8, gr, 12.0f, rgb(k.red), true);
        char text[24];
        std::snprintf(text, sizeof(text), "%.1f dB", gr);
        label(text, p.x + p.w - 12, p.y + 122, 11, rgb(k.plateText), ALIGN_RIGHT | ALIGN_MIDDLE);
    }

    void drawOutput(const Rect& p) {
        const hnl::ui::Skin k = skin();
        window(p, 10, 30, p.w - 20, 408);
        levelMeter(p.x + 20, p.y + 40, 14, 390, values_[kMeterOutL]);
        levelMeter(p.x + 38, p.y + 40, 14, 390, values_[kMeterOutR]);
        levelScale(p.x + 56, p.y + 40, 390);
        const float x = p.x + 132;
        readout("MOMENTARY", values_[kMeterLufsM], "LUFS", x, p.y + 52, 15, k.text);
        readout("SHORT-TERM", values_[kMeterLufsS], "LUFS", x, p.y + 122, 15, k.text);
        readout("INTEGRATED", values_[kMeterLufsI], "LUFS", x, p.y + 192, 20, k.text);
        readout("TRUE PEAK MAX", values_[kMeterTruePeak], "dBTP", x, p.y + 278, 15,
                values_[kMeterTruePeak] > values_[kLimCeiling] + 0.05f && on(kLimOn) ? k.red : k.text);
        hnl::ui::button(*this, k, {p.x + kResetBox.x, p.y + kResetBox.y, kResetBox.w, kResetBox.h}, "RESET METERS");
    }

    // ----------------------------------------------------------- controls

    void drawToggle(const Control& c) {
        hnl::ui::toggle(*this, skin(), cx(c), cy(c), on(c.id), kModules[c.module].accent);
    }

    void drawKnob(const Control& c) {
        const hnl::ui::Skin k = skin();
        const ParamInfo& p = kParams[c.id];
        const float x = cx(c), y = cy(c);
        // Parameters with a neutral point in the middle grow from there.
        float from = 0.0f;
        if (c.id == kWideWidth) from = paramToNorm(c.id, 100.0f);
        else if (p.kind == kLinear && p.min < 0.0f && p.max > 0.0f) from = paramToNorm(c.id, 0.0f);
        hnl::ui::knob(*this, k, x, y, c.r, paramToNorm(c.id, values_[c.id]), from, hnl::ui::printed(k, kModules[c.module].accent), 13);

        const bool small = c.r < 18;
        label(c.label, x, y - c.r - 12, small ? 8 : 9, rgb(k.plateDim), ALIGN_CENTER | ALIGN_MIDDLE);
        char text[32];
        formatValue(c.id, values_[c.id], text, sizeof(text));
        label(text, x, y + c.r + 13, small ? 10 : 11, rgb(k.plateText), ALIGN_CENTER | ALIGN_MIDDLE);
    }

    static void formatValue(uint32_t id, float v, char* out, size_t size) {
        switch (kParams[id].unit[0]) {
        case 'H':
            if (v >= 9995.0f) std::snprintf(out, size, "%.1f kHz", v / 1000.0f);
            else if (v >= 1000.0f) std::snprintf(out, size, "%.2f kHz", v / 1000.0f);
            else std::snprintf(out, size, "%.0f Hz", v);
            break;
        case 'd': std::snprintf(out, size, "%+.1f dB", v); break;
        case 'm': std::snprintf(out, size, v < 10.0f ? "%.1f ms" : "%.0f ms", v); break;
        case ':': std::snprintf(out, size, "%.1f:1", v); break;
        case 's': std::snprintf(out, size, "%.2f s", v); break;
        case 'Q': std::snprintf(out, size, "%.2f", v); break;
        default: std::snprintf(out, size, "%.0f %%", v); break;
        }
    }

    // ------------------------------------------------------------- meters

    static float levelPos(float db) { // -48..+6 dB -> 0..1
        const float t = (db + 48.0f) / 54.0f;
        return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    }

    void levelMeter(float x, float y, float w, float h, float db) {
        const hnl::ui::Skin k = skin();
        beginPath();
        rect(x, y, w, h);
        fillColor(rgb(k.solid ? k.track : k.well)); // solid: the segments that are not lit
        fill();
        const float top = levelPos(db);
        struct Zone { float from, to; uint32_t colour; };
        const Zone zones[3] = {{-48, -12, k.green}, {-12, -3, k.yellow}, {-3, 6, k.red}};
        for (const Zone& z : zones) {
            const float a = levelPos(z.from), b = std::min(levelPos(z.to), top);
            if (b <= a) continue;
            beginPath();
            rect(x, y + h * (1.0f - b), w, h * (b - a));
            fillColor(rgb(z.colour));
            fill();
        }
        hnl::ui::segments(*this, k, x, y, w, h);
    }

    void levelScale(float x, float y, float h) {
        static const int kMarks[] = {6, 0, -6, -12, -24, -36, -48};
        for (int db : kMarks) {
            char text[8];
            std::snprintf(text, sizeof(text), "%d", db);
            label(text, x, y + h * (1.0f - levelPos(static_cast<float>(db))), 8, rgb(skin().dim), ALIGN_LEFT | ALIGN_MIDDLE);
        }
    }

    /** Bar that grows with gain reduction (db <= 0): downward, or to the right. */
    void reductionMeter(float x, float y, float w, float h, float db, float range, const Color& colour, bool horizontal) {
        const hnl::ui::Skin k = skin();
        beginPath();
        rect(x, y, w, h);
        fillColor(rgb(k.solid ? k.track : k.well));
        fill();
        const float t = std::min(1.0f, -db / range);
        if (t > 0.0f) {
            beginPath();
            if (horizontal) rect(x, y, w * t, h);
            else rect(x, y, w, h * t);
            fillColor(colour);
            fill();
        }
        hnl::ui::segments(*this, k, x, y, w, h);
    }

    void readout(const char* title, float value, const char* unit, float x, float y, float size, uint32_t colour) {
        const uint32_t kDim = skin().dim;
        label(title, x, y, 8, rgb(kDim), ALIGN_CENTER | ALIGN_MIDDLE);
        char text[24];
        if (value <= -99.0f) std::snprintf(text, sizeof(text), "--");
        else std::snprintf(text, sizeof(text), "%.1f", value);
        label(text, x, y + 20, size, rgb(colour), ALIGN_CENTER | ALIGN_MIDDLE);
        label(unit, x, y + 38, 8, rgb(kDim), ALIGN_CENTER | ALIGN_MIDDLE);
    }

    float values_[kParamCount];
    std::vector<Control> controls_;
    Rect rects_[kModCount];
    bool folded_[kModCount] = {}; // by the arrow in a module's title; kept with the song as the state "folded"
    float width_ = UI_BASE_WIDTH;
    int drag_ = -1, lastClick_ = -1;
    uint lastClickTime_ = 0;
    float dragY_ = 0.0f, dragNorm_ = 0.0f;

    int preset_ = 0;
    std::string presetName_ = "Default";
    hnl::ui::PresetBar presetBar_{UI_BASE_WIDTH - kPresetRight, kHeaderY};
    hnl::PresetList list_ = presets::list();
    std::string presetNote_; // shown in place of the name until the next click
    hnl::ui::HelpTips help_;
    hnl::ui::SkinMenu skin_;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MasterUI)
};

UI* createUI() { return new MasterUI(); }

END_NAMESPACE_DISTRHO
