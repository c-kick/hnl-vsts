// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// HNL Master: user interface (NanoVG).
//
// The window is a row of modules. A stage that is switched off stays where it
// is and goes dim. The arrow next to a stage's switch folds it to a title bar,
// and the window shrinks to fit: only that arrow moves anything, so that a
// switch or a preset never moves what is under the mouse. Thin lines in the
// gaps between the modules show the way the sound goes. Coordinates are in
// design pixels (kHeight high, width from layout()), drawn as large as the window is (common/ui/WindowSize.hpp).
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

const float kHeight = UI_BASE_HEIGHT, kTop = 52, kBottom = 580, kMargin = 12, kGap = 12; // the gaps hold the lines of the signal path
const float kInputW = 84, kCompW = 486, kColumnW = 212, kOutputW = 182, kBarH = 30;

const ModuleInfo kModules[kModCount] = {
    {"INPUT", "INPUT", kBlue, kParamCount, 0},
    {"MULTIBAND COMPRESSOR", "COMPRESSOR", kAmber, kCompOn, 0},
    {"EQUALISER", "EQUALISER", kViolet, kEqOn, 242}, // with the two modules under it, this fills the column
    {"EXCITER", "EXCITER", kTeal, kExcOn, 148},
    {"STEREO WIDTH", "STEREO WIDTH", kSky, kWideOn, 114},
    {"REVERB", "REVERB", kRose, kRevOn, 114},
    {"LIMITER", "LIMITER", kRed, kLimOn, 162},
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
const Rect kLimModeBox = {12, 34, 188, 20}; // limiter brickwall or soft, relative to its module
const Rect kLinkBox = {12, 134, 96, 20};   // the link of the compressor's bands, relative to its module
const Rect kSpecBox = {12, 36, 462, 72};   // the spectrum of what goes into the compressor
const Rect kEqModeBox = {12, 58, 188, 20}; // what the equaliser works on
const Rect kBypassBox = {20, 426, 69, 20}, kMatchBox = {93, 426, 69, 20}, kDitherBox = {66, 452, 96, 20}; // in the output module
const float kBandNameY = 196, kSoloX = 96, kMuteX = 120, kKeyW = 22, kKeyH = 18; // solo and mute of a band, beside its name
const uint32_t kFlow = 0x646c7d;          // the lines of the signal path
const Rect kResetBox = {20, 484, 142, 28};
const float kFoldX = 62, kFoldY = 15; // the fold arrow of a module: this far from its right edge, and from its top

} // namespace

class MasterUI : public UI {
public:
    MasterUI() : UI(UI_BASE_WIDTH, UI_BASE_HEIGHT, false) {
        for (uint32_t i = 0; i < kParamCount; ++i) values_[i] = kParams[i].def;
        loadSharedResources();

        knob(kInputGain, kModInput, 42, 472, 19, "GAIN");

        knob(kXoverLow, kModComp, kBandWidth, 146, 15, "LOW X-OVER");
        knob(kXoverHigh, kModComp, 2 * kBandWidth, 146, 15, "HIGH X-OVER");
        for (int b = 0; b < 3; ++b) {
            const float k1 = b * kBandWidth + 40, k2 = b * kBandWidth + 100;
            const uint32_t base = kLowThreshold + 5 * b;
            knob(base + 0, kModComp, k1, 252, 20, "THRESHOLD");
            knob(base + 1, kModComp, k2, 252, 20, "RATIO");
            knob(base + 2, kModComp, k1, 356, 20, "ATTACK");
            knob(base + 3, kModComp, k2, 356, 20, "RELEASE");
            knob(base + 4, kModComp, k1, 460, 20, "GAIN");
        }

        const float four[4] = {31, 81, 131, 181}, three[3] = {46, 106, 166};
        knob(kEqLowFreq, kModEq, four[0], 118, 16, "LOW FREQ");
        knob(kEqLowGain, kModEq, four[1], 118, 16, "LOW GAIN");
        knob(kEqHighFreq, kModEq, four[2], 118, 16, "HIGH FREQ");
        knob(kEqHighGain, kModEq, four[3], 118, 16, "HIGH GAIN");
        knob(kEqLowCut, kModEq, four[0], 200, 16, "LOW CUT");
        knob(kEqMidFreq, kModEq, four[1], 200, 16, "PEAK FREQ");
        knob(kEqMidGain, kModEq, four[2], 200, 16, "PEAK GAIN");
        knob(kEqMidQ, kModEq, four[3], 200, 16, "PEAK Q");

        knob(kExcFreq, kModExc, four[0], 106, 16, "FREQ");
        knob(kExcDrive, kModExc, four[1], 106, 16, "DRIVE");
        knob(kExcColour, kModExc, four[2], 106, 16, "COLOUR");
        knob(kExcMix, kModExc, four[3], 106, 16, "MIX");

        knob(kWideWidth, kModWide, three[0], 72, 16, "WIDTH");
        knob(kWideFreq, kModWide, three[1], 72, 16, "ABOVE");

        knob(kRevDecay, kModRev, three[0], 72, 16, "DECAY");
        knob(kRevDamping, kModRev, three[1], 72, 16, "DAMPING");
        knob(kRevMix, kModRev, three[2], 72, 16, "AMOUNT");

        knob(kLimGain, kModLim, four[0], 94, 16, "GAIN");
        knob(kLimCeiling, kModLim, four[1], 94, 16, "CEILING");
        knob(kLimAttack, kModLim, four[2], 94, 16, "ATTACK");
        knob(kLimRelease, kModLim, four[3], 94, 16, "RELEASE");

        for (int m = 0; m < kModCount; ++m)
            if (kModules[m].toggle != kParamCount)
                controls_.push_back({kModules[m].toggle, static_cast<Module>(m), 0, 15, 0, "", true});

        size_.open(skin_); // the window opens at the size it was left at
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
        if (std::strcmp(key, "link") == 0) {
            linked_ = value[0] == '1';
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
        if (open(kModLim)) {
            drawLimiterMode(rects_[kModLim]);
            drawLimiter(rects_[kModLim]);
        }
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

        if (open(kModWide)) drawCorrelation(rects_[kModWide]); // of the output, whether the widener is on or not: never dim

        presetBar_.drawMenu(*this, theme, list_.count(), preset_, list_.factoryCount(), canSavePreset(), [this](int i) { return list_.name(i); });
        skin_.drawMenu(*this, theme);
        if (!presetBar_.isOpen() && !skin_.isOpen() && drag_ < 0) help_.drawTip(*this, theme, width_, kHeight);
    }

    // -------------------------------------------------------- interaction

    bool onMouse(const MouseEvent& ev) override {
        if (ev.button != 1) return false;
        const float s = size_.zoom();
        const float x = static_cast<float>(ev.pos.getX()) / s, y = static_cast<float>(ev.pos.getY()) / s;

        if (!ev.press) {
            if (drag_ < 0) return false;
            uint32_t ids[3];
            const int count = group(controls_[drag_].id, ids);
            for (int i = 0; i < count; ++i) editParameter(ids[i], false);
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

        if (open(kModEq) && inModule(kModEq, kEqModeBox, x, y)) {
            const int mode = static_cast<int>((x - rects_[kModEq].x - kEqModeBox.x) / (kEqModeBox.w / 3.0f));
            change(kEqMode, static_cast<float>(mode < 0 ? 0 : (mode > 2 ? 2 : mode)), true);
            return true;
        }
        if (inModule(kModOutput, kDitherBox, x, y)) {
            const int mode = static_cast<int>((x - rects_[kModOutput].x - kDitherBox.x) / (kDitherBox.w / 3.0f));
            change(kDither, static_cast<float>(mode < 0 ? 0 : (mode > 2 ? 2 : mode)), true);
            return true;
        }
        if (inModule(kModOutput, kBypassBox, x, y) || inModule(kModOutput, kMatchBox, x, y)) {
            const uint32_t id = inModule(kModOutput, kBypassBox, x, y) ? kBypass : kMatch;
            change(id, on(id) ? 0.0f : 1.0f, true);
            return true;
        }
        if (open(kModComp))
            for (int b = 0; b < 3; ++b)
                for (int mute = 0; mute < 2; ++mute) {
                    if (!inModule(kModComp, bandKey(b, mute == 1), x, y)) continue;
                    const uint32_t id = kLowSolo + 2 * b + mute;
                    change(id, on(id) ? 0.0f : 1.0f, true);
                    return true;
                }
        if (open(kModLim) && inModule(kModLim, kLimModeBox, x, y)) {
            change(kLimSoft, x - rects_[kModLim].x - kLimModeBox.x < kLimModeBox.w * 0.5f ? 0.0f : 1.0f, true);
            return true;
        }
        if (open(kModComp) && inModule(kModComp, kLinkBox, x, y)) {
            linked_ = !linked_;
            setState("link", linked_ ? "1" : "0");
            repaint();
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
        uint32_t ids[3];
        const int count = group(c.id, ids);
        if (doubleClick || (ev.mod & kModifierControl)) {
            for (int i = 0; i < count; ++i) change(ids[i], kParams[ids[i]].def, true);
            return true;
        }
        drag_ = hit;
        dragY_ = y;
        dragNorm_ = dragStart_ = paramToNorm(c.id, shown(c.id));
        for (int i = 0; i < count; ++i) {
            linkStart_[i] = paramToNorm(ids[i], values_[ids[i]]);
            editParameter(ids[i], true);
        }
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
        const uint32_t id = controls_[drag_].id;
        dragNorm_ = std::max(0.0f, std::min(maxNorm(id), dragNorm_ + (dragY_ - y) / range));
        dragY_ = y;
        change(id, normToParam(id, dragNorm_), false);
        // Linked bands go the same way from where they stood when the drag began, each as far as its own
        // range lets it: what is pushed against an end comes back when the knob is turned back.
        uint32_t ids[3];
        const int count = group(id, ids);
        for (int i = 1; i < count; ++i) change(ids[i], normToParam(ids[i], linkStart_[i] + dragNorm_ - dragStart_), false);
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
        const int hit = controlAt(static_cast<float>(ev.pos.getX()) / s, static_cast<float>(ev.pos.getY()) / s);
        if (hit < 0 || controls_[hit].toggle) return false;
        const uint32_t id = controls_[hit].id;
        const float step = (ev.mod & kModifierShift) ? 0.004f : 0.02f;
        const float from = paramToNorm(id, shown(id));
        const float norm = std::max(0.0f, std::min(maxNorm(id), from + step * static_cast<float>(ev.delta.getY())));
        uint32_t ids[3];
        const int count = group(id, ids);
        change(id, normToParam(id, norm), true);
        for (int i = 1; i < count; ++i) change(ids[i], normToParam(ids[i], paramToNorm(ids[i], values_[ids[i]]) + norm - from), true);
        return true;
    }

private:
    void knob(uint32_t id, Module module, float dx, float dy, float r, const char* text) {
        controls_.push_back({id, module, dx, dy, r, text, false});
    }

    bool on(uint32_t id) const { return values_[id] > 0.5f; }

    /** How far a knob turns, 0..1: all the way, but the limiter's attack as a brickwall stops at its longest look-ahead. */
    float maxNorm(uint32_t id) const { return id == kLimAttack && !on(kLimSoft) ? paramToNorm(id, kBrickwallMaxAttack) : 1.0f; }
    /** The value a knob shows, which is the one in use: the attack of a brickwall is never longer than that. */
    float shown(uint32_t id) const { return id == kLimAttack && !on(kLimSoft) ? std::min(values_[id], kBrickwallMaxAttack) : values_[id]; }

    /** The parameters that move with `id`, itself first: the same control of the other two bands when the bands are linked. */
    int group(uint32_t id, uint32_t* ids) const {
        ids[0] = id;
        if (!linked_ || id < kLowThreshold || id > kHighMakeup) return 1;
        int count = 1;
        for (uint32_t other = kLowThreshold + (id - kLowThreshold) % 5; other <= kHighMakeup; other += 5)
            if (other != id) ids[count++] = other;
        return count;
    }
    /** A module works when it has no on/off switch or is switched on. */
    bool active(Module m) const { return kModules[m].toggle == kParamCount || (on(kModules[m].toggle) && !on(kBypass)); }
    /** The solo or mute key of a band, relative to the compressor. */
    static Rect bandKey(int band, bool mute) { return {band * kBandWidth + (mute ? kMuteX : kSoloX), kBandNameY - kKeyH * 0.5f, kKeyW, kKeyH}; }
    /** A band is heard unless it is muted, or another one is soloed and it is not. */
    bool heard(int band) const {
        const bool anySolo = on(kLowSolo) || on(kMidSolo) || on(kHighSolo);
        return !on(kLowMute + 2 * band) && (!anySolo || on(kLowSolo + 2 * band));
    }
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

        size_.design(width_, kHeight); // the window follows, at the size the user chose
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
        help_.add(skin_.buttonBox(), "LOOK AND SIZE", hnl::ui::SkinMenu::helpWithSizes());
        help_.add(presetBar_.nameBox(), "PRESETS", help::kPresets);
        const Rect& in = rects_[kModInput];
        help_.add({in.x + 16, in.y + 34, 56, 396}, "INPUT LEVEL", help::kInputMeter);
        const Rect& out = rects_[kModOutput];
        help_.add({out.x + 14, out.y + 34, 60, 380}, "OUTPUT LEVEL", help::kOutputMeter);
        help_.add({out.x + 84, out.y + 40, 96, 196}, "LOUDNESS", help::kLoudness);
        help_.add({out.x + 84, out.y + 246, 96, 64}, "TRUE PEAK", help::kTruePeak);
        help_.add({out.x + 84, out.y + 318, 96, 64}, "MATCH GAIN", help::kMatchGain);
        help_.add({out.x + kBypassBox.x, out.y + kBypassBox.y, kBypassBox.w, kBypassBox.h}, "BYPASS", help::forParam(kBypass));
        help_.add({out.x + kMatchBox.x, out.y + kMatchBox.y, kMatchBox.w, kMatchBox.h}, "MATCH LOUDNESS", help::forParam(kMatch));
        help_.add({out.x + 20, out.y + kDitherBox.y, 142, kDitherBox.h}, "DITHER", help::forParam(kDither));
        help_.add({out.x + kResetBox.x, out.y + kResetBox.y, kResetBox.w, kResetBox.h}, "RESET METERS", help::kReset);
        if (open(kModComp)) {
            const Rect& p = rects_[kModComp];
            help_.add({p.x + kSpecBox.x, p.y + kSpecBox.y, kSpecBox.w, kSpecBox.h}, "SPECTRUM", help::kSpectrum);
            for (int b = 0; b < 3; ++b) {
                help_.add({p.x + b * kBandWidth + 70, p.y + 430, 86, 90}, "GAIN REDUCTION", help::kGainReduction);
                const Rect s = bandKey(b, false), m = bandKey(b, true);
                help_.add({p.x + s.x, p.y + s.y, s.w, s.h}, "SOLO", help::forParam(kLowSolo));
                help_.add({p.x + m.x, p.y + m.y, m.w, m.h}, "MUTE", help::forParam(kLowMute));
            }
        }
        if (open(kModWide)) help_.add({rects_[kModWide].x + 136, rects_[kModWide].y + 34, 68, 76}, "CORRELATION", help::kCorrelation);
        if (open(kModComp)) help_.add({rects_[kModComp].x + kLinkBox.x, rects_[kModComp].y + kLinkBox.y, kLinkBox.w, kLinkBox.h}, "LINK BANDS", help::kLink);
        if (open(kModLim)) {
            const Rect& p = rects_[kModLim];
            help_.add({p.x + 8, p.y + 132, kColumnW - 16, 24}, "GAIN REDUCTION", help::kGainReduction);
            help_.add({p.x + kLimModeBox.x, p.y + kLimModeBox.y, kLimModeBox.w, kLimModeBox.h}, "LIMITER MODE", help::forParam(kLimSoft));
        }
        if (open(kModExc)) {
            const Rect& p = rects_[kModExc];
            help_.add({p.x + kModeBox.x, p.y + kModeBox.y, kModeBox.w, kModeBox.h}, "EXCITER MODE", help::forParam(kExcMode));
        }
        if (open(kModEq)) {
            const Rect& p = rects_[kModEq];
            help_.add({p.x + kOrderBox.x, p.y + kOrderBox.y, kOrderBox.w, kOrderBox.h}, "EQUALISER PLACE", help::forParam(kEqFirst));
            help_.add({p.x + kEqModeBox.x, p.y + kEqModeBox.y, kEqModeBox.w, kEqModeBox.h}, "EQUALISER WORKS ON", help::forParam(kEqMode));
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
            if (!presets::inPreset(id) || values_[id] == v) continue;
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
            if (presets::inPreset(id) && std::fabs(values_[id] - list_.value(preset_, id)) > tolerance) return true;
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
        window(p, 12, 30, 60, 404);
        levelMeter(p.x + 22, p.y + 40, 12, 384, values_[kMeterInL]);
        levelMeter(p.x + 38, p.y + 40, 12, 384, values_[kMeterInR]);
        levelScale(p.x + 54, p.y + 40, 384);
    }

    void drawCompressor(const Rect& p) {
        static const char* const kBandNames[3] = {"LOW", "MID", "HIGH"};
        static const char* const kLinkName[1] = {"LINK BANDS"}, *const kSolo[1] = {"S"}, *const kMute[1] = {"M"};
        drawSpectrum(p);
        drawChoice(p, kLinkBox, kLinkName, 1, linked_ ? 0 : -1, kAmber);
        for (int b = 0; b < 3; ++b) {
            const float x = p.x + b * kBandWidth;
            const hnl::ui::Skin k = skin();
            label(kBandNames[b], x + 52, p.y + kBandNameY, 12, rgb(k.solid ? k.plateText : kAmber, heard(b) ? 1.0f : 0.4f), ALIGN_CENTER | ALIGN_MIDDLE);
            drawChoice(p, bandKey(b, false), kSolo, 1, on(kLowSolo + 2 * b) ? 0 : -1, kAmber);
            drawChoice(p, bandKey(b, true), kMute, 1, on(kLowMute + 2 * b) ? 0 : -1, kRed);
            if (b > 0) hnl::ui::groove(*this, k, x, p.y + 184, x, p.y + 512);
            const float gr = values_[kMeterGrLow + b];
            window(p, b * kBandWidth + 132, 220, 18, 296);
            reductionMeter(x + 136, p.y + 224, 10, 288, gr, 20.0f, rgb(kModules[kModComp].accent), false);
            label("GR", x + 100, p.y + 448, 9, rgb(k.plateDim), ALIGN_CENTER | ALIGN_MIDDLE);
            char text[24];
            std::snprintf(text, sizeof(text), "%.1f dB", gr);
            label(text, x + 100, p.y + 466, 12, rgb(k.plateText), ALIGN_CENTER | ALIGN_MIDDLE);
        }
    }

    /**
     * The spectrum of what goes into the compressor, 20 Hz to 20 kHz over its width and 72 dB over its height,
     * with the two crossovers as lines. A band that is not heard (muted, or not the soloed one) is dark.
     */
    void drawSpectrum(const Rect& p) {
        const hnl::ui::Skin k = skin();
        const Rect box = {p.x + kSpecBox.x, p.y + kSpecBox.y, kSpecBox.w, kSpecBox.h};
        if (k.solid) {
            hnl::ui::displayPanel(*this, k, box);
        } else {
            beginPath();
            roundedRect(box.x, box.y, box.w, box.h, 4);
            fillColor(rgb(k.well));
            fill();
        }
        const float x0 = box.x + 4, w = box.w - 8, y0 = box.y + 5, h = box.h - 9, floor = y0 + h;
        const auto at = [&](float hz) { return x0 + w * std::log(hz / 20.0f) / std::log(1000.0f); };
        const uint32_t ink = k.solid ? k.yellow : kAmber;
        static const struct { float hz; const char* name; } kMarks[] = {{100, "100"}, {1000, "1k"}, {10000, "10k"}};
        for (const auto& m : kMarks) {
            beginPath();
            moveTo(at(m.hz), y0);
            lineTo(at(m.hz), floor);
            strokeColor(rgb(k.dim, 0.22f));
            strokeWidth(1);
            stroke();
            label(m.name, at(m.hz) - 3, y0 + 4, 7, rgb(k.dim), ALIGN_RIGHT | ALIGN_MIDDLE);
        }
        float ys[kSpectrumBands];
        for (int i = 0; i < kSpectrumBands; ++i) {
            const float t = (values_[kSpectrum + i] + 72.0f) / 72.0f;
            ys[i] = floor - h * (t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t));
        }
        for (int pass = 0; pass < 2; ++pass) { // the area under the line, then the line
            beginPath();
            moveTo(x0, pass == 0 ? floor : ys[0]);
            if (pass == 0) lineTo(x0, ys[0]);
            for (int i = 0; i < kSpectrumBands; ++i) lineTo(x0 + w * (i + 0.5f) / kSpectrumBands, ys[i]);
            lineTo(x0 + w, ys[kSpectrumBands - 1]);
            if (pass == 0) {
                lineTo(x0 + w, floor);
                closePath();
                fillColor(rgb(ink, 0.28f));
                fill();
            } else {
                strokeColor(rgb(ink));
                strokeWidth(1.5f);
                stroke();
            }
        }
        // The crossovers, where the compressor has them: the high one at least an octave above the low one.
        const float f1 = values_[kXoverLow], f2 = std::max(values_[kXoverHigh], 2.0f * f1);
        const float edges[4] = {x0, at(f1), std::min(at(f2), x0 + w), x0 + w};
        for (int b = 0; b < 3; ++b) {
            if (heard(b)) continue;
            beginPath();
            rect(edges[b], y0, edges[b + 1] - edges[b], h);
            fillColor(rgb(k.solid ? 0x000000 : k.well, 0.7f));
            fill();
        }
        for (int i = 1; i < 3; ++i) {
            beginPath();
            moveTo(edges[i], y0);
            lineTo(edges[i], floor);
            strokeColor(rgb(k.solid ? k.text : k.plateText, 0.75f));
            strokeWidth(1);
            stroke();
        }
    }

    /** How alike left and right are at the output: a bar from the middle, to the right when they agree, to the left (red) when they cancel in mono. */
    void drawCorrelation(const Rect& p) {
        const hnl::ui::Skin k = skin();
        const float v = std::max(-1.0f, std::min(1.0f, values_[kMeterCorr]));
        const float x = p.x + 142, y = p.y + 68, w = 54, h = 8, mid = x + w * 0.5f;
        label("CORRELATION", p.x + 169, p.y + 44, 8, rgb(k.plateDim), ALIGN_CENTER | ALIGN_MIDDLE);
        window(p, 138, 64, 62, 16);
        beginPath();
        rect(x, y, w, h);
        fillColor(rgb(k.solid ? k.track : k.well));
        fill();
        if (v != 0.0f) {
            beginPath();
            rect(v > 0.0f ? mid : mid + v * w * 0.5f, y, std::fabs(v) * w * 0.5f, h);
            fillColor(rgb(v > 0.0f ? k.green : k.red));
            fill();
        }
        hnl::ui::segments(*this, k, x, y, w, h);
        beginPath();
        rect(mid - 0.5f, y - 2, 1, h + 4);
        fillColor(rgb(k.solid ? k.text : k.plateDim));
        fill();
        char text[16];
        std::snprintf(text, sizeof(text), "%+.2f", v);
        label(text, p.x + 169, p.y + 101, 10, rgb(v < 0.0f ? k.red : k.plateText), ALIGN_CENTER | ALIGN_MIDDLE);
    }

    /** A row of choices in a box, the chosen one in the module's colour. */
    void drawChoice(const Rect& p, const Rect& box, const char* const* names, int count, int chosen, uint32_t accent) {
        hnl::ui::keys(*this, skin(), {p.x + box.x, p.y + box.y, box.w, box.h}, count, chosen, [&](int i) { return names[i]; }, accent, kDim);
    }

    void drawExciterMode(const Rect& p) {
        static const char* const kNames[3] = {"TAPE", "TUBE", "RETRO"}; // kExciterModes, as the panel writes them
        drawChoice(p, kModeBox, kNames, 3, static_cast<int>(std::lround(values_[kExcMode])), kTeal);
    }

    void drawLimiterMode(const Rect& p) {
        static const char* const kNames[2] = {"BRICKWALL", "SOFT"};
        drawChoice(p, kLimModeBox, kNames, 2, on(kLimSoft) ? 1 : 0, kRed);
    }

    void drawEqOrder(const Rect& p) {
        static const char* const kNames[2] = {"BEFORE COMP", "AFTER COMP"}, *const kOn[3] = {"STEREO", "MID", "SIDE"}; // kEqModes
        drawChoice(p, kOrderBox, kNames, 2, on(kEqFirst) ? 0 : 1, kViolet);
        drawChoice(p, kEqModeBox, kOn, 3, static_cast<int>(std::lround(values_[kEqMode])), kViolet);
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
        label("GR", p.x + 12, p.y + 144, 9, rgb(k.plateDim), ALIGN_LEFT | ALIGN_MIDDLE);
        window(p, 28, 136, 116, 16);
        reductionMeter(p.x + 32, p.y + 140, 108, 8, gr, 12.0f, rgb(k.red), true);
        char text[24];
        std::snprintf(text, sizeof(text), "%.1f dB", gr);
        label(text, p.x + p.w - 12, p.y + 144, 11, rgb(k.plateText), ALIGN_RIGHT | ALIGN_MIDDLE);
    }

    void drawOutput(const Rect& p) {
        const hnl::ui::Skin k = skin();
        static const char* const kBypassName[1] = {"BYPASS"}, *const kMatchName[1] = {"MATCH"}, *const kDitherNames[3] = {"OFF", "16", "24"};
        window(p, 10, 30, p.w - 20, 386);
        levelMeter(p.x + 20, p.y + 40, 14, 368, values_[kMeterOutL]);
        levelMeter(p.x + 38, p.y + 40, 14, 368, values_[kMeterOutR]);
        levelScale(p.x + 56, p.y + 40, 368);
        const float x = p.x + 132;
        readout("MOMENTARY", values_[kMeterLufsM], "LUFS", x, p.y + 50, 15, k.text);
        readout("SHORT-TERM", values_[kMeterLufsS], "LUFS", x, p.y + 116, 15, k.text);
        readout("INTEGRATED", values_[kMeterLufsI], "LUFS", x, p.y + 182, 20, k.text);
        readout("TRUE PEAK MAX", values_[kMeterTruePeak], "dBTP", x, p.y + 258, 15,
                values_[kMeterTruePeak] > values_[kLimCeiling] + 0.05f && on(kLimOn) && !on(kBypass) ? k.red : k.text);
        readout("MATCH GAIN", on(kMatch) ? values_[kMeterMatch] : -100.0f, "dB", x, p.y + 330, 15, on(kMatch) ? k.yellow : k.text);
        drawChoice(p, kBypassBox, kBypassName, 1, on(kBypass) ? 0 : -1, kRed);
        drawChoice(p, kMatchBox, kMatchName, 1, on(kMatch) ? 0 : -1, kAmber);
        label("DITHER", p.x + 20, p.y + kDitherBox.y + kDitherBox.h * 0.5f, 9, rgb(k.plateDim), ALIGN_LEFT | ALIGN_MIDDLE);
        drawChoice(p, kDitherBox, kDitherNames, 3, static_cast<int>(std::lround(values_[kDither])), kBlue);
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
        hnl::ui::knob(*this, k, x, y, c.r, paramToNorm(c.id, shown(c.id)), from, hnl::ui::printed(k, kModules[c.module].accent), 13);

        const bool small = c.r < 18;
        label(c.label, x, y - c.r - 12, small ? 8 : 9, rgb(k.plateDim), ALIGN_CENTER | ALIGN_MIDDLE);
        char text[32];
        formatValue(c.id, shown(c.id), text, sizeof(text));
        label(text, x, y + c.r + 13, small ? 10 : 11, rgb(k.plateText), ALIGN_CENTER | ALIGN_MIDDLE);
    }

    static void formatValue(uint32_t id, float v, char* out, size_t size) {
        if (id == kEqLowCut && lowCutIsOff(v)) {
            std::snprintf(out, size, "OFF");
            return;
        }
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
    bool linked_ = false;         // the compressor's bands move together; kept with the song as the state "link"
    float width_ = UI_BASE_WIDTH;
    int drag_ = -1, lastClick_ = -1;
    uint lastClickTime_ = 0;
    float dragY_ = 0.0f, dragNorm_ = 0.0f, dragStart_ = 0.0f;
    float linkStart_[3] = {}; // where the knobs of group() stood when the drag began

    int preset_ = 0;
    std::string presetName_ = "Default";
    hnl::ui::PresetBar presetBar_{UI_BASE_WIDTH - kPresetRight, kHeaderY};
    hnl::PresetList list_ = presets::list();
    std::string presetNote_; // shown in place of the name until the next click
    hnl::ui::HelpTips help_;
    hnl::ui::SkinMenu skin_;
    hnl::ui::WindowSize<MasterUI> size_{*this, "master", UI_BASE_WIDTH, UI_BASE_HEIGHT};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MasterUI)
};

UI* createUI() { return new MasterUI(); }

END_NAMESPACE_DISTRHO
