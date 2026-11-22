// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// HNL Patina: user interface (NanoVG). All coordinates are in a fixed
// UI_BASE_WIDTH x UI_BASE_HEIGHT design space, scaled by the host's UI scale.
//
// The knobs of the six modules show one of the two scenes, A or B (the edit switch). Around each
// of them are two arcs, the outer one scene A's value and the inner one scene B's, each in its
// scene's colour, and a short line in Morph's colour outside them where Morph has the knob now.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "DistrhoUI.hpp"

#include "../../common/ui/Skin.hpp"
#include "../../common/ui/Widgets.hpp"
#include "dsp/Platter.hpp"
#include "Help.hpp"
#include "Params.hpp"
#include "Presets.hpp"

START_NAMESPACE_DISTRHO

using DGL_NAMESPACE::Color;

namespace {

Color rgb(uint32_t hex, float alpha = 1.0f) {
    return Color(((hex >> 16) & 255) / 255.0f, ((hex >> 8) & 255) / 255.0f, (hex & 255) / 255.0f, alpha);
}

/** A colour part of the way from one to another. */
uint32_t between(uint32_t a, uint32_t b, float t) {
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    uint32_t out = 0;
    for (int shift = 16; shift >= 0; shift -= 8) {
        const float ca = static_cast<float>((a >> shift) & 255), cb = static_cast<float>((b >> shift) & 255);
        out |= static_cast<uint32_t>(std::lround(ca + t * (cb - ca))) << shift;
    }
    return out;
}

// Two colours: brass for scene A and verdigris for scene B, what a metal starts as and what it ages into.
const uint32_t kBrass = 0xd1a758, kVerdigris = 0x58b8a6;
const uint32_t kSceneColour[2] = {kBrass, kVerdigris};
const uint32_t kMorphColour = 0xb9a3ee; // Morph has a colour of its own: its dial, and the line it puts at every knob

/**
 * One of the three colours as it stands on the front plate. On the dark looks it is a light and
 * stays as it is. On bare metal a light colour is lost and a darkened one turns to mud, so there
 * each has a strong printed ink of its own: orange, teal and violet.
 */
uint32_t plate(const hnl::ui::Skin& k, uint32_t colour) {
    if (!k.scale) return colour;
    return colour == kBrass ? 0xd97a00 : (colour == kVerdigris ? 0x008f78 : (colour == kMorphColour ? 0x7436e8 : hnl::ui::printed(k, colour)));
}
const uint32_t kVinylBlack = 0x0f0e0d; // the record itself, in every look
const uint32_t kPlatter = 0x34312d, kMetal = 0xc4beb4, kMetalDark = 0x6b665f, kArm = 0x56524d; // the turntable, in every look

/** The classic look in this plugin's own colours; the other looks are the same for every plugin (common/ui/Skin.hpp). */
hnl::ui::Skin classic() {
    hnl::ui::Skin k = hnl::ui::classicSkin(0x1a1917, 0x22201e, 0x3a3632, 0x3b3733, 0x0f0e0d, 0xece7df, 0x9a938a, kBrass, 0x2b2825, 0x45403a);
    k.knobHi = k.knobLo = k.knobEdge = 0x2b2826;
    return k;
}
const hnl::ui::Skin kClassic = classic();

typedef hnl::ui::Box Rect;

const Rect kRecord = {12, 52, 484, 162}, kScenes = {504, 52, 304, 196};
const Rect kTrace = {224, 84, 256, 50};
const Rect kIdleKeys = {104, 222, 110, 22};
const Rect kEditKeys = {520, 84, 92, 24};
const Rect kCopy[3] = {{520, 116, 92, 20}, {520, 140, 92, 20}, {520, 164, 92, 20}};
const Rect kOutBox = {520, 194, 92, 42};
const float kRecordX = 96, kRecordY = 140, kRecordR = 58;

const float kStripY = 256, kStripH = 306, kStripW = 127, kStripStep = 133.8f;
inline float stripX(int module) { return 12 + module * kStripStep; }
inline Rect strip(int module) { return {stripX(module), kStripY, kStripW, kStripH}; }
inline Rect toggleBox(int module) { return {stripX(module) + 6, kStripY + 8, 34, 18}; }
inline Rect typeBox(int module) { return {stripX(module) + 10, kStripY + 30, kStripW - 20, 22}; }
inline Rect barBox(int module) { return {stripX(module) + 10, kStripY + 262, kStripW - 20, 4}; }
const Rect kMainsBox = {stripX(kNoise) + 82, kStripY + 8, 38, 17};
const Rect kModeKeys = {stripX(kNoise) + 10, kStripY + 272, kStripW - 20, 20};

/** A knob: one of the three for the whole plugin (module -1, `which` is its parameter), or one of a module's three. */
struct Knob { int module; uint32_t which; float x, y, r; const char* label; };
const int kGlobalKnobs = 3, kKnobCount = kGlobalKnobs + 3 * kModuleCount;
Knob knobSpot(int i) {
    static const Knob global[kGlobalKnobs] = {{-1, kMorph, 680, 146, 40, "MORPH"}, {-1, kMix, 770, 118, 15, "MIX"}, {-1, kOutput, 770, 196, 15, "OUTPUT"}};
    if (i < kGlobalKnobs) return global[i];
    const int module = (i - kGlobalKnobs) / 3, part = (i - kGlobalKnobs) % 3;
    const float x = stripX(module);
    if (part == 0) return {module, mAmount, x + kStripW * 0.5f, kStripY + 110, 27, "AMOUNT"};
    if (part == 1) return {module, mReact, x + 34, kStripY + 210, 16, "REACT"};
    return {module, mThird, x + kStripW - 34, kStripY + 210, 16, kThirdNames[module]};
}

const char* const kModuleCaps[kModuleCount] = {"DRIVE", "MOTION", "VINYL", "NOISE", "FILTER", "SPACE"};
const char* const kCaption[kModuleCount][2] = {
    {"louder playing is", "pushed harder"}, {"hits drag", "the platter"}, {"crackle hides under", "loud notes"},
    {"", ""}, {"loud notes open", "it, tails darken"}, {"the tail ducks", "while you play"}};

const float kPresetX = 524, kHeaderY = 14; // the preset bar ends 10 px before the help button
const float kHelpX = UI_BASE_WIDTH - 52, kSkinX = UI_BASE_WIDTH - 24; // help, and right of it the choice of look

const int kHistory = 128; // points of the pitch trace

std::string upper(const char* s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
    return out;
}

} // namespace

class PatinaUI : public UI {
public:
    PatinaUI() : UI(UI_BASE_WIDTH, UI_BASE_HEIGHT, false) {
        for (uint32_t i = 0; i < kParamCount; ++i) values_[i] = kParams[i].def;
        loadSharedResources();
        const double s = getScaleFactor();
        if (s != 1.0) setSize(static_cast<uint>(std::lround(UI_BASE_WIDTH * s)), static_cast<uint>(std::lround(UI_BASE_HEIGHT * s)));

        // Help: wide areas first, single controls on top.
        help_.setButton(kHelpX, kHeaderY + 12);
        skin_.setButton(kSkinX, kHeaderY + 12);
        help_.add(help_.buttonBox(), "HELP", help::kHelp);
        help_.add(skin_.buttonBox(), "LOOK", hnl::ui::SkinMenu::help());
        help_.add(presetBar_.nameBox(), "PRESETS", help::kPresets);
        help_.add(kRecord, "RECORD", help::kRecord);
        help_.add({kTrace.x - 8, 140, kTrace.w + 16, 70}, "LISTENER", help::kListener);
        help_.add({16, kIdleKeys.y - 2, 204, kIdleKeys.h + 4}, kParams[kIdle].name, help::forParam(kIdle));
        help_.add(kEditKeys, "SCENE BEING EDITED", help::kEdit);
        help_.add({kCopy[0].x, kCopy[0].y, kCopy[0].w, kCopy[2].y + kCopy[2].h - kCopy[0].y}, "COPY", help::kCopy);
        help_.add(kOutBox, "OUTPUT LEVEL", help::kOut);
        for (int m = 0; m < kModuleCount; ++m) {
            const uint32_t base = kModuleBase[m];
            help_.add(strip(m), kModuleCaps[m], help::forScene(base + mOn));
            help_.add(typeBox(m), kParams[kSceneA + base + mType].name + 2, help::forScene(base + mType));
            help_.add({barBox(m).x, barBox(m).y - 4, barBox(m).w, 12}, "REACT NOW", help::kReactBar);
        }
        help_.add(kMainsBox, kParams[kMains].name, help::forParam(kMains));
        help_.add(kModeKeys, kParams[kSceneA + sNoiseMode].name + 2, help::forScene(sNoiseMode));
        for (int i = 0; i < kKnobCount; ++i) {
            const Knob k = knobSpot(i);
            const Rect area = {k.x - k.r - 10, k.y - k.r - 20, 2 * k.r + 20, 2 * k.r + 42};
            if (k.module < 0) help_.add(area, kParams[k.which].name, help::forParam(k.which));
            else help_.add(area, kParams[kSceneA + kModuleBase[k.module] + k.which].name + 2, help::forScene(kModuleBase[k.module] + k.which));
        }
        history_.assign(kHistory, 0.0f);
        marks_.assign(kHistory, 0);
    }

protected:
    void parameterChanged(uint32_t index, float value) override {
        if (index >= kParamCount || values_[index] == value) return;
        values_[index] = value;
        repaint();
    }

    void stateChanged(const char* key, const char* value) override {
        if (std::strcmp(key, "edit") == 0) {
            edit_ = std::strcmp(value, "b") == 0 ? 1 : 0;
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
        // The pitch of the record, one point for each time the window is idle, and where a hit fell.
        history_[at_] = values_[kMeterCents];
        marks_[at_] = values_[kMeterHits] > 0.5f && !wasHit_ ? 1 : 0;
        wasHit_ = values_[kMeterHits] > 0.5f;
        at_ = (at_ + 1) % kHistory;
        if (recordTurns()) repaint();
    }

    // ------------------------------------------------------------ drawing

    void onNanoDisplay() override {
        const float s = static_cast<float>(getScaleFactor());
        scale(s, s);
        fontFace(NANOVG_DEJAVU_SANS_TTF);

        const hnl::ui::Skin k = skin();
        const hnl::ui::Theme theme = k.theme();
        hnl::ui::faceplate(*this, k, UI_BASE_WIDTH, UI_BASE_HEIGHT);

        hnl::ui::title(*this, "PATINA", rgb(k.plateText), rgb(k.plateDim));
        std::string presetText = presetName_;
        if (presetModified()) presetText += " *";
        if (!presetNote_.empty()) presetText = presetNote_;
        presetBar_.drawBar(*this, theme, presetText.c_str());
        help_.drawButton(*this, theme);
        skin_.drawButton(*this, theme);

        hnl::ui::displayPanel(*this, k, kRecord);
        drawRecord(k);
        drawTrace(k);
        drawListener(k);
        label("IN SILENCE", 24, kIdleKeys.y + kIdleKeys.h * 0.5f, 9, rgb(k.plateDim), ALIGN_LEFT | ALIGN_MIDDLE);
        hnl::ui::keys(*this, k, kIdleKeys, 2, choice(kIdle), [](int i) { return i == 0 ? "STOP" : "KEEP"; });
        label(choice(kIdle) == 0 ? "crackle and the bed stop 4 s after the music" : "crackle and the bed go on without the music", kIdleKeys.x + kIdleKeys.w + 12,
              kIdleKeys.y + kIdleKeys.h * 0.5f, 8, rgb(k.plateDim), ALIGN_LEFT | ALIGN_MIDDLE);

        hnl::ui::platePanel(*this, k, kScenes);
        drawScenes(k);
        for (int m = 0; m < kModuleCount; ++m) drawStrip(k, m);

        presetBar_.drawMenu(*this, theme, list_.count(), preset_, list_.factoryCount(), canSavePreset(), [this](int i) { return list_.name(i); });
        skin_.drawMenu(*this, theme);
        if (!presetBar_.isOpen() && !skin_.isOpen() && drag_ < 0) help_.drawTip(*this, theme, UI_BASE_WIDTH, UI_BASE_HEIGHT);
    }

    // -------------------------------------------------------- interaction

    bool onMouse(const MouseEvent& ev) override {
        if (ev.button != 1) return false;
        const float s = static_cast<float>(getScaleFactor());
        const float x = static_cast<float>(ev.pos.getX()) / s, y = static_cast<float>(ev.pos.getY()) / s;

        if (!ev.press) {
            if (drag_ < 0) return false;
            editParameter(knobId(drag_), false);
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

        if (kEditKeys.contains(x, y)) { // which scene the knobs show
            edit_ = x < kEditKeys.x + kEditKeys.w * 0.5f ? 0 : 1;
            setState("edit", edit_ == 0 ? "a" : "b");
            repaint();
            return true;
        }
        for (int i = 0; i < 3; ++i)
            if (kCopy[i].contains(x, y)) {
                copyScenes(i);
                return true;
            }
        if (kIdleKeys.contains(x, y)) {
            change(kIdle, x < kIdleKeys.x + kIdleKeys.w * 0.5f ? 0.0f : 1.0f, true);
            return true;
        }
        if (kMainsBox.contains(x, y)) {
            change(kMains, choice(kMains) == 0 ? 1.0f : 0.0f, true);
            return true;
        }
        if (kModeKeys.contains(x, y)) {
            change(sceneParam(edit_, sNoiseMode), x < kModeKeys.x + kModeKeys.w * 0.5f ? 0.0f : 1.0f, true);
            return true;
        }
        for (int m = 0; m < kModuleCount; ++m) {
            if (toggleBox(m).contains(x, y)) {
                const uint32_t id = moduleParam(edit_, m, mOn);
                change(id, values_[id] >= 0.5f ? 0.0f : 1.0f, true);
                return true;
            }
            const Rect t = typeBox(m);
            if (t.contains(x, y)) { // the left half steps back, the right half on, round and round
                const uint32_t id = moduleParam(edit_, m, mType);
                const int count = kModuleTypeCount[m], step = x < t.x + t.w * 0.5f ? count - 1 : 1;
                change(id, static_cast<float>((choice(id) + step) % count), true);
                return true;
            }
        }

        const int hit = knobAt(x, y);
        if (hit < 0) return false;
        const uint32_t id = knobId(hit);
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
        change(knobId(drag_), stepped(knobId(drag_), normToParam(knobId(drag_), dragNorm_)), false);
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
        if (presetBar_.isBusy() || skin_.isOpen() || drag_ >= 0) return false;
        const float s = static_cast<float>(getScaleFactor());
        const int hit = knobAt(static_cast<float>(ev.pos.getX()) / s, static_cast<float>(ev.pos.getY()) / s);
        if (hit < 0) return false;
        const uint32_t id = knobId(hit);
        const float dy = static_cast<float>(ev.delta.getY());
        if (kParams[id].kind == kChoice) { // a knob with steps: one step for each notch of the wheel
            change(id, normToParam(id, paramToNorm(id, values_[id] + (dy > 0 ? 1.0f : -1.0f))), true);
            return true;
        }
        const float step = (ev.mod & kModifierShift) ? 0.004f : 0.02f;
        change(id, normToParam(id, paramToNorm(id, values_[id]) + step * dy), true);
        return true;
    }

private:
    /** The parameter under a knob: for a module's knob, that of the scene being edited. */
    uint32_t knobId(int i) const {
        const Knob k = knobSpot(i);
        return k.module < 0 ? k.which : moduleParam(edit_, k.module, k.which);
    }
    int knobAt(float x, float y) const {
        for (int i = 0; i < kKnobCount; ++i) {
            const Knob k = knobSpot(i);
            if (std::hypot(x - k.x, y - k.y) <= k.r + 8) return i;
        }
        return -1;
    }
    /** A knob with steps (the record's speed) stands on a step. */
    static float stepped(uint32_t id, float value) { return kParams[id].kind == kChoice ? std::round(value) : value; }

    /** Applies a value locally and sends it to the host. */
    void change(uint32_t id, float value, bool gesture) {
        if (gesture) editParameter(id, true);
        values_[id] = value;
        setParameterValue(id, value);
        if (gesture) editParameter(id, false);
        repaint();
    }

    /** 0: A over B. 1: B over A. 2: exchange them. */
    void copyScenes(int how) {
        for (uint32_t s = 0; s < kSceneSize; ++s) {
            const float a = values_[kSceneA + s], b = values_[kSceneB + s];
            if (a == b) continue;
            if (how != 0) change(kSceneA + s, b, true);
            if (how != 1) change(kSceneB + s, a, true);
        }
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

    // ------------------------------------------------- what Morph makes of it

    float morph() const { return values_[kMorph] * 0.01f; }
    bool isOn(int scene, int m) const { return values_[moduleParam(scene, m, mOn)] >= 0.5f; }
    /** A module's Amount 0..1 as it is heard: off counts as nothing. */
    float amountNow(int m) const {
        const float a = isOn(0, m) ? values_[moduleParam(0, m, mAmount)] : 0.0f, b = isOn(1, m) ? values_[moduleParam(1, m, mAmount)] : 0.0f;
        return (a + morph() * (b - a)) * 0.01f;
    }
    /** The scene a module takes its type from: the one in which it is on, or the one Morph is nearer to. */
    int sceneNow(int m) const { return isOn(0, m) && !isOn(1, m) ? 0 : (isOn(1, m) && !isOn(0, m) ? 1 : (morph() < 0.5f ? 0 : 1)); }
    int typeNow(int m) const { return choice(moduleParam(sceneNow(m), m, mType)); }
    int speedNow() const { return choice(moduleParam(morph() < 0.5f ? 0 : 1, kMotion, mThird)); }
    bool recordTurns() const { return amountNow(kMotion) > 0.0f || amountNow(kVinyl) > 0.0f; }

    /** The look: in every one of them what is on or lit has this plugin's own colour. */
    hnl::ui::Skin skin() const { return skin_.skin() == hnl::ui::kSkinClassic ? kClassic : hnl::ui::solidSkin(skin_.skin(), kClassic.accent); }

    void label(const char* text, float x, float y, float size, const Color& colour, int align) {
        fontSize(size);
        textAlign(align);
        fillColor(colour);
        this->text(x, y, text, nullptr);
    }
    void line(float x0, float y0, float x1, float y1, float width, const Color& colour) {
        beginPath();
        moveTo(x0, y0);
        lineTo(x1, y1);
        strokeColor(colour);
        strokeWidth(width);
        lineCap(ROUND);
        stroke();
    }
    void disc(float x, float y, float r, const Color& colour) {
        beginPath();
        circle(x, y, r);
        fillColor(colour);
        fill();
    }
    void bar(const hnl::ui::Skin& k, float x, float y, float w, float h, float part, uint32_t colour) {
        part = part < 0 ? 0 : (part > 1 ? 1 : part);
        beginPath();
        rect(x, y, w, h);
        fillColor(rgb(k.solid ? k.track : k.well));
        fill();
        if (part > 0.0f) {
            beginPath();
            rect(x, y, w * part, h);
            fillColor(rgb(colour));
            fill();
        }
    }

    int choice(uint32_t id) const { return static_cast<int>(std::lround(values_[id])); }

    // ---------------------------------------------------------- the record

    void drawRecord(const hnl::ui::Skin& k) {
        const Rect& p = kRecord;
        const int speed = speedNow();
        const bool turns = recordTurns();
        const float motion = amountNow(kMotion), vinyl = amountNow(kVinyl);
        label("RECORD", p.x + 16, p.y + 16, 10, rgb(k.text), ALIGN_LEFT | ALIGN_MIDDLE);
        char text[96];
        if (!turns) std::snprintf(text, sizeof(text), "NO RECORD");
        else if (motion > 0.0f) std::snprintf(text, sizeof(text), "%s RPM \xC2\xB7 %s \xC2\xB1%.0f CENT", kSpeedNames[speed], upper(kMotionTypes[typeNow(kMotion)]).c_str(),
                                              motion * (typeNow(kMotion) == hnl::patina::Platter::kBelt ? 7.0f : 50.0f));
        else std::snprintf(text, sizeof(text), "%s RPM \xC2\xB7 RUNS TRUE", kSpeedNames[speed]);
        label(text, p.x + 74, p.y + 16, 8, rgb(k.dim), ALIGN_LEFT | ALIGN_MIDDLE);

        const float pi = 3.14159265f, turn = 2 * pi * values_[kMeterAngle];
        const float R = kRecordR;
        const int motionType = typeNow(kMotion), vinylType = typeNow(kVinyl);
        const bool shellac = turns && vinyl > 0.0f && vinylType == 3;

        // The platter: its shadow on the plinth, the mat, and the bright edge of the rim around it.
        disc(kRecordX + 1.5f, kRecordY + 2.5f, R + 5, rgb(0x000000, 0.45f));
        disc(kRecordX, kRecordY, R + 4.5f, rgb(0x161514));
        ring(kRecordX, kRecordY, R + 4.0f, 0.8f, rgb(kMetal, 0.8f));

        // An off-centre hole: the record swings around the spindle as it turns.
        const float off = turns && motionType == hnl::patina::Platter::kOffCentre ? 4.0f * motion : 0.0f;
        const float cx = kRecordX + off * std::cos(turn), cy = kRecordY + off * std::sin(turn);

        // The arm is one stiff piece that turns on its bearing: a tube, and a head set at an angle to it.
        // On the record the needle rides at one distance from the record's centre, so an off-centre
        // record swings the arm; with no record the arm lies on its rest, straight down.
        const float ax = kRecordX + 70, ay = kRecordY - 44;                 // the bearing
        const float tube = 76.0f, headTurn = 0.38f, toNeedle = 9.0f;        // the needle sits this far into the head
        const float px = tube + toNeedle * std::cos(headTurn), py = toNeedle * std::sin(headTurn); // the needle, seen along the tube
        const float reach = std::hypot(px, py), lead = std::atan2(py, px);
        float swing = 0.5f * pi; // the direction of the tube: straight down on the rest
        if (turns) {
            const float far = std::hypot(cx - ax, cy - ay), groove = 43.0f;
            const float open = std::acos(std::clamp((far * far + reach * reach - groove * groove) / (2 * far * reach), -1.0f, 1.0f));
            swing = std::atan2(cy - ay, cx - ax) - open - lead;
        }
        const float ux = std::cos(swing), uy = std::sin(swing), vx = -uy, vy = ux;                     // along the tube, and across it
        const float hx = std::cos(swing + headTurn), hy = std::sin(swing + headTurn), wx = -hy, wy = hx; // along the head, and across it
        const float ex = ax + tube * ux, ey = ay + tube * uy;                // where the tube meets the head
        const float nx = ex + toNeedle * hx, ny = ey + toNeedle * hy;
        const float needle = std::atan2(ny - cy, nx - cx); // where the needle sits, seen from the record's centre
        if (turns) {
            const float labelR = shellac ? 23.0f : 19.0f, grooveIn = labelR + 7.0f, grooveOut = R - 2.5f;
            disc(cx, cy, R, rgb(shellac ? 0x15100c : kVinylBlack));
            ring(cx, cy, R - 0.7f, 0.9f, rgb(0xffffff, 0.10f)); // the raised edge
            // The grooves: a band a little lighter than the bare vinyl of the edge and the run-out.
            band(cx, cy, grooveIn, grooveOut, 0.0f, 2 * pi, rgb(0xffffff, shellac ? 0.035f : 0.05f));
            for (float r = grooveIn + 1.2f; r < grooveOut; r += 2.3f) ring(cx, cy, r, 0.5f, rgb(0x000000, 0.35f));

            // The light on the grooves: two fans opposite each other that stay where the lamp is while the
            // record turns under them. A warped record throws them about; a worn one is dull.
            const float worn = vinyl > 0.0f ? (vinylType == 2 ? 0.75f : 0.35f) * vinyl : 0.0f;
            const float shine = (shellac ? 0.5f : 1.0f) * (1.0f - worn);
            const float sway = motionType == hnl::patina::Platter::kWarped ? 0.22f * motion : 0.0f;
            for (int side = 0; side < 2; ++side) {
                const float at = -0.80f + pi * side + sway * std::sin(2 * turn + 1.3f * side);
                const float wide = 0.34f * (1.0f + (side == 0 ? 1.0f : -1.0f) * 1.2f * sway * std::cos(2 * turn));
                for (int layer = 0; layer < 5; ++layer) {
                    const float half = wide * (1.0f - layer / 5.0f);
                    band(cx, cy, grooveIn, grooveOut, at - half, at + half, rgb(0xffffff, 0.05f * shine));
                }
            }
            if (worn > 0.0f && vinylType == 2) band(cx, cy, grooveIn + 4, grooveOut - 6, 0.0f, 2 * pi, rgb(0x8a8378, 0.10f * vinyl)); // played grey

            // The gaps between the songs cut through the light; a 78 has one song a side.
            if (!shellac) {
                static const float kGaps[4] = {0.19f, 0.43f, 0.60f, 0.83f};
                for (float g : kGaps) ring(cx, cy, grooveIn + g * (grooveOut - grooveIn), 1.1f, rgb(kVinylBlack));
            }
            ring(cx, cy, labelR + 3.2f, 0.5f, rgb(0xffffff, 0.08f)); // the run-out groove

            if (vinyl > 0.0f && vinylType == 0) { // dust, more of it with Amount, lying still on the record
                hnl::patina::Rng dust;
                dust.seed(0x51ed270bu);
                const int specks = static_cast<int>(10 + 70 * vinyl);
                for (int i = 0; i < specks; ++i) {
                    const float a = turn + 2 * pi * static_cast<float>(dust.unit());
                    const float r = grooveIn - 4 + (R - grooveIn + 2) * std::sqrt(static_cast<float>(dust.unit()));
                    const float size = 0.35f + 0.5f * static_cast<float>(dust.unit());
                    disc(cx + r * std::cos(a), cy + r * std::sin(a), size, rgb(0xd8d2c6, 0.30f + 0.35f * static_cast<float>(dust.unit())));
                }
            }
            if (vinyl > 0.0f && vinylType == 1) { // the scratches, where the engine has them: each passes the needle as it clicks
                const int count = hnl::patina::scratchesAt(vinyl);
                for (int i = 0; i < count; ++i) {
                    const float a = needle + turn - 2 * pi * static_cast<float>(hnl::patina::kScratchAngle[i]);
                    const float from = grooveIn + 5 * (i % 3), to = grooveOut - 1 - 5 * (i % 2), bend = 0.02f + 0.015f * (i % 4);
                    beginPath(); // a little bent, as a slip of the hand is
                    for (int step = 0; step <= 6; ++step) {
                        const float t = step / 6.0f, r = from + (to - from) * t, b = a + bend * t * t;
                        if (step == 0) moveTo(cx + r * std::cos(b), cy + r * std::sin(b));
                        else lineTo(cx + r * std::cos(b), cy + r * std::sin(b));
                    }
                    strokeColor(rgb(0xe9e4da, 0.6f));
                    strokeWidth(0.6f);
                    lineCap(ROUND);
                    stroke();
                }
            }

            // The label, in the colour of the scene: a printed rim, two lines of type that turn, and the hole.
            const uint32_t paper = between(kBrass, kVerdigris, morph()), ink = between(paper, 0x000000, 0.62f);
            disc(cx, cy, labelR, rgb(paper));
            ring(cx, cy, labelR - 1.6f, 0.7f, rgb(ink, 0.7f));
            band(cx, cy, 0.0f, labelR - 2.6f, turn + 0.25f * pi, turn + 0.75f * pi, rgb(ink, 0.16f)); // the darker half of the print
            band(cx, cy, 0.0f, labelR - 2.6f, turn + 1.25f * pi, turn + 1.75f * pi, rgb(ink, 0.16f));
            curve(cx, cy, labelR - 5.0f, turn - 1.15f, turn + 1.15f, 1.5f, rgb(ink, 0.8f)); // the name
            curve(cx, cy, labelR - 8.2f, turn - 0.75f, turn + 0.75f, 0.9f, rgb(ink, 0.6f));
            curve(cx, cy, labelR - 5.5f, turn + pi - 0.9f, turn + pi + 0.9f, 0.8f, rgb(ink, 0.6f)); // the small print
            curve(cx, cy, labelR - 7.6f, turn + pi - 0.6f, turn + pi + 0.6f, 0.8f, rgb(ink, 0.45f));
            ring(cx, cy, 5.2f, 0.6f, rgb(ink, 0.5f));
        }
        // The spindle stands still.
        disc(kRecordX, kRecordY, 2.3f, rgb(kMetalDark));
        disc(kRecordX - 0.6f, kRecordY - 0.6f, 1.0f, rgb(kMetal));

        // The arm's board with the dial for the side force, and the rest the arm lies on when it is not playing.
        disc(ax + 1.5f, ay + 2.5f, 13.5f, rgb(0x000000, 0.4f));
        disc(ax, ay, 13.0f, rgb(0x121110));
        ring(ax, ay, 12.6f, 0.6f, rgb(kMetal, 0.22f));
        disc(ax + 8.5f, ay + 1.5f, 2.6f, rgb(kMetalDark));
        disc(ax + 8.5f, ay + 1.5f, 1.2f, rgb(kMetal));
        thick(ax - 3.5f, ay + 22, ax + 5.5f, ay + 22, 3.2f, rgb(0x2a2725));
        thick(ax - 3.5f, ay + 21, ax + 5.5f, ay + 21, 0.6f, rgb(kMetal, 0.35f));

        const float sx = 2.0f, sy = 3.5f; // the shadow of what floats above the record
        thick(ax + sx, ay + sy, ex + sx, ey + sy, 2.8f, rgb(0x000000, 0.35f));
        quad(ex + sx - 3.4f * wx, ey + sy - 3.4f * wy, ex + sx + 3.4f * wx, ey + sy + 3.4f * wy, 13.0f * hx, 13.0f * hy, rgb(0x000000, 0.35f));
        // The weight behind the bearing, the tube with the light along it, the bearing itself.
        thick(ax - 7 * ux, ay - 7 * uy, ax - 19 * ux, ay - 19 * uy, 10.5f, rgb(kArm));
        thick(ax - 7 * ux, ay - 7 * uy, ax - 8.2f * ux, ay - 8.2f * uy, 10.5f, rgb(kMetal, 0.45f));
        thick(ax - 18 * ux, ay - 18 * uy, ax - 19 * ux, ay - 19 * uy, 10.5f, rgb(0x000000, 0.4f));
        thick(ax - 7 * ux, ay - 7 * uy, ex, ey, 2.8f, rgb(kArm));
        line(ax + 6 * ux - 0.6f * vx, ay + 6 * uy - 0.6f * vy, ex - 0.6f * vx, ey - 0.6f * vy, 0.6f, rgb(kMetal, 0.75f));
        disc(ax, ay, 6.0f, rgb(kArm));
        ring(ax, ay, 6.0f, 0.7f, rgb(kMetal, 0.6f));
        disc(ax, ay, 2.6f, rgb(0x121110));
        disc(ax - 0.5f, ay - 0.5f, 1.0f, rgb(kMetal, 0.8f));
        // The head, wider at its tip, with the pick-up under it and the finger lift on the outside.
        beginPath();
        moveTo(ex - 2.2f * wx, ey - 2.2f * wy);
        lineTo(ex + 2.2f * wx, ey + 2.2f * wy);
        lineTo(ex + 13 * hx + 3.6f * wx, ey + 13 * hy + 3.6f * wy);
        lineTo(ex + 13 * hx - 3.6f * wx, ey + 13 * hy - 3.6f * wy);
        closePath();
        fillColor(rgb(kArm));
        fill();
        quad(ex + 5.5f * hx - 2.7f * wx, ey + 5.5f * hy - 2.7f * wy, ex + 5.5f * hx + 2.7f * wx, ey + 5.5f * hy + 2.7f * wy, 6.5f * hx, 6.5f * hy, rgb(0x191716));
        thick(ex + 11.2f * hx - 2.7f * wx, ey + 11.2f * hy - 2.7f * wy, ex + 11.2f * hx + 2.7f * wx, ey + 11.2f * hy + 2.7f * wy, 0.8f, rgb(k.text, 0.85f));
        line(ex + 3 * hx - 2.6f * wx, ey + 3 * hy - 2.6f * wy, ex + 4.5f * hx - 8.0f * wx, ey + 4.5f * hy - 8.0f * wy, 1.1f, rgb(kMetal, 0.8f));
    }

    /** A circle drawn as a line. */
    void ring(float x, float y, float r, float width, const Color& colour) {
        beginPath();
        circle(x, y, r);
        strokeColor(colour);
        strokeWidth(width);
        stroke();
    }
    /** Part of a circle's line, from one angle to another. */
    void curve(float x, float y, float r, float from, float to, float width, const Color& colour) {
        beginPath();
        arc(x, y, r, from, to, CW);
        strokeColor(colour);
        strokeWidth(width);
        lineCap(BUTT);
        stroke();
    }
    /** A filled piece of a ring between two radii and two angles; with an inner radius of 0, a slice. */
    void band(float x, float y, float inner, float outer, float from, float to, const Color& colour) {
        const int steps = std::max(4, static_cast<int>((to - from) * 14.0f));
        beginPath();
        for (int i = 0; i <= steps; ++i) {
            const float a = from + (to - from) * i / steps;
            if (i == 0) moveTo(x + outer * std::cos(a), y + outer * std::sin(a));
            else lineTo(x + outer * std::cos(a), y + outer * std::sin(a));
        }
        for (int i = steps; i >= 0; --i) {
            const float a = from + (to - from) * i / steps;
            lineTo(x + inner * std::cos(a), y + inner * std::sin(a));
        }
        closePath();
        fillColor(colour);
        fill();
    }
    /** A line with square ends. */
    void thick(float x0, float y0, float x1, float y1, float width, const Color& colour) {
        beginPath();
        moveTo(x0, y0);
        lineTo(x1, y1);
        strokeColor(colour);
        strokeWidth(width);
        lineCap(BUTT);
        stroke();
    }
    /** A box that stands on the line from (x0, y0) to (x1, y1) and reaches (dx, dy) from it. */
    void quad(float x0, float y0, float x1, float y1, float dx, float dy, const Color& colour) {
        beginPath();
        moveTo(x0, y0);
        lineTo(x1, y1);
        lineTo(x1 + dx, y1 + dy);
        lineTo(x0 + dx, y0 + dy);
        closePath();
        fillColor(colour);
        fill();
    }

    void drawTrace(const hnl::ui::Skin& k) {
        const Rect& t = kTrace;
        label("SPEED", t.x, t.y - 8, 8, rgb(k.dim), ALIGN_LEFT | ALIGN_MIDDLE);
        label("CENT", t.x + t.w, t.y - 8, 8, rgb(k.dim), ALIGN_RIGHT | ALIGN_MIDDLE);
        beginPath();
        rect(t.x, t.y, t.w, t.h);
        fillColor(rgb(k.well));
        fill();
        const float mid = t.y + t.h * 0.5f, span = 60.0f; // +-60 cent from top to bottom
        line(t.x, mid, t.x + t.w, mid, 0.6f, rgb(k.track));
        label("+50", t.x - 4, mid - 50 / span * t.h * 0.5f, 7, rgb(k.dim), ALIGN_RIGHT | ALIGN_MIDDLE);
        label("0", t.x - 4, mid, 7, rgb(k.dim), ALIGN_RIGHT | ALIGN_MIDDLE);
        label("-50", t.x - 4, mid + 50 / span * t.h * 0.5f, 7, rgb(k.dim), ALIGN_RIGHT | ALIGN_MIDDLE);
        beginPath();
        for (int i = 0; i < kHistory; ++i) {
            const float v = history_[(at_ + i) % kHistory];
            const float x = t.x + t.w * i / (kHistory - 1), y = mid - std::max(-span, std::min(span, v)) / span * t.h * 0.5f;
            if (i == 0) moveTo(x, y);
            else lineTo(x, y);
        }
        strokeColor(rgb(between(kBrass, kVerdigris, morph())));
        strokeWidth(1.3f);
        stroke();
        for (int i = 0; i < kHistory; ++i) // where a hit fell
            if (marks_[(at_ + i) % kHistory]) {
                const float x = t.x + t.w * i / (kHistory - 1);
                hnl::ui::triangle(*this, x - 3, t.y + t.h - 1, x + 3, t.y + t.h - 1, x, t.y + t.h - 6, rgb(k.text));
            }
    }

    void drawListener(const hnl::ui::Skin& k) {
        const float x = kTrace.x, w = kTrace.w, y = 150;
        label("LISTENER", x, y, 8, rgb(k.dim), ALIGN_LEFT | ALIGN_MIDDLE);
        label("against the last seconds of music", x + w, y, 7, rgb(k.dim), ALIGN_RIGHT | ALIGN_MIDDLE);
        static const char* const kRows[3] = {"LEVEL", "HITS", "GAPS"};
        const float part[3] = {(values_[kMeterLevel] + 12.0f) / 24.0f, values_[kMeterHits], values_[kMeterGaps]};
        const uint32_t colour[3] = {k.green, k.text, k.dim};
        for (int row = 0; row < 3; ++row) {
            const float ry = y + 10 + row * 13;
            label(kRows[row], x, ry + 4, 7.5f, rgb(k.dim), ALIGN_LEFT | ALIGN_MIDDLE);
            bar(k, x + 40, ry, w - 40, 7, part[row], colour[row]);
            hnl::ui::segments(*this, k, x + 40, ry, w - 40, 7);
        }
    }

    // ---------------------------------------------------------- the scenes

    void drawScenes(const hnl::ui::Skin& k) {
        const Rect& p = kScenes;
        label("SCENES", p.x + 16, p.y + 16, 10, rgb(k.plateText), ALIGN_LEFT | ALIGN_MIDDLE);
        label(edit_ == 0 ? "EDITING A" : "EDITING B", p.x + p.w - 16, p.y + 16, 8, rgb(k.plateDim), ALIGN_RIGHT | ALIGN_MIDDLE);
        // What the rings around the knobs below mean.
        const float lx = p.x + 78, ly = p.y + 16;
        disc(lx, ly, 3, rgb(plate(k, kBrass)));
        label("A", lx + 6, ly, 8, rgb(k.plateDim), ALIGN_LEFT | ALIGN_MIDDLE);
        disc(lx + 24, ly, 3, rgb(plate(k, kVerdigris)));
        label("B", lx + 30, ly, 8, rgb(k.plateDim), ALIGN_LEFT | ALIGN_MIDDLE);
        line(lx + 48, ly - 4, lx + 48, ly + 4, 2.4f, rgb(plate(k, kMorphColour)));
        label("NOW", lx + 54, ly, 8, rgb(k.plateDim), ALIGN_LEFT | ALIGN_MIDDLE);
        hnl::ui::keys(*this, k, kEditKeys, 2, edit_, [](int i) { return i == 0 ? "A" : "B"; }, kSceneColour[edit_], k.text);
        static const char* const kCopyText[3] = {"A \xE2\x96\xB8 B", "B \xE2\x96\xB8 A", "SWAP"};
        for (int i = 0; i < 3; ++i) hnl::ui::button(*this, k, kCopy[i], kCopyText[i]);

        // The level coming out, in a display of its own.
        hnl::ui::displayPanel(*this, k, kOutBox);
        label("OUT", kOutBox.x + 8, kOutBox.y + 21, 7.5f, rgb(k.dim), ALIGN_LEFT | ALIGN_MIDDLE);
        for (int c = 0; c < 2; ++c) {
            const float db = values_[kMeterOutL + c], part = (db + 48.0f) / 54.0f, y = kOutBox.y + 12 + c * 12;
            const float bx = kOutBox.x + 32, bw = kOutBox.w - 40;
            bar(k, bx, y, bw, 6, part, db > -0.05f ? k.red : (db > -12.0f ? k.yellow : k.green));
            hnl::ui::segments(*this, k, bx, y, bw, 6);
        }

        for (int i = 0; i < kGlobalKnobs; ++i) drawGlobalKnob(k, knobSpot(i));
        const Knob m = knobSpot(0);
        label("A", m.x - 44, m.y + 50, 10, rgb(plate(k, kBrass)), ALIGN_CENTER | ALIGN_MIDDLE);
        label("B", m.x + 44, m.y + 50, 10, rgb(plate(k, kVerdigris)), ALIGN_CENTER | ALIGN_MIDDLE);
        // The knobs change little that can be heard while the other scene has most of the morph.
        const float share = edit_ == 0 ? 1.0f - morph() : morph();
        if (share < 0.25f)
            label(edit_ == 0 ? "you hear mostly B" : "you hear mostly A", m.x, m.y + 82, 8, rgb(plate(k, kSceneColour[1 - edit_])), ALIGN_CENTER | ALIGN_MIDDLE);
    }

    void drawGlobalKnob(const hnl::ui::Skin& k, const Knob& spot) {
        const uint32_t id = spot.which;
        const float norm = paramToNorm(id, values_[id]);
        const bool big = id == kMorph;
        hnl::ui::knob(*this, k, spot.x, spot.y, spot.r, norm, id == kOutput ? paramToNorm(kOutput, 0.0f) : 0.0f, big ? plate(k, kMorphColour) : k.ring, big ? 22 : 9);
        if (big && k.solid) ringArc(spot.x, spot.y, spot.r, 0.0f, norm, 3.0f, rgb(plate(k, kMorphColour))); // the shared knob's ring of light is thin
        label(spot.label, spot.x, spot.y - spot.r - (big ? 12 : 11), big ? 9 : 8, rgb(k.plateDim), ALIGN_CENTER | ALIGN_MIDDLE);
        char text[32];
        if (id == kOutput) std::snprintf(text, sizeof(text), "%+.1f dB", values_[id]);
        else std::snprintf(text, sizeof(text), "%.0f %%", values_[id]);
        label(text, spot.x, spot.y + spot.r + (big ? 26 : 13), big ? 13 : 9.5f, rgb(k.plateText), ALIGN_CENTER | ALIGN_MIDDLE);
    }

    // ---------------------------------------------------------- a module

    void drawStrip(const hnl::ui::Skin& k, int m) {
        const float x = stripX(m), mid = x + kStripW * 0.5f;
        const bool on = isOn(edit_, m);
        const uint32_t colour = kSceneColour[edit_];
        hnl::ui::platePanel(*this, k, strip(m));
        if (m > 0) label("\xE2\x80\xBA", x - 3.4f, kStripY + 16, 12, rgb(k.plateDim), ALIGN_CENTER | ALIGN_MIDDLE); // the sound goes on to the right
        hnl::ui::toggle(*this, k, x + 23, kStripY + 17, on, colour);
        label(kModuleCaps[m], x + 44, kStripY + 17, 10, rgb(on ? k.plateText : k.plateDim), ALIGN_LEFT | ALIGN_MIDDLE);
        if (m == kNoise) hnl::ui::button(*this, k, kMainsBox, kMainsNames[choice(kMains)]);

        // The type, in a window of its own, with a step back and a step on.
        const Rect t = typeBox(m);
        hnl::ui::displayPanel(*this, k, t);
        const float ty = t.y + t.h * 0.5f;
        hnl::ui::triangle(*this, t.x + 12, ty - 4, t.x + 12, ty + 4, t.x + 6, ty, rgb(k.dim));
        hnl::ui::triangle(*this, t.x + t.w - 12, ty - 4, t.x + t.w - 12, ty + 4, t.x + t.w - 6, ty, rgb(k.dim));
        label(kModuleTypes[m][choice(moduleParam(edit_, m, mType))], mid, ty, 9.5f, rgb(on ? k.text : k.dim), ALIGN_CENTER | ALIGN_MIDDLE);

        for (int part = 0; part < 3; ++part) drawModuleKnob(k, knobSpot(kGlobalKnobs + 3 * m + part), on);

        // What React is doing to the module right now.
        const Rect b = barBox(m);
        bar(k, b.x, b.y, b.w, b.h, values_[kMeterReact + m], k.solid ? k.accent : k.text);

        if (m == kNoise) {
            hnl::ui::keys(*this, k, kModeKeys, 2, choice(sceneParam(edit_, sNoiseMode)), [](int i) { return i == 0 ? "FOLLOW" : "DUCK"; }, colour, k.text);
        } else {
            label(kCaption[m][0], mid, kStripY + 279, 7.5f, rgb(k.plateDim), ALIGN_CENTER | ALIGN_MIDDLE);
            label(kCaption[m][1], mid, kStripY + 290, 7.5f, rgb(k.plateDim), ALIGN_CENTER | ALIGN_MIDDLE);
        }
    }

    /** An arc of a knob's ring, from one position 0..1 to another. */
    void ringArc(float x, float y, float r, float from, float to, float width, const Color& colour) {
        const float pi = 3.14159265f, a0 = 0.75f * pi, sweep = 1.5f * pi;
        if (to < from) std::swap(from, to);
        if (to - from < 0.002f) return;
        beginPath();
        arc(x, y, r, a0 + from * sweep, a0 + to * sweep, CW);
        strokeColor(colour);
        strokeWidth(width);
        lineCap(BUTT);
        stroke();
    }

    /**
     * A module's knob. The pointer is the scene being edited. Around it two arcs, always in the same
     * place: the outer one is the knob in scene A, the inner one in scene B, each in its scene's
     * colour, the one being edited at full strength. The line outside them is where Morph has the knob now.
     */
    void drawModuleKnob(const hnl::ui::Skin& k, const Knob& spot, bool on) {
        const int m = spot.module;
        const uint32_t id = moduleParam(edit_, m, spot.which);
        const bool twoWay = m == kDrive && spot.which == mThird; // Tone goes both ways from the middle
        const bool steps = kParams[id].kind == kChoice;
        const bool isAmount = spot.which == mAmount;
        const float norm = paramToNorm(id, values_[id]), from = twoWay ? 0.5f : 0.0f;
        const float pi = 3.14159265f, a0 = 0.75f * pi, sweep = 1.5f * pi;
        const bool large = spot.r > 20;

        // The knob itself, with no arc of its own: the two scenes' arcs are drawn over its ring.
        hnl::ui::knob(*this, k, spot.x, spot.y, spot.r, norm, norm, k.ring, large ? 17 : 9);
        const float radius[2] = {spot.r + 1.0f, spot.r - 2.0f}, width = 2.4f;
        float at[2];
        for (int scene = 0; scene < 2; ++scene) {
            const uint32_t sid = moduleParam(scene, m, spot.which);
            const bool sceneOn = isOn(scene, m);
            at[scene] = isAmount && !sceneOn ? from : paramToNorm(sid, values_[sid]); // off counts as nothing
            ringArc(spot.x, spot.y, radius[scene], 0.0f, 1.0f, width, rgb(k.solid ? k.groove : k.track, k.scale ? 0.16f : (k.solid ? 0.5f : 1.0f)));
            // The scene that is not being edited is fainter; on bare metal less so, where faint ink is soon gone.
            const float strength = (scene == edit_ ? 1.0f : (k.scale ? 0.75f : 0.55f)) * (sceneOn ? 1.0f : 0.45f);
            ringArc(spot.x, spot.y, radius[scene], from, at[scene], width, rgb(plate(k, kSceneColour[scene]), strength));
        }

        // Where Morph has the knob now, in Morph's colour. A knob with steps is on the step of the scene Morph is nearer to.
        if (isOn(0, m) || isOn(1, m)) {
            float a = at[0], b = at[1];
            if (!isAmount) { // everything but Amount comes from the scene in which the module is on
                if (!isOn(0, m)) a = b;
                if (!isOn(1, m)) b = a;
            }
            const float now = steps ? (sceneNow(m) == 0 ? a : b) : a + morph() * (b - a);
            // A short line just outside the two arcs, so it covers neither.
            const float angle = a0 + now * sweep, r0 = spot.r + 3.5f, r1 = spot.r + (large ? 9.0f : 7.5f);
            line(spot.x + r0 * std::cos(angle), spot.y + r0 * std::sin(angle), spot.x + r1 * std::cos(angle), spot.y + r1 * std::sin(angle), 2.4f, rgb(plate(k, kMorphColour)));
        }

        label(upper(spot.label).c_str(), spot.x, spot.y - spot.r - (large ? 14 : 12), large ? 8 : 7.5f, rgb(k.plateDim), ALIGN_CENTER | ALIGN_MIDDLE);
        char text[32];
        const float v = values_[id];
        if (isAmount && !on) std::snprintf(text, sizeof(text), "Off");
        else if (steps) std::snprintf(text, sizeof(text), "%s", kParams[id].choices[choice(id)]);
        else if (twoWay) std::snprintf(text, sizeof(text), std::fabs(v) < 0.5f ? "0 %%" : "%+.0f %%", v);
        else if (m == kSpace && spot.which == mThird) std::snprintf(text, sizeof(text), "%.1f s", v);
        else std::snprintf(text, sizeof(text), "%.0f %%", v);
        label(text, spot.x, spot.y + spot.r + (large ? 21 : 16), large ? 11 : 9.5f, rgb(on ? k.plateText : k.plateDim), ALIGN_CENTER | ALIGN_MIDDLE);
    }

    float values_[kParamCount];
    int edit_ = 0; // the scene the knobs show: 0 = A, 1 = B
    int drag_ = -1, lastClick_ = -1;
    uint lastClickTime_ = 0;
    float dragY_ = 0.0f, dragNorm_ = 0.0f;

    std::vector<float> history_; // the record's pitch, oldest at at_
    std::vector<char> marks_;
    int at_ = 0;
    bool wasHit_ = false;

    int preset_ = 0;
    std::string presetName_ = "Default";
    hnl::ui::PresetBar presetBar_{kPresetX, kHeaderY};
    hnl::PresetList list_ = presets::list();
    std::string presetNote_; // shown in place of the name until the next click
    hnl::ui::HelpTips help_;
    hnl::ui::SkinMenu skin_;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PatinaUI)
};

UI* createUI() { return new PatinaUI(); }

END_NAMESPACE_DISTRHO
